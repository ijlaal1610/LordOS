/* netperm.c — lord-netperm: the root service that enforces per-app network permissions in the
 * kernel (docs/FIREWALL.md, step 2). Closed and minimal:
 *
 *   - no arguments, executes nothing, no network of its own (it creates no AF_INET/AF_INET6
 *     sockets: the systemd service also forbids it with RestrictAddressFamilies);
 *   - loads the BPF programs it carries inside (np.bpf.c → build/gen/np_bpf.h) into the cgroup of
 *     lordos.service, which contains the subtrees of all sessions: the network ones and, with the
 *     same per-app policy, the device program ("no camera", "no USB") and the Bluetooth socket
 *     program ("no Bluetooth"; it needs the kernel's function list, /sys/kernel/btf/vmlinux);
 *   - reads the rules of each user with a session from ~/.config/lordos/firewall.json (fixed,
 *     validated path) and compiles them into maps (npcompile.c);
 *   - learns names only from the system resolver (npdns.c);
 *   - tells each lord-server about its apps' new connections over /run/lordos/netperm.sock
 *     (npproto.h). The only thing it accepts there is "reload my rules".
 *
 * If the kernel has no cgroup BPF (or CAP_BPF is missing), it says so in the log and carries on
 * doing nothing: the desktop doesn't notice. With the service stopped, the programs detach on
 * their own (bpf_link): everything is allowed. */
#include "netperm/bpfsys.h"
#include "netperm/npcompile.h"
#include "netperm/npdns.h"
#include "netperm/npload.h"
#include "netperm/npproto.h"
#include "fw/fwmap.h"
#include "errcode.h"
#include "sec/json.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/bpf.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifdef NP_HAVE_BPF
#include "np_bpf.h"
#endif

#define CG_ROOT      "/sys/fs/cgroup"
#define CG_SERVICE   "system.slice/lordos.service"
#define CG_USER      "user.slice"       /* user.slice/user-<uid>.slice/user@<uid>.service */
#define SVC_LEVEL    2                  /* system.slice/lordos.service */
#define SCOPE_LEVEL  (SVC_LEVEL + 2)    /* lordos-session-<uid>/<scope> */
#define USER_LEVEL   3                  /* user.slice/user-<uid>.slice/user@<uid>.service */
#define NAMES_APP    "lordos"           /* dnsmap key for what the resolver says */
#define MAX_SESS     NP_MAX_SESS
#define MAX_CLIENTS  64
#define CLIENT_OUT   (256u << 10)

typedef struct {
    char name[256];
    char app[FW_APP_MAX];
    uint64_t cgid;
    uint32_t idx;
    unsigned gen;                       /* generation of its installed nodes */
    int has_domains, seen;
    NpApp app_v;                        /* the last thing written to np_apps */
    uint64_t allowed, denied;           /* kernel counters */
} Scope;

typedef struct {
    unsigned uid;
    char dir[64];
    int wd, seen;
    FwStore *store;
    Scope *sc;
    int nsc, cap;
    int dirty_counts;
    uint64_t user_cg;                   /* id of user@<uid>.service, or 0 if it isn't there (nor attached) */
    uint32_t snd_tgid[2];               /* its PipeWire and WirePlumber in np_sndsvc (0 = none) */
    int ulinks[16], nulinks;            /* the programs attached to user@<uid>.service */
} Session;

typedef struct {
    int fd;
    unsigned uid;
    unsigned char in[NP_IN_MAX + 8];
    size_t inlen;
    char *out;
    size_t outlen;
} Client;

typedef struct {
    uint32_t idx;
    unsigned gen;
    NpList list;
} Installed;

static struct {
    int bpf_ok;
    char bpf_err[200];                  /* a code line (errcode.h): the firewall page shows it translated */
    int m_np_cfg, m_np_apps, m_np_union, m_np_lpm4, m_np_lpm6, m_np_seen, m_np_counts, m_np_events, m_np_snd, m_np_sndsvc;
    uint32_t snd[NP_MAX_SND];           /* the ALSA minors in np_snd */
    int nsnd;
    int dev_ok, bt_ok;                  /* the device / Bluetooth programs loaded */
    int links[16], nlinks;
    int progs[16], nprogs;
    uint32_t attach[16];
    uint64_t attached_cg;               /* id of the attached lordos.service cgroup */
    int sys_wd;
    BpfRing ring;
    int cgfd;
    int ep, lfd, ifd, tfd, root_wd, dns_fd;
    Session sess[MAX_SESS];
    int nsess;
    Client cl[MAX_CLIENTS];
    int ncl;
    FwMap idxmap;                       /* "uid:app" → idx */
    uint32_t next_idx;
    FwMap installed;                    /* node key (idx, fam, prefix, address) → Installed */
    DnsMap *names;
    NpDns *dns;
    FwStore *empty;
    long long tick;
} G;

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("lord-netperm: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static long long mono(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

/* ---- reading a user's rules ----------------------------------------------------------------- */

/* ~/.config/lordos/firewall.json without following links: every folder and the file must belong
 * to the user, the file must be regular and at most FW_FILE_MAX. The home folder comes from /etc/passwd. */
static char *read_user_rules(unsigned uid, size_t *len, char *why, size_t wn) {
    struct passwd *pw = getpwuid(uid);
    if (!pw || pw->pw_dir[0] != '/') { snprintf(why, wn, "user without a home folder"); return NULL; }
    int fd = open(pw->pw_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    static const char *const parts[] = { ".config", "lordos", "firewall.json" };
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || st.st_uid != uid) { snprintf(why, wn, "odd home folder"); if (fd >= 0) close(fd); return NULL; }
    for (int i = 0; i < 3; i++) {
        int last = i == 2;
        int nfd = openat(fd, parts[i], O_RDONLY | O_NOFOLLOW | O_CLOEXEC | (last ? O_NONBLOCK : O_DIRECTORY));
        close(fd);
        if (nfd < 0) { snprintf(why, wn, errno == ENOENT ? "no rules" : "%s: %s", parts[i], strerror(errno)); return NULL; }
        fd = nfd;
        if (fstat(fd, &st) || st.st_uid != uid || (last && (!S_ISREG(st.st_mode) || st.st_size > (off_t)FW_FILE_MAX))) {
            snprintf(why, wn, "%s is not the user's or not a regular file", parts[i]);
            close(fd);
            return NULL;
        }
    }
    char *buf = malloc((size_t)st.st_size + 1);
    size_t got = 0;
    while (buf && got < (size_t)st.st_size) {
        ssize_t n = read(fd, buf + got, (size_t)st.st_size - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    if (!buf || got != (size_t)st.st_size) { free(buf); snprintf(why, wn, "could not read"); return NULL; }
    buf[got] = 0;
    *len = got;
    return buf;
}

static void load_rules(Session *s) {
    size_t len = 0;
    char why[160] = "", err[200] = "";
    char *text = read_user_rules(s->uid, &len, why, sizeof why);
    FwStore *n = fw_store_new();
    if (!n) { free(text); return; }
    if (text && fw_from_json(n, text, len, err, sizeof err)) {
        logf_("uid %u: invalid firewall.json (%s); the previous rules stay in use", s->uid, err);
        fw_store_free(n);
        free(text);
        return;
    }
    if (!text && strcmp(why, "no rules")) logf_("uid %u: %s", s->uid, why);
    free(text);
    fw_store_free(s->store);
    s->store = n;
    logf_("uid %u: %zu rules, mode %d", s->uid, fw_rule_count(n), (int)fw_mode(n));
}

/* ---- maps ------------------------------------------------------------------------------------ */

#ifdef NP_HAVE_BPF                     /* the program loader's relocations (bpf_setup) */
static int map_fd(const char *name) {
    if (!strcmp(name, "np_cfg")) return G.m_np_cfg;
    if (!strcmp(name, "np_apps")) return G.m_np_apps;
    if (!strcmp(name, "np_union")) return G.m_np_union;
    if (!strcmp(name, "np_lpm4")) return G.m_np_lpm4;
    if (!strcmp(name, "np_lpm6")) return G.m_np_lpm6;
    if (!strcmp(name, "np_seen")) return G.m_np_seen;
    if (!strcmp(name, "np_counts")) return G.m_np_counts;
    if (!strcmp(name, "np_events")) return G.m_np_events;
    if (!strcmp(name, "np_snd")) return G.m_np_snd;
    if (!strcmp(name, "np_sndsvc")) return G.m_np_sndsvc;
    return -1;
}
#endif

static uint32_t idx_of(unsigned uid, const char *app) {
    char key[FW_APP_MAX + 16];
    int kl = snprintf(key, sizeof key, "%u:%s", uid, app);
    void **slot = fwmap_slot(&G.idxmap, key, (size_t)kl, 1);
    if (!slot) return 0;
    if (!*slot) *slot = (void *)(uintptr_t)++G.next_idx;
    return (uint32_t)(uintptr_t)*slot;
}

static size_t inst_key(uint32_t idx, const NpNode *n, unsigned char *k) {
    memcpy(k, &idx, 4);
    k[4] = (unsigned char)n->fam;
    k[5] = (unsigned char)n->prefix;
    memcpy(k + 6, n->addr, 16);
    return 22;
}

static int lpm_put(uint32_t idx, const NpNode *n) {
    if (n->fam == 4) { NpLpm4Key k; np_key4(idx, n, &k); return bpfs_map_update(G.m_np_lpm4, &k, &n->list, BPF_ANY); }
    NpLpm6Key k; np_key6(idx, n, &k); return bpfs_map_update(G.m_np_lpm6, &k, &n->list, BPF_ANY);
}

static void lpm_del(uint32_t idx, const unsigned char *ik) {
    NpNode n;
    memset(&n, 0, sizeof n);
    n.fam = ik[4];
    n.prefix = ik[5];
    memcpy(n.addr, ik + 6, 16);
    if (n.fam == 4) { NpLpm4Key k; np_key4(idx, &n, &k); bpfs_map_delete(G.m_np_lpm4, &k); }
    else { NpLpm6Key k; np_key6(idx, &n, &k); bpfs_map_delete(G.m_np_lpm6, &k); }
}

/* Installs a node if it changed. 0 ok. */
static int install(uint32_t idx, unsigned gen, const NpNode *n) {
    unsigned char k[22];
    size_t kl = inst_key(idx, n, k);
    void **slot = fwmap_slot(&G.installed, k, kl, 1);
    if (!slot) return -1;
    Installed *in = *slot;
    if (in && in->list.n == n->list.n && !memcmp(in->list.e, n->list.e, n->list.n * sizeof(NpEnt))) { in->gen = gen; return 0; }
    if (lpm_put(idx, n)) { logf_("a trie entry didn't fit: %s", strerror(errno)); return -1; }
    if (!in && !(in = *slot = malloc(sizeof *in))) return -1;
    in->idx = idx;
    in->gen = gen;
    in->list = n->list;
    return 0;
}

/* Deletes idx's nodes that aren't from generation gen (gen 0: all). */
static void prune(uint32_t idx, unsigned gen) {
    FWMAP_EACH(&G.installed, e) {
        Installed *in = e->val;
        if (in->idx != idx || (gen && in->gen == gen)) continue;
        lpm_del(idx, e->key);
        free(fwmap_take(&G.installed, e->key, e->klen));
    }
}

static FwStore *store_of(Session *s) { return s->store ? s->store : G.empty; }

static void sync_scope(Session *s, Scope *sc) {
    if (!G.bpf_ok) return;
    NpCompiled c;
    if (np_compile_app(store_of(s), sc->app, sc->idx, G.names, NAMES_APP, mono(), &c)) { logf_("out of memory compiling %s", sc->app); return; }
    if (c.truncated) logf_("uid %u %s: %d rules didn't fit in the maps (at most %d per network)", s->uid, sc->app, c.truncated, NP_LIST_MAX);
    unsigned gen = ++sc->gen ? sc->gen : ++sc->gen;
    for (int i = 0; i < c.nnodes; i++) install(sc->idx, gen, &c.nodes[i]);
    prune(sc->idx, gen);
    sc->has_domains = c.has_domains;
    sc->app_v = c.app;
    if (bpfs_map_update(G.m_np_apps, &sc->cgid, &c.app, BPF_ANY)) logf_("np_apps: %s", strerror(errno));
    np_compiled_free(&c);
}

static void drop_scope(Scope *sc) {
    if (!G.bpf_ok) return;
    bpfs_map_delete(G.m_np_apps, &sc->cgid);
    bpfs_map_delete(G.m_np_counts, &sc->cgid);
    prune(sc->idx, 0);
}

/* ---- user@<uid>.service: the systemd-run escape ------------------------------------------- */

/* An app without a sandbox, running as the user, can ask its systemd manager
 * (systemd-run --user, scope or service) to move a process into user@<uid>.service, outside
 * lordos.service, where the apps' scopes don't reach. To close that, the same programs are also
 * attached to each session's user@<uid>.service and, when the process isn't in any app's scope,
 * it falls into np_union[user@]: the strict union (OR of the NP_R_* restrictions) of the user's
 * restricted apps. That way an escaped process is never less restricted than its own policy; it
 * grants nothing. If no restricted app is running, there's no entry and user@ stays free (the
 * user manager and its services aren't touched). */

/* Recompiles the user's union from the flags of their scopes and writes it (or deletes it if
 * there's no restriction). Only takes effect with user@ attached. */
static void sync_sndsvc(Session *s);

static void sync_union(Session *s) {
    if (!G.bpf_ok || !s->user_cg) return;
    NpApp u;
    memset(&u, 0, sizeof u);
    for (int k = 0; k < s->nsc; k++) u.flags |= s->sc[k].app_v.flags;
    if (u.flags) {
        u.trusted = 1;                  /* whatever the union doesn't restrict is allowed without notice */
        u.mode = NP_M_SILENT;
        if (bpfs_map_update(G.m_np_union, &s->user_cg, &u, BPF_ANY)) logf_("np_union: %s", strerror(errno));
    } else {
        bpfs_map_delete(G.m_np_union, &s->user_cg);
    }
    sync_sndsvc(s);
}

/* The user's real sound services, which keep the ALSA capture nodes under the union's no-mic
 * (PipeWire opens them to record, WirePlumber to probe the card's profiles): the exact cgroups
 * user.slice/user-<uid>.slice/user@<uid>.service/session.slice/{pipewire,wireplumber}.service
 * (never a nested one that merely ends in the same names), and only while each holds exactly one
 * process. Its tgid and cgroup id go into np_sndsvc; the program matches both. Refreshed every
 * second (a restart is a new process). */
static void sync_sndsvc(Session *s) {
    if (!G.bpf_ok) return;
    static const char *const unit[2] = { "pipewire.service", "wireplumber.service" };
    char dir[200], path[300], line[32];
    snprintf(dir, sizeof dir, CG_ROOT "/" CG_USER "/user-%u.slice/user@%u.service/session.slice", s->uid, s->uid);
    int dfd = s->user_cg ? open(dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
    for (int k = 0; k < 2; k++) {
        uint32_t tgid = 0;
        uint64_t cg = dfd >= 0 ? bpfs_cgroup_id(dfd, unit[k]) : 0;
        if (cg) {
            snprintf(path, sizeof path, "%s/%s/cgroup.procs", dir, unit[k]);
            FILE *f = fopen(path, "re");
            int n = 0;
            unsigned long v = 0;
            while (f && fgets(line, sizeof line, f)) { if (!n) v = strtoul(line, NULL, 10); n++; }
            if (f) fclose(f);
            if (n == 1 && v > 0 && v <= UINT32_MAX) tgid = (uint32_t)v;
        }
        if (s->snd_tgid[k] && s->snd_tgid[k] != tgid) bpfs_map_delete(G.m_np_sndsvc, &s->snd_tgid[k]);
        if (tgid && bpfs_map_update(G.m_np_sndsvc, &tgid, &cg, BPF_ANY)) tgid = 0;
        s->snd_tgid[k] = tgid;
    }
    if (dfd >= 0) close(dfd);
}

static void drop_user_links(Session *s) {
    for (int i = 0; i < s->nulinks; i++) close(s->ulinks[i]);
    s->nulinks = 0;
}

/* Attaches (or re-attaches) the programs to user@<uid>.service. systemd creates it at login and
 * deletes and recreates it with another id when the user manager restarts, so this is called on
 * every pass: if the id changed (or disappeared), it drops the old links and the old union. */
static void sync_user_scope(Session *s) {
    if (!G.bpf_ok) return;
    char path[200];
    snprintf(path, sizeof path, CG_ROOT "/" CG_USER "/user-%u.slice", s->uid);
    char leaf[64];
    snprintf(leaf, sizeof leaf, "user@%u.service", s->uid);
    uint64_t id = 0;
    int ufd = -1, sfd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (sfd >= 0) {
        id = bpfs_cgroup_id(sfd, leaf);
        if (id) ufd = openat(sfd, leaf, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        close(sfd);
    }
    if (id == s->user_cg) { if (ufd >= 0) close(ufd); return; }
    drop_user_links(s);
    if (s->user_cg) { bpfs_map_delete(G.m_np_union, &s->user_cg); bpfs_map_delete(G.m_np_counts, &s->user_cg); }
    s->user_cg = 0;
    if (!id || ufd < 0) { if (ufd >= 0) close(ufd); return; }
    for (int i = 0; i < G.nprogs && s->nulinks < (int)(sizeof s->ulinks / sizeof s->ulinks[0]); i++) {
        int l = bpfs_link_create(G.progs[i], ufd, G.attach[i]);
        if (l < 0) { logf_("uid %u: could not attach to user@: %s", s->uid, strerror(errno)); drop_user_links(s); close(ufd); return; }
        s->ulinks[s->nulinks++] = l;
    }
    close(ufd);
    s->user_cg = id;
    logf_("uid %u: %d programs attached to user@%u.service", s->uid, s->nulinks, s->uid);
    sync_union(s);
}

/* ---- sessions and scopes --------------------------------------------------------------------- */

static int scope_app(const char *name, char *app) {
    if (!strcmp(name, "server")) { snprintf(app, FW_APP_MAX, "lordos"); return 0; }
    size_t n = strlen(name);
    if (n < 18 || strncmp(name, "app-lordos-", 11) || strcmp(name + n - 6, ".scope")) return -1;
    return fw_app_canon(name, app, FW_APP_MAX);
}

static void scan_session(Session *s) {
    char path[160];
    snprintf(path, sizeof path, CG_ROOT "/" CG_SERVICE "/%s", s->dir);
    int dfd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) return;
    DIR *d = fdopendir(dup(dfd));
    for (int i = 0; i < s->nsc; i++) s->sc[i].seen = 0;
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_type != DT_DIR || e->d_name[0] == '.' || strlen(e->d_name) >= sizeof s->sc[0].name) continue;
        char app[FW_APP_MAX];
        if (scope_app(e->d_name, app)) continue;
        uint64_t id = bpfs_cgroup_id(dfd, e->d_name);
        if (!id) continue;
        Scope *sc = NULL;
        for (int i = 0; i < s->nsc; i++) if (s->sc[i].cgid == id && !strcmp(s->sc[i].name, e->d_name)) sc = &s->sc[i];
        if (!sc) {
            if (s->nsc == s->cap) {
                int nc = s->cap ? s->cap * 2 : 8;
                Scope *ns = realloc(s->sc, (size_t)nc * sizeof *ns);
                if (!ns) break;
                s->sc = ns;
                s->cap = nc;
            }
            sc = &s->sc[s->nsc++];
            memset(sc, 0, sizeof *sc);
            snprintf(sc->name, sizeof sc->name, "%s", e->d_name);
            snprintf(sc->app, sizeof sc->app, "%s", app);
            sc->cgid = id;
            sc->idx = idx_of(s->uid, app);
            sync_scope(s, sc);
        }
        sc->seen = 1;
    }
    if (d) closedir(d);
    close(dfd);
    for (int i = 0; i < s->nsc;) {
        if (s->sc[i].seen) { i++; continue; }
        drop_scope(&s->sc[i]);
        s->sc[i] = s->sc[--s->nsc];
    }
    sync_user_scope(s);                 /* attaches/re-attaches user@ and recompiles the union */
    sync_union(s);
}

static void drop_session(int k) {
    Session *s = &G.sess[k];
    for (int i = 0; i < s->nsc; i++) drop_scope(&s->sc[i]);
    drop_user_links(s);
    if (s->user_cg) { bpfs_map_delete(G.m_np_union, &s->user_cg); bpfs_map_delete(G.m_np_counts, &s->user_cg); }
    for (int k = 0; k < 2; k++) if (s->snd_tgid[k]) bpfs_map_delete(G.m_np_sndsvc, &s->snd_tgid[k]);
    if (s->wd >= 0) inotify_rm_watch(G.ifd, s->wd);
    free(s->sc);
    fw_store_free(s->store);
    G.sess[k] = G.sess[--G.nsess];
}

/* Every ALSA node (/sys/class/sound/<name>/dev, "116:<minor>"), capture PCMs (pcmC*D*c) marked,
 * for "no microphone" against apps that read the sound card directly instead of through PipeWire.
 * The program denies a no-mic app's own scope every ALSA minor not marked NP_SND_OTHER, so a node
 * that appears between two scans (a USB microphone plugged in) is denied until it is known: fail
 * closed. Rescanned with the sessions. Only sysfs: no device node is opened. */
static int capture_pcm(const char *name) {
    size_t l = strlen(name);
    return !strncmp(name, "pcmC", 4) && l >= 6 && name[l - 1] == 'c';
}

static void scan_snd(void) {
    if (!G.bpf_ok) return;
    static uint32_t now[NP_MAX_SND], kind[NP_MAX_SND];
    int n = 0;
    DIR *d = opendir("/sys/class/sound");
    struct dirent *e;
    while (d && (e = readdir(d)) && n < NP_MAX_SND) {
        if (e->d_name[0] == '.' || strlen(e->d_name) > 64) continue;
        char p[300], buf[32];
        snprintf(p, sizeof p, "/sys/class/sound/%s/dev", e->d_name);
        int fd = open(p, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) continue;
        ssize_t k = read(fd, buf, sizeof buf - 1);
        close(fd);
        unsigned ma, mi;
        if (k > 0 && (buf[k] = 0, sscanf(buf, "%u:%u", &ma, &mi) == 2) && ma == NP_MAJOR_SND) {
            now[n] = mi;
            kind[n++] = capture_pcm(e->d_name) ? NP_SND_CAPTURE : NP_SND_OTHER;
        }
    }
    if (d) closedir(d);
    for (int k = 0; k < n; k++) bpfs_map_update(G.m_np_snd, &now[k], &kind[k], BPF_ANY);
    for (int i = 0; i < G.nsnd; i++) {
        int keep = 0;
        for (int k = 0; k < n && !keep; k++) keep = now[k] == G.snd[i];
        if (!keep) bpfs_map_delete(G.m_np_snd, &G.snd[i]);
    }
    memcpy(G.snd, now, sizeof(uint32_t) * (size_t)n);
    G.nsnd = n;
}

static void scan_all(void) {
    scan_snd();
    DIR *d = opendir(CG_ROOT "/" CG_SERVICE);
    for (int i = 0; i < G.nsess; i++) G.sess[i].seen = 0;
    struct dirent *e;
    while (d && (e = readdir(d))) {
        unsigned uid;
        char tail;
        if (e->d_type != DT_DIR || sscanf(e->d_name, "lordos-session-%u%c", &uid, &tail) != 1) continue;
        Session *s = NULL;
        for (int i = 0; i < G.nsess; i++) if (G.sess[i].uid == uid) s = &G.sess[i];
        if (!s) {
            if (G.nsess == MAX_SESS) continue;
            s = &G.sess[G.nsess++];
            memset(s, 0, sizeof *s);
            s->uid = uid;
            snprintf(s->dir, sizeof s->dir, "lordos-session-%u", uid);
            char path[160];
            snprintf(path, sizeof path, CG_ROOT "/" CG_SERVICE "/%s", s->dir);
            s->wd = inotify_add_watch(G.ifd, path, IN_CREATE | IN_DELETE | IN_ONLYDIR);
            load_rules(s);
        }
        s->seen = 1;
        scan_session(s);
    }
    if (d) closedir(d);
    for (int i = 0; i < G.nsess;) {
        if (G.sess[i].seen) i++;
        else drop_session(i);
    }
}

static Scope *scope_by_cgid(uint64_t id, Session **out) {
    for (int i = 0; i < G.nsess; i++)
        for (int k = 0; k < G.sess[i].nsc; k++)
            if (G.sess[i].sc[k].cgid == id) { *out = &G.sess[i]; return &G.sess[i].sc[k]; }
    return NULL;
}

static Session *session_by_user_cg(uint64_t id) {
    for (int i = 0; i < G.nsess; i++) if (G.sess[i].user_cg && G.sess[i].user_cg == id) return &G.sess[i];
    return NULL;
}

/* ---- clients --------------------------------------------------------------------------------- */

static void cl_drop(int k) {
    epoll_ctl(G.ep, EPOLL_CTL_DEL, G.cl[k].fd, NULL);
    close(G.cl[k].fd);
    free(G.cl[k].out);
    G.cl[k] = G.cl[--G.ncl];
}

static void cl_flush(Client *c) {
    while (c->outlen) {
        ssize_t n = send(c->fd, c->out, c->outlen, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n <= 0) break;
        memmove(c->out, c->out + n, c->outlen - (size_t)n);
        c->outlen -= (size_t)n;
    }
    struct epoll_event ev = { .events = EPOLLIN | (c->outlen ? EPOLLOUT : 0), .data.u64 = 1000 + (uint64_t)c->fd };
    epoll_ctl(G.ep, EPOLL_CTL_MOD, c->fd, &ev);
}

static void cl_send(Client *c, int type, const char *json, size_t n, int droppable) {
    if (!c->out && !(c->out = malloc(CLIENT_OUT))) return;
    if (c->outlen + n + 5 > CLIENT_OUT) { (void)droppable; return; }   /* nobody is reading: notices are lost (the counters go out again when they change) */
    uint32_t l = (uint32_t)n + 1;
    unsigned char h[5] = { (unsigned char)l, (unsigned char)(l >> 8), (unsigned char)(l >> 16), (unsigned char)(l >> 24), (unsigned char)type };
    memcpy(c->out + c->outlen, h, 5);
    memcpy(c->out + c->outlen + 5, json, n);
    c->outlen += n + 5;
    cl_flush(c);
}

static void send_uid(unsigned uid, int type, const char *json, size_t n, int droppable) {
    for (int i = 0; i < G.ncl; i++) if (G.cl[i].uid == uid) cl_send(&G.cl[i], type, json, n, droppable);
}

static void status_json(char *b, size_t n) {
    char err[260];
    json_quote(err, sizeof err, G.bpf_ok ? "" : G.bpf_err);
    snprintf(b, n, "{\"bpf\":%s,\"devices\":%s,\"bluetooth\":%s,\"dns\":\"%s\",\"error\":%s}", G.bpf_ok ? "true" : "false",
             G.bpf_ok && G.dev_ok ? "true" : "false", G.bpf_ok && G.bt_ok ? "true" : "false", G.dns ? npdns_source(G.dns) : "none", err);
}

static void accept_client(void) {
    for (;;) {
        int fd = accept4(G.lfd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0) return;
        struct ucred cr;
        socklen_t cl = sizeof cr;
        if (G.ncl == MAX_CLIENTS || getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &cl)) { close(fd); continue; }
        int per = 0;
        for (int i = 0; i < G.ncl; i++) per += G.cl[i].uid == cr.uid;
        if (per >= 4) { close(fd); continue; }               /* up to 4 per user */
        Client *c = &G.cl[G.ncl++];
        memset(c, 0, sizeof *c);
        c->fd = fd;
        c->uid = cr.uid;
        struct epoll_event ev = { .events = EPOLLIN, .data.u64 = 1000 + (uint64_t)fd };
        epoll_ctl(G.ep, EPOLL_CTL_ADD, fd, &ev);
        char st[400];
        status_json(st, sizeof st);
        cl_send(c, NP_MSG_STATUS, st, strlen(st), 0);
        for (int i = 0; i < G.nsess; i++) if (G.sess[i].uid == cr.uid) G.sess[i].dirty_counts = 1;
    }
}

static void reload_uid(unsigned uid) {
    for (int i = 0; i < G.nsess; i++) {
        if (G.sess[i].uid != uid) continue;
        load_rules(&G.sess[i]);
        for (int k = 0; k < G.sess[i].nsc; k++) sync_scope(&G.sess[i], &G.sess[i].sc[k]);
        sync_union(&G.sess[i]);
    }
}

static void client_read(int k) {
    Client *c = &G.cl[k];
    for (;;) {
        ssize_t n = recv(c->fd, c->in + c->inlen, sizeof c->in - c->inlen, MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) { cl_drop(k); return; }
        if (n < 0) return;
        c->inlen += (size_t)n;
        while (c->inlen >= 5) {
            uint32_t l = (uint32_t)c->in[0] | (uint32_t)c->in[1] << 8 | (uint32_t)c->in[2] << 16 | (uint32_t)c->in[3] << 24;
            if (l != 1 || c->in[4] != NP_MSG_RELOAD) { cl_drop(k); return; }   /* the only thing accepted */
            reload_uid(c->uid);
            memmove(c->in, c->in + 5, c->inlen - 5);
            c->inlen -= 5;
        }
        if (c->inlen == sizeof c->in) { cl_drop(k); return; }
    }
}

/* ---- notices and counters -------------------------------------------------------------------- */

static const char *by_name(int r) {
    return r == NP_BY_RESTRICTED ? "restricted" : r == NP_BY_RULE ? "rule" : r == NP_BY_TRUSTED ? "trusted" : "mode";
}

static void on_event(void *u, const void *data, uint32_t len) {
    (void)u;
    if (len < sizeof(NpEvent)) return;
    NpEvent ev;
    memcpy(&ev, data, sizeof ev);
    Session *s;
    Scope *sc = scope_by_cgid(ev.cgid, &s);
    if (ev.dir == NP_D_DEV || ev.dir == NP_D_BT) {
        /* a device open or a Bluetooth socket: only in the journal (the Monitor's counters and
         * connection log are about the network) */
        Session *us = sc ? s : session_by_user_cg(ev.cgid);
        unsigned minor = (unsigned)ev.addr[0] | (unsigned)ev.addr[1] << 8 | (unsigned)ev.addr[2] << 16 | (unsigned)ev.addr[3] << 24;
        if (!us) return;
        if (ev.dir == NP_D_DEV)
            logf_("uid %u %s: denied opening device %u:%u (%s)", us->uid, sc ? sc->app : "user@ (union of restricted apps)", ev.port, minor, fw_flag_name(ev.flag));
        else
            logf_("uid %u %s: denied a Bluetooth socket (%s)", us->uid, sc ? sc->app : "user@ (union of restricted apps)", fw_flag_name(ev.flag));
        return;
    }
    char addr[INET6_ADDRSTRLEN] = "";
    if (ev.fam == 4) inet_ntop(AF_INET, ev.addr, addr, sizeof addr);
    else if (ev.fam == 6) inet_ntop(AF_INET6, ev.addr, addr, sizeof addr);
    if (!sc) {
        /* the user@ union: a process that escaped with systemd-run --user (no app scope) */
        Session *us = session_by_user_cg(ev.cgid);
        if (us && ev.verdict == NP_V_DENY)
            logf_("uid %u user@: denied %s %s %s:%u (union of restricted apps%s%s)", us->uid,
                  ev.dir == NP_D_IN ? "in" : "out", ev.proto == NP_P_TCP ? "tcp" : ev.proto == NP_P_UDP ? "udp" : "other",
                  addr[0] ? addr : "?", ev.port, ev.flag ? " " : "", ev.flag ? fw_flag_name(ev.flag) : "");
        return;
    }
    const char *flag = ev.flag ? fw_flag_name(ev.flag) : "";
    /* the names the system resolver gave for that address (at most 4, so the Monitor can offer
     * "block this domain" with the real name) */
    char hosts[4 * (FW_NAME_MAX * 2 + 4) + 4] = "[]";
    if (ev.fam == 4 || ev.fam == 6) {
        char names[4][FW_NAME_MAX];
        int nh = dnsmap_lookup(G.names, NAMES_APP, ev.fam, ev.addr, mono(), names, 4);
        size_t w = 0;
        hosts[w++] = '[';
        for (int i = 0; i < nh; i++) {
            char q[FW_NAME_MAX * 2 + 4];
            json_quote(q, sizeof q, names[i]);
            w += (size_t)snprintf(hosts + w, sizeof hosts - w, "%s%s", i ? "," : "", q);
        }
        snprintf(hosts + w, sizeof hosts - w, "]");
    }
    char app[FW_APP_MAX * 2 + 4], j[700 + sizeof hosts];
    json_quote(app, sizeof app, sc->app);
    int n = snprintf(j, sizeof j, "{\"app\":%s,\"v\":\"%s\",\"dir\":\"%s\",\"proto\":\"%s\",\"addr\":\"%s\",\"port\":%u,\"by\":\"%s\",\"rule\":%u,\"flag\":\"%s\",\"record\":%d,\"hosts\":%s}",
                     app, ev.verdict == NP_V_DENY ? "deny" : "allow", ev.dir == NP_D_IN ? "in" : "out",
                     ev.proto == NP_P_TCP ? "tcp" : ev.proto == NP_P_UDP ? "udp" : "other", addr, ev.port, by_name(ev.reason),
                     ev.rule_id, flag ? flag : "", ev.record, hosts);
    if (n > 0 && (size_t)n < sizeof j) send_uid(s->uid, NP_MSG_EVENT, j, (size_t)n, 1);
    if (ev.verdict == NP_V_DENY)
        logf_("uid %u %s: denied %s %s %s:%u (%s%s%s)", s->uid, sc->app, ev.dir == NP_D_IN ? "in" : "out",
              ev.proto == NP_P_TCP ? "tcp" : ev.proto == NP_P_UDP ? "udp" : "other", addr[0] ? addr : "?", ev.port, by_name(ev.reason),
              ev.flag ? " " : "", ev.flag && flag ? flag : "");
}

static void push_counts(void) {
    for (int i = 0; i < G.nsess; i++) {
        Session *s = &G.sess[i];
        int changed = s->dirty_counts;
        for (int k = 0; k < s->nsc; k++) {
            NpCount c = { 0, 0 };
            if (G.bpf_ok) bpfs_map_lookup(G.m_np_counts, &s->sc[k].cgid, &c);
            if (c.allowed != s->sc[k].allowed || c.denied != s->sc[k].denied) changed = 1;
            s->sc[k].allowed = c.allowed;
            s->sc[k].denied = c.denied;
        }
        if (!changed) continue;
        s->dirty_counts = 0;
        size_t cap = 64 + (size_t)s->nsc * (FW_APP_MAX * 2 + 48), w = 0;
        char *j = malloc(cap);
        if (!j) continue;
        j[w++] = '{';
        for (int k = 0; k < s->nsc; k++) {
            char app[FW_APP_MAX * 2 + 4];
            json_quote(app, sizeof app, s->sc[k].app);
            w += (size_t)snprintf(j + w, cap - w, "%s%s:[%llu,%llu]", w > 1 ? "," : "", app,
                                  (unsigned long long)s->sc[k].allowed, (unsigned long long)s->sc[k].denied);
        }
        j[w++] = '}';
        send_uid(s->uid, NP_MSG_COUNTS, j, w, 0);
        free(j);
    }
}

/* ---- names from the resolver ----------------------------------------------------------------- */

static void on_dns(void *u, const uint8_t *pkt, size_t len) {
    (void)u;
    long long now = mono();
    if (dnsmap_learn(G.names, NAMES_APP, pkt, len, now) <= 0 || !G.bpf_ok) return;
    DnsResponse *r = malloc(sizeof *r);
    if (!r || dns_parse_response(pkt, len, r)) { free(r); return; }
    /* only the nodes of these addresses, for the apps with domain rules (before the app connects:
     * resolved notifies the monitor before answering the app) */
    for (int a = 0; a < r->naddr; a++) {
        char names[DNSMAP_PER_IP][FW_NAME_MAX];
        int n = dnsmap_lookup(G.names, NAMES_APP, r->addr[a].fam, r->addr[a].addr, now, names, DNSMAP_PER_IP);
        if (!n) continue;
        for (int i = 0; i < G.nsess; i++) {
            Session *s = &G.sess[i];
            for (int k = 0; k < s->nsc; k++) {
                Scope *sc = &s->sc[k];
                if (!sc->has_domains) continue;
                NpNode node;
                int tr = 0;
                if (np_compile_host(store_of(s), sc->app, r->addr[a].fam, r->addr[a].addr, (const char (*)[FW_NAME_MAX])names, n, &node, &tr) != 1) continue;
                if (install(sc->idx, sc->gen, &node)) continue;
                np_u8 *has = node.fam == 4 ? &sc->app_v.has4 : &sc->app_v.has6;
                if (!*has) { *has = 1; bpfs_map_update(G.m_np_apps, &sc->cgid, &sc->app_v, BPF_ANY); }
            }
        }
    }
    free(r);
}

/* ---- BPF ------------------------------------------------------------------------------------- */

#ifdef NP_HAVE_BPF
/* Major of /dev/hidraw* (dynamic): the "hidraw" line of /proc/devices, or 0. */
static uint32_t hidraw_major(void) {
    FILE *f = fopen("/proc/devices", "re");
    char line[128];
    unsigned m = 0, v;
    char name[64];
    int chr = 0;
    while (f && fgets(line, sizeof line, f)) {
        if (!strncmp(line, "Character devices:", 18)) { chr = 1; continue; }
        if (!strncmp(line, "Block devices:", 14)) chr = 0;
        if (chr && sscanf(line, "%u %63s", &v, name) == 2 && !strcmp(name, "hidraw")) m = v;
    }
    if (f) fclose(f);
    return m;
}
#endif

static int bpf_setup(void) {
#ifndef NP_HAVE_BPF
    lerr_set(G.bpf_err, sizeof G.bpf_err, "fw.bpfNotBuilt", NULL);
    return -1;
#else
#define NP_MKMAP(n, t, k, v, m, f) G.m_##n = bpfs_map_create(t, k, v, m, f, #n);
    NP_MAPS(NP_MKMAP)
#undef NP_MKMAP
    if (G.m_np_snd < 0 || G.m_np_sndsvc < 0 || G.m_np_cfg < 0 || G.m_np_apps < 0 || G.m_np_union < 0 || G.m_np_lpm4 < 0 || G.m_np_lpm6 < 0 || G.m_np_seen < 0 || G.m_np_counts < 0 || G.m_np_events < 0) {
        lerr_set(G.bpf_err, sizeof G.bpf_err, "fw.bpfMaps", "errno", lerr_errno(errno), NULL);
        return -1;
    }
    if (bpfs_ring_open(&G.ring, G.m_np_events, NP_RINGBUF)) {
        lerr_set(G.bpf_err, sizeof G.bpf_err, "fw.bpfRing", "errno", lerr_errno(errno), NULL);
        return -1;
    }
    static char log[1 << 20];
    int np = 0;
    for (int w = 0; w < NP_NSPECS; w++) {
        const NpProgSpec *want = np_prog_specs;
        const NpProgDesc *d = NULL;
        for (int i = 0; i < NP_NPROGS; i++) if (!strcmp(np_progs[i].section, want[w].sec)) d = &np_progs[i];
        if (!d) { lerr_set(G.bpf_err, sizeof G.bpf_err, "fw.bpfMissing", "program", want[w].sec, NULL); return -1; }
        uint32_t btf_id = 0;
        if (want[w].btf && !(btf_id = bpfs_btf_func_id(NULL, want[w].btf))) {
            logf_("%s: %s is not in the kernel's BTF: \"no Bluetooth\" is not enforced", want[w].sec, want[w].btf);
            continue;
        }
        int fd = bpfs_prog_load_btf(want[w].type, want[w].attach, btf_id, np_prog_license(&want[w]), d, map_fd, log, sizeof log);
        if (fd < 0 && want[w].opt) {
            int e = errno;
            size_t ll = strlen(log);
            fprintf(stderr, "%s\n", ll > 3000 ? log + ll - 3000 : log);
            logf_("%s: the kernel refused it (%s); that restriction is not enforced", want[w].sec, strerror(e));
            continue;
        }
        if (fd < 0) {
            int e = errno;
            size_t ll = strlen(log);
            fprintf(stderr, "%s\n", ll > 3000 ? log + ll - 3000 : log);
            lerr_set(G.bpf_err, sizeof G.bpf_err, "fw.bpfRejected", "program", want[w].sec, "errno", lerr_errno(e), NULL);
            return -1;
        }
        if (want[w].attach == BPF_CGROUP_DEVICE) G.dev_ok = 1;
        if (want[w].attach == BPF_LSM_CGROUP) G.bt_ok = 1;
        G.attach[np] = want[w].attach;
        G.progs[np++] = fd;
    }
    G.nprogs = np;
    NpCfg cfg = { SCOPE_LEVEL, 1, USER_LEVEL, hidraw_major() };
    uint32_t zero = 0;
    if (bpfs_map_update(G.m_np_cfg, &zero, &cfg, BPF_ANY)) { lerr_set(G.bpf_err, sizeof G.bpf_err, "fw.bpfConfig", "errno", lerr_errno(errno), NULL); return -1; }
    logf_("%d BPF programs loaded (devices: %s, Bluetooth sockets: %s, hidraw major %u)", np, G.dev_ok ? "yes" : "no",
          G.bt_ok ? "yes" : "no", cfg.hidraw_major);
    return 0;
#endif
}

/* Attaches the programs to the lordos.service cgroup. systemd deletes and recreates it every
 * time the service restarts (and it may not exist yet at startup), so this is called at start,
 * on every inotify notice in system.slice and once per second: if the cgroup is a different one
 * (another id), the old links are dropped and attached again. */
static void attach_service(void) {
    if (!G.bpf_ok) return;
    int dfd = open(CG_ROOT "/system.slice", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    uint64_t id = dfd >= 0 ? bpfs_cgroup_id(dfd, "lordos.service") : 0;
    if (dfd >= 0) close(dfd);
    if (id == G.attached_cg) return;
    for (int k = 0; k < G.nlinks; k++) close(G.links[k]);
    G.nlinks = 0;
    if (G.cgfd >= 0) close(G.cgfd);
    G.cgfd = -1;
    G.attached_cg = 0;
    if (!id) { logf_("waiting for lordos.service (its cgroup doesn't exist)"); return; }
    G.cgfd = open(CG_ROOT "/" CG_SERVICE, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (G.cgfd < 0) return;
    for (int i = 0; i < G.nprogs; i++) {
        int l = bpfs_link_create(G.progs[i], G.cgfd, G.attach[i]);
        if (l < 0) {
            logf_("could not attach a program: %s", strerror(errno));
            for (int k = 0; k < G.nlinks; k++) close(G.links[k]);
            G.nlinks = 0;
            return;
        }
        G.links[G.nlinks++] = l;
    }
    G.attached_cg = id;
    if (G.root_wd >= 0) inotify_rm_watch(G.ifd, G.root_wd);
    G.root_wd = inotify_add_watch(G.ifd, CG_ROOT "/" CG_SERVICE, IN_CREATE | IN_DELETE | IN_ONLYDIR);
    logf_("%d BPF programs attached to " CG_SERVICE " (scopes at level %d)", G.nlinks, SCOPE_LEVEL);
}

/* ---- main ------------------------------------------------------------------------------------ */

static volatile sig_atomic_t quit;
static void on_term(int s) { (void)s; quit = 1; }

static int listen_sock(void) {
    mkdir("/run/lordos", 0755);
    struct stat st;
    if (lstat("/run/lordos", &st) || !S_ISDIR(st.st_mode) || st.st_uid != 0) { logf_("/run/lordos is not a root-owned folder"); return -1; }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", NP_SOCK_PATH);
    unlink(NP_SOCK_PATH);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) || chmod(NP_SOCK_PATH, 0666) || listen(fd, 16)) {
        logf_("could not open %s: %s", NP_SOCK_PATH, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static void watch_dns(void) {
    int fd = G.dns ? npdns_fd(G.dns) : -1;
    if (fd == G.dns_fd) return;
    if (G.dns_fd >= 0) epoll_ctl(G.ep, EPOLL_CTL_DEL, G.dns_fd, NULL);
    G.dns_fd = fd;
    if (fd >= 0) {
        struct epoll_event ev = { .events = EPOLLIN, .data.u64 = 5 };
        epoll_ctl(G.ep, EPOLL_CTL_ADD, fd, &ev);
    }
    char st[400];
    status_json(st, sizeof st);
    for (int i = 0; i < G.ncl; i++) cl_send(&G.cl[i], NP_MSG_STATUS, st, strlen(st), 0);
}

int main(int argc, char **argv) {
    (void)argv;
    if (argc != 1) { fprintf(stderr, "lord-netperm takes no arguments (see docs/FIREWALL.md)\n"); return 2; }
    if (geteuid() != 0) { fprintf(stderr, "lord-netperm: must run as root (lord-netperm.service)\n"); return 1; }
    setvbuf(stderr, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa = { .sa_handler = on_term };
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    G.dns_fd = -1;
    G.cgfd = -1;
    G.empty = fw_store_new();
    G.names = dnsmap_new(16384);
    G.ep = epoll_create1(EPOLL_CLOEXEC);
    G.ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    G.tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    G.lfd = listen_sock();
    if (!G.empty || !G.names || G.ep < 0 || G.ifd < 0 || G.tfd < 0 || G.lfd < 0) { logf_("could not start"); return 1; }

    G.bpf_ok = bpf_setup() == 0;
    if (!G.bpf_ok) logf_("BPF not available (%s): no rule is enforced; the desktop carries on as usual", G.bpf_err);
    else G.dns = npdns_new(on_dns, NULL);

    struct itimerspec its = { { 1, 0 }, { 1, 0 } };
    timerfd_settime(G.tfd, 0, &its, NULL);
    struct epoll_event ev = { .events = EPOLLIN, .data.u64 = 1 };
    epoll_ctl(G.ep, EPOLL_CTL_ADD, G.lfd, &ev);
    ev.data.u64 = 2;
    epoll_ctl(G.ep, EPOLL_CTL_ADD, G.ifd, &ev);
    ev.data.u64 = 3;
    epoll_ctl(G.ep, EPOLL_CTL_ADD, G.tfd, &ev);
    if (G.bpf_ok) { ev.data.u64 = 4; epoll_ctl(G.ep, EPOLL_CTL_ADD, G.m_np_events, &ev); }
    G.root_wd = -1;
    G.sys_wd = inotify_add_watch(G.ifd, CG_ROOT "/system.slice", IN_CREATE | IN_ONLYDIR);
    attach_service();
    if (G.root_wd < 0) G.root_wd = inotify_add_watch(G.ifd, CG_ROOT "/" CG_SERVICE, IN_CREATE | IN_DELETE | IN_ONLYDIR);
    scan_all();
    watch_dns();

    while (!quit) {
        struct epoll_event evs[32];
        int n = epoll_wait(G.ep, evs, 32, -1);
        if (n < 0 && errno != EINTR) break;
        for (int i = 0; i < n; i++) {
            uint64_t t = evs[i].data.u64;
            if (t == 1) accept_client();
            else if (t == 2) {
                char buf[4096];
                while (read(G.ifd, buf, sizeof buf) > 0) {}
                attach_service();                   /* did lordos.service start again? */
                scan_all();                         /* a scope or a session that's new or gone */
            } else if (t == 3) {
                uint64_t x;
                if (read(G.tfd, &x, sizeof x) < 0) {}
                G.tick++;
                attach_service();
                if (G.dns) { npdns_tick(G.dns); watch_dns(); }
                if (G.tick % 5 == 0) scan_all();    /* in case an inotify notice was lost */
                for (int k = 0; k < G.nsess; k++) sync_sndsvc(&G.sess[k]);
                if (G.tick % 30 == 0) {
                    dnsmap_expire(G.names, mono());
                    for (int s = 0; s < G.nsess; s++)
                        for (int k = 0; k < G.sess[s].nsc; k++)
                            if (G.sess[s].sc[k].has_domains) sync_scope(&G.sess[s], &G.sess[s].sc[k]);
                }
                push_counts();
            } else if (t == 4) bpfs_ring_drain(&G.ring, on_event, NULL);
            else if (t == 5) { if (G.dns) npdns_ready(G.dns); watch_dns(); }
            else if (t >= 1000) {
                int fd = (int)(t - 1000);
                for (int k = 0; k < G.ncl; k++) {
                    if (G.cl[k].fd != fd) continue;
                    if (evs[i].events & (EPOLLHUP | EPOLLERR)) { cl_drop(k); break; }
                    if (evs[i].events & EPOLLOUT) cl_flush(&G.cl[k]);
                    if (evs[i].events & EPOLLIN) client_read(k);
                    break;
                }
            }
        }
    }
    logf_("exiting: the programs detach (everything is allowed)");
    unlink(NP_SOCK_PATH);
    return 0;
}

/* netperm.c — lord-netperm: el servicio root que aplica en el kernel los permisos de red por app
 * (docs/FIREWALL.md, paso 2). Cerrado y mínimo:
 *
 *   - sin argumentos, sin ejecutar nada, sin red propia (no crea sockets AF_INET/AF_INET6: el
 *     servicio de systemd además se lo prohíbe con RestrictAddressFamilies);
 *   - carga los programas BPF que lleva adentro (np.bpf.c → build/gen/np_bpf.h) en la cgroup de
 *     lordos.service, que contiene los subárboles de todas las sesiones;
 *   - lee las reglas de cada usuario con sesión desde ~/.config/lordos/firewall.json (ruta fija,
 *     validada) y las compila a mapas (npcompile.c);
 *   - aprende los nombres solo del resolvedor del sistema (npdns.c);
 *   - le cuenta a cada lord-server las conexiones nuevas de sus apps por /run/lordos/netperm.sock
 *     (npproto.h). Lo único que acepta por ahí es "volvé a leer mis reglas".
 *
 * Si el kernel no tiene BPF de cgroup (o falta CAP_BPF), lo dice en el registro y sigue sin
 * hacer nada: el escritorio no se entera. Con el servicio parado, los programas se desenganchan
 * solos (bpf_link): todo queda permitido. */
#include "netperm/bpfsys.h"
#include "netperm/npcompile.h"
#include "netperm/npdns.h"
#include "netperm/npproto.h"
#include "fw/fwmap.h"
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
#define NAMES_APP    "lordos"           /* clave de dnsmap para lo que dice el resolvedor */
#define MAX_SESS     64
#define MAX_CLIENTS  64
#define CLIENT_OUT   (256u << 10)

typedef struct {
    char name[256];
    char app[FW_APP_MAX];
    uint64_t cgid;
    uint32_t idx;
    unsigned gen;                       /* generación de sus nodos instalados */
    int has_domains, seen;
    NpApp app_v;                        /* lo último escrito en np_apps */
    uint64_t allowed, denied;           /* contadores del kernel */
} Scope;

typedef struct {
    unsigned uid;
    char dir[64];
    int wd, seen;
    FwStore *store;
    Scope *sc;
    int nsc, cap;
    int dirty_counts;
    uint64_t user_cg;                   /* id de user@<uid>.service, o 0 si no está (ni enganchado) */
    int ulinks[16], nulinks;            /* los programas enganchados en user@<uid>.service */
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
    char bpf_err[200];
    int m_cfg, m_apps, m_union, m_lpm4, m_lpm6, m_seen, m_counts, m_events;
    int links[16], nlinks;
    int progs[16], nprogs;
    uint32_t attach[16];
    uint64_t attached_cg;               /* id de la cgroup de lordos.service enganchada */
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
    FwMap installed;                    /* clave del nodo (idx, fam, prefijo, dirección) → Installed */
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

/* ---- leer las reglas de un usuario ---------------------------------------------------------- */

/* ~/.config/lordos/firewall.json sin seguir enlaces: cada carpeta y el archivo tienen que ser del
 * usuario, el archivo común y de hasta FW_FILE_MAX. La carpeta personal sale de /etc/passwd. */
static char *read_user_rules(unsigned uid, size_t *len, char *why, size_t wn) {
    struct passwd *pw = getpwuid(uid);
    if (!pw || pw->pw_dir[0] != '/') { snprintf(why, wn, "usuario sin carpeta personal"); return NULL; }
    int fd = open(pw->pw_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    static const char *const parts[] = { ".config", "lordos", "firewall.json" };
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || st.st_uid != uid) { snprintf(why, wn, "carpeta personal rara"); if (fd >= 0) close(fd); return NULL; }
    for (int i = 0; i < 3; i++) {
        int last = i == 2;
        int nfd = openat(fd, parts[i], O_RDONLY | O_NOFOLLOW | O_CLOEXEC | (last ? O_NONBLOCK : O_DIRECTORY));
        close(fd);
        if (nfd < 0) { snprintf(why, wn, errno == ENOENT ? "sin reglas" : "%s: %s", parts[i], strerror(errno)); return NULL; }
        fd = nfd;
        if (fstat(fd, &st) || st.st_uid != uid || (last && (!S_ISREG(st.st_mode) || st.st_size > (off_t)FW_FILE_MAX))) {
            snprintf(why, wn, "%s no es del usuario o no es un archivo común", parts[i]);
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
    if (!buf || got != (size_t)st.st_size) { free(buf); snprintf(why, wn, "no se pudo leer"); return NULL; }
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
        logf_("uid %u: firewall.json inválido (%s); se siguen usando las reglas anteriores", s->uid, err);
        fw_store_free(n);
        free(text);
        return;
    }
    if (!text && strcmp(why, "sin reglas")) logf_("uid %u: %s", s->uid, why);
    free(text);
    fw_store_free(s->store);
    s->store = n;
    logf_("uid %u: %zu reglas, modo %d", s->uid, fw_rule_count(n), (int)fw_mode(n));
}

/* ---- mapas ----------------------------------------------------------------------------------- */

static int map_fd(const char *name) {
    if (!strcmp(name, "np_cfg")) return G.m_cfg;
    if (!strcmp(name, "np_apps")) return G.m_apps;
    if (!strcmp(name, "np_union")) return G.m_union;
    if (!strcmp(name, "np_lpm4")) return G.m_lpm4;
    if (!strcmp(name, "np_lpm6")) return G.m_lpm6;
    if (!strcmp(name, "np_seen")) return G.m_seen;
    if (!strcmp(name, "np_counts")) return G.m_counts;
    if (!strcmp(name, "np_events")) return G.m_events;
    return -1;
}

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
    if (n->fam == 4) { NpLpm4Key k; np_key4(idx, n, &k); return bpfs_map_update(G.m_lpm4, &k, &n->list, BPF_ANY); }
    NpLpm6Key k; np_key6(idx, n, &k); return bpfs_map_update(G.m_lpm6, &k, &n->list, BPF_ANY);
}

static void lpm_del(uint32_t idx, const unsigned char *ik) {
    NpNode n;
    memset(&n, 0, sizeof n);
    n.fam = ik[4];
    n.prefix = ik[5];
    memcpy(n.addr, ik + 6, 16);
    if (n.fam == 4) { NpLpm4Key k; np_key4(idx, &n, &k); bpfs_map_delete(G.m_lpm4, &k); }
    else { NpLpm6Key k; np_key6(idx, &n, &k); bpfs_map_delete(G.m_lpm6, &k); }
}

/* Instala un nodo si cambió. 0 bien. */
static int install(uint32_t idx, unsigned gen, const NpNode *n) {
    unsigned char k[22];
    size_t kl = inst_key(idx, n, k);
    void **slot = fwmap_slot(&G.installed, k, kl, 1);
    if (!slot) return -1;
    Installed *in = *slot;
    if (in && in->list.n == n->list.n && !memcmp(in->list.e, n->list.e, n->list.n * sizeof(NpEnt))) { in->gen = gen; return 0; }
    if (lpm_put(idx, n)) { logf_("no entró una entrada en el trie: %s", strerror(errno)); return -1; }
    if (!in && !(in = *slot = malloc(sizeof *in))) return -1;
    in->idx = idx;
    in->gen = gen;
    in->list = n->list;
    return 0;
}

/* Borra los nodos de idx que no son de la generación gen (gen 0: todos). */
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
    if (np_compile_app(store_of(s), sc->app, sc->idx, G.names, NAMES_APP, mono(), &c)) { logf_("sin memoria al compilar %s", sc->app); return; }
    if (c.truncated) logf_("uid %u %s: %d reglas no entraron en los mapas (máximo %d por red)", s->uid, sc->app, c.truncated, NP_LIST_MAX);
    unsigned gen = ++sc->gen ? sc->gen : ++sc->gen;
    for (int i = 0; i < c.nnodes; i++) install(sc->idx, gen, &c.nodes[i]);
    prune(sc->idx, gen);
    sc->has_domains = c.has_domains;
    sc->app_v = c.app;
    if (bpfs_map_update(G.m_apps, &sc->cgid, &c.app, BPF_ANY)) logf_("np_apps: %s", strerror(errno));
    np_compiled_free(&c);
}

static void drop_scope(Scope *sc) {
    if (!G.bpf_ok) return;
    bpfs_map_delete(G.m_apps, &sc->cgid);
    bpfs_map_delete(G.m_counts, &sc->cgid);
    prune(sc->idx, 0);
}

/* ---- user@<uid>.service: la fuga de systemd-run --------------------------------------------- */

/* Una app sin caja de arena, corriendo como el usuario, puede pedirle a su gestor de systemd
 * (systemd-run --user, scope o servicio) que mueva un proceso a user@<uid>.service, fuera de
 * lordos.service, donde las scopes de las apps no llegan. Para cerrarlo se enganchan los mismos
 * programas también en user@<uid>.service de cada sesión y, cuando el proceso no está en la scope
 * de ninguna app, cae en np_union[user@]: la unión estricta (OR de las restricciones NP_R_*) de
 * las apps restringidas del usuario. Así un proceso escapado nunca es menos restrictivo que su
 * propia política; no otorga nada. Si no hay ninguna app restringida corriendo, no hay entrada y
 * user@ queda libre (el gestor de usuario y sus servicios no se tocan). */

/* Recompila la unión del usuario desde las flags de sus scopes y la escribe (o la borra si no hay
 * ninguna restricción). Solo tiene efecto con user@ enganchado. */
static void sync_union(Session *s) {
    if (!G.bpf_ok || !s->user_cg) return;
    NpApp u;
    memset(&u, 0, sizeof u);
    for (int k = 0; k < s->nsc; k++) u.flags |= s->sc[k].app_v.flags;
    if (u.flags) {
        u.trusted = 1;                  /* lo que la unión no restringe, se permite sin avisar */
        u.mode = NP_M_SILENT;
        if (bpfs_map_update(G.m_union, &s->user_cg, &u, BPF_ANY)) logf_("np_union: %s", strerror(errno));
    } else {
        bpfs_map_delete(G.m_union, &s->user_cg);
    }
}

static void drop_user_links(Session *s) {
    for (int i = 0; i < s->nulinks; i++) close(s->ulinks[i]);
    s->nulinks = 0;
}

/* Engancha (o re-engancha) los programas en user@<uid>.service. systemd lo crea al ingresar y lo
 * borra y recrea con otro id al reiniciar el gestor de usuario, así que esto se llama en cada
 * repaso: si el id cambió (o desapareció), suelta los links viejos y la unión vieja. */
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
    if (s->user_cg) { bpfs_map_delete(G.m_union, &s->user_cg); bpfs_map_delete(G.m_counts, &s->user_cg); }
    s->user_cg = 0;
    if (!id || ufd < 0) { if (ufd >= 0) close(ufd); return; }
    for (int i = 0; i < G.nprogs && s->nulinks < (int)(sizeof s->ulinks / sizeof s->ulinks[0]); i++) {
        int l = bpfs_link_create(G.progs[i], ufd, G.attach[i]);
        if (l < 0) { logf_("uid %u: no se pudo enganchar en user@: %s", s->uid, strerror(errno)); drop_user_links(s); close(ufd); return; }
        s->ulinks[s->nulinks++] = l;
    }
    close(ufd);
    s->user_cg = id;
    logf_("uid %u: %d programas enganchados en user@%u.service", s->uid, s->nulinks, s->uid);
    sync_union(s);
}

/* ---- sesiones y scopes ----------------------------------------------------------------------- */

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
    sync_user_scope(s);                 /* engancha/re-engancha user@ y recompila la unión */
    sync_union(s);
}

static void drop_session(int k) {
    Session *s = &G.sess[k];
    for (int i = 0; i < s->nsc; i++) drop_scope(&s->sc[i]);
    drop_user_links(s);
    if (s->user_cg) { bpfs_map_delete(G.m_union, &s->user_cg); bpfs_map_delete(G.m_counts, &s->user_cg); }
    if (s->wd >= 0) inotify_rm_watch(G.ifd, s->wd);
    free(s->sc);
    fw_store_free(s->store);
    G.sess[k] = G.sess[--G.nsess];
}

static void scan_all(void) {
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

/* ---- clientes -------------------------------------------------------------------------------- */

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
    if (c->outlen + n + 5 > CLIENT_OUT) { (void)droppable; return; }   /* nadie lee: se pierden avisos (los contadores van otra vez al cambiar) */
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
    snprintf(b, n, "{\"bpf\":%s,\"dns\":\"%s\",\"error\":%s}", G.bpf_ok ? "true" : "false", G.dns ? npdns_source(G.dns) : "none", err);
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
        if (per >= 4) { close(fd); continue; }               /* hasta 4 por usuario */
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
            if (l != 1 || c->in[4] != NP_MSG_RELOAD) { cl_drop(k); return; }   /* lo único que se acepta */
            reload_uid(c->uid);
            memmove(c->in, c->in + 5, c->inlen - 5);
            c->inlen -= 5;
        }
        if (c->inlen == sizeof c->in) { cl_drop(k); return; }
    }
}

/* ---- avisos y contadores --------------------------------------------------------------------- */

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
    char addr[INET6_ADDRSTRLEN] = "";
    if (ev.fam == 4) inet_ntop(AF_INET, ev.addr, addr, sizeof addr);
    else if (ev.fam == 6) inet_ntop(AF_INET6, ev.addr, addr, sizeof addr);
    if (!sc) {
        /* la unión de user@: un proceso que se escapó con systemd-run --user (sin scope de app) */
        Session *us = session_by_user_cg(ev.cgid);
        if (us && ev.verdict == NP_V_DENY)
            logf_("uid %u user@: denegada %s %s %s:%u (unión de apps restringidas%s%s)", us->uid,
                  ev.dir == NP_D_IN ? "entrada" : "salida", ev.proto == NP_P_TCP ? "tcp" : ev.proto == NP_P_UDP ? "udp" : "otro",
                  addr[0] ? addr : "?", ev.port, ev.flag ? " " : "", ev.flag ? fw_flag_name(ev.flag) : "");
        return;
    }
    const char *flag = ev.flag ? fw_flag_name(ev.flag) : "";
    char app[FW_APP_MAX * 2 + 4], j[700];
    json_quote(app, sizeof app, sc->app);
    int n = snprintf(j, sizeof j, "{\"app\":%s,\"v\":\"%s\",\"dir\":\"%s\",\"proto\":\"%s\",\"addr\":\"%s\",\"port\":%u,\"by\":\"%s\",\"rule\":%u,\"flag\":\"%s\",\"record\":%d}",
                     app, ev.verdict == NP_V_DENY ? "deny" : "allow", ev.dir == NP_D_IN ? "in" : "out",
                     ev.proto == NP_P_TCP ? "tcp" : ev.proto == NP_P_UDP ? "udp" : "other", addr, ev.port, by_name(ev.reason),
                     ev.rule_id, flag ? flag : "", ev.record);
    if (n > 0 && (size_t)n < sizeof j) send_uid(s->uid, NP_MSG_EVENT, j, (size_t)n, 1);
    if (ev.verdict == NP_V_DENY)
        logf_("uid %u %s: denegada %s %s %s:%u (%s%s%s)", s->uid, sc->app, ev.dir == NP_D_IN ? "entrada" : "salida",
              ev.proto == NP_P_TCP ? "tcp" : ev.proto == NP_P_UDP ? "udp" : "otro", addr[0] ? addr : "?", ev.port, by_name(ev.reason),
              ev.flag ? " " : "", ev.flag && flag ? flag : "");
}

static void push_counts(void) {
    for (int i = 0; i < G.nsess; i++) {
        Session *s = &G.sess[i];
        int changed = s->dirty_counts;
        for (int k = 0; k < s->nsc; k++) {
            NpCount c = { 0, 0 };
            if (G.bpf_ok) bpfs_map_lookup(G.m_counts, &s->sc[k].cgid, &c);
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

/* ---- nombres del resolvedor ------------------------------------------------------------------ */

static void on_dns(void *u, const uint8_t *pkt, size_t len) {
    (void)u;
    long long now = mono();
    if (dnsmap_learn(G.names, NAMES_APP, pkt, len, now) <= 0 || !G.bpf_ok) return;
    DnsResponse *r = malloc(sizeof *r);
    if (!r || dns_parse_response(pkt, len, r)) { free(r); return; }
    /* solo los nodos de estas direcciones, para las apps con reglas de dominio (antes de que la
     * app conecte: resolved avisa al monitor antes de contestarle a la app) */
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
                if (!*has) { *has = 1; bpfs_map_update(G.m_apps, &sc->cgid, &sc->app_v, BPF_ANY); }
            }
        }
    }
    free(r);
}

/* ---- BPF ------------------------------------------------------------------------------------- */

static int bpf_setup(void) {
#ifndef NP_HAVE_BPF
    snprintf(G.bpf_err, sizeof G.bpf_err, "compilado sin los programas BPF (BPF=0: falta clang)");
    return -1;
#else
    G.m_cfg = bpfs_map_create(BPF_MAP_TYPE_ARRAY, 4, sizeof(NpCfg), 1, 0, "np_cfg");
    G.m_apps = bpfs_map_create(BPF_MAP_TYPE_HASH, 8, sizeof(NpApp), NP_MAX_APPS, 0, "np_apps");
    G.m_union = bpfs_map_create(BPF_MAP_TYPE_HASH, 8, sizeof(NpApp), MAX_SESS, 0, "np_union");
    G.m_lpm4 = bpfs_map_create(BPF_MAP_TYPE_LPM_TRIE, sizeof(NpLpm4Key), sizeof(NpList), NP_MAX_LPM, BPF_F_NO_PREALLOC, "np_lpm4");
    G.m_lpm6 = bpfs_map_create(BPF_MAP_TYPE_LPM_TRIE, sizeof(NpLpm6Key), sizeof(NpList), NP_MAX_LPM, BPF_F_NO_PREALLOC, "np_lpm6");
    G.m_seen = bpfs_map_create(BPF_MAP_TYPE_LRU_HASH, sizeof(NpSeenKey), 8, NP_MAX_SEEN, 0, "np_seen");
    G.m_counts = bpfs_map_create(BPF_MAP_TYPE_HASH, 8, sizeof(NpCount), NP_MAX_APPS, 0, "np_counts");
    G.m_events = bpfs_map_create(BPF_MAP_TYPE_RINGBUF, 0, 0, NP_RINGBUF, 0, "np_events");
    if (G.m_cfg < 0 || G.m_apps < 0 || G.m_union < 0 || G.m_lpm4 < 0 || G.m_lpm6 < 0 || G.m_seen < 0 || G.m_counts < 0 || G.m_events < 0) {
        snprintf(G.bpf_err, sizeof G.bpf_err, "no se pudieron crear los mapas BPF: %s", strerror(errno));
        return -1;
    }
    if (bpfs_ring_open(&G.ring, G.m_events, NP_RINGBUF)) {
        snprintf(G.bpf_err, sizeof G.bpf_err, "no se pudo mapear el anillo: %s", strerror(errno));
        return -1;
    }
    static const struct { const char *sec; uint32_t type, attach; } want[] = {
        { "cgroup/connect4", BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_INET4_CONNECT },
        { "cgroup/connect6", BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_INET6_CONNECT },
        { "cgroup/sendmsg4", BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_UDP4_SENDMSG },
        { "cgroup/sendmsg6", BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_UDP6_SENDMSG },
        { "cgroup/bind4", BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_INET4_BIND },
        { "cgroup/bind6", BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_INET6_BIND },
        { "cgroup/sock_create", BPF_PROG_TYPE_CGROUP_SOCK, BPF_CGROUP_INET_SOCK_CREATE },
    };
    static char log[1 << 20];
    int np = 0;
    for (size_t w = 0; w < sizeof want / sizeof want[0]; w++) {
        const NpProgDesc *d = NULL;
        for (int i = 0; i < NP_NPROGS; i++) if (!strcmp(np_progs[i].section, want[w].sec)) d = &np_progs[i];
        if (!d) { snprintf(G.bpf_err, sizeof G.bpf_err, "falta el programa %s", want[w].sec); return -1; }
        int fd = bpfs_prog_load(want[w].type, want[w].attach, d, map_fd, log, sizeof log);
        if (fd < 0) {
            int e = errno;
            size_t ll = strlen(log);
            fprintf(stderr, "%s\n", ll > 3000 ? log + ll - 3000 : log);
            snprintf(G.bpf_err, sizeof G.bpf_err, "el kernel no aceptó %s: %s", want[w].sec, strerror(e));
            return -1;
        }
        G.attach[np] = want[w].attach;
        G.progs[np++] = fd;
    }
    G.nprogs = np;
    NpCfg cfg = { SCOPE_LEVEL, 1, USER_LEVEL };
    uint32_t zero = 0;
    if (bpfs_map_update(G.m_cfg, &zero, &cfg, BPF_ANY)) { snprintf(G.bpf_err, sizeof G.bpf_err, "np_cfg: %s", strerror(errno)); return -1; }
    logf_("%d programas BPF cargados", np);
    return 0;
#endif
}

/* Engancha los programas en la cgroup de lordos.service. systemd la borra y la vuelve a crear
 * cada vez que el servicio reinicia (y puede no existir todavía al arrancar), así que esto se
 * llama al empezar, con cada aviso de inotify en system.slice y una vez por segundo: si la cgroup
 * es otra (otro id), se sueltan los links viejos y se enganchan de nuevo. */
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
    if (!id) { logf_("esperando a lordos.service (su cgroup no existe)"); return; }
    G.cgfd = open(CG_ROOT "/" CG_SERVICE, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (G.cgfd < 0) return;
    for (int i = 0; i < G.nprogs; i++) {
        int l = bpfs_link_create(G.progs[i], G.cgfd, G.attach[i]);
        if (l < 0) {
            logf_("no se pudo enganchar un programa: %s", strerror(errno));
            for (int k = 0; k < G.nlinks; k++) close(G.links[k]);
            G.nlinks = 0;
            return;
        }
        G.links[G.nlinks++] = l;
    }
    G.attached_cg = id;
    if (G.root_wd >= 0) inotify_rm_watch(G.ifd, G.root_wd);
    G.root_wd = inotify_add_watch(G.ifd, CG_ROOT "/" CG_SERVICE, IN_CREATE | IN_DELETE | IN_ONLYDIR);
    logf_("%d programas BPF enganchados en " CG_SERVICE " (scopes en el nivel %d)", G.nlinks, SCOPE_LEVEL);
}

/* ---- principal ------------------------------------------------------------------------------- */

static volatile sig_atomic_t quit;
static void on_term(int s) { (void)s; quit = 1; }

static int listen_sock(void) {
    mkdir("/run/lordos", 0755);
    struct stat st;
    if (lstat("/run/lordos", &st) || !S_ISDIR(st.st_mode) || st.st_uid != 0) { logf_("/run/lordos no es una carpeta de root"); return -1; }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", NP_SOCK_PATH);
    unlink(NP_SOCK_PATH);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) || chmod(NP_SOCK_PATH, 0666) || listen(fd, 16)) {
        logf_("no se pudo abrir %s: %s", NP_SOCK_PATH, strerror(errno));
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
    if (argc != 1) { fprintf(stderr, "lord-netperm no recibe argumentos (ver docs/FIREWALL.md)\n"); return 2; }
    if (geteuid() != 0) { fprintf(stderr, "lord-netperm: tiene que correr como root (lord-netperm.service)\n"); return 1; }
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
    if (!G.empty || !G.names || G.ep < 0 || G.ifd < 0 || G.tfd < 0 || G.lfd < 0) { logf_("no se pudo arrancar"); return 1; }

    G.bpf_ok = bpf_setup() == 0;
    if (!G.bpf_ok) logf_("BPF no disponible (%s): no se aplica ninguna regla; el escritorio sigue igual", G.bpf_err);
    else G.dns = npdns_new(on_dns, NULL);

    struct itimerspec its = { { 1, 0 }, { 1, 0 } };
    timerfd_settime(G.tfd, 0, &its, NULL);
    struct epoll_event ev = { .events = EPOLLIN, .data.u64 = 1 };
    epoll_ctl(G.ep, EPOLL_CTL_ADD, G.lfd, &ev);
    ev.data.u64 = 2;
    epoll_ctl(G.ep, EPOLL_CTL_ADD, G.ifd, &ev);
    ev.data.u64 = 3;
    epoll_ctl(G.ep, EPOLL_CTL_ADD, G.tfd, &ev);
    if (G.bpf_ok) { ev.data.u64 = 4; epoll_ctl(G.ep, EPOLL_CTL_ADD, G.m_events, &ev); }
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
                attach_service();                   /* ¿lordos.service volvió a empezar? */
                scan_all();                         /* una scope o una sesión nueva o que se fue */
            } else if (t == 3) {
                uint64_t x;
                if (read(G.tfd, &x, sizeof x) < 0) {}
                G.tick++;
                attach_service();
                if (G.dns) { npdns_tick(G.dns); watch_dns(); }
                if (G.tick % 5 == 0) scan_all();    /* por si se perdió un aviso de inotify */
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
    logf_("saliendo: los programas se desenganchan (todo queda permitido)");
    unlink(NP_SOCK_PATH);
    return 0;
}

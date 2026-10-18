/* mon.c — sampling of system metrics on its own thread (see mon.h and docs/MONITOR.md).
 *
 * Sources, all read-only and readable by the session user:
 *   /proc/stat (total and per-core CPU), /proc/meminfo, /proc/loadavg, /proc/uptime,
 *   /proc/net/dev (traffic per interface), /proc/diskstats + /sys/block (reads and writes per
 *   disk), /sys/class/hwmon and /sys/class/thermal (temperature and fans),
 *   /proc/<pid>/{stat,cmdline,exe} (processes) and sock_diag (connections, in conn.c).
 *
 * The thread builds ready-made JSON; the screen thread only copies the latest (mon_*_json). No
 * program is executed. */
#include "mon.h"
#include "monpriv.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TICK_MS 250                     /* 4 samples per second, like TangoOS's Monitor */
#define IDLE_MS 3000                    /* no queries for 3 s → the thread goes to sleep */
#define MAX_CPU 256
#define MAX_IF 32
#define MAX_DISK 32
#define MAX_PROC 8192

/* ---- utilities ------------------------------------------------------------------------ */

double mon_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

double mon_wall_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

long mon_read_small(const char *path, char *out, size_t n) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t got = 0;
    while (got + 1 < n) {
        ssize_t r = read(fd, out + got, n - 1 - got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    out[got] = 0;
    return (long)got;
}

static void mon_buf_grow(Buf *b, size_t extra) {
    if (b->bad || b->n + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < b->n + extra + 1) cap *= 2;
    char *p = realloc(b->p, cap);
    if (!p) { b->bad = 1; return; }
    b->p = p;
    b->cap = cap;
}

void mon_buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[512];
    int k = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (k < 0) { b->bad = 1; return; }
    mon_buf_grow(b, (size_t)k);
    if (b->bad) return;
    if ((size_t)k < sizeof tmp) memcpy(b->p + b->n, tmp, (size_t)k + 1);
    else {
        va_start(ap, fmt);
        vsnprintf(b->p + b->n, (size_t)k + 1, fmt, ap);
        va_end(ap);
    }
    b->n += (size_t)k;
}

/* Length of a valid UTF-8 sequence at s (0 if invalid). */
static int utf8_len(const unsigned char *s) {
    if (s[0] < 0x80) return 1;
    int n = s[0] >= 0xf0 && s[0] <= 0xf4 ? 4 : s[0] >= 0xe0 ? 3 : s[0] >= 0xc2 && s[0] < 0xe0 ? 2 : 0;
    for (int i = 1; i < n; i++) if ((s[i] & 0xc0) != 0x80) return 0;
    return n;
}

void mon_buf_str(Buf *b, const char *s) {
    mon_buf_grow(b, strlen(s) * 6 + 2);
    if (b->bad) return;
    char *o = b->p + b->n;
    *o++ = '"';
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        if (*p == '"' || *p == '\\') { *o++ = '\\'; *o++ = (char)*p++; continue; }
        if (*p < 0x20 || *p == 0x7f) { o += sprintf(o, "\\u%04x", *p++); continue; }
        int n = utf8_len(p);
        if (!n) { *o++ = '?'; p++; continue; }
        memcpy(o, p, (size_t)n);
        o += n;
        p += n;
    }
    *o++ = '"';
    b->n = (size_t)(o - b->p);
    b->p[b->n] = 0;
}

void mon_buf_free(Buf *b) { free(b->p); memset(b, 0, sizeof *b); }

/* ---- thread state ---------------------------------------------------------------------- */

static struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int started, refs, want_procs, want_conns, want_geo;
    double last_poll;
    char *sample, *procs, *conns;
    uint64_t sseq, pseq, cseq;
    char geo_path[PATH_MAX];
} M = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

/* ---- CPU, memory, load ------------------------------------------------------------------- */

typedef struct { unsigned long long total, busy; } CpuT;

static struct {
    double t;                           /* mon_now_ms of the previous sample (0 = none) */
    CpuT cpu[MAX_CPU + 1];
    int ncpu;
    struct { char name[32]; unsigned long long rx, tx; } ifs[MAX_IF];
    int nif;
    struct { char name[32]; unsigned long long rd, wr; } disks[MAX_DISK];
    int ndisk;
} prev;

/* Disks: model and size (read once per thread start). */
static struct { char name[32], model[96]; unsigned long long size; } dinfo[MAX_DISK];
static int ndinfo;

/* Sensors: paths found at startup. */
static char temp_path[8][PATH_MAX], fan_path[8][PATH_MAX];
static int ntemp, nfan;

static unsigned long long ull(const char *s) { return strtoull(s, NULL, 10); }

static void cpu_sample(Buf *b, double dt) {
    static char txt[65536];
    if (mon_read_small("/proc/stat", txt, sizeof txt) <= 0) { mon_buf_printf(b, "\"cpu\":0,\"cores\":[]"); return; }
    CpuT now[MAX_CPU + 1];
    int n = 0;
    for (char *line = txt; line && *line && n <= MAX_CPU; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
        if (strncmp(line, "cpu", 3)) break;
        unsigned long long v[10] = { 0 };
        char *p = line + 3;
        while (*p && *p != ' ') p++;
        for (int i = 0; i < 10; i++) v[i] = strtoull(p, &p, 10);
        unsigned long long idle = v[3] + v[4], total = 0;
        for (int i = 0; i < 8; i++) total += v[i];              /* guest is already included in user */
        now[n].total = total;
        now[n].busy = total - idle;
        n++;
    }
    double use[MAX_CPU + 1];
    for (int i = 0; i < n; i++) {
        use[i] = 0;
        if (prev.t > 0 && i < prev.ncpu && dt > 0) {
            unsigned long long dtot = now[i].total - prev.cpu[i].total, dbusy = now[i].busy - prev.cpu[i].busy;
            if (now[i].total >= prev.cpu[i].total && dtot) use[i] = (double)dbusy / (double)dtot;
        }
        if (use[i] < 0) use[i] = 0;
        if (use[i] > 1) use[i] = 1;
    }
    memcpy(prev.cpu, now, sizeof(CpuT) * (size_t)n);
    prev.ncpu = n;
    mon_buf_printf(b, "\"cpu\":%.4f,\"cores\":[", n ? use[0] : 0.0);
    for (int i = 1; i < n; i++) mon_buf_printf(b, "%s%.4f", i > 1 ? "," : "", use[i]);
    mon_buf_printf(b, "]");
}

static void mem_sample(Buf *b) {
    char txt[8192];
    unsigned long long total = 0, avail = 0, buffers = 0, cached = 0, srec = 0, shmem = 0, swt = 0, swf = 0;
    if (mon_read_small("/proc/meminfo", txt, sizeof txt) > 0) {
        for (char *l = txt; l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
            char *c = strchr(l, ':');
            if (!c) continue;
            unsigned long long v = ull(c + 1) * 1024;
            size_t k = (size_t)(c - l);
#define IS(name) (k == sizeof(name) - 1 && !strncmp(l, name, k))
            if (IS("MemTotal")) total = v;
            else if (IS("MemAvailable")) avail = v;
            else if (IS("Buffers")) buffers = v;
            else if (IS("Cached")) cached = v;
            else if (IS("SReclaimable")) srec = v;
            else if (IS("Shmem")) shmem = v;
            else if (IS("SwapTotal")) swt = v;
            else if (IS("SwapFree")) swf = v;
#undef IS
        }
    }
    unsigned long long used = total > avail ? total - avail : 0;
    unsigned long long cache = buffers + cached + srec;
    cache = cache > shmem ? cache - shmem : 0;
    if (cache > total - used) cache = total - used;
    mon_buf_printf(b, ",\"mem\":{\"total\":%llu,\"used\":%llu,\"cache\":%llu,\"avail\":%llu,\"swapTotal\":%llu,\"swapUsed\":%llu}",
               total, used, cache, avail, swt, swt > swf ? swt - swf : 0);
}

static void load_sample(Buf *b) {
    char txt[256];
    double l1 = 0, l5 = 0, l15 = 0, up = 0;
    int run = 0, all = 0;
    if (mon_read_small("/proc/loadavg", txt, sizeof txt) > 0) sscanf(txt, "%lf %lf %lf %d/%d", &l1, &l5, &l15, &run, &all);
    if (mon_read_small("/proc/uptime", txt, sizeof txt) > 0) sscanf(txt, "%lf", &up);
    mon_buf_printf(b, ",\"load\":[%.2f,%.2f,%.2f],\"tasks\":{\"running\":%d,\"total\":%d},\"uptime\":%.0f", l1, l5, l15, run, all, up);
}

/* ---- network ------------------------------------------------------------------------------ */

static double rate(unsigned long long now, unsigned long long before, double dt) {
    return prev.t > 0 && dt > 0 && now >= before ? (double)(now - before) * 1000.0 / dt : 0;
}

static void net_sample(Buf *b, double dt) {
    char txt[16384];
    if (mon_read_small("/proc/net/dev", txt, sizeof txt) <= 0) { mon_buf_printf(b, ",\"net\":{\"rx\":0,\"tx\":0,\"ifs\":[]}"); return; }
    struct { char name[32]; unsigned long long rx, tx; } cur[MAX_IF];
    int n = 0;
    for (char *l = txt; l && n < MAX_IF; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
        char *c = strchr(l, ':');
        if (!c || c - l > 40) continue;
        char *s = l;
        while (*s == ' ') s++;
        if (c - s <= 0 || c - s >= 32) continue;
        snprintf(cur[n].name, sizeof cur[n].name, "%.*s", (int)(c - s), s);
        unsigned long long v[16];
        char *p = c + 1;
        for (int i = 0; i < 16; i++) v[i] = strtoull(p, &p, 10);
        cur[n].rx = v[0];
        cur[n].tx = v[8];
        n++;
    }
    double rx = 0, tx = 0;
    unsigned long long rxt = 0, txt_ = 0;
    Buf ifs = { 0 };
    for (int i = 0; i < n; i++) {
        double r = 0, t = 0;
        for (int j = 0; j < prev.nif; j++)
            if (!strcmp(prev.ifs[j].name, cur[i].name)) { r = rate(cur[i].rx, prev.ifs[j].rx, dt); t = rate(cur[i].tx, prev.ifs[j].tx, dt); }
        if (strcmp(cur[i].name, "lo")) { rx += r; tx += t; rxt += cur[i].rx; txt_ += cur[i].tx; }
        mon_buf_printf(&ifs, "%s{\"name\":", i ? "," : "");
        mon_buf_str(&ifs, cur[i].name);
        mon_buf_printf(&ifs, ",\"rx\":%.0f,\"tx\":%.0f,\"rxTotal\":%llu,\"txTotal\":%llu}", r, t, cur[i].rx, cur[i].tx);
    }
    memcpy(prev.ifs, cur, sizeof cur[0] * (size_t)n);
    prev.nif = n;
    mon_buf_printf(b, ",\"net\":{\"rx\":%.0f,\"tx\":%.0f,\"rxTotal\":%llu,\"txTotal\":%llu,\"ifs\":[%s]}", rx, tx, rxt, txt_, ifs.p && !ifs.bad ? ifs.p : "");
    mon_buf_free(&ifs);
}

/* ---- disks -------------------------------------------------------------------------------- */

/* Whole disk (not a partition, nor loop/ram/zram/device-mapper): it's in /sys/block and has device/. */
static int whole_disk(const char *name) {
    if (!strncmp(name, "loop", 4) || !strncmp(name, "ram", 3) || !strncmp(name, "zram", 4) || !strncmp(name, "dm-", 3)) return 0;
    char p[PATH_MAX];
    struct stat st;
    snprintf(p, sizeof p, "/sys/block/%s/device", name);
    return !stat(p, &st);
}

static void disk_info_scan(void) {
    ndinfo = 0;
    DIR *d = opendir("/sys/block");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && ndinfo < MAX_DISK) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= 32 || !whole_disk(e->d_name)) continue;
        char p[PATH_MAX], t[128];
        snprintf(dinfo[ndinfo].name, sizeof dinfo[0].name, "%s", e->d_name);
        snprintf(p, sizeof p, "/sys/block/%s/device/model", e->d_name);
        if (mon_read_small(p, t, sizeof t) <= 0) t[0] = 0;
        size_t k = strlen(t);
        while (k && isspace((unsigned char)t[k - 1])) t[--k] = 0;
        snprintf(dinfo[ndinfo].model, sizeof dinfo[0].model, "%s", t);
        snprintf(p, sizeof p, "/sys/block/%s/size", e->d_name);
        dinfo[ndinfo].size = mon_read_small(p, t, sizeof t) > 0 ? ull(t) * 512 : 0;
        ndinfo++;
    }
    closedir(d);
}

static void disk_sample(Buf *b, double dt) {
    static char txt[65536];
    struct { char name[32]; unsigned long long rd, wr; } cur[MAX_DISK];
    int n = 0;
    if (mon_read_small("/proc/diskstats", txt, sizeof txt) > 0) {
        for (char *l = txt; l && *l && n < MAX_DISK; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
            unsigned maj, min;
            char name[64];
            unsigned long long f[11];
            if (sscanf(l, "%u %u %63s %llu %llu %llu %llu %llu %llu %llu %llu", &maj, &min, name,
                       &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7]) != 11) continue;
            int known = 0;
            for (int i = 0; i < ndinfo; i++) if (!strcmp(dinfo[i].name, name)) known = 1;
            if (!known) continue;
            snprintf(cur[n].name, sizeof cur[n].name, "%.31s", name);
            cur[n].rd = f[2] * 512;                 /* sectors read */
            cur[n].wr = f[6] * 512;                 /* sectors written */
            n++;
        }
    }
    double rd = 0, wr = 0;
    Buf list = { 0 };
    for (int i = 0; i < n; i++) {
        double r = 0, w = 0;
        for (int j = 0; j < prev.ndisk; j++)
            if (!strcmp(prev.disks[j].name, cur[i].name)) { r = rate(cur[i].rd, prev.disks[j].rd, dt); w = rate(cur[i].wr, prev.disks[j].wr, dt); }
        rd += r;
        wr += w;
        const char *model = "";
        unsigned long long size = 0;
        for (int k = 0; k < ndinfo; k++) if (!strcmp(dinfo[k].name, cur[i].name)) { model = dinfo[k].model; size = dinfo[k].size; }
        mon_buf_printf(&list, "%s{\"name\":", i ? "," : "");
        mon_buf_str(&list, cur[i].name);
        mon_buf_printf(&list, ",\"model\":");
        mon_buf_str(&list, model);
        mon_buf_printf(&list, ",\"size\":%llu,\"read\":%.0f,\"write\":%.0f,\"readTotal\":%llu,\"writeTotal\":%llu}", size, r, w, cur[i].rd, cur[i].wr);
    }
    memcpy(prev.disks, cur, sizeof cur[0] * (size_t)n);
    prev.ndisk = n;
    mon_buf_printf(b, ",\"disk\":{\"read\":%.0f,\"write\":%.0f,\"disks\":[%s]}", rd, wr, list.p && !list.bad ? list.p : "");
    mon_buf_free(&list);
}

/* ---- sensors ------------------------------------------------------------------------------ */

static int cpu_chip(const char *name) {
    static const char *const chips[] = { "coretemp", "k10temp", "zenpower", "cpu_thermal", "soc_thermal", "acpitz" };
    for (int i = 0; i < 6; i++) if (!strcmp(name, chips[i])) return 6 - i;   /* higher = better */
    return 0;
}

static void sensors_scan(void) {
    ntemp = nfan = 0;
    int best = 0;
    char best_dir[PATH_MAX] = "";
    DIR *d = opendir("/sys/class/hwmon");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strncmp(e->d_name, "hwmon", 5)) continue;
            char p[PATH_MAX], name[64];
            snprintf(p, sizeof p, "/sys/class/hwmon/%s/name", e->d_name);
            if (mon_read_small(p, name, sizeof name) <= 0) continue;
            name[strcspn(name, "\n")] = 0;
            int score = cpu_chip(name);
            if (score > best) { best = score; snprintf(best_dir, sizeof best_dir, "/sys/class/hwmon/%s", e->d_name); }
            for (int i = 1; i <= 8 && nfan < 8; i++) {
                struct stat st;
                snprintf(p, sizeof p, "/sys/class/hwmon/%s/fan%d_input", e->d_name, i);
                if (!stat(p, &st)) snprintf(fan_path[nfan++], PATH_MAX, "%s", p);
            }
        }
        closedir(d);
    }
    if (best_dir[0]) {
        for (int i = 1; i <= 32 && ntemp < 8; i++) {
            char p[PATH_MAX];
            struct stat st;
            snprintf(p, sizeof p, "%s/temp%d_input", best_dir, i);
            if (!stat(p, &st)) snprintf(temp_path[ntemp++], PATH_MAX, "%s", p);
        }
    }
    if (!ntemp) {                                   /* fallback: the package's thermal zone */
        for (int i = 0; i < 16; i++) {
            char p[PATH_MAX], type[64];
            snprintf(p, sizeof p, "/sys/class/thermal/thermal_zone%d/type", i);
            if (mon_read_small(p, type, sizeof type) <= 0) continue;
            if (strncmp(type, "x86_pkg_temp", 12) && strncmp(type, "cpu", 3) && ntemp) continue;
            snprintf(temp_path[0], PATH_MAX, "/sys/class/thermal/thermal_zone%d/temp", i);
            ntemp = 1;
            if (!strncmp(type, "x86_pkg_temp", 12)) break;
        }
    }
}

static void sensors_sample(Buf *b) {
    double temp = -1;
    long fan = -1;
    for (int i = 0; i < ntemp; i++) {         /* the highest from the CPU chip (package or core) */
        char t[32];
        if (mon_read_small(temp_path[i], t, sizeof t) > 0) { double v = strtol(t, NULL, 10) / 1000.0; if (v > temp && v < 150) temp = v; }
    }
    for (int i = 0; i < nfan; i++) {
        char t[32];
        if (mon_read_small(fan_path[i], t, sizeof t) > 0) { long v = strtol(t, NULL, 10); if (v > fan) fan = v; }
    }
    if (temp >= 0) mon_buf_printf(b, ",\"temp\":%.1f", temp); else mon_buf_printf(b, ",\"temp\":null");
    if (fan >= 0) mon_buf_printf(b, ",\"fan\":%ld", fan); else mon_buf_printf(b, ",\"fan\":null");
}

/* ---- processes ---------------------------------------------------------------------------- */

typedef struct { int pid; unsigned long long start, ticks; } ProcPrev;
static ProcPrev *pprev, *pcur;
static int npprev;
static double pprev_t;

static struct { unsigned uid; char name[33]; } users[64];
static int nusers;

static const char *user_name(unsigned uid) {
    for (int i = 0; i < nusers; i++) if (users[i].uid == uid) return users[i].name;
    struct passwd pw, *r = NULL;
    char tmp[2048];
    int slot = nusers < 64 ? nusers++ : (int)(uid % 64);
    users[slot].uid = uid;
    if (!getpwuid_r(uid, &pw, tmp, sizeof tmp, &r) && r) snprintf(users[slot].name, sizeof users[slot].name, "%s", r->pw_name);
    else snprintf(users[slot].name, sizeof users[slot].name, "%u", uid);
    return users[slot].name;
}

static int cmp_pid(const void *a, const void *b) { return ((const ProcPrev *)a)->pid - ((const ProcPrev *)b)->pid; }

static void procs_scan(Buf *b) {
    if (!pprev) pprev = calloc(MAX_PROC, sizeof *pprev);
    if (!pcur) pcur = calloc(MAX_PROC, sizeof *pcur);
    if (!pprev || !pcur) { b->bad = 1; return; }
    double now = mon_now_ms(), dt = pprev_t > 0 ? (now - pprev_t) / 1000.0 : 0;
    long hz = sysconf(_SC_CLK_TCK), page = sysconf(_SC_PAGESIZE);
    unsigned me = (unsigned)getuid();
    int self = (int)getpid(), n = 0;
    DIR *d = opendir("/proc");
    if (!d) { b->bad = 1; return; }
    mon_buf_printf(b, "{\"t\":%.0f,\"list\":[", mon_wall_ms());
    struct dirent *e;
    while ((e = readdir(d)) && n < MAX_PROC) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        int pid = atoi(e->d_name);
        char p[64], st[1024];
        snprintf(p, sizeof p, "/proc/%d/stat", pid);
        if (mon_read_small(p, st, sizeof st) <= 0) continue;
        char *lp = strchr(st, '('), *rp = strrchr(st, ')');
        if (!lp || !rp || rp < lp || rp[1] != ' ') continue;
        char comm[64];
        snprintf(comm, sizeof comm, "%.*s", (int)(rp - lp - 1 < 63 ? rp - lp - 1 : 63), lp + 1);
        /* fields from 3 (state) onward */
        char *f = rp + 2, state = *f;
        unsigned long long v[24] = { 0 };
        char *q = f + 1;
        for (int i = 0; i < 21; i++) v[i] = strtoull(q, &q, 10);
        /* v[0]=ppid(4) … v[10]=utime(14) v[11]=stime(15) v[16]=threads(20) v[18]=starttime(22) v[20]=rss(24) */
        unsigned long long ticks = v[10] + v[11], start = v[18];
        struct stat sb;
        snprintf(p, sizeof p, "/proc/%d", pid);
        unsigned uid = stat(p, &sb) ? 0 : (unsigned)sb.st_uid;
        char cmd[256] = "";
        snprintf(p, sizeof p, "/proc/%d/cmdline", pid);
        if (mon_read_small(p, cmd, sizeof cmd) > 0) {
            char *base = strrchr(cmd, '/');
            if (base) memmove(cmd, base + 1, strlen(base + 1) + 1);
        }
        char exe[PATH_MAX] = "";
        if (uid == me) {
            snprintf(p, sizeof p, "/proc/%d/exe", pid);
            ssize_t k = readlink(p, exe, sizeof exe - 1);
            exe[k > 0 ? k : 0] = 0;
        }
        double cpu = 0;
        ProcPrev key = { pid, 0, 0 }, *old = npprev ? bsearch(&key, pprev, (size_t)npprev, sizeof key, cmp_pid) : NULL;
        if (old && old->start == start && ticks >= old->ticks && dt > 0) cpu = (double)(ticks - old->ticks) / ((double)hz * dt);
        pcur[n].pid = pid;
        pcur[n].start = start;
        pcur[n].ticks = ticks;
        n++;
        mon_buf_printf(b, "%s{\"pid\":%d,\"ppid\":%llu,\"uid\":%u,\"user\":", n > 1 ? "," : "", pid, v[0], uid);
        mon_buf_str(b, user_name(uid));
        mon_buf_printf(b, ",\"name\":");
        mon_buf_str(b, comm);
        mon_buf_printf(b, ",\"cmd\":");
        mon_buf_str(b, cmd);
        mon_buf_printf(b, ",\"exe\":");
        mon_buf_str(b, exe);
        mon_buf_printf(b, ",\"state\":\"%c\",\"threads\":%llu,\"cpu\":%.4f,\"rss\":%llu,\"start\":%.0f,\"self\":%s}",
                   isalpha((unsigned char)state) ? state : '?', v[16], cpu, v[20] * (unsigned long long)page,
                   (double)start / (double)hz, pid == self ? "true" : "false");
    }
    closedir(d);
    qsort(pcur, (size_t)n, sizeof *pcur, cmp_pid);
    ProcPrev *t = pprev;
    pprev = pcur;
    pcur = t;
    npprev = n;
    pprev_t = now;
    mon_buf_printf(b, "],\"total\":%d,\"hz\":%ld}", n, hz);
}

/* ---- thread ------------------------------------------------------------------------------ */

static void publish(char **slot, uint64_t *seq, Buf *b) {
    if (b->bad || !b->p) { mon_buf_free(b); return; }
    pthread_mutex_lock(&M.mu);
    free(*slot);
    *slot = b->p;
    (*seq)++;
    pthread_mutex_unlock(&M.mu);
    memset(b, 0, sizeof *b);
}

static void *worker(void *arg) {
    (void)arg;
    Mmdb geo;
    int geo_open = 0, geo_tried = 0;
    unsigned tick = 0;
    for (;;) {
        pthread_mutex_lock(&M.mu);
        int slept = 0;
        while (!(M.refs > 0 && mon_now_ms() - M.last_poll < IDLE_MS)) {
            slept = 1;
            pthread_cond_wait(&M.cv, &M.mu);
        }
        int wp = M.want_procs, wc = M.want_conns, wg = M.want_geo;
        pthread_mutex_unlock(&M.mu);
        if (slept || !tick) {                       /* (re)start: no stale rates */
            memset(&prev, 0, sizeof prev);
            npprev = 0;
            pprev_t = 0;
            conn_reset();
            disk_info_scan();
            sensors_scan();
            tick = 0;
        }
        double t0 = mon_now_ms(), dt = prev.t > 0 ? t0 - prev.t : 0;
        Buf b = { 0 };
        mon_buf_printf(&b, "{\"t\":%.0f,\"dt\":%.1f,", mon_wall_ms(), dt);
        cpu_sample(&b, dt);
        mem_sample(&b);
        load_sample(&b);
        net_sample(&b, dt);
        disk_sample(&b, dt);
        sensors_sample(&b);
        mon_buf_printf(&b, "}");
        prev.t = t0;
        publish(&M.sample, &M.sseq, &b);
        if (wp && tick % 4 == 0) {
            Buf p = { 0 };
            procs_scan(&p);
            publish(&M.procs, &M.pseq, &p);
        }
        if (wc && tick % 4 == 2) {
            if (wg && !geo_tried) { geo_tried = 1; geo_open = M.geo_path[0] && !mmdb_open(&geo, M.geo_path); }
            Buf c = { 0 };
            conn_scan(&c, geo_open && wg ? &geo : NULL);
            publish(&M.conns, &M.cseq, &c);
        }
        tick++;
        /* the country database (~4 MB in memory) is released if nobody is looking at connections */
        if ((!wc || !wg) && geo_open) { mmdb_close(&geo); geo_open = 0; geo_tried = 0; }
        double wait = TICK_MS - (mon_now_ms() - t0);
        if (wait > 0) {
            struct timespec ts = { 0, (long)(wait * 1e6) };
            while (nanosleep(&ts, &ts) && errno == EINTR) {}
        }
        pthread_mutex_lock(&M.mu);
        int idle = !(M.refs > 0 && mon_now_ms() - M.last_poll < IDLE_MS);
        pthread_mutex_unlock(&M.mu);
        if (idle && geo_open) { mmdb_close(&geo); geo_open = 0; geo_tried = 0; }
    }
    return NULL;
}

void mon_init(const char *assets_dir) {
    pthread_mutex_lock(&M.mu);
    if (assets_dir && *assets_dir) snprintf(M.geo_path, sizeof M.geo_path, "%s/monitor/geoip-country.mmdb.xz", assets_dir);
    pthread_mutex_unlock(&M.mu);
}

void mon_reset(void) {
    pthread_mutex_lock(&M.mu);
    M.refs = 0;
    M.want_procs = M.want_conns = M.want_geo = 0;
    pthread_mutex_unlock(&M.mu);
}

void mon_start(void) {
    pthread_mutex_lock(&M.mu);
    if (M.refs < 64) M.refs++;
    M.last_poll = mon_now_ms();
    if (!M.started) {
        pthread_t th;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&at, 512 * 1024);
        if (!pthread_create(&th, &at, worker, NULL)) M.started = 1;
        pthread_attr_destroy(&at);
    }
    pthread_cond_signal(&M.cv);
    pthread_mutex_unlock(&M.mu);
}

void mon_stop(void) {
    pthread_mutex_lock(&M.mu);
    if (M.refs > 0) M.refs--;
    pthread_mutex_unlock(&M.mu);
}

void mon_want(int procs, int conns, int geo) {
    pthread_mutex_lock(&M.mu);
    M.want_procs = !!procs;
    M.want_conns = !!conns;
    M.want_geo = !!geo;
    pthread_mutex_unlock(&M.mu);
}

static char *take(char *slot, uint64_t have, uint64_t *seq) {
    pthread_mutex_lock(&M.mu);
    M.last_poll = mon_now_ms();
    pthread_cond_signal(&M.cv);
    char *r = NULL;
    if (slot && have != *seq) { r = strdup(slot); *seq = have; }
    pthread_mutex_unlock(&M.mu);
    return r;
}

char *mon_sample_json(uint64_t *seq) { return take(M.sample, M.sseq, seq); }
char *mon_procs_json(uint64_t *seq) { return take(M.procs, M.pseq, seq); }
char *mon_conns_json(uint64_t *seq) { return take(M.conns, M.cseq, seq); }

/* ---- fixed data ----------------------------------------------------------------------- */

/* "+DDMM" / "+DDDMM" / with seconds → degrees */
static double coord(const char *s, int deg_digits, int len) {
    if (len < 1 + deg_digits + 2) return 0;
    double sign = s[0] == '-' ? -1 : 1, v = 0;
    char t[8];
    snprintf(t, sizeof t, "%.*s", deg_digits, s + 1);
    v = atof(t);
    snprintf(t, sizeof t, "%.2s", s + 1 + deg_digits);
    v += atof(t) / 60;
    if (len >= 1 + deg_digits + 4) { snprintf(t, sizeof t, "%.2s", s + 3 + deg_digits); v += atof(t) / 3600; }
    return sign * v;
}

/* Approximate location of this machine: that of its time zone (tzdata's zone1970.tab). No network. */
static void home_json(Buf *b) {
    char zone[256] = "", link[PATH_MAX];
    const char *tz = getenv("TZ");
    if (tz && *tz) {
        const char *z = tz[0] == ':' ? tz + 1 : tz, *zi = strstr(z, "zoneinfo/");
        if (zi) z = zi + 9;
        if (strchr(z, '/') && z[0] != '/') snprintf(zone, sizeof zone, "%s", z);
    }
    if (!zone[0]) {
        ssize_t k = readlink("/etc/localtime", link, sizeof link - 1);
        if (k > 0) {
            link[k] = 0;
            char *z = strstr(link, "zoneinfo/");
            if (z) snprintf(zone, sizeof zone, "%s", z + 9);
        }
    }
    if (!zone[0] && mon_read_small("/etc/timezone", zone, sizeof zone) > 0) zone[strcspn(zone, "\n")] = 0;
    const char *tabs[] = { "/usr/share/zoneinfo/zone1970.tab", "/usr/share/zoneinfo/zone.tab" };
    for (int i = 0; i < 2 && zone[0]; i++) {
        FILE *f = fopen(tabs[i], "r");
        if (!f) continue;
        char line[512];
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#') continue;
            char *cc = strtok(line, "\t"), *co = strtok(NULL, "\t"), *name = strtok(NULL, "\t\n");
            if (!cc || !co || !name || strcmp(name, zone)) continue;
            char *mid = co + 1;
            while (*mid && *mid != '+' && *mid != '-') mid++;
            int la = (int)(mid - co), lo = (int)strlen(mid);
            mon_buf_printf(b, ",\"home\":{\"zone\":");
            mon_buf_str(b, zone);
            mon_buf_printf(b, ",\"cc\":\"%.2s\",\"lat\":%.3f,\"lon\":%.3f}", cc, coord(co, 2, la), coord(mid, 3, lo));
            fclose(f);
            return;
        }
        fclose(f);
    }
    mon_buf_printf(b, ",\"home\":null");
}

char *mon_info_json(void) {
    Buf b = { 0 };
    char txt[8192], model[128] = "";
    if (mon_read_small("/proc/cpuinfo", txt, sizeof txt) > 0) {
        char *m = strstr(txt, "model name");
        if (m && (m = strchr(m, ':'))) {
            m++;
            while (*m == ' ' || *m == '\t') m++;
            snprintf(model, sizeof model, "%.*s", (int)strcspn(m, "\n"), m);
        }
    }
    struct stat st;
    pthread_mutex_lock(&M.mu);
    int geo = M.geo_path[0] && !stat(M.geo_path, &st);
    pthread_mutex_unlock(&M.mu);
    mon_buf_printf(&b, "{\"cpuModel\":");
    mon_buf_str(&b, model);
    mon_buf_printf(&b, ",\"cores\":%ld,\"geo\":%s,\"tick\":%d", sysconf(_SC_NPROCESSORS_ONLN), geo ? "true" : "false", TICK_MS);
    home_json(&b);
    mon_buf_printf(&b, "}");
    if (b.bad) { mon_buf_free(&b); return NULL; }
    return b.p;
}

/* lbrun.c — see lbrun.h. */
#include "lbrun.h"
#include "lbcond.h"
#include "lbproto.h"
#include "bench.h"
#include "../hw/jbuf.h"
#include "lord_version.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { ST_IDLE, ST_WAITING, ST_RUNNING, ST_DONE, ST_STOPPED, ST_FAILED, ST_CANCELLED };
static const char *const st_name[] = { "idle", "waiting", "running", "done", "stopped", "failed", "cancelled" };

#define QUIET_SECS   10                 /* CPU below QUIET_PCT for this long before starting */
#define QUIET_PCT    15.0
#define QUIET_MAX_S  30                 /* after this, the run starts anyway and is marked busy */
#define MAX_SAMPLES  200

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int state = ST_IDLE, step = -1, done_count;
static volatile int cancel_req;
static pid_t child = -1;
static char *result_json;

typedef struct {
    int full, sustained_s;
    char *extra;
} Job;

/* ---- the helper's fixed path, next to lord-server, as for lord-doc and lord-media ---- */

static int helper_path(char *out, size_t n) {
    char self[PATH_MAX];
    ssize_t k = readlink("/proc/self/exe", self, sizeof self - 1);
    if (k <= 0) return -1;
    self[k] = 0;
    char *slash = strrchr(self, '/');
    if (!slash) return -1;
    *slash = 0;
    if ((size_t)snprintf(out, n, "%s/lord-bench", self) >= n) return -1;
    struct stat st;
    if (stat(out, &st) || !S_ISREG(st.st_mode) || !(st.st_mode & S_IXUSR)) return -1;
    if ((st.st_uid != 0 && st.st_uid != getuid()) || (st.st_mode & (S_IWGRP | S_IWOTH))) return -1;
    return 0;
}

/* ---- what lord-server opens for it ---- */

static int open_render_node(void) {
    for (int i = 128; i < 136; i++) {
        char p[32];
        snprintf(p, sizeof p, "/dev/dri/renderD%d", i);
        int fd = open(p, O_RDWR | O_CLOEXEC | O_NOCTTY);
        if (fd >= 0) return fd;
    }
    return -1;
}

/* the storage scratch file: an unnamed file (O_TMPFILE) in the user's cache folder, so nothing is left
 * behind if anything dies; its size is at most 1 GB and a quarter of the free space */
static int open_scratch(char *dir, size_t dn, unsigned *mb) {
    const char *xc = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    if (xc && xc[0] == '/') snprintf(dir, dn, "%s", xc);
    else if (home && home[0] == '/') snprintf(dir, dn, "%s/.cache", home);
    else return -1;
    mkdir(dir, 0700);
    int fd = open(dir, O_TMPFILE | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    struct statvfs vf;
    unsigned long long freemb = fstatvfs(fd, &vf) ? 0 : (unsigned long long)vf.f_bavail * vf.f_frsize / (1 << 20);
    unsigned long long m = freemb / 4;
    if (m > 1024) m = 1024;
    if (m < 64) { close(fd); return -1; }
    *mb = (unsigned)m;
    return fd;
}

/* ---- the result ---- */

typedef struct {
    double value[LB_COUNT], spread[LB_COUNT];
    int have[LB_COUNT], skip[LB_COUNT];
    int gpu_kind, sandbox;
    double sample_t[MAX_SAMPLES], sample_v[MAX_SAMPLES], sample_temp[MAX_SAMPLES];
    int nsamples;
    double throttle_at, throttle_temp;
} Res;

static void cond_json(HBuf *b, const LbCond *c) {
    hb_printf(b, "{");
    if (c->temp_c > -270) hb_printf(b, "\"tempC\":%.1f,", c->temp_c);
    if (c->zone[0]) { hb_printf(b, "\"zone\":"); hb_str(b, c->zone); hb_printf(b, ","); }
    if (c->passive_c > 0) hb_printf(b, "\"passiveC\":%.1f,", c->passive_c);
    if (c->critical_c > 0) hb_printf(b, "\"criticalC\":%.1f,", c->critical_c);
    if (c->throttle_known) hb_printf(b, "\"throttleEvents\":%lu,", c->throttle_events);
    if (c->rpi_throttled >= 0) hb_printf(b, "\"rpiThrottled\":%d,", c->rpi_throttled);
    if (c->freq_mhz > 0) hb_printf(b, "\"freqMHz\":%.0f,", c->freq_mhz);
    if (c->freq_max_mhz > 0) hb_printf(b, "\"freqMaxMHz\":%.0f,", c->freq_max_mhz);
    if (c->governor[0]) { hb_printf(b, "\"governor\":"); hb_str(b, c->governor); hb_printf(b, ","); }
    if (c->platform_profile[0]) { hb_printf(b, "\"platformProfile\":"); hb_str(b, c->platform_profile); hb_printf(b, ","); }
    hb_printf(b, "\"power\":\"%s\"", c->on_battery < 0 ? "unknown" : c->on_battery ? "battery" : "mains");
    if (c->battery_pct >= 0) hb_printf(b, ",\"batteryPct\":%d", c->battery_pct);
    hb_printf(b, "}");
}

static char *build_result(const Job *j, int st, const char *why, const Res *r, const LbCond *c0, const LbCond *c1,
                          double busy_pct, double waited, int quiet, int threads, const char *storage, unsigned storage_mb,
                          double secs) {
    HBuf b = { 0 };
    struct utsname u;
    if (uname(&u)) u.release[0] = u.machine[0] = 0;
    hb_printf(&b, "{\"lordbench\":%d,\"mode\":\"%s\",\"at\":%lld,\"seconds\":%.0f,\"status\":\"%s\",\"stopReason\":",
              LB_VERSION, j->full ? "full" : "quick", (long long)time(NULL), secs, st_name[st]);
    hb_str(&b, why ? why : "");
    hb_printf(&b, ",\"lordos\":\"%s%s%s\",\"kernel\":", LORD_VERSION, LORD_CHANNEL[0] ? "-" : "", LORD_CHANNEL);
    hb_str(&b, u.release);
    hb_printf(&b, ",\"arch\":");
    hb_str(&b, u.machine);
    hb_printf(&b, ",\"live\":%s,\"threads\":%d,\"cpus\":%ld", access("/run/live", F_OK) ? "false" : "true", threads, sysconf(_SC_NPROCESSORS_ONLN));
    hb_printf(&b, ",\"quiet\":{\"quiet\":%s,\"busyPct\":%.1f,\"waitedS\":%.0f}", quiet ? "true" : "false", busy_pct, waited);
    hb_printf(&b, ",\"before\":");
    cond_json(&b, c0);
    hb_printf(&b, ",\"after\":");
    cond_json(&b, c1);
    hb_printf(&b, ",\"storage\":{\"kind\":\"%s\",\"mb\":%u}", storage, storage_mb);
    hb_printf(&b, ",\"gpuKind\":\"%s\"", r->gpu_kind == 0 ? "hardware" : r->gpu_kind == 1 ? "virtual" : r->gpu_kind == 2 ? "software" : "");
    hb_printf(&b, ",\"sandbox\":%d", r->sandbox);
    if (j->extra) hb_printf(&b, ",\"session\":%s", j->extra);
    hb_printf(&b, ",\"results\":{");
    int first = 1;
    for (int i = 0; i < LB_COUNT; i++) if (r->have[i]) {
        hb_printf(&b, "%s\"%s\":{\"value\":%.6g,\"spread\":%.4g,\"unit\":\"%s\"%s}", first ? "" : ",", lb_name(i), r->value[i], r->spread[i],
                  lb_unit(i), lb_lower_better(i) ? ",\"lowerBetter\":true" : "");
        first = 0;
    }
    hb_printf(&b, "},\"skipped\":{");
    first = 1;
    static const char *const why_skip[] = { "", "nodev", "failed", "mode", "memory" };
    for (int i = 0; i < LB_COUNT; i++) if (!r->have[i] && r->skip[i]) {
        hb_printf(&b, "%s\"%s\":\"%s\"", first ? "" : ",", lb_name(i), why_skip[r->skip[i]]);
        first = 0;
    }
    hb_printf(&b, "}");
    if (r->nsamples) {
        hb_printf(&b, ",\"sustained\":{\"samples\":[");
        for (int i = 0; i < r->nsamples; i++)
            if (r->sample_temp[i] > -270) hb_printf(&b, "%s[%.1f,%.3g,%.1f]", i ? "," : "", r->sample_t[i], r->sample_v[i], r->sample_temp[i]);
            else hb_printf(&b, "%s[%.1f,%.3g,null]", i ? "," : "", r->sample_t[i], r->sample_v[i]);
        hb_printf(&b, "]");
        if (r->throttle_at >= 0) hb_printf(&b, ",\"throttleAtS\":%.0f,\"throttleTempC\":%.1f", r->throttle_at, r->throttle_temp);
        hb_printf(&b, "}");
    }
    hb_printf(&b, "}");
    return hb_take(&b);
}

/* ~/.local/state/lordos/lordbench.json, the previous one kept as lordbench-prev.json */
static const char *saving;
static void write_saving(FILE *f) { fputs(saving, f); fputc('\n', f); }
static int state_dir(char *out, size_t n) {
    const char *st = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
    if (st && st[0] == '/') snprintf(out, n, "%s/lordos", st);
    else if (home && home[0] == '/') snprintf(out, n, "%s/.local/state/lordos", home);
    else return -1;
    return 0;
}
static void save_result(const char *json) {
    char dir[PATH_MAX], a[PATH_MAX + 32], b[PATH_MAX + 32];
    if (!state_dir(dir, sizeof dir)) {
        snprintf(a, sizeof a, "%s/lordbench.json", dir);
        snprintf(b, sizeof b, "%s/lordbench-prev.json", dir);
        rename(a, b);
    }
    saving = json;
    bench_save("lordbench", write_saving);
}

char *lbrun_saved(int which) {
    char dir[PATH_MAX], p[PATH_MAX + 32];
    if (state_dir(dir, sizeof dir)) return NULL;
    snprintf(p, sizeof p, "%s/%s.json", dir, which ? "lordbench-prev" : "lordbench");
    int fd = open(p, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 256 * 1024) { close(fd); return NULL; }
    char *s = malloc((size_t)st.st_size + 1);
    ssize_t n = s ? read(fd, s, (size_t)st.st_size) : -1;
    close(fd);
    if (n <= 0) { free(s); return NULL; }
    s[n] = 0;
    return s;                                       /* the caller parses it as JSON (JS_ParseJSON) */
}

/* ---- the run ---- */

static void set_state(int s) { pthread_mutex_lock(&mu); state = s; pthread_mutex_unlock(&mu); }

static double cpu_busy(unsigned long long b0, unsigned long long t0, unsigned long long b1, unsigned long long t1) {
    return t1 > t0 ? 100.0 * (double)(b1 - b0) / (double)(t1 - t0) : 0;
}

static void *run(void *arg) {
    Job *j = arg;
    Res *r = calloc(1, sizeof *r);
    LbCond c0, c1, cn;
    char why_buf[16] = "";
    const char *why = NULL;
    int st = ST_FAILED, threads = (int)sysconf(_SC_NPROCESSORS_ONLN), sock = -1, gpu = -1, disk = -1, quiet = 0;
    unsigned storage_mb = 0;
    char scratch_dir[PATH_MAX] = "";
    const char *storage = "none";
    double busy_pct = 0, waited = 0, t_start = bench_now_ms();
    if (!r) goto finish;
    for (int i = 0; i < LB_COUNT; i++) r->skip[i] = 0;
    r->gpu_kind = -1;
    r->throttle_at = -1;
    lb_cond_read("", &c0);
    c1 = c0;
    if (lb_cond_battery_low(&c0)) { why = "battery"; goto finish; }

    /* a quiet system first: QUIET_SECS seconds in a row below QUIET_PCT, at most QUIET_MAX_S */
    unsigned long long b0, t0, b1, t1, wb, wt;
    if (lb_cpu_ticks("", &b0, &t0)) { b0 = t0 = 0; }
    wb = b0; wt = t0;
    int calm = 0;
    double w0 = bench_now_ms();
    while (!cancel_req && (bench_now_ms() - w0) < QUIET_MAX_S * 1000) {
        struct timespec ts = { 1, 0 };
        nanosleep(&ts, NULL);
        if (lb_cpu_ticks("", &b1, &t1)) break;
        double p = cpu_busy(b0, t0, b1, t1);
        b0 = b1; t0 = t1;
        calm = p < QUIET_PCT ? calm + 1 : 0;
        if (calm >= QUIET_SECS) { quiet = 1; break; }
    }
    waited = (bench_now_ms() - w0) / 1000;
    if (!lb_cpu_ticks("", &b1, &t1)) busy_pct = cpu_busy(wb, wt, b1, t1);
    if (cancel_req) { st = ST_CANCELLED; goto finish; }

    /* the helper, with what lord-server opened for it */
    char path[PATH_MAX];
    if (helper_path(path, sizeof path)) { why = "helper"; goto finish; }
    gpu = open_render_node();
    if (j->full) {
        disk = open_scratch(scratch_dir, sizeof scratch_dir, &storage_mb);
        storage = disk >= 0 ? lb_storage_kind("", scratch_dir) : "none";
    }
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv)) goto finish;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, sv[1], 3);
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t none, all;
    sigemptyset(&none);
    sigfillset(&all);
    posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setsigdefault(&at, &all);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    char sessiond[32] = "LORDOS_SESSIOND=1";
    char *envp[] = { "LANG=C.UTF-8", getenv("LORDOS_SESSIOND") ? sessiond : NULL, NULL };
    char *argv[] = { "lord-bench", NULL };
    pid_t pid;
    int e = posix_spawn(&pid, path, &fa, &at, argv, envp);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(sv[1]);
    if (e) { close(sv[0]); why = "helper"; goto finish; }
    sock = sv[0];
    pthread_mutex_lock(&mu);
    child = pid;
    state = ST_RUNNING;
    pthread_mutex_unlock(&mu);

    LbGo go = { LB_MAGIC, j->full ? LB_MODE_FULL : LB_MODE_QUICK, (gpu >= 0 ? LB_GO_GPU : 0) | (disk >= 0 ? LB_GO_STORAGE : 0),
                (uint32_t)threads, storage_mb, j->full ? (uint32_t)j->sustained_s : 0 };
    int fds[2], nfd = 0;
    if (gpu >= 0) fds[nfd++] = gpu;
    if (disk >= 0) fds[nfd++] = disk;
    char cbuf[CMSG_SPACE(2 * sizeof(int))];
    memset(cbuf, 0, sizeof cbuf);
    struct iovec iov = { &go, sizeof go };
    struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1 };
    if (nfd) {
        mh.msg_control = cbuf;
        mh.msg_controllen = CMSG_SPACE((size_t)nfd * sizeof(int));
        struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN((size_t)nfd * sizeof(int));
        memcpy(CMSG_DATA(cm), fds, (size_t)nfd * sizeof(int));
    }
    if (sendmsg(sock, &mh, MSG_NOSIGNAL) != (ssize_t)sizeof go) { why = "helper"; goto stop; }
    if (gpu >= 0) { close(gpu); gpu = -1; }
    if (disk >= 0) { close(disk); disk = -1; }

    /* the records, and the guards every second */
    double deadline = bench_now_ms() + (j->full ? (600.0 + j->sustained_s) : 180.0) * 1000, next_guard = 0;
    int sustained_phase = 0, finished = 0;
    while (!finished) {
        if (cancel_req) { st = ST_CANCELLED; goto stop; }
        if (bench_now_ms() > deadline) { why = "timeout"; goto stop; }
        if (bench_now_ms() >= next_guard) {
            next_guard = bench_now_ms() + 1000;
            lb_cond_read("", &cn);
            const char *s = lb_cond_stop(&c0, &cn, !sustained_phase);
            if (s) {
                if (sustained_phase && !strcmp(s, "throttled")) { /* recorded, not stopped */ }
                else { snprintf(why_buf, sizeof why_buf, "%s", s); why = why_buf; st = ST_STOPPED; goto stop; }
            }
            if (sustained_phase && r->throttle_at < 0 &&
                ((c0.throttle_known && cn.throttle_events > c0.throttle_events) || (cn.rpi_throttled > 0 && (cn.rpi_throttled & (LB_RPI_THROTTLED | LB_RPI_CAPPED | LB_RPI_SOFTTEMP))))) {
                r->throttle_at = r->nsamples ? r->sample_t[r->nsamples - 1] : 0;
                r->throttle_temp = cn.temp_c;
            }
        }
        struct pollfd pf = { sock, POLLIN, 0 };
        int pr = poll(&pf, 1, 250);
        if (pr < 0 && errno != EINTR) { why = "helper"; goto stop; }
        if (pr <= 0) continue;
        LbMsg m;
        ssize_t n = recv(sock, &m, sizeof m, MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN)) { why = "helper"; goto stop; }
        if (n < 0) continue;
        if (n != (ssize_t)sizeof m || !lb_msg_valid(&m)) { why = "protocol"; goto stop; }
        if (m.kind == LB_MSG_SAMPLE) lb_cond_read("", &cn);
        pthread_mutex_lock(&mu);
        switch (m.kind) {
        case LB_MSG_STEP:
            step = (int)m.id;
            if (m.id == LB_SUS_START) sustained_phase = 1;
            break;
        case LB_MSG_RESULT:
            r->have[m.id] = 1; r->value[m.id] = m.value; r->spread[m.id] = m.aux;
            done_count++;
            break;
        case LB_MSG_SKIP:
            r->skip[m.id] = (int)m.aux;
            done_count++;
            break;
        case LB_MSG_SAMPLE:
            if (r->nsamples < MAX_SAMPLES) {
                r->sample_t[r->nsamples] = m.aux;
                r->sample_v[r->nsamples] = m.value;
                r->sample_temp[r->nsamples] = cn.temp_c;
                r->nsamples++;
            }
            break;
        case LB_MSG_GPUINFO: r->gpu_kind = (int)m.value; break;
        case LB_MSG_DONE: r->sandbox = (int)m.aux; finished = 1; st = ST_DONE; break;
        }
        pthread_mutex_unlock(&mu);
    }
stop:
    if (st != ST_DONE && st != ST_CANCELLED && st != ST_STOPPED) st = ST_FAILED;
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    pthread_mutex_lock(&mu);
    child = -1;
    pthread_mutex_unlock(&mu);
finish:
    if (sock >= 0) close(sock);
    if (gpu >= 0) close(gpu);
    if (disk >= 0) close(disk);
    lb_cond_read("", &c1);
    if (r) {
        if (!why && st == ST_FAILED) why = "helper";
        if (st == ST_FAILED && why && !strcmp(why, "battery")) st = ST_STOPPED;
        char *json = build_result(j, st, why, r, &c0, &c1, busy_pct, waited, quiet, threads, storage, storage_mb,
                                  (bench_now_ms() - t_start) / 1000);
        int any = 0;
        for (int i = 0; i < LB_COUNT; i++) any |= r->have[i];
        if (json && any && (st == ST_DONE || st == ST_STOPPED)) save_result(json);   /* a refused run keeps the last result */
        pthread_mutex_lock(&mu);
        free(result_json);
        result_json = json;
        state = st;
        pthread_mutex_unlock(&mu);
        free(r);
    } else set_state(ST_FAILED);
    free(j->extra);
    free(j);
    return NULL;
}

int lbrun_start(const char *mode, int sustained_s, const char *extra) {
    int full = mode && !strcmp(mode, "full");
    if (!mode || (!full && strcmp(mode, "quick"))) return -1;
    if (sustained_s && (sustained_s < 20 || sustained_s > 1800)) return -1;
    char path[PATH_MAX];
    if (helper_path(path, sizeof path)) return -1;
    pthread_mutex_lock(&mu);
    if (state == ST_WAITING || state == ST_RUNNING) { pthread_mutex_unlock(&mu); return -1; }
    state = ST_WAITING;
    step = -1;
    done_count = 0;
    cancel_req = 0;
    free(result_json);
    result_json = NULL;
    pthread_mutex_unlock(&mu);
    Job *j = calloc(1, sizeof *j);
    if (!j) { set_state(ST_IDLE); return -1; }
    j->full = full;
    j->sustained_s = full ? sustained_s : 0;
    j->extra = extra ? strdup(extra) : NULL;
    pthread_t th;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    int e = pthread_create(&th, &a, run, j);
    pthread_attr_destroy(&a);
    if (e) { free(j->extra); free(j); set_state(ST_IDLE); return -1; }
    return 0;
}

void lbrun_cancel(void) {
    pthread_mutex_lock(&mu);
    if (state == ST_WAITING || state == ST_RUNNING) cancel_req = 1;
    pthread_mutex_unlock(&mu);
}

char *lbrun_poll(void) {
    HBuf b = { 0 };
    pthread_mutex_lock(&mu);
    hb_printf(&b, "{\"state\":\"%s\",\"step\":", st_name[state]);
    hb_str(&b, step >= 0 ? lb_name(step) : "");
    hb_printf(&b, ",\"progress\":%.3f", done_count / (double)LB_COUNT);
    if (result_json && state != ST_WAITING && state != ST_RUNNING) hb_printf(&b, ",\"result\":%s", result_json);
    hb_printf(&b, "}");
    pthread_mutex_unlock(&mu);
    return hb_take(&b);
}

/* ---- boot history (lord-sessiond's boot-times.jsonl) ---- */

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

char *lbrun_boot_history(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > 256 * 1024 || (st.st_uid != 0 && st.st_uid != getuid()) || (st.st_mode & 022)) { close(fd); return NULL; }
    char *txt = malloc((size_t)st.st_size + 1);
    ssize_t n = txt ? read(fd, txt, (size_t)st.st_size) : -1;
    close(fd);
    if (n < 0) { free(txt); return NULL; }
    txt[n] = 0;
    /* versions in order of appearance; for each, the boots' seconds; the first boot of each version kept apart */
    enum { MAXV = 32, MAXB = 200 };
    static struct { char v[40]; double s[MAXB]; int n; double first; } vs[MAXV];
    int nv = 0;
    char last[40] = "";
    for (char *line = txt; line && *line; ) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = 0;
        char *ver = strstr(line, "\"version\":\""), *sec = strstr(line, "\"seconds\":");
        if (ver && sec) {
            ver += 11;
            size_t k = strspn(ver, "0123456789.-+~abcdefghijklmnopqrstuvwxyz");
            double s = atof(sec + 10);
            if (k && k < sizeof last && ver[k] == '"' && s > 0 && s < 3600) {
                char v[40];
                memcpy(v, ver, k);
                v[k] = 0;
                int i;
                for (i = 0; i < nv && strcmp(vs[i].v, v); i++) {}
                if (i == nv && nv < MAXV) { snprintf(vs[nv].v, sizeof vs[nv].v, "%s", v); vs[nv].n = 0; vs[nv].first = -1; nv++; }
                if (i < nv) {
                    if (strcmp(last, v)) { if (vs[i].first < 0) vs[i].first = s; else if (vs[i].n < MAXB) vs[i].s[vs[i].n++] = s; }
                    else if (vs[i].n < MAXB) vs[i].s[vs[i].n++] = s;
                }
                snprintf(last, sizeof last, "%s", v);
            }
        }
        line = eol ? eol + 1 : NULL;
    }
    free(txt);
    HBuf b = { 0 };
    hb_printf(&b, "[");
    for (int i = 0; i < nv; i++) {
        hb_printf(&b, "%s{\"version\":", i ? "," : "");
        hb_str(&b, vs[i].v);
        hb_printf(&b, ",\"boots\":%d", vs[i].n);
        if (vs[i].n) {
            qsort(vs[i].s, (size_t)vs[i].n, sizeof(double), cmp_d);
            int m = vs[i].n;
            double med = m % 2 ? vs[i].s[m / 2] : (vs[i].s[m / 2 - 1] + vs[i].s[m / 2]) / 2;
            hb_printf(&b, ",\"median\":%.2f,\"min\":%.2f,\"max\":%.2f", med, vs[i].s[0], vs[i].s[m - 1]);
        }
        if (vs[i].first >= 0) hb_printf(&b, ",\"first\":%.2f", vs[i].first);
        hb_printf(&b, "}");
    }
    hb_printf(&b, "]");
    return hb_take(&b);
}

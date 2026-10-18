/* mailc.c — see mailc.h. */
#include "mailc.h"
#include "proto.h"
#include "../doc/proto.h"
#include "../doc/wire.h"
#include "../sec/json.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define MAX_IMG    256
#define IMG_BUDGET ((size_t)96 << 20)
#define MAX_ALLOW  128
#define MAX_GRANT  64

typedef struct { char host[256]; int port; int suffix; } Allow;
typedef struct { char host[256]; int port; time_t until; } Grant;
typedef struct { int handle, w, h; uint32_t *px; uint64_t seq; } Img;

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t send_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t img_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t thread;
static int running, sock = -1, wakefd = -1;
static pid_t child;
static int next_id = 1, pending;
static JBuf events;
static char helper[PATH_MAX], cfg_path[PATH_MAX], cache_path[PATH_MAX];
static Allow allow[MAX_ALLOW];
static int nallow;
static Grant grants[MAX_GRANT];
static Img imgs[MAX_IMG];
static size_t img_bytes;
static uint64_t img_seq;
static int next_handle = 1;
static double last_crash;

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static void paths(void) {
    if (cfg_path[0]) return;
    const char *h = getenv("HOME");
    if (!h) h = "/nonexistent";
    snprintf(cfg_path, sizeof cfg_path, "%s/.config/lordos/mail/accounts.json", h);
    snprintf(cache_path, sizeof cache_path, "%s/.local/share/lordos/mail", h);
}

const char *mail_config_path(void) { paths(); return cfg_path; }
const char *mail_cache_path(void) { paths(); return cache_path; }

int mail_available(void) {
    if (!helper[0]) {
        char self[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
        if (n <= 0) return -1;
        self[n] = 0;
        char *slash = strrchr(self, '/');
        if (!slash) return -1;
        *slash = 0;
        if (strlen(self) + 12 > sizeof helper) return -1;
        snprintf(helper, sizeof helper, "%s/lord-maild", self);
    }
    struct stat st, me;
    if (stat(helper, &st) || !S_ISREG(st.st_mode) || !(st.st_mode & S_IXUSR)) return -1;
    /* owned by root or by lord-server's owner, and not writable by anyone else */
    if (stat("/proc/self/exe", &me) || (st.st_uid != 0 && st.st_uid != me.st_uid) || (st.st_mode & 022)) return -1;
    return 0;
}

/* ---- allowed endpoints ---- */

static int host_ok(const char *h) {
    size_t n = strlen(h);
    if (!n || n > 253) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)h[i];
        if (!(isalnum(c) || c == '-' || c == '.' || c == ':' || c == '[' || c == ']')) return 0;
    }
    return 1;
}

/* "https://host:port/path" → host, port */
static int url_host(const char *url, char *host, size_t cap, int *port) {
    host[0] = 0;
    *port = 0;
    if (!url) return 0;
    int https = !strncasecmp(url, "https://", 8), http = !strncasecmp(url, "http://", 7);
    if (!https && !http) return 0;
    const char *p = url + (https ? 8 : 7);
    const char *e = p + strcspn(p, "/?#");
    const char *at = memchr(p, '@', (size_t)(e - p));
    if (at) p = at + 1;
    const char *colon = NULL;
    if (*p == '[') { const char *rb = memchr(p, ']', (size_t)(e - p)); if (!rb) return 0; colon = rb + 1 < e && rb[1] == ':' ? rb + 1 : NULL; }
    else colon = memchr(p, ':', (size_t)(e - p));
    size_t hl = (size_t)((colon ? colon : e) - p);
    if (!hl || hl >= cap) return 0;
    for (size_t i = 0; i < hl; i++) host[i] = (char)tolower((unsigned char)p[i]);
    host[hl] = 0;
    *port = colon ? atoi(colon + 1) : (https ? 443 : 80);
    if (*port <= 0 || *port > 65535) return 0;
    return host_ok(host);
}

static void add_allow(Allow *list, int *n, const char *host, int port, int suffix) {
    if (*n >= MAX_ALLOW || !host || !host_ok(host) || port <= 0 || port > 65535) return;
    Allow *a = &list[(*n)++];
    snprintf(a->host, sizeof a->host, "%s", host);
    for (char *c = a->host; *c; c++) *c = (char)tolower((unsigned char)*c);
    a->port = port;
    a->suffix = suffix;
}

static void allow_server(Allow *list, int *n, const JVal *srv) {
    if (!srv || srv->type != J_OBJ) return;
    const char *h = json_str(srv, "host");
    int port = (int)json_num(srv, "port", 0);
    if (h) add_allow(list, n, h, port, 0);
}

static void allow_url(Allow *list, int *n, const char *url) {
    char host[256];
    int port;
    if (!url_host(url, host, sizeof host, &port)) return;
    add_allow(list, n, host, port, 0);
    /* CalDAV/CardDAV discovery redirects inside the same provider (to a numbered host such as pNN-caldav.<domain>):
     * allow other hosts of the same domain, HTTPS only */
    const char *dot1 = strrchr(host, '.');
    if (!dot1) return;
    const char *dot2 = dot1;
    while (dot2 > host && dot2[-1] != '.') dot2--;
    if (dot2 > host && strchr(host, '.') != dot1) add_allow(list, n, dot2, 443, 1);
}

static int build_allow(const char *json, size_t len, Allow *list) {
    int n = 0;
    if (!json || !len) return 0;
    JDoc d;
    if (json_parse(json, len, &d, NULL, 0)) return 0;
    const JVal *acc = json_get(d.root, "accounts");
    for (const JVal *a = acc && acc->type == J_ARR ? acc->kid : NULL; a; a = a->next) {
        allow_server(list, &n, json_get(a, "imap"));
        allow_server(list, &n, json_get(a, "smtp"));
        allow_url(list, &n, json_str(a, "url"));          /* CalDAV/CardDAV accounts */
        allow_url(list, &n, json_str(a, "caldav"));
        allow_url(list, &n, json_str(a, "carddav"));
    }
    json_free(&d);
    return n;
}

static int match(const Allow *list, int n, const char *host, int port) {
    for (int i = 0; i < n; i++) {
        if (list[i].port != port) continue;
        if (!strcasecmp(list[i].host, host)) return 1;
        if (list[i].suffix) {
            size_t hl = strlen(host), sl = strlen(list[i].host);
            if (hl > sl && host[hl - sl - 1] == '.' && !strcasecmp(host + hl - sl, list[i].host)) return 1;
        }
    }
    return 0;
}

int mail_endpoint_allowed(const char *config_json, size_t len, const char *host, int port) {
    Allow tmp[MAX_ALLOW];
    int n = build_allow(config_json, len, tmp);
    return match(tmp, n, host, port);
}

static char *read_config(size_t *len) {
    paths();
    *len = 0;
    int fd = open(cfg_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > MAIL_CONFIG_MAX) { close(fd); return NULL; }
    char *b = malloc((size_t)st.st_size + 1);
    if (!b) { close(fd); return NULL; }
    ssize_t r = read(fd, b, (size_t)st.st_size);
    close(fd);
    if (r < 0) { free(b); return NULL; }
    b[r] = 0;
    JDoc d;
    if (json_parse(b, (size_t)r, &d, NULL, 0)) { free(b); return NULL; }
    json_free(&d);
    *len = (size_t)r;
    return b;
}

void mail_allow_hosts(const char *const *urls, int n, int seconds) {
    time_t until = time(NULL) + (seconds > 0 && seconds < 3600 ? seconds : 600);
    pthread_mutex_lock(&mu);
    for (int i = 0; i < n; i++) {
        char host[256];
        int port;
        if (!url_host(urls[i], host, sizeof host, &port)) continue;
        if (port != 80 && port != 443) continue;          /* remote images: plain web ports only */
        int slot = -1;
        for (int k = 0; k < MAX_GRANT; k++) {
            if (grants[k].host[0] && !strcmp(grants[k].host, host) && grants[k].port == port) { slot = k; break; }
            if (slot < 0 && (!grants[k].host[0] || grants[k].until < time(NULL))) slot = k;
        }
        if (slot < 0) slot = 0;
        snprintf(grants[slot].host, sizeof grants[slot].host, "%s", host);
        grants[slot].port = port;
        grants[slot].until = until;
    }
    pthread_mutex_unlock(&mu);
}

static int allowed_now(const char *host, int port) {
    int ok = 0;
    pthread_mutex_lock(&mu);
    if (match(allow, nallow, host, port)) ok = 1;
    time_t now = time(NULL);
    for (int k = 0; !ok && k < MAX_GRANT; k++)
        if (grants[k].host[0] && grants[k].until >= now && grants[k].port == port && !strcasecmp(grants[k].host, host)) ok = 2;
    pthread_mutex_unlock(&mu);
    return ok;
}

/* ---- events ---- */

static void ev_push(int id, const char *type, const char *data, size_t dlen) {
    pthread_mutex_lock(&mu);
    if (events.n) jb_raw(&events, ",");
    jb_printf(&events, "{\"id\":%d,\"type\":\"%s\"", id, type);
    if (data && dlen) {
        jb_raw(&events, ",\"data\":");
        /* the helper's JSON is re-validated before JS sees it */
        JDoc d;
        if (!json_parse(data, dlen, &d, NULL, 0)) { json_free(&d); char *tmp = malloc(dlen + 1); if (tmp) { memcpy(tmp, data, dlen); tmp[dlen] = 0; jb_raw(&events, tmp); free(tmp); } else jb_raw(&events, "null"); }
        else jb_raw(&events, "{\"error\":\"bad-json\"}");
    }
    jb_raw(&events, "}");
    if (strcmp(type, "evt") && pending > 0) pending--;
    pthread_mutex_unlock(&mu);
}

char *mail_poll(void) {
    pthread_mutex_lock(&mu);
    char *r = NULL;
    if (events.n) {
        size_t n = events.n;
        r = malloc(n + 3);
        if (r) { r[0] = '['; memcpy(r + 1, events.p, n); r[n + 1] = ']'; r[n + 2] = 0; }
        events.n = 0;
    }
    pthread_mutex_unlock(&mu);
    return r;
}

int mail_pending(void) { pthread_mutex_lock(&mu); int p = pending; pthread_mutex_unlock(&mu); return p; }

/* ---- images ---- */

static int img_store(int w, int h, int fd) {
    size_t bytes = (size_t)w * (size_t)h * 4;
    void *m = mmap(NULL, bytes, PROT_READ, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) return -1;
    uint32_t *px = malloc(bytes);
    if (!px) { munmap(m, bytes); return -1; }
    memcpy(px, m, bytes);
    munmap(m, bytes);
    pthread_mutex_lock(&img_mu);
    /* make room: oldest first */
    while (img_bytes + bytes > IMG_BUDGET) {
        int old = -1;
        for (int i = 0; i < MAX_IMG; i++) if (imgs[i].px && (old < 0 || imgs[i].seq < imgs[old].seq)) old = i;
        if (old < 0) break;
        img_bytes -= (size_t)imgs[old].w * (size_t)imgs[old].h * 4;
        free(imgs[old].px);
        memset(&imgs[old], 0, sizeof imgs[old]);
    }
    int slot = -1;
    for (int i = 0; i < MAX_IMG; i++) if (!imgs[i].px) { slot = i; break; }
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < MAX_IMG; i++) if (imgs[i].seq < imgs[slot].seq) slot = i;
        img_bytes -= (size_t)imgs[slot].w * (size_t)imgs[slot].h * 4;
        free(imgs[slot].px);
    }
    imgs[slot] = (Img){ next_handle++, w, h, px, ++img_seq };
    img_bytes += bytes;
    int handle = imgs[slot].handle;
    pthread_mutex_unlock(&img_mu);
    return handle;
}

const uint32_t *mail_image_lock(int handle, int *w, int *h) {
    pthread_mutex_lock(&img_mu);
    for (int i = 0; i < MAX_IMG; i++) if (imgs[i].px && imgs[i].handle == handle) { *w = imgs[i].w; *h = imgs[i].h; imgs[i].seq = ++img_seq; return imgs[i].px; }
    return NULL;
}
void mail_image_unlock(void) { pthread_mutex_unlock(&img_mu); }
void mail_image_release(int handle) {
    pthread_mutex_lock(&img_mu);
    for (int i = 0; i < MAX_IMG; i++) if (imgs[i].px && imgs[i].handle == handle) {
        img_bytes -= (size_t)imgs[i].w * (size_t)imgs[i].h * 4;
        free(imgs[i].px);
        memset(&imgs[i], 0, sizeof imgs[i]);
    }
    pthread_mutex_unlock(&img_mu);
}

/* ---- connector ---- */

static int v4_public(uint32_t a) {          /* host byte order */
    uint8_t b0 = (uint8_t)(a >> 24), b1 = (uint8_t)(a >> 16);
    if (b0 == 0 || b0 == 10 || b0 == 127) return 0;                    /* this net, private, loopback */
    if (b0 == 100 && (b1 & 0xc0) == 64) return 0;                       /* 100.64/10 carrier NAT */
    if (b0 == 169 && b1 == 254) return 0;                               /* link-local, cloud metadata */
    if (b0 == 172 && (b1 & 0xf0) == 16) return 0;                       /* 172.16/12 */
    if (b0 == 192 && b1 == 168) return 0;                               /* 192.168/16 */
    if (b0 == 192 && b1 == 0 && (uint8_t)(a >> 8) == 0) return 0;       /* 192.0.0/24 IETF */
    if (b0 == 198 && (b1 & 0xfe) == 18) return 0;                       /* 198.18/15 benchmarking */
    if (b0 >= 224) return 0;                                            /* multicast, reserved, broadcast */
    return 1;
}

int mail_addr_public(const struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) return v4_public(ntohl(((const struct sockaddr_in *)(const void *)sa)->sin_addr.s_addr));
    if (sa->sa_family != AF_INET6) return 0;
    const uint8_t *b = ((const struct sockaddr_in6 *)(const void *)sa)->sin6_addr.s6_addr;
    static const uint8_t zero[16];
    if (!memcmp(b, zero, 16)) return 0;                                  /* :: */
    if (!memcmp(b, zero, 15) && b[15] == 1) return 0;                    /* ::1 */
    if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) return 0;                 /* fe80::/10 link-local */
    if (b[0] == 0xfe && (b[1] & 0xc0) == 0xc0) return 0;                 /* fec0::/10 site-local (old) */
    if ((b[0] & 0xfe) == 0xfc) return 0;                                 /* fc00::/7 unique local */
    if (b[0] == 0xff) return 0;                                          /* multicast */
    uint32_t v4 = (uint32_t)b[12] << 24 | (uint32_t)b[13] << 16 | (uint32_t)b[14] << 8 | b[15];
    /* v4-mapped ::ffff:a.b.c.d and v4-compatible ::a.b.c.d */
    if (!memcmp(b, zero, 10) && b[10] == 0xff && b[11] == 0xff) return v4_public(v4);
    if (!memcmp(b, zero, 12)) return 0;
    /* NAT64 64:ff9b::/96 (and the local-use 64:ff9b:1::/48): judge the embedded IPv4 */
    if (b[0] == 0 && b[1] == 0x64 && b[2] == 0xff && b[3] == 0x9b) { if (b[4] == 0 && b[5] == 1) return 0; return v4_public(v4); }
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x0d && b[3] == 0xb8) return 0;   /* 2001:db8::/32 documentation */
    if (b[0] == 0x20 && b[1] == 0x02) return v4_public((uint32_t)b[2] << 24 | (uint32_t)b[3] << 16 | (uint32_t)b[4] << 8 | b[5]);   /* 6to4 */
    return 1;
}

typedef struct { uint32_t cid; char host[256]; int port; int image; } ConnReq;

static int send_msg(uint32_t op, uint32_t id, const char *json, int fd) {
    pthread_mutex_lock(&send_mu);
    int s = sock;
    int r = s >= 0 ? doc_send(s, op, id, json, json ? strlen(json) : 0, fd >= 0 ? &fd : NULL, fd >= 0 ? 1 : 0) : -1;
    pthread_mutex_unlock(&send_mu);
    return r;
}

static int connect_timeout(const struct addrinfo *ai, int ms) {
    int fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, ai->ai_protocol);
    if (fd < 0) return -1;
    int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (r && errno != EINPROGRESS) { close(fd); return -1; }
    if (r) {
        struct pollfd p = { fd, POLLOUT, 0 };
        if (poll(&p, 1, ms) != 1) { close(fd); return -1; }
        int err = 0;
        socklen_t el = sizeof err;
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) || err) { close(fd); return -1; }
    }
    int fl = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    return fd;
}

static void *connector(void *arg) {
    ConnReq *q = arg;
    char port[8];
    snprintf(port, sizeof port, "%d", q->port);
    struct addrinfo hints = { 0 }, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;
    int fd = -1;
    const char *why = "dns";
    if (!getaddrinfo(q->host, port, &hints, &res)) {
        why = "connect";
        for (struct addrinfo *a = res; a && fd < 0; a = a->ai_next) {
            /* image hosts (chosen by whoever sent the mail): never loopback, LAN or link-local,
             * judged on the resolved address we actually connect to (no DNS rebinding) */
            if (q->image && !mail_addr_public(a->ai_addr)) { why = "denied"; continue; }
            fd = connect_timeout(a, 10000);
        }
        freeaddrinfo(res);
    }
    if (fd >= 0) { send_msg(MQ_SOCK, q->cid, "{}", fd); close(fd); }
    else { char j[64]; snprintf(j, sizeof j, "{\"error\":\"%s\"}", why); send_msg(MQ_SOCK, q->cid, j, -1); }
    free(q);
    return NULL;
}

static void on_connect(uint32_t cid, const char *json, size_t len) {
    JDoc d;
    char host[256] = "";
    int port = 0;
    if (json && !json_parse(json, len, &d, NULL, 0)) {
        const char *h = json_str(d.root, "host");
        if (h && strlen(h) < sizeof host) { snprintf(host, sizeof host, "%s", h); for (char *c = host; *c; c++) *c = (char)tolower((unsigned char)*c); }
        port = (int)json_num(d.root, "port", 0);
        json_free(&d);
    }
    int how = host_ok(host) && port > 0 && port <= 65535 ? allowed_now(host, port) : 0;
    if (!how) {
        fprintf(stderr, "lord-server: lord-maild asked for %s:%d, which is not a configured server: denied\n", host, port);
        send_msg(MQ_SOCK, cid, "{\"error\":\"denied\"}", -1);
        return;
    }
    ConnReq *q = malloc(sizeof *q);
    if (!q) { send_msg(MQ_SOCK, cid, "{\"error\":\"connect\"}", -1); return; }
    q->cid = cid;
    snprintf(q->host, sizeof q->host, "%s", host);
    q->port = port;
    q->image = how == 2;
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &at, connector, q)) { free(q); send_msg(MQ_SOCK, cid, "{\"error\":\"connect\"}", -1); }
    pthread_attr_destroy(&at);
}

/* ---- the helper process ---- */

static void *loop(void *arg) {
    (void)arg;
    for (;;) {
        struct pollfd p[2] = { { sock, POLLIN, 0 }, { wakefd, POLLIN, 0 } };
        int r = poll(p, 2, -1);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (p[1].revents & POLLIN) { uint64_t v; if (read(wakefd, &v, sizeof v) < 0) { /* nothing */ } }
        if (!(p[0].revents & (POLLIN | POLLHUP | POLLERR))) continue;
        DocMsg m;
        int k = doc_recv(sock, &m);
        if (k) {
            /* the helper is gone: every pending request fails, and the next one restarts it */
            pthread_mutex_lock(&send_mu);
            close(sock);
            sock = -1;
            pthread_mutex_unlock(&send_mu);
            if (child > 0) { kill(child, SIGKILL); waitpid(child, NULL, 0); child = 0; }
            pthread_mutex_lock(&mu);
            int np = pending;
            pending = 0;
            running = 0;
            pthread_mutex_unlock(&mu);
            (void)np;
            ev_push(0, "crash", NULL, 0);
            last_crash = now_ms();
            break;
        }
        switch (m.h.op) {
        case MR_DONE: ev_push((int)m.h.id, "done", m.json, m.len); break;
        case MR_ERR: ev_push((int)m.h.id, "err", m.json, m.len); break;
        case MR_EVT: ev_push(0, "evt", m.json, m.len); break;
        case MR_CONNECT: on_connect(m.h.id, m.json, m.len); break;
        case MR_IMG: {
            JDoc d;
            int w = 0, h = 0;
            if (m.json && !json_parse(m.json, m.len, &d, NULL, 0)) { w = (int)json_num(d.root, "w", 0); h = (int)json_num(d.root, "h", 0); json_free(&d); }
            int handle = -1;
            if (m.nfds == 1 && w > 0 && h > 0 && w <= 8192 && h <= 8192 && doc_check_sealed(m.fds[0], (size_t)w * (size_t)h * 4) >= 0)
                handle = img_store(w, h, m.fds[0]);
            char j[96];
            if (handle > 0) snprintf(j, sizeof j, "{\"image\":%d,\"w\":%d,\"h\":%d}", handle, w, h);
            else snprintf(j, sizeof j, "{\"error\":\"image\"}");
            ev_push((int)m.h.id, handle > 0 ? "done" : "err", j, strlen(j));
            break;
        }
        default: break;
        }
        doc_msg_free(&m);
    }
    return NULL;
}

static int local_zone(char *out, size_t cap) {
    char l[PATH_MAX];
    ssize_t n = readlink("/etc/localtime", l, sizeof l - 1);
    out[0] = 0;
    if (n <= 0) return 0;
    l[n] = 0;
    const char *z = strstr(l, "zoneinfo/");
    if (!z) return 0;
    snprintf(out, cap, "%s", z + 9);
    return 1;
}

static int mkdirs(const char *p, mode_t mode) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", p);
    for (char *s = tmp + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(tmp, mode); *s = '/'; }
    return mkdir(tmp, mode) && errno != EEXIST ? -1 : 0;
}

int mail_start(void) {
    pthread_mutex_lock(&mu);
    int r = running;
    pthread_mutex_unlock(&mu);
    if (r) return 0;
    if (mail_available()) return -1;
    if (last_crash && now_ms() - last_crash < 3000) return -1;     /* do not spin on a crash loop */
    if (thread) { pthread_join(thread, NULL); thread = 0; }
    paths();
    if (mkdirs(cache_path, 0700)) return -1;
    int dfd = open(cache_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dfd < 0) return -1;
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv)) { close(dfd); return -1; }
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
    char henv[PATH_MAX + 8], *envp[6];
    int ne = 0;
    const char *home = getenv("HOME");
    if (home) { snprintf(henv, sizeof henv, "HOME=%s", home); envp[ne++] = henv; }
    envp[ne++] = "LANG=C.UTF-8";
    if (getenv("LORD_MAIL_DEBUG")) envp[ne++] = "LORD_MAIL_DEBUG=1";
    envp[ne] = NULL;
    char *argv[] = { "lord-maild", NULL };
    pid_t pid;
    int e = posix_spawn(&pid, helper, &fa, &at, argv, envp);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(sv[1]);
    if (e) { close(sv[0]); close(dfd); return -1; }
    size_t clen;
    char *cfg = read_config(&clen);
    pthread_mutex_lock(&mu);
    nallow = build_allow(cfg, clen, allow);
    pthread_mutex_unlock(&mu);
    char zone[128];
    local_zone(zone, sizeof zone);
    JBuf b;
    jb_init(&b);
    jb_raw(&b, "{\"config\":");
    jb_raw(&b, cfg ? cfg : "{}");
    jb_raw(&b, ",\"zone\":");
    jb_str(&b, zone);
    jb_raw(&b, "}");
    free(cfg);
    int ok = !b.oom && doc_send(sv[0], MQ_INIT, 0, b.p, b.n, &dfd, 1) == 0;
    jb_free(&b);
    close(dfd);
    if (!ok) { close(sv[0]); kill(pid, SIGKILL); waitpid(pid, NULL, 0); return -1; }
    if (wakefd < 0) wakefd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    pthread_mutex_lock(&send_mu);
    sock = sv[0];
    pthread_mutex_unlock(&send_mu);
    child = pid;
    pthread_mutex_lock(&mu);
    running = 1;
    pthread_mutex_unlock(&mu);
    if (pthread_create(&thread, NULL, loop, NULL)) { pthread_mutex_lock(&mu); running = 0; pthread_mutex_unlock(&mu); return -1; }
    return 0;
}

int mail_request(const char *json, size_t len, int *fds, int nfds) {
    if (mail_start()) { for (int i = 0; i < nfds; i++) close(fds[i]); return -1; }
    pthread_mutex_lock(&mu);
    int id = next_id++;
    if (next_id > 1 << 30) next_id = 1;
    pending++;
    pthread_mutex_unlock(&mu);
    pthread_mutex_lock(&send_mu);
    int r = sock >= 0 ? doc_send(sock, MQ_REQ, (uint32_t)id, json, len, nfds ? fds : NULL, nfds) : -1;
    pthread_mutex_unlock(&send_mu);
    for (int i = 0; i < nfds; i++) close(fds[i]);
    if (r) { pthread_mutex_lock(&mu); pending--; pthread_mutex_unlock(&mu); return -1; }
    return id;
}

void mail_reload_config(void) {
    size_t clen;
    char *cfg = read_config(&clen);
    pthread_mutex_lock(&mu);
    nallow = build_allow(cfg, clen, allow);
    int r = running;
    pthread_mutex_unlock(&mu);
    if (r) {
        JBuf b;
        jb_init(&b);
        jb_raw(&b, "{\"config\":");
        jb_raw(&b, cfg ? cfg : "{}");
        jb_raw(&b, "}");
        if (!b.oom) send_msg(MQ_CONFIG, 0, b.p, -1);
        jb_free(&b);
    }
    free(cfg);
}

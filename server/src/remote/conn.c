/* conn.c — one remote desktop connection, in a child process of lord-sessiond.
 *
 *   1. TLS (OpenSSL) and one HTTP request: the login page (/), the login itself (POST /login),
 *      the desktop (/desk), logout (/logout) or the WebSocket (/ws).
 *   2. Login: system user and password, checked against /etc/shadow with crypt(3). Only regular
 *      accounts (uid >= 1000, with a shell, not locked; if the configuration asks for it, in the
 *      lordos-remote group). Failures per IP wait longer each time (up to 5 minutes). A random
 *      cookie (Secure, HttpOnly, SameSite=Strict) lasts 12 hours.
 *   3. WebSocket: finds the user's session (the same one that may be showing on the monitor) or
 *      creates it (sess.c, docs/SESSIONS.md).
 *      After that this process DROPS root and stays as the user: the rest (TLS, compression,
 *      bridge) runs unprivileged.
 *   4. Bridge: session rectangles → PNG/JPEG → browser; mouse, keyboard and clipboard
 *      from the browser → session. Flow control: the browser acknowledges each frame ("ack") and
 *      only then does the session build the next one, so a slow network gets fewer frames.
 * Browser protocol: docs/REMOTE.md. */
#include "sessiond.h"
#include "proto.h"

#include <crypt.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <shadow.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/evp.h>

extern const char _binary_src_remote_login_html_start[], _binary_src_remote_login_html_end[];
extern const char _binary_src_remote_client_html_start[], _binary_src_remote_client_html_end[];

static SSL *ssl;
static int sock = -1;
static char peer[48];


/* ---- blocking TLS I/O (before the bridge) ---- */

static int tls_write_all(const void *p, size_t n) {
    const char *c = p;
    while (n) {
        int w = SSL_write(ssl, c, n > 1u << 30 ? 1 << 30 : (int)n);
        if (w <= 0) {
            int e = SSL_get_error(ssl, w);
            if (e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ) {
                struct pollfd p2 = { sock, (short)(e == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN), 0 };
                if (poll(&p2, 1, 10000) <= 0) return -1;
                continue;
            }
            return -1;
        }
        c += w;
        n -= (size_t)w;
    }
    return 0;
}

static void respond(int code, const char *status, const char *type, const char *extra, const char *body, size_t len) {
    char head[1024];
    int n = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n"
        "X-Frame-Options: DENY\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: same-origin\r\n"
        "Content-Security-Policy: default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' blob: data:; connect-src 'self' wss:; frame-ancestors 'none'\r\n"
        "%s\r\n", code, status, type, len, extra ? extra : "");
    tls_write_all(head, (size_t)n);
    if (len) tls_write_all(body, len);
}

static void redirect(const char *to, const char *extra) {
    char h[512];
    snprintf(h, sizeof h, "Location: %s\r\n%s", to, extra ? extra : "");
    respond(303, "See Other", "text/plain", h, "", 0);
}

/* ---- HTTP request ---- */

typedef struct {
    char method[8], path[256], query[256];
    char cookie[256], upgrade[32], wskey[64], origin[160], host[160];
    long content_length;
    char body[4096];
    size_t body_len;
} Req;

static void header_val(const char *line, const char *name, char *out, size_t n) {
    size_t l = strlen(name);
    if (strncasecmp(line, name, l) || line[l] != ':') return;
    const char *v = line + l + 1;
    while (*v == ' ') v++;
    snprintf(out, n, "%s", v);
}

static int read_request(Req *r) {
    memset(r, 0, sizeof *r);
    char buf[16384];
    size_t len = 0;
    char *end = NULL;
    while (!end) {
        if (len >= sizeof buf - 1) return -1;
        struct pollfd p = { sock, POLLIN, 0 };
        if (!SSL_pending(ssl) && poll(&p, 1, 15000) <= 0) return -1;
        int n = SSL_read(ssl, buf + len, (int)(sizeof buf - 1 - len));
        if (n <= 0) {
            int e = SSL_get_error(ssl, n);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
            return -1;
        }
        len += (size_t)n;
        buf[len] = 0;
        end = strstr(buf, "\r\n\r\n");
    }
    *end = 0;
    char *body = end + 4;
    size_t have = len - (size_t)(body - buf);
    char *line = buf, *nl = strstr(line, "\r\n");
    if (nl) *nl = 0;
    char target[512] = "";
    if (sscanf(line, "%7s %511s", r->method, target) != 2) return -1;
    char *q = strchr(target, '?');
    if (q) { *q++ = 0; snprintf(r->query, sizeof r->query, "%s", q); }
    snprintf(r->path, sizeof r->path, "%s", target);
    for (line = nl ? nl + 2 : NULL; line && *line; line = nl ? nl + 2 : NULL) {
        nl = strstr(line, "\r\n");
        if (nl) *nl = 0;
        char cl[24] = "";
        header_val(line, "Cookie", r->cookie, sizeof r->cookie);
        header_val(line, "Upgrade", r->upgrade, sizeof r->upgrade);
        header_val(line, "Sec-WebSocket-Key", r->wskey, sizeof r->wskey);
        header_val(line, "Origin", r->origin, sizeof r->origin);
        header_val(line, "Host", r->host, sizeof r->host);
        header_val(line, "Content-Length", cl, sizeof cl);
        if (cl[0]) r->content_length = atol(cl);
    }
    if (r->content_length < 0 || r->content_length > (long)sizeof r->body - 1) return -1;
    if (have > (size_t)r->content_length) have = (size_t)r->content_length;
    memcpy(r->body, body, have);
    r->body_len = have;
    while (r->body_len < (size_t)r->content_length) {
        int n = SSL_read(ssl, r->body + r->body_len, (int)((size_t)r->content_length - r->body_len));
        if (n <= 0) return -1;
        r->body_len += (size_t)n;
    }
    r->body[r->body_len] = 0;
    return 0;
}

/* application/x-www-form-urlencoded: value of a key (decoded) */
static int form_get(const char *body, const char *key, char *out, size_t n) {
    size_t kl = strlen(key);
    for (const char *p = body; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL) {
        if (strncmp(p, key, kl) || p[kl] != '=') continue;
        const char *v = p + kl + 1;
        size_t o = 0;
        for (; *v && *v != '&' && o + 1 < n; v++) {
            if (*v == '+') out[o++] = ' ';
            else if (*v == '%' && isxdigit((unsigned char)v[1]) && isxdigit((unsigned char)v[2])) {
                char hx[3] = { v[1], v[2], 0 };
                out[o++] = (char)strtol(hx, NULL, 16);
                v += 2;
            } else out[o++] = *v;
        }
        out[o] = 0;
        return 0;
    }
    out[0] = 0;
    return -1;
}

static int query_int(const char *q, const char *key, int def) {
    char v[16];
    return form_get(q, key, v, sizeof v) == 0 && v[0] ? atoi(v) : def;
}

/* ---- login ---- */

static int ct_equal(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    unsigned char d = (unsigned char)(la != lb);
    for (size_t i = 0; i < la && i < lb; i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}

static void new_token(uid_t uid, const char *user, char out[65]) {
    unsigned char r[32];
    if (getrandom(r, sizeof r, 0) != sizeof r) { out[0] = 0; return; }
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", r[i]);
    double now = rd_now();
    rd_lock();
    int slot = 0;
    for (int i = 0; i < 64; i++) if (rd_shared->tokens[i].expires < rd_shared->tokens[slot].expires) slot = i;
    RdToken *t = &rd_shared->tokens[slot];
    snprintf(t->tok, sizeof t->tok, "%s", out);
    t->uid = uid;
    snprintf(t->user, sizeof t->user, "%.32s", user);
    t->expires = now + 12 * 3600;
    rd_unlock();
}

static int token_from_cookie(const char *cookie, char out[65]) {
    const char *p = strstr(cookie, "lordos_remote=");
    out[0] = 0;
    if (!p) return -1;
    p += 14;
    size_t n = 0;
    while (n < 64 && isxdigit((unsigned char)p[n])) n++;
    if (n != 64) return -1;
    memcpy(out, p, 64);
    out[64] = 0;
    return 0;
}

/* user from the cookie, or -1 */
static int session_user(const Req *r, char user[33], uid_t *uid) {
    char tok[65];
    if (token_from_cookie(r->cookie, tok)) return -1;
    double now = rd_now();
    int ok = -1;
    rd_lock();
    for (int i = 0; i < 64; i++) {
        RdToken *t = &rd_shared->tokens[i];
        if (t->tok[0] && t->expires > now && ct_equal(t->tok, tok)) { snprintf(user, 33, "%s", t->user); *uid = t->uid; ok = 0; }
    }
    rd_unlock();
    if (ok == 0) {     /* the account may have changed since login */
        struct passwd *pw = getpwnam(user);
        if (!pw || pw->pw_uid != *uid || !sd_user_allowed(pw, 1)) ok = -1;
    }
    return ok;
}

static void token_drop_tok(const char *tok) {
    if (!tok[0]) return;
    rd_lock();
    for (int i = 0; i < 64; i++) if (ct_equal(rd_shared->tokens[i].tok, tok)) memset(&rd_shared->tokens[i], 0, sizeof rd_shared->tokens[i]);
    rd_unlock();
}

/* The login page, with an optional message (?e=1 password, ?e=2 wait). */
static void send_login(const Req *r) {
    respond(200, "OK", "text/html; charset=utf-8", NULL, _binary_src_remote_login_html_start,
            (size_t)(_binary_src_remote_login_html_end - _binary_src_remote_login_html_start));
    (void)r;
}

/* Same origin (protects the login form from other sites). */
static int same_origin(const Req *r) {
    if (!r->origin[0]) return 1;
    const char *o = strstr(r->origin, "://");
    return o && !strcmp(o + 3, r->host);
}

/* ---- WebSocket ---- */

static unsigned char *wout;
static size_t wout_len, wout_off, wout_cap;

static void wput(const void *p, size_t n) {
    if (wout_len + n > wout_cap) {
        size_t cap = wout_cap ? wout_cap * 2 : 1u << 20;
        while (cap < wout_len + n) cap *= 2;
        unsigned char *nb = realloc(wout, cap);
        if (!nb) return;
        wout = nb;
        wout_cap = cap;
    }
    memcpy(wout + wout_len, p, n);
    wout_len += n;
}

static void ws_frame(int opcode, const void *a, size_t an, const void *b, size_t bn) {
    unsigned char h[10];
    size_t n = an + bn, hl;
    h[0] = (unsigned char)(0x80 | opcode);
    if (n < 126) { h[1] = (unsigned char)n; hl = 2; }
    else if (n < 65536) { h[1] = 126; h[2] = (unsigned char)(n >> 8); h[3] = (unsigned char)n; hl = 4; }
    else { h[1] = 127; for (int i = 0; i < 8; i++) h[2 + i] = (unsigned char)((uint64_t)n >> (56 - 8 * i)); hl = 10; }
    wput(h, hl);
    if (an) wput(a, an);
    if (bn) wput(b, bn);
}

static void ws_text(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0 && (size_t)n < sizeof buf) ws_frame(1, buf, (size_t)n, NULL, 0);
}

/* text as a JSON string (for the clipboard) */
static void ws_clip(const char *t, size_t n) {
    size_t cap = n * 6 + 32, o = 0;
    char *j = malloc(cap);
    if (!j) return;
    o += (size_t)snprintf(j, cap, "{\"t\":\"clip\",\"text\":\"");
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)t[i];
        if (c == '"' || c == '\\') { j[o++] = '\\'; j[o++] = (char)c; }
        else if (c < 0x20) o += (size_t)snprintf(j + o, cap - o, "\\u%04x", c);
        else j[o++] = (char)c;
    }
    o += (size_t)snprintf(j + o, cap - o, "\"}");
    ws_frame(1, j, o, NULL, 0);
    free(j);
}

static int ws_flush(void) {
    while (wout_off < wout_len) {
        int w = SSL_write(ssl, wout + wout_off, (int)(wout_len - wout_off > (1u << 20) ? (1u << 20) : wout_len - wout_off));
        if (w <= 0) {
            int e = SSL_get_error(ssl, w);
            if (e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ) return 0;
            return -1;
        }
        wout_off += (size_t)w;
    }
    wout_off = wout_len = 0;
    return 0;
}

static int ws_handshake(const Req *r) {
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char k[128];
    snprintf(k, sizeof k, "%s%s", r->wskey, guid);
    unsigned char sha[20];
    unsigned int sl = 0;
    if (!EVP_Digest(k, strlen(k), sha, &sl, EVP_sha1(), NULL)) return -1;
    unsigned char acc[64];
    EVP_EncodeBlock(acc, sha, 20);
    char head[512];
    int n = snprintf(head, sizeof head, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n\r\n", acc);
    return tls_write_all(head, (size_t)n);
}

/* ---- bridge ---- */

static int sess = -1;                  /* session socket */
static uint32_t *fb;                   /* copy of the screen (to replace with PNG) */
static int W, H;
typedef struct { uint16_t x, y, w, h; } Box16;
static Box16 jpegs[512];
static int njpegs;
static double last_frame;

static void sess_send(int type, const void *a, size_t an) {
    unsigned char h[5];
    uint32_t len = (uint32_t)(1 + an);
    memcpy(h, &len, 4);
    h[4] = (unsigned char)type;
    if (write(sess, h, 5) != 5) return;
    if (an && write(sess, a, an) != (ssize_t)an) return;
}

static char my_token[65];              /* this connection's cookie (wiped on lock) */
static int locked;                     /* the session got locked: cut off */

static void send_rect(int x, int y, int w, int h, int allow_jpeg) {
    int len = 0, jpeg = 0;
    unsigned char *img = rd_encode(fb + (size_t)y * W + x, W, w, h, allow_jpeg, &len, &jpeg);
    if (!img) return;
    unsigned char hd[10] = { 1, (unsigned char)(x >> 8), (unsigned char)x, (unsigned char)(y >> 8), (unsigned char)y,
                             (unsigned char)(w >> 8), (unsigned char)w, (unsigned char)(h >> 8), (unsigned char)h, (unsigned char)jpeg };
    ws_frame(2, hd, 10, img, (size_t)len);
    free(img);
    if (jpeg && njpegs < 512) jpegs[njpegs++] = (Box16){ (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h };
}

/* Session message → browser. */
static void from_session(int type, const unsigned char *d, uint32_t n) {
    switch (type) {
    case RP_SIZE:
        if (n < 4) break;
        {
            uint16_t s[2];
            memcpy(s, d, 4);
            W = s[0];
            H = s[1];
            free(fb);
            fb = calloc((size_t)W * H, 4);
            njpegs = 0;
            ws_text("{\"t\":\"size\",\"w\":%d,\"h\":%d}", W, H);
        }
        break;
    case RP_RECT: {
        if (n < 8 || !fb) break;
        uint16_t r[4];
        memcpy(r, d, 8);
        if ((uint32_t)r[0] + r[2] > (uint32_t)W || (uint32_t)r[1] + r[3] > (uint32_t)H || n < 8 + (uint32_t)r[2] * r[3] * 4) break;
        for (int j = 0; j < r[3]; j++) memcpy(fb + (size_t)(r[1] + j) * W + r[0], d + 8 + (size_t)j * r[2] * 4, (size_t)r[2] * 4);
        /* whatever gets overwritten no longer needs replacing */
        for (int i = 0; i < njpegs; i++)
            if (jpegs[i].x >= r[0] && jpegs[i].y >= r[1] && jpegs[i].x + jpegs[i].w <= r[0] + r[2] && jpegs[i].y + jpegs[i].h <= r[1] + r[3]) jpegs[i--] = jpegs[--njpegs];
        /* screen in motion (consecutive frames): straight JPEG, replaced with PNG once it's still */
        send_rect(r[0], r[1], r[2], r[3], rd_now() - last_frame < 0.25 ? 2 : 1);
        break;
    }
    case RP_END:
        ws_frame(2, (unsigned char[]){ 3 }, 1, NULL, 0);
        last_frame = rd_now();
        break;
    case RP_CURSOR:
        if (n >= 5) {
            int16_t x, y;
            memcpy(&x, d, 2);
            memcpy(&y, d + 2, 2);
            ws_text("{\"t\":\"cur\",\"x\":%d,\"y\":%d,\"m\":%d}", x, y, d[4]);
        }
        break;
    case RP_SHAPE:
        if (n >= 8) {
            uint16_t s[4];
            memcpy(s, d, 8);
            if (n < 8 + (uint32_t)s[0] * s[1] * 4 || !s[0] || !s[1]) break;
            uint32_t *px = malloc((size_t)s[0] * s[1] * 4);
            if (!px) break;
            memcpy(px, d + 8, (size_t)s[0] * s[1] * 4);
            int len = 0;
            unsigned char *png = rd_encode_rgba(px, s[0], s[1], &len);
            free(px);
            if (png) {
                unsigned char hd[9] = { 2, (unsigned char)(s[0] >> 8), (unsigned char)s[0], (unsigned char)(s[1] >> 8), (unsigned char)s[1],
                                        (unsigned char)(s[2] >> 8), (unsigned char)s[2], (unsigned char)(s[3] >> 8), (unsigned char)s[3] };
                ws_frame(2, hd, 9, png, (size_t)len);
                free(png);
            }
        }
        break;
    case RP_CLIP: ws_clip((const char *)d, n); break;
    case RP_SESSION:
        /* the user locked their session: this browser has to log in again too */
        if (n >= 10 && memmem(d, n, "\"lock\"", 6)) {
            token_drop_tok(my_token);
            ws_text("{\"t\":\"bye\",\"lock\":1}");
            locked = 1;
        }
        break;
    default: break;
    }
}

/* Flat JSON from the browser: {"t":"m","x":1,"y":2} — number or string of a key */
static int jnum(const char *j, const char *key, long *out) {
    char k[24];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(j, k);
    if (!p) return -1;
    char *e;
    long v = strtol(p + strlen(k), &e, 10);
    if (e == p + strlen(k)) return -1;
    *out = v;
    return 0;
}

static int jstr(const char *j, const char *key, char *out, size_t n) {
    char k[24];
    snprintf(k, sizeof k, "\"%s\":\"", key);
    const char *p = strstr(j, k);
    if (!p) return -1;
    p += strlen(k);
    size_t o = 0;
    for (; *p && *p != '"' && o + 4 < n; p++) {
        if (*p != '\\') { out[o++] = *p; continue; }
        p++;
        if (*p == 'n') out[o++] = '\n';
        else if (*p == 't') out[o++] = '\t';
        else if (*p == 'r') out[o++] = '\r';
        else if (*p == 'u' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2]) && isxdigit((unsigned char)p[3]) && isxdigit((unsigned char)p[4])) {
            char hx[5] = { p[1], p[2], p[3], p[4], 0 };
            unsigned cp = (unsigned)strtol(hx, NULL, 16);
            p += 4;
            if (cp < 0x80) out[o++] = (char)cp;
            else if (cp < 0x800) { out[o++] = (char)(0xc0 | cp >> 6); out[o++] = (char)(0x80 | (cp & 63)); }
            else { out[o++] = (char)(0xe0 | cp >> 12); out[o++] = (char)(0x80 | ((cp >> 6) & 63)); out[o++] = (char)(0x80 | (cp & 63)); }
        } else if (*p) out[o++] = *p;
        else break;
    }
    out[o] = 0;
    return (int)o;
}

static void from_browser(const char *j, size_t n) {
    char t[12] = "";
    if (jstr(j, "t", t, sizeof t) < 0) return;
    long a = 0, b = 0;
    if (!strcmp(t, "m") && !jnum(j, "x", &a) && !jnum(j, "y", &b)) {
        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (a >= W) a = W - 1;
        if (b >= H) b = H - 1;
        int16_t p[2] = { (int16_t)a, (int16_t)b };
        sess_send(RP_PTR, p, 4);
    } else if (!strcmp(t, "b") && !jnum(j, "b", &a) && !jnum(j, "d", &b) && a >= 0 && a <= 2) {
        unsigned char p[2] = { (unsigned char)a, (unsigned char)(b != 0) };
        sess_send(RP_BTN, p, 2);
    } else if (!strcmp(t, "w") && !jnum(j, "d", &a)) {
        signed char d = (signed char)(a > 10 ? 10 : a < -10 ? -10 : a);
        sess_send(RP_WHEEL, &d, 1);
    } else if ((!strcmp(t, "s") || !strcmp(t, "pz")) && !jnum(j, "p", &a) && a >= 1 && a <= 3) {
        /* browser trackpad: fine scroll {t:'s', p, x, y} (1/256 px) or pinch {t:'pz', p, s, r}
         * (scale ×65536, rotation in degrees ×256) */
        long v1 = 0, v2 = 0;
        int sc = !strcmp(t, "s");
        if (jnum(j, sc ? "x" : "s", &v1) || jnum(j, sc ? "y" : "r", &v2)) return;
        long lim = sc ? 4000L * 256 : 20L * 65536;
        if (v1 > lim) v1 = lim;
        if (v1 < -lim) v1 = -lim;
        if (v2 > 360L * 256 * 4) v2 = 360L * 256 * 4;
        if (v2 < -360L * 256 * 4) v2 = -360L * 256 * 4;
        if (sc && (v2 > lim || v2 < -lim)) v2 = v2 > 0 ? lim : -lim;
        unsigned char p[9];
        int32_t w[2] = { (int32_t)v1, (int32_t)v2 };
        p[0] = (unsigned char)a;
        memcpy(p + 1, w, 8);
        sess_send(sc ? RP_SCROLL : RP_PINCH, p, 9);
    } else if (!strcmp(t, "k") && !jnum(j, "c", &a) && !jnum(j, "d", &b) && a > 0 && a < 0x2ff) {
        unsigned char p[3];
        uint16_t c = (uint16_t)a;
        memcpy(p, &c, 2);
        p[2] = (unsigned char)(b != 0);
        sess_send(RP_KEY, p, 3);
    } else if (!strcmp(t, "ack")) sess_send(RP_READY, NULL, 0);
    else if (!strcmp(t, "resize") && !jnum(j, "w", &a) && !jnum(j, "h", &b) && a >= 640 && b >= 480 && a <= 3840 && b <= 2400) {
        uint16_t sz[2] = { (uint16_t)(a & ~1), (uint16_t)(b & ~1) };      /* if the monitor shows it, the monitor wins */
        sess_send(RP_RESIZE, sz, 4);
    }
    else if (!strcmp(t, "ping") && !jnum(j, "ts", &a)) ws_text("{\"t\":\"pong\",\"ts\":%ld}", a);
    else if (!strcmp(t, "clip")) {
        char *txt = malloc(n + 1);
        if (txt) {
            int l = jstr(j, "text", txt, n + 1);
            if (l > 0) sess_send(RP_SETCLIP, txt, (size_t)l);
            free(txt);
        }
    }
}

/* Reads frames from the browser (masked). Returns -1 if it closed. */
static unsigned char *win;
static size_t win_len, win_cap;
static int ws_read(void) {
    for (;;) {
        if (win_cap - win_len < 16384) {
            size_t cap = win_cap ? win_cap * 2 : 65536;
            if (cap > (4u << 20)) return -1;
            unsigned char *nb = realloc(win, cap);
            if (!nb) return -1;
            win = nb;
            win_cap = cap;
        }
        int r = SSL_read(ssl, win + win_len, (int)(win_cap - win_len));
        if (r <= 0) {
            int e = SSL_get_error(ssl, r);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) break;
            return -1;
        }
        win_len += (size_t)r;
    }
    size_t off = 0;
    while (win_len - off >= 2) {
        unsigned char *f = win + off;
        int op = f[0] & 15, masked = f[1] & 0x80;
        uint64_t len = f[1] & 127;
        size_t hl = 2;
        if (len == 126) { if (win_len - off < 4) break; len = (uint64_t)f[2] << 8 | f[3]; hl = 4; }
        else if (len == 127) { if (win_len - off < 10) break; len = 0; for (int i = 0; i < 8; i++) len = len << 8 | f[2 + i]; hl = 10; }
        if (!masked || len > (2u << 20)) return -1;
        if (win_len - off < hl + 4 + len) break;
        unsigned char *mask = f + hl, *p = f + hl + 4;
        for (uint64_t i = 0; i < len; i++) p[i] ^= mask[i & 3];
        if (op == 8) return -1;
        if (op == 9) ws_frame(10, p, (size_t)len, NULL, 0);
        else if (op == 1 && (f[0] & 0x80)) {
            char save = (char)p[len];
            p[len] = 0;
            from_browser((const char *)p, (size_t)len);
            p[len] = (unsigned char)save;
        }
        off += hl + 4 + (size_t)len;
    }
    memmove(win, win + off, win_len - off);
    win_len -= off;
    return 0;
}

static void conn_register(const char *user) {
    rd_lock();
    for (int i = 0; i < RD_MAX_CONNS; i++)
        if (!rd_shared->conns[i].pid) {
            rd_shared->conns[i] = (RdConn){ .pid = getpid(), .since = rd_now() };
            snprintf(rd_shared->conns[i].ip, 48, "%s", peer);
            snprintf(rd_shared->conns[i].user, 33, "%s", user);
            rd_shared->version++;
            break;
        }
    rd_unlock();
}

static void bridge(const struct passwd *pw) {
    char hello[96];
    snprintf(hello, sizeof hello, "%s %s", peer, pw->pw_name);
    sess_send(RP_HELLO, hello, strlen(hello));
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
    ws_text("{\"t\":\"hello\",\"user\":\"%s\"}", pw->pw_name);
    unsigned char *sin = NULL;
    size_t sin_len = 0, sin_cap = 0;
    double last_input = rd_now();
    for (;;) {
        if (ws_flush()) return;
        struct pollfd p[2] = { { sock, (short)(POLLIN | (wout_len ? POLLOUT : 0)), 0 }, { sess, POLLIN, 0 } };
        /* while there is something to send, nothing more is read from the session (flow control) */
        if (wout_len > (4u << 20)) p[1].events = 0;
        int timeout = njpegs ? 200 : 15000;
        int r = SSL_pending(ssl) ? 1 : poll(p, 2, timeout);
        if (r < 0 && errno != EINTR) return;
        if (SSL_pending(ssl) || (p[0].revents & POLLIN)) {
            if (ws_read()) return;
            last_input = rd_now();
        }
        if (p[0].revents & (POLLERR | POLLHUP)) return;
        if (p[1].revents & (POLLERR | POLLHUP)) { ws_text("{\"t\":\"bye\"}"); ws_flush(); return; }
        if (p[1].revents & POLLIN) {
            for (;;) {
                if (sin_cap - sin_len < 262144) {
                    size_t cap = sin_cap ? sin_cap * 2 : 1u << 20;
                    if (cap > RP_MAX_MSG + (1u << 20)) return;
                    unsigned char *nb = realloc(sin, cap);
                    if (!nb) return;
                    sin = nb;
                    sin_cap = cap;
                }
                ssize_t n = recv(sess, sin + sin_len, sin_cap - sin_len, MSG_DONTWAIT);
                if (n < 0 && errno == EINTR) continue;
                if (n < 0 && errno == EAGAIN) break;
                if (n <= 0) { ws_text("{\"t\":\"bye\"}"); ws_flush(); return; }
                sin_len += (size_t)n;
            }
            size_t off = 0;
            while (sin_len - off >= 5) {
                uint32_t len;
                memcpy(&len, sin + off, 4);
                if (!len || len > RP_MAX_MSG) return;
                if (sin_len - off < 4 + (size_t)len) break;
                from_session(sin[off + 4], sin + off + 5, len - 1);
                off += 4 + len;
            }
            memmove(sin, sin + off, sin_len - off);
            sin_len -= off;
        }
        if (locked) { ws_flush(); return; }
        /* screen is still: whatever was JPEG is replaced with PNG (lossless) */
        if (njpegs && rd_now() - last_frame > 0.4 && wout_len < (1u << 20)) {
            int k = njpegs;
            njpegs = 0;
            for (int i = 0; i < k; i++) send_rect(jpegs[i].x, jpegs[i].y, jpegs[i].w, jpegs[i].h, 0);
            ws_frame(2, (unsigned char[]){ 4 }, 1, NULL, 0);    /* replacement (no ack) */
        }
        if (rd_now() - last_input > 60) return;     /* the browser sends a ping every 2 s */
    }
}

/* ---- one connection ---- */

void rd_serve(SSL_CTX *ctx, int fd, const char *ip) {
    sock = fd;
    snprintf(peer, sizeof peer, "%s", ip);
    char prefix[96];
    snprintf(prefix, sizeof prefix, "lord-sessiond[%d] %s", (int)getpid(), peer);
    sd_log_prefix(prefix);
    ssl = SSL_new(ctx);
    if (!ssl) return;
    SSL_set_fd(ssl, fd);
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    struct timeval tv = { 15, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (SSL_accept(ssl) <= 0) return;
    Req r;
    if (read_request(&r)) return;

    char user[33];
    uid_t uid = 0;
    int logged = session_user(&r, user, &uid) == 0;

    if (!strcmp(r.method, "POST") && !strcmp(r.path, "/login")) {
        if (!same_origin(&r)) {
            sd_log("ingreso rechazado: origen «%s» y host «%s»", r.origin, r.host);
            respond(403, "Forbidden", "text/plain", NULL, "origen", 6);
            return;
        }
        double wait = sd_fail_wait(peer);
        if (wait > 0) { redirect("/?e=2", NULL); return; }
        char u[64], pass[300];
        form_get(r.body, "user", u, sizeof u);
        form_get(r.body, "pass", pass, sizeof pass);
        int ok = sd_check_password(u, pass, 1);
        explicit_bzero(pass, sizeof pass);
        explicit_bzero(r.body, sizeof r.body);
        if (!ok) {
            sd_fail_note(peer, 1);
            sd_log("failed login for '%s'", sd_valid_username(u) ? u : "?");
            sleep(1);
            char to[64] = "/?e=1";
            if (sd_valid_username(u)) snprintf(to, sizeof to, "/?e=1&u=%.32s", u);
            redirect(to, NULL);
            return;
        }
        sd_fail_note(peer, 0);
        struct passwd *pw = getpwnam(u);
        char tok[65], cookie[160];
        new_token(pw->pw_uid, u, tok);
        snprintf(cookie, sizeof cookie, "Set-Cookie: lordos_remote=%s; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age=43200\r\n", tok);
        sd_log("%s logged in", u);
        redirect("/desk", cookie);
        return;
    }
    if (!strcmp(r.path, "/logout")) {
        char tok[65];
        if (!token_from_cookie(r.cookie, tok)) token_drop_tok(tok);
        redirect("/", "Set-Cookie: lordos_remote=; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age=0\r\n");
        return;
    }
    if (!strcmp(r.path, "/") || !strcmp(r.path, "/index.html")) {
        if (logged) redirect("/desk", NULL); else send_login(&r);
        return;
    }
    if (!strcmp(r.path, "/bg.png")) {             /* the login screen background */
        char p[600];
        snprintf(p, sizeof p, "%s", rd_conf.greeter_theme);
        char *slash = strrchr(p, '/');
        if (slash) snprintf(slash, sizeof p - (size_t)(slash - p), "/assets/background.png");
        int fd = open(p, O_RDONLY | O_CLOEXEC);
        struct stat st;
        char *img = NULL;
        if (fd >= 0 && !fstat(fd, &st) && st.st_size > 0 && st.st_size < (8 << 20) && (img = malloc((size_t)st.st_size)) &&
            read(fd, img, (size_t)st.st_size) == st.st_size)
            respond(200, "OK", "image/png", NULL, img, (size_t)st.st_size);
        else respond(404, "Not Found", "text/plain", NULL, "no", 2);
        free(img);
        if (fd >= 0) close(fd);
        return;
    }
    if (!strcmp(r.path, "/desk")) {
        if (!logged) { redirect("/", NULL); return; }
        respond(200, "OK", "text/html; charset=utf-8", NULL, _binary_src_remote_client_html_start,
                (size_t)(_binary_src_remote_client_html_end - _binary_src_remote_client_html_start));
        return;
    }
    if (!strcmp(r.path, "/ws")) {
        if (!logged || strcasecmp(r.upgrade, "websocket") || !r.wskey[0] || !same_origin(&r)) {
            respond(403, "Forbidden", "text/plain", NULL, "no", 2);
            return;
        }
        struct passwd *pw = getpwnam(user);
        if (!pw) return;
        struct passwd me = *pw;
        static char name[33], dir[512];
        snprintf(name, sizeof name, "%s", pw->pw_name);
        snprintf(dir, sizeof dir, "%s", pw->pw_dir);
        me.pw_name = name;
        me.pw_dir = dir;
        int w = query_int(r.query, "w", 1280), h = query_int(r.query, "h", 800);
        if (w < 800) w = 800;
        if (h < 600) h = 600;
        if (w > 2560) w = 2560;
        if (h > 1600) h = 1600;
        sess = sd_session_get(&me, w & ~1, h & ~1);
        token_from_cookie(r.cookie, my_token);
        if (sess < 0) { respond(503, "Service Unavailable", "text/plain", NULL, "no session", 10); return; }
        if (ws_handshake(&r)) return;
        conn_register(name);
        /* from here on, no root: TLS, compression and bridge as the user */
        if (initgroups(name, me.pw_gid) || setgid(me.pw_gid) || setuid(me.pw_uid) || setuid(0) == 0) {
            sd_log("could not drop privileges");
            return;
        }
        sd_log("connected to %s's session", name);
        bridge(&me);
        sd_log("disconnected");
        return;
    }
    respond(404, "Not Found", "text/plain", NULL, "no", 2);
}

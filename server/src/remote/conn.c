/* conn.c — one remote desktop connection, in a child process of lord-sessiond.
 *
 *   1. TLS (OpenSSL) and one HTTP request, parsed strictly (rsec.c) under a deadline: the login
 *      page (GET /), the login itself (POST /login), the desktop (GET /desk), logout (POST /logout)
 *      or the WebSocket (GET /ws). Anything else is refused and the connection closed.
 *   2. Login: system user and password, checked against /etc/shadow with crypt(3) (sess.c). Only
 *      regular accounts (uid 1000..59999, a shell from /etc/shells, not locked, not expired, never
 *      root; if the configuration asks for it, in the lordos-remote group). Unknown and refused
 *      accounts cost the same time as a wrong password. 3 failures block the source (lockout.c):
 *      15 min, then 1 h, then 24 h. A random cookie (__Host-, Secure, HttpOnly, SameSite=Strict,
 *      only its SHA-256 is kept) lasts 12 hours.
 *   3. WebSocket: finds the user's session (the same one that may be showing on the monitor) or
 *      creates it (sess.c, docs/SESSIONS.md). Then this process forks the bridge, which unmaps the
 *      shared state, closes every other descriptor and DROPS root for good: TLS, compression and
 *      the bridge run as the user. This root process only waits, and when the bridge says the
 *      session was locked it ends every cookie of that user.
 *   4. Bridge: session rectangles → PNG/JPEG → browser; mouse, keyboard and clipboard from the
 *      browser (validated, rsec.c rs_browser_input) → session. Flow control: the browser
 *      acknowledges each frame ("ack") and only then does the session build the next one.
 * Browser protocol and the security analysis: docs/REMOTE.md. */
#include "sessiond.h"
#include "proto.h"
#include "rsec.h"

#include <ctype.h>
#include <dirent.h>
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
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>

extern const char _binary_src_remote_login_html_start[], _binary_src_remote_login_html_end[];
extern const char _binary_src_remote_client_html_start[], _binary_src_remote_client_html_end[];

#define HEAD_SECONDS 15         /* TLS handshake + request head + body, from accept */
#define ALARM_SECONDS 90        /* hard limit before the bridge (covers starting a session) */
#define TOKEN_SECONDS (12 * 3600)

static SSL *ssl;
static int sock = -1;
static char peer[48];           /* address as shown */
static char peer_key[48];       /* lockout key (IPv4, or the IPv6 /64) */
static double deadline;
static int lock_fd = -1;        /* bridge → root parent: "L" = the session was locked */

static double mono(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}

/* A failed login is answered exactly FAIL_SECONDS after the password check began, whatever the
 * account (unknown, refused or a wrong password): the time says nothing about who exists. */
#define FAIL_SECONDS 1.25
static void sleep_until(double t) {
    struct timespec ts = { (time_t)t, (long)((t - (double)(time_t)t) * 1e9) };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {}
}

/* ---- blocking TLS I/O (before the bridge) ---- */

static int wait_io(int out) {
    if (!out && SSL_pending(ssl)) return 0;
    double left = deadline - mono();
    if (left <= 0) return -1;
    struct pollfd p = { sock, (short)(out ? POLLOUT : POLLIN), 0 };
    int ms = (int)(left * 1000) + 1;
    return poll(&p, 1, ms) > 0 ? 0 : -1;
}

static int tls_write_all(const void *p, size_t n) {
    const char *c = p;
    while (n) {
        int w = SSL_write(ssl, c, n > 1u << 30 ? 1 << 30 : (int)n);
        if (w <= 0) {
            int e = SSL_get_error(ssl, w);
            if ((e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ) && !wait_io(e == SSL_ERROR_WANT_WRITE)) continue;
            return -1;
        }
        c += w;
        n -= (size_t)w;
    }
    return 0;
}

/* ---- responses ---- */

/* CSP hashes of the inline <script> and <style> of each page, so nothing else can run there */
typedef struct { const char *start, *end; char script[64], style[64]; } Page;
static Page login_page = { _binary_src_remote_login_html_start, _binary_src_remote_login_html_end, "", "" };
static Page desk_page = { _binary_src_remote_client_html_start, _binary_src_remote_client_html_end, "", "" };

static void hash_block(const char *s, const char *e, const char *open, const char *close, char out[64]) {
    out[0] = 0;
    size_t ol = strlen(open);
    const char *a = NULL;
    for (const char *p = s; p + ol <= e; p++) if (!memcmp(p, open, ol)) { a = p + ol; break; }
    if (!a) return;
    size_t cl = strlen(close);
    const char *b = NULL;
    for (const char *p = a; p + cl <= e; p++) if (!memcmp(p, close, cl)) { b = p; break; }
    if (!b) return;
    unsigned char md[32];
    unsigned int ml = 0;
    if (!EVP_Digest(a, (size_t)(b - a), md, &ml, EVP_sha256(), NULL)) return;
    EVP_EncodeBlock((unsigned char *)out, md, 32);
}

static void page_hashes(Page *p) {
    if (p->script[0]) return;
    hash_block(p->start, p->end, "<script>", "</script>", p->script);
    hash_block(p->start, p->end, "<style>", "</style>", p->style);
}

static const char *status_text(int code) {
    switch (code) {
    case 200: return "OK";
    case 303: return "See Other";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Content Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 426: return "Upgrade Required";
    case 431: return "Request Header Fields Too Large";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

/* Every answer: no caching, no framing, no referrer to other sites (same-origin, not no-referrer: with
 * no-referrer browsers send "Origin: null" on our own form posts, and the Origin check needs it), no sniffing, no powerful features, and a CSP
 * that only lets the page's own inline code run (or nothing, for non-HTML answers). No HSTS: the
 * certificate is self-signed, and HSTS would make its warning impossible to get past. */
static void respond_page(int code, const char *type, const char *extra, const char *body, size_t len, const Page *pg, const char *host) {
    char csp[600];
    if (pg) {
        char wss[300] = "";
        if (rs_valid_host(host)) snprintf(wss, sizeof wss, " wss://%s", host);
        snprintf(csp, sizeof csp,
                 "default-src 'none'; script-src 'sha256-%s'; style-src 'sha256-%s'; img-src 'self' blob: data:; "
                 "connect-src 'self'%s; form-action 'self'; base-uri 'none'; frame-ancestors 'none'",
                 pg->script, pg->style, wss);
    } else snprintf(csp, sizeof csp, "default-src 'none'; frame-ancestors 'none'; form-action 'none'; base-uri 'none'");
    char head[2048];
    int n = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n"
        "Cache-Control: no-store, max-age=0\r\nPragma: no-cache\r\n"
        "X-Frame-Options: DENY\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: same-origin\r\n"
        "Cross-Origin-Opener-Policy: same-origin\r\nCross-Origin-Resource-Policy: same-origin\r\n"
        "Permissions-Policy: camera=(), microphone=(), geolocation=(), usb=(), serial=(), hid=(), bluetooth=(), payment=(), display-capture=()\r\n"
        "Content-Security-Policy: %s\r\n%s\r\n", code, status_text(code), type, len, csp, extra ? extra : "");
    if (n < 0 || (size_t)n >= sizeof head) return;
    tls_write_all(head, (size_t)n);
    if (len) tls_write_all(body, len);
}

static void respond(int code, const char *extra) {
    const char *t = status_text(code);
    respond_page(code, "text/plain; charset=utf-8", extra, t, strlen(t), NULL, NULL);
}

static void redirect(const char *to, const char *extra) {
    char h[700];
    snprintf(h, sizeof h, "Location: %s\r\n%s", to, extra ? extra : "");
    respond_page(303, "text/plain; charset=utf-8", h, "", 0, NULL, NULL);
}

/* The login page; with an error code e (1 wrong, 2 wait, 4 blocked) marked on <html data-e>, for the
 * answers that can't be a redirect (a blocked source's next request is refused). */
static void send_login(int code, int e, const char *host) {
    page_hashes(&login_page);
    size_t len = (size_t)(login_page.end - login_page.start);
    static const char tag[] = "<html lang=\"en\" translate=\"no\">";
    const char *at = e ? memmem(login_page.start, len, tag, sizeof tag - 1) : NULL;
    if (!at) { respond_page(code, "text/html; charset=utf-8", NULL, login_page.start, len, &login_page, host); return; }
    char mark[48];
    int ml = snprintf(mark, sizeof mark, "<html lang=\"en\" translate=\"no\" data-e=\"%d\">", e);
    char *b = malloc(len + (size_t)ml);
    if (!b) return;
    size_t pre = (size_t)(at - login_page.start);
    memcpy(b, login_page.start, pre);
    memcpy(b + pre, mark, (size_t)ml);
    memcpy(b + pre + (size_t)ml, at + sizeof tag - 1, len - pre - (sizeof tag - 1));
    respond_page(code, "text/html; charset=utf-8", NULL, b, len - (sizeof tag - 1) + (size_t)ml, &login_page, host);
    free(b);
}

/* ---- HTTP request ---- */

static char reqbuf[RS_MAX_HEAD + RS_MAX_BODY + 1];
static char *body;
static size_t body_len;

/* 0, -status to answer with, or -1 to just close */
static int read_request(RsReq *r) {
    size_t len = 0;
    int rc;
    while ((rc = rs_parse_head(reqbuf, len, r)) == RS_NEED_MORE) {
        if (len >= RS_MAX_HEAD) return -431;
        if (wait_io(0)) return len ? -408 : -1;
        int n = SSL_read(ssl, reqbuf + len, (int)(RS_MAX_HEAD - len));
        if (n <= 0) {
            int e = SSL_get_error(ssl, n);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
            return -1;
        }
        len += (size_t)n;
    }
    if (rc < 0) return rc;
    size_t cl = r->content_length > 0 ? (size_t)r->content_length : 0, have = len - r->head_len;
    if (have > cl) return -400;                 /* one request per connection: nothing after it */
    body = reqbuf + r->head_len;
    while (have < cl) {
        if (wait_io(0)) return -408;
        int n = SSL_read(ssl, body + have, (int)(cl - have));
        if (n <= 0) {
            int e = SSL_get_error(ssl, n);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
            return -1;
        }
        have += (size_t)n;
    }
    body[cl] = 0;
    body_len = cl;
    return 0;
}

/* "w=1280&h=800": a number of up to 5 digits, or def */
static int query_num(const char *q, const char *key, int def) {
    size_t kl = strlen(key);
    for (const char *p = q; *p;) {
        const char *e = strchr(p, '&');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l > kl + 1 && l <= kl + 6 && !strncmp(p, key, kl) && p[kl] == '=') {
            int v = 0, ok = 1;
            for (size_t i = kl + 1; i < l; i++) { if (!isdigit((unsigned char)p[i])) { ok = 0; break; } v = v * 10 + (p[i] - '0'); }
            if (ok) return v;
        }
        p += l;
        if (*p == '&') p++;
    }
    return def;
}

/* Same origin, strictly: browsers always send Origin on a POST and on a WebSocket; a request
 * without it is not from our page. Sec-Fetch-Site, when sent, must agree. */
static int origin_ok(const RsReq *r) {
    if (!rs_same_origin(r->origin, r->host)) return 0;
    if (r->fetch_site[0] && strcmp(r->fetch_site, "same-origin") && strcmp(r->fetch_site, "none")) return 0;
    return 1;
}

/* ---- cookies (shared memory; only root processes map it) ---- */

static void sha256(const void *p, size_t n, unsigned char out[32]) {
    unsigned int l = 0;
    if (!EVP_Digest(p, n, out, &l, EVP_sha256(), NULL)) memset(out, 0, 32);
}

/* a digest of the account's shadow hash: if the password changes or gets locked, cookies end */
static void pw_digest(const char *user, unsigned char out[16]) {
    struct spwd *sp = getspnam(user);
    unsigned char md[32];
    const char *h = sp && sp->sp_pwdp ? sp->sp_pwdp : "";
    sha256(h, strlen(h), md);
    memcpy(out, md, 16);
    explicit_bzero(md, sizeof md);
}

static int new_token(uid_t uid, const char *user, char out[65]) {
    unsigned char r[32];
    if (getrandom(r, sizeof r, 0) != sizeof r) { out[0] = 0; return -1; }
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", r[i]);
    explicit_bzero(r, sizeof r);
    RdToken t = { .uid = uid, .expires = rd_now() + TOKEN_SECONDS };
    sha256(out, 64, t.hash);
    pw_digest(user, t.pwh);
    snprintf(t.user, sizeof t.user, "%.32s", user);
    double now = rd_now();
    rd_lock();
    int slot = 0;
    for (int i = 0; i < 64; i++) {
        RdToken *x = &rd_shared->tokens[i];
        if (x->expires <= now) { slot = i; break; }
        if (x->expires < rd_shared->tokens[slot].expires) slot = i;
    }
    rd_shared->tokens[slot] = t;
    rd_unlock();
    return 0;
}

static void token_drop_hash(const unsigned char h[32]) {
    rd_lock();
    for (int i = 0; i < 64; i++)
        if (!CRYPTO_memcmp(rd_shared->tokens[i].hash, h, 32)) memset(&rd_shared->tokens[i], 0, sizeof rd_shared->tokens[i]);
    rd_unlock();
}

void rd_tokens_drop_uid(uid_t uid) {
    rd_lock();
    for (int i = 0; i < 64; i++)
        if (rd_shared->tokens[i].expires > 0 && rd_shared->tokens[i].uid == uid) memset(&rd_shared->tokens[i], 0, sizeof rd_shared->tokens[i]);
    rd_unlock();
}

void rd_tokens_clear(void) {
    rd_lock();
    memset(rd_shared->tokens, 0, sizeof rd_shared->tokens);
    rd_unlock();
}

/* user from the cookie, or -1; *hash = the cookie's digest (to drop it on logout) */
static int session_user(const RsReq *r, char user[33], uid_t *uid, unsigned char hash[32]) {
    char tok[65];
    memset(hash, 0, 32);
    if (rs_cookie_token(r->cookie, RD_COOKIE, tok)) return -1;
    sha256(tok, 64, hash);
    explicit_bzero(tok, sizeof tok);
    double now = rd_now();
    int ok = -1;
    unsigned char pwh[16];
    rd_lock();
    for (int i = 0; i < 64; i++) {
        RdToken *t = &rd_shared->tokens[i];
        if (t->expires > now && !CRYPTO_memcmp(t->hash, hash, 32)) {
            snprintf(user, 33, "%s", t->user);
            *uid = t->uid;
            memcpy(pwh, t->pwh, 16);
            ok = 0;
        }
    }
    rd_unlock();
    if (ok == 0) {     /* the account may have changed since login */
        struct passwd *pw = getpwnam(user);
        unsigned char now_pwh[16];
        pw_digest(user, now_pwh);
        if (!pw || pw->pw_uid != *uid || !sd_remote_account_ok(user) || CRYPTO_memcmp(now_pwh, pwh, 16)) {
            token_drop_hash(hash);
            ok = -1;
        }
    }
    return ok;
}

#define CLEAR_COOKIES "Set-Cookie: " RD_COOKIE "=; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age=0\r\n" \
                      "Set-Cookie: lordos_remote=; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age=0\r\n"

/* ---- login ---- */

static void push_notice(const char *key, double until, int strikes) {
    unsigned i = rd_shared->notice_seq % RD_NOTICES;
    RdNotice *n = &rd_shared->notices[i];
    snprintf(n->key, sizeof n->key, "%s", key);
    n->until = until;
    n->at = rd_now();
    n->strikes = strikes;
    rd_shared->notice_seq++;
}

/* the recent failed sign-ins (under rd_lock): one row per source and attempted name */
static void note_failure(const char *key, const char *user) {
    double now = rd_now();
    RdFailLog *slot = NULL, *old = &rd_shared->faillog[0];
    for (int i = 0; i < RD_FAILLOG; i++) {
        RdFailLog *f = &rd_shared->faillog[i];
        if (f->count && !strcmp(f->key, key) && !strcmp(f->user, user)) { slot = f; break; }
        if (!f->count) { if (old->count) old = f; }
        else if (old->count && f->last < old->last) old = f;
    }
    if (!slot) {
        slot = old;
        memset(slot, 0, sizeof *slot);
        snprintf(slot->key, sizeof slot->key, "%s", key);
        snprintf(slot->user, sizeof slot->user, "%s", user);
        slot->first = now;
    }
    slot->count++;
    slot->last = now;
}

static void handle_login(const RsReq *r) {
    if (r->query[0]) { respond(400, NULL); return; }            /* credentials never travel in a URL */
    if (!origin_ok(r)) {
        char o[64], h[64], f[24];
        rs_log_clean(r->origin, strlen(r->origin), o, sizeof o);
        rs_log_clean(r->host, strlen(r->host), h, sizeof h);
        rs_log_clean(r->fetch_site, strlen(r->fetch_site), f, sizeof f);
        sd_log("login refused: not from this page (Origin \"%s\", Host \"%s\", Sec-Fetch-Site \"%s\")", o, h, f);
        respond(403, NULL);
        return;
    }
    if (!rs_form_type(r->ctype)) { respond(415, NULL); return; }
    rd_lock();
    double left = rl_blocked(&rd_shared->lockout, peer_key, rd_now());
    rd_unlock();
    if (left > 0) { send_login(403, 4, r->host); return; }
    if (sd_fail_wait(peer_key) > 0) { redirect("/?e=2", NULL); return; }

    char user[33], pass[RS_PASS_MAX + 1], raw[33];
    size_t plen = 0;
    int fr = rs_parse_login(body, body_len, user, pass, &plen, raw);
    explicit_bzero(reqbuf, sizeof reqbuf);
    if (fr == RS_FORM_BAD) {
        sd_log("login refused: malformed form");
        respond(400, NULL);
        return;
    }
    int ok = 0;
    char ukey[40] = "";
    double t0 = mono();
    if (fr == RS_FORM_OK) {
        snprintf(ukey, sizeof ukey, "u:%s", user);
        double d = rl_user_delay(sd_fail_count(ukey));        /* per account: slower, never locked */
        if (d > 0) sleep((unsigned)d);
        t0 = mono();
        ok = sd_check_password(user, pass, 1);
    }
    explicit_bzero(pass, sizeof pass);
    if (!ok) {
        sd_fail_note(peer_key, 1);
        if (ukey[0]) sd_fail_note(ukey, 1);
        double until = 0;
        int strikes = 0;
        rd_lock();
        int blocked = rl_fail(&rd_shared->lockout, peer_key, rd_now(), &until, &strikes);
        if (blocked) push_notice(peer_key, until, strikes);
        note_failure(peer_key, raw[0] ? raw : "?");
        rd_shared->lockout_dirty = 1;
        rd_shared->version++;
        rd_unlock();
        sd_log("failed login for \"%s\"%s", raw[0] ? raw : "?", fr == RS_FORM_USER ? " (not a valid user name)" : "");
        if (blocked) {
            sd_log("blocked %s for %.0f minutes after %d failed logins (block %d in 24 h)", peer_key, (until - rd_now()) / 60 + 0.5,
                   RL_FAILS, strikes);
            sleep_until(t0 + FAIL_SECONDS);
            send_login(403, 4, r->host);
            return;
        }
        sleep_until(t0 + FAIL_SECONDS);
        redirect("/?e=1", NULL);
        return;
    }
    rd_lock();
    rl_success(&rd_shared->lockout, peer_key);
    rd_shared->lockout_dirty = 1;
    rd_unlock();
    sd_fail_note(peer_key, 0);
    sd_fail_note(ukey, 0);
    struct passwd *pw = getpwnam(user);
    if (!pw) { respond(503, NULL); return; }
    /* a cookie the browser already had is never kept (no fixation): it ends here */
    unsigned char oldh[32];
    char olduser[33];
    uid_t olduid;
    if (session_user(r, olduser, &olduid, oldh) == 0) token_drop_hash(oldh);
    char tok[65], cookie[400];
    if (new_token(pw->pw_uid, user, tok)) { respond(503, NULL); return; }
    snprintf(cookie, sizeof cookie, "Set-Cookie: " RD_COOKIE "=%s; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age=%d\r\n"
             "Set-Cookie: lordos_remote=; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age=0\r\n", tok, TOKEN_SECONDS);
    explicit_bzero(tok, sizeof tok);
    sd_log("%s logged in", user);
    redirect("/desk", cookie);
    explicit_bzero(cookie, sizeof cookie);
}

/* ---- WebSocket ---- */

static unsigned char *wout;
static size_t wout_len, wout_off, wout_cap;
static unsigned long long bytes_rx, bytes_tx;   /* from / to the browser, reported to the root parent */

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

static void ws_text(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void ws_text(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0 && (size_t)n < sizeof buf) ws_frame(1, buf, (size_t)n, NULL, 0);
}

static void ws_close(int code) {
    unsigned char c[2] = { (unsigned char)(code >> 8), (unsigned char)code };
    ws_frame(8, c, 2, NULL, 0);
}

/* text as a JSON string (for the clipboard); invalid UTF-8 from the session is not sent */
static void ws_clip(const char *t, size_t n) {
    if (n > RS_CLIP_MAX || !rs_utf8_valid((const unsigned char *)t, n)) return;
    size_t cap = n * 6 + 32, o = 0;
    char *j = malloc(cap);
    if (!j) return;
    o += (size_t)snprintf(j, cap, "{\"t\":\"clip\",\"text\":\"");
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)t[i];
        if (c == '"' || c == '\\') { j[o++] = '\\'; j[o++] = (char)c; }
        else if (c < 0x20 || c == 0x7f) o += (size_t)snprintf(j + o, cap - o, "\\u%04x", c);
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
        bytes_tx += (unsigned long long)w;
    }
    wout_off = wout_len = 0;
    return 0;
}

static int ws_handshake(const RsReq *r) {
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

/* ---- bridge (runs as the user) ---- */

static int sess = -1;                  /* session socket */
static uint32_t *fb;                   /* copy of the screen (to replace with PNG) */
static int W, H;
typedef struct { uint16_t x, y, w, h; } Box16;
static Box16 jpegs[512];
static int njpegs;
static double last_frame;
static int locked, closing;

static void sess_send(int type, const void *a, size_t an) {
    unsigned char h[5];
    uint32_t len = (uint32_t)(1 + an);
    memcpy(h, &len, 4);
    h[4] = (unsigned char)type;
    if (write(sess, h, 5) != 5) return;
    if (an && write(sess, a, an) != (ssize_t)an) return;
}

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
            if (!s[0] || !s[1] || s[0] > 8192 || s[1] > 8192) break;
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
            ws_text("{\"t\":\"cur\",\"x\":%d,\"y\":%d,\"m\":%d}", x, y, d[4] > 2 ? 0 : d[4]);
        }
        break;
    case RP_SHAPE:
        if (n >= 8) {
            uint16_t s[4];
            memcpy(s, d, 8);
            if (!s[0] || !s[1] || s[0] > 512 || s[1] > 512 || n < 8 + (uint32_t)s[0] * s[1] * 4) break;
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
        /* the user locked their session: every browser of this user has to log in again */
        if (n >= 10 && memmem(d, n, "\"lock\"", 6)) {
            if (lock_fd >= 0 && write(lock_fd, "L\n", 2) != 2) { /* the root parent is gone: the cookie expires anyway */ }
            ws_text("{\"t\":\"bye\",\"lock\":1}");
            locked = 1;
        }
        break;
    default: break;
    }
}

/* One validated browser message → session. */
static void from_browser(const char *j, size_t n) {
    char *sb = malloc(n + 1);
    if (!sb) return;
    RsJson o;
    RsInput in;
    if (rs_json_parse(j, n, &o, sb, n + 1) == 0) {
        switch (rs_browser_input(&o, W, H, &in)) {
        case RS_IN_PTR: { int16_t p[2] = { (int16_t)in.a, (int16_t)in.b }; sess_send(RP_PTR, p, 4); break; }
        case RS_IN_BTN: { unsigned char p[2] = { (unsigned char)in.a, (unsigned char)in.b }; sess_send(RP_BTN, p, 2); break; }
        case RS_IN_WHEEL: { signed char d = (signed char)in.a; sess_send(RP_WHEEL, &d, 1); break; }
        case RS_IN_SCROLL:
        case RS_IN_PINCH: {
            unsigned char p[9];
            int32_t w[2] = { in.v1, in.v2 };
            p[0] = (unsigned char)in.a;
            memcpy(p + 1, w, 8);
            sess_send(in.type == RS_IN_SCROLL ? RP_SCROLL : RP_PINCH, p, 9);
            break;
        }
        case RS_IN_KEY: {
            unsigned char p[3];
            uint16_t c = (uint16_t)in.a;
            memcpy(p, &c, 2);
            p[2] = (unsigned char)in.b;
            sess_send(RP_KEY, p, 3);
            break;
        }
        case RS_IN_ACK: sess_send(RP_READY, NULL, 0); break;
        case RS_IN_RESIZE: { uint16_t sz[2] = { (uint16_t)in.a, (uint16_t)in.b }; sess_send(RP_RESIZE, sz, 4); break; }
        case RS_IN_PING: ws_text("{\"t\":\"pong\",\"ts\":%lld}", in.ts); break;
        case RS_IN_CLIP: sess_send(RP_SETCLIP, in.text, in.tlen); break;
        default: break;
        }
    }
    explicit_bzero(sb, n + 1);
    free(sb);
}

static void on_ws(void *ud, int op, unsigned char *data, size_t len) {
    (void)ud;
    if (closing) return;
    if (op == 1) from_browser((const char *)data, len);
    else if (op == 9) ws_frame(10, data, len, NULL, 0);
    else if (op == 8) { ws_close(1000); closing = 1; }
    else if (op == 2) { ws_close(1003); closing = 1; }        /* our page never sends binary */
}

/* Reads frames from the browser (rsec.c). Returns -1 if the connection is over. */
static unsigned char *win;
static size_t win_len, win_cap;
static RsWs wsr;

static int ws_read(void) {
    for (;;) {
        if (win_cap - win_len < 16384) {
            size_t cap = win_cap ? win_cap * 2 : 65536;
            if (cap > RS_WS_MAX_MSG + 65536) return -1;
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
        bytes_rx += (unsigned long long)r;
    }
    size_t used = 0;
    int rc = rs_ws_consume(&wsr, win, win_len, &used, on_ws, NULL);
    memmove(win, win + used, win_len - used);
    win_len -= used;
    if (rc) { ws_close(rc); closing = 1; }
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

static int conns_full(void) {
    int n = 0;
    rd_lock();
    for (int i = 0; i < RD_MAX_CONNS; i++) if (rd_shared->conns[i].pid) n++;
    rd_unlock();
    return n >= RD_MAX_CONNS;
}

static void bridge(const struct passwd *pw) {
    char hello[96];
    snprintf(hello, sizeof hello, "%s %s", peer, pw->pw_name);
    sess_send(RP_HELLO, hello, strlen(hello));
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
    ws_text("{\"t\":\"hello\",\"user\":\"%s\"}", pw->pw_name);
    rs_ws_init(&wsr, RS_WS_MAX_MSG);
    unsigned char *sin = NULL;
    size_t sin_len = 0, sin_cap = 0;
    double last_input = rd_now(), last_report = 0;
    for (;;) {
        if (ws_flush()) return;
        if (lock_fd >= 0 && rd_now() - last_report >= 2) {      /* traffic, for Settings and Monitor */
            char line[64];
            int n = snprintf(line, sizeof line, "T %llu %llu\n", bytes_rx, bytes_tx);
            if (write(lock_fd, line, (size_t)n) != n) { /* parent gone */ }
            last_report = rd_now();
        }
        if (closing) {                         /* our close frame went out (or will not): done */
            for (int i = 0; i < 20 && wout_len; i++) { struct pollfd q = { sock, POLLOUT, 0 }; poll(&q, 1, 100); if (ws_flush()) break; }
            return;
        }
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

/* ---- privilege drop (in the bridge process) ---- */

static void close_all_but(const int *keep, int nkeep) {
    int fds[1024], n = 0;
    DIR *d = opendir("/proc/self/fd");
    if (d) {
        struct dirent *e;
        int dfd = dirfd(d);
        while ((e = readdir(d)) && n < 1024) {
            char *end;
            long fd = strtol(e->d_name, &end, 10);
            if (*end || fd < 3 || fd == dfd) continue;
            int k = 0;
            for (int i = 0; i < nkeep; i++) if (keep[i] == fd) k = 1;
            if (!k) fds[n++] = (int)fd;
        }
        closedir(d);
    }
    for (int i = 0; i < n; i++) close(fds[i]);
}

/* For good: no shared state, no descriptor but the three it needs, no environment, the user's
 * groups and ids (real, effective and saved), checked; no new privileges, no core dumps, and it dies
 * with its root parent. */
static int drop_root(const struct passwd *pw, pid_t parent) {
    if (rd_shared) munmap(rd_shared, sizeof *rd_shared);
    rd_shared = NULL;
    int keep[3] = { sock, sess, lock_fd };
    close_all_but(keep, 3);
    clearenv();
    struct rlimit core = { 0, 0 };
    setrlimit(RLIMIT_CORE, &core);
    if (pw->pw_uid == 0 || pw->pw_gid == 0) return -1;
    if (initgroups(pw->pw_name, pw->pw_gid) || setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
        setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid)) return -1;
    uid_t ru, eu, su;
    gid_t rg, eg, sg;
    if (getresuid(&ru, &eu, &su) || getresgid(&rg, &eg, &sg)) return -1;
    if (ru != pw->pw_uid || eu != pw->pw_uid || su != pw->pw_uid || rg != pw->pw_gid || eg != pw->pw_gid || sg != pw->pw_gid) return -1;
    if (setuid(0) == 0 || seteuid(0) == 0 || setgid(0) == 0 || setegid(0) == 0) return -1;
    gid_t groups[256];
    int ng = getgroups(256, groups);
    for (int i = 0; i < ng; i++) if (groups[i] == 0) return -1;
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) || prctl(PR_SET_DUMPABLE, 0, 0, 0, 0)) return -1;
    if (prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0) || getppid() != parent) return -1;
    return 0;
}

/* ---- one connection ---- */

static void handle_ws(const RsReq *r, int logged, const char *user, uid_t uid) {
    if (!logged) { respond(403, NULL); return; }
    if (!origin_ok(r) || !rs_has_token(r->upgrade, "websocket") || !rs_has_token(r->connection, "upgrade") || !rs_valid_wskey(r->wskey)) {
        respond(400, NULL);
        return;
    }
    if (strcmp(r->wsversion, "13")) { respond(426, "Sec-WebSocket-Version: 13\r\n"); return; }
    if (conns_full()) { respond(503, NULL); return; }
    struct passwd *pw = getpwnam(user);
    if (!pw || pw->pw_uid != uid) { respond(403, NULL); return; }
    struct passwd me = *pw;
    static char name[33], dir[512];
    snprintf(name, sizeof name, "%s", pw->pw_name);
    snprintf(dir, sizeof dir, "%s", pw->pw_dir);
    me.pw_name = name;
    me.pw_dir = dir;
    int w = query_num(r->query, "w", 1280), h = query_num(r->query, "h", 800);
    if (w < 800) w = 800;
    if (h < 600) h = 600;
    if (w > 2560) w = 2560;
    if (h > 1600) h = 1600;
    sess = sd_session_get(&me, w & ~1, h & ~1);
    if (sess < 0) { respond(503, NULL); return; }
    if (ws_handshake(r)) return;
    conn_register(name);
    int pp[2];
    if (pipe2(pp, O_CLOEXEC)) return;
    pid_t parent = getpid();
    alarm(0);
    pid_t b = fork();
    if (b < 0) return;
    if (b == 0) {
        close(pp[0]);
        lock_fd = pp[1];
        /* from here on, no root: TLS, compression and bridge as the user */
        if (drop_root(&me, parent)) { sd_log("could not drop privileges"); _exit(1); }
        sd_log("connected to %s's session", name);
        bridge(&me);
        sd_log("disconnected");
        _exit(0);
    }
    /* the root side keeps nothing of the connection: it only listens for "the session was locked" */
    close(pp[1]);
    close(sess);
    close(sock);
    /* lines from the bridge (not trusted beyond its own counters): "L" the session was locked,
     * "T <rx> <tx>" its traffic */
    char line[80];
    size_t ll = 0;
    for (;;) {
        char c;
        ssize_t n = read(pp[0], &c, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (c != '\n') { if (ll < sizeof line - 1) line[ll++] = c; else ll = sizeof line; continue; }
        if (ll >= sizeof line) { ll = 0; continue; }
        line[ll] = 0;
        ll = 0;
        unsigned long long rx, tx;
        char extra;
        if (!strcmp(line, "L")) { rd_tokens_drop_uid(me.pw_uid); sd_log("%s locked the session: every remote cookie of that user ended", name); }
        else if (sscanf(line, "T %llu %llu %c", &rx, &tx, &extra) == 2) {
            rd_lock();
            for (int i = 0; i < RD_MAX_CONNS; i++)
                if (rd_shared->conns[i].pid == getpid()) { rd_shared->conns[i].rx = rx; rd_shared->conns[i].tx = tx; rd_shared->version++; }
            rd_unlock();
        }
    }
    while (waitpid(b, NULL, 0) < 0 && errno == EINTR) {}
}

void rd_serve(SSL_CTX *ctx, int fd, const char *ip, const char *key) {
    sock = fd;
    snprintf(peer, sizeof peer, "%s", ip);
    snprintf(peer_key, sizeof peer_key, "%s", key);
    char prefix[96];
    snprintf(prefix, sizeof prefix, "lord-sessiond[%d] %s", (int)getpid(), peer);
    sd_log_prefix(prefix);
    alarm(ALARM_SECONDS);                       /* backstop: SIGALRM ends a stuck connection */
    deadline = mono() + HEAD_SECONDS;
    ssl = SSL_new(ctx);
    if (!ssl) return;
    SSL_set_fd(ssl, fd);
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    for (;;) {                                  /* handshake, under the same deadline */
        int a = SSL_accept(ssl);
        if (a == 1) break;
        int e = SSL_get_error(ssl, a);
        if ((e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) && !wait_io(e == SSL_ERROR_WANT_WRITE)) continue;
        return;
    }
    RsReq r;
    int rc = read_request(&r);
    if (rc == -1) return;
    if (rc < 0) {
        if (rc != -408) sd_log("bad request (%d)", -rc);
        respond(-rc, NULL);
        return;
    }
    deadline = mono() + 30;                     /* answering (session start is covered by the alarm) */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);

    int get = !strcmp(r.method, "GET"), post = !strcmp(r.method, "POST");
    if (!post && r.content_length > 0) { respond(400, NULL); return; }
    char user[33] = "";
    uid_t uid = 0;
    unsigned char hash[32];
    int logged = session_user(&r, user, &uid, hash) == 0;

    if (!strcmp(r.path, "/login")) {
        if (!post) { respond(405, "Allow: POST\r\n"); return; }
        handle_login(&r);
        return;
    }
    if (!strcmp(r.path, "/logout")) {
        if (!post) { respond(405, "Allow: POST\r\n"); return; }
        if (!origin_ok(&r)) { respond(403, NULL); return; }
        if (logged) { token_drop_hash(hash); sd_log("%s logged out", user); }
        redirect("/", CLEAR_COOKIES);
        return;
    }
    if (!strcmp(r.path, "/") || !strcmp(r.path, "/index.html")) {
        if (!get) { respond(405, "Allow: GET\r\n"); return; }
        if (logged) redirect("/desk", NULL); else send_login(200, 0, r.host);
        return;
    }
    if (!strcmp(r.path, "/bg.jpg")) {             /* the login screen background (links to Crystal Crown) */
        if (!get) { respond(405, "Allow: GET\r\n"); return; }
        char p[600];
        snprintf(p, sizeof p, "%s", rd_conf.greeter_theme);
        char *slash = strrchr(p, '/');
        if (slash) snprintf(slash, sizeof p - (size_t)(slash - p), "/assets/background.jpg");
        int f = open(p, O_RDONLY | O_CLOEXEC);     /* a fixed path in root's theme folder (a link to the wallpaper) */
        struct stat st;
        char *img = NULL;
        size_t got = 0;
        if (f >= 0 && !fstat(f, &st) && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size < (8 << 20) && (img = malloc((size_t)st.st_size))) {
            while (got < (size_t)st.st_size) {
                ssize_t n = read(f, img + got, (size_t)st.st_size - got);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                got += (size_t)n;
            }
        }
        if (img && got == (size_t)st.st_size) respond_page(200, "image/jpeg", NULL, img, got, NULL, NULL);
        else respond(404, NULL);
        free(img);
        if (f >= 0) close(f);
        return;
    }
    if (!strcmp(r.path, "/desk")) {
        if (!get) { respond(405, "Allow: GET\r\n"); return; }
        if (!logged) { redirect("/", NULL); return; }
        page_hashes(&desk_page);
        respond_page(200, "text/html; charset=utf-8", NULL, desk_page.start, (size_t)(desk_page.end - desk_page.start), &desk_page, r.host);
        return;
    }
    if (!strcmp(r.path, "/ws")) {
        if (!get) { respond(405, "Allow: GET\r\n"); return; }
        handle_ws(&r, logged, user, uid);
        return;
    }
    respond(404, NULL);
}

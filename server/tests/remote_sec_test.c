/* remote_sec_test.c — the remote desktop's strict parsers and rules (src/remote/rsec.c, ipnet.c,
 * lockout.c; docs/REMOTE.md, "Security analysis (0.8.3)"). Every case is a request, a form, a frame
 * or an address our own pages would (or would never) send. */
#include "remote/rsec.h"
#include "remote/ipnet.h"
#include "remote/lockout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int head(const char *s, RsReq *r) { return rs_parse_head(s, strlen(s), r); }

static void test_head(void) {
    RsReq r;
    CHECK(head("GET / HTTP/1.1\r\nHost: 10.0.0.5:5673\r\n\r\n", &r) > 0);
    CHECK(!strcmp(r.method, "GET") && !strcmp(r.path, "/") && !strcmp(r.host, "10.0.0.5:5673") && r.content_length == -1);
    CHECK(head("GET /?e=1 HTTP/1.1\r\nHost: a\r\n\r\n", &r) > 0 && !strcmp(r.query, "e=1"));
    CHECK(head("POST /login HTTP/1.1\r\nHost: a\r\nContent-Length: 20\r\nContent-Type: application/x-www-form-urlencoded\r\n"
               "Origin: https://a\r\nCookie: x=1\r\n\r\n", &r) > 0 && r.content_length == 20 && !strcmp(r.origin, "https://a"));
    CHECK(head("GET / HTTP/1.1\r\nHost: a\r\n", &r) == RS_NEED_MORE);              /* not complete */
    CHECK(head("GET / HTTP/1.1\r\n", &r) == RS_NEED_MORE);
    CHECK(head("GET / HTTP/1.1\r\n\r\n", &r) == -400);                            /* 1.1 without Host */
    CHECK(head("GET / HTTP/1.0\r\n\r\n", &r) > 0);
    CHECK(head("GET / HTTP/2.0\r\nHost: a\r\n\r\n", &r) == -400);
    CHECK(head("GET  / HTTP/1.1\r\nHost: a\r\n\r\n", &r) == -400);                /* double space */
    CHECK(head("get / HTTP/1.1\r\nHost: a\r\n\r\n", &r) == -400);
    CHECK(head("GET http://x/ HTTP/1.1\r\nHost: a\r\n\r\n", &r) == -400);         /* absolute form */
    CHECK(head("GET /#x HTTP/1.1\r\nHost: a\r\n\r\n", &r) == -400);
    CHECK(head("GET / HTTP/1.1\nHost: a\r\n\r\n", &r) == -400);                   /* bare LF */
    CHECK(head("GET / HTTP/1.1\r\nHost: a\rb\r\n\r\n", &r) == -400);              /* bare CR */
    CHECK(head("GET / HTTP/1.1\r\nHost: a\r\nX: \x01\r\n\r\n", &r) == -400);      /* control byte */
    CHECK(head("GET / HTTP/1.1\r\nHost: \xc3\xa1\r\n\r\n", &r) == -400);          /* non-ASCII */
    CHECK(head("GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", &r) == -400);      /* duplicate Host */
    CHECK(head("GET / HTTP/1.1\r\nHost: a\r\nCookie: a=1\r\nCookie: b=2\r\n\r\n", &r) == -400);
    CHECK(head("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\n", &r) == -400);
    CHECK(head("POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n", &r) == -501);   /* smuggling */
    CHECK(head("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: -1\r\n\r\n", &r) == -400);
    CHECK(head("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 1e3\r\n\r\n", &r) == -400);
    CHECK(head("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 4097\r\n\r\n", &r) == -413);
    CHECK(head("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 99999999999\r\n\r\n", &r) == -413);
    CHECK(head("GET / HTTP/1.1\r\nHost : a\r\n\r\n", &r) == -400);                /* space before the colon */
    CHECK(head("GET / HTTP/1.1\r\nHost: a\r\n folded\r\n\r\n", &r) == -400);      /* obs-fold */
    CHECK(head("GET / HTTP/1.1\r\nHost: a\r\nNoColon\r\n\r\n", &r) == -400);
    CHECK(head("\r\nGET / HTTP/1.1\r\nHost: a\r\n\r\n", &r) == -400);
    {   /* too many headers, a too-long field, a too-long head, a too-long path */
        char b[9000];
        int o = sprintf(b, "GET / HTTP/1.1\r\nHost: a\r\n");
        for (int i = 0; i < 41; i++) o += sprintf(b + o, "X-%d: 1\r\n", i);
        strcpy(b + o, "\r\n");
        CHECK(head(b, &r) == -431);
        o = sprintf(b, "GET / HTTP/1.1\r\nHost: a\r\nCookie: ");
        memset(b + o, 'a', 1100);
        strcpy(b + o + 1100, "\r\n\r\n");
        CHECK(head(b, &r) == -431);
        memset(b, 'a', sizeof b);
        memcpy(b, "GET / HTTP/1.1\r\nX: ", 19);
        CHECK(rs_parse_head(b, sizeof b, &r) == -431);
        o = sprintf(b, "GET /");
        memset(b + o, 'p', 200);
        strcpy(b + o + 200, " HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(head(b, &r) == -414);
    }
    CHECK(head("GET / HTTP/1.1\r\nhOsT: x\r\nsec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", &r) > 0 &&
          !strcmp(r.host, "x") && !strcmp(r.wskey, "dGhlIHNhbXBsZSBub25jZQ==") && !strcmp(r.wsversion, "13"));
}

static void test_small(void) {
    CHECK(rs_valid_username("lord") && rs_valid_username("_x") && rs_valid_username("a-b_9"));
    CHECK(!rs_valid_username("") && !rs_valid_username("Lord") && !rs_valid_username("9a") && !rs_valid_username("-a"));
    CHECK(!rs_valid_username("a b") && !rs_valid_username("a$") && !rs_valid_username("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    CHECK(rs_valid_username("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));      /* 32 */
    CHECK(rs_valid_host("192.168.1.5:5673") && rs_valid_host("lordos.local") && rs_valid_host("[fe80::1]:5673") && rs_valid_host("[::1]"));
    CHECK(!rs_valid_host("") && !rs_valid_host("a b") && !rs_valid_host("a:0") && !rs_valid_host("a:65536") && !rs_valid_host("[zz]"));
    CHECK(!rs_valid_host("a..b") && !rs_valid_host("-a") && !rs_valid_host("a:5673x") && !rs_valid_host("a/b"));
    CHECK(rs_same_origin("https://10.0.0.5:5673", "10.0.0.5:5673"));
    CHECK(rs_same_origin("https://LordOS.local:5673", "lordos.local:5673"));
    CHECK(!rs_same_origin("http://10.0.0.5:5673", "10.0.0.5:5673"));
    CHECK(!rs_same_origin("https://evil.example", "10.0.0.5:5673"));
    CHECK(!rs_same_origin("https://10.0.0.5:5673.evil", "10.0.0.5:5673"));
    CHECK(!rs_same_origin("", "10.0.0.5:5673") && !rs_same_origin("null", "10.0.0.5:5673"));
    CHECK(rs_has_token("keep-alive, Upgrade", "upgrade") && rs_has_token("Upgrade", "upgrade"));
    CHECK(!rs_has_token("keep-alive", "upgrade") && !rs_has_token("upgraded", "upgrade"));
    CHECK(rs_valid_wskey("dGhlIHNhbXBsZSBub25jZQ==") && !rs_valid_wskey("dGhlIHNhbXBsZSBub25jZR==") && !rs_valid_wskey("abc"));
    CHECK(rs_form_type("application/x-www-form-urlencoded") && rs_form_type("application/x-www-form-urlencoded;charset=UTF-8"));
    CHECK(rs_form_type("application/x-www-form-urlencoded; charset=\"utf-8\""));
    CHECK(!rs_form_type("multipart/form-data") && !rs_form_type("application/x-www-form-urlencodedX") && !rs_form_type("text/plain"));
    CHECK(!rs_form_type("application/x-www-form-urlencoded; charset=latin1"));
    char tok[65], hex[65];
    memset(hex, 'a', 64);
    hex[64] = 0;
    char c[300];
    snprintf(c, sizeof c, "theme=dark; __Host-lordos_remote=%s; x=y", hex);
    CHECK(rs_cookie_token(c, "__Host-lordos_remote", tok) == 0 && !strcmp(tok, hex));
    snprintf(c, sizeof c, "x__Host-lordos_remote=%s", hex);
    CHECK(rs_cookie_token(c, "__Host-lordos_remote", tok) == -1);                  /* suffix match is not a match */
    snprintf(c, sizeof c, "__Host-lordos_remote=%s; __Host-lordos_remote=%s", hex, hex);
    CHECK(rs_cookie_token(c, "__Host-lordos_remote", tok) == -2);                  /* cookie tossing */
    CHECK(rs_cookie_token("__Host-lordos_remote=ABC", "__Host-lordos_remote", tok) == -2);
    hex[10] = 'G';
    snprintf(c, sizeof c, "__Host-lordos_remote=%s", hex);
    CHECK(rs_cookie_token(c, "__Host-lordos_remote", tok) == -2);
    CHECK(rs_cookie_token("", "__Host-lordos_remote", tok) == -1);
    char clean[33];
    rs_log_clean("a\nb\"c\\d\x1b", 8, clean, sizeof clean);
    CHECK(!strcmp(clean, "a?b?c?d?"));
}

static int login(const char *body, char *user, char *pass, size_t *pl) {
    char raw[33];
    return rs_parse_login(body, strlen(body), user, pass, pl, raw);
}

static void test_login(void) {
    char u[33], p[RS_PASS_MAX + 1], raw[33];
    size_t pl;
    CHECK(login("user=lord&pass=s3cret", u, p, &pl) == RS_FORM_OK && !strcmp(u, "lord") && !strcmp(p, "s3cret") && pl == 6);
    CHECK(login("pass=a+b%26c%3D&user=bob", u, p, &pl) == RS_FORM_OK && !strcmp(u, "bob") && !strcmp(p, "a b&c="));
    CHECK(login("user=lord&pass=%C3%B1", u, p, &pl) == RS_FORM_OK && pl == 2);
    CHECK(login("user=lord", u, p, &pl) == RS_FORM_BAD);
    CHECK(login("pass=x", u, p, &pl) == RS_FORM_BAD);
    CHECK(login("user=lord&pass=x&x=1", u, p, &pl) == RS_FORM_BAD);                 /* unknown field */
    CHECK(login("user=lord&user=root&pass=x", u, p, &pl) == RS_FORM_BAD);           /* duplicate */
    CHECK(login("user=lord&pass=x&pass=y", u, p, &pl) == RS_FORM_BAD);
    CHECK(login("user=lord&pass=x&", u, p, &pl) == RS_FORM_BAD);
    CHECK(login("&user=lord&pass=x", u, p, &pl) == RS_FORM_BAD);
    CHECK(login("user=lord&pass=%4", u, p, &pl) == RS_FORM_BAD);                    /* truncated escape */
    CHECK(login("user=lord&pass=%zz", u, p, &pl) == RS_FORM_BAD);
    CHECK(login("user=lord&pass=a b", u, p, &pl) == RS_FORM_BAD);                   /* raw space */
    CHECK(login("user=lord&pass=a\"", u, p, &pl) == RS_FORM_BAD);
    CHECK(login("user=lord&pass", u, p, &pl) == RS_FORM_BAD);
    CHECK(login("user=Lord&pass=x", u, p, &pl) == RS_FORM_USER);
    CHECK(login("user=lord%00x&pass=x", u, p, &pl) == RS_FORM_USER);                /* NUL in the name */
    CHECK(login("user=&pass=x", u, p, &pl) == RS_FORM_USER);
    CHECK(login("user=lo%20rd&pass=x", u, p, &pl) == RS_FORM_USER);
    CHECK(login("user=lord&pass=", u, p, &pl) == RS_FORM_PASS);
    CHECK(login("user=lord&pass=ab%00cd", u, p, &pl) == RS_FORM_PASS);              /* NUL in the password */
    char big[4200];
    int o = sprintf(big, "user=lord&pass=");
    memset(big + o, 'x', 1024);
    big[o + 1024] = 0;
    CHECK(login(big, u, p, &pl) == RS_FORM_OK && pl == 1024);
    big[o + 1024] = 'x';
    big[o + 1025] = 0;
    CHECK(login(big, u, p, &pl) == RS_FORM_PASS);                                   /* 1025 bytes */
    o = sprintf(big, "user=");
    memset(big + o, 'a', 40);
    strcpy(big + o + 40, "&pass=x");
    CHECK(login(big, u, p, &pl) == RS_FORM_USER);
    CHECK(rs_parse_login("user=root\n&pass=x", 17, u, p, &pl, raw) == RS_FORM_BAD);
    CHECK(login("user=x%0Ay&pass=x", u, p, &pl) == RS_FORM_USER);
    rs_parse_login("user=x%0Ay&pass=x", 17, u, p, &pl, raw);
    CHECK(!strcmp(raw, "x?y"));                                                     /* sanitized for the journal */
}

typedef struct { int n, op[8]; char data[8][256]; size_t len[8]; } Got;

static void on(void *ud, int op, unsigned char *d, size_t l) {
    Got *g = ud;
    if (g->n >= 8) return;
    g->op[g->n] = op;
    memcpy(g->data[g->n], d, l < 255 ? l : 255);
    g->data[g->n][l < 255 ? l : 255] = 0;
    g->len[g->n++] = l;
}

/* a masked client frame */
static size_t frame(unsigned char *out, int fin, int op, const char *p, size_t n) {
    static const unsigned char m[4] = { 0x12, 0x34, 0x56, 0x78 };
    size_t o = 0;
    out[o++] = (unsigned char)((fin ? 0x80 : 0) | op);
    if (n < 126) out[o++] = (unsigned char)(0x80 | n);
    else if (n < 65536) { out[o++] = 0x80 | 126; out[o++] = (unsigned char)(n >> 8); out[o++] = (unsigned char)n; }
    else { out[o++] = 0x80 | 127; for (int i = 0; i < 8; i++) out[o++] = (unsigned char)((uint64_t)n >> (56 - 8 * i)); }
    memcpy(out + o, m, 4);
    o += 4;
    for (size_t i = 0; i < n; i++) out[o + i] = (unsigned char)p[i] ^ m[i & 3];
    return o + n;
}

static int ws(unsigned char *b, size_t n, Got *g, size_t *used) {
    RsWs w;
    rs_ws_init(&w, 70000);
    memset(g, 0, sizeof *g);
    int rc = rs_ws_consume(&w, b, n, used, on, g);
    rs_ws_free(&w);
    return rc;
}

static void test_ws(void) {
    unsigned char b[200000];
    Got g;
    size_t used, n;
    n = frame(b, 1, 1, "{\"t\":\"ack\"}", 11);
    CHECK(ws(b, n, &g, &used) == 0 && g.n == 1 && g.op[0] == 1 && !strcmp(g.data[0], "{\"t\":\"ack\"}") && used == n);
    CHECK(ws(b, n - 1, &g, &used) == 0 && g.n == 0 && used == 0);                   /* incomplete: waits */
    n = frame(b, 0, 1, "ab", 2);
    n += frame(b + n, 1, 9, "p", 1);                                                /* ping between fragments */
    n += frame(b + n, 1, 0, "cd", 2);
    CHECK(ws(b, n, &g, &used) == 0 && g.n == 2 && g.op[0] == 9 && g.op[1] == 1 && !strcmp(g.data[1], "abcd"));
    n = frame(b, 1, 0, "x", 1);
    CHECK(ws(b, n, &g, &used) == 1002);                                             /* continuation of nothing */
    n = frame(b, 0, 1, "a", 1);
    n += frame(b + n, 1, 1, "b", 1);
    CHECK(ws(b, n, &g, &used) == 1002);                                             /* new message inside one */
    n = frame(b, 1, 1, "x", 1);
    b[1] &= 0x7f;
    CHECK(ws(b, n, &g, &used) == 1002);                                             /* unmasked */
    n = frame(b, 1, 1, "x", 1);
    b[0] |= 0x40;
    CHECK(ws(b, n, &g, &used) == 1002);                                             /* RSV bit */
    n = frame(b, 1, 3, "x", 1);
    CHECK(ws(b, n, &g, &used) == 1002);                                             /* reserved opcode */
    n = frame(b, 0, 9, "x", 1);
    CHECK(ws(b, n, &g, &used) == 1002);                                             /* fragmented control */
    char big[70001];
    memset(big, 'a', sizeof big);
    n = frame(b, 1, 9, big, 126);
    CHECK(ws(b, n, &g, &used) == 1002);                                             /* control > 125 */
    n = frame(b, 1, 1, big, 70001);
    CHECK(ws(b, n, &g, &used) == 1009);                                             /* message too big */
    n = frame(b, 0, 1, big, 40000);
    n += frame(b + n, 1, 0, big, 40000);
    CHECK(ws(b, n, &g, &used) == 1009);                                             /* too big in pieces */
    n = frame(b, 1, 1, "\xc3\x28", 2);
    CHECK(ws(b, n, &g, &used) == 1007);                                             /* invalid UTF-8 */
    n = frame(b, 1, 1, "\xed\xa0\x80", 3);
    CHECK(ws(b, n, &g, &used) == 1007);                                             /* surrogate */
    n = frame(b, 1, 2, "\xff", 1);
    CHECK(ws(b, n, &g, &used) == 0 && g.op[0] == 2);                                /* binary is passed up */
    n = frame(b, 1, 8, "\x03\xe8", 2);
    n += frame(b + n, 1, 1, "late", 4);
    CHECK(ws(b, n, &g, &used) == 0 && g.n == 1 && g.op[0] == 8);                    /* nothing after close */
    /* non-minimal length encoding */
    unsigned char nm[16] = { 0x81, 0x80 | 126, 0, 5, 1, 2, 3, 4, 'a', 'b', 'c', 'd', 'e' };
    CHECK(ws(nm, 13, &g, &used) == 1002);
    unsigned char huge[14] = { 0x81, 0x80 | 127, 0x80, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 };
    CHECK(ws(huge, 14, &g, &used) == 1002);
    CHECK(rs_utf8_valid((const unsigned char *)"\xf0\x9f\x91\x91", 4) && !rs_utf8_valid((const unsigned char *)"\xf4\x90\x80\x80", 4));
    CHECK(!rs_utf8_valid((const unsigned char *)"\xc0\xaf", 2) && !rs_utf8_valid((const unsigned char *)"\xe2\x82", 2));
}

static int jin(const char *j, int W, int H, RsInput *in) {
    static char sb[4096];
    RsJson o;
    if (rs_json_parse(j, strlen(j), &o, sb, sizeof sb)) return -1;
    return rs_browser_input(&o, W, H, in);
}

static void test_json(void) {
    RsInput in;
    CHECK(jin("{\"t\":\"m\",\"x\":10,\"y\":20}", 1280, 800, &in) == RS_IN_PTR && in.a == 10 && in.b == 20);
    CHECK(jin("{\"t\":\"m\",\"x\":-5,\"y\":99999}", 1280, 800, &in) == RS_IN_PTR && in.a == 0 && in.b == 799);
    CHECK(jin(" { \"y\" : 1 , \"t\" : \"m\" , \"x\" : 2 } ", 1280, 800, &in) == RS_IN_PTR);
    CHECK(jin("{\"t\":\"m\",\"x\":1}", 1280, 800, &in) == RS_IN_NONE);
    CHECK(jin("{\"t\":\"m\",\"x\":1,\"y\":2}", 0, 0, &in) == RS_IN_NONE);            /* no screen yet */
    CHECK(jin("{\"t\":\"m\",\"x\":\"\\\"y\\\":5\",\"y\":2}", 1280, 800, &in) == RS_IN_NONE);   /* key confusion */
    CHECK(jin("{\"t\":\"m\",\"t\":\"k\",\"x\":1,\"y\":2}", 1280, 800, &in) == -1);   /* duplicate key */
    CHECK(jin("{\"t\":\"m\",\"x\":1.5,\"y\":2}", 1280, 800, &in) == -1);
    CHECK(jin("{\"t\":\"m\",\"x\":01,\"y\":2}", 1280, 800, &in) == -1);
    CHECK(jin("{\"t\":\"m\",\"x\":9007199254740992,\"y\":2}", 1280, 800, &in) == -1);
    CHECK(jin("{\"t\":\"m\",\"x\":1,\"y\":2}x", 1280, 800, &in) == -1);             /* trailing bytes */
    CHECK(jin("{\"t\":\"m\",\"x\":{\"a\":1},\"y\":2}", 1280, 800, &in) == -1);     /* nested */
    CHECK(jin("[1]", 1280, 800, &in) == -1 && jin("", 1280, 800, &in) == -1 && jin("{", 1280, 800, &in) == -1);
    CHECK(jin("{\"T\":\"m\"}", 1280, 800, &in) == -1);
    CHECK(jin("{\"t\":\"b\",\"b\":0,\"d\":1}", 1280, 800, &in) == RS_IN_BTN && in.a == 0 && in.b == 1);
    CHECK(jin("{\"t\":\"b\",\"b\":3,\"d\":1}", 1280, 800, &in) == RS_IN_NONE);
    CHECK(jin("{\"t\":\"w\",\"d\":-500}", 1280, 800, &in) == RS_IN_WHEEL && in.a == -10);
    CHECK(jin("{\"t\":\"k\",\"c\":30,\"d\":1}", 1280, 800, &in) == RS_IN_KEY && in.a == 30);
    CHECK(jin("{\"t\":\"k\",\"c\":272,\"d\":1}", 1280, 800, &in) == RS_IN_NONE);    /* BTN_LEFT is not a key */
    CHECK(jin("{\"t\":\"k\",\"c\":0,\"d\":1}", 1280, 800, &in) == RS_IN_NONE);
    CHECK(jin("{\"t\":\"s\",\"p\":2,\"x\":99999999,\"y\":-99999999}", 1280, 800, &in) == RS_IN_SCROLL && in.v1 == 4000 * 256 && in.v2 == -4000 * 256);
    CHECK(jin("{\"t\":\"s\",\"p\":4,\"x\":1,\"y\":1}", 1280, 800, &in) == RS_IN_NONE);
    CHECK(jin("{\"t\":\"pz\",\"p\":1,\"s\":65536,\"r\":0}", 1280, 800, &in) == RS_IN_PINCH && in.v1 == 65536);
    CHECK(jin("{\"t\":\"pz\",\"p\":2,\"s\":0,\"r\":0}", 1280, 800, &in) == RS_IN_PINCH && in.v1 == 3277);
    CHECK(jin("{\"t\":\"resize\",\"w\":1921,\"h\":1080}", 1280, 800, &in) == RS_IN_RESIZE && in.a == 1920);
    CHECK(jin("{\"t\":\"resize\",\"w\":100000,\"h\":1080}", 1280, 800, &in) == RS_IN_NONE);
    CHECK(jin("{\"t\":\"resize\",\"w\":100,\"h\":100}", 1280, 800, &in) == RS_IN_NONE);
    CHECK(jin("{\"t\":\"ping\",\"ts\":123}", 1280, 800, &in) == RS_IN_PING && in.ts == 123);
    CHECK(jin("{\"t\":\"ack\"}", 1280, 800, &in) == RS_IN_ACK);
    CHECK(jin("{\"t\":\"clip\",\"text\":\"h\\u00e9\\ud83d\\udc51\\n\"}", 1280, 800, &in) == RS_IN_CLIP && in.tlen == 8 &&
          !memcmp(in.text, "h\xc3\xa9\xf0\x9f\x91\x91\n", 8));
    CHECK(jin("{\"t\":\"clip\",\"text\":\"\\ud83d\"}", 1280, 800, &in) == -1);        /* lone surrogate */
    CHECK(jin("{\"t\":\"clip\",\"text\":\"\\udc51\"}", 1280, 800, &in) == -1);
    CHECK(jin("{\"t\":\"clip\",\"text\":\"\\u0000\"}", 1280, 800, &in) == -1);        /* NUL */
    CHECK(jin("{\"t\":\"clip\",\"text\":\"a\\x\"}", 1280, 800, &in) == -1);
    CHECK(jin("{\"t\":\"clip\",\"text\":\"\"}", 1280, 800, &in) == RS_IN_NONE);
    CHECK(jin("{\"t\":\"clip\",\"text\":\"a\x01\"}", 1280, 800, &in) == -1);           /* raw control */
    CHECK(jin("{\"t\":\"nope\"}", 1280, 800, &in) == RS_IN_NONE);
}

static void test_ipnet(void) {
    RsAddr a;
    RsCidr c;
    char t[64];
    CHECK(!rs_addr_parse("192.168.1.10", &a) && a.v4);
    CHECK(!rs_addr_parse("::ffff:192.168.1.10", &a) && a.v4);
    CHECK(!rs_addr_parse("2001:db8::1", &a) && !a.v4);
    CHECK(rs_addr_parse("192.168.01.10", &a) && rs_addr_parse("1.2.3", &a) && rs_addr_parse("fe80::1%eth0", &a) && rs_addr_parse(" 1.2.3.4", &a));
    CHECK(rs_addr_parse("256.1.1.1", &a) && rs_addr_parse("", &a));
    CHECK(!rs_cidr_parse("192.168.1.77/24", &c));
    rs_cidr_format(&c, t, sizeof t);
    CHECK(!strcmp(t, "192.168.1.0/24"));                                            /* host bits cleared */
    CHECK(!rs_cidr_parse("10.0.0.1", &c) && c.prefix == 128);
    rs_cidr_format(&c, t, sizeof t);
    CHECK(!strcmp(t, "10.0.0.1"));
    CHECK(rs_cidr_parse("0.0.0.0/0", &c) && rs_cidr_parse("10.0.0.0/7", &c) && rs_cidr_parse("10.0.0.0/33", &c));
    CHECK(rs_cidr_parse("10.0.0.0/024", &c) && rs_cidr_parse("10.0.0.0/", &c) && rs_cidr_parse("10.0.0.0/2a", &c) && rs_cidr_parse("::/0", &c));
    CHECK(!rs_cidr_parse("2001:db8::/32", &c) && !rs_cidr_parse("10.0.0.0/8", &c));
    CHECK(!rs_addr_parse("10.200.3.4", &a) && rs_cidr_contains(&c, &a));
    CHECK(!rs_addr_parse("11.0.0.1", &a) && !rs_cidr_contains(&c, &a));
    CHECK(!rs_addr_parse("2001:db8:1:2:3:4:5:6", &a));
    rs_addr_key(&a, t, sizeof t);
    CHECK(!strcmp(t, "2001:db8:1:2::/64"));
    CHECK(!rs_key_parse("2001:db8:1:2::/64", &c) && rs_cidr_contains(&c, &a));
    CHECK(!rs_key_parse("10.0.0.1", &c) && rs_key_parse("10.0.0.0/24", &c) && rs_key_parse("2001:db8::/48", &c));
    CHECK(!rs_addr_parse("192.168.1.1", &a) && rs_addr_is_local(&a));
    CHECK(!rs_addr_parse("172.20.0.1", &a) && rs_addr_is_local(&a));
    CHECK(!rs_addr_parse("172.32.0.1", &a) && !rs_addr_is_local(&a));
    CHECK(!rs_addr_parse("8.8.8.8", &a) && !rs_addr_is_local(&a));
    CHECK(!rs_addr_parse("fe80::5", &a) && rs_addr_is_local(&a));
    CHECK(!rs_addr_parse("fd12::5", &a) && rs_addr_is_local(&a));
    CHECK(!rs_addr_parse("2a00::5", &a) && !rs_addr_is_local(&a));
    CHECK(!rs_addr_parse("::ffff:8.8.8.8", &a) && !rs_addr_is_local(&a));

    RsAccess acc;
    int bad;
    rs_access_parse("# x\nallow 203.0.113.0/24\nblock 192.168.1.66\nblock nope\nallow 1.2.3.4 extra\nweird line\n", &acc, &bad);
    CHECK(acc.nallow == 1 && acc.nblock == 1 && bad == 3);
    char text[4096];
    CHECK(rs_access_format(&acc, text, sizeof text) > 0 && strstr(text, "allow 203.0.113.0/24\n") && strstr(text, "block 192.168.1.66\n"));
    RsAccess back;
    rs_access_parse(text, &back, &bad);
    CHECK(back.nallow == 1 && back.nblock == 1 && bad == 0);
    rs_addr_parse("192.168.1.66", &a);
    CHECK(rs_access_decide(&acc, RS_NET_ANY, &a, 0) == RS_ACC_BLOCKED);            /* manual block always wins */
    rs_addr_parse("192.168.1.67", &a);
    CHECK(rs_access_decide(&acc, RS_NET_LAN, &a, 0) == RS_ACC_OK);
    CHECK(rs_access_decide(&acc, RS_NET_LIST, &a, 0) == RS_ACC_NOT_ALLOWED);
    rs_addr_parse("203.0.113.9", &a);
    CHECK(rs_access_decide(&acc, RS_NET_LAN, &a, 0) == RS_ACC_NOT_ALLOWED);         /* public, default */
    CHECK(rs_access_decide(&acc, RS_NET_LAN, &a, 1) == RS_ACC_OK);                  /* ...unless over a VPN */
    CHECK(rs_access_decide(&acc, RS_NET_LIST, &a, 0) == RS_ACC_OK);
    CHECK(rs_access_decide(&acc, RS_NET_ANY, &a, 0) == RS_ACC_OK);
    rs_cidr_parse("192.168.1.0/24", &c);
    CHECK(rs_access_add(acc.block, &acc.nblock, &c) == 0 && rs_access_add(acc.block, &acc.nblock, &c) == 1);
    CHECK(rs_access_del(acc.block, &acc.nblock, &c) == 2 && acc.nblock == 0);     /* the /24 and the host inside */
    for (int i = 0; i < RS_ACCESS_MAX; i++) { char s[32]; snprintf(s, sizeof s, "10.0.%d.0/24", i); rs_cidr_parse(s, &c); rs_access_add(acc.block, &acc.nblock, &c); }
    rs_cidr_parse("10.9.9.9", &c);
    CHECK(rs_access_add(acc.block, &acc.nblock, &c) == -1);
    CHECK(rs_net_mode("lan") == RS_NET_LAN && rs_net_mode("any") == RS_NET_ANY && rs_net_mode("list") == RS_NET_LIST && rs_net_mode("all") < 0);
    CHECK(rs_vpn_ifname("wg-home") && rs_vpn_ifname("ovpn-work") && rs_vpn_ifname("tun0") && !rs_vpn_ifname("eth0") && !rs_vpn_ifname("wlan0"));
}

static void test_lockout(void) {
    static RlTable t;
    memset(&t, 0, sizeof t);
    double now = 1e9, until = 0;
    int strikes = 0;
    const char *k = "192.168.1.50";
    CHECK(!rl_fail(&t, k, now, &until, &strikes) && !rl_fail(&t, k, now + 1, &until, &strikes));
    CHECK(rl_blocked(&t, k, now + 2) == 0);
    CHECK(rl_fail(&t, k, now + 2, &until, &strikes) && strikes == 1 && until == now + 2 + 900);   /* 3rd: 15 min */
    CHECK(rl_blocked(&t, k, now + 3) > 890 && rl_blocked(&t, k, now + 903) == 0);
    rl_fail(&t, k, now + 1000, &until, &strikes);
    rl_fail(&t, k, now + 1001, &until, &strikes);
    CHECK(rl_fail(&t, k, now + 1002, &until, &strikes) && strikes == 2 && until == now + 1002 + 3600);   /* 1 h */
    rl_fail(&t, k, now + 5000, &until, &strikes);
    rl_fail(&t, k, now + 5001, &until, &strikes);
    CHECK(rl_fail(&t, k, now + 5002, &until, &strikes) && strikes == 3 && until == now + 5002 + 86400);  /* 24 h */
    /* a day later the strikes are forgotten: 15 min again */
    double later = now + 5002 + 86400 + RL_WINDOW + 10;
    rl_fail(&t, k, later, &until, &strikes);
    rl_fail(&t, k, later + 1, &until, &strikes);
    CHECK(rl_fail(&t, k, later + 2, &until, &strikes) && strikes == 1 && until == later + 2 + 900);
    /* a success clears the count */
    const char *k2 = "192.168.1.51";
    rl_fail(&t, k2, now, &until, &strikes);
    rl_fail(&t, k2, now, &until, &strikes);
    rl_success(&t, k2);
    CHECK(!rl_fail(&t, k2, now, &until, &strikes) && !rl_fail(&t, k2, now, &until, &strikes) && rl_fail(&t, k2, now, &until, &strikes));
    /* persistence */
    char text[RL_MAX * 128 + 256];
    CHECK(rl_format(&t, later + 3, text, sizeof text) > 0);
    static RlTable back;
    rl_parse(&back, text, later + 3);
    CHECK(rl_blocked(&back, k, later + 3) > 800);
    rl_parse(&back, "2001:db8:1:2::/64 1 1000 1 1000 9999999999\n10.0.0.1 2 1000 0 0 0\nbad\n10.0.0.01 1 1 1 1 1\n"
                    "10.0.0.2 7 1000 0 0 0\n10.0.0.3 1 1000 0 0 1e300\n", 2000);
    CHECK(rl_blocked(&back, "2001:db8:1:2::/64", 2000) == 0);                       /* until too far: refused */
    int n = 0;
    for (int i = 0; i < RL_MAX; i++) if (back.e[i].key[0]) n++;
    CHECK(n == 1);                                                                  /* only 10.0.0.1 is valid */
    rl_parse(&back, "2001:db8:1:2::/64 0 1990 1 1990 2500\n", 2000);
    CHECK(rl_blocked(&back, "2001:db8:1:2::/64", 2000) == 500);
    CHECK(rl_unblock(&back, "2001:db8:1:2:aaaa::1") == 1 && rl_blocked(&back, "2001:db8:1:2::/64", 2000) == 0);
    rl_fail(&t, "10.1.1.1", now, NULL, NULL);
    rl_fail(&t, "10.1.1.1", now, NULL, NULL);
    rl_fail(&t, "10.1.1.1", now, NULL, NULL);
    CHECK(rl_unblock(&t, "10.1.0.0/16") == 1 && rl_blocked(&t, "10.1.1.1", now) == 0);
    CHECK(rl_unblock(&t, "garbage") == 0);
    /* a flood of other addresses doesn't evict a running block */
    memset(&t, 0, sizeof t);
    for (int i = 0; i < 3; i++) rl_fail(&t, "192.168.9.9", now, NULL, NULL);
    for (int i = 0; i < 5000; i++) { char s[32]; snprintf(s, sizeof s, "10.%d.%d.%d", i >> 16 & 255, i >> 8 & 255, i & 255); rl_fail(&t, s, now + 1, NULL, NULL); }
    CHECK(rl_blocked(&t, "192.168.9.9", now + 2) > 0);
    CHECK(rl_user_delay(0) == 0 && rl_user_delay(4) == 0 && rl_user_delay(5) == 1 && rl_user_delay(7) == 4 && rl_user_delay(100) == 8);
    static RcTable rc;
    int ok = 0;
    for (int i = 0; i < 100; i++) ok += rc_take(&rc, "1.2.3.4", 10.0);
    CHECK(ok == 20);                                                                /* the burst */
    CHECK(rc_take(&rc, "1.2.3.4", 10.6) == 1 && rc_take(&rc, "1.2.3.4", 10.6) == 0);  /* 2 per second */
    CHECK(rc_take(&rc, "1.2.3.5", 10.6) == 1);                                      /* others are not affected */
}

int main(void) {
    test_head();
    test_small();
    test_login();
    test_ws();
    test_json();
    test_ipnet();
    test_lockout();
    printf("remote-sec-test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}

/* remote_fuzz.c — the remote desktop's parsers under ASan+UBSan with random and mutated inputs
 * (HTTP heads, login forms, cookies, WebSocket streams, JSON messages, addresses, the lockout file).
 * Built by `make -C server test` with -fsanitize=address,undefined; any memory error or undefined
 * behaviour aborts. Usage: remote-fuzz [iterations] [seed]. */
#include "remote/rsec.h"
#include "remote/ipnet.h"
#include "remote/lockout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long st = 88172645463325252ULL;
static unsigned rnd(void) { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return (unsigned)(st >> 11); }

static const char *const seeds[] = {
    "GET / HTTP/1.1\r\nHost: 10.0.0.5:5673\r\n\r\n",
    "POST /login HTTP/1.1\r\nHost: a:5673\r\nOrigin: https://a:5673\r\nContent-Type: application/x-www-form-urlencoded\r\n"
    "Content-Length: 20\r\nCookie: __Host-lordos_remote=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n\r\nuser=lord&pass=x",
    "GET /ws?w=1280&h=800 HTTP/1.1\r\nHost: [fe80::1]:5673\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nSec-Fetch-Site: same-origin\r\n\r\n",
    "user=lord&pass=a%20b+c%26",
    "{\"t\":\"m\",\"x\":10,\"y\":20}",
    "{\"t\":\"clip\",\"text\":\"h\\u00e9\\ud83d\\udc51\\n\"}",
    "{\"t\":\"pz\",\"p\":2,\"s\":65536,\"r\":-90}",
    "192.168.1.0/24", "2001:db8::/32", "::ffff:10.0.0.1",
    "10.0.0.1 2 1000 0 0 0\n2001:db8:1:2::/64 0 1990 1 1990 2500\n",
    "allow 203.0.113.0/24\nblock 192.168.1.66\n",
};
static const char alphabet[] = "\r\n :%&=+/?#\"\\{}[],.-_0123456789abcdefABCDEFghijkluvwxyzGETPOSTHosUpgrade\x00\x01\x7f\x80\xc3\xa9\xed\xff";

static size_t mutate(char *b, size_t n, size_t cap) {
    int k = 1 + (int)(rnd() % 8);
    while (k--) {
        unsigned op = rnd() % 6;
        if (op == 0 && n) b[rnd() % n] = alphabet[rnd() % (sizeof alphabet - 1)];
        else if (op == 1 && n) b[rnd() % n] ^= (char)(1 << (rnd() % 8));
        else if (op == 2 && n + 1 < cap) { size_t p = n ? rnd() % n : 0; memmove(b + p + 1, b + p, n - p); b[p] = alphabet[rnd() % (sizeof alphabet - 1)]; n++; }
        else if (op == 3 && n) { size_t p = rnd() % n; memmove(b + p, b + p + 1, n - p - 1); n--; }
        else if (op == 4 && n && n * 2 < cap) { size_t p = rnd() % n, l = 1 + rnd() % (n - p); memcpy(b + n, b + p, l); n += l; }
        else if (op == 5) n = n ? rnd() % n : 0;
    }
    return n;
}

static void on(void *ud, int op, unsigned char *d, size_t l) {
    (void)ud;
    if (op == 1) {
        char *sb = malloc(l + 1);
        RsJson o;
        RsInput in;
        if (sb && !rs_json_parse((const char *)d, l, &o, sb, l + 1)) rs_browser_input(&o, 1280, 800, &in);
        free(sb);
    }
}

static void one(const char *b, size_t n) {
    /* each parser gets an exact-size heap copy, so ASan sees any byte read past the end */
    char *c = malloc(n + 1);
    memcpy(c, b, n);
    RsReq r;
    rs_parse_head(c, n, &r);
    char u[33], p[RS_PASS_MAX + 1], raw[33], tok[65];
    size_t pl;
    rs_parse_login(c, n, u, p, &pl, raw);
    {
        RsJson o;
        RsInput in;
        char *sb = malloc(n + 1);
        if (!rs_json_parse(c, n, &o, sb, n + 1)) rs_browser_input(&o, (int)(rnd() % 3000), (int)(rnd() % 3000), &in);
        free(sb);
    }
    rs_utf8_valid((unsigned char *)c, n);
    {
        RsWs w;
        rs_ws_init(&w, 1 + rnd() % 70000);
        unsigned char *m = malloc(n ? n : 1);
        memcpy(m, c, n);
        size_t pos = 0, end = 0, used = 0;
        while (end < n) {                       /* the stream arrives in random pieces, as TLS records do */
            end += 1 + rnd() % (n - end);
            if (rs_ws_consume(&w, m + pos, end - pos, &used, on, NULL)) break;
            pos += used;
        }
        free(m);
        rs_ws_free(&w);
    }
    c[n] = 0;                                   /* the string functions get a C string */
    rs_cookie_token(c, "__Host-lordos_remote", tok);
    rs_valid_host(c);
    rs_same_origin(c, "a:5673");
    rs_form_type(c);
    rs_has_token(c, "upgrade");
    rs_valid_wskey(c);
    rs_valid_username(c);
    RsCidr cd;
    RsAddr ad;
    if (!rs_cidr_parse(c, &cd)) { char t[64]; rs_cidr_format(&cd, t, sizeof t); RsCidr back; if (rs_cidr_parse(t, &back) || !rs_cidr_equal(&back, &cd)) abort(); }
    if (!rs_addr_parse(c, &ad)) { char t[64]; rs_addr_key(&ad, t, sizeof t); rs_addr_is_local(&ad); }
    static RlTable t;
    rl_parse(&t, c, 1e9);
    char out[RL_MAX * 128 + 256];
    rl_format(&t, 1e9, out, sizeof out);
    rl_unblock(&t, c);
    RsAccess acc;
    rs_access_parse(c, &acc, NULL);
    char at[8192];
    rs_access_format(&acc, at, sizeof at);
    free(c);
}

/* random WebSocket stream: well-formed frames with random flags, lengths and payloads */
static size_t ws_stream(unsigned char *b, size_t cap) {
    size_t o = 0;
    while (o + 200 < cap && rnd() % 8) {
        size_t len = rnd() % 4 == 0 ? rnd() % 70000 : rnd() % 130;
        if (o + len + 14 >= cap) break;
        int op = (int)(rnd() % 11);
        b[o++] = (unsigned char)((rnd() % 4 ? 0x80 : 0) | op | (rnd() % 50 == 0 ? 0x40 : 0));
        if (len < 126) b[o++] = (unsigned char)(0x80 | len);
        else if (len < 65536) { b[o++] = 0x80 | 126; b[o++] = (unsigned char)(len >> 8); b[o++] = (unsigned char)len; }
        else { b[o++] = 0x80 | 127; for (int i = 0; i < 8; i++) b[o++] = (unsigned char)((unsigned long long)len >> (56 - 8 * i)); }
        for (int i = 0; i < 4; i++) b[o++] = (unsigned char)rnd();
        for (size_t i = 0; i < len; i++) b[o++] = (unsigned char)(rnd() % 3 ? (unsigned)(unsigned char)"{\"t\":\"m\",\"x\":1}"[i % 15] : rnd());
    }
    return o;
}

int main(int argc, char **argv) {
    long iters = argc > 1 ? atol(argv[1]) : 100000;
    if (argc > 2) st = strtoull(argv[2], NULL, 10) | 1;
    static char b[200000];
    for (long i = 0; i < iters; i++) {
        size_t n;
        unsigned kind = rnd() % 10;
        if (kind < 6) {
            const char *s = seeds[rnd() % (sizeof seeds / sizeof *seeds)];
            n = strlen(s);
            memcpy(b, s, n);
            n = mutate(b, n, 16384);
        } else if (kind < 8) {
            n = rnd() % 2048;
            for (size_t k = 0; k < n; k++) b[k] = (char)rnd();
        } else n = ws_stream((unsigned char *)b, sizeof b);
        one(b, n);
    }
    printf("remote-fuzz: %ld inputs, no memory errors or undefined behaviour\n", iters);
    return 0;
}

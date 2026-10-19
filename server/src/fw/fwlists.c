/* fwlists.c — subscribed blocklists: raw parsers, the set and the compiled file (see fwlists.h). */
#include "fwlists.h"
#include "fwmap.h"
#include "rules.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FwLists {
    FwMap exact, sub, nets;             /* name → tag; name → tag; (fam, prefix, network) → tag */
    unsigned char has4[33], has6[129];  /* prefix lengths present */
    size_t ndom, nnet;
};

FwLists *fwl_new(void) { return calloc(1, sizeof(FwLists)); }

void fwl_free(FwLists *l) {
    if (!l) return;
    fwmap_free(&l->exact, NULL);
    fwmap_free(&l->sub, NULL);
    fwmap_free(&l->nets, NULL);
    free(l);
}

size_t fwl_domains(const FwLists *l) { return l ? l->ndom : 0; }
size_t fwl_nets(const FwLists *l) { return l ? l->nnet : 0; }

FwlFormat fwl_format_from_name(const char *n) {
    if (!n) return 0;
    if (!strcmp(n, "hosts")) return FWL_FMT_HOSTS;
    if (!strcmp(n, "domains")) return FWL_FMT_DOMAINS;
    if (!strcmp(n, "ips")) return FWL_FMT_IPS;
    return 0;
}

/* ---- the set --------------------------------------------------------------------------------- */

static size_t net_key(int fam, const uint8_t *addr, int prefix, unsigned char *k) {
    memset(k, 0, 18);
    k[0] = (unsigned char)fam;
    k[1] = (unsigned char)prefix;
    int bytes = fam == 4 ? 4 : 16;
    for (int i = 0; i < bytes; i++) {
        int bits = prefix - i * 8;
        unsigned char m = bits >= 8 ? 0xff : bits <= 0 ? 0 : (unsigned char)(0xff << (8 - bits));
        k[2 + i] = addr[i] & m;
    }
    return (size_t)(2 + bytes);
}

static int put(FwMap *m, const void *key, size_t kl, uint16_t tag) {
    void **slot = fwmap_slot(m, key, kl, 1);
    if (!slot) return -2;
    if (*slot) return 1;
    *slot = (void *)(uintptr_t)tag;
    return 0;
}

/* A name with at least one dot, normalized; 0 ok. */
static int good_name(const char *in, size_t n, char *out) {
    char tmp[FW_NAME_MAX + 2];
    if (!n || n > FW_NAME_MAX) return -1;
    memcpy(tmp, in, n);
    tmp[n] = 0;
    if (fw_norm_domain(tmp, out)) return -1;
    return strchr(out, '.') ? 0 : -1;
}

static int parse_net(const char *s, size_t n, FwRule *r) {
    char tmp[80];
    if (!n || n >= sizeof tmp) return -1;
    memcpy(tmp, s, n);
    tmp[n] = 0;
    if (!strchr(tmp, ':') && !strchr(tmp, '/') && !strchr(tmp, '.')) return -1;
    if (fw_parse_dest(tmp, r) || r->kind != FW_DST_CIDR) return -1;
    return 0;
}

/* Networks a blocklist may never carry: the LAN (it would cut this machine off its own network)
 * and anything broader than /8 (IPv4) or /16 (IPv6). */
static int net_usable(const FwRule *r) {
    if (r->prefix < (r->fam == 4 ? 8 : 16)) return 0;
    return !fw_addr_is_lan(r->fam, r->addr);
}

int fwl_add_line(FwLists *l, const char *line, size_t len, uint16_t tag) {
    if (!l || !line || len < 3 || line[1] != ' ' || !tag) return -1;
    const char *v = line + 2;
    size_t vn = len - 2;
    if (line[0] == 'd' || line[0] == 's') {
        char name[FW_NAME_MAX];
        if (good_name(v, vn, name) || strlen(name) != vn || memcmp(name, v, vn)) return -1;   /* already normalized */
        if (l->ndom >= FWL_MAX_DOMAINS) return -2;
        int r = put(line[0] == 'd' ? &l->exact : &l->sub, name, vn, tag);
        if (!r) l->ndom++;
        return r;
    }
    if (line[0] == 'n') {
        FwRule r;
        if (parse_net(v, vn, &r) || !net_usable(&r)) return -1;
        if (l->nnet >= FWL_MAX_NETS) return -2;
        unsigned char k[18];
        size_t kl = net_key(r.fam, r.addr, r.prefix, k);
        int x = put(&l->nets, k, kl, tag);
        if (!x) {
            l->nnet++;
            if (r.fam == 4) l->has4[r.prefix] = 1;
            else l->has6[r.prefix] = 1;
        }
        return x;
    }
    return -1;
}

long fwl_add_text(FwLists *l, const char *text, size_t len, uint16_t tag) {
    long added = 0;
    for (size_t i = 0; i < len;) {
        const char *nl = memchr(text + i, '\n', len - i);
        size_t e = nl ? (size_t)(nl - text) : len;
        if (e > i) {
            int r = fwl_add_line(l, text + i, e - i, tag);
            if (r == -1) return -1;
            if (r == -2) return added;
            if (r == 0) added++;
        }
        i = e + 1;
    }
    return added;
}

FwLists *fwl_load_text(const char *text, size_t len, char *err, size_t errlen) {
    size_t hl = strlen(FWL_HEADER);
    if (!text || len > FWL_FILE_MAX || memchr(text, 0, len)) { snprintf(err, errlen, "not a list file"); return NULL; }
    if (len < hl || memcmp(text, FWL_HEADER, hl) || (len > hl && text[hl] != '\n')) { snprintf(err, errlen, "no header"); return NULL; }
    FwLists *l = fwl_new();
    if (!l) { snprintf(err, errlen, "out of memory"); return NULL; }
    size_t i = hl + 1, lineno = 1;
    while (i < len) {
        const char *nl = memchr(text + i, '\n', len - i);
        size_t e = nl ? (size_t)(nl - text) : len;
        lineno++;
        int r = e > i ? fwl_add_line(l, text + i, e - i, 1) : 0;
        if (r == -1 || r == -2) {
            snprintf(err, errlen, r == -2 ? "too many entries (line %zu)" : "invalid line %zu", lineno);
            fwl_free(l);
            return NULL;
        }
        i = e + 1;
    }
    return l;
}

typedef struct { char *p; size_t n, cap; int err; } Out;

static void out_put(Out *o, const char *s, size_t n) {
    if (o->err) return;
    if (o->n + n + 2 > o->cap) {
        size_t c = o->cap ? o->cap : 4096;
        while (c < o->n + n + 2) c *= 2;
        char *p = realloc(o->p, c);
        if (!p) { o->err = 1; return; }
        o->p = p;
        o->cap = c;
    }
    memcpy(o->p + o->n, s, n);
    o->n += n;
    o->p[o->n] = 0;
}

static char *text_of(const FwLists *l, int header, size_t *len) {
    Out o = { 0 };
    if (header) out_put(&o, FWL_HEADER "\n", strlen(FWL_HEADER) + 1);
    else out_put(&o, "", 0);
    char line[FW_NAME_MAX + 8];
    FWMAP_EACH(&l->exact, e) {
        int n = snprintf(line, sizeof line, "d %.*s\n", (int)e->klen, (const char *)e->key);
        out_put(&o, line, (size_t)n);
    }
    FWMAP_EACH(&l->sub, e) {
        int n = snprintf(line, sizeof line, "s %.*s\n", (int)e->klen, (const char *)e->key);
        out_put(&o, line, (size_t)n);
    }
    FWMAP_EACH(&l->nets, e) {
        char ip[INET6_ADDRSTRLEN];
        uint8_t a[16] = { 0 };
        int fam = e->key[0], prefix = e->key[1];
        memcpy(a, e->key + 2, fam == 4 ? 4 : 16);
        if (!inet_ntop(fam == 4 ? AF_INET : AF_INET6, a, ip, sizeof ip)) continue;
        int n = prefix == (fam == 4 ? 32 : 128) ? snprintf(line, sizeof line, "n %s\n", ip) : snprintf(line, sizeof line, "n %s/%d\n", ip, prefix);
        out_put(&o, line, (size_t)n);
    }
    if (o.err) { free(o.p); return NULL; }
    *len = o.n;
    return o.p;
}

char *fwl_to_text(const FwLists *l, size_t *len) { return l ? text_of(l, 1, len) : NULL; }

uint16_t fwl_match_name(const FwLists *l, const char *in) {
    if (!l || !in || !l->ndom) return 0;
    char name[FW_NAME_MAX];
    if (strnlen(in, FW_NAME_MAX + 1) > FW_NAME_MAX || fw_norm_domain(in, name)) return 0;
    void *v = fwmap_get(&l->exact, name, strlen(name));
    if (v) return (uint16_t)(uintptr_t)v;
    for (const char *sfx = name; sfx;) {
        v = fwmap_get(&l->sub, sfx, strlen(sfx));
        if (v) return (uint16_t)(uintptr_t)v;
        sfx = strchr(sfx, '.');
        if (sfx) sfx++;
    }
    return 0;
}

uint16_t fwl_match_addr(const FwLists *l, int fam, const uint8_t *addr) {
    if (!l || !addr || !l->nnet || (fam != 4 && fam != 6)) return 0;
    uint8_t a[16] = { 0 };
    static const uint8_t mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
    if (fam == 6 && !memcmp(addr, mapped, 12)) { fam = 4; memcpy(a, addr + 12, 4); }
    else memcpy(a, addr, fam == 4 ? 4 : 16);
    const unsigned char *has = fam == 4 ? l->has4 : l->has6;
    for (int p = fam == 4 ? 32 : 128; p >= 0; p--) {
        if (!has[p]) continue;
        unsigned char k[18];
        size_t kl = net_key(fam, a, p, k);
        void *v = fwmap_get(&l->nets, k, kl);
        if (v) return (uint16_t)(uintptr_t)v;
    }
    return 0;
}

void fwl_net_each(const FwLists *l, int (*cb)(void *u, int fam, const uint8_t *addr, int prefix), void *u) {
    if (!l) return;
    FWMAP_EACH(&l->nets, e) {
        uint8_t a[16] = { 0 };
        memcpy(a, e->key + 2, e->key[0] == 4 ? 4 : 16);
        if (cb(u, e->key[0], a, e->key[1])) return;
    }
}

uint16_t fwl_block_names(const FwLists *l, const char *const *names, int n) {
    uint16_t first = 0;
    for (int i = 0; i < n; i++) {
        uint16_t t = fwl_match_name(l, names[i]);
        if (!t) return 0;               /* an unlisted name shares the address */
        if (!first) first = t;
    }
    return first;
}

/* ---- raw lists ------------------------------------------------------------------------------- */

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }

/* Names a hosts file maps to itself, not blocklist entries. */
static int hosts_noise(const char *name) {
    static const char *const v[] = { "localhost", "localhost.localdomain", "local", "broadcasthost", "ip6-localhost",
                                     "ip6-loopback", "ip6-localnet", "ip6-mcastprefix", "ip6-allnodes", "ip6-allrouters",
                                     "ip6-allhosts", "0.0.0.0", NULL };
    for (int i = 0; v[i]; i++) if (!strcmp(name, v[i])) return 1;
    return 0;
}

/* The sink-hole addresses a hosts-format blocklist points names to. */
static int hosts_target(const char *s, size_t n) {
    static const char *const v[] = { "0.0.0.0", "127.0.0.1", "::", "::1", NULL };
    for (int i = 0; v[i]; i++) if (strlen(v[i]) == n && !memcmp(s, v[i], n)) return 1;
    return 0;
}

static int set_err(char *err, size_t errlen, const char *code) {
    if (err && errlen) snprintf(err, errlen, "%s", code);
    return -1;
}

/* One name from a list: adds it (d/s) or counts why not. 1 added, 0 not, -1 the set is full. */
static int raw_name(FwLists *l, const char *s, size_t n, int sub, FwlStats *st) {
    char name[FW_NAME_MAX];
    if (n > 2 && s[0] == '*' && s[1] == '.') { s += 2; n -= 2; sub = 1; }
    if (n > 3 && s[0] == '|' && s[1] == '|' && s[n - 1] == '^') { s += 2; n -= 3; sub = 1; }
    char tmp[FW_NAME_MAX + 2];
    if (!n || n > FW_NAME_MAX) { st->bad++; return 0; }
    memcpy(tmp, s, n);
    tmp[n] = 0;
    for (char *c = tmp; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    if (hosts_noise(tmp)) { st->skipped++; return 0; }
    if (fw_norm_domain(tmp, name)) { st->bad++; return 0; }
    if (!strchr(name, '.') || hosts_noise(name)) { st->skipped++; return 0; }
    char line[FW_NAME_MAX + 4];
    int ln = snprintf(line, sizeof line, "%c %s", sub ? 's' : 'd', name);
    if (st->domains >= FWL_LIST_DOMAINS) return -1;
    int r = fwl_add_line(l, line, (size_t)ln, 1);
    if (r == -2) return -1;
    if (r == 1) { st->dups++; return 0; }
    if (r) { st->bad++; return 0; }
    st->domains++;
    return 1;
}

static int raw_net(FwLists *l, const char *s, size_t n, FwlStats *st) {
    FwRule r;
    if (parse_net(s, n, &r)) { st->bad++; return 0; }
    if (!net_usable(&r)) { st->skipped++; return 0; }
    char ip[INET6_ADDRSTRLEN], line[80];
    inet_ntop(r.fam == 4 ? AF_INET : AF_INET6, r.addr, ip, sizeof ip);
    int ln = r.prefix == (r.fam == 4 ? 32 : 128) ? snprintf(line, sizeof line, "n %s", ip) : snprintf(line, sizeof line, "n %s/%d", ip, r.prefix);
    if (st->nets >= FWL_LIST_NETS) return -1;
    int x = fwl_add_line(l, line, (size_t)ln, 1);
    if (x == -2) return -1;
    if (x == 1) { st->dups++; return 0; }
    if (x) { st->bad++; return 0; }
    st->nets++;
    return 1;
}

int fwl_parse_raw(const char *raw, size_t len, FwlFormat fmt, int sub, char **out, size_t *outlen,
                  FwlStats *st, char *err, size_t errlen) {
    FwlStats z = { 0 };
    if (!st) st = &z;
    memset(st, 0, sizeof *st);
    *out = NULL;
    *outlen = 0;
    if (fmt != FWL_FMT_HOSTS && fmt != FWL_FMT_DOMAINS && fmt != FWL_FMT_IPS) return set_err(err, errlen, "format");
    if (len > FWL_RAW_MAX) return set_err(err, errlen, "big");
    if (memchr(raw, 0, len)) return set_err(err, errlen, "nul");
    FwLists *l = fwl_new();
    if (!l) return set_err(err, errlen, "oom");
    int full = 0;
    size_t i = 0;
    /* a UTF-8 byte order mark at the start */
    if (len >= 3 && (unsigned char)raw[0] == 0xef && (unsigned char)raw[1] == 0xbb && (unsigned char)raw[2] == 0xbf) i = 3;
    while (i < len && !full) {
        const char *nl = memchr(raw + i, '\n', len - i);
        size_t e = nl ? (size_t)(nl - raw) : len;
        if (e - i > FWL_LINE_MAX) { fwl_free(l); return set_err(err, errlen, "line"); }
        const char *s = raw + i;
        size_t n = e - i;
        i = e + 1;
        /* comments: whole-line (#, !, ;) and, after a space, inline (#) */
        while (n && is_space(*s)) { s++; n--; }
        if (!n || *s == '#' || *s == '!' || *s == ';' || (n >= 2 && s[0] == '/' && s[1] == '/')) continue;
        for (size_t k = 1; k < n; k++) if (s[k] == '#' && is_space(s[k - 1])) { n = k; break; }
        if (fmt == FWL_FMT_IPS) for (size_t k = 0; k < n; k++) if (s[k] == ';') { n = k; break; }
        while (n && is_space(s[n - 1])) n--;
        if (!n) continue;
        st->lines++;
        /* tokens */
        const char *tok[16];
        size_t tl[16];
        int nt = 0, over = 0;
        for (size_t k = 0; k < n;) {
            while (k < n && is_space(s[k])) k++;
            if (k >= n) break;
            size_t b = k;
            while (k < n && !is_space(s[k])) k++;
            if (nt == 16) { over = 1; break; }
            tok[nt] = s + b;
            tl[nt++] = k - b;
        }
        for (size_t k = 0; k < n; k++) if ((unsigned char)s[k] < 0x20 && !is_space(s[k])) { over = 1; break; }
        if (over || !nt) { st->bad++; continue; }
        int r = 0;
        if (fmt == FWL_FMT_HOSTS) {
            if (nt < 2 || !hosts_target(tok[0], tl[0])) { st->bad++; continue; }
            for (int t = 1; t < nt && r >= 0; t++) r = raw_name(l, tok[t], tl[t], sub, st);
        } else if (fmt == FWL_FMT_DOMAINS) {
            if (nt != 1) { st->bad++; continue; }
            r = raw_name(l, tok[0], tl[0], sub, st);
        } else {
            r = raw_net(l, tok[0], tl[0], st);     /* other columns (counts, dates) are ignored */
        }
        if (r < 0) { full = 1; st->truncated = 1; }
    }
    if (st->bad > 20 && st->bad * 10 > st->lines) { fwl_free(l); return set_err(err, errlen, "format"); }
    if (!st->domains && !st->nets) { fwl_free(l); return set_err(err, errlen, "empty"); }
    char *t = text_of(l, 0, outlen);
    fwl_free(l);
    if (!t) return set_err(err, errlen, "oom");
    *out = t;
    return 0;
}

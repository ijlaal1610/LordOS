/* npcompile.c — reglas → entradas de los mapas BPF (ver npcompile.h). */
#include "netperm/npcompile.h"
#include "fw/fwmap.h"

#include <stdlib.h>
#include <string.h>

/* ---- orden de precedencia (el mismo de consider() en fw/rules.c) ------------------------------ */

static int nlabels(const char *d) {
    int n = 1;
    for (; *d; d++) n += *d == '.';
    return n;
}

static int dst_score(const FwRule *r) {
    switch (r->kind) {
    case FW_DST_DOMAIN:   return (r->wildcard ? 2000 : 3000) + nlabels(r->domain);
    case FW_DST_CIDR:     return 1000 + r->prefix;
    case FW_DST_LAN:
    case FW_DST_INTERNET: return 500;
    default:              return 0;
    }
}

/* < 0 si a va antes que b (gana): destino más específico, rango de puertos más chico, protocolo
 * concreto, denegar, y la más nueva. */
static int cmp_prec(const void *x, const void *y) {
    const FwRule *a = *(const FwRule *const *)x, *b = *(const FwRule *const *)y;
    int da = dst_score(a), db = dst_score(b);
    if (da != db) return db - da;
    int pa = a->port_hi - a->port_lo, pb = b->port_hi - b->port_lo;
    if (pa != pb) return pa - pb;
    int qa = a->proto != FW_PROTO_ANY, qb = b->proto != FW_PROTO_ANY;
    if (qa != qb) return qb - qa;
    if (a->action != b->action) return a->action == FW_DENY ? -1 : 1;
    return a->id > b->id ? -1 : a->id < b->id;
}

static void ent_of(const FwRule *r, NpEnt *e) {
    memset(e, 0, sizeof *e);
    e->port_lo = r->port_lo;
    e->port_hi = r->port_hi;
    e->proto = r->proto == FW_PROTO_TCP ? NP_P_TCP : r->proto == FW_PROTO_UDP ? NP_P_UDP : NP_P_ANY;
    e->dir = r->dir == FW_IN ? NP_D_IN : NP_D_OUT;
    e->verdict = r->action == FW_DENY ? NP_V_DENY : NP_V_ALLOW;
    e->kind = r->kind == FW_DST_LAN ? NP_K_LAN : r->kind == FW_DST_INTERNET ? NP_K_INET : r->kind == FW_DST_ANY ? NP_K_ANY : NP_K_RULE;
    e->rule_id = r->id;
}

/* Ordena v[0..n) y lo copia en la lista (hasta max); devuelve cuántas quedaron afuera. */
static int fill(const FwRule **v, int n, NpEnt *out, np_u32 *count, int max) {
    qsort(v, (size_t)n, sizeof *v, cmp_prec);
    int k = n < max ? n : max;
    for (int i = 0; i < k; i++) ent_of(v[i], &out[i]);
    *count = (np_u32)k;
    return n - k;
}

/* ---- direcciones y dominios ------------------------------------------------------------------ */

static int bit_at(const uint8_t *a, int i) { return a[i >> 3] >> (7 - (i & 7)) & 1; }

int np_cidr_contains(const FwRule *r, int fam, const uint8_t *addr, int prefix) {
    if (r->kind != FW_DST_CIDR || r->fam != fam || r->prefix > prefix) return 0;
    for (int i = 0; i < r->prefix; i++) if (bit_at(r->addr, i) != bit_at(addr, i)) return 0;
    return 1;
}

static int domain_match(const FwRule *r, const char *name) {
    if (r->kind != FW_DST_DOMAIN) return 0;
    if (!strcmp(r->domain, name)) return 1;
    if (!r->wildcard) return 0;
    size_t nl = strlen(name), dl = strlen(r->domain);
    return nl > dl && name[nl - dl - 1] == '.' && !strcmp(name + nl - dl, r->domain);
}

/* ---- armado ---------------------------------------------------------------------------------- */

typedef struct {
    FwRule *rules;
    int n;
} Rules;

static int load_rules(FwStore *s, const char *app, Rules *R) {
    R->rules = NULL;
    R->n = 0;
    int total = fw_rule_list(s, app, NULL, 0);
    if (total < 0) return -1;
    if (!total) return 0;
    FwRule *all = malloc((size_t)total * sizeof *all);
    if (!all) return -1;
    int got = fw_rule_list(s, app, all, total);
    if (got < 0) { free(all); return -1; }
    if (got > total) got = total;
    int k = 0;
    for (int i = 0; i < got; i++) if (all[i].life != FW_LIFE_ONCE) all[k++] = all[i];
    R->rules = all;
    R->n = k;
    return 0;
}

static NpNode *node_push(NpCompiled *c) {
    if (c->nnodes == c->cap) {
        int nc = c->cap ? c->cap * 2 : 16;
        NpNode *nn = realloc(c->nodes, (size_t)nc * sizeof *nn);
        if (!nn) return NULL;
        c->nodes = nn;
        c->cap = nc;
    }
    NpNode *n = &c->nodes[c->nnodes++];
    memset(n, 0, sizeof *n);
    return n;
}

/* Lista de un nodo: las reglas de dominio dadas (ya elegidas) + las redes que lo contienen. */
static int build_list(const Rules *R, const FwRule **dom, int ndom, NpNode *node, int *trunc) {
    const FwRule **v = malloc(((size_t)R->n + (size_t)ndom + 1) * sizeof *v);
    if (!v) return -1;
    int k = 0;
    for (int i = 0; i < ndom; i++) v[k++] = dom[i];
    for (int i = 0; i < R->n; i++)
        if (np_cidr_contains(&R->rules[i], node->fam, node->addr, node->prefix)) v[k++] = &R->rules[i];
    *trunc += fill(v, k, node->list.e, &node->list.n, NP_LIST_MAX);
    free(v);
    return 0;
}

static int host_rules(const Rules *R, const char (*names)[FW_NAME_MAX], int n, const FwRule ***out) {
    const FwRule **v = malloc(((size_t)R->n + 1) * sizeof *v);
    if (!v) return -1;
    int k = 0;
    for (int i = 0; i < R->n; i++) {
        const FwRule *r = &R->rules[i];
        if (r->kind != FW_DST_DOMAIN) continue;
        for (int j = 0; j < n; j++) if (domain_match(r, names[j])) { v[k++] = r; break; }
    }
    *out = v;
    return k;
}

static void node_key(const NpNode *n, unsigned char *key, size_t *len) {
    key[0] = (unsigned char)n->fam;
    key[1] = (unsigned char)n->prefix;
    size_t al = n->fam == 4 ? 4 : 16;
    memcpy(key + 2, n->addr, al);
    *len = 2 + al;
}

typedef struct {
    const Rules *R;
    NpCompiled *c;
    FwMap *seen;                /* clave del nodo → índice + 1 */
    int err;
} HostCtx;

static int host_cb(void *u, int fam, const uint8_t *addr, const char (*names)[FW_NAME_MAX], int n) {
    HostCtx *h = u;
    const FwRule **dom;
    int nd = host_rules(h->R, names, n, &dom);
    if (nd < 0) { h->err = 1; return 1; }
    if (!nd) { free(dom); return 0; }
    NpNode tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.fam = fam;
    tmp.prefix = fam == 4 ? 32 : 128;
    memcpy(tmp.addr, addr, fam == 4 ? 4 : 16);
    tmp.host = 1;
    if (build_list(h->R, dom, nd, &tmp, &h->c->truncated)) { free(dom); h->err = 1; return 1; }
    free(dom);
    unsigned char key[18];
    size_t kl;
    node_key(&tmp, key, &kl);
    void **slot = fwmap_slot(h->seen, key, kl, 1);
    if (!slot) { h->err = 1; return 1; }
    if (*slot) {                                /* había un /32 de una regla: este lo incluye */
        h->c->nodes[(intptr_t)*slot - 1] = tmp;
        return 0;
    }
    NpNode *nn = node_push(h->c);
    if (!nn) { h->err = 1; return 1; }
    *nn = tmp;
    *slot = (void *)(intptr_t)h->c->nnodes;
    return 0;
}

int np_compile_app(FwStore *s, const char *app, uint32_t idx, DnsMap *names, const char *names_app,
                   long long now, NpCompiled *out) {
    memset(out, 0, sizeof *out);
    Rules R;
    if (load_rules(s, app, &R)) return -1;
    NpApp *a = &out->app;
    a->idx = idx;
    a->flags = fw_restricted(s, app) & (NP_R_NO_INTERNET | NP_R_NO_LAN | NP_R_NO_NETWORK | NP_R_NO_INCOMING);
    a->trusted = (np_u8)(fw_is_trusted(s, app) != 0);
    a->mode = fw_mode(s) == FW_MODE_OFF ? NP_M_OFF : NP_M_SILENT;

    int rc = -1;
    const FwRule **v = malloc(((size_t)R.n + 1) * sizeof *v);
    FwMap seen = { 0 };
    if (!v) goto done;
    int k = 0;
    for (int i = 0; i < R.n; i++) {
        FwDstKind kd = R.rules[i].kind;
        if (kd == FW_DST_ANY || kd == FW_DST_LAN || kd == FW_DST_INTERNET) v[k++] = &R.rules[i];
        if (kd == FW_DST_DOMAIN) out->has_domains = 1;
    }
    out->truncated += fill(v, k, a->gen, &a->ngen, NP_GEN_MAX);

    /* un nodo por cada red distinta */
    for (int i = 0; i < R.n; i++) {
        const FwRule *r = &R.rules[i];
        if (r->kind != FW_DST_CIDR) continue;
        NpNode tmp;
        memset(&tmp, 0, sizeof tmp);
        tmp.fam = r->fam;
        tmp.prefix = r->prefix;
        memcpy(tmp.addr, r->addr, 16);
        unsigned char key[18];
        size_t kl;
        node_key(&tmp, key, &kl);
        void **slot = fwmap_slot(&seen, key, kl, 1);
        if (!slot) goto done;
        if (*slot) continue;
        if (build_list(&R, NULL, 0, &tmp, &out->truncated)) goto done;
        NpNode *nn = node_push(out);
        if (!nn) goto done;
        *nn = tmp;
        *slot = (void *)(intptr_t)out->nnodes;
    }
    /* un nodo por cada dirección con un nombre que coincide con una regla de dominio */
    if (out->has_domains && names && names_app) {
        HostCtx h = { &R, out, &seen, 0 };
        dnsmap_each(names, names_app, now, host_cb, &h);
        if (h.err) goto done;
    }
    for (int i = 0; i < out->nnodes; i++) {
        if (out->nodes[i].fam == 4) a->has4 = 1;
        else a->has6 = 1;
    }
    rc = 0;
done:
    fwmap_free(&seen, NULL);
    free(v);
    free(R.rules);
    if (rc) np_compiled_free(out);
    return rc;
}

int np_compile_host(FwStore *s, const char *app, int fam, const uint8_t *addr,
                    const char (*names)[FW_NAME_MAX], int n, NpNode *out, int *truncated) {
    Rules R;
    if (load_rules(s, app, &R)) return -1;
    const FwRule **dom;
    int nd = host_rules(&R, names, n, &dom);
    int rc = nd < 0 ? -1 : 0;
    if (nd > 0) {
        memset(out, 0, sizeof *out);
        out->fam = fam;
        out->prefix = fam == 4 ? 32 : 128;
        memcpy(out->addr, addr, fam == 4 ? 4 : 16);
        out->host = 1;
        int tr = 0;
        rc = build_list(&R, dom, nd, out, &tr) ? -1 : 1;
        if (truncated) *truncated += tr;
    }
    if (nd >= 0) free(dom);
    free(R.rules);
    return rc;
}

void np_compiled_free(NpCompiled *c) {
    if (!c) return;
    free(c->nodes);
    c->nodes = NULL;
    c->nnodes = c->cap = 0;
}

static void put_idx(uint8_t *d, uint32_t idx) {
    d[0] = (uint8_t)(idx >> 24); d[1] = (uint8_t)(idx >> 16); d[2] = (uint8_t)(idx >> 8); d[3] = (uint8_t)idx;
}

void np_key4(uint32_t idx, const NpNode *n, NpLpm4Key *k) {
    memset(k, 0, sizeof *k);
    k->prefixlen = 32 + (uint32_t)n->prefix;
    put_idx(k->data, idx);
    memcpy(k->data + 4, n->addr, 4);
}

void np_key6(uint32_t idx, const NpNode *n, NpLpm6Key *k) {
    memset(k, 0, sizeof *k);
    k->prefixlen = 32 + (uint32_t)n->prefix;
    put_idx(k->data, idx);
    memcpy(k->data + 4, n->addr, 16);
}

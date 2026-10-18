/* resolve.c — resolvedor de dependencias (ver resolve.h).
 *
 * Cada paquete que aparece es un nodo con su versión instalada (de dpkg), su candidata (del
 * índice) y su destino: quedarse como está, instalar/actualizar a un registro del índice, o no
 * estar. Los pedidos cambian destinos y encolan los grupos de dependencias de lo que cambia; la
 * cola se procesa hasta que todo grupo esté cumplido. Cuando un grupo se puede cumplir de varias
 * maneras (alternativas "a | b", paquetes virtuales) se prueba en orden con vuelta atrás, con un
 * presupuesto fijo. Los conflictos (Conflicts/Breaks, en los dos sentidos) se resuelven
 * actualizando al otro paquete si su versión nueva ya no choca, y si no quitándolo; las quitas
 * que no se pidieron necesitan confirmación (allow_remove/cascade). Al final se repasa todo lo
 * que cambió y lo que depende de ello hasta que no cambie nada más. */
#include "resolve.h"
#include "deb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T_KEEP (-2)
#define T_NONE (-1)
#define MAX_ALTS 32
#define BUDGET 4000

typedef struct {
    const char *name;
    size_t nlen;
    const StPkg *inst;
    long cand, target;
    uint8_t requested, remove_req, touched, orphan, mark;
    char reason[200];
} Node;

typedef struct { int node; long old; uint8_t old_touched; char reason[200]; } Undo;

typedef struct { int node; long expect; const char *g, *ge; uint8_t optional; } Item;

typedef struct { const char *name; size_t nlen; const StPkg *p; const char *ver; size_t vlen; int op; } Ref;

typedef struct {
    const Idx *x;
    const Status *st;
    const PlanOpts *o;
    Node **blocks;                      /* bloques de 1024: los punteros a nodos no se mueven */
    int nn, nblocks;
    int *ht;
    int htcap;
    Undo *log;
    int nlog, caplog;
    Item *q;
    int nq, capq;
    Ref *prov, *rdep, *conf;            /* de lo instalado: provee, depende de, choca con */
    int nprov, nrdep, nconf;
    int budget, may_remove;
    char err[512];
} S;

/* ---- nodos ---- */

static inline Node *nd(const S *s, int i) { return &s->blocks[i >> 10][i & 1023]; }

static uint32_t hash(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h;
}

long pkg_candidate(const Idx *x, const char *name, size_t n, const StPkg *inst) {
    long first = idx_find(x, name, n);
    if (first < 0) return -1;
    long best500 = -1, best100 = -1;
    int inst_in_100 = 0;
    for (long i = first; i < (long)x->h->npkg && !strcmp(idx_s(x, x->pkg[i].name), idx_s(x, x->pkg[first].name)); i++) {
        uint32_t prio = x->src[x->pkg[i].src].prio;
        if (prio >= 500 && best500 < 0) best500 = i;
        if (prio >= 100 && best100 < 0) best100 = i;
        if (inst && prio >= 100 && prio < 500 && !strcmp(idx_s(x, x->pkg[i].version), inst->version)) inst_in_100 = 1;
    }
    if (!inst) return best500 >= 0 ? best500 : best100;
    /* instalado desde backports (o más nuevo que lo normal): sigue con la versión más alta */
    long c = best500;
    if (inst_in_100 || c < 0 || ver_cmp(idx_s(x, x->pkg[c].version), inst->version) < 0) c = best100;
    if (c >= 0 && ver_cmp(idx_s(x, x->pkg[c].version), inst->version) < 0) return -1;     /* nunca bajar */
    return c;
}

static int node_get(S *s, const char *name, size_t n) {
    if ((s->nn + 1) * 2 > s->htcap) {
        int cap = s->htcap ? s->htcap * 2 : 4096;
        int *h = malloc(sizeof *h * (size_t)cap);
        if (!h) return -1;
        memset(h, 0xff, sizeof *h * (size_t)cap);
        for (int i = 0; i < s->nn; i++) {
            uint32_t j = hash(nd(s, i)->name, nd(s, i)->nlen) & (uint32_t)(cap - 1);
            while (h[j] >= 0) j = (j + 1) & (uint32_t)(cap - 1);
            h[j] = i;
        }
        free(s->ht);
        s->ht = h;
        s->htcap = cap;
    }
    uint32_t j = hash(name, n) & (uint32_t)(s->htcap - 1);
    while (s->ht[j] >= 0) {
        Node *e = nd(s, s->ht[j]);
        if (e->nlen == n && !memcmp(e->name, name, n)) return s->ht[j];
        j = (j + 1) & (uint32_t)(s->htcap - 1);
    }
    if ((s->nn >> 10) >= s->nblocks) {
        Node **nb = realloc(s->blocks, sizeof *nb * (size_t)(s->nblocks + 1));
        if (!nb) return -1;
        s->blocks = nb;
        if (!(s->blocks[s->nblocks] = malloc(sizeof(Node) * 1024))) return -1;
        s->nblocks++;
    }
    Node *e = nd(s, s->nn);
    memset(e, 0, sizeof *e);
    e->inst = status_find(s->st, name, n);
    e->name = e->inst ? e->inst->name : name;          /* un puntero estable */
    e->nlen = n;
    if (!e->inst) {
        long f = idx_find(s->x, name, n);
        if (f >= 0) e->name = idx_s(s->x, s->x->pkg[f].name);
    }
    e->cand = pkg_candidate(s->x, name, n, e->inst);
    e->target = e->inst ? T_KEEP : T_NONE;
    s->ht[j] = s->nn;
    return s->nn++;
}

static int present(const Node *n) { return n->target != T_NONE; }

static const char *node_ver(const S *s, const Node *n) {
    if (n->target == T_KEEP) return n->inst->version;
    if (n->target >= 0) return idx_s(s->x, s->x->pkg[n->target].version);
    return NULL;
}

enum { F_DEPENDS, F_PREDEPENDS, F_RECOMMENDS, F_PROVIDES, F_CONFLICTS, F_BREAKS };

static const char *field_of(const S *s, const Node *n, long target, int f) {
    if (target == T_KEEP) {
        const StPkg *p = n->inst;
        const char *v = f == F_DEPENDS ? p->depends : f == F_PREDEPENDS ? p->predepends : f == F_RECOMMENDS ? p->recommends :
                        f == F_PROVIDES ? p->provides : f == F_CONFLICTS ? p->conflicts : p->breaks;
        return v ? v : "";
    }
    if (target < 0) return "";
    const IdxPkg *p = &s->x->pkg[target];
    uint32_t o = f == F_DEPENDS ? p->depends : f == F_PREDEPENDS ? p->predepends : f == F_RECOMMENDS ? p->recommends :
                 f == F_PROVIDES ? p->provides : f == F_CONFLICTS ? p->conflicts : p->breaks;
    return idx_s(s->x, o);
}

/* ---- tablas de lo instalado ---- */

static int ref_cmp(const void *a, const void *b) {
    const Ref *x = a, *y = b;
    size_t n = x->nlen < y->nlen ? x->nlen : y->nlen;
    int c = memcmp(x->name, y->name, n);
    return c ? c : (x->nlen < y->nlen ? -1 : x->nlen > y->nlen);
}

static int refs_add(Ref **v, int *n, int *cap, const char *field, const StPkg *p) {
    if (!field) return 0;
    const char *e = field + strlen(field);
    DebRel r[MAX_ALTS];
    int k;
    while ((k = deb_next_group(&field, e, r, MAX_ALTS)) > 0)
        for (int i = 0; i < k; i++) {
            if (*n == *cap) {
                *cap = *cap ? *cap * 2 : 1024;
                Ref *nv = realloc(*v, sizeof *nv * (size_t)*cap);
                if (!nv) return -1;
                *v = nv;
            }
            (*v)[(*n)++] = (Ref){ r[i].name, r[i].nlen, p, r[i].ver, r[i].vlen, r[i].op };
        }
    return 0;
}

static int build_tables(S *s) {
    int cp = 0, cr = 0, cc = 0;
    for (int i = 0; i < s->st->n; i++) {
        const StPkg *p = &s->st->p[i];
        if (!p->native) continue;
        if (refs_add(&s->prov, &s->nprov, &cp, p->provides, p) || refs_add(&s->rdep, &s->nrdep, &cr, p->depends, p) ||
            refs_add(&s->rdep, &s->nrdep, &cr, p->predepends, p) || refs_add(&s->conf, &s->nconf, &cc, p->conflicts, p) ||
            refs_add(&s->conf, &s->nconf, &cc, p->breaks, p)) return -1;
    }
    qsort(s->prov, (size_t)s->nprov, sizeof *s->prov, ref_cmp);
    qsort(s->rdep, (size_t)s->nrdep, sizeof *s->rdep, ref_cmp);
    qsort(s->conf, (size_t)s->nconf, sizeof *s->conf, ref_cmp);
    return 0;
}

static void refs_range(const Ref *v, int n, const char *name, size_t len, int *lo, int *hi) {
    Ref key = { name, len, NULL, NULL, 0, 0 };
    int a = 0, b = n;
    while (a < b) { int m = (a + b) / 2; if (ref_cmp(&v[m], &key) < 0) a = m + 1; else b = m; }
    int e = a;
    while (e < n && !ref_cmp(&v[e], &key)) e++;
    *lo = a;
    *hi = e;
}

/* ---- registro para deshacer ---- */

static int mark(S *s, int node) {
    if (s->nlog == s->caplog) {
        int cap = s->caplog ? s->caplog * 2 : 256;
        Undo *n = realloc(s->log, sizeof *n * (size_t)cap);
        if (!n) return -1;
        s->log = n;
        s->caplog = cap;
    }
    Undo *u = &s->log[s->nlog++];
    u->node = node;
    u->old = nd(s, node)->target;
    u->old_touched = nd(s, node)->touched;
    memcpy(u->reason, nd(s, node)->reason, sizeof u->reason);
    return 0;
}

static void undo_to(S *s, int pos) {
    while (s->nlog > pos) {
        Undo *u = &s->log[--s->nlog];
        Node *n = nd(s, u->node);
        n->target = u->old;
        n->touched = u->old_touched;
        memcpy(n->reason, u->reason, sizeof n->reason);
    }
}

static int enqueue(S *s, int node, long expect, const char *field, int optional) {
    const char *e = field + strlen(field), *p = field;
    DebRel r[MAX_ALTS];
    for (;;) {
        const char *g = p;
        while (g < e && (*g == ' ' || *g == ',' || *g == '\n')) g++;
        if (deb_next_group(&p, e, r, MAX_ALTS) <= 0) break;
        if (s->nq == s->capq) {
            int cap = s->capq ? s->capq * 2 : 1024;
            Item *n = realloc(s->q, sizeof *n * (size_t)cap);
            if (!n) return -1;
            s->q = n;
            s->capq = cap;
        }
        s->q[s->nq++] = (Item){ node, expect, g, p, (uint8_t)optional };
    }
    return 0;
}

static int enqueue_deps(S *s, int node) {
    Node *n = nd(s, node);
    if (!present(n)) return 0;
    return enqueue(s, node, n->target, field_of(s, n, n->target, F_PREDEPENDS), 0) ||
           enqueue(s, node, n->target, field_of(s, n, n->target, F_DEPENDS), 0) ||
           (s->o->recommends && n->target >= 0 && enqueue(s, node, n->target, field_of(s, n, n->target, F_RECOMMENDS), 1));
}

/* ---- cumplimiento ---- */

/* ¿El campo provides (de un paquete presente) provee name, con una versión que cumple r? */
static int provides_match(const char *provides, const DebRel *r) {
    const char *e = provides + strlen(provides);
    DebRel p[MAX_ALTS];
    int k;
    while ((k = deb_next_group(&provides, e, p, MAX_ALTS)) > 0)
        for (int i = 0; i < k; i++) {
            if (p[i].nlen != r->nlen || memcmp(p[i].name, r->name, r->nlen)) continue;
            if (r->op == REL_NONE) return 1;
            if (p[i].op != REL_EQ) continue;
            char v[160];
            snprintf(v, sizeof v, "%.*s", (int)p[i].vlen, p[i].ver);
            if (ver_satisfies(v, r->op, r->ver, r->vlen)) return 1;
        }
    return 0;
}

static int rel_by_node(const S *s, const Node *n, const DebRel *r) {
    if (!present(n)) return 0;
    if (n->nlen == r->nlen && !memcmp(n->name, r->name, r->nlen) && ver_satisfies(node_ver(s, n), r->op, r->ver, r->vlen)) return 1;
    return provides_match(field_of(s, n, n->target, F_PROVIDES), r);
}

/* ¿Algún paquete presente cumple r? exclude: nodo que no cuenta (para conflictos). */
static int who_satisfies(S *s, const DebRel *r, int exclude, int *out, int max) {
    int found = 0;
    int ni = node_get(s, r->name, r->nlen);
    if (ni >= 0 && ni != exclude && present(nd(s, ni)) && ver_satisfies(node_ver(s, nd(s, ni)), r->op, r->ver, r->vlen)) {
        if (found < max) out[found] = ni;
        found++;
    }
    int lo, hi;
    refs_range(s->prov, s->nprov, r->name, r->nlen, &lo, &hi);
    for (int i = lo; i < hi && found < max; i++) {
        int pi = node_get(s, s->prov[i].p->name, strlen(s->prov[i].p->name));
        if (pi < 0 || pi == exclude || pi == ni || nd(s, pi)->target != T_KEEP) continue;
        if (!provides_match(nd(s, pi)->inst->provides, r)) continue;
        int dup = 0;
        for (int k = 0; k < found; k++) if (out[k] == pi) dup = 1;
        if (!dup) out[found++] = pi;
    }
    long plo, phi;
    idx_providers(s->x, r->name, r->nlen, &plo, &phi);
    for (long i = plo; i < phi && found < max; i++) {
        long rec = s->x->prov[i].pkg;
        const char *pn = idx_s(s->x, s->x->pkg[rec].name);
        int pi = node_get(s, pn, strlen(pn));
        if (pi < 0 || pi == exclude || pi == ni || nd(s, pi)->target != rec) continue;
        if (!provides_match(idx_s(s->x, s->x->pkg[rec].provides), r)) continue;
        int dup = 0;
        for (int k = 0; k < found; k++) if (out[k] == pi) dup = 1;
        if (!dup) out[found++] = pi;
    }
    return found;
}

static int group_ok(S *s, const DebRel *alts, int n) {
    int who[1];
    for (int i = 0; i < n; i++) if (who_satisfies(s, &alts[i], -1, who, 1)) return 1;
    return 0;
}

/* ---- aplicar cambios ---- */

static int apply_install(S *s, int ni, long rec, const char *why);

static int removable(const Node *n) {
    return !n->inst || !(n->inst->flags & (IF_ESSENTIAL | IF_PROTECTED));
}

static int apply_remove(S *s, int ni, const char *why) {
    Node *n = nd(s, ni);
    if (!removable(n)) { snprintf(s->err, sizeof s->err, "%s es esencial para el sistema y no se puede quitar", n->name); return 0; }
    if (mark(s, ni)) return 0;
    n->target = T_NONE;
    n->touched = 1;
    if (!n->remove_req) snprintf(n->reason, sizeof n->reason, "%s", why);
    /* lo instalado que dependía de este paquete (o de algo que proveía) se vuelve a revisar */
    const char *names[MAX_ALTS + 1];
    size_t lens[MAX_ALTS + 1];
    int nn = 0;
    names[nn] = n->name; lens[nn++] = n->nlen;
    if (n->inst && n->inst->provides) {
        const char *p = n->inst->provides, *e = p + strlen(p);
        DebRel r[MAX_ALTS];
        int k;
        while ((k = deb_next_group(&p, e, r, MAX_ALTS)) > 0 && nn < MAX_ALTS)
            for (int i = 0; i < k && nn < MAX_ALTS; i++) { names[nn] = r[i].name; lens[nn++] = r[i].nlen; }
    }
    for (int k = 0; k < nn; k++) {
        int lo, hi;
        refs_range(s->rdep, s->nrdep, names[k], lens[k], &lo, &hi);
        for (int i = lo; i < hi; i++) {
            int di = node_get(s, s->rdep[i].p->name, strlen(s->rdep[i].p->name));
            if (di >= 0 && nd(s, di)->target == T_KEEP && enqueue_deps(s, di)) return 0;
        }
    }
    return 1;
}

/* ¿Chocan el registro rec (si fuera el destino de a) y el nodo b tal como está? */
static int clash(S *s, int a, long rec, int b) {
    Node *na = nd(s, a), *nb = nd(s, b);
    if (!present(nb) || a == b) return 0;
    for (int f = F_CONFLICTS; f <= F_BREAKS; f++) {
        const char *p = field_of(s, na, rec, f), *e = p + strlen(p);
        DebRel r[MAX_ALTS];
        int k;
        while ((k = deb_next_group(&p, e, r, MAX_ALTS)) > 0)
            for (int i = 0; i < k; i++) if (rel_by_node(s, nb, &r[i])) return 1;
        /* y al revés: lo que b rechaza contra la versión rec de a */
        p = field_of(s, nb, nb->target, f);
        e = p + strlen(p);
        while ((k = deb_next_group(&p, e, r, MAX_ALTS)) > 0)
            for (int i = 0; i < k; i++) {
                if (r[i].nlen == na->nlen && !memcmp(r[i].name, na->name, na->nlen)) {
                    const char *v = rec == T_KEEP ? na->inst->version : idx_s(s->x, s->x->pkg[rec].version);
                    if (ver_satisfies(v, r[i].op, r[i].ver, r[i].vlen)) return 1;
                }
                if (provides_match(field_of(s, na, rec, F_PROVIDES), &r[i]) && r[i].op == REL_NONE) return 1;
            }
    }
    return 0;
}

static int fix_clash(S *s, int ni, int ti) {
    Node *n = nd(s, ni), *t = nd(s, ti);
    char why[200];
    if (t->target == T_KEEP) {
        if (t->cand >= 0 && ver_cmp(idx_s(s->x, s->x->pkg[t->cand].version), t->inst->version) > 0 && !clash(s, ti, t->cand, ni)) {
            snprintf(why, sizeof why, "se actualiza para %s", n->name);
            return apply_install(s, ti, t->cand, why);
        }
        if (s->may_remove && removable(t) && !t->requested) {
            snprintf(why, sizeof why, "choca con %s", n->name);
            return apply_remove(s, ti, why);
        }
        snprintf(s->err, sizeof s->err, "%s choca con %s, que está instalado", n->name, t->name);
        return 0;
    }
    snprintf(s->err, sizeof s->err, "%s y %s no se pueden instalar juntos", n->name, t->name);
    return 0;
}

static int apply_install(S *s, int ni, long rec, const char *why) {
    Node *n = nd(s, ni);
    if (n->remove_req) { snprintf(s->err, sizeof s->err, "%s hace falta, pero se pidió quitarlo", n->name); return 0; }
    if (mark(s, ni)) return 0;
    n->target = rec;
    n->touched = 1;
    if (!n->requested && why) snprintf(n->reason, sizeof n->reason, "%s", why);
    if (enqueue_deps(s, ni)) return 0;
    /* conflictos con todo lo presente que se nombra en sus Conflicts/Breaks */
    for (int f = F_CONFLICTS; f <= F_BREAKS; f++) {
        const char *p = field_of(s, n, rec, f), *e = p + strlen(p);
        DebRel r[MAX_ALTS];
        int k;
        while ((k = deb_next_group(&p, e, r, MAX_ALTS)) > 0)
            for (int i = 0; i < k; i++) {
                int who[16];
                int c = who_satisfies(s, &r[i], ni, who, 16);
                for (int w = 0; w < c && w < 16; w++)
                    if (nd(s, ni)->target == rec && present(nd(s, who[w])) && clash(s, ni, rec, who[w]) && !fix_clash(s, ni, who[w])) return 0;
            }
    }
    /* y lo instalado que rechaza a este paquete (por nombre o por lo que provee) */
    const char *names[MAX_ALTS + 1];
    size_t lens[MAX_ALTS + 1];
    int nn = 0;
    names[nn] = n->name; lens[nn++] = n->nlen;
    {
        const char *p = idx_s(s->x, s->x->pkg[rec].provides), *e = p + strlen(p);
        DebRel r[MAX_ALTS];
        int k;
        while ((k = deb_next_group(&p, e, r, MAX_ALTS)) > 0 && nn < MAX_ALTS)
            for (int i = 0; i < k && nn < MAX_ALTS; i++) { names[nn] = r[i].name; lens[nn++] = r[i].nlen; }
    }
    for (int k = 0; k < nn; k++) {
        int lo, hi;
        refs_range(s->conf, s->nconf, names[k], lens[k], &lo, &hi);
        for (int i = lo; i < hi; i++) {
            int ti = node_get(s, s->conf[i].p->name, strlen(s->conf[i].p->name));
            if (ti < 0 || ti == ni || nd(s, ti)->target != T_KEEP) continue;
            if (nd(s, ni)->target == rec && clash(s, ni, rec, ti) && !fix_clash(s, ni, ti)) return 0;
        }
    }
    /* lo nuevo de esta misma operación */
    for (int t = 0; t < s->nn; t++)
        if (t != ni && nd(s, t)->touched && nd(s, t)->target >= 0 && nd(s, ni)->target == rec && clash(s, ni, rec, t) && !fix_clash(s, ni, t)) return 0;
    return 1;
}

/* ---- búsqueda ---- */

typedef struct { int node; long rec; } Opt;

static int prov_rank(const S *s, long rec) {
    int p = s->x->pkg[rec].prio;
    return p ? p : 9;
}

static int gather(S *s, const DebRel *alts, int n, Opt *opt, int max) {
    int k = 0;
    for (int a = 0; a < n && k < max; a++) {
        int ni = node_get(s, alts[a].name, alts[a].nlen);
        if (ni < 0) continue;
        Node *an = nd(s, ni);
        /* lo que esta misma operación quitó no se vuelve a instalar */
        if (an->cand >= 0 && !an->remove_req && an->target != an->cand && !(an->inst && an->target == T_NONE) &&
            ver_satisfies(idx_s(s->x, s->x->pkg[an->cand].version), alts[a].op, alts[a].ver, alts[a].vlen))
            opt[k++] = (Opt){ ni, an->cand };
        long lo, hi;
        idx_providers(s->x, alts[a].name, alts[a].nlen, &lo, &hi);
        int first = k;
        for (long i = lo; i < hi && k < max; i++) {
            long rec = s->x->prov[i].pkg;
            const char *pn = idx_s(s->x, s->x->pkg[rec].name);
            int pi = node_get(s, pn, strlen(pn));
            if (pi < 0) continue;
            Node *pd = nd(s, pi);
            if (pd->cand != rec || pd->remove_req || pd->target == rec || (pd->inst && pd->target == T_NONE)) continue;
            if (!provides_match(idx_s(s->x, s->x->pkg[rec].provides), &alts[a])) continue;
            int dup = 0;
            for (int j = 0; j < k; j++) if (opt[j].node == pi) dup = 1;
            if (!dup) opt[k++] = (Opt){ pi, rec };
        }
        /* proveedores: primero los de más prioridad (required, important…), después por nombre */
        for (int i = first + 1; i < k; i++)
            for (int j = i; j > first; j--) {
                int ra = prov_rank(s, opt[j - 1].rec), rb = prov_rank(s, opt[j].rec);
                if (ra < rb || (ra == rb && strcmp(nd(s, opt[j - 1].node)->name, nd(s, opt[j].node)->name) <= 0)) break;
                Opt t = opt[j]; opt[j] = opt[j - 1]; opt[j - 1] = t;
            }
    }
    return k;
}

/* Largo del texto de un grupo sin la coma ni los espacios del final. */
static int group_len(const char *g, const char *ge) {
    while (ge > g && (ge[-1] == ',' || ge[-1] == ' ' || ge[-1] == '\n')) ge--;
    return (int)(ge - g > 120 ? 120 : ge - g);
}

static void describe_missing(S *s, const Node *owner, const char *g, const char *ge) {
    snprintf(s->err, sizeof s->err, "%s depende de %.*s, que no se puede instalar", owner->name, group_len(g, ge), g);
}

static int solve(S *s, int qpos) {
    while (qpos < s->nq) {
        Item it = s->q[qpos++];
        Node *owner = nd(s, it.node);
        if (owner->target != it.expect) continue;           /* cambió: sus dependencias ya se encolaron */
        DebRel alts[MAX_ALTS];
        const char *p = it.g;
        int na = deb_next_group(&p, it.ge, alts, MAX_ALTS);
        if (na <= 0 || group_ok(s, alts, na)) continue;
        Opt opt[MAX_ALTS + 1];
        int no = 0;
        /* algo instalado que se quedó sin lo que pide: primero, actualizarlo a él */
        if (owner->target == T_KEEP && owner->cand >= 0 && !owner->remove_req &&
            ver_cmp(idx_s(s->x, s->x->pkg[owner->cand].version), owner->inst->version) > 0)
            opt[no++] = (Opt){ it.node, owner->cand };
        no += gather(s, alts, na, opt + no, MAX_ALTS);
        char why[200];
        snprintf(why, sizeof why, "dependencia de %s", owner->name);
        /* si nada sirve y es algo instalado, se quita también (con confirmación) */
        int can_drop = owner->target == T_KEEP && s->may_remove && !owner->requested && removable(owner);
        char w2[200];
        snprintf(w2, sizeof w2, "dependía de %.*s", group_len(it.g, it.ge), it.g);
        if (!no) {
            if (it.optional) continue;
            if (can_drop) { if (!apply_remove(s, it.node, w2)) return 0; continue; }
            describe_missing(s, owner, it.g, it.ge);
            return 0;
        }
        /* una sola opción y nada más que probar: sin vuelta atrás. Si quitar al dueño es otra
         * salida, se prueba la opción con el resto de la cola y si falla se lo quita. */
        if (no == 1 && !can_drop) {
            int lm = s->nlog, qm = s->nq;
            if (!apply_install(s, opt[0].node, opt[0].rec, opt[0].node == it.node ? "actualización" : why)) {
                undo_to(s, lm);
                s->nq = qm;
                if (it.optional) continue;
                return 0;
            }
            continue;
        }
        for (int k = 0; k < no; k++) {
            if (--s->budget <= 0) { snprintf(s->err, sizeof s->err, "demasiadas combinaciones posibles; pedí los paquetes de a uno"); return 0; }
            int lm = s->nlog, qm = s->nq;
            if (apply_install(s, opt[k].node, opt[k].rec, opt[k].node == it.node ? "actualización" : why) && solve(s, qpos)) return 1;
            undo_to(s, lm);
            s->nq = qm;
        }
        if (it.optional) continue;
        if (can_drop) return apply_remove(s, it.node, w2) && solve(s, qpos);
        return 0;
    }
    return 1;
}

/* Repasa lo que cambió y lo que depende de ello hasta que no cambie nada más. */
static int settle(S *s) {
    for (int round = 0; round < 8; round++) {
        int before = s->nlog, qstart = s->nq;
        for (int i = 0; i < s->nn; i++) {
            Node *n = nd(s, i);
            if (!n->touched) continue;
            if (present(n) && enqueue_deps(s, i)) return 0;
            if (n->inst) {
                int lo, hi;
                refs_range(s->rdep, s->nrdep, n->name, n->nlen, &lo, &hi);
                for (int k = lo; k < hi; k++) {
                    int di = node_get(s, s->rdep[k].p->name, strlen(s->rdep[k].p->name));
                    if (di >= 0 && nd(s, di)->target == T_KEEP && enqueue_deps(s, di)) return 0;
                }
            }
        }
        if (!solve(s, qstart)) return 0;
        if (s->nlog == before) return 1;
    }
    return 1;
}

/* ---- huérfanos (como apt autoremove) ---- */

static int is_auto(const PlanOpts *o, const char *name) {
    for (int i = 0; i < o->nautos; i++) if (!strcmp(o->autos[i], name)) return 1;
    return 0;
}

/* Marca desde las raíces (lo instalado a mano, lo esencial y lo que se instala ahora) todo lo
 * que hace falta por Pre-Depends, Depends o Recommends (como apt: los recomendados cuentan); lo
 * instalado como dependencia que quedó sin marcar se quita. */
static void mark_orphans(S *s) {
    int *stack = malloc(sizeof *stack * (size_t)(s->st->n + s->nn + 16)), sp = 0;
    if (!stack) return;
    int cap = s->st->n + s->nn + 16;
    for (int i = 0; i < s->nn; i++) nd(s, i)->mark = 0;
    for (int i = 0; i < s->st->n; i++) {
        const StPkg *p = &s->st->p[i];
        if (!p->native) continue;
        int k = node_get(s, p->name, strlen(p->name));
        if (k < 0) continue;
        Node *n = nd(s, k);
        if (present(n) && (!is_auto(s->o, n->name) || !removable(n) || n->target >= 0)) { n->mark = 1; stack[sp++] = k; }
    }
    for (int i = 0; i < s->nn; i++)
        if (nd(s, i)->target >= 0 && !nd(s, i)->mark && sp < cap) { nd(s, i)->mark = 1; stack[sp++] = i; }
    while (sp > 0) {
        int k = stack[--sp];
        Node *n = nd(s, k);
        for (int f = F_DEPENDS; f <= F_RECOMMENDS; f++) {
            const char *p = field_of(s, n, n->target, f), *e = p + strlen(p);
            DebRel r[MAX_ALTS];
            int c;
            while ((c = deb_next_group(&p, e, r, MAX_ALTS)) > 0)
                for (int j = 0; j < c; j++) {
                    int who[16];
                    int w = who_satisfies(s, &r[j], -1, who, 16);
                    for (int q = 0; q < w && q < 16; q++) {
                        Node *d = nd(s, who[q]);
                        if (d->mark) continue;
                        d->mark = 1;
                        if (sp == cap) {
                            int *ns = realloc(stack, sizeof *ns * (size_t)(cap * 2));
                            if (!ns) { free(stack); return; }
                            stack = ns;
                            cap *= 2;
                        }
                        stack[sp++] = who[q];
                    }
                }
        }
    }
    free(stack);
    for (int i = 0; i < s->nn; i++) {
        Node *n = nd(s, i);
        if (n->target == T_KEEP && !n->mark && removable(n) && is_auto(s->o, n->name)) {
            n->target = T_NONE;
            n->orphan = 1;
            n->touched = 1;
        }
    }
}

/* ---- plan ---- */

static int item_cmp(const void *a, const void *b) {
    const PlanItem *x = a, *y = b;
    if (x->action != y->action) return x->action - y->action;
    return strcmp(x->name, y->name);
}

void plan_free(Plan *p) {
    free(p->items);
    memset(p, 0, sizeof *p);
}

int plan_make(const Idx *x, const Status *st, const char *const *install, int ni,
              const char *const *remove, int nr, const PlanOpts *o, Plan *out) {
    memset(out, 0, sizeof *out);
    S s;
    memset(&s, 0, sizeof s);
    s.x = x;
    s.st = st;
    s.o = o;
    s.budget = BUDGET;
    s.may_remove = 1;                   /* las quitas extra se calculan y después se confirman */
    int ok = !build_tables(&s);
    if (!ok) snprintf(s.err, sizeof s.err, "sin memoria");
    if (!x->map && (ni || o->upgrade_all)) { ok = 0; snprintf(s.err, sizeof s.err, "no hay índice de paquetes: falta actualizar la lista"); }

    for (int i = 0; ok && i < nr; i++) {
        int k = node_get(&s, remove[i], strlen(remove[i]));
        if (k < 0) { ok = 0; break; }
        Node *n = nd(&s, k);
        if (!n->inst) { ok = 0; snprintf(s.err, sizeof s.err, "%s no está instalado", remove[i]); break; }
        n->remove_req = 1;
        n->requested = 1;
        ok = apply_remove(&s, k, "pedido");
    }
    for (int i = 0; ok && i < ni; i++) {
        int k = node_get(&s, install[i], strlen(install[i]));
        if (k < 0) { ok = 0; break; }
        Node *n = nd(&s, k);
        if (n->cand < 0 && !n->inst) {
            /* ¿virtual? un único proveedor, o hay que elegir */
            DebRel r = { install[i], NULL, strlen(install[i]), 0, REL_NONE };
            Opt opt[MAX_ALTS];
            int no = gather(&s, &r, 1, opt, MAX_ALTS);
            if (no == 1) { k = opt[0].node; n = nd(&s, k); }
            else if (no > 1) {
                int l = snprintf(s.err, sizeof s.err, "%s es virtual; hay que elegir uno:", install[i]);
                for (int j = 0; j < no && l < (int)sizeof s.err - 40; j++) l += snprintf(s.err + l, sizeof s.err - (size_t)l, " %s", nd(&s, opt[j].node)->name);
                ok = 0;
                break;
            } else { ok = 0; snprintf(s.err, sizeof s.err, "no hay ningún paquete llamado %s", install[i]); break; }
        }
        n->requested = 1;
        if (n->remove_req) { ok = 0; snprintf(s.err, sizeof s.err, "%s está para instalar y para quitar a la vez", n->name); break; }
        if (n->cand < 0 || (n->inst && !ver_cmp(idx_s(x, x->pkg[n->cand].version), n->inst->version))) continue;   /* ya está */
        ok = apply_install(&s, k, n->cand, NULL);
    }
    if (ok) ok = solve(&s, 0);
    if (ok && o->upgrade_all) {
        s.may_remove = o->allow_remove || o->cascade;
        for (int i = 0; i < st->n; i++) {
            const StPkg *p = &st->p[i];
            if (!p->native) continue;
            int k = node_get(&s, p->name, strlen(p->name));
            if (k < 0) continue;
            Node *n = nd(&s, k);
            if (n->target != T_KEEP || n->cand < 0 || ver_cmp(idx_s(x, x->pkg[n->cand].version), p->version) <= 0) continue;
            int lm = s.nlog, qm = s.nq;
            s.budget = BUDGET;
            if (apply_install(&s, k, n->cand, "actualización") && solve(&s, qm)) continue;
            undo_to(&s, lm);
            s.nq = qm;
            if (out->nheld < 32) snprintf(out->held[out->nheld++], sizeof out->held[0], "%s", p->name);
        }
        s.err[0] = 0;
        s.may_remove = 1;
    }
    if (ok) ok = settle(&s);
    if (ok && o->orphans) mark_orphans(&s);

    /* resultado */
    int count = 0;
    for (int i = 0; i < s.nn; i++) {
        Node *n = nd(&s, i);
        int changed = (n->target >= 0 && (!n->inst || ver_cmp(idx_s(x, x->pkg[n->target].version), n->inst->version))) ||
                      (n->target == T_NONE && n->inst);
        if (changed) count++;
    }
    out->items = calloc((size_t)(count ? count : 1), sizeof *out->items);
    if (!out->items) { ok = 0; snprintf(s.err, sizeof s.err, "sin memoria"); }
    int extra_removals = 0;
    char extra[300] = "";
    for (int i = 0; out->items && i < s.nn; i++) {
        Node *n = nd(&s, i);
        PlanItem it;
        memset(&it, 0, sizeof it);
        it.rec = -1;
        if (n->target >= 0 && (!n->inst || ver_cmp(idx_s(x, x->pkg[n->target].version), n->inst->version))) {
            const IdxPkg *r = &x->pkg[n->target];
            it.action = n->inst ? PA_UPGRADE : PA_INSTALL;
            it.version = idx_s(x, r->version);
            it.old_version = n->inst ? n->inst->version : NULL;
            it.rec = n->target;
            out->download += r->size;
            out->isize_delta += (int64_t)r->isize - (n->inst ? (int64_t)n->inst->isize : 0);
        } else if (n->target == T_NONE && n->inst) {
            it.action = PA_REMOVE;
            it.version = n->inst->version;
            out->isize_delta -= (int64_t)n->inst->isize;
            if (!n->remove_req && !n->orphan) {
                extra_removals++;
                size_t l = strlen(extra);
                if (l < sizeof extra - 70) snprintf(extra + l, sizeof extra - l, "%s%s", l ? ", " : "", n->name);
            }
        } else continue;
        it.name = n->name;
        it.requested = n->requested;
        it.orphan = n->orphan;
        snprintf(it.reason, sizeof it.reason, "%s", n->requested ? "pedido" : n->orphan ? "auto" : n->reason);
        out->items[out->n++] = it;
    }
    if (out->items) qsort(out->items, (size_t)out->n, sizeof *out->items, item_cmp);
    if (ok && extra_removals && !o->allow_remove && !o->cascade) {
        ok = 0;
        snprintf(s.err, sizeof s.err, "también hay que quitar: %s", extra);
    }
    out->ok = ok;
    snprintf(out->err, sizeof out->err, "%s", ok ? "" : s.err[0] ? s.err : "no se pudo resolver");
    for (int i = 0; i < s.nblocks; i++) free(s.blocks[i]);
    free(s.blocks); free(s.ht); free(s.log); free(s.q); free(s.prov); free(s.rdep); free(s.conf);
    return ok ? 0 : -1;
}

/* ---- orden para dpkg ---- */

typedef struct { const Idx *x; const Plan *p; int *state, *order, n; } Topo;

static int find_item(const Topo *t, const char *name, size_t n) {
    for (int i = 0; i < t->p->n; i++) {
        const PlanItem *it = &t->p->items[i];
        if (it->rec < 0) continue;
        if (strlen(it->name) == n && !memcmp(it->name, name, n)) return i;
    }
    for (int i = 0; i < t->p->n; i++) {             /* o algo de la tanda que lo provee */
        const PlanItem *it = &t->p->items[i];
        if (it->rec < 0) continue;
        DebRel r = { name, NULL, n, 0, REL_NONE };
        if (provides_match(idx_s(t->x, t->x->pkg[it->rec].provides), &r)) return i;
    }
    return -1;
}

static void visit(Topo *t, int i) {
    if (t->state[i]) return;                /* 1: en curso (ciclo: se corta), 2: listo */
    t->state[i] = 1;
    const IdxPkg *r = &t->x->pkg[t->p->items[i].rec];
    for (int f = 0; f < 2; f++) {
        const char *s = idx_s(t->x, f ? r->depends : r->predepends), *e = s + strlen(s);
        DebRel a[MAX_ALTS];
        int k;
        while ((k = deb_next_group(&s, e, a, MAX_ALTS)) > 0)
            for (int j = 0; j < k; j++) {
                int d = find_item(t, a[j].name, a[j].nlen);
                if (d >= 0 && d != i) visit(t, d);
            }
    }
    t->state[i] = 2;
    t->order[t->n++] = i;
}

int plan_order(const Idx *x, const Plan *p, int *order, int *group) {
    int *state = calloc((size_t)(p->n ? p->n : 1), sizeof *state);
    if (!state) return -1;
    Topo t = { x, p, state, order, 0 };
    for (int i = 0; i < p->n; i++) if (p->items[i].rec >= 0) visit(&t, i);
    /* tandas: un Pre-Depends sobre algo de la tanda actual obliga a configurarla antes */
    int g = 0, start = 0;
    for (int k = 0; k < t.n; k++) {
        const IdxPkg *r = &x->pkg[p->items[order[k]].rec];
        const char *s = idx_s(x, r->predepends), *e = s + strlen(s);
        DebRel a[MAX_ALTS];
        int n, cut = 0;
        while (!cut && (n = deb_next_group(&s, e, a, MAX_ALTS)) > 0)
            for (int j = 0; j < n && !cut; j++) {
                int d = find_item(&t, a[j].name, a[j].nlen);
                for (int m = start; m < k && !cut; m++) if (order[m] == d) cut = 1;
            }
        if (cut) { g++; start = k; }
        group[k] = g;
    }
    free(state);
    return t.n;
}

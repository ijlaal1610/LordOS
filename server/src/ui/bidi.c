/* bidi.c — algoritmo bidireccional de Unicode (UAX #9, revisión de Unicode 15.1), completo:
 * incrustaciones y anulaciones explícitas (LRE/RLE/LRO/RLO/PDF), aislamientos (LRI/RLI/FSI/PDI),
 * secuencias de ejecución aisladas, reglas W1–W7, pares de paréntesis (N0), N1–N2, I1–I2 y,
 * por línea, L1–L2. Se verifica con BidiCharacterTest.txt (server/tests/bidi_test.c).
 *
 * Las tablas salen de unicode.c (tools/gen-unicode.mjs). Sin fribidi ni ICU. */
#include "bidi.h"

#include <stdlib.h>
#include <string.h>

#include "unicode.h"

#define MAX_DEPTH 125
#define MAX_BRACKETS 63

int bidi_class(uint32_t cp) { return uc_bidi(cp); }
uint32_t bidi_mirror(uint32_t cp) { return uc_mirror(cp); }

/* Par de un paréntesis normalizado por equivalencia canónica (U+2329 = U+3008). */
static uint32_t canon(uint32_t cp) { return cp == 0x2329 ? 0x3008 : cp == 0x232A ? 0x3009 : cp; }
static uint32_t bracket(uint32_t cp, int *open) { uint32_t b = uc_bracket(cp, open); return b ? canon(b) : 0; }

static int is_isolate_init(int t) { return t == BIDI_LRI || t == BIDI_RLI || t == BIDI_FSI; }
static int is_removed(int t) {
    return t == BIDI_LRE || t == BIDI_RLE || t == BIDI_LRO || t == BIDI_RLO || t == BIDI_PDF || t == BIDI_BN;
}
static int is_ni(int t) {
    return t == BIDI_B || t == BIDI_S || t == BIDI_WS || t == BIDI_ON || is_isolate_init(t) || t == BIDI_PDI;
}
/* Dirección fuerte para N0/N1: L, o R (EN y AN cuentan como R); -1 si es neutral. */
static int strong_dir(int t) {
    if (t == BIDI_L) return BIDI_L;
    if (t == BIDI_R || t == BIDI_AL || t == BIDI_EN || t == BIDI_AN) return BIDI_R;
    return -1;
}

/* P2/P3 sobre [start, end), saltando lo que está entre un aislamiento y su PDI. */
static int first_strong(const uint8_t *t, const int *match, int start, int end) {
    for (int i = start; i < end; i++) {
        if (t[i] == BIDI_L) return 0;
        if (t[i] == BIDI_R || t[i] == BIDI_AL) return 1;
        if (t[i] == BIDI_B) break;
        if (is_isolate_init(t[i])) {
            if (match[i] < 0) break;
            i = match[i];
        }
    }
    return -1;
}

/* BD9: PDI que cierra cada aislamiento (-1 si no tiene). */
static void match_isolates(const uint8_t *t, int n, int *match, int *stack) {
    int sp = 0;
    for (int i = 0; i < n; i++) {
        match[i] = -1;
        if (is_isolate_init(t[i])) stack[sp++] = i;
        else if (t[i] == BIDI_PDI && sp > 0) match[stack[--sp]] = i;
        else if (t[i] == BIDI_B) sp = 0;
    }
}

int bidi_direction(const uint32_t *cp, int n) {
    int depth = 0;
    for (int i = 0; i < n; i++) {
        int t = bidi_class(cp[i]);
        if (is_isolate_init(t)) depth++;
        else if (t == BIDI_PDI) { if (depth > 0) depth--; }
        else if (t == BIDI_B) break;
        else if (!depth && t == BIDI_L) return 0;
        else if (!depth && (t == BIDI_R || t == BIDI_AL)) return 1;
    }
    return -1;
}

typedef struct {
    const uint32_t *cp;
    const uint8_t *orig;        /* clases originales */
    uint8_t *t;                 /* clases que van cambiando */
    uint8_t *levels;
    const uint8_t *elev;        /* niveles de incrustación (X1–X8), antes de I1/I2 */
    int n, para;
} Para;

/* Resuelve una secuencia de ejecución aislada (W1–I2). idx: sus caracteres en orden. */
static void resolve_sequence(Para *p, const int *idx, int m, uint8_t *t, uint8_t *pre) {
    int level = p->elev[idx[0]];
    /* sos / eos */
    int before = p->para;
    for (int i = idx[0] - 1; i >= 0; i--) if (!is_removed(p->orig[i])) { before = p->elev[i]; break; }
    int after = p->para, last = idx[m - 1];
    if (!is_isolate_init(p->orig[last]))
        for (int i = last + 1; i < p->n; i++) if (!is_removed(p->orig[i])) { after = p->elev[i]; break; }
    int sos = ((before > level ? before : level) & 1) ? BIDI_R : BIDI_L;
    int eos = ((after > level ? after : level) & 1) ? BIDI_R : BIDI_L;

    for (int k = 0; k < m; k++) pre[k] = t[k] = p->t[idx[k]];

    /* W1: NSM toma la clase del anterior (ON después de un aislamiento o PDI). */
    for (int k = 0; k < m; k++)
        if (t[k] == BIDI_NSM) t[k] = k == 0 ? (uint8_t)sos : is_isolate_init(t[k - 1]) || t[k - 1] == BIDI_PDI ? BIDI_ON : t[k - 1];
    /* W2: EN después de AL → AN.  W3: AL → R. */
    int last_strong = sos;
    for (int k = 0; k < m; k++) {
        if (t[k] == BIDI_L || t[k] == BIDI_R || t[k] == BIDI_AL) last_strong = t[k];
        else if (t[k] == BIDI_EN && last_strong == BIDI_AL) t[k] = BIDI_AN;
    }
    for (int k = 0; k < m; k++) if (t[k] == BIDI_AL) t[k] = BIDI_R;
    /* W4: un ES entre EN, un CS entre EN o entre AN. */
    for (int k = 1; k + 1 < m; k++) {
        if (t[k] == BIDI_ES && t[k - 1] == BIDI_EN && t[k + 1] == BIDI_EN) t[k] = BIDI_EN;
        else if (t[k] == BIDI_CS && t[k - 1] == BIDI_EN && t[k + 1] == BIDI_EN) t[k] = BIDI_EN;
        else if (t[k] == BIDI_CS && t[k - 1] == BIDI_AN && t[k + 1] == BIDI_AN) t[k] = BIDI_AN;
    }
    /* W5: ET junto a EN → EN. */
    for (int k = 0; k < m; k++) {
        if (t[k] != BIDI_ET) continue;
        int e = k;
        while (e < m && t[e] == BIDI_ET) e++;
        if ((k > 0 && t[k - 1] == BIDI_EN) || (e < m && t[e] == BIDI_EN))
            for (int j = k; j < e; j++) t[j] = BIDI_EN;
        k = e - 1;
    }
    /* W6: los separadores que quedan → ON.  W7: EN después de L → L. */
    for (int k = 0; k < m; k++) if (t[k] == BIDI_ES || t[k] == BIDI_ET || t[k] == BIDI_CS) t[k] = BIDI_ON;
    last_strong = sos;
    for (int k = 0; k < m; k++) {
        if (t[k] == BIDI_L || t[k] == BIDI_R) last_strong = t[k];
        else if (t[k] == BIDI_EN && last_strong == BIDI_L) t[k] = BIDI_L;
    }

    /* N0: pares de paréntesis (BD16). */
    int e_dir = (level & 1) ? BIDI_R : BIDI_L;
    int stack_pos[MAX_BRACKETS];
    uint32_t stack_cp[MAX_BRACKETS];
    int sp = 0, npairs = 0, cap = 0;
    int *pairs = NULL;              /* [abre, cierra] por par */
    for (int k = 0; k < m; k++) {
        if (t[k] != BIDI_ON) continue;
        int open;
        uint32_t other = bracket(p->cp[idx[k]], &open);
        if (!other) continue;
        if (open) {
            if (sp == MAX_BRACKETS) { npairs = 0; break; }
            stack_pos[sp] = k;
            stack_cp[sp++] = other;
        } else {
            uint32_t me = canon(p->cp[idx[k]]);
            for (int s = sp - 1; s >= 0; s--) {
                if (stack_cp[s] != me) continue;
                if (npairs == cap) {
                    cap = cap ? cap * 2 : 16;
                    int *np = realloc(pairs, sizeof(int) * 2 * (size_t)cap);
                    if (!np) { npairs = 0; sp = 0; break; }
                    pairs = np;
                }
                pairs[2 * npairs] = stack_pos[s];
                pairs[2 * npairs + 1] = k;
                npairs++;
                sp = s;
                break;
            }
        }
    }
    /* ordenar por posición de apertura (inserción: suelen ser pocos) */
    for (int a = 1; a < npairs; a++) {
        int o = pairs[2 * a], c = pairs[2 * a + 1], b = a - 1;
        while (b >= 0 && pairs[2 * b] > o) { pairs[2 * b + 2] = pairs[2 * b]; pairs[2 * b + 3] = pairs[2 * b + 1]; b--; }
        pairs[2 * b + 2] = o;
        pairs[2 * b + 3] = c;
    }
    for (int a = 0; a < npairs; a++) {
        int o = pairs[2 * a], c = pairs[2 * a + 1];
        int found_e = 0, found_opp = 0;
        for (int k = o + 1; k < c; k++) {
            int d = strong_dir(t[k]);
            if (d == e_dir) { found_e = 1; break; }
            if (d >= 0) found_opp = 1;
        }
        int set = -1;
        if (found_e) set = e_dir;
        else if (found_opp) {
            int ctx = sos;
            for (int k = o - 1; k >= 0; k--) { int d = strong_dir(t[k]); if (d >= 0) { ctx = d; break; } }
            set = ctx != e_dir ? ctx : e_dir;
        }
        if (set < 0) continue;
        t[o] = t[c] = (uint8_t)set;
        for (int k = o + 1; k < m && pre[k] == BIDI_NSM; k++) t[k] = (uint8_t)set;
        for (int k = c + 1; k < m && pre[k] == BIDI_NSM; k++) t[k] = (uint8_t)set;
    }
    free(pairs);

    /* N1/N2: neutrales entre dos fuertes iguales toman esa dirección; si no, la del nivel. */
    for (int k = 0; k < m; k++) {
        if (!is_ni(t[k])) continue;
        int e = k;
        while (e < m && is_ni(t[e])) e++;
        int lead = k == 0 ? sos : strong_dir(t[k - 1]);
        int trail = e == m ? eos : strong_dir(t[e]);
        int d = lead == trail && lead >= 0 ? lead : e_dir;
        for (int j = k; j < e; j++) t[j] = (uint8_t)d;
        k = e - 1;
    }

    /* I1/I2 */
    for (int k = 0; k < m; k++) {
        int lv = level;
        if (!(lv & 1)) { if (t[k] == BIDI_R) lv += 1; else if (t[k] == BIDI_AN || t[k] == BIDI_EN) lv += 2; }
        else if (t[k] == BIDI_L || t[k] == BIDI_EN || t[k] == BIDI_AN) lv += 1;
        p->levels[idx[k]] = (uint8_t)lv;
    }
}

int bidi_levels(const uint32_t *cp, int n, int para, uint8_t *levels, uint8_t *removed) {
    if (n <= 0) return para > 0 ? 1 : 0;
    /* un solo bloque: clases ×3, coincidencias, pila, índices de secuencia, ejecuciones */
    size_t ints = (size_t)n * 5;
    uint8_t *bytes = malloc((size_t)n * 5);
    int *mem = malloc(sizeof(int) * ints);
    if (!bytes || !mem) {
        free(bytes); free(mem);
        int lv = para > 0 ? 1 : 0;
        memset(levels, lv, (size_t)n);
        if (removed) memset(removed, 0, (size_t)n);
        return lv;
    }
    uint8_t *orig = bytes, *t = bytes + n, *seq_t = bytes + 2 * n, *seq_pre = bytes + 3 * n, *elev = bytes + 4 * n;
    int *match = mem, *stack = mem + n, *idx = mem + 2 * n, *run_start = mem + 3 * n, *run_of = mem + 4 * n;

    for (int i = 0; i < n; i++) orig[i] = t[i] = (uint8_t)bidi_class(cp[i]);
    match_isolates(orig, n, match, stack);
    if (para < 0) para = first_strong(orig, match, 0, n) == 1 ? 1 : 0;

    /* X1–X8: pila de niveles explícitos */
    struct { uint8_t level, override, isolate; } st[MAX_DEPTH + 2];
    int sp = 0, ovf_iso = 0, ovf_emb = 0, valid_iso = 0;
    st[sp].level = (uint8_t)para; st[sp].override = 0; st[sp].isolate = 0; sp++;
    for (int i = 0; i < n; i++) {
        int c = orig[i];
        int top = st[sp - 1].level;
        switch (c) {
        case BIDI_RLE: case BIDI_LRE: case BIDI_RLO: case BIDI_LRO: {
            int nl = (c == BIDI_RLE || c == BIDI_RLO) ? (top + 1) | 1 : (top + 2) & ~1;
            levels[i] = (uint8_t)top;
            if (nl <= MAX_DEPTH && !ovf_iso && !ovf_emb) {
                st[sp].level = (uint8_t)nl;
                st[sp].override = c == BIDI_LRO ? 1 : c == BIDI_RLO ? 2 : 0;   /* 1 → L, 2 → R */
                st[sp].isolate = 0;
                sp++;
            } else if (!ovf_iso) ovf_emb++;
            break;
        }
        case BIDI_RLI: case BIDI_LRI: case BIDI_FSI: {
            levels[i] = (uint8_t)top;
            if (st[sp - 1].override) t[i] = st[sp - 1].override == 1 ? BIDI_L : BIDI_R;
            int rtl = c == BIDI_RLI || (c == BIDI_FSI && first_strong(orig, match, i + 1, match[i] >= 0 ? match[i] : n) == 1);
            int nl = rtl ? (top + 1) | 1 : (top + 2) & ~1;
            if (nl <= MAX_DEPTH && !ovf_iso && !ovf_emb) {
                valid_iso++;
                st[sp].level = (uint8_t)nl; st[sp].override = 0; st[sp].isolate = 1;
                sp++;
            } else ovf_iso++;
            break;
        }
        case BIDI_PDI:
            if (ovf_iso > 0) ovf_iso--;
            else if (valid_iso > 0) {
                ovf_emb = 0;
                while (!st[sp - 1].isolate) sp--;
                sp--;
                valid_iso--;
            }
            levels[i] = st[sp - 1].level;
            if (st[sp - 1].override) t[i] = st[sp - 1].override == 1 ? BIDI_L : BIDI_R;
            break;
        case BIDI_PDF:
            if (ovf_iso > 0) {}
            else if (ovf_emb > 0) ovf_emb--;
            else if (!st[sp - 1].isolate && sp >= 2) sp--;
            levels[i] = (uint8_t)top;
            break;
        case BIDI_B:
            levels[i] = (uint8_t)para;
            break;
        case BIDI_BN:
            levels[i] = (uint8_t)top;
            break;
        default:
            levels[i] = (uint8_t)top;
            if (st[sp - 1].override) t[i] = st[sp - 1].override == 1 ? BIDI_L : BIDI_R;
            break;
        }
    }

    /* X9/X10: ejecuciones de nivel (sin los caracteres quitados) y secuencias aisladas */
    int nruns = 0, prev = -1;
    for (int i = 0; i < n; i++) {
        run_of[i] = -1;
        if (is_removed(orig[i])) continue;
        if (prev < 0 || levels[i] != levels[prev]) run_start[nruns++] = i;
        run_of[i] = nruns - 1;
        prev = i;
    }
    memcpy(elev, levels, (size_t)n);
    Para p = { cp, orig, t, levels, elev, n, para };
    for (int r = 0; r < nruns; r++) {
        int first = run_start[r];
        /* las que empiezan con un PDI que cierra un aislamiento siguen a otra secuencia */
        if (orig[first] == BIDI_PDI) {
            int matched = 0;
            for (int j = first - 1; j >= 0 && !matched; j--) if (match[j] == first) matched = 1;
            if (matched) continue;
        }
        int m = 0, run = r;
        for (;;) {
            int lastc = -1;
            for (int i = run_start[run]; i < n; i++) {
                if (run_of[i] < 0) continue;
                if (run_of[i] != run) break;
                idx[m++] = i;
                lastc = i;
            }
            if (lastc < 0 || !is_isolate_init(orig[lastc]) || match[lastc] < 0) break;
            int next = run_of[match[lastc]];
            if (next < 0) break;
            run = next;
        }
        if (m) resolve_sequence(&p, idx, m, seq_t, seq_pre);
    }

    /* los quitados toman el nivel del anterior (para dibujar y armar formas) */
    for (int i = 0; i < n; i++) {
        int rm = is_removed(orig[i]);
        if (removed) removed[i] = (uint8_t)rm;
        if (rm) levels[i] = i > 0 ? levels[i - 1] : (uint8_t)para;
    }
    free(bytes);
    free(mem);
    return para;
}

void bidi_line(const uint32_t *cp, uint8_t *levels, int start, int end, int para, int *order) {
    /* L1: separadores, y los espacios/aislamientos antes de ellos o al final, al nivel del párrafo */
    int trailing = 1;
    for (int i = end - 1; i >= start; i--) {
        int c = bidi_class(cp[i]);
        if (c == BIDI_S || c == BIDI_B) { levels[i] = (uint8_t)para; trailing = 1; continue; }
        if (trailing && (c == BIDI_WS || is_isolate_init(c) || c == BIDI_PDI || is_removed(c))) { levels[i] = (uint8_t)para; continue; }
        trailing = 0;
    }
    /* L2: invertir desde el nivel más alto hasta el impar más bajo */
    int n = end - start, hi = 0, lo_odd = 255;
    for (int k = 0; k < n; k++) {
        order[k] = start + k;
        int lv = levels[start + k];
        if (lv > hi) hi = lv;
        if ((lv & 1) && lv < lo_odd) lo_odd = lv;
    }
    for (int lv = hi; lv >= lo_odd && lv > 0; lv--) {
        for (int k = 0; k < n; k++) {
            if (levels[order[k]] < lv) continue;
            int e = k;
            while (e < n && levels[order[e]] >= lv) e++;
            for (int a = k, b = e - 1; a < b; a++, b--) { int tmp = order[a]; order[a] = order[b]; order[b] = tmp; }
            k = e;
        }
    }
}

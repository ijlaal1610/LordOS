/* textlayout.c — texto de la interfaz con escrituras complejas: árabe y hebreo (derecha a
 * izquierda, formas contextuales, marcas), CJK (sin espacios entre palabras), emoji y
 * cualquier escritura con una fuente Noto instalada.
 *
 * Por cada texto: párrafos (\n) → bidi (bidi.c) → tramos de una fuente, escritura y nivel →
 * armado (shape.c) → cortes de línea (espacios y, en CJK, entre caracteres, respetando
 * 「」、。) → orden visual por línea (L1–L2). El resultado se guarda en una caché chica: la
 * misma etiqueta se mide en el layout y se dibuja en cada repintado.
 *
 * El texto latino, griego y cirílico que Inter cubre no pasa por acá (text.c, camino rápido).
 * La dirección del párrafo es la del primer carácter fuerte (como "natural" en macOS); la
 * alineación inicio/fin la resuelve paint.c con la dirección del nodo. */
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bidi.h"
#include "fonts.h"
#include "shape.h"
#include "thbrk.h"
#include "unicode.h"

/* ---- fuentes de respaldo ---- */

static char lang[16] = "es";
static int script_fonts[SCRIPT_COUNT][2];
static int generic[2][8], ngeneric[2] = { -1, -1 };
static int color_font = -2, inited;

static void init_fonts(void) {
    if (inited) return;
    inited = 1;
    for (int s = 0; s < SCRIPT_COUNT; s++) script_fonts[s][0] = script_fonts[s][1] = -2;
}

static int load_family(const char *fam, int bold) {
    const FontEntry *e = fonts_pick(fam, bold, 0);
    return e ? text_font_load(e->path, e->index) : -1;
}

static int is_cjk(int sc) { return sc == SCRIPT_HANI || sc == SCRIPT_HIRA || sc == SCRIPT_KANA || sc == SCRIPT_HANG; }
/* Puntuación y formas de ancho completo (escritura "Common") que se ven bien con la CJK. */
static int cjk_common(uint32_t cp) {
    return (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0x3200 && cp <= 0x33FF) || (cp >= 0xFE30 && cp <= 0xFE4F) ||
           (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x30FB && cp <= 0x30FC);
}

/* El mismo ideograma se dibuja distinto en chino, japonés y coreano: la fuente CJK sale del
 * idioma de la interfaz. mono: la variante monoespaciada (terminal). */
static int cjk_font(int bold, int mono) {
    const char *pref = !strncmp(lang, "ja", 2) ? "JP" : !strncmp(lang, "ko", 2) ? "KR" :
                       (!strncmp(lang, "zh-TW", 5) || !strncmp(lang, "zh_TW", 5) || !strncmp(lang, "zh-HK", 5) || !strncmp(lang, "zh-Hant", 7)) ? "TC" : "SC";
    const char *order[] = { pref, "SC", "JP", "KR", "TC" };
    char fam[48];
    for (int i = 0; i < 5; i++) {
        snprintf(fam, sizeof fam, mono ? "Noto Sans Mono CJK %s" : "Noto Sans CJK %s", order[i]);
        int f = load_family(fam, bold);
        if (f >= 0) return f;
    }
    for (int i = 0; mono && i < 5; i++) {
        snprintf(fam, sizeof fam, "Noto Sans CJK %s", order[i]);
        int f = load_family(fam, bold);
        if (f >= 0) return f;
    }
    static const char *const other[] = { "Droid Sans Fallback", "WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "Source Han Sans" };
    for (size_t i = 0; i < sizeof other / sizeof *other; i++) { int f = load_family(other[i], bold); if (f >= 0) return f; }
    return -1;
}

static int script_font(int sc, int bold) {
    init_fonts();
    if (sc < 0 || sc >= SCRIPT_COUNT) return -1;
    int *slot = &script_fonts[sc][bold];
    if (*slot != -2) return *slot;
    int f = -1;
    if (is_cjk(sc)) f = cjk_font(bold, 0);
    else if (sc == SCRIPT_ARAB) {
        static const char *const fams[] = { "Noto Sans Arabic", "Noto Naskh Arabic", "Noto Kufi Arabic", "Amiri", "KacstOne", "DejaVu Sans" };
        for (size_t i = 0; i < sizeof fams / sizeof *fams && f < 0; i++) f = load_family(fams[i], bold);
    } else if (sc == SCRIPT_HEBR) {
        static const char *const fams[] = { "Noto Sans Hebrew", "Noto Serif Hebrew", "Noto Rashi Hebrew", "DejaVu Sans" };
        for (size_t i = 0; i < sizeof fams / sizeof *fams && f < 0; i++) f = load_family(fams[i], bold);
    } else if (sc > SCRIPT_INHERITED) {
        char fam[96];
        snprintf(fam, sizeof fam, "Noto Sans %s", uc_script_name(sc));
        f = load_family(fam, bold);
        if (f < 0) { snprintf(fam, sizeof fam, "Noto Serif %s", uc_script_name(sc)); f = load_family(fam, bold); }   /* tibetano */
        if (f < 0) {                    /* "Phags Pa" → "Noto Sans PhagsPa" */
            int k = snprintf(fam, sizeof fam, "Noto Sans ");
            for (const char *q = uc_script_name(sc); *q && k < (int)sizeof fam - 1; q++) if (*q != ' ') fam[k++] = *q;
            fam[k] = 0;
            f = load_family(fam, bold);
        }
    }
    *slot = f;
    return f;
}

static int generic_font(int k, int bold) {
    if (ngeneric[bold] < 0) {
        ngeneric[bold] = 0;
        static const char *const fams[] = { "Noto Sans", "DejaVu Sans", "Noto Sans Symbols", "Noto Sans Symbols 2", "Noto Sans Math", "FreeSerif" };
        for (size_t i = 0; i < sizeof fams / sizeof *fams; i++) {
            int f = load_family(fams[i], bold);
            if (f >= 0) generic[bold][ngeneric[bold]++] = f;
        }
    }
    return k < ngeneric[bold] ? generic[bold][k] : -1;
}

static int emoji_font(void) {
    if (color_font == -2) {
        const FontEntry *e = fonts_color();
        color_font = e ? text_font_load(e->path, e->index) : -1;
    }
    return color_font;
}

static int mono_cjk[2] = { -2, -2 };

void text_set_language(const char *l) {
    if (!l || !strcmp(l, lang)) return;
    snprintf(lang, sizeof lang, "%s", l);
    init_fonts();
    int cjk[] = { SCRIPT_HANI, SCRIPT_HIRA, SCRIPT_KANA, SCRIPT_HANG };
    for (int i = 0; i < 4; i++) script_fonts[cjk[i]][0] = script_fonts[cjk[i]][1] = -2;
    mono_cjk[0] = mono_cjk[1] = -2;
    text_layout_flush();
}

/* Para la terminal: fuente de la escritura del carácter (CJK monoespaciada, árabe, hebreo,
 * Noto de cualquier otra escritura instalada), o -1. */
int text_script_fallback(uint32_t cp, int bold) {
    int sc = cjk_common(cp) ? SCRIPT_HANI : uc_script(cp);
    int f;
    if (is_cjk(sc)) {
        if (mono_cjk[bold] == -2) mono_cjk[bold] = cjk_font(bold, 1);
        f = mono_cjk[bold];
    } else f = script_font(sc, bold);
    if (f >= 0 && text_font_has(f, cp)) return f;
    for (int k = 0; (f = generic_font(k, bold)) >= 0; k++) if (text_font_has(f, cp)) return f;
    return -1;
}

static int emoji_presentation(uint32_t cp, uint32_t next) {
    return next == 0xFE0F || (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x1F1E6 && cp <= 0x1F1FF);
}

/* Fuente para un carácter: la de la interfaz, la de su escritura, las genéricas y la de emoji. */
static int font_for_cp(uint32_t cp, uint32_t next, int sc, int primary, int bold) {
    if (emoji_presentation(cp, next)) { int e = emoji_font(); if (e >= 0 && text_font_has(e, cp)) return e; }
    if (text_font_has(primary, cp)) return primary;
    int f = script_font(cjk_common(cp) ? SCRIPT_HANI : sc, bold);
    if (f >= 0 && text_font_has(f, cp)) return f;
    for (int k = 0; (f = generic_font(k, bold)) >= 0; k++) if (text_font_has(f, cp)) return f;
    int e = emoji_font();
    if (e >= 0 && text_font_has(e, cp)) return e;
    return primary;
}

/* ---- ¿hace falta el camino complejo? ---- */

static uint32_t utf8_decode(const unsigned char **p) {
    const unsigned char *s = *p;
    uint32_t c = s[0];
    int n = 0;
    if (c < 0x80) n = 0;
    else if ((c & 0xe0) == 0xc0) { c &= 0x1f; n = 1; }
    else if ((c & 0xf0) == 0xe0) { c &= 0x0f; n = 2; }
    else if ((c & 0xf8) == 0xf0) { c &= 0x07; n = 3; }
    else { *p += 1; return 0xfffd; }
    for (int i = 1; i <= n; i++) {
        if ((s[i] & 0xc0) != 0x80) { *p += i; return 0xfffd; }
        c = (c << 6) | (s[i] & 0x3f);
    }
    *p += n + 1;
    return c;
}

int text_needs_layout(const char *s, int font) {
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        if (*p < 0x80) { p++; continue; }
        uint32_t cp = utf8_decode(&p);
        if (cp >= 0x0590 || uc_ccc(cp) || uc_ignorable(cp) || !text_font_has(font, cp)) return 1;
    }
    return 0;
}

/* ---- layout ---- */

typedef struct { uint16_t gid, font; int cp; float adv, dx, dy; int att; } LGlyph;
typedef struct { int start, end, vis0, nvis; float width; } LLine;

typedef struct {
    char *text;
    uint64_t hash;
    int font, size_q, maxw_q, nowrap;
    unsigned used;
    int ncp, ng, nvis, nlines;
    uint32_t *cp;
    uint8_t *lv;
    float *cpw;
    LGlyph *g;
    int *vis;
    LLine *lines;
    float width;
} Layout;

#define CACHE 48
static Layout cache[CACHE];
static unsigned tick;
static Shaper shaper;

static void layout_free(Layout *L) {
    free(L->text); free(L->cp); free(L->lv); free(L->cpw); free(L->g); free(L->vis); free(L->lines);
    memset(L, 0, sizeof *L);
}

void text_layout_flush(void) { for (int i = 0; i < CACHE; i++) layout_free(&cache[i]); }

static int is_space(uint32_t cp) { return cp == ' ' || cp == 0x3000 || cp == '\t'; }

/* No se corta antes de marcas, unidores ni selectores de variante. */
static int sticks_to_prev(uint32_t cp) {
    return uc_ccc(cp) || uc_ignorable(cp) || uc_bidi(cp) == BIDI_NSM || uc_join(cp) == JOIN_T || (cp >= 0x1F3FB && cp <= 0x1F3FF);
}

static int can_break_before(const uint32_t *cp, int i) {
    uint32_t a = cp[i - 1], b = cp[i];
    if (sticks_to_prev(b) || is_space(b)) return 0;
    if (is_space(a)) return 1;
    int la = uc_linebreak(a), lb = uc_linebreak(b);
    if (lb == LB_CL || lb == LB_NS || lb == LB_EX || lb == LB_IS) return 0;
    if (la == LB_OP) return 0;
    return la == LB_ID || lb == LB_ID;
}

static int grow(void **p, size_t elem, int *cap, int need) {
    if (need <= *cap) return 0;
    int nc = need + need / 2 + 16;
    void *np = realloc(*p, elem * (size_t)nc);
    if (!np) return -1;
    *p = np;
    *cap = nc;
    return 0;
}

/* Arma un tramo [rs, re) y agrega sus glifos. */
static int shape_span(Layout *L, int *gcap, int rs, int re, int font, int sc, int rtl, float size) {
    Otf *o = text_font_otf(font);
    if (!o) return 0;
    const char *tag = uc_script_tag(sc);
    int n = shape_run(&shaper, o, L->cp + rs, re - rs, tag, rtl);
    if (grow((void **)&L->g, sizeof *L->g, gcap, L->ng + n)) return -1;
    float scale = text_font_scale(font, size);
    int base = L->ng;
    for (int k = 0; k < n; k++) {
        const OtfPos *p = &shaper.pos[k];
        LGlyph *g = &L->g[base + k];
        g->gid = shaper.run.g[k];
        g->font = (uint16_t)font;
        g->cp = rs + shaper.run.cl[k];
        g->adv = p->adv * scale;
        g->dx = p->dx * scale;
        g->dy = p->dy * scale;
        g->att = p->att >= 0 ? base + p->att : -1;
    }
    if (!shaper.gpos_kern && !rtl && !text_font_is_color(font))
        for (int k = 1; k < n; k++)
            if (L->g[base + k].att < 0) L->g[base + k - 1].adv += text_font_kern_glyphs(font, L->g[base + k - 1].gid, L->g[base + k].gid) * scale;
    L->ng += n;
    return 0;
}

/* Orden visual de una línea: L1–L2 sobre los caracteres y, por cada uno, sus glifos (al revés
 * en los tramos RTL). */
static int line_visual(Layout *L, LLine *ln, int para, const int *gstart, const int *glist, int *vcap,
                       uint8_t **lvbuf, int **obuf, int *bcap) {
    int len = ln->end - ln->start;
    ln->vis0 = L->nvis;
    ln->nvis = 0;
    if (len <= 0) return 0;
    if (grow((void **)lvbuf, 1, bcap, len) < 0) return -1;
    int ocap = len;
    int *ord = realloc(*obuf, sizeof(int) * (size_t)ocap);
    if (!ord) return -1;
    *obuf = ord;
    memcpy(*lvbuf, L->lv + ln->start, (size_t)len);
    bidi_line(L->cp + ln->start, *lvbuf, 0, len, para, ord);
    for (int k = 0; k < len; k++) {
        int c = ln->start + ord[k];
        int a = gstart[c], b = gstart[c + 1];
        if (grow((void **)&L->vis, sizeof *L->vis, vcap, L->nvis + (b - a))) return -1;
        int rtl = (*lvbuf)[ord[k]] & 1;
        for (int j = 0; j < b - a; j++) L->vis[L->nvis++] = glist[rtl ? b - 1 - j : a + j];
    }
    ln->nvis = L->nvis - ln->vis0;
    return 0;
}

static int build(Layout *L, const char *s, int font, float size, float max_w, int nowrap) {
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p;) { utf8_decode(&p); n++; }
    L->ncp = n;
    L->cp = malloc(sizeof *L->cp * (size_t)(n + 1));
    L->lv = malloc((size_t)n + 1);
    L->cpw = calloc((size_t)n + 1, sizeof *L->cpw);
    int *fnt = malloc(sizeof(int) * (size_t)(n + 1)), *scr = malloc(sizeof(int) * (size_t)(n + 1));
    int *paras = malloc(sizeof(int) * (size_t)(n + 1));     /* nivel del párrafo de cada carácter */
    int *gstart = NULL, *glist = NULL, *obuf = NULL;
    uint8_t *lvbuf = NULL, *bk = NULL;
    int gcap = 0, lcap = 0, vcap = 0, bcap = 0, ok = -1;
    if (!L->cp || !L->lv || !L->cpw || !fnt || !scr || !paras) goto out;
    n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p;) L->cp[n++] = utf8_decode(&p);
    int bold = text_ui_font_bold(font);

    /* 1. por párrafo: bidi, escritura, fuente, tramos y armado */
    for (int ps = 0; ps <= n;) {
        int pe = ps;
        while (pe < n && L->cp[pe] != '\n') pe++;
        int len = pe - ps, para = 0;
        if (len > 0) {
            para = bidi_levels(L->cp + ps, len, -1, L->lv + ps, NULL);
            int last = SCRIPT_COMMON;
            for (int i = ps; i < pe; i++) {         /* las comunes heredan la escritura vecina */
                int sc = uc_script(L->cp[i]);
                if (sc <= SCRIPT_INHERITED) sc = last; else last = sc;
                scr[i] = sc;
            }
            for (int i = pe - 1, next = SCRIPT_COMMON; i >= ps; i--) {
                if (scr[i] <= SCRIPT_INHERITED) scr[i] = next; else next = scr[i];
            }
            for (int i = ps; i < pe; i++) {
                uint32_t c = L->cp[i], nx = i + 1 < pe ? L->cp[i + 1] : 0;
                int common = uc_script(c) <= SCRIPT_INHERITED;
                if (i > ps && sticks_to_prev(c)) fnt[i] = fnt[i - 1];     /* marcas y unidores con su base */
                else if (i > ps && common && !emoji_presentation(c, nx) && !text_font_is_color(fnt[i - 1]) && text_font_has(fnt[i - 1], c)) fnt[i] = fnt[i - 1];
                else fnt[i] = font_for_cp(c, nx, scr[i], font, bold);
            }
            for (int rs = ps; rs < pe;) {
                int re = rs + 1;
                while (re < pe && fnt[re] == fnt[rs] && scr[re] == scr[rs] && L->lv[re] == L->lv[rs]) re++;
                if (shape_span(L, &gcap, rs, re, fnt[rs], scr[rs], L->lv[rs] & 1, size)) goto out;
                rs = re;
            }
        }
        for (int i = ps; i <= pe && i <= n; i++) paras[i] = para;
        if (pe < n) L->lv[pe] = (uint8_t)para;          /* el \n */
        if (pe >= n) break;
        ps = pe + 1;
    }

    /* 2. ancho de cada carácter y glifos agrupados por carácter */
    gstart = calloc((size_t)n + 2, sizeof *gstart);
    glist = malloc(sizeof *glist * (size_t)(L->ng + 1));
    if (!gstart || !glist) goto out;
    for (int k = 0; k < L->ng; k++) {
        int c = L->g[k].cp;
        if (c < 0 || c >= n) continue;
        L->cpw[c] += L->g[k].att >= 0 ? 0 : L->g[k].adv;
        gstart[c + 1]++;
    }
    for (int c = 0; c < n; c++) gstart[c + 1] += gstart[c];
    {
        int *fill = calloc((size_t)n + 1, sizeof *fill);
        if (!fill) goto out;
        for (int k = 0; k < L->ng; k++) {
            int c = L->g[k].cp;
            if (c >= 0 && c < n) glist[gstart[c] + fill[c]++] = k;
        }
        free(fill);
    }

    /* 3. cortes posibles de las escrituras sin espacios (SA): el tailandés por palabras con el
     * diccionario de libthai-data (thbrk.c); el jemer y el birmano por sílaba (cada sílaba es
     * un grupo de glifos). bk: 0 = la regla común, 1 = se puede, 2 = no. */
    bk = calloc((size_t)n + 1, 1);
    if (!bk) goto out;
    thbrk_text(L->cp, n, bk);
    for (int i = 0; i < n;) {
        int sc = uc_script(L->cp[i]);
        if (uc_linebreak(L->cp[i]) != LB_SA || (sc != SCRIPT_KHMR && sc != SCRIPT_MYMR)) { i++; continue; }
        int e = i + 1;
        while (e < n && ((uc_linebreak(L->cp[e]) == LB_SA && uc_script(L->cp[e]) == sc) || sticks_to_prev(L->cp[e]))) e++;
        /* entre sílabas (el primer carácter de cada una tiene los glifos), salvo antes de una
         * consonante final: con asat o virama en birmano (င်), con bantoc o viriam en jemer (ញ់) */
        for (int k = i + 1; k < e; k++) {
            uint32_t nx = k + 1 < n ? L->cp[k + 1] : 0;
            int fin = nx == 0x103A || nx == 0x1039 || nx == 0x17CB || nx == 0x17D1;
            bk[k] = gstart[k + 1] > gstart[k] && !sticks_to_prev(L->cp[k]) && !fin ? 1 : 2;
        }
        i = e;
    }

    /* 4. líneas: cortes y orden visual */
    for (int ps = 0; ps <= n;) {
        int pe = ps;
        while (pe < n && L->cp[pe] != '\n') pe++;
        for (int ls = ps;;) {
            float w = 0;
            int brk = -1, i = ls;
            for (; i < pe; i++) {
                if (i > ls && (bk[i] == 1 || (!bk[i] && can_break_before(L->cp, i)))) brk = i;
                if (!nowrap && max_w > 0 && i > ls && w + L->cpw[i] > max_w + 0.5f && !is_space(L->cp[i])) break;
                w += L->cpw[i];
            }
            int wrapped = i < pe;
            int le = wrapped ? (brk > ls ? brk : i) : pe;
            /* sin dónde cortar: por lo menos no partir un grupo de glifos (sílaba, marcas) */
            if (wrapped && brk <= ls) {
                int k = le;
                while (k > ls + 1 && (gstart[k + 1] == gstart[k] || sticks_to_prev(L->cp[k]))) k--;
                if (k > ls + 1 || (gstart[k + 1] > gstart[k] && !sticks_to_prev(L->cp[k]))) le = k;
            }
            int te = le;
            while (wrapped && te > ls && is_space(L->cp[te - 1])) te--;
            if (grow((void **)&L->lines, sizeof *L->lines, &lcap, L->nlines + 1)) goto out;
            LLine *ln = &L->lines[L->nlines++];
            ln->start = ls;
            ln->end = te;
            ln->width = 0;
            for (int k = ls; k < te; k++) ln->width += L->cpw[k];
            if (ln->width > L->width) L->width = ln->width;
            if (line_visual(L, ln, paras[ls < n ? ls : n], gstart, glist, &vcap, &lvbuf, &obuf, &bcap)) goto out;
            ls = le;
            while (wrapped && ls < pe && is_space(L->cp[ls])) ls++;
            if (!wrapped || ls >= pe) break;
        }
        if (pe >= n) break;
        ps = pe + 1;
    }
    ok = 0;
out:
    free(fnt); free(scr); free(paras); free(gstart); free(glist); free(obuf); free(lvbuf); free(bk);
    return ok;
}

static uint64_t fnv(const char *s) {
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ull;
    return h;
}

static Layout *get(const char *s, int font, float size, float max_w, int nowrap) {
    uint64_t h = fnv(s);
    int sq = (int)(size * 2 + 0.5f), mq = max_w > 0 && !nowrap ? (int)(max_w * 2 + 0.5f) : 0;
    Layout *lru = &cache[0];
    for (int i = 0; i < CACHE; i++) {
        Layout *L = &cache[i];
        if (L->text && L->hash == h && L->font == font && L->size_q == sq && L->maxw_q == mq && L->nowrap == nowrap && !strcmp(L->text, s)) {
            L->used = ++tick;
            return L;
        }
        if (L->used < lru->used) lru = L;
    }
    layout_free(lru);
    lru->text = strdup(s);
    lru->hash = h;
    lru->font = font;
    lru->size_q = sq;
    lru->maxw_q = mq;
    lru->nowrap = nowrap;
    lru->used = ++tick;
    if (!lru->text || build(lru, s, font, size, max_w, nowrap)) { layout_free(lru); return NULL; }
    return lru;
}

void text_layout_measure(const char *s, int font, float size, float line_h, float max_w, int nowrap, float *w, float *h) {
    Layout *L = get(s, font, size, max_w, nowrap);
    *w = L ? ceilf(L->width) : 0;
    *h = (L ? L->nlines : 1) * text_line_height(size, line_h);
}

void text_layout_draw(Canvas *c, const char *s, int font, float size, float line_h,
                      float x, float y, float box_w, int align, int nowrap, uint32_t argb) {
    Layout *L = get(s, font, size, box_w, nowrap);
    if (!L || !L->ng) return;
    float lh = text_line_height(size, line_h), asc, desc;
    text_font_vmetrics(font, size, &asc, &desc);
    static float *ox, *oy;
    static int ocap;
    if (L->ng > ocap) {
        float *nx = realloc(ox, sizeof *nx * (size_t)L->ng), *ny = realloc(oy, sizeof *ny * (size_t)L->ng);
        if (nx) ox = nx;
        if (ny) oy = ny;
        if (!nx || !ny) return;
        ocap = L->ng;
    }
    float opacity = (argb >> 24) / 255.0f;
    for (int li = 0; li < L->nlines; li++) {
        const LLine *ln = &L->lines[li];
        float pen = x;
        if (align == TEXT_CENTER) pen += (box_w - ln->width) / 2;
        else if (align == TEXT_RIGHT) pen += box_w - ln->width;
        float baseline = roundf(y + li * lh + (lh - (asc - desc)) / 2 + asc);
        if (baseline - asc > c->clip.y1 || baseline - desc < c->clip.y0) continue;
        const int *vis = L->vis + ln->vis0;
        for (int k = 0; k < ln->nvis; k++) {
            const LGlyph *g = &L->g[vis[k]];
            if (g->att >= 0) continue;
            ox[vis[k]] = pen + g->dx;
            oy[vis[k]] = g->dy;
            pen += g->adv;
        }
        /* marcas ancladas: la base tiene índice menor (orden lógico) */
        for (int pass = 0; pass < 4; pass++)
            for (int k = 0; k < ln->nvis; k++) {
                const LGlyph *g = &L->g[vis[k]];
                if (g->att < 0) continue;
                ox[vis[k]] = ox[g->att] + g->dx;
                oy[vis[k]] = oy[g->att] + g->dy;
            }
        for (int k = 0; k < ln->nvis; k++) {
            const LGlyph *g = &L->g[vis[k]];
            float gx = ox[vis[k]], gy = baseline - oy[vis[k]];
            if (text_font_is_color(g->font)) {
                int bw = (int)ceilf(g->adv > 0 ? g->adv : size), bh = (int)ceilf(asc - desc);
                const Glyph *gl = text_glyph_color(g->font, g->gid, bw, bh);
                if (gl->argb) canvas_blit(c, gl->argb, gl->w, gl->h, (int)roundf(gx), (int)roundf(baseline - asc + (bh - gl->h) / 2.0f), 0, opacity, 0);
                continue;
            }
            const Glyph *gl = text_glyph_id(g->font, g->gid, size);
            if (gl->bmp) canvas_alpha_mask(c, (int)roundf(gx) + gl->xoff, (int)roundf(gy) + gl->yoff, gl->bmp, gl->w, gl->h, gl->w, argb);
        }
    }
}

int text_direction(const char *s) {
    uint32_t buf[256];
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && n < 256;) buf[n++] = utf8_decode(&p);
    return bidi_direction(buf, n) == 1;
}

/* Depuración (tests/text_test.c): glifos en orden visual, "fuente:glifo" por línea. */
void text_layout_dump(const char *s, int font, float size) {
    Layout *L = get(s, font, size, 0, 1);
    if (!L) return;
    for (int li = 0; li < L->nlines; li++) {
        for (int k = 0; k < L->lines[li].nvis; k++) {
            const LGlyph *g = &L->g[L->vis[L->lines[li].vis0 + k]];
            printf("%s%u", k ? "|" : "", g->gid);
        }
        printf("\n");
    }
}

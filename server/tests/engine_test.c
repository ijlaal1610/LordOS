/* engine_test.c — headless tests of the UI engine's newer features (docs/DESIGN.md "Engine notes"):
 * text truncation with an ellipsis (LTR, RTL, Arabic, Thai, emoji ZWJ sequences and flags),
 * per-side borders and per-corner radii, logical margins/insets, transform math, transformed
 * painting and hit testing. It links the real layout, text and paint code; the terminal, Wayland,
 * media and GPU hooks of paint.c are stubbed out.
 *   build/engine-test            runs the tests
 *   build/engine-test bench      times layout and paint of a 2000-row list and a window drag
 * Fallback fonts are looked up as in LordOS (DejaVu Sans covers Arabic and Hebrew in the build
 * container); the checks don't depend on which font draws a script. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ui/fonts.h"
#include "ui/ui.h"
#include "ui/unicode.h"

/* ---- stubs for what paint.c reaches outside the UI engine ---- */
void term_paint(Canvas *c, int id, float x, float y, float w, float h, float op) { (void)c; (void)id; (void)x; (void)y; (void)w; (void)h; (void)op; }
void wl_paint(Canvas *c, int id, int node, float x, float y, float w, float h, float op) { (void)c; (void)id; (void)node; (void)x; (void)y; (void)w; (void)h; (void)op; }
void wl_paint_holes(int on) { (void)on; }
void wl_paint_kept(void) {}
void wl_paint_keep(void) {}
void wl_paint_begin(void) {}
void wl_paint_popups(Canvas *c) { (void)c; }
void media_paint(Canvas *c, int id, int node, float x, float y, float w, float h, float r, float op) { (void)c; (void)id; (void)node; (void)x; (void)y; (void)w; (void)h; (void)r; (void)op; }
int media_cover_node(float x0, float y0, float x1, float y1) { (void)x0; (void)y0; (void)x1; (void)y1; return 0; }
int gpu_active(void) { return 0; }

void paint_layer_begin(int node, int resize);
void paint_layer_end(void);
void paint_layer_stats(long *renders, long *blits);
void paint_tf_stats(long *renders, long *composites);
int text_layout_visual(const char *s, int font, float size, int *cps, int max);

static int fails, checks;
static int quiet;                       /* bench: styles an older engine doesn't know are skipped */
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#ifndef ENGINE_BEFORE
/* ---- UTF-8 helpers ---- */
static int u8_decode(const char *s, uint32_t *out, int max) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < max) {
        uint32_t c = *p;
        int k = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : 3;
        c = k == 0 ? c : k == 1 ? c & 0x1f : k == 2 ? c & 0x0f : c & 0x07;
        p++;
        for (int i = 0; i < k && *p; i++, p++) c = c << 6 | (*p & 0x3f);
        out[n++] = c;
    }
    return n;
}

/* what must stay with the code point before it (a grapheme cluster continues) */
static int is_mark_like(uint32_t c) {
    return uc_mark(c) || c == 0x0E33 || c == 0x200D || c == 0xFE0F || (c >= 0x1F3FB && c <= 0x1F3FF);
}

/* Checks a truncation of `s` in a box of width w: out = prefix + "…", the prefix ends on a cluster
 * boundary and it fits. Returns the prefix length in code points (-1 = not truncated). */
static int check_cut(const char *name, const char *s, float w, int force) {
    char out[1024];
    float tw = text_ellipsize(s, 400, 13, w, force, out, sizeof out);
    if (tw < 0) { CHECK(!strcmp(out, s), "%s: fits but changed", name); return -1; }
    size_t lo = strlen(out);
    CHECK(lo >= 3 && !strcmp(out + lo - 3, "\xe2\x80\xa6"), "%s w=%.0f: no ellipsis at the end: %s", name, w, out);
    size_t lp = lo - 3;
    CHECK(!strncmp(out, s, lp), "%s w=%.0f: not a prefix: %s", name, w, out);
    uint32_t a[512], b[512];
    int na = u8_decode(s, a, 512);
    char pre[1024];
    memcpy(pre, out, lp);
    pre[lp] = 0;
    int nb = u8_decode(pre, b, 512);
    if (nb > 0 && nb < na) {
        /* trailing spaces are dropped: find where the prefix really ended in s */
        int k = nb;
        while (k < na && a[k] == ' ') k++;
        uint32_t next = a[nb], prev = a[nb - 1];
        CHECK(!is_mark_like(next), "%s w=%.0f: cut before U+%04X (prefix %d cps)", name, w, next, nb);
        CHECK(prev != 0x200D, "%s w=%.0f: cut after a ZWJ", name, w);
        if (next >= 0x1F1E6 && next <= 0x1F1FF) {
            int r = 0;
            for (int i = nb - 1; i >= 0 && a[i] >= 0x1F1E6 && a[i] <= 0x1F1FF; i--) r++;
            CHECK(!(r & 1), "%s w=%.0f: cut inside a flag", name, w);
        }
        (void)k;
    }
    if (nb > 0) CHECK(tw <= w + 0.5f, "%s w=%.0f: too wide (%.1f)", name, w, tw);
    return nb;
}

static void sweep(const char *name, const char *s) {
    int last = -1;
    for (float w = 8; w <= 400; w += 3) {
        int n = check_cut(name, s, w, 0);
        if (n < 0) break;
        CHECK(n >= last, "%s: a wider box (%.0f) kept less text (%d < %d)", name, w, n, last);
        last = n;
    }
}

/* Visual position of the "…" in the cut form: 0 = leftmost. */
static void check_side(const char *name, const char *s, float w, int want_left) {
    char out[1024];
    text_ellipsize(s, 400, 13, w, 1, out, sizeof out);
    int vis[512];
    int n = text_layout_visual(out, 0, 13, vis, 512);
    uint32_t cps[512];
    int ncp = u8_decode(out, cps, 512);
    int pos = -1;
    for (int i = 0; i < n; i++) if (vis[i] == ncp - 1) pos = i;
    CHECK(n > 0 && pos >= 0, "%s: no glyph for the ellipsis", name);
    if (pos >= 0) {
        if (want_left) CHECK(pos == 0, "%s: ellipsis at visual %d of %d, want the left edge", name, pos, n);
        else CHECK(pos == n - 1, "%s: ellipsis at visual %d of %d, want the right edge", name, pos, n);
    }
}

static void test_ellipsis(void) {
    const char *ltr = "Quarterly report for the board, final version (2).pdf";
    const char *arabic = "\xd8\xaa\xd9\x82\xd8\xb1\xd9\x8a\xd8\xb1 \xd8\xa7\xd9\x84\xd9\x85\xd8\xa8\xd9\x8a\xd8\xb9\xd8\xa7\xd8\xaa "
                         "\xd8\xa7\xd9\x84\xd8\xb3\xd9\x86\xd9\x88\xd9\x8a \xd9\x84\xd9\x84\xd8\xb9\xd8\xa7\xd9\x85 \xd8\xa7\xd9\x84\xd9\x85\xd8\xa7\xd8\xb6\xd9\x8a "
                         "\xd8\xa8\xd8\xa7\xd9\x84\xd8\xaa\xd9\x81\xd8\xb5\xd9\x8a\xd9\x84 \xd9\x84\xd8\xa7 \xd8\xb9\xd9\x8e\xd9\x84\xd9\x91\xd9\x8e\xd9\x85\xd9\x8e";
    const char *hebrew = "\xd7\x93\xd7\x95\xd7\x97 \xd7\xa8\xd7\x91\xd7\xa2\xd7\x95\xd7\xa0\xd7\x99 \xd7\xa1\xd7\x95\xd7\xa4\xd7\x99 \xd7\x9c\xd7\x93\xd7\x99\xd7\xa8\xd7\xa7\xd7\x98\xd7\x95\xd7\xa8\xd7\x99\xd7\x95\xd7\x9f";
    const char *thai = "\xe0\xb8\xaa\xe0\xb8\xa7\xe0\xb8\xb1\xe0\xb8\xaa\xe0\xb8\x94\xe0\xb8\xb5\xe0\xb8\x84\xe0\xb8\xa3\xe0\xb8\xb1\xe0\xb8\x9a "
                       "\xe0\xb8\xa2\xe0\xb8\xb4\xe0\xb8\x99\xe0\xb8\x94\xe0\xb8\xb5\xe0\xb8\x95\xe0\xb9\x89\xe0\xb8\xad\xe0\xb8\x99\xe0\xb8\xa3\xe0\xb8\xb1\xe0\xb8\x9a "
                       "\xe0\xb8\x99\xe0\xb9\x89\xe0\xb8\xb3\xe0\xb8\x97\xe0\xb8\xb3\xe0\xb8\x99\xe0\xb9\x89\xe0\xb8\xb3\xe0\xb9\x83\xe0\xb8\x88";
    const char *emoji = "Family \xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6 trip "
                        "\xf0\x9f\x87\xa6\xf0\x9f\x87\xb7\xf0\x9f\x87\xa7\xf0\x9f\x87\xb7\xf0\x9f\x87\xa8\xf0\x9f\x87\xb1 \xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd "
                        "\xe2\x9d\xa4\xef\xb8\x8f done";
    const char *marks = "Cafe\xcc\x81 de\xcc\x81ja\xcc\x80 vu na\xc3\xafve re\xcc\x81sume\xcc\x81 tre\xcc\x80s longue";

    /* fits: unchanged */
    char out[256];
    CHECK(text_ellipsize("Short", 400, 13, 200, 0, out, sizeof out) < 0 && !strcmp(out, "Short"), "short text was cut");
    /* forced (more lines follow) adds the ellipsis even if it fits */
    text_ellipsize("Short", 400, 13, 200, 1, out, sizeof out);
    CHECK(!strcmp(out, "Short\xe2\x80\xa6"), "forced: %s", out);
    /* a box smaller than the ellipsis: only the ellipsis */
    text_ellipsize(ltr, 400, 13, 2, 0, out, sizeof out);
    CHECK(!strcmp(out, "\xe2\x80\xa6"), "tiny box: %s", out);

    sweep("LTR", ltr);
    sweep("Arabic", arabic);
    sweep("Hebrew", hebrew);
    sweep("Thai", thai);
    sweep("emoji", emoji);
    sweep("combining", marks);

    /* the ellipsis goes on the visual left of RTL text, on the right of LTR */
    check_side("Arabic", arabic, 120, 1);
    check_side("Hebrew", hebrew, 90, 1);
    check_side("LTR in complex path", emoji, 90, 0);

    /* Arabic: the cut letter takes its final form (the cut form is shaped again), so the last
     * glyph before the ellipsis differs from the same letter in the middle of the word */
    {
        char a[256];
        text_ellipsize(arabic, 400, 13, 60, 0, a, sizeof a);
        float w1, h1;
        text_measure(a, 400, 13, 0, 0, 1, &w1, &h1);
        CHECK(w1 <= 60.5f, "Arabic cut form measures %.1f > 60", w1);
    }

    /* measure with a line clamp: at most N lines, and the width of the box when cut */
    const char *para = "The quick brown fox jumps over the lazy dog and keeps running across the whole field until night";
    float w, h, w2, h2;
    text_measure_ex(para, 400, 13, 0, 120, 0, 0, &w, &h);
    text_measure_ex(para, 400, 13, 0, 120, 0, 2, &w2, &h2);
    float lh = text_line_height(13, 0);
    CHECK(h > 2 * lh && fabsf(h2 - 2 * lh) < 0.01f, "clamp: %f lines -> %f", h / lh, h2 / lh);
    CHECK(fabsf(w2 - 120) < 0.6f, "clamp width %.1f", w2);
    text_measure_ex(arabic, 400, 13, 0, 90, 0, 2, &w2, &h2);
    CHECK(fabsf(h2 - 2 * lh) < 0.01f, "Arabic clamp: %f lines", h2 / lh);

    /* drawing: nowrap + ellipsis stays inside the box; the last clamped line ends in an ellipsis */
    static uint32_t px[300 * 80];
    Canvas c;
    canvas_init(&c, px, 300, 80, 300);
    canvas_clear(&c, 0);
    text_draw_ex(&c, ltr, 400, 13, 0, 10, 10, 100, TEXT_LEFT, 1, 0, 1, 0xffffffffu);
    int ink_out = 0, ink_in = 0;
    for (int y = 0; y < 80; y++)
        for (int x = 0; x < 300; x++)
            if (px[y * 300 + x] & 0xff) { if (x >= 111) ink_out++; else ink_in++; }
    CHECK(ink_in > 50 && ink_out == 0, "nowrap ellipsis drew outside the box (%d in, %d out)", ink_in, ink_out);
    canvas_clear(&c, 0);
    text_draw_ex(&c, para, 400, 13, 0, 10, 0, 120, TEXT_LEFT, 0, 2, 1, 0xffffffffu);
    int below = 0;
    for (int y = (int)(2 * lh) + 1; y < 80; y++) for (int x = 0; x < 300; x++) if (px[y * 300 + x] & 0xff) below++;
    CHECK(below == 0, "clamped text drew a third line (%d px)", below);
}

#endif

/* ---- nodes ---- */
static Node *text(Node *parent, const char *s, float size);
static Node *view(Node *parent) {
    Node *n = node_new(NODE_VIEW);
    if (parent) node_append(parent, n);
    return n;
}
/* as lord.setStyle (js.c): set, then invalidate */
static void sn(Node *n, const char *k, double v) { if (style_set_number(&n->s, k, v) && !quiet) { printf("unknown style %s\n", k); fails++; } node_invalidate(n); }
static void ss(Node *n, const char *k, const char *v) { if (style_set_string(&n->s, k, v) && !quiet) { printf("unknown style %s=%s\n", k, v); fails++; } node_invalidate(n); }

#ifndef ENGINE_BEFORE
static uint32_t *fb;
static int FW, FH;
static Canvas frame;

static void render(Node *root, int w, int h) {
    if (FW != w || FH != h) { free(fb); fb = calloc((size_t)w * h, 4); FW = w; FH = h; }
    canvas_init(&frame, fb, w, h, w);
    layout_root(root, (float)w, (float)h);
    paint_damage(root);
    ui.damage = (Box){ 0, 0, (float)w, (float)h };
    canvas_clear(&frame, 0);
    paint_root(&frame, root);
    ui.damage = (Box){ 0, 0, 0, 0 };
}
static uint32_t pix(int x, int y) { return fb[y * FW + x] & 0xffffff; }

static void test_borders(void) {
    ui_reset();
    Node *root = view(NULL);
    ui.root = root;
    sn(root, "backgroundColor", 0xffffffff);
    /* bottom hairline 2 px red and a 1 px blue left edge, no top or right */
    Node *a = view(root);
    ss(a, "position", "absolute");
    sn(a, "left", 10); sn(a, "top", 10); sn(a, "width", 50); sn(a, "height", 30);
    sn(a, "borderBottomWidth", 2); sn(a, "borderBottomColor", 0xffff0000);
    sn(a, "borderLeftWidth", 1); sn(a, "borderLeftColor", 0xff0000ff);
    /* RTL: borderStart is the right side */
    Node *b = view(root);
    ss(b, "position", "absolute"); ss(b, "direction", "rtl");
    sn(b, "left", 70); sn(b, "top", 10); sn(b, "width", 20); sn(b, "height", 20);
    sn(b, "borderStartWidth", 3); sn(b, "borderStartColor", 0xff00ff00);
    /* only the top-left corner rounded */
    Node *r = view(root);
    ss(r, "position", "absolute");
    sn(r, "left", 10); sn(r, "top", 50); sn(r, "width", 40); sn(r, "height", 40);
    sn(r, "backgroundColor", 0xff000000); sn(r, "borderTopLeftRadius", 12);
    /* a rounded box with a per-side border: the sides meet on the corner's diagonal */
    Node *q = view(root);
    ss(q, "position", "absolute");
    sn(q, "left", 60); sn(q, "top", 50); sn(q, "width", 40); sn(q, "height", 40);
    sn(q, "borderRadius", 8); sn(q, "borderWidth", 4); sn(q, "borderColor", 0xff808080);
    sn(q, "borderTopColor", 0xffff0000);
    /* uniform border: the old path */
    Node *u = view(root);
    ss(u, "position", "absolute");
    sn(u, "left", 110); sn(u, "top", 10); sn(u, "width", 20); sn(u, "height", 20);
    sn(u, "borderWidth", 1); sn(u, "borderColor", 0xff000000);
    render(root, 140, 100);

    CHECK(!node_edges_plain(a) && node_edges_plain(u), "edges_plain flags");
    CHECK(pix(35, 39) == 0xff0000 && pix(35, 38) == 0xff0000, "bottom border: %06x", pix(35, 39));
    CHECK(pix(35, 37) == 0xffffff, "bottom border too thick: %06x", pix(35, 37));
    CHECK(pix(10, 25) == 0x0000ff, "left border: %06x", pix(10, 25));
    CHECK(pix(35, 10) == 0xffffff, "a top border appeared: %06x", pix(35, 10));
    CHECK(pix(59, 25) == 0xffffff, "a right border appeared: %06x", pix(59, 25));
    CHECK(node_border(b, EDGE_R) == 3 && node_border(b, EDGE_L) == 0, "RTL start = right (%g %g)", node_border(b, EDGE_L), node_border(b, EDGE_R));
    CHECK(pix(88, 20) == 0x00ff00 && pix(70, 20) == 0xffffff, "RTL border start: %06x %06x", pix(88, 20), pix(70, 20));
    CHECK(pix(11, 51) == 0xffffff, "top-left corner not rounded: %06x", pix(11, 51));
    CHECK(pix(48, 51) == 0x000000 && pix(11, 88) == 0x000000 && pix(48, 88) == 0x000000, "other corners should be square");
    CHECK(pix(80, 51) == 0xff0000 && pix(61, 70) == 0x808080 && pix(80, 88) == 0x808080, "rounded per-side border: %06x %06x %06x",
          pix(80, 51), pix(61, 70), pix(80, 88));
    CHECK(pix(80, 70) == 0xffffff, "rounded border filled the inside: %06x", pix(80, 70));
    CHECK(pix(110, 20) == 0x000000 && pix(120, 10) == 0x000000 && pix(120, 20) == 0xffffff, "uniform border");
    /* the content box shrinks by the per-side widths */
    Node *in = view(a);
    sn(in, "flexGrow", 1);
    render(root, 140, 100);
    CHECK(in->x == 1 && in->y == 0 && in->w == 49 && in->h == 28, "content box %g %g %g %g", in->x, in->y, in->w, in->h);
}

static void test_logical(void) {
    ui_reset();
    Node *root = view(NULL);
    ui.root = root;
    Node *col = view(root);
    sn(col, "width", 200); sn(col, "height", 100);
    Node *c1 = view(col);
    sn(c1, "height", 10); sn(c1, "marginStart", 10); sn(c1, "marginEnd", 30);
    Node *c2 = view(col);
    sn(c2, "height", 10); sn(c2, "paddingStart", 7);
    Node *t = view(c2);
    sn(t, "height", 4);
    Node *abs = view(col);
    ss(abs, "position", "absolute"); sn(abs, "width", 20); sn(abs, "height", 20); sn(abs, "start", 5); sn(abs, "top", 0);
    render(root, 300, 120);
    CHECK(c1->x == 10 && c1->w == 160, "LTR marginStart: x %g w %g", c1->x, c1->w);
    CHECK(t->x == 7, "LTR paddingStart: %g", t->x);
    CHECK(abs->x == 5, "LTR start: %g", abs->x);
    ss(col, "direction", "rtl");
    node_invalidate(col);
    render(root, 300, 120);
    CHECK(c1->x == 30 && c1->w == 160, "RTL marginStart: x %g w %g", c1->x, c1->w);
    CHECK(t->x == 0 && t->w == 193, "RTL paddingStart: x %g w %g", t->x, t->w);
    CHECK(abs->x == 175, "RTL start: %g", abs->x);
    /* a physical value loses against the logical one, as in React Native */
    sn(c1, "marginLeft", 50);
    render(root, 300, 120);
    CHECK(c1->x == 30 && node_margin(c1)[EDGE_R] == 10, "RTL marginLeft loses against marginEnd: x %g", c1->x);
    /* removing the logical values gives the physical ones back */
    style_reset(&c1->s, "marginStart");
    style_reset(&c1->s, "marginEnd");
    node_invalidate(c1);
    render(root, 300, 120);
    CHECK(c1->x == 50 && node_margin(c1)[EDGE_R] == 0, "reset marginStart/End: x %g right %g", c1->x, node_margin(c1)[EDGE_R]);
}

static void test_transform_math(void) {
    Affine s = { 2, 0, 0, 3, 5, 7 }, inv;
    CHECK(affine_invert(s, &inv), "invert");
    Affine id = affine_mul(s, inv);
    CHECK(fabsf(id.a - 1) < 1e-5f && fabsf(id.d - 1) < 1e-5f && fabsf(id.e) < 1e-4f && fabsf(id.f) < 1e-4f && fabsf(id.b) < 1e-6f,
          "m * inv(m) = identity");
    Style st;
    style_defaults(&st);
    CHECK(!style_has_transform(&st), "default style has no transform");
    style_set_number(&st, "scaleX", 2);
    style_set_number(&st, "scaleY", 2);
    Affine m = style_transform(&st, 10, 10, 20, 20);     /* scale 2 around the center (20, 20) */
    float x, y;
    affine_apply(m, 20, 20, &x, &y);
    CHECK(fabsf(x - 20) < 1e-4f && fabsf(y - 20) < 1e-4f, "origin fixed: %g %g", x, y);
    affine_apply(m, 10, 10, &x, &y);
    CHECK(fabsf(x) < 1e-4f && fabsf(y) < 1e-4f, "corner scaled: %g %g", x, y);
    free(st.ext);
    style_defaults(&st);
    style_set_number(&st, "rotate", 90);                /* clockwise on screen */
    m = style_transform(&st, 0, 0, 20, 20);
    affine_apply(m, 20, 10, &x, &y);                    /* right middle → bottom middle */
    CHECK(fabsf(x - 10) < 1e-4f && fabsf(y - 20) < 1e-4f, "rotate 90: %g %g", x, y);
    style_set_number(&st, "transformOriginX", 0);
    style_set_number(&st, "transformOriginY", 0);
    m = style_transform(&st, 0, 0, 20, 20);
    free(st.ext);
    affine_apply(m, 10, 0, &x, &y);
    CHECK(fabsf(x) < 1e-4f && fabsf(y - 10) < 1e-4f, "rotate around the top-left: %g %g", x, y);
    style_defaults(&st);
    style_set_number(&st, "rotate", 45);
    Box b = affine_box(style_transform(&st, 0, 0, 10, 10), (Box){ 0, 0, 10, 10 });
    CHECK(fabsf((b.x1 - b.x0) - 14.142f) < 0.01f, "rotated bounding box %g", b.x1 - b.x0);
    style_set_number(&st, "rotate", 360);
    CHECK(style_has_transform(&st) == 0, "rotate 360 is no transform");
    free(st.ext);
}

/* Shell layers (sdk/layers.js, docs/LAYERS.md): full-screen root children with zIndex and
 * pointerEvents box-none. Paint and hit order follow the zIndex whatever the tree order, a layer
 * never takes a click itself, and pointerEvents none still hides a whole subtree. */
static Node *layer_node(Node *root, float z, float w, float h) {
    Node *l = view(root);
    ss(l, "position", "absolute");
    sn(l, "left", 0); sn(l, "top", 0); sn(l, "width", w); sn(l, "height", h);
    ss(l, "pointerEvents", "box-none");
    sn(l, "zIndex", z);
    return l;
}
static Node *box_in(Node *parent, float x, float y, float w, float h, uint32_t color) {
    Node *b = view(parent);
    ss(b, "position", "absolute");
    sn(b, "left", x); sn(b, "top", y); sn(b, "width", w); sn(b, "height", h);
    sn(b, "backgroundColor", color);
    return b;
}
static void test_layers(void) {
    ui_reset();
    Node *root = view(NULL);
    ui.root = root;
    sn(root, "backgroundColor", 0xffffffff);
    /* created in the "wrong" tree order on purpose: popover before dock, both before the windows */
    Node *pop = layer_node(root, 50, 160, 100);
    Node *dockl = layer_node(root, 10, 160, 100);
    Node *base = view(root);                            /* the theme's tree (zIndex 0) */
    sn(base, "width", 160); sn(base, "height", 100);
    Node *win = box_in(base, 0, 0, 160, 100, 0xff0000ff);         /* a window: blue */
    Node *dock = box_in(dockl, 0, 10, 30, 80, 0xff00ff00);        /* the Dock on the left: green */
    Node *menu = box_in(pop, 10, 20, 60, 30, 0xffff0000);         /* the crown menu: red */
    Node *ghost = box_in(pop, 100, 60, 20, 20, 0xff000000);       /* pointerEvents none: never hit */
    ss(ghost, "pointerEvents", "none");
    render(root, 160, 100);

    CHECK(pix(20, 30) == 0xff0000, "menu painted over the Dock: %06x", pix(20, 30));
    CHECK(pix(5, 70) == 0x00ff00, "Dock painted over the window: %06x", pix(5, 70));
    CHECK(pix(100, 30) == 0x0000ff, "window painted under empty layers: %06x", pix(100, 30));
    CHECK(hit_test(root, 20, 30) == menu, "a click on the menu over the Dock goes to the menu");
    CHECK(hit_test(root, 5, 70) == dock, "a click on the Dock goes to the Dock");
    CHECK(hit_test(root, 100, 30) == win, "an empty layer lets the click through to the window");
    CHECK(hit_test(root, 110, 70) == win, "pointerEvents none: the click goes through");
    /* the base can't climb above a layer, the order holds after a re-render */
    sn(win, "zIndex", 5);
    render(root, 160, 100);
    CHECK(hit_test(root, 20, 30) == menu && pix(20, 30) == 0xff0000, "a zIndex inside the base stays under the layers");
}

/* Closing a menu repaints only where it was; the translucent, blurred menu bar above it must come
 * out as in a full repaint (before 0.8.1 the blur mixed in the bar's last frame: a lighter band). */
static void test_backdrop_band(void) {
    ui_reset();
    Node *root = view(NULL);
    ui.root = root;
    sn(root, "backgroundColor", 0xff102040);
    for (int i = 0; i < 8; i++) box_in(root, (float)(i * 20), 0, 10, 100, 0xff4060a0);   /* stripes to blur */
    Node *barl = layer_node(root, 20, 160, 100);
    Node *bar = box_in(barl, 0, 0, 160, 28, 0x80ffffff);
    sn(bar, "backdropBlur", 12);
    Node *popl = layer_node(root, 50, 160, 100);
    Node *menu = box_in(popl, 30, 20, 60, 60, 0xffff0000);       /* reaches into the bar, like its shadow */
    render(root, 160, 100);
    node_destroy(menu);
    layout_root(root, 160, 100);
    Box d = { 0, 0, 0, 0 };
    if (paint_damage(root)) d = ui.damage;
    CHECK(d.x0 <= 0 && d.y0 <= 0 && d.x1 >= 160 && d.y1 >= 28, "the blurred bar is repainted whole: %.0f,%.0f %.0f,%.0f", d.x0, d.y0, d.x1, d.y1);
    ui.damage = d;
    Rect r = { (int)fmaxf(0, floorf(d.x0)), (int)fmaxf(0, floorf(d.y0)), (int)fminf(160, ceilf(d.x1)), (int)fminf(100, ceilf(d.y1)) };
    canvas_set_clip(&frame, r);
    canvas_clear(&frame, 0);
    paint_root(&frame, root);
    canvas_set_clip(&frame, (Rect){ 0, 0, 160, 100 });
    ui.damage = (Box){ 0, 0, 0, 0 };
    uint32_t *partial = malloc(160 * 28 * 4);
    if (!partial) return;
    memcpy(partial, fb, 160 * 28 * 4);
    render(root, 160, 100);
    int worst = 0;
    for (int i = 0; i < 160 * 28; i++)
        for (int sh = 0; sh < 24; sh += 8) {
            int dv = abs((int)((partial[i] >> sh) & 255) - (int)((fb[i] >> sh) & 255));
            if (dv > worst) worst = dv;
        }
    CHECK(worst <= 1, "menu bar after closing a menu differs from a full repaint by %d", worst);
    free(partial);
}

/* Several damaged rectangles (0.8.2): dots moving far apart on a "map" are repainted one rectangle
 * at a time, and every frame must be pixel-identical to a full repaint, also with a translucent,
 * blurred bar whose margin a dot crosses (the 0.8.1 band rule, test_backdrop_band). */
static uint32_t rng_state = 12345;
static uint32_t rnd(void) { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 8; }

static int paint_partial(Node *root, int w, int h, int *nrects, float *area) {
    Box d = { 0, 0, 0, 0 };
    if (!paint_damage(root)) { *nrects = 0; *area = 0; return 0; }
    d = ui.damage;
    Box rs[DAMAGE_MAX];
    int n = paint_damage_rects(rs, DAMAGE_MAX);
    *nrects = n;
    *area = 0;
    for (int i = 0; i < n; i++) {
        Box b = rs[i];
        if (b.x0 < d.x0 || b.y0 < d.y0 || b.x1 > d.x1 || b.y1 > d.y1) return -1;     /* outside the box */
        for (int j = i + 1; j < n; j++)
            if (b.x0 < rs[j].x1 && rs[j].x0 < b.x1 && b.y0 < rs[j].y1 && rs[j].y0 < b.y1) return -2;   /* overlap */
        Rect r = { (int)fmaxf(0, floorf(b.x0)), (int)fmaxf(0, floorf(b.y0)), (int)fminf((float)w, ceilf(b.x1)), (int)fminf((float)h, ceilf(b.y1)) };
        if (r.x1 <= r.x0 || r.y1 <= r.y0) continue;
        *area += (float)(r.x1 - r.x0) * (r.y1 - r.y0);
        ui.damage = (Box){ (float)r.x0, (float)r.y0, (float)r.x1, (float)r.y1 };
        canvas_set_clip(&frame, r);
        canvas_clear(&frame, 0);
        paint_root(&frame, root);
    }
    canvas_set_clip(&frame, (Rect){ 0, 0, w, h });
    ui.damage = (Box){ 0, 0, 0, 0 };
    return 1;
}

static int full_differs(Node *root, int w, int h, int *where) {
    static uint32_t *ref;
    static int rw, rh;
    if (rw != w || rh != h) { free(ref); ref = calloc((size_t)w * h, 4); rw = w; rh = h; }
    if (!ref) return 999;
    Canvas rc;
    canvas_init(&rc, ref, w, h, w);
    ui.damage = (Box){ 0, 0, (float)w, (float)h };
    canvas_clear(&rc, 0);
    paint_root(&rc, root);
    ui.damage = (Box){ 0, 0, 0, 0 };
    int worst = 0;
    for (int i = 0; i < w * h; i++)
        for (int sh = 0; sh < 24; sh += 8) {
            int dv = abs((int)((ref[i] >> sh) & 255) - (int)((fb[i] >> sh) & 255));
            if (dv > worst) { worst = dv; *where = i; }
        }
    return worst;
}

static void test_multi_damage(void) {
    enum { W = 640, H = 400, DOTS = 24 };
    paint_multi_damage = 1;                 /* off by default (paint.c); LORD_DAMAGE=multi */
    ui_reset();
    Node *root = view(NULL);
    ui.root = root;
    sn(root, "backgroundColor", 0xff1c1c1e);
    for (int i = 0; i < 16; i++) box_in(root, (float)(i * 40), 0, 14, H, 0xff2c4a7a);      /* something to blur */
    Node *map = box_in(root, 40, 34, 560, 340, 0xff20304a);
    ss(map, "overflow", "hidden");
    sn(map, "borderRadius", 12);
    for (int i = 0; i < 6; i++) box_in(map, (float)(30 + i * 80), (float)(40 + (i % 3) * 70), 50, 30, 0xff3a5a3a);   /* "land" */
    Node *dots[DOTS];
    float dx[DOTS], dy[DOTS];
    for (int i = 0; i < DOTS; i++) {
        dx[i] = (float)(rnd() % 540);
        dy[i] = (float)(rnd() % 300);
        dots[i] = box_in(map, dx[i], dy[i], 6, 6, i % 2 ? 0xd8ff9f0a : 0xd830d158);   /* translucent, round */
        sn(dots[i], "borderRadius", 3);
    }
    Node *barl = layer_node(root, 20, W, H);
    Node *bar = box_in(barl, 0, 0, W, 28, 0x80ffffff);
    sn(bar, "backdropBlur", 10);
    render(root, W, H);
    int multi = 0, worst_all = 0, bad = 0;
    float saved = 0;
    for (int f = 0; f < 40; f++) {
        for (int i = 0; i < DOTS; i++) {
            if (i % 3 == f % 3) continue;            /* not all of them every frame */
            dx[i] += (float)((int)(rnd() % 9) - 4) + 0.5f;
            dy[i] += (float)((int)(rnd() % 7) - 3);
            if (f % 10 == 7 && i == 0) dy[i] = -20;  /* up near the bar: inside its blur margin (the map clips it) */
            if (dx[i] < -6) dx[i] = 540;
            if (dx[i] > 560) dx[i] = 0;
            if (dy[i] > 320 || dy[i] < -20) dy[i] = 100;
            sn(dots[i], "left", dx[i]);
            sn(dots[i], "top", dy[i]);
        }
        if (f % 13 == 5) sn(bar, "backgroundColor", f % 2 ? 0x80ffffff : 0x90f0f0ff);    /* the bar itself */
        if (f % 17 == 9) { Node *m = box_in(barl, 200, 20, 80, 60, 0xffff3030); render(root, W, H); node_destroy(m); }   /* a menu opens, then closes */
        layout_root(root, W, H);
        int n = 0;
        float area = 0;
        int ok = paint_partial(root, W, H, &n, &area);
        if (ok < 0) { bad++; continue; }
        if (n > 1) { multi++; saved += area; }
        int where = 0, worst = full_differs(root, W, H, &where);
        if (worst > worst_all) worst_all = worst;
        CHECK(worst <= 1, "frame %d (%d rects) differs from a full repaint by %d at %d,%d", f, n, worst, where % W, where / W);
    }
    CHECK(!bad, "%d frames had a rectangle outside the box or two that overlap", bad);
    CHECK(multi >= 20, "only %d of 40 frames used several rectangles", multi);
    /* the same with one rectangle forced, for the numbers */
    paint_multi_damage = 0;
    float one = 0;
    int ones = 0;
    for (int f = 0; f < 10; f++) {
        for (int i = 0; i < DOTS; i++) { dx[i] = (float)(rnd() % 540); sn(dots[i], "left", dx[i]); }
        layout_root(root, W, H);
        int n = 0;
        float area = 0;
        paint_partial(root, W, H, &n, &area);
        one += area;
        ones++;
        int where = 0;
        CHECK(n <= 1 && full_differs(root, W, H, &where) <= 1, "one rectangle (LORD_DAMAGE=1) differs from a full repaint");
    }
    paint_multi_damage = 1;
    if (multi && ones) printf("multi-damage: %d frames, %.0f px per frame in rectangles; one box: %.0f px\n", multi, saved / multi, one / ones);
    /* ui.damage set directly (a full repaint): one rectangle, the whole of it */
    damage_add((Box){ 10, 10, 20, 20 });
    ui.damage = (Box){ 0, 0, W, H };
    damage_add((Box){ 100, 100, 110, 110 });
    layout_root(root, W, H);
    paint_damage(root);
    Box rs[DAMAGE_MAX];
    int n = paint_damage_rects(rs, DAMAGE_MAX);
    CHECK(n == 1 && rs[0].x0 <= 0 && rs[0].x1 >= W && rs[0].y1 >= H, "a full repaint set directly stays one rectangle (%d)", n);
    ui.damage = (Box){ 0, 0, 0, 0 };
    /* the list itself: overlaps merge, the cap holds */
    DamageList l = { .n = 0 };
    dlist_add(&l, (Box){ 0, 0, 10, 10 });
    dlist_add(&l, (Box){ 5, 5, 15, 15 });
    dlist_add(&l, (Box){ 100, 100, 110, 110 });
    CHECK(l.n == 2 && l.r[0].x1 == 15 && l.r[1].x0 == 100, "dlist merges overlaps (%d)", l.n);
    for (int i = 0; i < 100; i++) dlist_add(&l, (Box){ (float)(i * 20), 300, (float)(i * 20 + 5), 305 });
    CHECK(l.n <= DAMAGE_MAX, "dlist stays at %d rectangles (%d)", DAMAGE_MAX, l.n);
    int overl = 0;
    for (int i = 0; i < l.n; i++) for (int j = i + 1; j < l.n; j++)
        if (l.r[i].x0 < l.r[j].x1 && l.r[j].x0 < l.r[i].x1 && l.r[i].y0 < l.r[j].y1 && l.r[j].y0 < l.r[i].y1) overl++;
    CHECK(!overl, "dlist never holds two overlapping rectangles (%d)", overl);
    (void)worst_all;
    paint_multi_damage = 0;
}


/* Minimizing a window over the Monitor's moving dots (0.8.2 release): the window is remounted with a
 * shrinking transform towards the Dock, then removed, while the dots keep moving. Every frame,
 * painted as several rectangles, must match a full repaint: once a few small rectangles are the
 * only damage, anything left behind stays on screen (the grey rectangle seen during integration). */
static Node *mk_window(Node *root, float x, float y, float w, float h) {
    Node *win = box_in(root, x, y, w, h, 0xff3a3a3c);
    ss(win, "overflow", "hidden");
    sn(win, "borderRadius", 10);
    sn(win, "shadowColor", 0x80000000);
    sn(win, "shadowBlur", 28);
    sn(win, "shadowOffsetY", 10);
    sn(win, "borderWidth", 1);
    sn(win, "borderColor", 0x40ffffff);
    box_in(win, 0, 0, w, 28, 0xff505052);                /* title bar */
    box_in(win, 8, 36, w - 16, h - 44, 0xff1e1e1e);      /* content */
    return win;
}

static void test_minimize_damage(void) {
    enum { W = 640, H = 400, DOTS = 24 };
    for (int variant = 0; variant < 8; variant++) {
        paint_multi_damage = variant < 4;     /* both: several rectangles (LORD_DAMAGE=multi) and one box */
        ui_reset();
        Node *root = view(NULL);
        ui.root = root;
        sn(root, "backgroundColor", 0xff1c1c1e);
        Node *map = box_in(root, 20, 30, 600, 350, 0xff20304a);
        ss(map, "overflow", "hidden");
        sn(map, "borderRadius", 12);
        Node *dots[DOTS];
        float dx[DOTS], dy[DOTS];
        for (int i = 0; i < DOTS; i++) {
            dx[i] = (float)(rnd() % 580);
            dy[i] = (float)(rnd() % 330);
            dots[i] = box_in(map, dx[i], dy[i], 6, 6, i % 2 ? 0xd8ff9f0a : 0xd830d158);
            sn(dots[i], "borderRadius", 3);
        }
        Node *barl = layer_node(root, 20, W, H);
        Node *bar = box_in(barl, 0, 0, W, 24, 0x80ffffff);
        sn(bar, "backdropBlur", 10);
        Node *dockl = layer_node(root, 10, W, H);
        Node *dock = box_in(dockl, 220, 350, 200, 46, 0x90303034);
        sn(dock, "backdropBlur", 20);
        sn(dock, "borderRadius", 14);
        float wx = 140 + (variant % 4) * 20, wy = 90, ww = 320, wh = 200;
        Node *win = mk_window(root, wx, wy, ww, wh);
        render(root, W, H);
        int steps = 6 + (variant % 4) * 3, bad = 0;
        for (int f = 0; f < 40; f++) {
            for (int i = 0; i < DOTS; i++) {
                if ((i + f) % 4 == 0) continue;
                dx[i] += (float)((int)(rnd() % 9) - 4) + 0.5f;
                dy[i] += (float)((int)(rnd() % 7) - 3);
                if (dx[i] < -6) dx[i] = 580;
                if (dx[i] > 600) dx[i] = 0;
                if (dy[i] > 350 || dy[i] < -6) dy[i] = 100;
                sn(dots[i], "left", dx[i]);
                sn(dots[i], "top", dy[i]);
            }
            if (f == 4) {                                   /* minimize: the same window, remounted */
                node_destroy(win);
                win = mk_window(root, wx, wy, ww, wh);
            }
            if (f >= 4 && f < 4 + steps) {
                float p = (float)(f - 4 + 1) / (float)steps, e = p * p;
                sn(win, "scaleX", 1 - 0.92f * e);
                sn(win, "scaleY", 1 - 0.92f * e);
                sn(win, "translateX", (320 - (wx + ww / 2)) * e);
                sn(win, "translateY", (364 - (wy + wh / 2)) * e);
                sn(win, "opacity", 1 - 0.7f * e);
            }
            if (f == 4 + steps && win) { node_destroy(win); win = NULL; }
            if (f == 30) win = mk_window(root, wx, wy, ww, wh);   /* restored */
            layout_root(root, W, H);
            int n = 0;
            float area = 0;
            if (paint_partial(root, W, H, &n, &area) < 0) { bad++; continue; }
            int where = 0, worst = full_differs(root, W, H, &where);
            CHECK(worst <= 1, "minimize variant %d frame %d (%d rects) differs from a full repaint by %d at %d,%d",
                  variant, f, n, worst, where % W, where / W);
        }
        CHECK(!bad, "minimize variant %d: %d frames with bad rectangles", variant, bad);
    }
    paint_multi_damage = 0;
}


/* A row with a fixed child and one that grows (a list item: bullet + text): the growing child's
 * wrapped height is measured at the width it gets, not the whole row's (0.8.2: the last line of a
 * long "What's new" bullet in Store → Updates drew over the next bullet). */
static void test_grow_wrap_height(void) {
    ui_reset();
    Node *root = view(NULL);
    ui.root = root;
    sn(root, "width", 200);
    Node *row = view(root);
    ss(row, "flexDirection", "row");
    Node *bullet = view(row);
    sn(bullet, "width", 18); sn(bullet, "height", 14);
    Node *text = view(row);
    sn(text, "flexGrow", 1); sn(text, "flexBasis", 0);
    Node *words = view(text);
    ss(words, "flexDirection", "row");
    ss(words, "flexWrap", "wrap");
    for (int i = 0; i < 10; i++) { Node *w = view(words); sn(w, "width", 40); sn(w, "height", 14); }
    Node *next = view(root);
    sn(next, "height", 10);
    layout_root(root, 200, 300);
    /* 182 px for the words: 4 per line, 3 lines (at 200 it would be 5 per line, 2 lines) */
    CHECK(fabsf(words->h - 42) < 0.5f, "wrapped words: %g high, want 42", words->h);
    CHECK(row->h >= words->h - 0.5f, "the row (%g) is shorter than its wrapped text (%g)", row->h, words->h);
    CHECK(next->ay >= row->ay + words->h - 0.5f, "the next item (y %g) overlaps the wrapped text (ends at %g)", next->ay, row->ay + words->h);
}

static void test_transform_paint_hit(void) {
    ui_reset();
    Node *root = view(NULL);
    ui.root = root;
    sn(root, "backgroundColor", 0xffffffff);
    Node *a = view(root);                               /* 40×40 red, scaled 0.5 around its center */
    ss(a, "position", "absolute");
    sn(a, "left", 10); sn(a, "top", 10); sn(a, "width", 40); sn(a, "height", 40);
    sn(a, "backgroundColor", 0xffff0000);
    sn(a, "scaleX", 0.5); sn(a, "scaleY", 0.5);
    Node *ac = view(a);                                 /* a child: transformed with it */
    ss(ac, "position", "absolute");
    sn(ac, "left", 0); sn(ac, "top", 0); sn(ac, "width", 20); sn(ac, "height", 20);
    sn(ac, "backgroundColor", 0xff0000ff);
    Node *b = view(root);                               /* 60×10 green bar rotated 90° */
    ss(b, "position", "absolute");
    sn(b, "left", 70); sn(b, "top", 45); sn(b, "width", 60); sn(b, "height", 10);
    sn(b, "backgroundColor", 0xff00ff00);
    sn(b, "rotate", 90);
    render(root, 160, 100);

    /* layout is unaffected */
    CHECK(a->x == 10 && a->w == 40 && b->w == 60, "layout changed by the transform");
    /* painting: a covers 20..40, its child (top-left quarter) 20..30 */
    CHECK(pix(15, 15) == 0xffffff, "scaled box painted at its layout size: %06x", pix(15, 15));
    CHECK(pix(25, 25) == 0x0000ff && pix(35, 35) == 0xff0000, "scaled box and child: %06x %06x", pix(25, 25), pix(35, 35));
    CHECK(pix(45, 30) == 0xffffff, "scaled box too wide: %06x", pix(45, 30));
    /* the bar: vertical, x 95..105, y 20..80 */
    CHECK(pix(100, 25) == 0x00ff00 && pix(100, 75) == 0x00ff00, "rotated bar: %06x %06x", pix(100, 25), pix(100, 75));
    CHECK(pix(75, 50) == 0xffffff && pix(125, 50) == 0xffffff, "rotated bar still horizontal: %06x", pix(75, 50));

    /* hit testing uses the transformed boxes */
    CHECK(hit_test(root, 15, 15) == root, "hit inside the untransformed box only");
    CHECK(hit_test(root, 25, 25) == ac, "hit the child of a scaled box");
    CHECK(hit_test(root, 35, 35) == a, "hit the scaled box");
    CHECK(hit_test(root, 100, 22) == b, "hit the rotated bar's end");
    CHECK(hit_test(root, 75, 50) == root, "hit where the bar was before rotating");

    /* the raster is reused while only the scale changes within a step, and redone when a child changes */
    long r0, c0;
    paint_tf_stats(&r0, &c0);
    sn(a, "scaleX", 0.48); sn(a, "scaleY", 0.48);
    node_invalidate(a);
    render(root, 160, 100);
    long r1, c1;
    paint_tf_stats(&r1, &c1);
    CHECK(r1 == 0 && c1 == 2, "scale change re-rasterized (%ld renders, %ld composites)", r1, c1);
    sn(ac, "backgroundColor", 0xff000000);
    node_invalidate(ac);
    render(root, 160, 100);
    paint_tf_stats(&r1, &c1);
    CHECK(r1 == 1, "child change did not re-rasterize (%ld)", r1);
    CHECK(pix(26, 26) == 0x000000, "child color not updated: %06x", pix(26, 26));

    /* at 150 % the raster is at the display's resolution (sharp edge, no blur band) */
    PaintView keep = paint_view;
    paint_view = (PaintView){ 0, 0, 1.5f, 0, 0, 0 };
    sn(a, "scaleX", 1); sn(a, "scaleY", 1); sn(a, "rotate", 0);
    sn(b, "scaleX", 2); sn(b, "scaleY", 2); sn(b, "rotate", 0);
    node_invalidate(a); node_invalidate(b);
    if (FW != 240) { free(fb); fb = calloc(240 * 150, 4); FW = 240; FH = 150; }
    canvas_init(&frame, fb, 240, 150, 240);
    layout_root(root, 160, 100);
    paint_damage(root);
    ui.damage = (Box){ 0, 0, 160, 100 };
    canvas_clear(&frame, 0);
    paint_root(&frame, root);
    paint_view = keep;
    /* b: 60×10 at (70,45) scaled 2 around (100,50) → x 40..160, y 40..60 logical → 60..240, 60..90 physical */
    int soft = 0;
    for (int x = 100; x < 230; x++) { uint32_t p = pix(x, 61); if (p != 0x00ff00 && p != 0xffffff && p != 0xff0000 && p != 0x000000) soft++; }
    CHECK(pix(150, 75) == 0x00ff00, "scaled bar at 150%%: %06x", pix(150, 75));
    CHECK(soft == 0, "blurry row inside the scaled bar at 150%% (%d px)", soft);

    /* a scaled subtree with a backdrop blur inside is still painted scaled (the Classic task view:
     * a translucent Terminal); before, it was painted at full size */
    if (FW != 160) { free(fb); fb = calloc(160 * 100, 4); FW = 160; FH = 100; }
    canvas_init(&frame, fb, 160, 100, 160);
    sn(a, "scaleX", 0.5); sn(a, "scaleY", 0.5);
    sn(b, "scaleX", 1); sn(b, "scaleY", 1); sn(b, "rotate", 90);
    sn(ac, "backgroundColor", 0x800000ff);
    sn(ac, "backdropBlur", 6);
    node_invalidate(a); node_invalidate(ac); node_invalidate(b);
    render(root, 160, 100);
    CHECK(pix(15, 15) == 0xffffff && pix(45, 30) == 0xffffff, "a blurred child kept its box unscaled: %06x %06x", pix(15, 15), pix(45, 30));
    CHECK(pix(35, 35) == 0xff0000, "the scaled box with a blurred child: %06x", pix(35, 35));
}

/* The overlay scroll bar as ScrollView (sdk/components.jsx, sdk/scrollbar.js) builds it: an
 * overflow-hidden view, the content translated inside it with its normal end padding, and over it a
 * 14 px hit strip at the end edge holding the 6 px thumb 2 px from the edge. The bar is painted
 * over the content (no strip reserved), never over the text, takes the pointer only while shown,
 * fades by opacity and goes to the left in RTL. */
static void test_overlay_scrollbar(void) {
    ui_reset();
    Node *root = view(NULL);
    ui.root = root;
    sn(root, "backgroundColor", 0xffffffff);
    Node *sv = view(root);                               /* the ScrollView: 200 × 100 */
    ss(sv, "position", "absolute");
    sn(sv, "left", 20); sn(sv, "top", 10); sn(sv, "width", 200); sn(sv, "height", 100);
    ss(sv, "overflow", "hidden");
    Node *in = view(sv);                                 /* content: 400 high, 12 px end padding */
    sn(in, "backgroundColor", 0xffddeeff);
    sn(in, "paddingEnd", 12);
    sn(in, "translateY", -40);
    Node *rows[8], *label = NULL;
    for (int i = 0; i < 8; i++) {
        rows[i] = view(in);
        sn(rows[i], "height", 50);
        ss(rows[i], "flexDirection", "row");
        Node *tx = text(rows[i], "Value 1,234 MB", 12);
        sn(tx, "flexGrow", 1); ss(tx, "textAlign", "end"); ss(tx, "whiteSpace", "nowrap");
        if (i == 1) label = tx;
    }
    /* thumb geometry for view 100 / content 400 / offset 40: track 96, size 24, top 2 + 72 * 40/300 */
    float top = 2 + (96 - 24) * (40.0f / 300.0f);
    Node *strip = view(sv);
    ss(strip, "position", "absolute");
    sn(strip, "end", 0); sn(strip, "top", top); sn(strip, "width", 14); sn(strip, "height", 24);
    Node *thumb = view(strip);
    ss(thumb, "position", "absolute");
    sn(thumb, "end", 2); sn(thumb, "top", 0); sn(thumb, "width", 6); sn(thumb, "height", 24);
    sn(thumb, "borderRadius", 3); sn(thumb, "backgroundColor", 0xff808080); sn(thumb, "opacity", 1);
    render(root, 240, 120);

    int tx = (int)(sv->ax + 200 - 2 - 3), ty = (int)(sv->ay + top + 12);   /* the thumb's center */
    CHECK(in->w == 200, "the content keeps the full width (no strip reserved): %g", in->w);
    CHECK(pix(tx, ty) == 0x808080, "thumb painted over the content: %06x", pix(tx, ty));
    CHECK(pix((int)(sv->ax + 199), ty) == 0xddeeff, "2 px inset: the edge shows the content: %06x", pix((int)(sv->ax + 199), ty));
    CHECK(pix((int)(sv->ax + 200 - 8 - 2), ty) == 0xddeeff, "thumb wider than 6 px: %06x", pix((int)(sv->ax + 190), ty));
    CHECK(label && label->ax + label->w <= thumb->ax, "the text (ends at %g) runs under the thumb (starts at %g)",
          label ? label->ax + label->w : -1, thumb->ax);
    CHECK(fabsf(rows[1]->ay - (sv->ay + 50 - 40)) < 0.5f, "content scrolled by translateY: row at %g", rows[1]->ay);
    /* hit testing: the strip takes the pointer (drag), just outside it the content does */
    CHECK(hit_test(root, (float)tx, (float)ty) == thumb || hit_test(root, (float)tx, (float)ty) == strip, "thumb not hit");
    CHECK(hit_test(root, sv->ax + 200 - 13, (float)ty) == strip, "the 14 px strip is not hit at its start");
    CHECK(hit_test(root, sv->ax + 200 - 15, (float)ty) != strip && hit_test(root, sv->ax + 200 - 15, (float)ty) != thumb, "hit outside the strip");
    CHECK(hit_test(root, (float)tx, sv->ay + top + 30) != strip, "hit below the thumb went to the strip");

    /* fading: half opacity blends the thumb with the content */
    sn(thumb, "opacity", 0.5);
    render(root, 240, 120);
    uint32_t p = pix(tx, ty);
    int r = (int)(p >> 16 & 0xff), b = (int)(p & 0xff);
    CHECK(r > 0x90 && r < 0xd0 && b > 0x90 && b < 0xf0, "half-faded thumb: %06x", p);
    /* hidden: opacity 0 and pointerEvents none: nothing painted, the click goes to the content */
    sn(thumb, "opacity", 0);
    ss(strip, "pointerEvents", "none");
    render(root, 240, 120);
    CHECK(pix(tx, ty) == 0xddeeff, "hidden thumb painted: %06x", pix(tx, ty));
    Node *h = hit_test(root, (float)tx, (float)ty);
    CHECK(h != strip && h != thumb, "a hidden bar takes the click");

    /* RTL: the bar is on the left edge */
    sn(thumb, "opacity", 1);
    ss(strip, "pointerEvents", "auto");
    ss(sv, "direction", "rtl");
    node_invalidate(sv);
    render(root, 240, 120);
    CHECK(fabsf(strip->ax - sv->ax) < 0.5f, "RTL strip at the left edge: %g (view at %g)", strip->ax, sv->ax);
    CHECK(pix((int)(sv->ax + 2 + 3), ty) == 0x808080, "RTL thumb painted on the left: %06x", pix((int)(sv->ax + 5), ty));
    CHECK(pix((int)(sv->ax + 200 - 5), ty) != 0x808080, "RTL thumb still on the right");
}

#endif

/* ---- benchmark: a Files-like list and a window drag (docs/DESIGN.md, Engine notes) ---- */
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static double median(double *v, int n) { qsort(v, (size_t)n, sizeof *v, cmp_d); return v[n / 2]; }

static Node *text(Node *parent, const char *s, float size) {
    Node *t = node_new(NODE_TEXT), *r = node_new(NODE_RAW);
    node_set_text(r, s);
    node_append(t, r);
    node_append(parent, t);
    sn(t, "fontSize", size);
    sn(t, "color", 0xff202020);
    return t;
}

typedef struct { int w, h; float x, k; uint32_t *px; Canvas c; } Disp;

static double t_layout, t_damage, t_paint;
static void frame_paint(Node *root, Disp *d, int nd) {
    double q0 = now_ms();
    layout_root(root, 1280, 800);
    double q1 = now_ms();
    Box dmg = { 0, 0, 0, 0 };
    if (paint_damage(root)) dmg = ui.damage;
    double q2 = now_ms();
    t_layout += q1 - q0;
    t_damage += q2 - q1;
    int ln = paint_layer_node();
    if (ln) {
        Node *w = node_get(ln);
        paint_layer_view(w && w->prev_box.x0 + (w->prev_box.x1 - w->prev_box.x0) / 2 >= 1280 / (float)nd * (nd - 1) && nd > 1 ? 1 : 0);
    }
    for (int i = 0; i < nd; i++) {
        if (dmg.x1 <= dmg.x0) break;
        float vx0 = d[i].x, vx1 = d[i].x + d[i].w / d[i].k;
        Box b = { fmaxf(dmg.x0, vx0), fmaxf(dmg.y0, 0), fminf(dmg.x1, vx1), fminf(dmg.y1, d[i].h / d[i].k) };
        if (b.x1 <= b.x0 || b.y1 <= b.y0) continue;
        paint_view = (PaintView){ d[i].x, 0, d[i].k, 0, 0, i };
        Rect r = { (int)floorf((b.x0 - d[i].x) * d[i].k), (int)floorf(b.y0 * d[i].k), (int)ceilf((b.x1 - d[i].x) * d[i].k), (int)ceilf(b.y1 * d[i].k) };
        if (r.x1 > d[i].w) r.x1 = d[i].w;
        if (r.y1 > d[i].h) r.y1 = d[i].h;
        ui.damage = b;
        canvas_set_clip(&d[i].c, r);
        canvas_clear(&d[i].c, 0);
        paint_root(&d[i].c, root);
        canvas_set_clip(&d[i].c, (Rect){ 0, 0, d[i].w, d[i].h });
    }
    paint_view = (PaintView){ 0, 0, 1, 0, 0, 0 };
    ui.damage = (Box){ 0, 0, 0, 0 };
    t_paint += now_ms() - q2;
}

static void bench(void) {
    static const char *const words[] = { "Quarterly", "report", "holiday", "photos", "final", "version", "backup", "draft",
                                         "invoice", "scan", "notes", "presentation", "budget", "summary" };
    quiet = 1;
    ui_reset();
    Node *root = view(NULL);
    ui.root = root;
    sn(root, "backgroundColor", 0xff3a4a5a);
    /* Files: a 900×600 window with a 2000-row list view (icon, name with ellipsis, size, date) */
    Node *win = view(root);
    ss(win, "position", "absolute");
    sn(win, "left", 120); sn(win, "top", 80); sn(win, "width", 900); sn(win, "height", 600);
    sn(win, "backgroundColor", 0xfff5f5f5); sn(win, "borderRadius", 10);
    sn(win, "shadowColor", 0x66000000); sn(win, "shadowBlur", 24); sn(win, "shadowOffsetY", 8);
    ss(win, "overflow", "hidden");
    Node *bar = view(win);
    sn(bar, "height", 44); sn(bar, "backgroundColor", 0xffe8e8e8);
    sn(bar, "borderBottomWidth", 1); sn(bar, "borderBottomColor", 0xffd0d0d0);
    Node *list = view(win);
    sn(list, "flexGrow", 1); ss(list, "overflow", "hidden");
    Node *inner = view(list);
    char name[256];
    srand(7);
    for (int i = 0; i < 2000; i++) {
        Node *row = view(inner);
        ss(row, "flexDirection", "row"); ss(row, "alignItems", "center");
        sn(row, "height", 24); sn(row, "paddingLeft", 10); sn(row, "paddingRight", 10); sn(row, "gap", 8);
        if (i & 1) sn(row, "backgroundColor", 0xffefefef);
        Node *icon = view(row);
        sn(icon, "width", 16); sn(icon, "height", 16); sn(icon, "borderRadius", 3); sn(icon, "backgroundColor", 0xff5b8def);
        int len = 0, nw = 2 + rand() % 9;
        for (int k = 0; k < nw; k++) len += snprintf(name + len, sizeof name - (size_t)len, "%s%s", k ? " " : "", words[rand() % 14]);
        snprintf(name + len, sizeof name - (size_t)len, " %d.pdf", i);
        Node *t = text(row, name, 13);
        sn(t, "flexGrow", 1); sn(t, "flexShrink", 1); sn(t, "flexBasis", 0);
        ss(t, "whiteSpace", "nowrap");
        if (!getenv("BENCH_NO_ELLIPSIS")) ss(t, "textOverflow", "ellipsis");   /* ignored by an engine without it */
        Node *size = text(row, "1.2 MB", 12);
        sn(size, "width", 70); ss(size, "textAlign", "right");
        Node *date = text(row, "18 Oct 2026, 04:20", 12);
        sn(date, "width", 130);
    }
    /* a second window to drag over it (a dialog with some controls) */
    Node *dlg = view(root);
    ss(dlg, "position", "absolute");
    sn(dlg, "left", 300); sn(dlg, "top", 200); sn(dlg, "width", 520); sn(dlg, "height", 360);
    sn(dlg, "backgroundColor", 0xffffffff); sn(dlg, "borderRadius", 10); sn(dlg, "padding", 20); sn(dlg, "gap", 8);
    sn(dlg, "shadowColor", 0x66000000); sn(dlg, "shadowBlur", 24); sn(dlg, "shadowOffsetY", 8);
    for (int i = 0; i < 12; i++) {
        Node *r = view(dlg);
        ss(r, "flexDirection", "row"); sn(r, "height", 20); sn(r, "gap", 8);
        text(r, "Label of the setting", 13);
        Node *f = view(r);
        sn(f, "flexGrow", 1); sn(f, "borderWidth", 1); sn(f, "borderColor", 0xffc0c0c0); sn(f, "borderRadius", 5);
    }
    /* a Dock with blur, above the windows */
    Node *dock = view(root);
    ss(dock, "position", "absolute");
    sn(dock, "left", 340); sn(dock, "bottom", 8); sn(dock, "width", 600); sn(dock, "height", 64);
    sn(dock, "backgroundColor", 0x80ffffff); sn(dock, "borderRadius", 16); sn(dock, "backdropBlur", 20);

    Disp d[2];
    d[0] = (Disp){ 1280, 800, 0, 1, NULL, {0} };
    d[1] = (Disp){ 1920, 1200, 1280, 1.5f, NULL, {0} };
    for (int i = 0; i < 2; i++) { d[i].px = calloc((size_t)d[i].w * d[i].h, 4); canvas_init(&d[i].c, d[i].px, d[i].w, d[i].h, d[i].w); }

    /* layout: cold (every text measured) and warm (cache); medians, the machine may be busy */
    double t0 = now_ms();
    layout_root(root, 1280, 800);
    double cold = now_ms() - t0;
    enum { N = 41 };
    double v[N];
    for (int i = 0; i < N; i++) { node_invalidate(inner); double a = now_ms(); layout_root(root, 1280, 800); v[i] = now_ms() - a; }
    double warm = median(v, N);
    /* full paint of the screen */
    paint_damage(root);
    for (int i = 0; i < N; i++) {
        ui.damage = (Box){ 0, 0, 1280, 800 };
        double a = now_ms();
        canvas_clear(&d[0].c, 0);
        paint_root(&d[0].c, root);
        v[i] = now_ms() - a;
    }
    printf("list: 2000 rows; layout cold %.2f ms, warm %.2f ms; full paint %.2f ms\n", cold, warm, median(v, N));
    /* scrolling the list: move the inner column and repaint (layout + damage + paint) */
    for (int i = 0; i < N; i++) {
        sn(inner, "marginTop", -24.0 * i);
        node_invalidate(inner);
        double a = now_ms();
        frame_paint(root, d, 1);
        v[i] = now_ms() - a;
    }
    printf("scroll frame: %.2f ms\n", median(v, N));

    /* drag: the dialog moves for 240 frames, on one display and on two (the second at 150 %) */
    for (int nd = 1; nd <= 2; nd++) {
        ui.damage = (Box){ 0, 0, 1280 * (float)nd, 800 };
        frame_paint(root, d, nd);
        paint_layer_begin(dlg->id, 0);
        long r, b;
        paint_layer_stats(&r, &b);
        double worst = 0, sum = 0;
        enum { F = 240 };
        static double fv[F];
        t_layout = t_damage = t_paint = 0;
        for (int f = 0; f < F; f++) {
            float ang = f * 0.05f;
            float x = 300 + 260 * cosf(ang) + (nd == 2 ? 340 : 0), y = 200 + 120 * sinf(ang);
            dlg->s.pos[EDGE_L] = (Dim){ x, UNIT_PX };
            dlg->s.pos[EDGE_T] = (Dim){ y, UNIT_PX };
#ifndef ENGINE_BEFORE
            node_moved(dlg);                    /* as grab.c does */
#endif
            ui.dirty = 1;
            double a = now_ms();
            frame_paint(root, d, nd);
            double ms = now_ms() - a;
            fv[f] = ms;
            sum += ms;
            if (f > 0 && ms > worst) worst = ms;
        }
        paint_layer_stats(&r, &b);
        paint_layer_end();
        frame_paint(root, d, nd);
        printf("drag (%d display%s): median %.2f ms per frame, mean %.2f (layout %.2f, damage %.2f, paint %.2f; worst %.2f after the first), layer renders %ld, blits %ld\n",
               nd, nd > 1 ? "s" : "", median(fv, F), sum / F, t_layout / F, t_damage / F, t_paint / F, worst, r, b);
    }
}

int main(int argc, char **argv) {
    const char *dir = getenv("LORD_FONTS") ? getenv("LORD_FONTS") : "../assets/fonts";
    fonts_set_builtin_dir(dir);
    if (text_init(dir)) { printf("engine-test: no fonts in %s\n", dir); return 1; }
    if (argc > 1 && !strcmp(argv[1], "bench")) { bench(); return 0; }
#ifdef ENGINE_BEFORE
    return 0;
#else
    test_ellipsis();
    test_borders();
    test_logical();
    test_transform_math();
    test_transform_paint_hit();
    test_layers();
    test_backdrop_band();
    test_multi_damage();
    test_minimize_damage();
    test_grow_wrap_height();
    test_overlay_scrollbar();
    printf("engine-test: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
#endif
}

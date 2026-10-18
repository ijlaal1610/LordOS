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
    printf("engine-test: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
#endif
}

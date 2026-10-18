/* paint.c — pinta el árbol de nodos en el canvas y resuelve qué nodo está bajo el cursor. */
#include "ui.h"
#include "../term/term.h"
#include "../wl/wl.h"
#include "../media/media.h"
#include "grab.h"
#include "../gpu/gpu.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Perfil por primitiva (solo con --debug). */
int paint_profile;
static double prof[6];
static const char *const prof_name[6] = {"sombra", "blur", "fondo", "imagen", "texto", "borde"};
static double clk(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
#define PROF(i, call) do { double _t = paint_profile ? clk() : 0; call; if (paint_profile) prof[i] += clk() - _t; } while (0)

#define MAX_KIDS 1024

static char text_buf[8192];

/* The display being painted (docs/DISPLAYS.md): layout is in logical px, the canvas is that display's
 * physical frame. Every coordinate, radius, border, shadow, blur and font size goes through it, so
 * text and vectors are rasterized at the display's resolution (150 % is sharp, not upscaled). */
PaintView paint_view = { 0, 0, 1, 0, 0, 0 };
static inline float VX(float x) { return (x - paint_view.x) * paint_view.k + paint_view.dx; }
static inline float VY(float y) { return (y - paint_view.y) * paint_view.k + paint_view.dy; }

static uint32_t with_opacity(uint32_t argb, float op) {
    uint32_t a = (uint32_t)((argb >> 24) * op + 0.5f);
    return a << 24 | (argb & 0xffffff);
}

/* Cuadro de video que tapa todo lo que cambió (media_cover_node): lo anterior a ese nodo en el
 * orden de pintado no se ve, así que no se pinta (solo se recorre, por los recortes). */
static int skip_until, skip_reached;

static int layer_paint(Canvas *c, Node *n, float opacity);

static void paint_node(Canvas *c, Node *n, float opacity) {
    if (n->type == NODE_RAW || n->s.display_none) return;
    if (layer_paint(c, n, opacity)) return;
    const Style *s = &n->s;
    float op = opacity * s->opacity;
    if (op < 0.004f) return;
    const float k = paint_view.k;
    float x = VX(n->ax), y = VY(n->ay), w = n->w * k, h = n->h * k, rad = s->radius * k;
    if (paint_capture_mode && n->protect) {     /* ventana protegida: negra en cualquier captura */
        canvas_rrect(c, x, y, w, h, rad, 0xff000000u);
        return;
    }
    if (skip_until && n->id == skip_until) skip_reached = 1;
    int own = !skip_until || skip_reached;     /* ¿se pinta este nodo (o solo sus hijos)? */

    if (own && (s->shadow_color >> 24) && (s->shadow_blur > 0 || s->shadow_x || s->shadow_y))
        PROF(0, canvas_shadow(c, x, y, w, h, rad, s->shadow_x * k, s->shadow_y * k, s->shadow_blur * k,
                              with_opacity(s->shadow_color, op)));

    Corners corners = {0};
    int round_clip = s->overflow_hidden && s->radius > 0;
    if (round_clip) canvas_corners_save(c, &corners, x, y, w, h, rad);

    if (own && s->backdrop_blur >= 1) PROF(1, canvas_backdrop_blur(c, x, y, w, h, rad, (int)(s->backdrop_blur * k + 0.5f)));
    if (own && s->bg >> 24) PROF(2, canvas_rrect(c, x, y, w, h, rad, with_opacity(s->bg, op)));
    if (own && s->has_gradient) PROF(2, canvas_rrect_gradient(c, x, y, w, h, rad, s->grad_from, s->grad_to, s->grad_angle, op));

    float bw = s->border_width;
    /* content box: logical (Wayland surfaces take logical coordinates and scale themselves) and physical */
    float lix = n->ax + s->padding[EDGE_L] + bw, liy = n->ay + s->padding[EDGE_T] + bw;
    float liw = n->w - s->padding[EDGE_L] - s->padding[EDGE_R] - 2 * bw;
    float lih = n->h - s->padding[EDGE_T] - s->padding[EDGE_B] - 2 * bw;
    float ix = VX(lix), iy = VY(liy), iw = liw * k, ih = lih * k;

    if (!own) goto children;
    if (n->type == NODE_IMAGE && n->img)
        PROF(3, image_draw(c, n->img, ix, iy, iw, ih, (s->radius - bw) * k, s->object_fit, op));
    if (n->type == NODE_TERM) term_paint(c, n->term, ix, iy, iw, ih, op);
    if (n->type == NODE_SURFACE) wl_paint(c, n->term, n->id, lix, liy, liw, lih, op);
    if (n->type == NODE_MEDIA) media_paint(c, n->term, n->id, ix, iy, iw, ih, (s->radius - bw) * k, op);
    if (n->type == NODE_TEXT && (s->color >> 24)) {
        node_gather_text(n, text_buf, sizeof text_buf);
        Rect old = canvas_push_clip(c, x, y, w, h);
        int al = s->text_align;
        if (al == TEXT_START) al = n->rtl ? TEXT_RIGHT : TEXT_LEFT;
        else if (al == TEXT_END) al = n->rtl ? TEXT_LEFT : TEXT_RIGHT;
        /* line breaks come from the logical metrics (the ones layout measured), glyphs at the display's size */
        text_paint_scale = k;
        PROF(4, text_draw(c, text_buf, TEXT_WEIGHT(s), s->font_size * k, s->line_height > 4 ? s->line_height * k : s->line_height,
                          ix, iy, iw, al, s->nowrap, with_opacity(s->color, op)));
        text_paint_scale = 1;
        canvas_set_clip(c, old);
    }

children:;
    Rect old_clip = c->clip;
    if (s->overflow_hidden) canvas_push_clip(c, x, y, w, h);
    if (n->type != NODE_TEXT) {
        Node *kids[MAX_KIDS];
        int k = node_children_ordered(n, kids, MAX_KIDS);
        for (int i = 0; i < k; i++) paint_node(c, kids[i], op);
    }
    canvas_set_clip(c, old_clip);

    if (own && bw > 0 && (s->border_color >> 24))
        PROF(5, canvas_rrect_stroke(c, x, y, w, h, rad, bw * k, with_opacity(s->border_color, op)));
    if (round_clip) canvas_corners_restore(c, &corners, x, y, w, h, rad);
}

/* Área que ocupa un nodo en pantalla, incluida su sombra. */
static Box visual_box(const Node *n) {
    float e = 1;
    const Style *s = &n->s;
    if ((s->shadow_color >> 24) && (s->shadow_blur > 0 || s->shadow_x || s->shadow_y))
        e += s->shadow_blur + fmaxf(fabsf(s->shadow_x), fabsf(s->shadow_y));
    return (Box){ floorf(n->ax - e), floorf(n->ay - e), ceilf(n->ax + n->w + e), ceilf(n->ay + n->h + e) };
}

static int box_eq(Box a, Box b) {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

/* ---- capa de la ventana que se mueve (grab.h) ----
 * Al empezar a mover una ventana su árbol se pinta una vez en un búfer aparte (con su sombra) y
 * en cada cuadro solo se copia en la posición nueva: no se vuelve a pintar texto, terminal ni
 * sombra. Si algo de adentro cambia (estilo, texto, la terminal, una app Linux) la capa se
 * vuelve a pintar. Sin capa (se pinta como siempre): con desenfoque de fondo adentro (necesita
 * lo de abajo) y, en el camino GPU, si adentro hay una ventana de app (va como textura aparte).
 *
 * Lo de abajo (todo lo que se pinta antes que la ventana: fondo, escritorio, las otras ventanas)
 * también se guarda una vez («below»): por cuadro se copia la región que cambió, encima va la capa
 * y después se pinta normal solo lo que está por encima (barra de menú, Dock). Si algo de abajo
 * cambia, se vuelve a armar. */
static struct {
    int node, want, valid, rendering;
    uint32_t *px;
    int w, h, cap;
    float ox, oy;               /* esquina de la capa respecto del nodo (logical px) */
    float pox, poy;             /* the same in physical px of the view it was painted for */
    int view;                   /* paint_view.index of the display that uses the layer (paint_layer_view) */
    float k;                    /* that display's scale when the layer was painted */
    float op;                   /* opacidad con la que se pintó */
    float gdx, gdy;             /* corrimiento del último cuadro (damage_walk) */
    Box at;                     /* dónde estaba en pantalla al pintarla */
    /* lo de abajo */
    uint32_t *below;
    int bw, bh, below_valid, stop, stopped;
    int since, no_below;        /* cuadros desde que se armó; si cambia seguido (un video), no se usa */
    Box part;                   /* parte de la capa a repintar (coordenadas de la capa), p. ej. el cursor de la terminal */
    int resize;                 /* cambiando el tamaño: sin capa, solo lo de abajo */
    uint8_t *hole;              /* camino GPU: dónde la capa tiene el hueco de una app (alfa 0 de verdad) */
    int painted;                /* la capa se pintó en este cuadro (las apps ya quedaron anotadas) */
} layer;

void paint_layer_begin(int node, int resize) {
    layer.node = node;
    layer.want = 1;
    layer.resize = resize;
    layer.valid = 0;
    layer.below_valid = 0;
}

void paint_layer_end(void) {
    if (!layer.node) return;
    Node *n = node_get(layer.node);
    if (n) damage_add(n->prev_box);
    free(layer.px);
    free(layer.below);
    free(layer.hole);
    memset(&layer, 0, sizeof layer);
}

void paint_layer_external_damage(void) { layer.valid = 0; }

/* Several displays: only the one with most of the window uses the layer and the copy of what is
 * below; the others paint the window as usual. Moving to another display rebuilds both. */
int paint_layer_node(void) { return layer.want ? layer.node : 0; }

void paint_layer_view(int index) {
    if (layer.view == index) return;
    layer.view = index;
    layer.valid = 0;
    layer.below_valid = 0;
}

static long layer_renders, layer_blits, below_renders;
void paint_layer_stats(long *renders, long *blits) {
    *renders = layer_renders + 1000 * below_renders;
    *blits = layer_blits;
    layer_renders = layer_blits = below_renders = 0;
}

static void subtree_box(Node *n, Box *b, int *ok) {
    if (n->type == NODE_RAW || n->s.display_none) return;
    if (n->s.backdrop_blur >= 1) *ok = 0;
    if (gpu_active() && n->type == NODE_MEDIA) *ok = 0;
    Box v = visual_box(n);
    if (v.x0 < b->x0) b->x0 = v.x0;
    if (v.y0 < b->y0) b->y0 = v.y0;
    if (v.x1 > b->x1) b->x1 = v.x1;
    if (v.y1 > b->y1) b->y1 = v.y1;
    if (n->type != NODE_TEXT) for (Node *c = n->first; c; c = c->next) subtree_box(c, b, ok);
}

/* Camino GPU: la capa empieza con un valor centinela (alfa 1) y no con 0, así después se distingue el
 * hueco de una app (canvas_clear deja 0) de lo que quedó vacío o las esquinas redondeadas. */
#define SENTINEL 0x01000000u
static void layer_fill(int x0, int y0, int x1, int y1) {
    uint32_t v = gpu_active() ? SENTINEL : 0;
    for (int y = y0; y < y1; y++) {
        uint32_t *row = layer.px + (size_t)y * layer.w;
        if (!v) memset(row + x0, 0, (size_t)(x1 - x0) * 4);
        else for (int x = x0; x < x1; x++) row[x] = v;
    }
}
static void layer_holes(int x0, int y0, int x1, int y1) {
    if (!gpu_active() || !layer.hole) return;
    for (int y = y0; y < y1; y++) {
        uint32_t *row = layer.px + (size_t)y * layer.w;
        uint8_t *m = layer.hole + (size_t)y * layer.w;
        for (int x = x0; x < x1; x++) {
            m[x] = row[x] == 0;
            if (row[x] == SENTINEL) row[x] = 0;
        }
    }
}

/* Vuelve a anotar las ventanas de apps de la capa (camino GPU) en su lugar de ahora, sin tocar el canvas. */
static void layer_register(Canvas *c, Node *n, float op) {
    if (n->type == NODE_RAW || n->s.display_none) return;
    op *= n->s.opacity;
    if (n->type == NODE_SURFACE) {
        const Style *s = &n->s;
        float bw = s->border_width;
        wl_paint(c, n->term, n->id, n->ax + s->padding[EDGE_L] + bw, n->ay + s->padding[EDGE_T] + bw,
                 n->w - s->padding[EDGE_L] - s->padding[EDGE_R] - 2 * bw, n->h - s->padding[EDGE_T] - s->padding[EDGE_B] - 2 * bw, op);
    }
    if (n->type != NODE_TEXT) for (Node *k = n->first; k; k = k->next) layer_register(c, k, op);
}

static int layer_render(Node *n, float opacity) {
    Box b = visual_box(n);
    int ok = 1;
    subtree_box(n, &b, &ok);
    /* the layer's pixels are the view's physical ones */
    int x0 = (int)floorf(VX(b.x0)), y0 = (int)floorf(VY(b.y0));
    int w = (int)ceilf(VX(b.x1)) - x0, h = (int)ceilf(VY(b.y1)) - y0;
    if (!ok || w <= 0 || h <= 0 || (size_t)w * h > 4096u * 4096u) { layer.want = 0; return 0; }
    if ((size_t)w * h > (size_t)layer.cap) {
        uint32_t *px = realloc(layer.px, (size_t)w * h * 4);
        uint8_t *hole = realloc(layer.hole, (size_t)w * h);
        if (px) layer.px = px;
        if (hole) layer.hole = hole;
        if (!px || !hole) { layer.want = 0; return 0; }
        layer.cap = w * h;
    }
    layer.w = w;
    layer.h = h;
    layer_fill(0, 0, w, h);
    layer.ox = b.x0 - n->ax;
    layer.oy = b.y0 - n->ay;
    layer.pox = x0 - VX(n->ax);
    layer.poy = y0 - VY(n->ay);
    layer.k = paint_view.k;
    layer.op = opacity;
    layer.at = b;
    layer.part = (Box){ 0, 0, 0, 0 };
    /* canvas con el origen corrido: el árbol se pinta en sus coordenadas de pantalla (así la terminal
     * y las apps Linux siguen anotando dónde están), pero cae en el búfer de la capa */
    Canvas lc;
    canvas_init(&lc, layer.px - ((ptrdiff_t)y0 * w + x0), x0 + w, y0 + h, w);
    canvas_set_clip(&lc, (Rect){ x0, y0, x0 + w, y0 + h });
    int skip = skip_until;
    skip_until = 0;
    layer.rendering = 1;
    paint_node(&lc, n, opacity);
    layer.rendering = 0;
    skip_until = skip;
    layer_holes(0, 0, w, h);
    layer.valid = 1;
    layer.painted = 1;
    layer_renders++;
    return 1;
}

/* Repinta solo layer.part (lo que cambió adentro: una línea de la terminal, el cursor que parpadea). */
static void layer_render_part(Node *n, float opacity) {
    int x0 = (int)lroundf(VX(n->ax) + layer.pox), y0 = (int)lroundf(VY(n->ay) + layer.poy);
    Box p = layer.part;
    layer.part = (Box){ 0, 0, 0, 0 };
    int px0 = (int)fmaxf(0, floorf(p.x0)), py0 = (int)fmaxf(0, floorf(p.y0));
    int px1 = (int)fminf((float)layer.w, ceilf(p.x1)), py1 = (int)fminf((float)layer.h, ceilf(p.y1));
    if (px0 >= px1 || py0 >= py1) return;
    layer_fill(px0, py0, px1, py1);
    Canvas lc;
    canvas_init(&lc, layer.px - ((ptrdiff_t)y0 * layer.w + x0), x0 + layer.w, y0 + layer.h, layer.w);
    canvas_set_clip(&lc, (Rect){ x0 + px0, y0 + py0, x0 + px1, y0 + py1 });
    int skip = skip_until;
    skip_until = 0;
    layer.rendering = 1;
    paint_node(&lc, n, opacity);
    layer.rendering = 0;
    skip_until = skip;
    layer_holes(px0, py0, px1, py1);
    layer.painted = 1;
    float k = paint_view.k, lx = (x0 - paint_view.dx) / k + paint_view.x, ly = (y0 - paint_view.dy) / k + paint_view.y;
    layer.at = (Box){ lx, ly, lx + layer.w / k, ly + layer.h / k };
    layer_renders++;
}

static int layer_paint(Canvas *c, Node *n, float opacity) {
    if (layer.stop && n->id == layer.stop) layer.stopped = 1;     /* armando lo de abajo: hasta acá */
    if (layer.stopped) return 1;
    if (!layer.want || layer.rendering || n->id != layer.node || paint_capture_mode || layer.resize || paint_view.index != layer.view) return 0;
    if (skip_until && !skip_reached && skip_until != layer.node) return 0;   /* lo tapa un video */
    if ((!layer.valid || layer.op != opacity) && !layer_render(n, opacity)) return 0;
    if (layer.part.x1 > layer.part.x0) layer_render_part(n, opacity);
    if (skip_until == layer.node) skip_reached = 1;
    int bx = (int)lroundf(VX(n->ax) + layer.pox), by = (int)lroundf(VY(n->ay) + layer.poy);
    canvas_blit(c, layer.px, layer.w, layer.h, bx, by, 0, 1, 0);
    if (gpu_active()) {
        /* los huecos de las apps: alfa 0 de verdad (la mezcla no los abre) y la app anotada donde está ahora */
        Rect r = c->clip;
        int x0 = bx > r.x0 ? bx : r.x0, x1 = bx + layer.w < r.x1 ? bx + layer.w : r.x1;
        for (int y = by > r.y0 ? by : r.y0; y < by + layer.h && y < r.y1; y++) {
            const uint8_t *m = layer.hole + (size_t)(y - by) * layer.w - bx;
            uint32_t *row = c->px + (size_t)y * c->stride;
            for (int x = x0; x < x1; x++) if (m[x]) row[x] = 0;
        }
        if (!layer.painted) {
            wl_paint_holes(0);
            layer_register(c, n, opacity);
            wl_paint_holes(1);
        }
    }
    layer.painted = 0;
    layer_blits++;
    return 1;
}

/* Lo de abajo de la ventana, en pantalla completa: se pinta el árbol hasta llegar a ella. En el camino
 * GPU sus ventanas de apps (huecos en la UI) se guardan y se vuelven a anotar en cada cuadro. */
static int below_ready(const Canvas *c, Node *root) {
    if (!layer.want || layer.rendering || paint_capture_mode || paint_view.index != layer.view) return 0;
    layer.since++;
    if (layer.no_below) return 0;
    if (layer.below_valid && layer.bw == c->w && layer.bh == c->h) { wl_paint_kept(); return 1; }
    if (layer.below && layer.since < 8) { layer.no_below = 1; return 0; }   /* lo de abajo cambia a cada rato */
    layer.since = 0;
    if (!layer.below || layer.bw != c->w || layer.bh != c->h) {
        free(layer.below);
        layer.below = malloc((size_t)c->w * c->h * 4);
        if (!layer.below) return 0;
        layer.bw = c->w;
        layer.bh = c->h;
    }
    Canvas bc;
    canvas_init(&bc, layer.below, c->w, c->h, c->w);
    canvas_clear(&bc, 0x000000);
    layer.stop = layer.node;
    layer.stopped = 0;
    paint_node(&bc, root, 1);
    layer.stop = layer.stopped = 0;
    wl_paint_keep();                /* las ventanas de apps de abajo (camino GPU) */
    layer.below_valid = 1;
    below_renders++;
    return 1;
}

static void below_copy(Canvas *c) {
    Rect r = c->clip;
    for (int y = r.y0; y < r.y1; y++)
        memcpy(c->px + (size_t)y * c->stride + r.x0, layer.below + (size_t)y * layer.bw + r.x0, (size_t)(r.x1 - r.x0) * 4);
}

/* in_layer: el nodo está dentro de la ventana que se mueve; ahí un nodo que solo se corrió lo
 * mismo que la ventana no invalida la capa (sí hay que repintar la pantalla donde estaba y donde
 * está). Afuera, cualquier cambio invalida lo de abajo. */
/* clip: lo visible según los ancestros con overflow: hidden. Un hijo que se corre adentro de una
 * caja recortada (el mapa del Monitor al moverlo) solo daña lo que se ve, no lo de alrededor. */
static Box box_and(Box a, Box b) {
    Box r = { fmaxf(a.x0, b.x0), fmaxf(a.y0, b.y0), fminf(a.x1, b.x1), fminf(a.y1, b.y1) };
    return r.x1 > r.x0 && r.y1 > r.y0 ? r : (Box){0, 0, 0, 0};
}

static void damage_walk(Node *n, int parent_dirty, int visible, int in_layer, Box clip) {
    if (n->type == NODE_RAW) { n->paint_dirty = 0; return; }
    int vis = visible && !n->s.display_none;
    int dirty = parent_dirty || n->paint_dirty;
    Box cur = vis ? box_and(visual_box(n), clip) : (Box){0, 0, 0, 0};
    if (layer.want && n->id == layer.node) {
        in_layer = 1;
        layer.gdx = cur.x0 - n->prev_box.x0;
        layer.gdy = cur.y0 - n->prev_box.y0;
    }
    if (dirty || !box_eq(cur, n->prev_box)) {
        damage_add(n->prev_box);
        damage_add(cur);
        Box moved = { n->prev_box.x0 + layer.gdx, n->prev_box.y0 + layer.gdy, n->prev_box.x1 + layer.gdx, n->prev_box.y1 + layer.gdy };
        if (in_layer && (dirty || !box_eq(cur, moved))) layer.valid = 0;
        if (!in_layer) layer.below_valid = 0;
    }
    n->prev_box = cur;
    n->paint_dirty = 0;
    Box inner = n->s.overflow_hidden ? box_and(clip, (Box){ floorf(n->ax), floorf(n->ay), ceilf(n->ax + n->w), ceilf(n->ay + n->h) }) : clip;
    for (Node *c = n->first; c; c = c->next) damage_walk(c, dirty, vis, in_layer, inner);
}

static int box_inside(Box a, Box b) { return a.x0 >= b.x0 && a.y0 >= b.y0 && a.x1 <= b.x1 && a.y1 <= b.y1; }
static int box_meets(Box a, Box b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

int paint_damage(Node *root) {
    /* lo que llegó de afuera desde el cuadro anterior (terminal, apps Linux, video): si toca la
     * ventana, se vuelve a pintar su capa; si cae afuera, lo de abajo */
    Box ext = ui.damage;
    if (layer.want && ext.x1 > ext.x0 && ext.y1 > ext.y0) {
        Node *n = node_get(layer.node);
        Box now = n ? n->prev_box : layer.at;
        if (box_inside(ext, layer.at)) {        /* adentro, donde se pintó: solo esa parte */
            float k = layer.k > 0 ? layer.k : 1, e = k != 1 ? 1 : 0;      /* layer coordinates are physical */
            Box q = { floorf((ext.x0 - layer.at.x0) * k) - e, floorf((ext.y0 - layer.at.y0) * k) - e,
                      ceilf((ext.x1 - layer.at.x0) * k) + e, ceilf((ext.y1 - layer.at.y0) * k) + e };
            Box *p = &layer.part;
            if (p->x1 <= p->x0) *p = q;
            else { p->x0 = fminf(p->x0, q.x0); p->y0 = fminf(p->y0, q.y0); p->x1 = fmaxf(p->x1, q.x1); p->y1 = fmaxf(p->y1, q.y1); }
        } else if (box_meets(ext, layer.at) || box_meets(ext, now)) layer.valid = 0;
        if (!box_inside(ext, layer.at) && !box_inside(ext, now)) layer.below_valid = 0;
    }
    if (root) damage_walk(root, 0, 1, 0, (Box){ -1e9f, -1e9f, 1e9f, 1e9f });
    if (layer.want && layer.valid && layer.part.x1 > layer.part.x0) {     /* esa parte, donde está ahora la ventana */
        Node *n = node_get(layer.node);
        if (n) {
            float x0 = roundf(n->ax + layer.ox), y0 = roundf(n->ay + layer.oy), k = layer.k > 0 ? layer.k : 1;
            damage_add((Box){ floorf(x0 + layer.part.x0 / k), floorf(y0 + layer.part.y0 / k), ceilf(x0 + layer.part.x1 / k), ceilf(y0 + layer.part.y1 / k) });
        }
    }
    Box *d = &ui.damage;
    return d->x1 > d->x0 && d->y1 > d->y0;
}

/* El nodo opaco de más arriba que tapa toda la región dañada (fondo opaco, opacidad 1, sin
 * desenfoque de fondo; la caja sin las esquinas redondeadas y recortada por sus ancestros): lo
 * anterior a él (fondo de pantalla, sombras, ventanas de atrás) no se ve y no se pinta. Con un
 * mapa o una lista que se mueve adentro de una tarjeta, se pinta desde la tarjeta. */
static void cover_walk(Node *n, Box d, Box clip, int *found) {
    const Style *s = &n->s;
    if (n->type == NODE_RAW || s->display_none || s->opacity < 0.999f) return;
    Box box = box_and((Box){ n->ax, n->ay, n->ax + n->w, n->ay + n->h }, clip);
    if ((s->bg >> 24) == 0xff && !s->has_gradient && s->backdrop_blur < 1) {
        float k = s->radius * 0.3f;             /* adentro de las esquinas redondeadas */
        Box in = { box.x0 + k, box.y0 + k, box.x1 - k, box.y1 - k };
        if (d.x0 >= in.x0 && d.y0 >= in.y0 && d.x1 <= in.x1 && d.y1 <= in.y1) *found = n->id;
    }
    if (n->type == NODE_TEXT) return;
    Box inner = s->overflow_hidden ? box : clip;
    Node *kids[MAX_KIDS];
    int k = node_children_ordered(n, kids, MAX_KIDS);
    for (int i = 0; i < k; i++) cover_walk(kids[i], d, inner, found);
}

static int opaque_cover_node(Node *root) {
    int found = 0;
    if (ui.damage.x1 > ui.damage.x0) cover_walk(root, ui.damage, (Box){ -1e9f, -1e9f, 1e9f, 1e9f }, &found);
    return found == root->id ? 0 : found;
}

void paint_root(Canvas *c, Node *root) {
    if (!root) return;
    wl_paint_begin();
    /* solo cambió un cuadro de video que tapa esa región: se pinta desde ese nodo (no en el camino
     * GPU, que arma acá la lista de ventanas de las apps) */
    skip_until = gpu_active() ? 0 : media_cover_node(ui.damage.x0, ui.damage.y0, ui.damage.x1, ui.damage.y1);
    if (!skip_until && !gpu_active() && !layer.want) skip_until = opaque_cover_node(root);
    skip_reached = 0;
    /* una ventana que se mueve: lo de abajo se copia y se pinta desde ella (capa del arrastre) */
    if (!skip_until && below_ready(c, root)) {
        below_copy(c);
        skip_until = layer.node;
    }
    paint_node(c, root, 1);
    skip_until = 0;
    wl_paint_popups(c);                     /* menús de las apps Wayland, encima de todo */
    if (paint_profile) {
        fprintf(stderr, "  paint:");
        for (int i = 0; i < 6; i++) { fprintf(stderr, " %s %.2f", prof_name[i], prof[i]); prof[i] = 0; }
        fprintf(stderr, " ms\n");
    }
}

int paint_capture_mode;

void paint_capture(Canvas *c, Node *root) {
    if (!root) return;
    paint_capture_mode = 1;
    paint_node(c, root, 1);
    wl_paint_popups(c);
    paint_capture_mode = 0;
}

static Node *hit(Node *n, float x, float y) {
    if (n->type == NODE_RAW || n->s.display_none || n->s.pointer_none) return NULL;
    int inside = x >= n->ax && y >= n->ay && x < n->ax + n->w && y < n->ay + n->h;
    if (n->s.overflow_hidden && !inside) return NULL;
    if (n->type != NODE_TEXT) {
        Node *kids[MAX_KIDS];
        int k = node_children_ordered(n, kids, MAX_KIDS);
        for (int i = k - 1; i >= 0; i--) {
            Node *h = hit(kids[i], x, y);
            if (h) return h;
        }
    }
    return inside ? n : NULL;
}

Node *hit_test(Node *root, float x, float y) {
    return root ? hit(root, x, y) : NULL;
}

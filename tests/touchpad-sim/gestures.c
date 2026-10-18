/* gestures.c — gestos guionados del touchpad simulado (ver gestures.h). Las posiciones están en
 * mm desde la esquina de arriba a la izquierda del touchpad; cada cuadro dura 12,5 ms (80 Hz). */
#include "gestures.h"

#include <linux/input.h>
#include <math.h>
#include <string.h>

typedef struct { double x, y; int p; } Pt;

#define P_FINGER 60
#define P_PALM 250

static void ev(SimGen *g, int type, int code, int value) { g->emit(g->ud, g->t, type, code, value); }

void sim_init(SimGen *g, SimEmit emit, void *ud) {
    memset(g, 0, sizeof *g);
    g->emit = emit;
    g->ud = ud;
    g->next_id = 100;
    g->frame_ms = 12.5;
    for (int i = 0; i < SIM_SLOTS; i++) g->id[i] = -1;
}

void sim_wait(SimGen *g, double ms) { g->t += ms; }

static int ux(double mm) { int v = (int)lround(SIM_X_MIN + mm * SIM_RES); return v < SIM_X_MIN ? SIM_X_MIN : v > SIM_X_MAX ? SIM_X_MAX : v; }
static int uy(double mm) { int v = (int)lround(SIM_Y_MIN + mm * SIM_RES); return v < SIM_Y_MIN ? SIM_Y_MIN : v > SIM_Y_MAX ? SIM_Y_MAX : v; }

/* Un cuadro con n contactos (en los slots 0..n-1) y el botón del clickpad. */
static void frame(SimGen *g, int n, const Pt *pts, int button) {
    static const int tools[] = { 0, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP, BTN_TOOL_QUADTAP, BTN_TOOL_QUINTTAP };
    for (int i = 0; i < SIM_SLOTS; i++) {
        int want = i < n;
        if (want) {
            int x = ux(pts[i].x), y = uy(pts[i].y), p = pts[i].p;
            if (!g->down[i]) {
                ev(g, EV_ABS, ABS_MT_SLOT, i);
                g->id[i] = g->next_id++;
                ev(g, EV_ABS, ABS_MT_TRACKING_ID, g->id[i]);
                ev(g, EV_ABS, ABS_MT_POSITION_X, x);
                ev(g, EV_ABS, ABS_MT_POSITION_Y, y);
                ev(g, EV_ABS, ABS_MT_PRESSURE, p);
            } else if (x != g->x[i] || y != g->y[i] || p != g->p[i]) {
                ev(g, EV_ABS, ABS_MT_SLOT, i);
                if (x != g->x[i]) ev(g, EV_ABS, ABS_MT_POSITION_X, x);
                if (y != g->y[i]) ev(g, EV_ABS, ABS_MT_POSITION_Y, y);
                if (p != g->p[i]) ev(g, EV_ABS, ABS_MT_PRESSURE, p);
            }
            g->down[i] = 1;
            g->x[i] = x;
            g->y[i] = y;
            g->p[i] = p;
        } else if (g->down[i]) {
            ev(g, EV_ABS, ABS_MT_SLOT, i);
            ev(g, EV_ABS, ABS_MT_TRACKING_ID, -1);
            g->down[i] = 0;
            g->id[i] = -1;
        }
    }
    /* emulación de un solo dedo (como el kernel): el primer contacto */
    if (n > 0) {
        ev(g, EV_ABS, ABS_X, g->x[0]);
        ev(g, EV_ABS, ABS_Y, g->y[0]);
        ev(g, EV_ABS, ABS_PRESSURE, g->p[0]);
    }
    int tool = n > 5 ? 5 : n;
    if (tool != g->tool) {
        if (g->tool) ev(g, EV_KEY, tools[g->tool], 0);
        if (!g->tool && tool) ev(g, EV_KEY, BTN_TOUCH, 1);
        if (tool) ev(g, EV_KEY, tools[tool], 1);
        else ev(g, EV_KEY, BTN_TOUCH, 0);
        g->tool = tool;
    }
    if (button != g->button) { ev(g, EV_KEY, BTN_LEFT, button); g->button = button; }
    ev(g, EV_SYN, SYN_REPORT, 0);
    g->t += g->frame_ms;
}

static double ease(double u) { return u * u * (3 - 2 * u); }

/* n dedos de from[] a to[] en ms (con aceleración y frenado suaves). */
static void stroke(SimGen *g, int n, const Pt *from, const Pt *to, double ms, int button) {
    int frames = (int)(ms / g->frame_ms);
    if (frames < 1) frames = 1;
    Pt p[SIM_SLOTS];
    for (int k = 0; k <= frames; k++) {
        double u = ease((double)k / frames);
        for (int i = 0; i < n; i++) {
            p[i].x = from[i].x + (to[i].x - from[i].x) * u;
            p[i].y = from[i].y + (to[i].y - from[i].y) * u;
            p[i].p = from[i].p;
        }
        frame(g, n, p, button);
    }
}

/* lo mismo pero a velocidad constante (para un scroll que se suelta en movimiento) */
static void stroke_linear(SimGen *g, int n, const Pt *from, const Pt *to, double ms) {
    int frames = (int)(ms / g->frame_ms);
    Pt p[SIM_SLOTS];
    for (int k = 0; k <= frames; k++) {
        double u = (double)k / frames;
        for (int i = 0; i < n; i++) {
            p[i].x = from[i].x + (to[i].x - from[i].x) * u;
            p[i].y = from[i].y + (to[i].y - from[i].y) * u;
            p[i].p = from[i].p;
        }
        frame(g, n, p, 0);
    }
}

static void hold(SimGen *g, int n, const Pt *at, double ms, int button) { stroke(g, n, at, at, ms, button); }
static void lift(SimGen *g) { frame(g, 0, NULL, g->button); }

/* ---- gestos ---- */

static void g_move(SimGen *g) {
    Pt a[] = { { 30, 45, P_FINGER } }, b[] = { { 75, 52, P_FINGER } };
    stroke(g, 1, a, b, 400, 0);
    lift(g);
}
static void g_move_back(SimGen *g) {
    Pt a[] = { { 75, 52, P_FINGER } }, b[] = { { 30, 45, P_FINGER } };
    stroke(g, 1, a, b, 400, 0);
    lift(g);
}
static void tapn(SimGen *g, int n) {
    Pt a[] = { { 42, 40, P_FINGER }, { 56, 41, P_FINGER }, { 70, 42, P_FINGER } };
    hold(g, n, a, 50, 0);
    lift(g);
}
static void g_tap(SimGen *g) { tapn(g, 1); sim_wait(g, 300); }
static void g_tap2(SimGen *g) { tapn(g, 2); sim_wait(g, 300); }
static void g_tap3(SimGen *g) { tapn(g, 3); sim_wait(g, 300); }
static void g_doubletap(SimGen *g) { tapn(g, 1); sim_wait(g, 70); tapn(g, 1); sim_wait(g, 300); }
static void g_tapdrag(SimGen *g) {
    tapn(g, 1);
    sim_wait(g, 70);
    Pt a[] = { { 42, 40, P_FINGER } }, b[] = { { 80, 50, P_FINGER } };
    stroke(g, 1, a, b, 450, 0);
    lift(g);
    sim_wait(g, 300);
}
static void clickn(SimGen *g, int n) {
    Pt a[] = { { 42, 40, P_FINGER }, { 56, 41, P_FINGER }, { 70, 42, P_FINGER } };
    hold(g, n, a, 40, 0);
    hold(g, n, a, 80, 1);
    hold(g, n, a, 30, 0);
    lift(g);
}
static void g_click(SimGen *g) { clickn(g, 1); }
static void g_click2(SimGen *g) { clickn(g, 2); }
static void g_click3(SimGen *g) { clickn(g, 3); }
static void g_thumbclick(SimGen *g) {          /* pulgar apoyado abajo + índice: clic izquierdo */
    Pt a[] = { { 50, 82, P_FINGER }, { 45, 40, P_FINGER } };
    hold(g, 2, a, 60, 0);
    hold(g, 2, a, 80, 1);
    hold(g, 2, a, 30, 0);
    lift(g);
}
static void g_scroll(SimGen *g) {             /* dos dedos hacia arriba, rápido, y se sueltan en movimiento */
    Pt a[] = { { 45, 65, P_FINGER }, { 60, 66, P_FINGER } }, b[] = { { 45, 30, P_FINGER }, { 60, 31, P_FINGER } };
    stroke_linear(g, 2, a, b, 220);
    lift(g);
    sim_wait(g, 1500);
}
static void g_scroll_down(SimGen *g) {
    Pt a[] = { { 45, 30, P_FINGER }, { 60, 31, P_FINGER } }, b[] = { { 45, 65, P_FINGER }, { 60, 66, P_FINGER } };
    stroke_linear(g, 2, a, b, 220);
    lift(g);
    sim_wait(g, 1500);
}
static void g_scroll_slow(SimGen *g) {        /* despacio y se frena antes de soltar: sin inercia */
    Pt a[] = { { 45, 60, P_FINGER }, { 60, 61, P_FINGER } }, b[] = { { 45, 40, P_FINGER }, { 60, 41, P_FINGER } };
    stroke(g, 2, a, b, 800, 0);
    hold(g, 2, b, 200, 0);
    lift(g);
    sim_wait(g, 300);
}
static void g_pinch_out(SimGen *g) {
    Pt a[] = { { 46, 45, P_FINGER }, { 56, 40, P_FINGER } }, b[] = { { 22, 60, P_FINGER }, { 80, 26, P_FINGER } };
    stroke(g, 2, a, b, 500, 0);
    lift(g);
    sim_wait(g, 200);
}
static void g_pinch_in(SimGen *g) {
    Pt a[] = { { 22, 60, P_FINGER }, { 80, 26, P_FINGER } }, b[] = { { 46, 45, P_FINGER }, { 56, 40, P_FINGER } };
    stroke(g, 2, a, b, 500, 0);
    lift(g);
    sim_wait(g, 200);
}
static void g_rotate(SimGen *g) {             /* dos dedos girando 60° alrededor del centro */
    int frames = 40;
    for (int k = 0; k <= frames; k++) {
        double ang = (-20 + 60.0 * ease((double)k / frames)) * M_PI / 180;
        Pt p[2] = { { 52 - 15 * cos(ang), 44 - 15 * sin(ang), P_FINGER }, { 52 + 15 * cos(ang), 44 + 15 * sin(ang), P_FINGER } };
        frame(g, 2, p, 0);
    }
    lift(g);
    sim_wait(g, 200);
}
static void swipe(SimGen *g, int n, double dx, double dy) {
    Pt a[4] = { { 32, 55, P_FINGER }, { 46, 52, P_FINGER }, { 60, 52, P_FINGER }, { 74, 55, P_FINGER } }, b[4];
    for (int i = 0; i < n; i++) { b[i] = a[i]; b[i].x += dx; b[i].y += dy; }
    stroke(g, n, a, b, 300, 0);
    lift(g);
    sim_wait(g, 300);
}
static void g_swipe3_up(SimGen *g) { swipe(g, 3, 0, -35); }
static void g_swipe3_down(SimGen *g) { swipe(g, 3, 0, 30); }
static void g_swipe3_left(SimGen *g) { swipe(g, 3, -28, 0); }
static void g_swipe3_right(SimGen *g) { swipe(g, 3, 28, 0); }
static void g_swipe4_up(SimGen *g) { swipe(g, 4, 0, -35); }
static void g_palm(SimGen *g) {               /* palma: contacto con mucha presión en el borde */
    Pt a[] = { { 101, 40, P_PALM } }, b[] = { { 100, 70, P_PALM } };
    stroke(g, 1, a, b, 600, 0);
    lift(g);
    sim_wait(g, 300);
}
static void g_edge(SimGen *g) {               /* un toque que se queda en el borde izquierdo */
    Pt a[] = { { 3, 25, P_FINGER } }, b[] = { { 3, 65, P_FINGER } };
    hold(g, 1, a, 320, 0);
    stroke(g, 1, a, b, 500, 0);
    lift(g);
    sim_wait(g, 300);
}
static void g_threedrag(SimGen *g) {          /* con «arrastrar con tres dedos» */
    Pt a[] = { { 40, 50, P_FINGER }, { 54, 47, P_FINGER }, { 68, 50, P_FINGER } }, b[3];
    for (int i = 0; i < 3; i++) { b[i] = a[i]; b[i].x += 30; b[i].y -= 10; }
    stroke(g, 3, a, b, 500, 0);
    lift(g);
    sim_wait(g, 700);
}

static const SimGesture list[] = {
    { "move", "1 dedo: mover el puntero a la derecha", g_move },
    { "move-back", "1 dedo: mover el puntero a la izquierda", g_move_back },
    { "tap", "tocar con 1 dedo (clic)", g_tap },
    { "tap2", "tocar con 2 dedos (clic secundario)", g_tap2 },
    { "tap3", "tocar con 3 dedos (clic del medio)", g_tap3 },
    { "doubletap", "doble toque", g_doubletap },
    { "tapdrag", "tocar y arrastrar", g_tapdrag },
    { "click", "clic físico con 1 dedo", g_click },
    { "click2", "clic físico con 2 dedos (secundario)", g_click2 },
    { "click3", "clic físico con 3 dedos (medio)", g_click3 },
    { "thumbclick", "pulgar apoyado abajo + índice: clic izquierdo", g_thumbclick },
    { "scroll", "2 dedos hacia arriba, rápido (inercia)", g_scroll },
    { "scroll-down", "2 dedos hacia abajo, rápido", g_scroll_down },
    { "scroll-slow", "2 dedos despacio, sin inercia", g_scroll_slow },
    { "pinch-out", "separar 2 dedos (ampliar)", g_pinch_out },
    { "pinch-in", "juntar 2 dedos (reducir)", g_pinch_in },
    { "rotate", "girar 2 dedos 60°", g_rotate },
    { "swipe3-up", "3 dedos hacia arriba", g_swipe3_up },
    { "swipe3-down", "3 dedos hacia abajo", g_swipe3_down },
    { "swipe3-left", "3 dedos a la izquierda", g_swipe3_left },
    { "swipe3-right", "3 dedos a la derecha", g_swipe3_right },
    { "swipe4-up", "4 dedos hacia arriba", g_swipe4_up },
    { "palm", "palma en el borde derecho (se ignora)", g_palm },
    { "edge", "toque quieto en el borde izquierdo (se ignora)", g_edge },
    { "threedrag", "arrastrar con 3 dedos", g_threedrag },
};

const SimGesture *sim_gestures(int *n) { *n = (int)(sizeof list / sizeof list[0]); return list; }

const SimGesture *sim_find(const char *name) {
    for (size_t i = 0; i < sizeof list / sizeof list[0]; i++) if (!strcmp(list[i].name, name)) return &list[i];
    return NULL;
}

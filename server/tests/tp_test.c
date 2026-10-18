/* tp_test.c — prueba del driver de touchpad (src/tp/tp.c) sin dispositivo: le pasa las mismas
 * secuencias evdev que reproduce el touchpad simulado (tests/touchpad-sim/gestures.c), con sus
 * tiempos, y revisa lo que reconoce (puntero, clics, scroll con inercia, pellizco, deslizar,
 * palmas, ignorar al escribir, pantalla táctil). También mide cuánto tarda en reconocer cada
 * gesto (en tiempo del gesto) y cuánto cuesta procesar un cuadro.
 *
 *   build/tp-test                 todas las pruebas
 *   build/tp-test --replay F.ev   reproduce una grabación (tpsim record/dump) y muestra lo que salió */
#include "../src/tp/tp.h"
#include "../../tests/touchpad-sim/gestures.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { O_MOTION, O_BUTTON, O_SCROLL, O_PINCH, O_SWIPE, O_HOLD, O_TOUCH, O_WARP };
typedef struct { int kind, phase, a, b; double t, dx, dy, scale, rot; } Out;

static Out outs[20000];
static int nout;
static double cur_t;
static int touch_consume;

static void put(int kind, int phase, int a, int b, double dx, double dy, double sc, double rot) {
    if (nout < (int)(sizeof outs / sizeof outs[0])) outs[nout++] = (Out){ kind, phase, a, b, cur_t, dx, dy, sc, rot };
}
static void r_motion(void *u, double dx, double dy, double rx, double ry) { (void)u; (void)rx; (void)ry; put(O_MOTION, 0, 0, 0, dx, dy, 0, 0); }
static void r_button(void *u, int b, int d) { (void)u; put(O_BUTTON, 0, b, d, 0, 0, 0, 0); }
static void r_scroll(void *u, int ph, double dx, double dy) { (void)u; put(O_SCROLL, ph, 0, 0, dx, dy, 0, 0); }
static void r_pinch(void *u, int ph, int n, double dx, double dy, double sc, double rot) { (void)u; put(O_PINCH, ph, n, 0, dx, dy, sc, rot); }
static void r_swipe(void *u, int ph, int n, double dx, double dy) { (void)u; put(O_SWIPE, ph, n, 0, dx, dy, 0, 0); }
static void r_hold(void *u, int ph, int n) { (void)u; put(O_HOLD, ph, n, 0, 0, 0, 0, 0); }
static int r_touch(void *u, int ph, int id, double x, double y) { (void)u; put(O_TOUCH, ph, id, 0, x, y, 0, 0); return touch_consume; }
static void r_warp(void *u, double x, double y) { (void)u; put(O_WARP, 0, 0, 0, x, y, 0, 0); }
static const TpSink sink = { r_motion, r_button, r_scroll, r_pinch, r_swipe, r_hold, r_touch, NULL, r_warp, NULL };

static TpDevice *dev;
static double last_tick;
static double proc_ns, proc_frames;

static double ns_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}

/* el bucle de lord-server: los temporizadores corren entre eventos */
static void ticks_until(double t) {
    for (;;) {
        double w = tp_timeout(last_tick);
        if (w < 0 || last_tick + w > t) break;
        last_tick += w < 1 ? 1 : w;
        cur_t = last_tick;
        tp_tick(last_tick);
    }
    last_tick = t;
}

static void feed(void *ud, double t, int type, int code, int value) {
    (void)ud;
    ticks_until(t);
    cur_t = t;
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = (unsigned short)type;
    ev.code = (unsigned short)code;
    ev.value = value;
    double t0 = ns_now();
    tp_event_at(dev, &ev, t);
    if (type == EV_SYN) { proc_ns += ns_now() - t0; proc_frames++; }
}

static TpCaps sim_caps(int kind) {
    TpCaps c;
    memset(&c, 0, sizeof c);
    c.kind = kind;
    c.clickpad = kind == TP_KIND_TOUCHPAD;
    c.nslots = SIM_SLOTS;
    c.max_fingers = 5;
    c.x = (struct input_absinfo){ .minimum = SIM_X_MIN, .maximum = SIM_X_MAX, .resolution = SIM_RES };
    c.y = (struct input_absinfo){ .minimum = SIM_Y_MIN, .maximum = SIM_Y_MAX, .resolution = SIM_RES };
    c.pressure = (struct input_absinfo){ .minimum = 0, .maximum = SIM_PMAX };
    c.has_pressure = 1;
    snprintf(c.name, sizeof c.name, "%s", SIM_NAME);
    return c;
}

static TpConfig defaults(void) {
    return (TpConfig){ .speed = 0.5, .tap = 1, .tap_drag = 1, .natural = 1, .three_drag = 0, .dwt = 1, .scroll = 1,
                       .kinetic = 1, .pinch = 1, .swipe3 = 1, .swipe4 = 0 };
}

static double base_t = 100000;      /* cada prueba empieza más tarde (lejos de la última tecla) */

static void run(const char *name, int kind, const TpConfig *cfg) {
    TpConfig c = cfg ? *cfg : defaults();
    tp_set_config(&c);
    TpCaps caps = sim_caps(kind);
    dev = tp_new(&caps);
    nout = 0;
    SimGen g;
    sim_init(&g, feed, NULL);
    g.t = base_t;
    last_tick = base_t;
    const SimGesture *gs = sim_find(name);
    if (!gs) { fprintf(stderr, "sin gesto %s\n", name); exit(2); }
    gs->play(&g);
    ticks_until(g.t + 3000);
    base_t = g.t + 10000;
    tp_free(dev);
    dev = NULL;
}

/* ---- resúmenes ---- */

typedef struct { double mx, my, sx, sy, wx, wy, scale, rot, swx, swy; int nmotion, nscroll, nmom, mom_end, pinch_b, pinch_e, swipe_b, swipe_e, fingers, holds;
                 int down[3], up[3]; char btns[64]; double t_first, t_scroll, t_pinch, t_swipe; } Sum;

static Sum summary(void) {
    Sum s;
    memset(&s, 0, sizeof s);
    s.scale = 1;
    s.t_first = nout ? outs[0].t : 0;
    int nb = 0;
    for (int i = 0; i < nout; i++) {
        Out *o = &outs[i];
        switch (o->kind) {
        case O_MOTION: s.mx += o->dx; s.my += o->dy; s.nmotion++; break;
        case O_BUTTON:
            if (o->b) s.down[o->a]++; else s.up[o->a]++;
            if (nb < 60) { s.btns[nb++] = (char)("LMR"[o->a] + (o->b ? 0 : 32)); s.btns[nb] = 0; }
            break;
        case O_SCROLL:
            if (o->phase == TP_BEGIN && !s.t_scroll) s.t_scroll = o->t;
            if (o->phase == TP_MOMENTUM || o->phase == TP_MOMENTUM_END) { s.nmom++; s.wx += o->dx; s.wy += o->dy; if (o->phase == TP_MOMENTUM_END) s.mom_end++; }
            else { s.sx += o->dx; s.sy += o->dy; if (o->phase == TP_UPDATE) s.nscroll++; }
            break;
        case O_PINCH:
            if (o->phase == TP_BEGIN) { s.pinch_b++; s.t_pinch = o->t; }
            if (o->phase == TP_END) s.pinch_e++;
            if (o->phase == TP_UPDATE || o->phase == TP_END) { s.scale = o->scale; s.rot = o->rot; }
            break;
        case O_SWIPE:
            if (o->phase == TP_BEGIN) { s.swipe_b++; s.fingers = o->a; s.t_swipe = o->t; }
            if (o->phase == TP_END) s.swipe_e++;
            s.swx += o->dx;
            s.swy += o->dy;
            break;
        case O_HOLD: if (o->phase == TP_BEGIN) s.holds++; break;
        case O_WARP: s.wx = o->dx; s.wy = o->dy; break;
        }
    }
    return s;
}

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  FALLA %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void print_sum(const char *name, const Sum *s) {
    printf("%-13s puntero %+6.0f,%+5.0f  botones %-8s scroll %+6.0f,%+6.0f (+inercia %+6.0f, %d)  pellizco %.2f %+4.0f°  deslizar %d dedos %+5.0f,%+5.0f\n",
           name, s->mx, s->my, s->btns[0] ? s->btns : "-", s->sx, s->sy, s->wy, s->nmom, s->scale, s->rot, s->fingers, s->swx, s->swy);
}

static Sum go(const char *name, const TpConfig *c) { run(name, TP_KIND_TOUCHPAD, c); Sum s = summary(); print_sum(name, &s); return s; }

static void t_pointer(void) {
    Sum s = go("move", NULL);
    CHECK(s.mx > 150 && s.mx < 1200, "45 mm a la derecha: %.0f px", s.mx);
    CHECK(fabs(s.my) < s.mx * 0.3, "casi horizontal: %.0f", s.my);
    CHECK(!s.btns[0] && !s.nscroll, "sin clics ni scroll");
    Sum b = go("move-back", NULL);
    CHECK(b.mx < -150, "a la izquierda: %.0f", b.mx);
    /* más velocidad en el ajuste = más píxeles */
    TpConfig fast = defaults();
    fast.speed = 1;
    Sum f = go("move", &fast);
    CHECK(f.mx > s.mx * 1.5, "velocidad máxima mueve más: %.0f vs %.0f", f.mx, s.mx);
    CHECK(tp_accel(5, 0.5) < tp_accel(100, 0.5) && tp_accel(100, 0.5) < tp_accel(300, 0.5) + 1e-9, "curva creciente");
}

static void t_taps(void) {
    Sum s = go("tap", NULL);
    CHECK(!strcmp(s.btns, "Ll"), "tocar = clic izquierdo (%s)", s.btns);
    s = go("tap2", NULL);
    CHECK(!strcmp(s.btns, "Rr"), "2 dedos = derecho (%s)", s.btns);
    s = go("tap3", NULL);
    CHECK(!strcmp(s.btns, "Mm"), "3 dedos = medio (%s)", s.btns);
    s = go("doubletap", NULL);
    CHECK(!strcmp(s.btns, "LlLl"), "doble toque (%s)", s.btns);
    s = go("tapdrag", NULL);
    CHECK(!strcmp(s.btns, "Ll") && s.mx > 100, "tocar y arrastrar: %s, %.0f px con el botón apretado", s.btns, s.mx);
    /* el botón queda apretado durante el movimiento */
    int down_at = -1, up_at = -1, moved_between = 0;
    for (int i = 0; i < nout; i++) {
        if (outs[i].kind == O_BUTTON && outs[i].b && down_at < 0) down_at = i;
        else if (outs[i].kind == O_BUTTON && !outs[i].b) up_at = i;
        else if (outs[i].kind == O_MOTION && down_at >= 0 && up_at < 0) moved_between++;
    }
    CHECK(moved_between > 10, "movimientos con el botón apretado: %d", moved_between);
    TpConfig off = defaults();
    off.tap = 0;
    s = go("tap", &off);
    CHECK(!s.btns[0], "tocar apagado: sin clic (%s)", s.btns);
}

static void t_clicks(void) {
    Sum s = go("click", NULL);
    CHECK(!strcmp(s.btns, "Ll"), "clic físico con 1 dedo (%s)", s.btns);
    s = go("click2", NULL);
    CHECK(!strcmp(s.btns, "Rr"), "clic físico con 2 dedos = derecho (%s)", s.btns);
    s = go("click3", NULL);
    CHECK(!strcmp(s.btns, "Mm"), "clic físico con 3 dedos = medio (%s)", s.btns);
    s = go("thumbclick", NULL);
    CHECK(!strcmp(s.btns, "Ll"), "pulgar apoyado + índice = izquierdo (%s)", s.btns);
    TpConfig area = defaults();
    area.secondary = TP_CLICK_BOTTOM_RIGHT;
    s = go("click2", &area);
    CHECK(!strcmp(s.btns, "Ll"), "modo esquina: 2 dedos arriba = izquierdo (%s)", s.btns);
}

static void t_scroll(void) {
    Sum s = go("scroll", NULL);
    CHECK(s.nscroll > 5 && s.sy > 100, "dedos arriba + natural = bajar el contenido: %.0f px", s.sy);
    CHECK(fabs(s.sx) < 10, "sin horizontal: %.0f", s.sx);
    CHECK(s.nmom > 5 && s.wy > 50 && s.mom_end == 1, "inercia: %d eventos, %.0f px, fin %d", s.nmom, s.wy, s.mom_end);
    CHECK(!s.nmotion && !s.btns[0], "no mueve el puntero ni hace clic");
    printf("  scroll reconocido a los %.0f ms de apoyar\n", s.t_scroll - s.t_first);
    Sum d = go("scroll-down", NULL);
    CHECK(d.sy < -100, "hacia abajo: %.0f", d.sy);
    TpConfig trad = defaults();
    trad.natural = 0;
    Sum t = go("scroll", &trad);
    CHECK(t.sy < -100, "sin natural: al revés (%.0f)", t.sy);
    Sum slow = go("scroll-slow", NULL);
    CHECK(slow.sy > 30 && slow.nmom == 0, "despacio y frenado: sin inercia (%d)", slow.nmom);
    TpConfig nok = defaults();
    nok.kinetic = 0;
    Sum k = go("scroll", &nok);
    CHECK(k.nmom == 0, "inercia apagada");
}

static void t_pinch(void) {
    Sum s = go("pinch-out", NULL);
    CHECK(s.pinch_b == 1 && s.pinch_e == 1, "pellizco begin/end: %d/%d", s.pinch_b, s.pinch_e);
    CHECK(s.scale > 2.0, "separar: escala %.2f", s.scale);
    CHECK(!s.nscroll && !s.nmotion, "no es scroll ni puntero");
    printf("  pellizco reconocido a los %.0f ms de apoyar\n", s.t_pinch - s.t_first);
    s = go("pinch-in", NULL);
    CHECK(s.scale < 0.5, "juntar: escala %.2f", s.scale);
    s = go("rotate", NULL);
    CHECK(s.pinch_b == 1 && s.rot > 45 && s.rot < 75, "girar 60°: %.0f°", s.rot);
    CHECK(fabs(s.scale - 1) < 0.15, "girar no cambia la escala: %.2f", s.scale);
    TpConfig off = defaults();
    off.pinch = 0;
    s = go("pinch-out", &off);
    CHECK(s.pinch_b == 0, "pellizco apagado");
}

static void t_swipe(void) {
    Sum s = go("swipe3-up", NULL);
    CHECK(s.swipe_b == 1 && s.swipe_e == 1 && s.fingers == 3 && s.swy < -100, "3 dedos arriba: %d dedos, %.0f", s.fingers, s.swy);
    CHECK(!s.btns[0] && !s.nmotion && !s.nscroll, "solo el gesto");
    printf("  deslizar reconocido a los %.0f ms de apoyar\n", s.t_swipe - s.t_first);
    s = go("swipe3-left", NULL);
    CHECK(s.swx < -100 && fabs(s.swy) < fabs(s.swx) / 2, "3 dedos izquierda: %.0f", s.swx);
    s = go("swipe3-right", NULL);
    CHECK(s.swx > 100, "3 dedos derecha: %.0f", s.swx);
    s = go("swipe3-down", NULL);
    CHECK(s.swy > 100, "3 dedos abajo: %.0f", s.swy);
    s = go("swipe4-up", NULL);
    CHECK(s.fingers == 4 && s.swy < -100, "4 dedos: %d", s.fingers);
    TpConfig td = defaults();
    td.three_drag = 1;
    s = go("threedrag", &td);
    CHECK(!strcmp(s.btns, "Ll") && s.mx > 100 && !s.swipe_b, "arrastrar con 3 dedos: %s, %.0f px", s.btns, s.mx);
}

static void t_palm(void) {
    Sum s = go("palm", NULL);
    CHECK(!s.nmotion && !s.btns[0], "palma ignorada: %d movimientos, %s", s.nmotion, s.btns);
    s = go("edge", NULL);
    CHECK(fabs(s.my) < 5 && !s.btns[0], "toque quieto en el borde ignorado: %.0f", s.my);
    /* ignorar al escribir: una tecla justo antes */
    tp_key_activity(KEY_A, base_t - 100 + 0);
    double kt = base_t;
    tp_key_activity(KEY_A, kt);
    Sum d = go("move", NULL);
    CHECK(!d.nmotion, "al escribir no se mueve: %d", d.nmotion);
    tp_key_activity(KEY_LEFTCTRL, base_t);          /* los modificadores no cuentan */
    d = go("move", NULL);
    CHECK(d.nmotion > 0, "Ctrl no bloquea");
    TpConfig nodwt = defaults();
    nodwt.dwt = 0;
    tp_key_activity(KEY_A, base_t);
    d = go("move", &nodwt);
    CHECK(d.nmotion > 0, "con dwt apagado sí se mueve");
}

static void t_hold(void) {
    go("scroll-slow", NULL);
    int b = 0, c = 0;
    for (int i = 0; i < nout; i++) if (outs[i].kind == O_HOLD) { if (outs[i].phase == TP_BEGIN) b++; else if (outs[i].phase == TP_CANCEL) c++; }
    CHECK(b >= 1 && c >= 1, "mantener empieza al apoyar y se cancela al desplazar: %d/%d", b, c);
}

static void t_touchscreen(void) {
    touch_consume = 0;
    run("tap", TP_KIND_TOUCHSCREEN, NULL);
    Sum s = summary();
    print_sum("pantalla:tap", &s);
    CHECK(!strcmp(s.btns, "Ll") && s.wx > 0.3 && s.wx < 0.5, "tocar = clic donde se toca (%s en %.2f)", s.btns, s.wx);
    run("move", TP_KIND_TOUCHSCREEN, NULL);
    s = summary();
    print_sum("pantalla:move", &s);
    CHECK(!strcmp(s.btns, "Ll") && s.wx > 0.65, "arrastrar: botón apretado y el puntero sigue al dedo (%.2f)", s.wx);
    run("scroll", TP_KIND_TOUCHSCREEN, NULL);
    s = summary();
    print_sum("pantalla:scroll", &s);
    CHECK(s.sy > 100 && !s.btns[0], "dos dedos arriba = bajar el contenido: %.0f", s.sy);
    run("pinch-out", TP_KIND_TOUCHSCREEN, NULL);
    s = summary();
    print_sum("pantalla:pinch", &s);
    CHECK(s.scale > 2, "pellizco: %.2f", s.scale);
    touch_consume = 1;                       /* hay una app Wayland con wl_touch debajo */
    run("tap", TP_KIND_TOUCHSCREEN, NULL);
    s = summary();
    int down = 0, up = 0;
    for (int i = 0; i < nout; i++) if (outs[i].kind == O_TOUCH) { if (outs[i].phase == TP_BEGIN) down++; if (outs[i].phase == TP_END) up++; }
    CHECK(down == 1 && up == 1 && !s.btns[0], "a la app: toque sin emular el puntero (%d/%d, %s)", down, up, s.btns);
    touch_consume = 0;
}

static int replay(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 1; }
    TpCaps caps = sim_caps(TP_KIND_TOUCHPAD);
    char line[256];
    dev = NULL;
    nout = 0;
    TpConfig c = defaults();
    tp_set_config(&c);
    while (fgets(line, sizeof line, f)) {
        int a, b, cc, d, e, g;
        if (sscanf(line, "# caps x %d %d %d y %d %d %d", &a, &b, &cc, &d, &e, &g) == 6) {
            caps.x = (struct input_absinfo){ .minimum = a, .maximum = b, .resolution = cc };
            caps.y = (struct input_absinfo){ .minimum = d, .maximum = e, .resolution = g };
            continue;
        }
        if (line[0] == '#') continue;
        double t;
        int type, code, value;
        if (sscanf(line, "%lf %d %d %d", &t, &type, &code, &value) != 4) continue;
        if (!dev) { dev = tp_new(&caps); last_tick = t; }
        feed(NULL, t + 1000, type, code, value);
    }
    fclose(f);
    if (!dev) return 1;
    ticks_until(last_tick + 3000);
    Sum s = summary();
    print_sum(path, &s);
    return 0;
}

int main(int argc, char **argv) {
    tp_set_sink(&sink, NULL);
    tp_set_screen(1280, 800);
    if (argc == 3 && !strcmp(argv[1], "--replay")) return replay(argv[2]);
    t_pointer();
    t_taps();
    t_clicks();
    t_scroll();
    t_pinch();
    t_swipe();
    t_palm();
    t_hold();
    t_touchscreen();
    printf("procesar un cuadro: %.2f µs (promedio de %.0f cuadros)\n", proc_ns / proc_frames / 1000, proc_frames);
    printf("tp-test: %d/%d bien\n", checks - fails, checks);
    return fails ? 1 : 0;
}

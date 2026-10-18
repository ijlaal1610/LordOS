/* appclient.c — «modo app»: `lord-server --app <id>` corre una app .lpk instalada como su propio
 * proceso, aislado (server/src/sec/sandbox.c), y se dibuja como un CLIENTE Wayland del compositor
 * de LordOS. El tema le pone su marco (pide decoración del servidor, como las apps Qt), aparece
 * en el Dock y se ve por el escritorio remoto, igual que cualquier app.
 *
 * Reusa el motor de interfaz propio (ui/: nodos, layout, pintado, texto) y QuickJS (js.c), igual
 * que el compositor, pero sin KMS ni evdev: dibuja en un búfer de memoria compartida (wl_shm) y
 * recibe la entrada del compositor. Las llamadas lord.* que no son de dibujo van por el broker
 * (ui/js_broker.c), y lord-server verifica los permisos de cada una (server/src/sec).
 *
 * Lo lanza lord-server (tercera excepción acotada a «el servidor no ejecuta comandos», ver
 * AGENTS.md): el único argumento es un id validado de un paquete instalado. */
#include "app.h"
#include "../ui/ui.h"
#include "../ui/js.h"
#include "../ui/js_sys.h"
#include "../ui/js_broker.h"
#include "../canvas.h"
#include "../keyboard.h"
#include "../lpk.h"
#include "../sec/sandbox.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "client/xdg-shell-client-protocol.h"
#include "client/xdg-decoration-unstable-v1-client-protocol.h"
#include "client/pointer-gestures-unstable-v1-client-protocol.h"

static struct wl_display *dpy;
static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_seat *seat;
static struct wl_pointer *pointer;
static struct wl_keyboard *keyboard;
static struct zxdg_decoration_manager_v1 *decman;
static struct wl_surface *surf;
static struct xdg_surface *xsurf;
static struct xdg_toplevel *top;

static int W = 640, H = 480, cw, ch, configured, closed, need_render = 1, frame_pending;
static double px_hx, px_hy;     /* última posición del puntero */
static int ptr_down[8], ptr_btn[8], nptr;

typedef struct { struct wl_buffer *b; uint32_t *px; size_t size; int busy, w, h; } Buf;
static Buf bufs[2];

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

/* ---- wl_shm doble búfer ---- */

static void buf_release(void *d, struct wl_buffer *b) { (void)b; ((Buf *)d)->busy = 0; }
static const struct wl_buffer_listener buf_listener = { buf_release };

static int make_buffer(Buf *b, int w, int h) {
    if (b->b) { wl_buffer_destroy(b->b); b->b = NULL; }
    if (b->px) { munmap(b->px, b->size); b->px = NULL; }
    size_t stride = (size_t)w * 4, size = stride * h;
    int fd = memfd_create("lordos-app", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size)) { if (fd >= 0) close(fd); return -1; }
    b->px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (b->px == MAP_FAILED) { b->px = NULL; close(fd); return -1; }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    b->b = wl_shm_pool_create_buffer(pool, 0, w, h, (int32_t)stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(b->b, &buf_listener, b);
    b->size = size;
    b->w = w;
    b->h = h;
    b->busy = 0;
    return 0;
}

static const struct wl_callback_listener fl;    /* definido abajo */

/* Layout y pintado de la UI en el búfer. */
static void render(void) {
    Buf *b = !bufs[0].busy ? &bufs[0] : !bufs[1].busy ? &bufs[1] : NULL;
    if (!b || cw <= 0 || ch <= 0) { need_render = 1; return; }
    if (b->w != cw || b->h != ch) { if (make_buffer(b, cw, ch)) return; }
    Canvas c;
    canvas_init(&c, b->px, cw, ch, cw);
    ui.width = (float)cw;
    ui.height = (float)ch;
    layout_root(ui.root, (float)cw, (float)ch);
    canvas_clear(&c, 0x00000000);
    paint_root(&c, ui.root);
    b->busy = 1;
    wl_surface_attach(surf, b->b, 0, 0);
    wl_surface_damage_buffer(surf, 0, 0, cw, ch);
    struct wl_callback *cb = wl_surface_frame(surf);
    wl_callback_add_listener(cb, &fl, NULL);
    frame_pending = 1;
    wl_surface_commit(surf);
    ui.dirty = 0;
    need_render = 0;
}

static void frame_done(void *d, struct wl_callback *cb, uint32_t t) { (void)d; (void)t; wl_callback_destroy(cb); frame_pending = 0; }
static const struct wl_callback_listener fl = { frame_done };

/* ---- xdg ---- */

static void xs_configure(void *d, struct xdg_surface *x, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(x, serial);
    if (!cw || !ch) { cw = W; ch = H; }
    configured = 1;
    need_render = 1;
}
static const struct xdg_surface_listener xs_listener = { xs_configure };

static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st) {
    (void)d; (void)t; (void)st;
    if (w > 0 && h > 0) { cw = w; ch = h; }
}
static void top_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; closed = 1; }
static void top_cfg_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h) { (void)d; (void)t; (void)w; (void)h; }
static void top_caps(void *d, struct xdg_toplevel *t, struct wl_array *c) { (void)d; (void)t; (void)c; }
static const struct xdg_toplevel_listener top_listener = { top_configure, top_close, top_cfg_bounds, top_caps };

static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t serial) { (void)d; xdg_wm_base_pong(w, serial); }
static const struct xdg_wm_base_listener wm_listener = { wm_ping };

/* ---- entrada ---- */

static void dispatch_pointer(void) {
    Node *target = hit_test(ui.root, (float)px_hx, (float)px_hy);
    int id = target ? target->id : 0;
    js_pointer("move", id, (float)px_hx, (float)px_hy, 0);
    for (int i = 0; i < nptr; i++) js_pointer(ptr_down[i] ? "down" : "up", id, (float)px_hx, (float)px_hy, ptr_btn[i]);
    nptr = 0;
}

static void p_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)s; (void)sf; px_hx = wl_fixed_to_double(x); px_hy = wl_fixed_to_double(y);
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf) { (void)d; (void)p; (void)s; (void)sf; }
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)t; px_hx = wl_fixed_to_double(x); px_hy = wl_fixed_to_double(y); dispatch_pointer();
}
static void p_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st) {
    (void)d; (void)p; (void)s; (void)t;
    int button = b == BTN_LEFT ? 0 : b == BTN_MIDDLE ? 1 : b == BTN_RIGHT ? 2 : -1;
    if (button >= 0 && nptr < 8) { ptr_down[nptr] = st == WL_POINTER_BUTTON_STATE_PRESSED; ptr_btn[nptr] = button; nptr++; }
    dispatch_pointer();
}
/* touchpad (axis_source finger): scroll fino con fases, como en lord-server (docs/TRACKPAD.md) */
static int axis_src = -1, fine_on;
static void p_source(void *d, struct wl_pointer *p, uint32_t src) { (void)d; (void)p; axis_src = (int)src; }
static void p_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {
    (void)d; (void)p; (void)t; (void)a;
    if (!fine_on) return;
    fine_on = 0;
    Node *n = hit_test(ui.root, (float)px_hx, (float)px_hy);
    js_gesture("scroll", "end", n ? n->id : 0, (float)px_hx, (float)px_hy, 0, 0, 0, 0);
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {
    (void)d; (void)p; (void)t;
    if (axis_src == WL_POINTER_AXIS_SOURCE_FINGER) {
        Node *n = hit_test(ui.root, (float)px_hx, (float)px_hy);
        if (!fine_on) { fine_on = 1; js_gesture("scroll", "begin", n ? n->id : 0, (float)px_hx, (float)px_hy, 0, 0, 0, 0); }
        double val = wl_fixed_to_double(v);
        int h = a == WL_POINTER_AXIS_HORIZONTAL_SCROLL;
        js_gesture("scroll", "update", n ? n->id : 0, (float)px_hx, (float)px_hy, h ? val : 0, h ? 0 : val, 0, 0);
        return;
    }
    if (a == WL_POINTER_AXIS_VERTICAL_SCROLL) { Node *n = hit_test(ui.root, (float)px_hx, (float)px_hy); js_pointer("wheel", n ? n->id : 0, (float)px_hx, (float)px_hy, wl_fixed_to_double(v) > 0 ? -1 : 1); }
}
static void p_frame(void *d, struct wl_pointer *p) { (void)d; (void)p; axis_src = -1; }
static void p_noop2(void *d, struct wl_pointer *p, uint32_t a, int32_t b) { (void)d; (void)p; (void)a; (void)b; }
static const struct wl_pointer_listener p_listener = { p_enter, p_leave, p_motion, p_button, p_axis, p_frame, p_source, p_stop, p_noop2, p_noop2, NULL };

/* pellizco y deslizar del touchpad (zwp_pointer_gestures_v1) → onPinch / onSwipe de la app */
static struct zwp_pointer_gestures_v1 *gestures;
static double g_rot;
static int g_node(void) { Node *n = hit_test(ui.root, (float)px_hx, (float)px_hy); return n ? n->id : 0; }
static void pi_begin(void *d, struct zwp_pointer_gesture_pinch_v1 *g, uint32_t s, uint32_t t, struct wl_surface *sf, uint32_t f) {
    (void)d; (void)g; (void)s; (void)t; (void)sf; (void)f;
    g_rot = 0;
    js_gesture("pinch", "begin", g_node(), (float)px_hx, (float)px_hy, 1, 0, 0, 0);
}
static void pi_update(void *d, struct zwp_pointer_gesture_pinch_v1 *g, uint32_t t, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t sc, wl_fixed_t rot) {
    (void)d; (void)g; (void)t;
    g_rot += wl_fixed_to_double(rot);
    js_gesture("pinch", "update", g_node(), (float)px_hx, (float)px_hy, wl_fixed_to_double(sc), g_rot, wl_fixed_to_double(dx), wl_fixed_to_double(dy));
}
static void pi_end(void *d, struct zwp_pointer_gesture_pinch_v1 *g, uint32_t s, uint32_t t, int32_t cancelled) {
    (void)d; (void)g; (void)s; (void)t;
    js_gesture("pinch", cancelled ? "cancel" : "end", g_node(), (float)px_hx, (float)px_hy, 1, g_rot, 0, 0);
}
static const struct zwp_pointer_gesture_pinch_v1_listener pinch_listener = { pi_begin, pi_update, pi_end };
static int sw_fingers;
static void sw_begin(void *d, struct zwp_pointer_gesture_swipe_v1 *g, uint32_t s, uint32_t t, struct wl_surface *sf, uint32_t f) {
    (void)d; (void)g; (void)s; (void)t; (void)sf;
    sw_fingers = (int)f;
    js_gesture("swipe", "begin", g_node(), (float)px_hx, (float)px_hy, 0, 0, f, 0);
}
static void sw_update(void *d, struct zwp_pointer_gesture_swipe_v1 *g, uint32_t t, wl_fixed_t dx, wl_fixed_t dy) {
    (void)d; (void)g; (void)t;
    js_gesture("swipe", "update", g_node(), (float)px_hx, (float)px_hy, wl_fixed_to_double(dx), wl_fixed_to_double(dy), sw_fingers, 0);
}
static void sw_end(void *d, struct zwp_pointer_gesture_swipe_v1 *g, uint32_t s, uint32_t t, int32_t cancelled) {
    (void)d; (void)g; (void)s; (void)t;
    js_gesture("swipe", cancelled ? "cancel" : "end", g_node(), (float)px_hx, (float)px_hy, 0, 0, sw_fingers, 0);
}
static const struct zwp_pointer_gesture_swipe_v1_listener swipe_listener = { sw_begin, sw_update, sw_end };
static void gestures_setup(void) {
    static int done;
    if (done || !gestures || !pointer) return;
    done = 1;
    zwp_pointer_gesture_pinch_v1_add_listener(zwp_pointer_gestures_v1_get_pinch_gesture(gestures, pointer), &pinch_listener, NULL);
    zwp_pointer_gesture_swipe_v1_add_listener(zwp_pointer_gestures_v1_get_swipe_gesture(gestures, pointer), &swipe_listener, NULL);
}

static void k_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t sz) { (void)d; (void)k; (void)fmt; (void)sz; if (fd >= 0) close(fd); }
static void k_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf, struct wl_array *keys) { (void)d; (void)k; (void)s; (void)sf; (void)keys; }
static void k_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf) { (void)d; (void)k; (void)s; (void)sf; }
static void k_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t st) {
    (void)d; (void)k; (void)s; (void)t;
    js_key((int)key, st == WL_KEYBOARD_KEY_STATE_PRESSED ? 1 : 0);
}
static void k_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp) {
    (void)d; (void)k; (void)s; (void)dep; (void)lat; (void)lock; (void)grp;   /* los modificadores salen de las propias teclas */
}
static void k_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay) { (void)d; (void)k; (void)rate; (void)delay; }
static const struct wl_keyboard_listener k_listener = { k_keymap, k_enter, k_leave, k_key, k_mods, k_repeat };

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps) {
    (void)d;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) { pointer = wl_seat_get_pointer(s); wl_pointer_add_listener(pointer, &p_listener, NULL); gestures_setup(); }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) { keyboard = wl_seat_get_keyboard(s); wl_keyboard_add_listener(keyboard, &k_listener, NULL); }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_listener = { seat_caps, seat_name };

/* ---- registry ---- */

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver) {
    (void)d;
    if (!strcmp(iface, "wl_compositor")) comp = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
    else if (!strcmp(iface, "wl_shm")) shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, "xdg_wm_base")) { wm = wl_registry_bind(r, name, &xdg_wm_base_interface, ver < 6 ? ver : 6); xdg_wm_base_add_listener(wm, &wm_listener, NULL); }
    else if (!strcmp(iface, "wl_seat")) { seat = wl_registry_bind(r, name, &wl_seat_interface, ver < 7 ? ver : 7); wl_seat_add_listener(seat, &seat_listener, NULL); }
    else if (!strcmp(iface, "zxdg_decoration_manager_v1")) decman = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
    else if (!strcmp(iface, "zwp_pointer_gestures_v1")) { gestures = wl_registry_bind(r, name, &zwp_pointer_gestures_v1_interface, 1); gestures_setup(); }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) { (void)d; (void)r; (void)name; }
static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

int app_main(const char *id, const char *theme_bundle, const char *fonts) {
    if (!lpk_valid_id(id)) { fprintf(stderr, "lord-server --app: id inválido\n"); return 2; }
    const char *root = lpk_root();
    char dir[PATH_MAX];
    struct stat st;
    if (!root || (size_t)snprintf(dir, sizeof dir, "%s/%s", root, id) >= sizeof dir || stat(dir, &st) || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "lord-server --app: la app '%s' no está instalada\n", id);
        return 2;
    }
    /* carpeta de datos de la app (la única que puede escribir) */
    const char *home = getenv("HOME");
    char data[PATH_MAX], parent[PATH_MAX - 48];
    snprintf(parent, sizeof parent, "%s/.local/share/lordos/appdata", home ? home : "/nonexistent");
    mkdir(parent, 0700);
    snprintf(data, sizeof data, "%s/%.40s", parent, id);
    mkdir(data, 0700);

    if (text_init(fonts)) { fprintf(stderr, "lord-server --app: sin fuentes\n"); return 1; }
    { char a[PATH_MAX * 2]; snprintf(a, sizeof a, "%.*s/..", (int)(sizeof a - 4), fonts); js_sys_set_assets(a); }
    kb_set("us", "");

    dpy = wl_display_connect(NULL);
    if (!dpy) { fprintf(stderr, "lord-server --app: no se pudo conectar al compositor\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!comp || !shm || !wm) { fprintf(stderr, "lord-server --app: el compositor no ofrece lo necesario\n"); return 1; }

    surf = wl_compositor_create_surface(comp);
    xsurf = xdg_wm_base_get_xdg_surface(wm, surf);
    xdg_surface_add_listener(xsurf, &xs_listener, NULL);
    top = xdg_surface_get_toplevel(xsurf);
    xdg_toplevel_add_listener(top, &top_listener, NULL);
    xdg_toplevel_set_app_id(top, id);        /* el Dock lo reconoce por su id */
    xdg_toplevel_set_title(top, id);
    if (decman) {                            /* que el tema dibuje el marco (como Qt) */
        struct zxdg_toplevel_decoration_v1 *deco = zxdg_decoration_manager_v1_get_toplevel_decoration(decman, top);
        zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
    wl_surface_commit(surf);
    wl_display_roundtrip(dpy);               /* primer configure */

    /* Caja de arena: después de conectar Wayland y abrir el broker, antes de cargar la app. */
    js_broker_set_pkgdir(dir);               /* la carpeta del paquete, antes de la caja de arena */
    int net = js_broker_wants_network();     /* del manifest + el permiso */
    /* el «app host» (React + SDK) es código del SO; su carpeta es de solo lectura */
    char hostdir[PATH_MAX];
    snprintf(hostdir, sizeof hostdir, "%s", theme_bundle);
    char *slash = strrchr(hostdir, '/');
    if (slash) *slash = 0; else snprintf(hostdir, sizeof hostdir, ".");
    char langs[PATH_MAX];                    /* paquetes de idioma del usuario (árabe, francés…): solo lectura */
    snprintf(langs, sizeof langs, "%s/.local/share/lordos/locales", home ? home : "/nonexistent");
    const char *ro[] = { fonts, hostdir, "/usr/share/fonts", langs, NULL };   /* + fuentes del sistema (emoji, respaldo por escritura) */
    SandboxConfig sb = { .id = id, .pkg_dir = dir, .data_dir = data, .runtime_dir = getenv("XDG_RUNTIME_DIR"), .ro_extra = ro, .network = net };
    sandbox_apply(&sb);

    if (js_init_app(theme_bundle, id)) { fprintf(stderr, "lord-server --app: no se pudo cargar la app\n"); return 1; }

    int wfd = wl_display_get_fd(dpy);
    double start = now_ms();
    (void)start;
    while (!closed && !ui.quit_requested) {
        double delay = -1;
        for (int i = 0; i < 64; i++) { delay = js_tick(now_ms()); if (delay != 0) break; }
        if (js_wants_frame()) { js_frame(now_ms()); }
        if (configured && (need_render || ui.dirty) && !frame_pending) render();
        wl_display_flush(dpy);
        struct pollfd pfd[2];
        int n = 0;
        pfd[n].fd = wfd; pfd[n].events = POLLIN; n++;
        int bfd = js_broker_fd();
        if (bfd >= 0) { pfd[n].fd = bfd; pfd[n].events = POLLIN; n++; }
        int wait = delay < 0 ? 100 : (int)ceil(delay);
        if ((need_render || ui.dirty) && !frame_pending && configured) wait = 0;
        if (wl_display_prepare_read(dpy) != 0) { wl_display_dispatch_pending(dpy); continue; }
        if (poll(pfd, (nfds_t)n, wait) < 0 && errno != EINTR) { wl_display_cancel_read(dpy); break; }
        if (pfd[0].revents & POLLIN) wl_display_read_events(dpy); else wl_display_cancel_read(dpy);
        wl_display_dispatch_pending(dpy);
        if (n > 1 && (pfd[1].revents & POLLIN)) js_broker_poll();
        if (pfd[0].revents & (POLLHUP | POLLERR)) break;
    }
    js_shutdown();
    wl_display_disconnect(dpy);
    return 0;
}

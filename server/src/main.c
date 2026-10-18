/*
 * lord-server — servidor gráfico de LordOS, escrito desde cero.
 *
 * Toma la placa de video (DRM/KMS) y el input (evdev) directamente del kernel, sin X11,
 * Wayland, libdrm ni libinput. La interfaz la define un tema React que corre en QuickJS
 * y construye un árbol de nodos nativos (ui/); el servidor hace layout, pinta y envía
 * los eventos de mouse/teclado de vuelta al tema.
 *
 * Arranca como root (sudo) solo para abrir pantalla, input y consola; después corre como
 * el usuario de la sesión conservando únicamente CAP_SYS_TTY_CONFIG.
 *
 * La terminal (term/) también es propia: emulador xterm, PTY y dibujo en C.
 *
 * Salir: Ctrl+Alt+Backspace, "Cerrar sesión" del tema, SIGINT/SIGTERM o --timeout <segundos>.
 * Con una señal o --timeout los shells de la Terminal quedan vivos (term/hold.c) y se
 * recuperan en el próximo arranque; las dos primeras formas los cierran.
 */
#include "admin.h"
#include "net/net.h"
#include "disk/disk.h"
#include "backup/backup.h"
#include "sound/snd.h"
#include "keyboard.h"
#include "ui/js_sys.h"
#include "canvas.h"
#include "input.h"
#include "kms.h"
#include "drmcard.h"
#include "term/term.h"
#include "ui/js.h"
#include "ui/ui.h"
#include "wl/wl.h"
#include "gpu/gpu.h"
#include "remote/mirror.h"
#include "tp/tp.h"
#include "tp/tp_route.h"
#include "wl/wl_input.h"
#include "media/media.h"
#include "app/app.h"
#include "sec/broker.h"
#include "cursor.h"
#include "ui/grab.h"
#include "ui/js_wm.h"
#include "ui/portal.h"
#include "ui/js_logs.h"
#include "hw/hw.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/capability.h>
#include <linux/kd.h>
#include <math.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

extern int paint_profile;

#define MAX_BUTTONS 16

typedef struct { int down, button; } ButtonEvent;

/* Medición de arrastres (docs/VENTANAS.md): con un botón apretado y el puntero en movimiento se
 * cuentan los cuadros y el trabajo de cada uno (JS del puntero + layout + pintado + presentar). Al
 * soltar se escribe una línea "arrastre: …" en el registro de la sesión. */
typedef struct {
    int held, active;
    double t0, last, js, work_js, work, work_max, gap_max, paint, present, layout;
    long frames, moves;
} DragStats;
static DragStats dstat;

typedef struct {
    Kms kms;
    Input input;
    Canvas scene;           /* UI pintada, sin cursor */
    int W, H;
    int cx, cy, ctrl, alt;
    int moved, wheel, cursor_dirty;
    ButtonEvent buttons[MAX_BUTTONS];
    int nbuttons;
    int quit, debug;
    long frames, renders;
    double layout_total, paint_total;
    /* puntero: movimiento crudo (relative-pointer) y última posición (bloqueo) */
    double rdx, rdy;
    int lx, ly;
    /* cursor: el de la app (wl_cursor) y si va por el plano de cursor de KMS (camino GPU) */
    unsigned cursor_gen;
    int cursor_mode, cursor_hx, cursor_hy, hw_cursor;
    GpuTex *cursor_tex;
    int cursor_tex_w, cursor_tex_h;
    /* sesión sin monitor (la muestra lord-sessiond en el monitor o el escritorio remoto): se
     * dibuja en memoria, sin DRM ni evdev; greeter = la pantalla de ingreso */
    int headless, greeter;
    uint32_t *frame;
    double last_render;
    /* GPU apagada porque alguien mira desde la red: vuelve cuando no queda nadie */
    int gpu_paused;
    /* movimiento del puntero juntado hasta el próximo cuadro; botones apretados (máscara); forma
     * del cursor según el nodo bajo el puntero (estilo `cursor`, docs/VENTANAS.md) */
    int motion_pending, held, shape, over_app;
    /* camino CPU: cada búfer de KMS solo recibe lo que cambió desde la última vez que se dibujó en
     * él (lo repintado en los dos últimos cuadros y donde estaba el cursor), no la escena entera */
    Rect stale[2], curs[2];
    int full_copy, was_fs;
} Server;

static Rect rect_join(Rect a, Rect b) {
    if (a.x1 <= a.x0 || a.y1 <= a.y0) return b;
    if (b.x1 <= b.x0 || b.y1 <= b.y0) return a;
    return (Rect){ a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1 };
}

/* Cursor que se ve: el de la app Linux bajo el puntero, o una forma propia (cursor.c).
 * mode 0 = la flecha de LordOS, 1 = imagen, 2 = oculto (como wl_cursor). */
typedef struct { int mode; const uint32_t *px; int w, h, hx, hy; unsigned gen; } Cur;

static void cursor_now(const Server *s, Cur *c) {
    c->mode = wl_cursor(&c->px, &c->w, &c->h, &c->hx, &c->hy, &c->gen);
    int grabbing = grab_active();
    if (c->mode && !grabbing && (s->over_app || wl_pointer_locked())) return;   /* la app eligió el suyo (o lo ocultó) */
    int shape = grabbing ? grab_cursor() : s->shape;
    if (shape <= CUR_DEFAULT) { c->mode = 0; return; }
    const CursorImage *ci = cursor_image(shape, cursor_scale_for(s->W, s->H));
    if (!ci) { c->mode = 0; return; }
    *c = (Cur){ 1, ci->px, ci->w, ci->h, ci->hx, ci->hy, 0xf0000000u + (unsigned)shape };
}

/* Para dibujarlo acá (CPU, plano de cursor, textura): la flecha también es una imagen. */
static void cursor_local(const Server *s, Cur *c) {
    cursor_now(s, c);
    if (c->mode != 0) return;
    const CursorImage *ci = cursor_image(CUR_DEFAULT, cursor_scale_for(s->W, s->H));
    if (ci) *c = (Cur){ 1, ci->px, ci->w, ci->h, ci->hx, ci->hy, 0xf0000000u + CUR_DEFAULT };
}

static const char *const cursor_arrow[] = {
    "X", "XX", "XoX", "XooX", "XoooX", "XooooX", "XoooooX", "XooooooX", "XoooooooX",
    "XooooooooX", "XoooooooooX", "XooooooooooX", "XooooooXXXXX", "XoooXooX",
    "XooX XooX", "XoX  XooX", "XX    XooX", "X     XooX", "       XX", NULL,
};

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/* Pasa de root al usuario que invocó sudo (o --user), conservando solo
 * CAP_SYS_TTY_CONFIG para poder devolver la consola a modo texto al salir. */
static int drop_privileges(const char *user) {
    if (geteuid() != 0) return 0;
    if (!user) user = getenv("SUDO_USER");
    if (!user) {
        fprintf(stderr, "lord-server: aviso, sigue corriendo como root (usar sudo o --user)\n");
        return 0;
    }
    struct passwd *pw = getpwnam(user);
    if (!pw || pw->pw_uid == 0) { fprintf(stderr, "lord-server: usuario inválido '%s'\n", user); return -1; }
    if (prctl(PR_SET_KEEPCAPS, 1) || initgroups(pw->pw_name, pw->pw_gid) ||
        setgid(pw->pw_gid) || setuid(pw->pw_uid)) {
        perror("lord-server: soltar privilegios");
        return -1;
    }
    struct __user_cap_header_struct hdr = { _LINUX_CAPABILITY_VERSION_3, 0 };
    struct __user_cap_data_struct caps[2];
    memset(caps, 0, sizeof caps);
    caps[0].effective = caps[0].permitted = 1u << CAP_SYS_TTY_CONFIG;
    if (syscall(SYS_capset, &hdr, caps)) perror("lord-server: capset");
    prctl(PR_SET_KEEPCAPS, 0);
    if (setuid(0) == 0) { fprintf(stderr, "lord-server: no se pudieron soltar privilegios\n"); return -1; }
    setenv("HOME", pw->pw_dir, 1);
    setenv("USER", pw->pw_name, 1);
    fprintf(stderr, "lord-server: corriendo como %s (uid %d)\n", pw->pw_name, (int)pw->pw_uid);
    return 0;
}

static void update_cursor(Server *s);
static int gpu_start(Server *s);

/* Forma del cursor del nodo bajo el puntero: la del primero hacia arriba que tenga `cursor`. */
static int node_cursor(const Node *n) {
    for (; n; n = n->parent) if (n->s.cursor) return n->s.cursor;
    return CUR_DEFAULT;
}

static void dispatch_pointer(Server *s) {
    s->motion_pending = 0;
    float x = (float)s->cx, y = (float)s->cy;
    /* arrastre de una ventana (grab.c): lo sigue C, sin JS ni la app Linux por cuadro */
    int grabbing = grab_active();
    if (grabbing && s->moved) grab_motion(x, y);
    for (int i = 0; i < s->nbuttons; i++) {
        if (s->buttons[i].down) s->held |= 1 << s->buttons[i].button;
        else s->held &= ~(1 << s->buttons[i].button);
    }
    grab_note_pointer(x, y, s->held);           /* antes del "down" en JS: ahí puede empezar un arrastre */
    int gnode = 0, gedges = 0;
    float gdx = 0, gdy = 0, gdw = 0, gdh = 0;
    int ended = grabbing && grab_finish(&gnode, &gedges, &gdx, &gdy, &gdw, &gdh);
    Node *target = hit_test(ui.root, x, y);
    int id = target ? target->id : 0;
    /* apps Wayland: reciben el puntero; el tema también (para enfocar y subir la ventana) */
    int down[MAX_BUTTONS], button[MAX_BUTTONS];
    for (int i = 0; i < s->nbuttons; i++) { down[i] = s->buttons[i].down; button[i] = s->buttons[i].button; }
    double t0 = now_ms();
    int over = 0;
    if (grabbing && !ended) wl_pointer_grab(s->nbuttons, down);
    else over = wl_pointer(x, y, target, s->moved, s->nbuttons, down, button, s->wheel);
    if (over == 2) id = 0;                  /* sobre un menú de una app Linux: el árbol no lo ve */
    if (ended) js_wm_end(gnode, gedges, gdx, gdy, gdw, gdh);
    int shape = over == 2 ? CUR_DEFAULT : node_cursor(target);
    if (shape != s->shape || grabbing != grab_active() || (over != 0) != s->over_app) { s->shape = shape; s->cursor_dirty = 1; }
    s->over_app = over != 0;                /* el cursor de la app solo vale sobre sus superficies */
    if (s->moved && !grabbing) js_pointer("move", id, x, y, 0);
    for (int i = 0; i < s->nbuttons; i++)          /* sobre un menú, solo los "up" (terminan arrastres) */
        if (over != 2 || !s->buttons[i].down) js_pointer(s->buttons[i].down ? "down" : "up", id, x, y, s->buttons[i].button);
    if (s->wheel && over != 2) js_pointer("wheel", id, x, y, s->wheel);
    if (dstat.held && s->moved) {
        if (!dstat.active) { dstat.active = 1; dstat.t0 = dstat.last = t0; }
        dstat.moves++;
        dstat.js += now_ms() - t0;
    }
    for (int i = 0; i < s->nbuttons; i++) {      /* máscara: dos dispositivos pueden repetir el mismo botón */
        if (s->buttons[i].down) dstat.held |= 1 << s->buttons[i].button;
        else dstat.held &= ~(1 << s->buttons[i].button);
    }
    if (!dstat.held) {
        if (dstat.active && dstat.frames > 1 && dstat.last > dstat.t0) {
            double span = dstat.last - dstat.t0;
            long lr = 0, lb = 0;
            paint_layer_stats(&lr, &lb);
            fprintf(stderr, "arrastre: %ld cuadros en %.0f ms = %.1f FPS (%ld movimientos); por cuadro: trabajo %.2f ms "
                            "(máx %.2f; JS %.2f, layout %.2f, pintado %.2f, presentar %.2f), intervalo máx %.1f ms; "
                            "capa: %ld pintadas, %ld copiadas (%s)\n",
                    dstat.frames, span, 1000.0 * dstat.frames / span, dstat.moves, dstat.work / dstat.frames, dstat.work_max,
                    dstat.js / dstat.frames, dstat.layout / dstat.frames, (dstat.paint - dstat.layout) / dstat.frames,
                    dstat.present / dstat.frames, dstat.gap_max, lr, lb, gpu_active() ? "GPU" : "CPU");
        }
        dstat = (DragStats){ 0 };
    }
    s->moved = s->wheel = s->nbuttons = 0;
}

/* ---- escritorio remoto: la entrada del navegador, como si fuera local (remote/mirror.c) ---- */

static Server *remote_server;

static void remote_settle(Server *s) {
    display_activity(now_ms());             /* idle timers (Settings → Battery) */
    if (s->cx < 0) s->cx = 0;
    if (s->cy < 0) s->cy = 0;
    if (s->cx >= s->W) s->cx = s->W - 1;
    if (s->cy >= s->H) s->cy = s->H - 1;
    wl_pointer_confine(&s->cx, &s->cy);
    s->lx = s->cx;
    s->ly = s->cy;
    if (s->hw_cursor) kms_cursor_move(&s->kms, s->cx - s->cursor_hx, s->cy - s->cursor_hy);
    else s->cursor_dirty = 1;
    dispatch_pointer(s);
}

static void remote_pointer(int x, int y) {
    Server *s = remote_server;
    if (x == s->cx && y == s->cy) return;
    s->rdx += x - s->cx;
    s->rdy += y - s->cy;
    s->cx = x;
    s->cy = y;
    s->moved = 1;
    wl_pointer_relative(s->rdx, s->rdy);
    s->rdx = s->rdy = 0;
    remote_settle(s);
}

static void remote_button(int button, int down) {
    Server *s = remote_server;
    if (s->nbuttons < MAX_BUTTONS) s->buttons[s->nbuttons++] = (ButtonEvent){ down, button };
    remote_settle(s);
}

static void remote_wheel(int steps) {
    Server *s = remote_server;
    s->wheel += steps;
    remote_settle(s);
}

static void remote_key(int code, int value) {
    Server *s = remote_server;
    if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) s->ctrl = value != 0;
    if (code == KEY_LEFTALT || code == KEY_RIGHTALT) s->alt = value != 0;
    js_key(code, value);
}
static void remote_clip(const char *text, unsigned len) { term_set_clipboard(text, len); }
static void remote_clients(const char *json) { js_remote_event(json); }
static void remote_refresh(void) {
    damage_add((Box){ 0, 0, (float)remote_server->W, (float)remote_server->H });
    ui.dirty = 1;
}

/* Otro tamaño de pantalla: el del monitor al que pasa la sesión, o el de la ventana del navegador. */
static void set_size(Server *s, int w, int h) {
    if (w == s->W && h == s->H) return;
    uint32_t *scene = malloc((size_t)w * h * 4), *frame = calloc((size_t)w * h, 4);
    if (!scene || !frame) { free(scene); free(frame); return; }
    free(s->scene.px);
    free(s->frame);
    canvas_init(&s->scene, scene, w, h, w);
    s->frame = frame;
    fprintf(stderr, "lord-server: pantalla %dx%d → %dx%d\n", s->W, s->H, w, h);
    s->W = w;
    s->H = h;
    ui.width = (float)w;
    ui.height = (float)h;
    if (s->cx >= w) s->cx = s->lx = w - 1;
    if (s->cy >= h) s->cy = s->ly = h - 1;
    wl_output_resized();
    js_screen_resized();
    s->full_copy = 2;
    damage_add((Box){ 0, 0, (float)w, (float)h });
    ui.dirty = 1;
}

/* Pedido del navegador: solo si la sesión no está en el monitor (ahí manda el monitor). */
static void remote_resize(int w, int h) {
    Server *s = remote_server;
    w &= ~1;
    h &= ~1;
    if (!s->headless || w < 640 || h < 480 || w > 3840 || h > 2400) return;
    set_size(s, w, h);
}

static void remote_greeter(const char *json) { js_greeter_event(json); }

/* ---- el monitor, prestado por lord-sessiond (docs/SESIONES.md) ----
 * fds[0] es la placa (lord-sessiond ya la hizo master), el resto teclados y mouses. La sesión
 * pasa a dibujar en la placa como un lord-server común: con GPU si hay, scanout directo de
 * pantalla completa y cursor por hardware. Al cambiar de usuario, lord-sessiond le saca el
 * master y revoca la entrada (EVIOCREVOKE); antes le avisa con RP_UNDISPLAY. */
/* lord-sessiond: un teclado, mouse o touchpad enchufado mientras la sesión está en el monitor */
static void remote_input_add(const int *fds, int n) {
    Server *s = remote_server;
    if (s->headless) { for (int i = 0; i < n; i++) close(fds[i]); return; }
    if (input_add_fds(&s->input, fds, n)) wl_input_devices_changed();
}

static void display_attach(const int *fds, int n) {
    Server *s = remote_server;
    if (!s->headless || n < 1) { for (int i = 0; i < n; i++) close(fds[i]); return; }
    if (kms_attach(&s->kms, fds[0])) {
        fprintf(stderr, "monitor: no se pudo usar la placa\n");
        s->kms.fd = -1;
        for (int i = 1; i < n; i++) close(fds[i]);
        return;
    }
    if (input_open_fds(&s->input, fds + 1, n - 1)) fprintf(stderr, "monitor: sin teclado ni mouse\n");
    wl_input_devices_changed();         /* ¿hay pantalla táctil? (wl_seat touch) */
    {
        int lw, lh;                         /* rotated by Settings → Displays: the logical size */
        display_logical_size((int)s->kms.width, (int)s->kms.height, &lw, &lh);
        set_size(s, lw & ~1, lh & ~1);
    }
    s->headless = 0;
    s->gpu_paused = 0;
    struct stat st;
    if (fstat(s->kms.fd, &st) == 0) wl_set_drm_device(st.st_rdev);
    if (display_rotation() || mirror_clients() || media_needs_cpu() || gpu_start(s)) s->gpu_paused = mirror_clients() > 0 || media_needs_cpu();
    /* el puntero donde está (una tablet absoluta no avisa si no se mueve) o en el medio */
    s->cx = s->W / 2;
    s->cy = s->H / 2;
    for (int i = 0; i < s->input.count; i++) {
        const InputDevice *d = &s->input.dev[i];
        int rx = d->ax.maximum - d->ax.minimum, ry = d->ay.maximum - d->ay.minimum;
        if (!d->has_abs || rx <= 0 || ry <= 0) continue;
        s->cx = (int)((long)(d->ax.value - d->ax.minimum) * (s->W - 1) / rx);
        s->cy = (int)((long)(d->ay.value - d->ay.minimum) * (s->H - 1) / ry);
        break;
    }
    s->lx = s->cx;
    s->ly = s->cy;
    s->full_copy = 2;
    damage_add((Box){ 0, 0, (float)s->W, (float)s->H });
    ui.dirty = 1;
    fprintf(stderr, "monitor: la sesión se muestra (%dx%d, %s)\n", s->W, s->H, gpu_active() ? "GPU" : "CPU");
    display_attached();                     /* Night Shift, idle timers, the list in Settings */
}

static void display_detach(void) {
    Server *s = remote_server;
    if (s->headless) return;
    gpu_tex_free(s->cursor_tex);
    s->cursor_tex = NULL;
    gpu_shutdown();
    input_close(&s->input);
    kms_close(&s->kms);
    s->kms.fd = -1;
    s->kms.flip_pending = 0;
    s->hw_cursor = 0;
    s->gpu_paused = 0;
    s->headless = 1;
    display_detached();
    if (!s->frame) s->frame = calloc((size_t)s->W * s->H, 4);
    damage_add((Box){ 0, 0, (float)s->W, (float)s->H });
    ui.dirty = 1;
    fprintf(stderr, "monitor: la sesión deja la pantalla\n");
}

static void on_input(void *ud, const InputDevice *d, const struct input_event *ev) {
    Server *s = ud;
    if (ev->type == EV_KEY || ev->type == EV_REL || ev->type == EV_ABS) display_activity(now_ms());
    if (s->debug && ev->type != EV_SYN)
        fprintf(stderr, "ev %s type=%u code=%u value=%d\n", d->name, ev->type, ev->code, ev->value);
    if (d->tp) {                        /* touchpad o pantalla táctil: driver propio (tp/tp.c) */
        tp_event(d->tp, ev);
        if (ev->type != EV_SYN || ev->code != SYN_REPORT) return;
    }
    switch (ev->type) {
    case EV_REL:
        if (ev->code == REL_X) { s->cx += ev->value; s->rdx += ev->value; s->moved = 1; }
        if (ev->code == REL_Y) { s->cy += ev->value; s->rdy += ev->value; s->moved = 1; }
        /* ruedas: la de alta resolución y la horizontal van como scroll fino (tp/tp_route.c, value120);
         * los pasos enteros de siempre, solo si el mouse no tiene la de alta resolución */
        if (ev->code == REL_WHEEL && !d->wheel_hires) s->wheel += ev->value;
        if (ev->code == REL_WHEEL_HI_RES) tp_route_wheel120(ev->value, 0);
        if (ev->code == REL_HWHEEL_HI_RES) tp_route_wheel120(0, ev->value);
        if (ev->code == REL_HWHEEL && !d->hwheel_hires) tp_route_wheel120(0, ev->value * 120);
        break;
    case EV_ABS: {
        const struct input_absinfo *a = ev->code == ABS_X ? &d->ax : ev->code == ABS_Y ? &d->ay : NULL;
        int range = a ? a->maximum - a->minimum : 0;
        if (range <= 0) break;
        if (display_rotation()) {
            /* rotated screen: the tablet reports physical positions; both axes can change */
            static double nx, ny;
            if (ev->code == ABS_X) nx = (double)(ev->value - a->minimum) / range; else ny = (double)(ev->value - a->minimum) / range;
            double lx, ly;
            display_unrotate_point(nx, ny, &lx, &ly);
            int x = (int)(lx * (s->W - 1)), y = (int)(ly * (s->H - 1));
            s->rdx += x - s->lx; s->rdy += y - s->ly;
            s->cx = x; s->cy = y;
            s->moved = 1;
            break;
        }
        int v = (int)((long)(ev->value - a->minimum) * (ev->code == ABS_X ? s->W - 1 : s->H - 1) / range);
        if (ev->code == ABS_X) { s->rdx += v - s->lx; s->cx = v; } else { s->rdy += v - s->ly; s->cy = v; }
        s->moved = 1;
        break;
    }
    case EV_KEY: {
        if (ev->value == 1 && ev->code < BTN_MISC) tp_key_activity(ev->code, now_ms());   /* ignorar el touchpad al escribir */
        if (ev->code == KEY_LEFTCTRL || ev->code == KEY_RIGHTCTRL) s->ctrl = ev->value != 0;
        if (ev->code == KEY_LEFTALT || ev->code == KEY_RIGHTALT) s->alt = ev->value != 0;
        if (ev->code == KEY_BACKSPACE && ev->value == 1 && s->ctrl && s->alt) s->quit = 1;
        /* Botones con la numeración del DOM: 0 izquierdo, 1 medio, 2 derecho. */
        int button = ev->code == BTN_LEFT ? 0 : ev->code == BTN_MIDDLE ? 1 : ev->code == BTN_RIGHT ? 2 : -1;
        if (button >= 0 && ev->value != 2 && s->nbuttons < MAX_BUTTONS)
            s->buttons[s->nbuttons++] = (ButtonEvent){ ev->value == 1, button };
        else if (ev->code < BTN_MISC) js_key(ev->code, ev->value);
        break;
    }
    case EV_SYN:
        if (s->moved) {
            /* juegos: movimiento relativo crudo; con el puntero bloqueado el cursor no se mueve */
            wl_pointer_relative(s->rdx, s->rdy);
            s->rdx = s->rdy = 0;
            if (wl_pointer_locked()) { s->cx = s->lx; s->cy = s->ly; }
        }
        if (s->cx < 0) s->cx = 0;
        if (s->cy < 0) s->cy = 0;
        if (s->cx >= s->W) s->cx = s->W - 1;
        if (s->cy >= s->H) s->cy = s->H - 1;
        wl_pointer_confine(&s->cx, &s->cy);
        s->lx = s->cx;
        s->ly = s->cy;
        if (s->moved) {
            if (s->hw_cursor) kms_cursor_move(&s->kms, s->cx - s->cursor_hx, s->cy - s->cursor_hy);
            else s->cursor_dirty = 1;
        }
        /* el movimiento solo se junta: se manda una vez por cuadro (pointer_flush, al terminar el
         * page flip); los botones y la rueda, enseguida (con el movimiento que haya hasta acá) */
        if (s->nbuttons || s->wheel) {
            dispatch_pointer(s);
            if (gpu_active()) update_cursor(s);     /* la app pudo pedir otro cursor (u ocultarlo) */
        } else if (s->moved) s->motion_pending = 1;
        if (d->tp) tp_latency(d->tp, ev);       /* touchpad: latencia por gesto en el registro */
        tp_route_wheel_flush();                 /* rueda de alta resolución u horizontal */
        break;
    }
}

/* ---- touchpad y pantalla táctil (tp/): mueven el puntero de siempre y pasan por dispatch_pointer ---- */

static void tp_move(double dx, double dy, double rx, double ry) {
    static double fx, fy;               /* fracciones de píxel que quedan para el próximo */
    Server *s = remote_server;
    fx += dx;
    fy += dy;
    int ix = (int)fx, iy = (int)fy;
    fx -= ix;
    fy -= iy;
    s->cx += ix;
    s->cy += iy;
    s->rdx += rx;
    s->rdy += ry;
    s->moved = 1;
}
static void tp_warp(double x, double y) {
    Server *s = remote_server;
    int ix = (int)(x + 0.5), iy = (int)(y + 0.5);
    s->rdx += ix - s->cx;
    s->rdy += iy - s->cy;
    s->cx = ix;
    s->cy = iy;
    s->moved = 1;
}
static void tp_button(int button, int down) {
    Server *s = remote_server;
    if (s->nbuttons < MAX_BUTTONS) s->buttons[s->nbuttons++] = (ButtonEvent){ down, button };
}
static void tp_position(double *x, double *y, int *w, int *h) {
    Server *s = remote_server;
    *x = s->cx;
    *y = s->cy;
    *w = s->W;
    *h = s->H;
    tp_set_screen(s->W, s->H);
}
/* salida de los temporizadores del driver (tocar, inercia): como un SYN de un dispositivo */
static void tp_flush(void) {
    static const InputDevice none = { .fd = -1 };
    struct input_event syn = { .type = EV_SYN, .code = SYN_REPORT };
    on_input(remote_server, &none, &syn);
}
static const TpHost tp_host = { tp_move, tp_warp, tp_button, tp_position, tp_flush };

/* escritorio remoto: scroll fino del trackpad del navegador y pellizco (ctrl+rueda) */
static void remote_scroll(int phase, double dx, double dy) { tp_route_scroll(phase, dx, dy, TPR_FINGER); }
static void remote_pinch(int phase, double scale, double rotation) { tp_route_pinch(phase, 2, 0, 0, scale, rotation); }

/* Layout y pintado de la UI en el canvas (solo lo que cambió). Devuelve el rectángulo repintado. */
static Rect paint_ui(Server *s) {
    Rect r = { 0, 0, 0, 0 };
    if (js_wants_frame()) js_frame(now_ms());
    if (!ui.dirty) return r;
    ui.dirty = 0;
    double t0 = now_ms();
    layout_root(ui.root, (float)s->W, (float)s->H);
    double t1 = now_ms();
    /* Solo se repinta la región que cambió (damage tracking). */
    if (paint_damage(ui.root)) {
        Box d = ui.damage;
        canvas_push_clip(&s->scene, d.x0, d.y0, d.x1 - d.x0, d.y1 - d.y0);
        r = s->scene.clip;
        ui.damage_px = (double)(r.x1 - r.x0) * (r.y1 - r.y0);
        canvas_clear(&s->scene, 0x000000);
        paint_root(&s->scene, ui.root);
        media_scene_painted(&s->scene);     /* grabación/escritorio remoto con ventanas protegidas */
        canvas_set_clip(&s->scene, (Rect){0, 0, s->W, s->H});
    } else ui.damage_px = 0;
    ui.damage = (Box){0, 0, 0, 0};
    double t2 = now_ms();
    ui.layout_ms = t1 - t0;
    ui.paint_ms = t2 - t1;
    s->layout_total += ui.layout_ms;
    s->paint_total += ui.paint_ms;
    s->renders++;
    if (s->debug || paint_profile)
        fprintf(stderr, "render %ld: layout %.2f ms, paint %.2f ms, repintado %.0f%%\n", s->renders,
                ui.layout_ms, ui.paint_ms, 100.0 * ui.damage_px / ((double)s->W * s->H));
    return r;
}

/* La flecha de LordOS en ARGB (para el plano de cursor o una textura). */
static void arrow_argb(uint32_t *px, int w, int h) {
    memset(px, 0, (size_t)w * h * 4);
    Canvas c;
    canvas_init(&c, px, w, h, w);
    canvas_bitmap(&c, 0, 0, cursor_arrow, 0xff000000u, 0xffffffffu);
}

/* Cursor del camino GPU: cambia cuando la app pide otro (o lo oculta, o vuelve la flecha). */
static void update_cursor(Server *s) {
    Cur c;
    cursor_local(s, &c);
    const uint32_t *px = c.px;
    int w = c.w, h = c.h, hx = c.hx, hy = c.hy, mode = c.mode;
    unsigned gen = c.gen;
    if (gen == s->cursor_gen && mode == s->cursor_mode && s->cursor_tex) return;
    s->cursor_gen = gen;
    s->cursor_mode = mode;
    static uint32_t arrow[64 * 64];
    if (mode != 1 || w > 64 || h > 64) { arrow_argb(arrow, 64, 64); px = arrow; w = h = 64; hx = hy = 0; }
    s->cursor_hx = hx;
    s->cursor_hy = hy;
    if (s->hw_cursor) {
        if (mode == 2) kms_cursor_show(&s->kms, 0);
        else {
            kms_cursor_image(&s->kms, px, w, h, hx, hy);
            kms_cursor_show(&s->kms, 1);
            kms_cursor_move(&s->kms, s->cx - hx, s->cy - hy);
        }
    }
    s->cursor_tex = gpu_tex_pixels(s->cursor_tex, px, w, h, 0);
    s->cursor_tex_w = w;
    s->cursor_tex_h = h;
}

/* GPU (Mesa) sobre la placa de la sesión; si no hay una usable, sigue el camino por CPU. */
static int gpu_start(Server *s) {
    if (gpu_init(&s->kms)) return -1;
    static uint32_t arrow[64 * 64];
    arrow_argb(arrow, 64, 64);
    s->hw_cursor = getenv("LORD_SW_CURSOR") == NULL && kms_cursor_image(&s->kms, arrow, 64, 64, 0, 0) == 0;
    if (s->hw_cursor) kms_cursor_move(&s->kms, s->cx, s->cy);
    s->cursor_gen = 0;
    s->cursor_mode = -1;
    fprintf(stderr, "gpu: cursor %s\n", s->hw_cursor ? "por hardware (plano de cursor)" : "dibujado en la composición");
    damage_add((Box){ 0, 0, (float)s->W, (float)s->H });
    ui.dirty = 1;
    return 0;
}

static Server *gpu_server;
static void gpu_layer(int layer) {
    wl_gpu_draw_layer(layer);
    Server *s = gpu_server;
    if (layer == 1 && !s->hw_cursor && s->cursor_mode != 2 && s->cursor_tex)      /* cursor sin plano propio */
        gpu_draw(s->cursor_tex, (float)(s->cx - s->cursor_hx), (float)(s->cy - s->cursor_hy), (float)s->cursor_tex_w,
                 (float)s->cursor_tex_h, 0, 0, (float)s->cursor_tex_w, (float)s->cursor_tex_h, (Rect){ 0, 0, s->W, s->H }, 1);
}

static void render_frame(Server *s);
static void render(Server *s) {
    double t0 = now_ms();
    render_frame(s);
    if (dstat.active) {
        double t1 = now_ms(), js = dstat.js - dstat.work_js;
        double work = (t1 - t0) + js;
        dstat.work_js = dstat.js;
        dstat.frames++;
        dstat.work += work;
        dstat.paint += ui.layout_ms + ui.paint_ms;
        dstat.layout += ui.layout_ms;
        dstat.present += (t1 - t0) - (ui.layout_ms + ui.paint_ms);
        if (work > dstat.work_max) dstat.work_max = work;
        if (t1 - dstat.last > dstat.gap_max && dstat.frames > 1) dstat.gap_max = t1 - dstat.last;
        dstat.last = t1;
    }
}

static void render_frame(Server *s) {
    ui.layout_ms = ui.paint_ms = 0;
    Rect r = paint_ui(s);
    s->last_render = now_ms();
    if (gpu_active() && (mirror_clients() || media_needs_cpu())) {    /* también al grabar la pantalla */
        /* alguien mira la sesión desde la red: la pantalla tiene que estar en memoria (camino CPU) */
        fprintf(stderr, "remoto: hay una conexión; se compone por CPU\n");
        gpu_tex_free(s->cursor_tex);
        s->cursor_tex = NULL;
        gpu_shutdown();
        s->gpu_paused = 1;
        kms_cursor_show(&s->kms, 0);
        s->hw_cursor = 0;
        kms_set_fb(&s->kms, s->kms.buf[s->kms.front].fb_id); s->full_copy = 2;
        s->kms.custom = 0;
        damage_add((Box){ 0, 0, (float)s->W, (float)s->H });
        ui.dirty = 1;
        r = paint_ui(s);
    }
    if (s->headless) {
        Canvas out;
        canvas_init(&out, s->frame, s->W, s->H, s->W);
        canvas_copy(&out, &s->scene);
        wl_paint_fullscreen(&out);
        Cur k;
        cursor_now(s, &k);
        const uint32_t *px = k.px;
        int w = k.w, h = k.h, hx = k.hx, hy = k.hy, mode = k.mode;
        unsigned gen = k.gen;
        /* lo que ve el navegador y lo que se graba: con las ventanas protegidas en negro */
        MediaCursor mc = { s->cx, s->cy, mode, px, w, h, hx, hy };
        const Canvas *seen = media_composed(&out, mirror_clients() > 0, &mc);
        mirror_frame(seen->px, seen->stride, s->W, s->H);
        mirror_cursor(s->cx, s->cy, mode, px, w, h, hx, hy, gen);
    } else if (gpu_active()) {
        update_cursor(s);
        gpu_server = s;
        /* pantalla completa compatible: directo a la placa; si no, se compone */
        if (!wl_try_scanout(s->hw_cursor || s->cursor_mode == 2) && gpu_frame(&s->scene, r, gpu_layer)) {
            fprintf(stderr, "gpu: falló un cuadro; se sigue por CPU\n");
            gpu_shutdown();
            kms_set_fb(&s->kms, s->kms.buf[s->kms.front].fb_id); s->full_copy = 2;
            s->kms.custom = 0;
            damage_add((Box){ 0, 0, (float)s->W, (float)s->H });
            ui.dirty = 1;
        }
    } else {
        KmsBuffer *b = kms_back(&s->kms);
        Canvas out, phys;
        /* rotated (Settings → Displays): the frame is composed upright in s->frame and copied
         * rotated into the card's buffer; otherwise straight into the buffer */
        int rot = display_rotation();
        canvas_init(&phys, b->map, rot ? (int)s->kms.width : s->W, rot ? (int)s->kms.height : s->H, (int)(b->pitch / 4));
        if (rot) canvas_init(&out, s->frame, s->W, s->H, s->W);
        else out = phys;
        int bi = s->kms.front ^ 1, fs = wl_fullscreen_active();
        if (fs != s->was_fs) { s->was_fs = fs; s->full_copy = 2; }
        Rect need = { 0, 0, s->W, s->H };       /* what this buffer gets now (also tinted) */
        if (s->full_copy > 0 || fs) {
            canvas_copy(&out, &s->scene);
            if (s->full_copy > 0) s->full_copy--;
        } else {
            need = rect_join(rect_join(s->stale[bi], r), s->curs[bi]);
            if (need.x0 < 0) need.x0 = 0;
            if (need.y0 < 0) need.y0 = 0;
            if (need.x1 > s->W) need.x1 = s->W;
            if (need.y1 > s->H) need.y1 = s->H;
            for (int y = need.y0; y < need.y1; y++)
                memcpy(out.px + (size_t)y * out.stride + need.x0, s->scene.px + (size_t)y * s->scene.stride + need.x0, (size_t)(need.x1 - need.x0) * 4);
        }
        s->stale[bi] = (Rect){ 0, 0, 0, 0 };
        s->stale[bi ^ 1] = rect_join(s->stale[bi ^ 1], r);
        s->curs[bi] = (Rect){ 0, 0, 0, 0 };
        wl_paint_fullscreen(&out);
        Cur k;
        cursor_now(s, &k);
        const uint32_t *px = k.px;
        int w = k.w, h = k.h, hx = k.hx, hy = k.hy, mode = k.mode;
        unsigned gen = k.gen;
        /* escritorio remoto (sin el cursor) y grabación: con las ventanas protegidas en negro */
        MediaCursor mc = { s->cx, s->cy, mode, px, w, h, hx, hy };
        const Canvas *seen = media_composed(&out, mirror_clients() > 0, &mc);
        mirror_frame(seen->px, seen->stride, s->W, s->H);
        mirror_cursor(s->cx, s->cy, mode, px, w, h, hx, hy, gen);
        cursor_local(s, &k);                    /* la flecha también es una imagen (cursor.c) */
        Rect cr = { s->cx - k.hx, s->cy - k.hy, s->cx - k.hx + k.w, s->cy - k.hy + k.h };
        if (!rot) {
            display_tint(&out, need);           /* Night Shift without a gamma LUT (software) */
            if (k.mode == 1) {
                canvas_blit(&out, k.px, k.w, k.h, s->cx - k.hx, s->cy - k.hy, 0, 1, 0);
                s->curs[bi] = cr;
            }
        } else {
            if (need.x1 > need.x0 && need.y1 > need.y0) {
                display_rotate_copy(&out, need, &phys, rot);
                display_tint(&phys, display_rotate_rect(need, s->W, s->H, rot));
            }
            /* the cursor is composited apart (s->frame stays clean) and copied rotated */
            static uint32_t tmp[128 * 128];
            Rect cc = cr;
            if (cc.x0 < 0) cc.x0 = 0;
            if (cc.y0 < 0) cc.y0 = 0;
            if (cc.x1 > s->W) cc.x1 = s->W;
            if (cc.y1 > s->H) cc.y1 = s->H;
            int cw = cc.x1 - cc.x0, ch = cc.y1 - cc.y0;
            if (k.mode == 1 && cw > 0 && ch > 0 && cw <= 128 && ch <= 128) {
                for (int y = 0; y < ch; y++) memcpy(tmp + (size_t)y * cw, out.px + (size_t)(cc.y0 + y) * out.stride + cc.x0, (size_t)cw * 4);
                Canvas t;
                canvas_init(&t, tmp, cw, ch, cw);
                canvas_blit(&t, k.px, k.w, k.h, cr.x0 - cc.x0, cr.y0 - cc.y0, 0, 1, 0);
                display_rotate_copy_at(tmp, cw, cc.x0, cc.y0, cc, s->W, s->H, &phys, rot);
                display_tint(&phys, display_rotate_rect(cc, s->W, s->H, rot));
                s->curs[bi] = cc;
            }
        }
        kms_flip(&s->kms);
    }
    s->cursor_dirty = 0;
    s->frames++;
    wl_frame_done((unsigned)now_ms());
}

/* Cambio de tema pedido desde JS (lord.themes.use): se descarta el contexto JS y el árbol. */
static void switch_theme(const char *current) {
    char path[sizeof ui.theme_request];
    snprintf(path, sizeof path, "%s", ui.theme_request);
    ui.theme_request[0] = 0;
    fprintf(stderr, "lord-server: cambiando a tema %s\n", path);
    js_shutdown();
    ui_reset();
    term_detach_all();
    if (js_init(path)) {
        fprintf(stderr, "lord-server: el tema falló, se vuelve a %s\n", current);
        snprintf(ui.theme_failed, sizeof ui.theme_failed, "%s", path);
        js_shutdown();
        ui_reset();
        js_init(current);
    }
}

/* ---- Settings → Displays and the other hardware pages (src/hw, docs/SETTINGS.md) ---- */

static Server *hw_server;
static Kms *hw_kms(void) { Server *s = hw_server; return s && !s->headless && s->kms.fd >= 0 ? &s->kms : NULL; }
static void hw_redraw(void) {
    Server *s = hw_server;
    if (!s) return;
    s->full_copy = 2;
    damage_add((Box){ 0, 0, (float)s->W, (float)s->H });
    ui.dirty = 1;
}
static const DisplayHost hw_host = { hw_kms, hw_redraw, js_hw_event };

/* A modeset queued by Settings, run between frames: the GPU path stops (its surfaces have the old
 * size), kms.c changes the mode, the session takes the new (logical) size and the GPU comes back.
 * A rotated screen stays on the CPU path (the rotation is done while copying the frame). */
static int gpu_was_on = 1;              /* try the GPU again after a modeset (fails quietly without one) */
static void run_modeset(Server *s, uint32_t conn, const struct drm_mode_modeinfo *m) {
    if (gpu_active()) { gpu_was_on = 1; gpu_tex_free(s->cursor_tex); s->cursor_tex = NULL; gpu_shutdown(); }
    kms_cursor_show(&s->kms, 0);
    s->hw_cursor = 0;
    int ok = kms_modeset(&s->kms, conn, m) == 0;
    int lw, lh;
    display_logical_size((int)s->kms.width, (int)s->kms.height, &lw, &lh);
    set_size(s, lw & ~1, lh & ~1);
    memset(s->stale, 0, sizeof s->stale);
    memset(s->curs, 0, sizeof s->curs);
    s->full_copy = 2;
    if (gpu_was_on && !display_rotation() && !mirror_clients() && !media_needs_cpu() && gpu_start(s) == 0) gpu_was_on = 0;
    display_modeset_done(ok);
    hw_redraw();
}

static void usage(const char *argv0) {
    fprintf(stderr, "uso: %s [--theme bundle.js] [--fonts dir] [--shell dir] [--user nombre] [--card /dev/dri/cardN]\n"
                    "          [--headless ANCHOxALTO [--greeter]] [--timeout s] [--debug] [--profile]\n", argv0);
}

int main(int argc, char **argv) {
    /* guardián de una sesión de terminal (ver term/hold.c): no abre pantalla ni input */
    if (argc > 1 && !strcmp(argv[1], "--term-holder")) return hold_main(argc, argv);
    /* «modo app»: lord-server --app <id> corre una app .lpk como cliente Wayland aislado
     * (app/appclient.c). El id ya viene validado del propio lord-server (no de JS). */
    if (argc >= 3 && !strcmp(argv[1], "--app")) {
        const char *fonts = "../assets/fonts", *host = "../assets/app-host.bundle.js";
        for (int i = 3; i + 1 < argc; i++) {
            if (!strcmp(argv[i], "--fonts")) fonts = argv[++i];
            else if (!strcmp(argv[i], "--host")) host = argv[++i];
        }
        return app_main(argv[2], host, fonts);
    }
    const char *card = NULL;               /* sin --card: la que tiene pantalla (drmcard.h) */
    const char *theme = "../themes/default/theme.bundle.js";
    const char *fonts = "../assets/fonts";
    const char *shell = "../assets/shell";     /* integración de la terminal con bash */
    const char *user = NULL;
    double timeout_ms = 0;
    static Server s;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout_ms = atof(argv[++i]) * 1000;
        else if (!strcmp(argv[i], "--card") && i + 1 < argc) card = argv[++i];
        else if (!strcmp(argv[i], "--theme") && i + 1 < argc) theme = argv[++i];
        else if (!strcmp(argv[i], "--fonts") && i + 1 < argc) fonts = argv[++i];
        else if (!strcmp(argv[i], "--shell") && i + 1 < argc) shell = argv[++i];
        else if (!strcmp(argv[i], "--user") && i + 1 < argc) user = argv[++i];
        else if (!strcmp(argv[i], "--headless") && i + 1 < argc) {
            /* sesión sin monitor para el escritorio remoto (la lanza lord-sessiond) */
            int w = 0, h = 0;
            if (sscanf(argv[++i], "%dx%d", &w, &h) != 2 || w < 640 || h < 480 || w > 3840 || h > 2400) { usage(argv[0]); return 2; }
            s.headless = 1;
            s.W = w & ~1;
            s.H = h & ~1;
        }
        else if (!strcmp(argv[i], "--greeter")) s.greeter = 1;      /* pantalla de ingreso (lord-sessiond) */
        else if (!strcmp(argv[i], "--debug")) s.debug = 1;
        else if (!strcmp(argv[i], "--profile")) paint_profile = 1;
        else { usage(argv[0]); return 2; }
    }

    if (s.greeter && !s.headless) { usage(argv[0]); return 2; }
    js_sys_set_greeter(s.greeter);
    if (text_init(fonts)) return 1;
    {   /* assets/ = la carpeta de las fuentes, un nivel arriba */
        char a[1024];
        snprintf(a, sizeof a, "%s/..", fonts);
        js_sys_set_assets(a);
    }
    term_init(fonts, shell);
    kb_set("latam", "");         /* la UI elige la del usuario al cargar sus ajustes */
    s.kms.fd = -1;
    if (s.headless) s.frame = calloc((size_t)s.W * s.H, 4);
    else {
        char picked[64];
        if (!card) { drm_pick_card(picked, sizeof picked); card = picked; }
        kms_set_picker(display_pick);       /* the display saved in Settings (displays.json) */
        if (kms_open(&s.kms, card)) return 1;
        s.W = (int)s.kms.width;
        s.H = (int)s.kms.height;
    }
    ui.width = (float)s.W;
    ui.height = (float)s.H;
    canvas_init(&s.scene, malloc((size_t)s.W * s.H * 4), s.W, s.H, s.W);
    s.cx = s.lx = s.W / 2;
    s.cy = s.ly = s.H / 2;

    int vt = -1;
    if (!s.headless) {
        if (input_open(&s.input)) fprintf(stderr, "input: sin dispositivos (¿permisos?)\n");
        /* La consola de texto deja de dibujar encima nuestro. */
        vt = open("/dev/tty0", O_RDWR | O_CLOEXEC);
        if (vt >= 0 && ioctl(vt, KDSETMODE, KD_GRAPHICS)) { close(vt); vt = -1; }
    }

    /* Ayudante privilegiado (acciones cerradas: región, zona horaria, paquetes de idioma). La
     * pantalla de ingreso no tiene: lo que necesita root lo hace lord-sessiond. */
    const char *session_user = s.greeter ? NULL : user ? user : getenv("SUDO_USER");
    if (session_user && admin_start(session_user)) fprintf(stderr, "lord-server: sin ayudante de administración\n");
    /* LordOS networking: with lord-sessiond the network belongs to the system (lord-netd brings it
     * up at boot) and net-restore only confirms; without it (a standalone lord-server) this
     * session's helper brings it up (see net_act_restore and docs/RED.md) */
    else if (session_user) admin_send("net-restore", "", 0);
    int ok = drop_privileges(user) == 0;
    if (ok && s.headless && getenv("HOME")) {
        /* sin terminal: el registro de la sesión va a la carpeta personal */
        char log[1024];
        snprintf(log, sizeof log, "%s/.cache", getenv("HOME"));
        mkdir(log, 0700);
        snprintf(log, sizeof log, "%s/.cache/lordos", getenv("HOME"));
        mkdir(log, 0700);
        snprintf(log, sizeof log, "%s/.cache/lordos/%s", getenv("HOME"), s.greeter ? "greeter.log" : "session.log");
        if (!freopen(log, "w", stderr)) { /* sigue sin registro */ }
        setvbuf(stderr, NULL, _IOLBF, 0);
    }
    /* GPU (Mesa) después de soltar root: si no hay una usable, sigue el camino por CPU */
    /* Settings' hardware pages: displays, uevents, Bluetooth, power profiles (src/hw) */
    kms_set_picker(display_pick);
    hw_server = &s;
    if (ok && !s.greeter) hw_start(&hw_host, 1);
    if (ok && !s.headless && !display_rotation()) gpu_start(&s);
    {
        struct stat st;
        if (s.kms.fd >= 0 && fstat(s.kms.fd, &st) == 0) wl_set_drm_device(st.st_rdev);
    }
    if (ok && wl_start()) fprintf(stderr, "lord-server: sin Wayland (las apps Linux no van a poder abrirse)\n");
    if (ok && net_open()) fprintf(stderr, "lord-server: sin avisos de red (rtnetlink)\n");
    /* Backups: a thread running as the user, nice 19 and idle I/O (backup/backup.c) */
    if (ok && !s.greeter && getenv("HOME") && session_user) backup_start(getenv("HOME"), session_user);
    if (ok && snd_start()) fprintf(stderr, "lord-server: sin PipeWire por ahora (sonido); se reintenta solo\n");
    if (ok && !s.greeter) portal_start();   /* modo oscuro, acento y botones para GTK/GNOME/Qt (ui/portal.c) */
    if (ok && !s.greeter) term_restore();   /* shells que siguieron vivos desde el arranque anterior */
    remote_server = &s;
    tp_route_init(&tp_host);
    static const MirrorInput mirror_in = { remote_pointer, remote_button, remote_wheel, remote_key, remote_clip, remote_clients,
                                           remote_refresh, remote_resize, remote_greeter, display_attach, display_detach,
                                           remote_scroll, remote_pinch, remote_input_add };
    if (ok && mirror_start(s.W, s.H, &mirror_in)) fprintf(stderr, "lord-server: sin escritorio remoto (socket)\n");
    if (ok) {
        char path[1024];
        snprintf(path, sizeof path, "%s", theme);
        ok = js_init(path) == 0;
    }
    if (!ok) {
        input_close(&s.input);
        if (!s.headless) kms_close(&s.kms);
        if (vt >= 0) { ioctl(vt, KDSETMODE, KD_TEXT); close(vt); }
        return 1;
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);

    char current[sizeof ui.theme_request];
    snprintf(current, sizeof current, "%s", theme);
    double start = now_ms();
    while (!s.quit && !ui.quit_requested) {
        if (ui.theme_request[0]) {
            char next[sizeof current];
            snprintf(next, sizeof next, "%s", ui.theme_request);
            switch_theme(current);
            snprintf(current, sizeof current, "%s", next);
        }
        /* El movimiento del puntero juntado: una vez por cuadro, cuando terminó el page flip. Así un
         * mouse de 1000 Hz no llama a JS (ni a las apps Linux) mil veces por segundo. */
        if (s.motion_pending && !s.kms.flip_pending) {
            dispatch_pointer(&s);
            if (gpu_active()) update_cursor(&s);
        }
        /* Timers del tema (React agenda su trabajo con setTimeout/setImmediate). */
        double delay = -1, tick0 = now_ms();
        for (int i = 0; i < 64; i++) {
            delay = js_tick(now_ms());
            if (delay != 0) break;
        }
        if (dstat.active) dstat.js += now_ms() - tick0;     /* React agenda su trabajo en timers */
        /* Terminales: parpadeo del cursor y región que cambió con la salida de los shells. */
        double now = now_ms();
        term_tick(now);
        if (term_damage()) ui.dirty = 1;
        double td = term_timeout(now);
        if (td >= 0 && (delay < 0 || td < delay)) delay = td;
        tp_tick(now);                       /* touchpad: tocar, arrastrar, inercia (tp/tp.c) */
        display_tick(now_ms());             /* screen off, lock and sleep after N minutes (src/hw) */
        double hwd = display_timeout(now_ms());
        if (hwd >= 0 && (delay < 0 || hwd < delay)) delay = hwd;
        if (!s.kms.flip_pending) {          /* a resolution/output chosen in Settings → Displays */
            uint32_t mconn;
            struct drm_mode_modeinfo mmode;
            if (display_take_modeset(&mconn, &mmode)) run_modeset(&s, mconn, &mmode);
        }
        double tpd = tp_timeout(now_ms());
        if (tpd >= 0 && (delay < 0 || tpd < delay)) delay = tpd;
        /* sin monitor y sin nadie mirando no se dibuja (al conectarse alguien llega un cuadro entero) */
        int unseen = s.headless && !mirror_clients() && !media_needs_cpu();   /* grabando la pantalla, sí */
        if (!s.headless && display_is_off()) unseen = 1;     /* screen off after idle (DPMS): nothing to draw */
        int wants = !unseen && (ui.dirty || s.cursor_dirty || js_wants_frame() || wl_wants_frame() || media_wants_frame());
        /* sin monitor no hay vblank: a lo sumo 60 cuadros por segundo */
        double pace = s.headless ? s.last_render + 16.6 - now_ms() : 0;
        if (wants && !s.kms.flip_pending && pace <= 0) { render(&s); wants = 0; }
        if (wants && pace > 0 && (delay < 0 || pace < delay)) delay = pace;

        int wait = delay < 0 ? -1 : (int)ceil(delay);
        if ((wants && !s.kms.flip_pending && pace <= 0) || ui.theme_request[0] || ui.quit_requested) wait = 0;
        if (timeout_ms > 0) {
            double left = start + timeout_ms - now_ms();
            if (left <= 0) break;
            if (wait < 0 || wait > left) wait = (int)left + 1;
        }

        wl_flush();
        mirror_clipboard(term_clipboard(), term_clipboard_gen());
        struct pollfd pfd[128 + INPUT_MAX_DEVICES + TERM_MAX];     /* +32 para los brokers de apps aisladas, +40 medios */
        int term_ids[TERM_MAX];
        int n = 0;
        pfd[n++] = (struct pollfd){ s.kms.fd, POLLIN, 0 };
        pfd[n++] = (struct pollfd){ sfd, POLLIN, 0 };
        for (int i = 0; i < s.input.count; i++) pfd[n++] = (struct pollfd){ s.input.dev[i].fd, POLLIN, 0 };
        int tbase = n, nterm = term_pollfds(pfd + n, TERM_MAX, term_ids);
        n += nterm;
        int abase = admin_fd() >= 0 ? n : -1;
        if (abase >= 0) pfd[n++] = (struct pollfd){ admin_fd(), POLLIN, 0 };
        int wbase = wl_fd() >= 0 ? n : -1;
        if (wbase >= 0) pfd[n++] = (struct pollfd){ wl_fd(), POLLIN, 0 };
        int nbase = net_fd() >= 0 ? n : -1;
        if (nbase >= 0) pfd[n++] = (struct pollfd){ net_fd(), POLLIN, 0 };
        int dkbase = disk_watch_fd() >= 0 ? n : -1;          /* Disks app open: mounts and udev */
        if (dkbase >= 0) pfd[n++] = (struct pollfd){ disk_watch_fd(), POLLIN, 0 };
        int bkbase = backup_fd() >= 0 ? n : -1;
        if (bkbase >= 0) pfd[n++] = (struct pollfd){ backup_fd(), POLLIN, 0 };
        int sbase = snd_fd() >= 0 ? n : -1;
        if (sbase >= 0) pfd[n++] = (struct pollfd){ snd_fd(), POLLIN, 0 };
        int pbase = portal_fd() >= 0 ? n : -1;
        if (pbase >= 0) pfd[n++] = (struct pollfd){ portal_fd(), (short)(POLLIN | (portal_wants_write() ? POLLOUT : 0)), 0 };
        int lgbase = logs_fd() >= 0 ? n : -1;                    /* app Registros: consultas e inotify */
        if (lgbase >= 0) pfd[n++] = (struct pollfd){ logs_fd(), POLLIN, 0 };
        int mbase = n, nm = mirror_pollfds(pfd + n, 12);
        n += nm;
        int brkbase = n, nbrk = brokersrv_pollfds(pfd + n, 32);   /* apps .lpk aisladas (broker) */
        n += nbrk;
        int abr = n, nabr = brokersrv_audio_pollfds(pfd + n, 16);  /* micrófono de las apps aisladas (PipeWire) */
        n += nabr;
        int medbase = n, nmed = media_pollfds(pfd + n, 40);      /* ayudantes lord-media y sus streams de PipeWire */
        n += nmed;
        int hwbase = n, nhw = hw_pollfds(pfd + n, 4);            /* uevents and the system bus (src/hw) */
        n += nhw;
        if (poll(pfd, n, wait) < 0 && errno != EINTR) { perror("poll"); break; }
        if (pfd[0].revents & POLLIN) {
            int was = s.kms.flip_pending;
            kms_dispatch(&s.kms);
            if (was && !s.kms.flip_pending)
                wl_presented(s.kms.flip_ns, s.kms.flip_seq, s.kms.mode.vrefresh ? 1000000000u / s.kms.mode.vrefresh : 16666667u);
        }
        if (pfd[1].revents & POLLIN) break;
        int lost[INPUT_MAX_DEVICES], nlost = 0;
        for (int i = 0; i < s.input.count; i++) {
            if (pfd[2 + i].revents & POLLIN) input_dispatch(&s.input, i, on_input, &s);
            if (pfd[2 + i].revents & (POLLERR | POLLHUP | POLLNVAL)) lost[nlost++] = i;
        }
        /* un dispositivo que dejó de servir (desenchufado, o revocado al cambiar de sesión): se cierra
         * solo ese. Dejar el monitor lo decide únicamente lord-sessiond (RP_UNDISPLAY), nunca un error */
        for (int k = nlost - 1; k >= 0; k--) input_remove(&s.input, lost[k]);
        if (nlost) wl_input_devices_changed();
        /* la GPU vuelve cuando ya nadie mira desde la red */
        if (s.gpu_paused && !s.headless && !mirror_clients() && !media_needs_cpu() && !s.kms.flip_pending && !display_rotation()) {
            s.gpu_paused = 0;
            gpu_start(&s);
        }
        for (int i = 0; i < nterm; i++)
            if (pfd[tbase + i].revents) term_dispatch(term_ids[i], pfd[tbase + i].revents);
        if (abase >= 0 && pfd[abase].revents) admin_dispatch(js_admin_event);
        if (wbase >= 0 && pfd[wbase].revents) wl_dispatch();
        if (nbase >= 0 && pfd[nbase].revents) net_dispatch(js_net_event);
        if (dkbase >= 0 && pfd[dkbase].revents && disk_watch_dispatch()) js_disk_event("change");
        if (sbase >= 0 && pfd[sbase].revents) snd_dispatch();
        if (pbase >= 0 && pfd[pbase].revents) portal_dispatch();
        if (bkbase >= 0 && pfd[bkbase].revents) backup_dispatch(js_backup_event);
        if (lgbase >= 0 && pfd[lgbase].revents) logs_dispatch(js_logs_event);
        if (nm) mirror_dispatch(pfd + mbase, nm);
        if (nbrk) brokersrv_dispatch(pfd + brkbase, nbrk);
        if (nabr) brokersrv_audio_dispatch(pfd + abr, nabr);
        if (nmed) media_dispatch(pfd + medbase, nmed);
        if (nhw) hw_dispatch(pfd + hwbase, nhw);
        hw_events();
        media_events(js_media_event);
        brokersrv_events(js_broker_event);        /* avisos de permiso e indicadores → tema */
        brokersrv_tick();                         /* teclado y reintentos hacia las apps aisladas */
        snd_events(js_sound_event);
        wl_events(js_wl_event);
        js_clipboard_poll();
        term_events(js_term_event);
    }

    if (s.renders)
        fprintf(stderr, "lord-server: %ld frames, %ld renders de UI, layout %.2f ms, paint %.2f ms (promedio)\n",
                s.frames, s.renders, s.layout_total / s.renders, s.paint_total / s.renders);
    input_close(&s.input);
    mirror_stop();
    gpu_tex_free(s.cursor_tex);
    gpu_shutdown();
    if (!s.headless) kms_close(&s.kms);
    /* Cerrar sesión (tema o Ctrl+Alt+Backspace) termina los shells; una señal o un reinicio
     * del servidor los deja vivos en sus guardianes para recuperarlos al volver. */
    term_shutdown(!(ui.quit_requested || s.quit));
    wl_stop();
    snd_stop();
    js_shutdown();
    if (vt >= 0) { ioctl(vt, KDSETMODE, KD_TEXT); close(vt); }
    free(s.scene.px);
    free(s.frame);
    return 0;
}

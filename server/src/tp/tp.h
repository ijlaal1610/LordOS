/* tp.h — driver propio de touchpads y pantallas táctiles (evdev multitouch, protocolo B), sin
 * libinput. Ver docs/TRACKPAD.md.
 *
 * tp.c es lógica pura: recibe los eventos evdev de un dispositivo (con su hora) y produce
 * movimiento del puntero, botones, scroll con píxeles exactos (e inercia), gestos (pellizco,
 * deslizar con 3/4 dedos, mantener) y toques directos. Lo que produce va a un TpSink: en
 * lord-server es tp_route.c (que lo reparte entre la UI y las apps Wayland); en las pruebas
 * (tests/tp_test.c) es una grabadora. */
#ifndef LORD_TP_H
#define LORD_TP_H

#include <linux/input.h>

enum { TP_KIND_NONE = 0, TP_KIND_TOUCHPAD = 1, TP_KIND_TOUCHSCREEN = 2 };

/* fases de un gesto o de un scroll */
enum { TP_BEGIN = 1, TP_UPDATE = 2, TP_END = 3, TP_CANCEL = 4, TP_MOMENTUM = 5, TP_MOMENTUM_END = 6 };

/* clic secundario (clic físico en un clickpad) */
enum { TP_CLICK_TWO_FINGERS = 0, TP_CLICK_BOTTOM_RIGHT = 1, TP_CLICK_BOTTOM_LEFT = 2 };

typedef struct {
    double speed;           /* 0..1, velocidad del puntero (0,5 = normal) */
    int tap;                /* tocar para hacer clic (1/2/3 dedos = izq/der/medio) */
    int tap_drag;           /* tocar y arrastrar (tocar, y volver a apoyar enseguida) */
    int natural;            /* desplazamiento natural (el contenido sigue a los dedos) */
    int secondary;          /* TP_CLICK_* */
    int three_drag;         /* arrastrar con tres dedos (estilo macOS) */
    int dwt;                /* ignorar el touchpad mientras se escribe */
    int scroll;             /* scroll con dos dedos */
    int kinetic;            /* inercia al soltar */
    int pinch;              /* pellizcar para hacer zoom (y girar) */
    int swipe3;             /* deslizar con 3 dedos: acciones del escritorio */
    int swipe4;             /* deslizar con 4 dedos: acciones del escritorio (si no, para las apps) */
} TpConfig;

/* Lo que produce el driver. dx/dy en píxeles de pantalla; scale relativo al comienzo del
 * pellizco, rotation acumulada en grados (horario +). x/y de los toques directos en 0..1. */
typedef struct {
    void (*motion)(void *ud, double dx, double dy, double raw_dx, double raw_dy);
    void (*button)(void *ud, int button, int down);         /* 0 izquierdo, 1 medio, 2 derecho */
    void (*scroll)(void *ud, int phase, double dx, double dy);
    void (*pinch)(void *ud, int phase, int fingers, double dx, double dy, double scale, double rotation);
    void (*swipe)(void *ud, int phase, int fingers, double dx, double dy);
    void (*hold)(void *ud, int phase, int fingers);
    /* pantalla táctil: 1 si una app Wayland tomó el toque (TP_BEGIN); entonces no se emula el puntero */
    int  (*touch)(void *ud, int phase, int id, double x, double y);
    void (*touch_frame)(void *ud);
    void (*warp)(void *ud, double x, double y);              /* puntero absoluto (0..1), pantalla táctil */
    void (*frame)(void *ud);                                 /* hubo salida desde tp_tick */
} TpSink;

/* Descripción de un dispositivo (la arma tp_open con ioctl, o las pruebas a mano). */
typedef struct {
    int kind;
    int clickpad;                       /* INPUT_PROP_BUTTONPAD: todo el touchpad es el botón */
    int has_right;                      /* tiene botón derecho físico */
    int nslots;
    int max_fingers;                    /* por BTN_TOOL_*TAP (puede ser más que nslots) */
    struct input_absinfo x, y, pressure, major;
    int has_pressure, has_major;
    char name[80];
} TpCaps;

typedef struct TpDevice TpDevice;

const TpConfig *tp_config(void);
void tp_set_config(const TpConfig *c);
void tp_set_sink(const TpSink *s, void *ud);
void tp_set_screen(int w, int h);       /* para las pantallas táctiles (mm → píxeles) */

/* Mira el fd: si es touchpad o pantalla táctil devuelve el dispositivo (y le pone el reloj
 * monotónico a los eventos); si no, NULL. */
TpDevice *tp_open(int fd, const char *name);
TpDevice *tp_new(const TpCaps *caps);
void tp_free(TpDevice *d);
int  tp_kind(const TpDevice *d);
const TpCaps *tp_caps(const TpDevice *d);

void tp_event(TpDevice *d, const struct input_event *ev);
void tp_event_at(TpDevice *d, const struct input_event *ev, double now_ms);   /* pruebas */
/* Teclado: para "ignorar mientras se escribe" (las teclas modificadoras no cuentan). */
void tp_key_activity(int code, double now_ms);

/* Después de despachar un cuadro (SYN_REPORT): mide desde la hora del evento. Al levantar los
 * dedos escribe una línea por gesto en el registro (stderr): cuadros, latencia media y máxima. */
void tp_latency(TpDevice *d, const struct input_event *syn);

/* Temporizadores (tocar, inercia, arrastre de tres dedos): ms hasta el próximo, o -1. */
double tp_timeout(double now_ms);
void   tp_tick(double now_ms);

/* Dispositivos abiertos: cuántos de cada tipo, y la lista en JSON para lord.input. */
int tp_count(int kind);
int tp_list_json(char *out, int size);

/* Curva de aceleración (px por mm según la velocidad en mm/s): la usa tp.c y la prueba. */
double tp_accel(double speed_mm_s, double setting);

#endif

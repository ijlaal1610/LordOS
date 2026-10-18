/* tp_route.h — reparte lo que produce el driver (tp.c) entre la interfaz de LordOS (JS) y las
 * apps Wayland (wl/wl_input.c). main.c le da el puntero; el escritorio remoto inyecta scroll y
 * pellizco del navegador por acá. */
#ifndef LORD_TP_ROUTE_H
#define LORD_TP_ROUTE_H

typedef struct {
    void (*move)(double dx, double dy, double raw_dx, double raw_dy);   /* relativo, px */
    void (*warp)(double x, double y);                                   /* absoluto, px */
    void (*button)(int button, int down);
    void (*position)(double *x, double *y, int *w, int *h);
    void (*flush)(void);            /* despachar botones y movimiento pendientes (como un SYN) */
} TpHost;

void tp_route_init(const TpHost *h);

/* fuentes de tp_route_scroll */
enum { TPR_FINGER = 0, TPR_WHEEL = 1, TPR_CONTINUOUS = 2 };
/* Scroll en píxeles (positivo = bajar el contenido) con fase TP_*; también desde el navegador. */
void tp_route_scroll(int phase, double dx, double dy, int source);
/* Pellizco: escala relativa al comienzo y giro acumulado (grados). */
void tp_route_pinch(int phase, int fingers, double dx, double dy, double scale, double rotation);

/* Ruedas del mouse (main.c): REL_WHEEL_HI_RES / REL_HWHEEL[_HI_RES] en unidades de 1/120 de paso
 * (+ arriba / + derecha). Se juntan hasta el SYN (tp_route_wheel_flush) y van como scroll fino:
 * a la app Wayland con axis_value120, a la interfaz con deltaX/deltaY (120 = 48 px). */
void tp_route_wheel120(int v120, int h120);
void tp_route_wheel_flush(void);

#endif

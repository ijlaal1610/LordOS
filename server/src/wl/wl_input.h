/* wl_input.h — gestos y toques para las apps Wayland (wl_input.c): zwp_pointer_gestures_v1
 * (pellizco, deslizar, mantener), scroll fino del touchpad (axis_source finger, axis_stop,
 * axis_value120) y wl_touch para las pantallas táctiles. Lo usa tp/tp_route.c. */
#ifndef LORD_WL_INPUT_H
#define LORD_WL_INPUT_H

#include <stdint.h>

struct wl_display;
struct wl_client;
struct wl_resource;

/* fuentes de scroll */
enum { WLI_FINGER = 0, WLI_WHEEL = 1, WLI_CONTINUOUS = 2 };

int  wl_input_has_focus(void);          /* ¿el puntero está sobre una app Wayland? */
/* scroll en píxeles (positivo = bajar el contenido); stop: terminó (axis_stop) */
void wl_input_axis(int source, double dx, double dy, int stop, int natural);
void wl_input_pinch(int phase, int fingers, double dx, double dy, double scale, double rotation_delta);
void wl_input_swipe(int phase, int fingers, double dx, double dy);
void wl_input_hold(int phase, int fingers);
/* pantalla táctil (x, y en píxeles de pantalla). En TP_BEGIN devuelve 1 si hay una app debajo
 * que escucha wl_touch. */
int  wl_input_touch(int phase, int id, double x, double y);
void wl_input_touch_frame(void);
void wl_input_devices_changed(void);    /* se conectó o quitó una pantalla táctil: capacidades */

/* lo llama wl.c */
void wl_input_globals(struct wl_display *dpy);
void wl_input_get_touch(struct wl_client *c, struct wl_resource *seat, uint32_t id);
void wl_input_seat_caps(struct wl_resource *seat);
/* rueda del mouse de siempre (wl_pointer en wl.c): axis + axis_source wheel + value120 */
void wl_input_wheel(struct wl_resource *pointer, uint32_t time, int steps);
/* rueda de alta resolución o horizontal: 1/120 de paso (+ arriba, + derecha), a la app con el puntero */
void wl_input_wheel120(int v120, int h120);

/* de wl.c, solo para wl_input.c */
struct wl_display  *wl__display(void);
struct wl_resource *wl__pointer_focus(void);          /* wl_surface bajo el puntero, o NULL */
struct wl_list     *wl__pointers(void);
struct wl_resource *wl__surface_at(float x, float y, float *lx, float *ly);

#endif

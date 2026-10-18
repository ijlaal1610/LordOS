/* wl.h — compositor Wayland de lord-server: las apps GTK y Qt como ventanas de LordOS (ver wl.c). */
#ifndef LORD_WL_H
#define LORD_WL_H

#include "../canvas.h"
#include "../keyboard.h"
#include <stdint.h>
#include <sys/types.h>

struct Node;

int  wl_start(void);                    /* después de soltar privilegios; 0 bien */
void wl_stop(void);
int  wl_fd(void);                       /* para poll; -1 si no arrancó */
void wl_dispatch(void);                 /* hay algo en wl_fd */
void wl_flush(void);                    /* manda lo pendiente a los clientes */
const char *wl_socket(void);            /* "wayland-0" */
void wl_frame_done(unsigned time_ms);   /* después de presentar un cuadro */

/* Entrada (main.c). hit = nodo bajo el puntero; down[i]/button[i] = botones de este evento
 * (0 izquierdo, 1 medio, 2 derecho). Devuelve 1 si el puntero está sobre una app Wayland y 2 si
 * está sobre uno de sus menús emergentes (que se dibujan encima de todo: el clic no es del tema). */
int  wl_pointer(float x, float y, struct Node *hit, int moved, int nbuttons, const int *down, const int *button, int wheel);
/* Mientras lord-server mueve o agranda una ventana (ui/grab.c): ninguna app recibe el puntero
 * (se les manda leave) y solo se lleva la cuenta de los botones. */
void wl_pointer_grab(int nbuttons, const int *down);
int  wl_key(int code, int value, const KeyInfo *k);   /* 1 si la tecla fue a la app con foco */
void wl_keymap_changed(void);
int  wl_send_paste(void);               /* Ctrl+V sintético a la app Linux con el teclado */
int  wl_has_key_focus(void);            /* ¿una app Linux tiene el teclado? */

/* Desde JS (lord.wayland) */
void wl_set_focus(int id);              /* 0: ninguna app Wayland tiene el teclado */
void wl_close(int id);                  /* le pide a la app que cierre la ventana */
void wl_set_state(int id, int maximized, int fullscreen);
int  wl_count(void);
void wl_output_resized(void);               /* cambió ui.width/ui.height */

/* Dibujo (paint.c): la superficie id en la caja del nodo, y los menús emergentes encima de todo.
 * En el camino GPU (src/gpu) la UI solo deja un hueco y las superficies se dibujan al presentar. */
void wl_paint_begin(void);
void wl_paint_keep(void);                /* guarda las ventanas anotadas hasta acá (paint.c, «lo de abajo») */
void wl_paint_kept(void);                /* y las vuelve a anotar en otro cuadro */
void wl_paint_holes(int on);             /* 0: wl_paint solo anota la ventana, sin abrir el hueco */
void wl_paint(Canvas *c, int id, int node, float x, float y, float w, float h, float op);
void wl_paint_popups(Canvas *c);

/* Presentación (main.c). */
void wl_gpu_draw_layer(int layer);          /* 0 = ventanas (debajo de la UI), 1 = menús y pantalla completa */
void wl_paint_fullscreen(Canvas *out);      /* camino CPU: la ventana a pantalla completa sobre el cuadro */
int  wl_fullscreen_active(void);
int  wl_try_scanout(int cursor_free);       /* camino GPU: 1 si la pantalla completa fue directo a la placa */
int  wl_wants_frame(void);                  /* una app cambió su contenido */
void wl_presented(uint64_t ns, unsigned seq, unsigned refresh_ns);   /* terminó un flip (wp_presentation) */
void wl_set_drm_device(dev_t dev);          /* dispositivo DRM para linux-dmabuf (camino CPU) */

/* Puntero: movimiento relativo (juegos), bloqueo y confinamiento (pointer-constraints). */
void wl_pointer_relative(double dx, double dy);
int  wl_pointer_locked(void);
void wl_pointer_confine(int *x, int *y);
/* Cursor que pidió la app bajo el puntero: 0 = el de LordOS, 1 = imagen (px ARGB), 2 = oculto.
 * gen cambia cuando hay que volver a dibujarlo. */
int  wl_cursor(const uint32_t **px, int *w, int *h, int *hx, int *hy, unsigned *gen);

/* Portapapeles: última imagen copiada por una app Linux (o elegida del historial). */
unsigned    wl_clip_image_gen(void);
const char *wl_clip_image_path(void);       /* "" si lo último copiado no es una imagen */
int         wl_clip_is_image(void);
const unsigned char *wl_clip_png(size_t *len);   /* el PNG copiado, o NULL */
const char *wl_clip_image_origin(void);
int         wl_clip_image_sensitive(void);
int         wl_clip_use_image(const char *path);   /* solo $XDG_RUNTIME_DIR/lordos/clip-N.png */

/* Eventos para JS: map, unmap, title, appid, geometry, move, resize, maximize, unmaximize,
 * minimize, fullscreen, unfullscreen. data es JSON. */
typedef void (*WlEventFn)(int id, const char *type, const char *json);
void wl_events(WlEventFn fn);

#endif

/* media.h — medios en lord-server (ver media.c y docs/MEDIOS.md).
 *
 * lord-server NUNCA enlaza FFmpeg: lanza su ayudante lord-media (ruta fija, sin argumentos) y le
 * pasa los archivos ya abiertos. Acá viven:
 *   - los ayudantes (reproductor, cámara, grabaciones y dos «workers» de trabajos cortos);
 *   - los «orígenes» de imagen que dibuja un nodo `media` (<MediaView>): cuadros de video por
 *     memoria compartida, fotos decodificadas, capturas de pantalla;
 *   - las capturas: pantalla, grabación y escritorio remoto, siempre con las ventanas
 *     protegidas (`protected`) en negro. */
#ifndef LORD_MEDIA_H
#define LORD_MEDIA_H

#include <poll.h>
#include <stdint.h>
#include "../canvas.h"

struct Node;

/* bucle principal (main.c) */
int  media_pollfds(struct pollfd *p, int max);
void media_dispatch(const struct pollfd *p, int n);
typedef void (*MediaEventFn)(int id, const char *type, const char *json);
void media_events(MediaEventFn fn);
int  media_wants_frame(void);           /* una grabación espera el próximo cuadro */
int  media_needs_cpu(void);             /* grabando la pantalla: componer por CPU (como el escritorio remoto) */
void media_shutdown(void);              /* cerrar todo (cambio de tema o salida) */

/* dibujo de un nodo media (paint.c) */
void media_paint(Canvas *c, int handle, int node, float x, float y, float w, float h, float radius, float op);

/* Nodo media opaco que contiene toda la región (x0,y0)-(x1,y1), para pintar solo desde él (paint.c). 0 si no hay. */
int  media_cover_node(float x0, float y0, float x1, float y1);

/* capturas (main.c): después de pintar la escena y después de armar cada cuadro. */
void media_scene_painted(const Canvas *scene);
typedef struct { int x, y, mode; const uint32_t *px; int w, h, hx, hy; } MediaCursor;
/* Devuelve lo que ve el escritorio remoto (las ventanas protegidas en negro) y alimenta la
 * grabación. out = el cuadro ya compuesto (sin cursor). */
const Canvas *media_composed(const Canvas *out, int remote, const MediaCursor *cur);

/* Ventanas protegidas presentes en el árbol (las cuenta en el momento). */
int media_protected_count(void);

#endif

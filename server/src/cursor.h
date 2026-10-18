/* cursor.h — formas del cursor de LordOS (dibujo propio, vectorial, a 1x y 2x). Ver docs/VENTANAS.md. */
#ifndef LORD_CURSOR_H
#define LORD_CURSOR_H

#include <stdint.h>

/* El orden coincide con CURSOR_CSS_NAMES (estilo `cursor` de los nodos, ui/style.c). */
enum {
    CUR_AUTO = 0, CUR_DEFAULT, CUR_POINTER, CUR_TEXT, CUR_MOVE, CUR_GRAB, CUR_GRABBING, CUR_NOT_ALLOWED,
    CUR_WAIT, CUR_PROGRESS, CUR_CROSSHAIR,
    CUR_N, CUR_S, CUR_E, CUR_W, CUR_NE, CUR_NW, CUR_SE, CUR_SW, CUR_EW, CUR_NS, CUR_NESW, CUR_NWSE,
    CUR_COUNT
};
#define CURSOR_CSS_NAMES "auto", "default", "pointer", "text", "move", "grab", "grabbing", "not-allowed", "wait", \
    "progress", "crosshair", "n-resize", "s-resize", "e-resize", "w-resize", "ne-resize", "nw-resize", "se-resize", \
    "sw-resize", "ew-resize", "ns-resize", "nesw-resize", "nwse-resize"

typedef struct {
    const uint32_t *px;     /* ARGB premultiplicado */
    int w, h, hx, hy;       /* tamaño y punto activo */
} CursorImage;

/* Imagen de una forma (se dibuja la primera vez y queda guardada). scale: 1 o 2. */
const CursorImage *cursor_image(int shape, int scale);
/* Escala de los cursores para una pantalla de W×H (2 en 4K/HiDPI). */
int cursor_scale_for(int W, int H);
/* Bordes de xdg_toplevel.resize (1 arriba, 2 abajo, 4 izquierda, 8 derecha) → forma. */
int cursor_for_edges(int edges);
/* Forma de cursor-shape-v1 (wp_cursor_shape_device_v1.shape) → forma propia; 0 si no hay. */
int cursor_from_wayland_shape(unsigned shape);

#endif

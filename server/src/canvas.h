/* canvas.h — rasterizador 2D por software (XRGB8888), propio. */
#ifndef LORD_CANVAS_H
#define LORD_CANVAS_H

#include <stdint.h>

typedef struct { int x0, y0, x1, y1; } Rect;

typedef struct {
    uint32_t *px;
    int w, h, stride;   /* stride en píxeles */
    Rect clip;
} Canvas;

enum { FIT_FILL, FIT_COVER, FIT_CONTAIN };

/* Colores en 0xAARRGGBB; AA = opacidad. Coordenadas en float con antialiasing. */
void canvas_init(Canvas *c, uint32_t *px, int w, int h, int stride);
Rect canvas_push_clip(Canvas *c, float x, float y, float w, float h);   /* devuelve el clip anterior */
void canvas_set_clip(Canvas *c, Rect r);
void canvas_copy(Canvas *dst, const Canvas *src);
void canvas_clear(Canvas *c, uint32_t rgb);        /* solo dentro del clip */

void canvas_rrect(Canvas *c, float x, float y, float w, float h, float r, uint32_t argb);
void canvas_rrect_gradient(Canvas *c, float x, float y, float w, float h, float r,
                           uint32_t from, uint32_t to, float angle_deg, float opacity);
void canvas_rrect_stroke(Canvas *c, float x, float y, float w, float h, float r, float bw, uint32_t argb);
/* Sombra estilo CSS box-shadow: no se pinta debajo de la caja. */
void canvas_shadow(Canvas *c, float x, float y, float w, float h, float r,
                   float dx, float dy, float blur, uint32_t argb);
/* backdrop-filter: blur() sobre lo ya dibujado, recortado al rectángulo redondeado. */
void canvas_backdrop_blur(Canvas *c, float x, float y, float w, float h, float r, int radius);

/* Esquinas: guardar antes de pintar un nodo con overflow:hidden y restaurar después. */
typedef struct { int x[4], y[4], n; uint32_t *buf; } Corners;
void canvas_corners_save(Canvas *c, Corners *k, float x, float y, float w, float h, float r);
void canvas_corners_restore(Canvas *c, Corners *k, float x, float y, float w, float h, float r);

/* Máscara de cobertura de 8 bits (glifos). */
void canvas_alpha_mask(Canvas *c, int x, int y, const uint8_t *a, int w, int h, int stride, uint32_t argb);
/* Imagen ARGB premultiplicada, con esquinas redondeadas y opacidad. */
void canvas_blit(Canvas *c, const uint32_t *src, int sw, int sh, int x, int y, float r, float opacity, int opaque);
/* Bitmap de caracteres: 'X' = fg, 'o' = bg, resto transparente (cursor). */
void canvas_bitmap(Canvas *c, int x, int y, const char *const *rows, uint32_t fg, uint32_t bg);

#endif

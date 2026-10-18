/* canvas.h — our own 2D software rasterizer (XRGB8888). */
#ifndef LORD_CANVAS_H
#define LORD_CANVAS_H

#include <stdint.h>

typedef struct { int x0, y0, x1, y1; } Rect;

typedef struct {
    uint32_t *px;
    int w, h, stride;   /* stride in pixels */
    Rect clip;
} Canvas;

enum { FIT_FILL, FIT_COVER, FIT_CONTAIN };

/* Colors as 0xAARRGGBB; AA = opacity. Float coordinates with antialiasing. */
void canvas_init(Canvas *c, uint32_t *px, int w, int h, int stride);
Rect canvas_push_clip(Canvas *c, float x, float y, float w, float h);   /* returns the previous clip */
void canvas_set_clip(Canvas *c, Rect r);
void canvas_copy(Canvas *dst, const Canvas *src);
void canvas_clear(Canvas *c, uint32_t rgb);        /* only inside the clip */

void canvas_rrect(Canvas *c, float x, float y, float w, float h, float r, uint32_t argb);
void canvas_rrect_gradient(Canvas *c, float x, float y, float w, float h, float r,
                           uint32_t from, uint32_t to, float angle_deg, float opacity);
void canvas_rrect_stroke(Canvas *c, float x, float y, float w, float h, float r, float bw, uint32_t argb);
/* CSS box-shadow style shadow: not painted under the box. */
void canvas_shadow(Canvas *c, float x, float y, float w, float h, float r,
                   float dx, float dy, float blur, uint32_t argb);
/* backdrop-filter: blur() over what is already drawn, clipped to the rounded rectangle. */
void canvas_backdrop_blur(Canvas *c, float x, float y, float w, float h, float r, int radius);

/* Corners: save before painting a node with overflow:hidden and restore afterwards. */
typedef struct { int x[4], y[4], n; uint32_t *buf; } Corners;
void canvas_corners_save(Canvas *c, Corners *k, float x, float y, float w, float h, float r);
void canvas_corners_restore(Canvas *c, Corners *k, float x, float y, float w, float h, float r);

/* 8-bit coverage mask (glyphs). */
void canvas_alpha_mask(Canvas *c, int x, int y, const uint8_t *a, int w, int h, int stride, uint32_t argb);
/* Premultiplied ARGB image, with rounded corners and opacity. */
void canvas_blit(Canvas *c, const uint32_t *src, int sw, int sh, int x, int y, float r, float opacity, int opaque);
/* Character bitmap: 'X' = fg, 'o' = bg, rest transparent (cursor). */
/* Scaled, bilinear: source rect (sx, sy, sw, sh) of an image sw_px × sh_px (stride in pixels) into the
 * destination rect (dx, dy, dw, dh). Wayland buffers at another scale and wp_viewporter (CPU path). */
void canvas_blit_scaled(Canvas *c, const uint32_t *src, int stride, int sw_px, int sh_px, float sx, float sy, float sw, float sh,
                        float dx, float dy, float dw, float dh, float opacity, int opaque);
void canvas_bitmap(Canvas *c, int x, int y, const char *const *rows, uint32_t fg, uint32_t bg);

#endif

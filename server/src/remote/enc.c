/* enc.c — compression of screen rectangles for the browser (lord-sessiond).
 *
 * PNG (lossless, ideal for text and UI) or JPEG (photos, videos, large gradients),
 * with vendored stb_image_write. It picks on its own: if the PNG of a large rectangle comes out heavy
 * (more than 1 byte per pixel) or the screen is in motion (allow_jpeg 2: straight to JPEG,
 * without trying PNG), JPEG is sent and the bridge replaces it with PNG once the screen stays
 * still (conn.c). */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "stb_image_write.h"
#pragma GCC diagnostic pop

#include "sessiond.h"

#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char *p; int n, cap; } Buf;

static void put(void *ctx, void *data, int size) {
    Buf *b = ctx;
    if (b->n + size > b->cap) {
        int cap = b->cap ? b->cap * 2 : 65536;
        while (cap < b->n + size) cap *= 2;
        unsigned char *nb = realloc(b->p, (size_t)cap);
        if (!nb) return;
        b->p = nb;
        b->cap = cap;
    }
    memcpy(b->p + b->n, data, (size_t)size);
    b->n += size;
}

static unsigned char *to_rgb(const uint32_t *px, int stride, int w, int h) {
    unsigned char *rgb = malloc((size_t)w * h * 3);
    if (!rgb) return NULL;
    unsigned char *o = rgb;
    for (int y = 0; y < h; y++) {
        const uint32_t *row = px + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            uint32_t v = row[x];
            *o++ = (unsigned char)(v >> 16);
            *o++ = (unsigned char)(v >> 8);
            *o++ = (unsigned char)v;
        }
    }
    return rgb;
}

unsigned char *rd_encode(const uint32_t *px, int stride, int w, int h, int allow_jpeg, int *len, int *jpeg) {
    *jpeg = 0;
    *len = 0;
    unsigned char *rgb = to_rgb(px, stride, w, h);
    if (!rgb) return NULL;
    long area = (long)w * h;
    if (allow_jpeg == 2 && area >= 16384) {
        Buf b = { 0 };
        stbi_write_jpg_to_func(put, &b, w, h, 3, rgb, 70);
        free(rgb);
        if (!b.p) return NULL;
        *len = b.n;
        *jpeg = 1;
        return b.p;
    }
    stbi_write_png_compression_level = 2;
    int n = 0;
    unsigned char *png = stbi_write_png_to_mem(rgb, w * 3, w, h, 3, &n);
    if (png && (!allow_jpeg || area < 16384 || n <= area)) {
        free(rgb);
        *len = n;
        return png;
    }
    Buf b = { 0 };
    stbi_write_jpg_to_func(put, &b, w, h, 3, rgb, 80);
    free(rgb);
    if (b.p && (!png || b.n < n)) {
        free(png);
        *len = b.n;
        *jpeg = 1;
        return b.p;
    }
    free(b.p);
    *len = n;
    return png;
}

unsigned char *rd_encode_rgba(const uint32_t *px, int w, int h, int *len) {
    unsigned char *rgba = malloc((size_t)w * h * 4);
    if (!rgba) return NULL;
    for (int i = 0; i < w * h; i++) {
        uint32_t v = px[i];
        unsigned a = v >> 24;
        /* the cursor image comes premultiplied: convert back to the real color */
        unsigned r = (v >> 16) & 255, g = (v >> 8) & 255, bl = v & 255;
        if (a && a < 255) { r = r * 255 / a; g = g * 255 / a; bl = bl * 255 / a; }
        rgba[i * 4] = (unsigned char)(r > 255 ? 255 : r);
        rgba[i * 4 + 1] = (unsigned char)(g > 255 ? 255 : g);
        rgba[i * 4 + 2] = (unsigned char)(bl > 255 ? 255 : bl);
        rgba[i * 4 + 3] = (unsigned char)a;
    }
    unsigned char *png = stbi_write_png_to_mem(rgba, w * 4, w, h, 4, len);
    free(rgba);
    return png;
}

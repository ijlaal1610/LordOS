/* gpu.h — composición por GPU (GBM + EGL + GLES2 de Mesa) y scanout directo (ver gpu.c y
 * docs/GPU.md). Si no hay GPU usable, gpu_init falla y lord-server sigue por CPU como siempre. */
#ifndef LORD_GPU_H
#define LORD_GPU_H

#include <stdint.h>
#include <sys/types.h>
#include "../canvas.h"
#include "../kms.h"

typedef struct GpuTex GpuTex;

/* Un búfer dmabuf de una app (hasta 4 planos). */
typedef struct {
    int width, height, nplanes;
    uint32_t fourcc;
    uint64_t modifier;                  /* DRM_FORMAT_MOD_INVALID = implícito */
    int fd[4];
    uint32_t offset[4], stride[4];
} GpuDmabuf;

typedef struct { uint32_t fourcc; uint64_t modifier; } GpuFormat;

int  gpu_init(Kms *k);                  /* 0 si quedó activo; LORD_GPU=0 lo apaga */
int  gpu_active(void);
void gpu_shutdown(void);
const char *gpu_renderer(void);         /* "llvmpipe (LLVM …)", "AMD Radeon …" */

/* Formatos/modificadores que se pueden importar (linux-dmabuf) y el dispositivo DRM. */
int  gpu_formats(GpuFormat *out, int max);
int  gpu_device(dev_t *dev);

/* Texturas: desde píxeles ARGB premultiplicados (shm) o desde un dmabuf (EGLImage). */
GpuTex *gpu_tex_pixels(GpuTex *t, const uint32_t *px, int w, int h, int opaque);
GpuTex *gpu_tex_dmabuf(const GpuDmabuf *d);
void    gpu_tex_free(GpuTex *t);

/* Un cuadro: se sube lo que cambió del canvas de la UI, se dibujan las capas y se hace el flip.
 * draw_layer(0) = ventanas de apps (debajo de la UI), draw_layer(1) = menús de las apps y
 * pantalla completa (encima). */
int  gpu_frame(int out, const Canvas *scene, Rect damage, void (*draw_layer)(int layer));
/* Another display (docs/DISPLAYS.md): its own GBM/EGL surface on the same context; gpu_init made
 * output 0. Returns the output index for gpu_frame, or -1 (then that display goes by the CPU). */
int  gpu_add_output(Kms *k);
void gpu_draw(GpuTex *t, float dx, float dy, float dw, float dh,
              float sx, float sy, float sw, float sh, Rect clip, float opacity);

/* Scanout directo de un dmabuf a pantalla completa (sin componer). done(data) se llama cuando
 * el búfer deja de estar en pantalla. 0 si se mandó el flip. */
int  gpu_scanout(const GpuDmabuf *d, void (*done)(void *), void *data);
/* ¿Se puede mostrar directo? (tamaño de la salida, formato y modificador del plano primario) */
int  gpu_can_scanout(const GpuDmabuf *d, int w, int h);

#endif

/* gpu.h — GPU composition (Mesa's GBM + EGL + GLES2) and direct scanout (see gpu.c and
 * docs/GPU.md). If there is no usable GPU, gpu_init fails and lord-server keeps going by CPU as always. */
#ifndef LORD_GPU_H
#define LORD_GPU_H

#include <stdint.h>
#include <sys/types.h>
#include "../canvas.h"
#include "../kms.h"

typedef struct GpuTex GpuTex;

/* An app's dmabuf buffer (up to 4 planes). */
typedef struct {
    int width, height, nplanes;
    uint32_t fourcc;
    uint64_t modifier;                  /* DRM_FORMAT_MOD_INVALID = implicit */
    int fd[4];
    uint32_t offset[4], stride[4];
} GpuDmabuf;

typedef struct { uint32_t fourcc; uint64_t modifier; } GpuFormat;

int  gpu_init(Kms *k);                  /* 0 if it is active; LORD_GPU=0 turns it off */
int  gpu_active(void);
void gpu_shutdown(void);
const char *gpu_renderer(void);         /* "llvmpipe (LLVM …)", "AMD Radeon …" */

/* Formats/modifiers that can be imported (linux-dmabuf) and the DRM device. */
int  gpu_formats(GpuFormat *out, int max);
int  gpu_device(dev_t *dev);

/* Textures: from premultiplied ARGB pixels (shm) or from a dmabuf (EGLImage). */
GpuTex *gpu_tex_pixels(GpuTex *t, const uint32_t *px, int w, int h, int opaque);
GpuTex *gpu_tex_dmabuf(const GpuDmabuf *d);
void    gpu_tex_free(GpuTex *t);

/* GPU context lifetime. Each gpu_init that succeeds starts a new context ("generation"); a
 * texture made in an earlier one is stale: its GL name and EGLImage died with that context, so it
 * must never be drawn and gpu_tex_free only frees its memory. The session leaving and coming back
 * to the monitor, a remote viewer, a display change or a failed frame all re-create the context. */
int      gpu_tex_current(const GpuTex *t);  /* 1 if t belongs to the live context */
unsigned gpu_generation(void);              /* 0 while there is no GPU */
/* Hooks for the compositor: lost() runs inside gpu_shutdown with the old context still current
 * (last chance to read textures back or free them); changed() runs once the switch is done, after
 * a successful gpu_init and after gpu_shutdown (to re-import buffers and re-send dmabuf feedback). */
void gpu_set_hooks(void (*lost)(void), void (*changed)(void));
/* Reads a texture back as premultiplied ARGB pixels (t->w × t->h, rows top to bottom), for the
 * last good frame of a dmabuf window when the GPU goes away. 0 on success. */
int  gpu_tex_read(const GpuTex *t, uint32_t *px, int w, int h);

/* One frame: what changed in the UI canvas is uploaded, the layers are drawn and the flip is done.
 * draw_layer(0) = app windows (below the UI), draw_layer(1) = app menus and
 * fullscreen (on top). */
int  gpu_frame(int out, const Canvas *scene, Rect damage, void (*draw_layer)(int layer));
/* Another display (docs/DISPLAYS.md): its own GBM/EGL surface on the same context; gpu_init made
 * output 0. Returns the output index for gpu_frame, or -1 (then that display goes by the CPU). */
int  gpu_add_output(Kms *k);
void gpu_draw(GpuTex *t, float dx, float dy, float dw, float dh,
              float sx, float sy, float sw, float sh, Rect clip, float opacity);

/* Direct scanout of a fullscreen dmabuf (without composing). done(data) is called when
 * the buffer leaves the screen. 0 if the flip was sent. */
int  gpu_scanout(const GpuDmabuf *d, void (*done)(void *), void *data);
/* Can it be shown directly? (output size, format and primary plane modifier) */
int  gpu_can_scanout(const GpuDmabuf *d, int w, int h);

#endif

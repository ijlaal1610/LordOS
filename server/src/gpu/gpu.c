/* gpu.c — composición por GPU de lord-server: GBM + EGL + GLES2 de Mesa (aprobado por el
 * usuario el 2026-10-16; ver docs/GPU.md).
 *
 * El modesetting sigue siendo el propio (kms.c, ioctls sin libdrm): GBM se crea sobre el mismo
 * fd de la tarjeta y sus búferes pasan a framebuffers con DRM_IOCTL_MODE_ADDFB2.
 *
 * Cada cuadro: la UI de LordOS (el canvas que pinta la CPU) es una textura de pantalla completa a
 * la que solo se le sube lo que cambió. Debajo se dibujan las ventanas de las apps (texturas de
 * sus búferes shm o dmabuf, sin pasar por la CPU) y encima los menús de las apps; donde hay una
 * ventana de app, la UI deja un hueco transparente (alfa 0). Una app a pantalla completa con un
 * búfer compatible va directo al plano primario: cero copias (gpu_scanout).
 *
 * Por defecto se activa solo con una GPU de verdad: con un renderer por software (llvmpipe,
 * softpipe) el camino por CPU propio es más rápido. LORD_GPU=1 lo fuerza (pruebas), LORD_GPU=0
 * lo apaga, LORD_GPU_DEBUG=1 cuenta qué hace. Compilado sin Mesa (sin -DLORD_GPU) son stubs. */
#include "gpu.h"
#include "../outputs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LORD_GPU

int  gpu_init(Kms *k) { (void)k; return -1; }
int  gpu_active(void) { return 0; }
void gpu_shutdown(void) {}
const char *gpu_renderer(void) { return ""; }
int  gpu_formats(GpuFormat *out, int max) { (void)out; (void)max; return 0; }
int  gpu_device(dev_t *dev) { (void)dev; return -1; }
GpuTex *gpu_tex_pixels(GpuTex *t, const uint32_t *px, int w, int h, int opaque) { (void)t; (void)px; (void)w; (void)h; (void)opaque; return NULL; }
GpuTex *gpu_tex_dmabuf(const GpuDmabuf *d) { (void)d; return NULL; }
void gpu_tex_free(GpuTex *t) { (void)t; }
int  gpu_add_output(Kms *k) { (void)k; return -1; }
int  gpu_frame(int out, const Canvas *s, Rect d, void (*f)(int)) { (void)out; (void)s; (void)d; (void)f; return -1; }
void gpu_draw(GpuTex *t, float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh, Rect c, float o) {
    (void)t; (void)dx; (void)dy; (void)dw; (void)dh; (void)sx; (void)sy; (void)sw; (void)sh; (void)c; (void)o;
}
int  gpu_scanout(const GpuDmabuf *d, void (*done)(void *), void *data) { (void)d; (void)done; (void)data; return -1; }
int  gpu_can_scanout(const GpuDmabuf *d, int w, int h) { (void)d; (void)w; (void)h; return 0; }

#else

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm/drm_fourcc.h>
#include <drm/virtgpu_drm.h>
#include <sys/ioctl.h>

/* ¿La placa tiene aceleración 3D? Se pregunta al kernel antes de cargar Mesa, así una máquina
 * sin GPU (o una VM sin virgl) no carga llvmpipe para nada. */
static int drm_accelerated(int fd, char *name, size_t n) {
    struct drm_version v;
    memset(&v, 0, sizeof v);
    v.name = name;
    v.name_len = n - 1;
    if (ioctl(fd, DRM_IOCTL_VERSION, &v)) { snprintf(name, n, "?"); return 1; }
    name[v.name_len < n - 1 ? v.name_len : n - 1] = 0;
    static const char *const plain[] = { "simpledrm", "bochs-drm", "bochs", "cirrus", "qxl", "ast", "mgag200", "vkms",
                                         "udl", "gm12u320", "hyperv_drm", "vboxvideo", "ofdrm", "efidrm", "vesadrm", NULL };
    for (int i = 0; plain[i]; i++) if (!strcmp(name, plain[i])) return 0;
    if (!strcmp(name, "virtio_gpu")) {
        int has3d = 0;
        struct drm_virtgpu_getparam g = { .param = VIRTGPU_PARAM_3D_FEATURES, .value = (uintptr_t)&has3d };
        if (ioctl(fd, DRM_IOCTL_VIRTGPU_GETPARAM, &g)) return 0;
        return has3d != 0;
    }
    return 1;
}

struct GpuTex {
    GLuint id;
    int w, h, opaque, external, swap;
    EGLImageKHR image;
};

/* Qué hay en pantalla (o en camino): un BO del gbm_surface o un dmabuf de una app. */
typedef struct {
    int kind;                           /* 0 nada, 1 BO de GBM, 2 scanout directo */
    struct gbm_bo *bo;
    uint32_t fb, handles[4];
    void (*done)(void *);
    void *data;
} Shown;

/* One display (docs/DISPLAYS.md): its GBM surface and EGL window surface, the texture of its UI frame
 * and what is on its screen. Every display shares the context, programs and app textures. */
typedef struct {
    int used, first;
    Kms *k;
    int W, H;
    struct gbm_surface *gs;
    EGLSurface surf;
    GpuTex canvas;
    Shown front, pending;
} GOut;

static struct {
    int on, debug;
    Kms *k;                             /* the first display's head (the card's fd) */
    struct gbm_device *gbm;
    EGLDisplay dpy;
    EGLContext ctx;
    EGLConfig cfg;
    GOut out[OUT_MAX];
    GOut *cur;                          /* the display being drawn (gpu_draw) */
    GLuint prog[2], vbo;                /* 0 = sampler2D, 1 = samplerExternalOES */
    GLint u_tex[2], u_op[2], u_opaque[2], u_swap[2];
    int bgra, have_external;
    char renderer[160];
    PFNEGLCREATEIMAGEKHRPROC create_image;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target;
    PFNEGLQUERYDMABUFFORMATSEXTPROC query_formats;
    PFNEGLQUERYDMABUFMODIFIERSEXTPROC query_modifiers;
    long frames, scanouts;
    char render_node[64];               /* where Mesa renders (on the Pi 5: v3d, not vc4) */
} G;

static void dbg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void dbg(const char *fmt, ...) {
    if (!G.debug) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "gpu: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static int has_ext(const char *list, const char *ext) {
    size_t n = strlen(ext);
    for (const char *p = list; p && (p = strstr(p, ext)); p += n)
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || !p[n])) return 1;
    return 0;
}

/* El render node que usa EGL (EGL_EXT_device_query + EGL_EXT_device_drm_render_node). En las
 * placas con pantalla y GPU separadas (Raspberry Pi 4/5: vc4 + v3d; muchos ARM con kmsro) no es
 * el de la tarjeta de la pantalla, y es el que las apps tienen que usar para sus búferes. */
#ifndef EGL_DRM_RENDER_NODE_FILE_EXT
#  define EGL_DRM_RENDER_NODE_FILE_EXT 0x3377
#endif
#ifndef EGL_DEVICE_EXT
#  define EGL_DEVICE_EXT 0x322C
#endif
static void query_render_node(void) {
    G.render_node[0] = 0;
    const char *cl = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!has_ext(cl, "EGL_EXT_device_query") && !has_ext(cl, "EGL_EXT_device_base")) return;
    typedef EGLBoolean (*QueryDisplayAttrib)(EGLDisplay, EGLint, EGLAttrib *);
    typedef const char *(*QueryDeviceString)(void *, EGLint);
    QueryDisplayAttrib qda = (QueryDisplayAttrib)eglGetProcAddress("eglQueryDisplayAttribEXT");
    QueryDeviceString qds = (QueryDeviceString)eglGetProcAddress("eglQueryDeviceStringEXT");
    EGLAttrib dev = 0;
    if (!qda || !qds || !qda(G.dpy, EGL_DEVICE_EXT, &dev) || !dev) return;
    const char *dexts = qds((void *)dev, EGL_EXTENSIONS);
    if (!has_ext(dexts, "EGL_EXT_device_drm_render_node")) return;
    const char *node = qds((void *)dev, EGL_DRM_RENDER_NODE_FILE_EXT);
    if (node && !strncmp(node, "/dev/dri/renderD", 16) && strlen(node) < sizeof G.render_node)
        snprintf(G.render_node, sizeof G.render_node, "%s", node);
}

/* ---- shaders ---- */

static const char *vs_src =
    "attribute vec2 a_pos; attribute vec2 a_uv; varying vec2 v_uv;\n"
    "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
static const char *fs_2d =
    "precision mediump float; varying vec2 v_uv; uniform sampler2D u_tex;\n"
    "uniform float u_op; uniform float u_opaque; uniform float u_swap;\n"
    "void main() { vec4 c = texture2D(u_tex, v_uv); c = mix(c, c.bgra, u_swap);\n"
    "  if (u_opaque > 0.5) c.a = 1.0; gl_FragColor = c * u_op; }\n";
static const char *fs_ext =
    "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float; varying vec2 v_uv; uniform samplerExternalOES u_tex;\n"
    "uniform float u_op; uniform float u_opaque; uniform float u_swap;\n"
    "void main() { vec4 c = texture2D(u_tex, v_uv);\n"
    "  if (u_opaque > 0.5) c.a = 1.0; gl_FragColor = c * u_op; }\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "gpu: shader: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static int make_prog(int i, const char *fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs_src), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return -1;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_uv");
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) return -1;
    G.prog[i] = p;
    G.u_tex[i] = glGetUniformLocation(p, "u_tex");
    G.u_op[i] = glGetUniformLocation(p, "u_op");
    G.u_opaque[i] = glGetUniformLocation(p, "u_opaque");
    G.u_swap[i] = glGetUniformLocation(p, "u_swap");
    return 0;
}

/* ---- flips ---- */

static void release(GOut *o, Shown *s) {
    if (s->kind == 1 && s->bo) gbm_surface_release_buffer(o->gs, s->bo);
    if (s->kind == 2) {
        kms_rm_fb(G.k, s->fb);
        for (int i = 0; i < 4; i++) {
            int dup = 0;
            for (int j = 0; j < i; j++) if (s->handles[j] == s->handles[i]) dup = 1;
            if (!dup) kms_close_handle(G.k, s->handles[i]);
        }
        if (s->done) s->done(s->data);
    }
    memset(s, 0, sizeof *s);
}

static void on_flip(void *data) {
    GOut *o = data;
    if (!o || o < G.out || o >= G.out + OUT_MAX || !o->used) return;
    release(o, &o->front);
    o->front = o->pending;
    memset(&o->pending, 0, sizeof o->pending);
}

static void bo_fb_destroy(struct gbm_bo *bo, void *data) {
    (void)bo;
    uint32_t fb = (uint32_t)(uintptr_t)data;
    if (G.k) kms_rm_fb(G.k, fb);
}

static uint32_t fb_for_bo(struct gbm_bo *bo) {
    uint32_t fb = (uint32_t)(uintptr_t)gbm_bo_get_user_data(bo);
    if (fb) return fb;
    uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
    int n = gbm_bo_get_plane_count(bo);
    for (int i = 0; i < n && i < 4; i++) {
        handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
        pitches[i] = gbm_bo_get_stride_for_plane(bo, i);
        offsets[i] = gbm_bo_get_offset(bo, i);
    }
    uint64_t mod = gbm_bo_get_modifier(bo);
    if (kms_add_fb2(G.k, gbm_bo_get_width(bo), gbm_bo_get_height(bo), gbm_bo_get_format(bo), handles, pitches, offsets,
                    mod, &fb) &&
        kms_add_fb2(G.k, gbm_bo_get_width(bo), gbm_bo_get_height(bo), gbm_bo_get_format(bo), handles, pitches, offsets,
                    DRM_FORMAT_MOD_INVALID, &fb)) {
        fprintf(stderr, "gpu: ADDFB2 del BO falló (%s)\n", strerror(errno));
        return 0;
    }
    gbm_bo_set_user_data(bo, (void *)(uintptr_t)fb, bo_fb_destroy);
    return fb;
}

static int present(GOut *o, Shown next) {
    if (o->first) {
        if (kms_set_fb(o->k, next.fb)) { fprintf(stderr, "gpu: SETCRTC failed (%s)\n", strerror(errno)); release(o, &next); return -1; }
        o->first = 0;
        release(o, &o->front);
        o->front = next;
        return 0;
    }
    o->pending = next;
    if (kms_flip_fb(o->k, next.fb, o)) {
        fprintf(stderr, "gpu: page flip failed (%s)\n", strerror(errno));
        memset(&o->pending, 0, sizeof o->pending);
        release(o, &next);
        return -1;
    }
    return 0;
}

/* A display's GBM surface, EGL surface and UI texture (the context already exists). */
static int out_setup(GOut *o, Kms *k) {
    memset(o, 0, sizeof *o);
    o->k = k;
    o->W = (int)k->width;
    o->H = (int)k->height;
    o->gs = gbm_surface_create(G.gbm, (uint32_t)o->W, (uint32_t)o->H, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!o->gs) return -1;
    o->surf = eglCreateWindowSurface(G.dpy, G.cfg, (EGLNativeWindowType)o->gs, NULL);
    if (o->surf == EGL_NO_SURFACE || !eglMakeCurrent(G.dpy, o->surf, o->surf, G.ctx)) {
        if (o->surf != EGL_NO_SURFACE) eglDestroySurface(G.dpy, o->surf);
        gbm_surface_destroy(o->gs);
        memset(o, 0, sizeof *o);
        return -1;
    }
    k->on_flip = on_flip;
    o->first = 1;
    o->used = 1;
    G.cur = o;
    return 0;
}

/* Its UI texture (after the GL extensions are known: BGRA or RGBA). */
static void out_texture(GOut *o) {
    glGenTextures(1, &o->canvas.id);
    glBindTexture(GL_TEXTURE_2D, o->canvas.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, G.bgra ? GL_BGRA_EXT : GL_RGBA, o->W, o->H, 0, G.bgra ? GL_BGRA_EXT : GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    o->canvas.w = o->W;
    o->canvas.h = o->H;
    o->canvas.swap = !G.bgra;
}

static void out_teardown(GOut *o) {
    if (!o->used) return;
    release(o, &o->pending);
    release(o, &o->front);
    if (o->surf && eglMakeCurrent(G.dpy, o->surf, o->surf, G.ctx)) glDeleteTextures(1, &o->canvas.id);
    eglMakeCurrent(G.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (o->surf) eglDestroySurface(G.dpy, o->surf);
    if (o->gs) gbm_surface_destroy(o->gs);
    if (o->k) { o->k->on_flip = NULL; o->k->custom = 0; }
    memset(o, 0, sizeof *o);
}

int gpu_add_output(Kms *k) {
    if (!G.on) return -1;
    for (int i = 0; i < OUT_MAX; i++) {
        if (G.out[i].used) continue;
        if (out_setup(&G.out[i], k)) { fprintf(stderr, "gpu: no EGL surface for the display on connector %u\n", k->conn_id); return -1; }
        out_texture(&G.out[i]);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        return i;
    }
    return -1;
}

/* ---- inicio ---- */

int gpu_active(void) { return G.on; }
const char *gpu_renderer(void) { return G.renderer; }

int gpu_init(Kms *k) {
    const char *env = getenv("LORD_GPU");
    G.debug = getenv("LORD_GPU_DEBUG") != NULL;
    if (env && !strcmp(env, "0")) { fprintf(stderr, "gpu: apagado (LORD_GPU=0), composición por CPU\n"); return -1; }
    G.k = k;
    char drv[64];
    if (!drm_accelerated(k->fd, drv, sizeof drv) && !(env && !strcmp(env, "1"))) {
        fprintf(stderr, "gpu: la placa (%s) no tiene aceleración 3D: composición por CPU (LORD_GPU=1 lo fuerza)\n", drv);
        return -1;
    }
    G.gbm = gbm_create_device(k->fd);
    if (!G.gbm) { fprintf(stderr, "gpu: sin GBM en la tarjeta, composición por CPU\n"); return -1; }
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    G.dpy = get_display ? get_display(EGL_PLATFORM_GBM_KHR, G.gbm, NULL) : eglGetDisplay((EGLNativeDisplayType)G.gbm);
    EGLint maj, min;
    if (G.dpy == EGL_NO_DISPLAY || !eglInitialize(G.dpy, &maj, &min)) { fprintf(stderr, "gpu: EGL no arrancó, composición por CPU\n"); goto fail; }
    const char *exts = eglQueryString(G.dpy, EGL_EXTENSIONS);
    query_render_node();
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                       EGL_ALPHA_SIZE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfgs[64], cfg = NULL;
    EGLint n = 0;
    eglChooseConfig(G.dpy, cfg_attr, cfgs, 64, &n);
    for (int i = 0; i < n && !cfg; i++) {
        EGLint vid = 0;
        if (eglGetConfigAttrib(G.dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &vid) && vid == GBM_FORMAT_XRGB8888) cfg = cfgs[i];
    }
    if (!cfg) { fprintf(stderr, "gpu: sin configuración XRGB8888 de EGL, composición por CPU\n"); goto fail; }
    static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    G.ctx = eglCreateContext(G.dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    G.cfg = cfg;
    if (G.ctx == EGL_NO_CONTEXT || out_setup(&G.out[0], k)) { fprintf(stderr, "gpu: could not create the context or the surface, composing by CPU\n"); goto fail; }

    const char *rend = (const char *)glGetString(GL_RENDERER), *gl_exts = (const char *)glGetString(GL_EXTENSIONS);
    snprintf(G.renderer, sizeof G.renderer, "%s", rend ? rend : "?");
    int software = strstr(G.renderer, "llvmpipe") || strstr(G.renderer, "softpipe") || strstr(G.renderer, "swrast");
    if (software && !(env && !strcmp(env, "1"))) {
        fprintf(stderr, "gpu: el renderer es por software (%s): composición por CPU (LORD_GPU=1 lo fuerza)\n", G.renderer);
        goto fail;
    }
    G.bgra = has_ext(gl_exts, "GL_EXT_texture_format_BGRA8888");
    G.have_external = has_ext(gl_exts, "GL_OES_EGL_image_external");
    if (!has_ext(gl_exts, "GL_EXT_unpack_subimage")) dbg("sin GL_EXT_unpack_subimage: se sube el canvas entero");
    if (make_prog(0, fs_2d) || (G.have_external && make_prog(1, fs_ext))) { fprintf(stderr, "gpu: shaders, composición por CPU\n"); goto fail; }
    if (has_ext(exts, "EGL_EXT_image_dma_buf_import")) {
        G.create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
        G.destroy_image = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
        G.image_target = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    }
    if (has_ext(exts, "EGL_EXT_image_dma_buf_import_modifiers")) {
        G.query_formats = (PFNEGLQUERYDMABUFFORMATSEXTPROC)eglGetProcAddress("eglQueryDmaBufFormatsEXT");
        G.query_modifiers = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress("eglQueryDmaBufModifiersEXT");
    }

    out_texture(&G.out[0]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    G.on = 1;
    if (G.debug) {
        GpuFormat f[64];
        int nf = gpu_formats(f, 64);
        for (int i = 0; i < nf; i++) dbg("formato dmabuf %08x mod %llx", f[i].fourcc, (unsigned long long)f[i].modifier);
    }
    fprintf(stderr, "gpu: composición por GPU (%s, EGL %d.%d, dmabuf %s, render %s, pantalla %s)\n", G.renderer, maj, min,
            G.create_image ? (G.query_modifiers ? "con modificadores" : "sí") : "no",
            G.render_node[0] ? G.render_node : "el de la tarjeta", k->driver[0] ? k->driver : drv);
    return 0;
fail:
    gpu_shutdown();
    return -1;
}

void gpu_shutdown(void) {
    if (G.dpy != EGL_NO_DISPLAY && G.dpy) {
        for (int i = OUT_MAX - 1; i >= 0; i--) out_teardown(&G.out[i]);
        eglMakeCurrent(G.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (G.ctx) eglDestroyContext(G.dpy, G.ctx);
        eglTerminate(G.dpy);
    }
    if (G.gbm) gbm_device_destroy(G.gbm);
    if (G.k) { G.k->on_flip = NULL; G.k->custom = 0; }
    int debug = G.debug;
    memset(&G, 0, sizeof G);
    G.debug = debug;
}
/* ---- formatos y dispositivo (linux-dmabuf) ---- */

static const uint32_t known[] = { DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888, DRM_FORMAT_ABGR8888, DRM_FORMAT_XBGR8888 };

int gpu_formats(GpuFormat *out, int max) {
    if (!G.on || !G.create_image) return 0;
    int n = 0;
    for (size_t f = 0; f < sizeof known / sizeof *known; f++) {
        EGLuint64KHR mods[64];
        EGLBoolean ext[64];
        EGLint nm = 0;
        if (G.query_modifiers) G.query_modifiers(G.dpy, (EGLint)known[f], 64, mods, ext, &nm);
        for (int i = 0; i < nm && n < max; i++) out[n++] = (GpuFormat){ known[f], mods[i] };
        if (n < max) out[n++] = (GpuFormat){ known[f], DRM_FORMAT_MOD_INVALID };   /* implícito */
    }
    return n;
}

/* El render node para el feedback de linux-dmabuf: el que usa EGL; si EGL no lo dice, el de la
 * misma placa que la tarjeta de la salida; y si esa placa no tiene (pantalla sin GPU, como vc4 en
 * la Pi 5), el primer render node que haya, que en esas placas es la GPU (v3d, panfrost…). */
int gpu_device(dev_t *dev) {
    if (!G.k) return -1;
    struct stat st;
    if (G.render_node[0] && !stat(G.render_node, &st) && S_ISCHR(st.st_mode)) { *dev = st.st_rdev; return 0; }
    if (fstat(G.k->fd, &st)) return -1;
    char card[PATH_MAX], want[PATH_MAX];
    snprintf(card, sizeof card, "/sys/dev/char/%u:%u/device", major(st.st_rdev), minor(st.st_rdev));
    if (!realpath(card, want)) { *dev = st.st_rdev; return 0; }
    DIR *d = opendir("/dev/dri");
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (strncmp(e->d_name, "renderD", 7)) continue;
        char path[PATH_MAX], sys[PATH_MAX], real[PATH_MAX];
        struct stat rs;
        snprintf(path, sizeof path, "/dev/dri/%s", e->d_name);
        if (stat(path, &rs)) continue;
        snprintf(sys, sizeof sys, "/sys/dev/char/%u:%u/device", major(rs.st_rdev), minor(rs.st_rdev));
        if (realpath(sys, real) && !strcmp(real, want)) { *dev = rs.st_rdev; closedir(d); return 0; }
    }
    /* ninguno es de la misma placa: pantalla y GPU separadas */
    int found = 0;
    dev_t other = 0;
    if (d) rewinddir(d);
    while (d && (e = readdir(d))) {
        if (strncmp(e->d_name, "renderD", 7)) continue;
        char path[PATH_MAX];
        struct stat rs;
        snprintf(path, sizeof path, "/dev/dri/%s", e->d_name);
        if (stat(path, &rs) || !S_ISCHR(rs.st_mode)) continue;
        if (!found || rs.st_rdev < other) other = rs.st_rdev;       /* el de número más bajo */
        found = 1;
    }
    if (d) closedir(d);
    *dev = found ? other : st.st_rdev;
    return 0;
}

/* ---- texturas ---- */

static void tex_params(GLenum target) {
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GpuTex *gpu_tex_pixels(GpuTex *t, const uint32_t *px, int w, int h, int opaque) {
    if (!G.on || !px || w <= 0 || h <= 0) return t;
    if (t && t->external) { gpu_tex_free(t); t = NULL; }
    int fresh = !t;
    if (!t) {
        t = calloc(1, sizeof *t);
        if (!t) return NULL;
        glGenTextures(1, &t->id);
        glBindTexture(GL_TEXTURE_2D, t->id);
        tex_params(GL_TEXTURE_2D);
    } else glBindTexture(GL_TEXTURE_2D, t->id);
    GLenum fmt = G.bgra ? GL_BGRA_EXT : GL_RGBA;
    if (fresh || t->w != w || t->h != h) glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, px);
    else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, px);
    t->w = w;
    t->h = h;
    t->opaque = opaque;
    t->swap = !G.bgra;
    return t;
}

static EGLImageKHR import_image(const GpuDmabuf *d, int with_mod);

GpuTex *gpu_tex_dmabuf(const GpuDmabuf *d) {
    if (!G.on || !G.create_image || !G.have_external || d->nplanes < 1 || d->nplanes > 4) return NULL;
    EGLImageKHR img = import_image(d, 1);
    /* lineal sin modificador explícito: hay drivers que solo aceptan el implícito */
    if (img == EGL_NO_IMAGE_KHR && d->modifier == DRM_FORMAT_MOD_LINEAR) img = import_image(d, 0);
    if (img == EGL_NO_IMAGE_KHR) {
        dbg("no se pudo importar un dmabuf %dx%d fourcc %08x mod %llx (EGL 0x%x)", d->width, d->height, d->fourcc,
            (unsigned long long)d->modifier, eglGetError());
        return NULL;
    }
    GpuTex *t = calloc(1, sizeof *t);
    if (!t) { G.destroy_image(G.dpy, img); return NULL; }
    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, t->id);
    tex_params(GL_TEXTURE_EXTERNAL_OES);
    G.image_target(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)img);
    t->image = img;
    t->external = 1;
    t->w = d->width;
    t->h = d->height;
    t->opaque = d->fourcc == DRM_FORMAT_XRGB8888 || d->fourcc == DRM_FORMAT_XBGR8888;
    return t;
}

static EGLImageKHR import_image(const GpuDmabuf *d, int with_mod) {
    static const EGLint fd_k[4] = { EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE3_FD_EXT };
    static const EGLint off_k[4] = { EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT };
    static const EGLint pitch_k[4] = { EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT };
    static const EGLint lo_k[4] = { EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT };
    static const EGLint hi_k[4] = { EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT };
    EGLint attr[64];
    int a = 0;
    attr[a++] = EGL_WIDTH; attr[a++] = d->width;
    attr[a++] = EGL_HEIGHT; attr[a++] = d->height;
    attr[a++] = EGL_LINUX_DRM_FOURCC_EXT; attr[a++] = (EGLint)d->fourcc;
    for (int i = 0; i < d->nplanes; i++) {
        attr[a++] = fd_k[i]; attr[a++] = d->fd[i];
        attr[a++] = off_k[i]; attr[a++] = (EGLint)d->offset[i];
        attr[a++] = pitch_k[i]; attr[a++] = (EGLint)d->stride[i];
        if (with_mod && d->modifier != DRM_FORMAT_MOD_INVALID && G.query_modifiers) {
            attr[a++] = lo_k[i]; attr[a++] = (EGLint)(d->modifier & 0xffffffff);
            attr[a++] = hi_k[i]; attr[a++] = (EGLint)(d->modifier >> 32);
        }
    }
    attr[a++] = EGL_IMAGE_PRESERVED_KHR; attr[a++] = EGL_TRUE;
    attr[a] = EGL_NONE;
    return G.create_image(G.dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attr);
}

void gpu_tex_free(GpuTex *t) {
    if (!t) return;
    if (G.on) {
        glDeleteTextures(1, &t->id);
        if (t->image && G.destroy_image) G.destroy_image(G.dpy, t->image);
    }
    free(t);
}

/* ---- dibujo ---- */

void gpu_draw(GpuTex *t, float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh, Rect clip, float op) {
    if (!G.on || !G.cur || !t || dw <= 0 || dh <= 0 || op <= 0.004f) return;
    const int W = G.cur->W, H = G.cur->H;           /* the display being drawn */
    if (clip.x0 < 0) clip.x0 = 0;
    if (clip.y0 < 0) clip.y0 = 0;
    if (clip.x1 > W) clip.x1 = W;
    if (clip.y1 > H) clip.y1 = H;
    if (clip.x1 <= clip.x0 || clip.y1 <= clip.y0) return;
    int p = t->external ? 1 : 0;
    glUseProgram(G.prog[p]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(t->external ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D, t->id);
    glUniform1i(G.u_tex[p], 0);
    glUniform1f(G.u_op[p], op);
    glUniform1f(G.u_opaque[p], t->opaque ? 1.0f : 0.0f);
    glUniform1f(G.u_swap[p], t->swap ? 1.0f : 0.0f);
    glEnable(GL_SCISSOR_TEST);
    glScissor(clip.x0, H - clip.y1, clip.x1 - clip.x0, clip.y1 - clip.y0);
    float x0 = dx / W * 2 - 1, x1 = (dx + dw) / W * 2 - 1;
    float y0 = 1 - dy / H * 2, y1 = 1 - (dy + dh) / H * 2;
    float u0 = sx / t->w, u1 = (sx + sw) / t->w, v0 = sy / t->h, v1 = (sy + sh) / t->h;
    const GLfloat v[] = { x0, y0, u0, v0,  x1, y0, u1, v0,  x0, y1, u0, v1,  x1, y1, u1, v1 };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, v);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, v + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_SCISSOR_TEST);
}

int gpu_frame(int out, const Canvas *scene, Rect dmg, void (*draw_layer)(int layer)) {
    if (!G.on || out < 0 || out >= OUT_MAX || !G.out[out].used) return -1;
    GOut *o = &G.out[out];
    if (!eglMakeCurrent(G.dpy, o->surf, o->surf, G.ctx)) { fprintf(stderr, "gpu: eglMakeCurrent failed\n"); return -1; }
    G.cur = o;
    glViewport(0, 0, o->W, o->H);
    /* what changed in the UI */
    if (dmg.x1 > dmg.x0 && dmg.y1 > dmg.y0) {
        glBindTexture(GL_TEXTURE_2D, o->canvas.id);
        GLenum fmt = G.bgra ? GL_BGRA_EXT : GL_RGBA;
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, scene->stride);
        glTexSubImage2D(GL_TEXTURE_2D, 0, dmg.x0, dmg.y0, dmg.x1 - dmg.x0, dmg.y1 - dmg.y0, fmt, GL_UNSIGNED_BYTE,
                        scene->px + (size_t)dmg.y0 * scene->stride + dmg.x0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
    }
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    Rect all = { 0, 0, o->W, o->H };
    if (draw_layer) draw_layer(0);
    gpu_draw(&o->canvas, 0, 0, (float)o->W, (float)o->H, 0, 0, (float)o->W, (float)o->H, all, 1);
    if (draw_layer) draw_layer(1);
    if (!eglSwapBuffers(G.dpy, o->surf)) { fprintf(stderr, "gpu: eglSwapBuffers failed\n"); return -1; }
    struct gbm_bo *bo = gbm_surface_lock_front_buffer(o->gs);
    if (!bo) { fprintf(stderr, "gpu: no GBM buffer\n"); return -1; }
    uint32_t fb = fb_for_bo(bo);
    if (!fb) { gbm_surface_release_buffer(o->gs, bo); return -1; }
    G.frames++;
    return present(o, (Shown){ .kind = 1, .bo = bo, .fb = fb });
}
/* ---- scanout directo ---- */

int gpu_can_scanout(const GpuDmabuf *d, int w, int h) {
    /* y que el plano primario acepte el modificador (vc4: solo lineal; UIF de v3d no) */
    return G.on && d->width == w && d->height == h && d->nplanes == 1 &&
           (d->fourcc == DRM_FORMAT_XRGB8888 || d->fourcc == DRM_FORMAT_ARGB8888) &&
           kms_scanout_ok(G.k, DRM_FORMAT_XRGB8888, d->modifier);
}

int gpu_scanout(const GpuDmabuf *d, void (*done)(void *), void *data) {
    GOut *o = &G.out[0];                /* one display only (wl_try_scanout) */
    if (!G.on || !o->used || o->first) return -1;
    Shown s = { .kind = 2, .done = done, .data = data };
    for (int i = 0; i < d->nplanes; i++)
        if (kms_prime_import(G.k, d->fd[i], &s.handles[i])) {
            for (int j = 0; j < i; j++) kms_close_handle(G.k, s.handles[j]);
            dbg("scanout: PRIME import falló (%s)", strerror(errno));
            return -1;
        }
    /* ARGB en el plano primario se muestra como XRGB (la salida no tiene nada debajo) */
    uint32_t fourcc = d->fourcc == DRM_FORMAT_ARGB8888 ? DRM_FORMAT_XRGB8888 : d->fourcc;
    if (kms_add_fb2(G.k, (uint32_t)d->width, (uint32_t)d->height, fourcc, s.handles, d->stride, d->offset, d->modifier, &s.fb)) {
        dbg("scanout: ADDFB2 falló (%s)", strerror(errno));
        s.done = NULL;
        s.kind = 2;
        release(o, &s);
        return -1;
    }
    G.scanouts++;
    if (G.debug && (G.scanouts == 1 || G.scanouts % 300 == 0)) dbg("scanout directo: %ld cuadros (compuestos %ld)", G.scanouts, G.frames);
    return present(o, s);
}

#endif

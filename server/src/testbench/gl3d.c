/* gl3d.c — lordos-testbench's 3D view: a rotating cube drawn with EGL and OpenGL ES 2 on a subsurface,
 * through Mesa's Wayland platform, the way a game or a GPU app draws. It shows which renderer did the
 * work (a GPU's driver, or a software one such as llvmpipe), how long the GPU takes per frame, and it
 * sends its buffers through the compositor's dmabuf path (Mesa uses linux-dmabuf with a GPU, wl_shm with
 * a software renderer). Built only when EGL, GLESv2 and wayland-egl are there (TB_GL). */
#include "gl3d.h"

#ifdef TB_GL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <wayland-egl.h>

static EGLDisplay edpy = EGL_NO_DISPLAY;
static EGLContext ectx = EGL_NO_CONTEXT;
static EGLSurface esurf = EGL_NO_SURFACE;
static struct wl_egl_window *ewin;
static struct wl_surface *gsurf;
static struct wl_subsurface *gsub;
static GLuint prog;
static GLint u_mvp, a_pos, a_col;
static int gw, gh, on;
Gl3dInfo gl3d;

static double ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    return ok ? s : 0;
}

static void classify(void) {
    const char *r = gl3d.renderer;
    if (strstr(r, "llvmpipe") || strstr(r, "softpipe") || strstr(r, "swrast") || strstr(r, "SwiftShader"))
        snprintf(gl3d.kind, sizeof gl3d.kind, "SOFTWARE (CPU)");
    else if (strstr(r, "virgl") || strstr(r, "VirGL") || strstr(r, "SVGA") || strstr(r, "virtio"))
        snprintf(gl3d.kind, sizeof gl3d.kind, "VIRTUAL GPU (HOST)");
    else snprintf(gl3d.kind, sizeof gl3d.kind, "HARDWARE GPU");
}

int gl3d_start(struct wl_display *dpy, struct wl_compositor *comp, struct wl_subcompositor *sc, struct wl_surface *parent, int x, int y, int w, int h) {
    if (on) { gl3d_place(x, y, w, h); return 0; }
    memset(&gl3d, 0, sizeof gl3d);
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    edpy = gpd ? gpd(EGL_PLATFORM_WAYLAND_KHR, dpy, NULL) : eglGetDisplay((EGLNativeDisplayType)dpy);
    EGLint maj, min;
    if (edpy == EGL_NO_DISPLAY || !eglInitialize(edpy, &maj, &min)) { snprintf(gl3d.error, sizeof gl3d.error, "EGL DID NOT START ON WAYLAND"); return -1; }
    snprintf(gl3d.egl, sizeof gl3d.egl, "EGL %d.%d %.40s", maj, min, eglQueryString(edpy, EGL_VENDOR));
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint ca[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 16,
                                 EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(edpy, ca, &cfg, 1, &n) || n < 1) { snprintf(gl3d.error, sizeof gl3d.error, "NO GLES2 CONFIG"); return -1; }
    static const EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    ectx = eglCreateContext(edpy, cfg, EGL_NO_CONTEXT, xa);
    if (ectx == EGL_NO_CONTEXT) { snprintf(gl3d.error, sizeof gl3d.error, "NO GLES2 CONTEXT"); return -1; }
    gsurf = wl_compositor_create_surface(comp);
    gsub = wl_subcompositor_get_subsurface(sc, gsurf, parent);
    wl_subsurface_set_desync(gsub);
    struct wl_region *empty = wl_compositor_create_region(comp);     /* input goes to the bench below */
    wl_surface_set_input_region(gsurf, empty);
    wl_region_destroy(empty);
    gw = w; gh = h;
    wl_subsurface_set_position(gsub, x, y);
    ewin = wl_egl_window_create(gsurf, w, h);
    esurf = eglCreateWindowSurface(edpy, cfg, (EGLNativeWindowType)ewin, NULL);
    if (esurf == EGL_NO_SURFACE || !eglMakeCurrent(edpy, esurf, esurf, ectx)) { snprintf(gl3d.error, sizeof gl3d.error, "NO EGL WINDOW SURFACE"); return -1; }
    eglSwapInterval(edpy, 0);           /* the bench's own frame callback paces the frames */
    snprintf(gl3d.renderer, sizeof gl3d.renderer, "%.90s", (const char *)glGetString(GL_RENDERER));
    snprintf(gl3d.vendor, sizeof gl3d.vendor, "%.60s", (const char *)glGetString(GL_VENDOR));
    snprintf(gl3d.version, sizeof gl3d.version, "%.60s", (const char *)glGetString(GL_VERSION));
    classify();
    static const char *vs = "uniform mat4 mvp; attribute vec3 pos; attribute vec3 col; varying vec3 c;"
                            "void main(){ c = col; gl_Position = mvp * vec4(pos, 1.0); }";
    static const char *fs = "precision mediump float; varying vec3 c; void main(){ gl_FragColor = vec4(c, 1.0); }";
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) { snprintf(gl3d.error, sizeof gl3d.error, "SHADERS DID NOT COMPILE"); return -1; }
    prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    u_mvp = glGetUniformLocation(prog, "mvp");
    a_pos = glGetAttribLocation(prog, "pos");
    a_col = glGetAttribLocation(prog, "col");
    glEnable(GL_DEPTH_TEST);
    on = 1;
    return 0;
}

void gl3d_place(int x, int y, int w, int h) {
    if (!on) return;
    wl_subsurface_set_position(gsub, x, y);
    if (w != gw || h != gh) { wl_egl_window_resize(ewin, w, h, 0, 0); gw = w; gh = h; }
}

static void mul(float *o, const float *a, const float *b) {
    float r[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            r[j * 4 + i] = 0;
            for (int k = 0; k < 4; k++) r[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
        }
    memcpy(o, r, sizeof r);
}

struct wl_surface *gl3d_surface(void) { return on ? gsurf : NULL; }

void gl3d_frame(double t_ms) {
    if (!on) return;
    /* a cube, two triangles per face, a colour per face */
    static const float P[8][3] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 }, { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };
    static const int F[6][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 1, 2, 6, 5 }, { 0, 3, 7, 4 } };
    static const float C[6][3] = { { .95f, .25f, .25f }, { .25f, .85f, .35f }, { .25f, .45f, .95f }, { .95f, .8f, .2f }, { .75f, .35f, .95f }, { .2f, .85f, .9f } };
    float v[36 * 3], c[36 * 3];
    int k = 0;
    for (int f = 0; f < 6; f++) {
        static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; i++, k++) {
            memcpy(v + k * 3, P[F[f][tri[i]]], 12);
            memcpy(c + k * 3, C[f], 12);
        }
    }
    double a = t_ms / 1000.0;
    float ca = (float)cos(a), sa = (float)sin(a), cb = (float)cos(a * 0.7), sb = (float)sin(a * 0.7);
    float ry[16] = { ca, 0, -sa, 0, 0, 1, 0, 0, sa, 0, ca, 0, 0, 0, 0, 1 };
    float rx[16] = { 1, 0, 0, 0, 0, cb, sb, 0, 0, -sb, cb, 0, 0, 0, 0, 1 };
    float asp = gh ? (float)gw / gh : 1, fn = 1.0f / (float)tan(0.5), zn = 1, zf = 20;
    float pr[16] = { fn / asp, 0, 0, 0, 0, fn, 0, 0, 0, 0, (zf + zn) / (zn - zf), -1, 0, 0, 2 * zf * zn / (zn - zf), 0 };
    float tr[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -5, 1 };
    float m[16], mvp[16];
    mul(m, rx, ry);
    mul(m, tr, m);
    mul(mvp, pr, m);
    double t0 = ms();
    glViewport(0, 0, gw, gh);
    glClearColor(0.07f, 0.07f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(prog);
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
    glVertexAttribPointer((GLuint)a_pos, 3, GL_FLOAT, GL_FALSE, 0, v);
    glVertexAttribPointer((GLuint)a_col, 3, GL_FLOAT, GL_FALSE, 0, c);
    glEnableVertexAttribArray((GLuint)a_pos);
    glEnableVertexAttribArray((GLuint)a_col);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glFinish();                         /* the GPU's time for this frame */
    double dt = ms() - t0;
    gl3d.gpu_ms = gl3d.frames ? gl3d.gpu_ms * 0.9 + dt * 0.1 : dt;
    gl3d.frames++;
    eglSwapBuffers(edpy, esurf);
}

void gl3d_stop(void) {
    if (!on) return;
    eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(edpy, esurf);
    wl_egl_window_destroy(ewin);
    wl_subsurface_destroy(gsub);
    wl_surface_destroy(gsurf);
    eglDestroyContext(edpy, ectx);
    eglTerminate(edpy);
    on = 0;
}

int gl3d_on(void) { return on; }

#else   /* built without EGL: the view says so */
Gl3dInfo gl3d;
int gl3d_start(struct wl_display *d, struct wl_compositor *c, struct wl_subcompositor *s, struct wl_surface *p, int x, int y, int w, int h) {
    (void)d; (void)c; (void)s; (void)p; (void)x; (void)y; (void)w; (void)h;
    snprintf(gl3d.error, sizeof gl3d.error, "BUILT WITHOUT EGL");
    return -1;
}
void gl3d_place(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void gl3d_frame(double t) { (void)t; }
void gl3d_stop(void) {}
int gl3d_on(void) { return 0; }
struct wl_surface *gl3d_surface(void) { return NULL; }
#endif

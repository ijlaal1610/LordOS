/* wl.c — LordOS's own Wayland compositor: Linux apps (GTK, Qt, foot…) open as LordOS
 * windows. It only uses libwayland-server (it serializes the protocol; the dependency policy
 * allows it since M1) and the code wayland-scanner generates for xdg-shell and xdg-decoration.
 *
 * Globals: wl_compositor 6, wl_subcompositor, wl_shm (from libwayland), wl_output 4, wl_seat 7
 * (pointer and keyboard with the XKB keymap from keyboard.c), wl_data_device_manager 3 (clipboard
 * shared with LordOS's), xdg_wm_base 6 and zxdg_decoration_manager_v1.
 *
 * Each surface copies its shared-memory buffer on commit (and releases it right away):
 * the UI draws it as one more node (NODE_SURFACE) inside the theme's window frame. Windows
 * with server-side decoration (Qt, foot) use the theme's frame; those that draw their
 * own (GTK) go without a frame and the shadow is clipped with their window geometry. Popup
 * menus (xdg_popup) are drawn on top of everything.
 *
 * Not there yet: linux-dmabuf (GL apps use shared memory), drag and drop,
 * app cursors, text-input (LordOS input methods inside Wayland apps),
 * Xwayland. */
#include "wl.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "xdg-shell-protocol.h"
#include "xdg-decoration-unstable-v1-protocol.h"
#include "linux-dmabuf-v1-protocol.h"
#include "presentation-time-protocol.h"
#include "viewporter-protocol.h"
#include "relative-pointer-unstable-v1-protocol.h"
#include "pointer-constraints-unstable-v1-protocol.h"
#include "cursor-shape-v1-protocol.h"
#include "fractional-scale-v1-protocol.h"
#include "xdg-output-unstable-v1-protocol.h"
#include "../outputs.h"
#include "../cursor.h"
#include "wl_input.h"
#include "../gpu/gpu.h"
#include <drm/drm_fourcc.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include "../term/term.h"
#include "../ui/ui.h"

enum { R_NONE, R_TOPLEVEL, R_POPUP, R_SUB, R_CURSOR };

/* An app's dmabuf buffer (zwp_linux_dmabuf_v1). Refcounted: 1 for the wl_buffer, 1 for the
 * surface showing it and 1 while it is on screen via direct scanout. */
typedef struct Dmabuf {
    GpuDmabuf d;
    struct wl_resource *res;            /* NULL if the app already destroyed it */
    GpuTex *tex;                        /* EGLImage (GPU path) */
    int refs, scanned;
    int cpu;                            /* EGL does not import it: it is mapped and copied (linear) */
} Dmabuf;

typedef struct Surf Surf;
struct Surf {
    struct wl_resource *res;
    struct wl_client *client;
    int id;
    /* pending until commit */
    struct wl_resource *p_buf;
    struct wl_listener p_buf_destroy;
    int p_attached, p_gx, p_gy, p_gw, p_gh, p_geom;
    struct wl_list p_frames, frames;
    /* current */
    uint32_t *px;
    int w, h, opaque;
    int role;
    struct wl_resource *xdg, *top, *popup, *deco, *sub;
    int sent_configure, mapped, conf_w, conf_h;
    int gx, gy, gw, gh, has_geom;
    char title[256], app_id[128];
    int maximized, fullscreen, activated, csd;
    int kde_ssd;                    /* asked for a server frame via org_kde_kwin_server_decoration (GTK 3) */
    struct wl_resource *kde;
    /* popup menu */
    Surf *parent;
    int pop_x, pop_y, grab;
    /* subsurface */
    int sx, sy, p_sx, p_sy;
    Surf *kids[16];
    int nkids;
    int node;                   /* UI node that shows it (windows) */
    /* content: shm buffer copied into px, or dmabuf in use (GPU path) */
    int content;
    Dmabuf *dmabuf;
    GpuTex *tex;                /* uploaded px (GPU path) */
    int tex_dirty;
    int lw, lh;                 /* logical size (viewporter; otherwise the buffer's) */
    /* wp_viewporter: pending and current (vsw < 0 = no crop, vdw <= 0 = no scaling) */
    struct wl_resource *viewport;
    float p_vsx, p_vsy, p_vsw, p_vsh, vsx, vsy, vsw, vsh;
    int p_vdw, p_vdh, vdw, vdh, p_vp;
    /* wp_presentation */
    struct wl_list p_feedback, feedback;
    int cursor_hx, cursor_hy, p_hx, p_hy;   /* cursor role: hotspot */
    unsigned hole_sig;          /* GPU path: shape of the hole the UI left last time */
    struct wl_list sent_feedback;   /* presentation: already in a frame, flip pending */
    int fs_saved_w, fs_saved_h;     /* size before fullscreen */
    /* HiDPI (docs/DISPLAYS.md): wl_surface.set_buffer_scale, the displays it is on (wl_surface
     * enter/leave, bit = output slot) and the scale it should render at (fractional-scale-v1,
     * preferred_buffer_scale) */
    int bscale, p_bscale;
    struct wl_resource *frac;
    unsigned outmask;
    int pref_scale;
    int logged_ratio;               /* buffer px per logical px ×100, last logged (docs/DISPLAYS.md) */
};

/* Shape of a surface's hole in the UI: if it changes, the UI must be repainted. */
static unsigned hole_sig(const Surf *s) {
    return (unsigned)s->lw * 2654435761u ^ (unsigned)s->lh * 40503u ^ (unsigned)(s->gx + 7 * s->gy) * 97u ^
           (unsigned)(s->gw * 13 + s->gh) * 31u ^ (unsigned)(s->sx * 5 + s->sy) ^ (unsigned)s->content << 30;
}

#define MAX_SURF 256
static Surf *surfs[MAX_SURF];
static int next_id = 1;

static struct wl_display *dpy;
static struct wl_event_loop *loop;
static char socket_name[64];

/* per-client resources */
static struct wl_list pointers, keyboards, data_devices, outputs;

static Surf *pointer_focus, *key_focus;
static int buttons_held;
static float last_x, last_y, press_x, press_y;     /* where it was pressed: base for move/resize */
static uint32_t mods_sent[4];

/* visible popup menus, bottom to top */
static Surf *popups[32];
static int npopups;

/* GPU path: what is drawn below (layer 0) and above (layer 1) the UI, in order */
typedef struct { int sid; float x, y; Rect clip; float op; } Draw;
static Draw draws[2][128];
static int ndraws[2];
static int need_frame;              /* an app changed its content: must present */
static Dmabuf *to_release[64];      /* replaced: released after the next frame */
static int nrelease;

/* fullscreen (presented by the compositor, on top of everything) */
static int oslots_used(int slot);
static void oslot_rect(int slot, float *x, float *y, float *w, float *h);
static Surf *fs_surf;
static int fs_slot = -1;                /* the display it covers (output slot), -1 = the whole space */

/* Logical rect of the display the fullscreen window covers. */
static void fs_rect(float *x, float *y, float *w, float *h) {
    if (fs_slot >= 0 && fs_slot < OUT_MAX && oslots_used(fs_slot)) { oslot_rect(fs_slot, x, y, w, h); return; }
    *x = 0; *y = 0; *w = ui.width; *h = ui.height;
}

/* cursor of the app with the pointer: 0 LordOS arrow, 1 app image, 2 hidden */
static Surf *cursor_surf;
static int cursor_mode;
static unsigned cursor_gen;

/* zwp_pointer_constraints / zwp_relative_pointer */
static struct wl_list rel_pointers;
typedef struct { struct wl_resource *res; Surf *surf; int lock, active, oneshot; } Constraint;
static Constraint *constraint;      /* the only active (or pending) one */
static dev_t drm_dev;

static unsigned now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* ---- events for JS ---- */

typedef struct { int id; char type[16]; char json[600]; } Ev;
static Ev evq[64];
static int nev;

static void json_str(char *out, size_t size, const char *s) {
    size_t o = 0;
    for (; *s && o + 7 < size; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c < 0x20) o += (size_t)snprintf(out + o, size - o, "\\u%04x", c);
        else out[o++] = (char)c;
    }
    out[o] = 0;
}

static void emit(Surf *s, const char *type, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void emit(Surf *s, const char *type, const char *fmt, ...) {
    if (nev == 64) return;
    Ev *e = &evq[nev++];
    e->id = s->id;
    snprintf(e->type, sizeof e->type, "%s", type);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->json, sizeof e->json, fmt, ap);
    va_end(ap);
}

void wl_events(WlEventFn fn) {
    for (int i = 0; i < nev; i++) fn(evq[i].id, evq[i].type, evq[i].json);
    nev = 0;
}

/* ---- utilities ---- */

static Surf *find(int id) {
    for (int i = 0; i < MAX_SURF; i++) if (surfs[i] && surfs[i]->id == id) return surfs[i];
    return NULL;
}

static void damage_node(Surf *s) {
    Surf *top = s;
    while (top->parent && top->role != R_TOPLEVEL) top = top->parent;
    /* GPU path: if only the content changed, presenting again is enough */
    if (gpu_active() && (top->role == R_POPUP || top == fs_surf || hole_sig(s) == s->hole_sig)) { need_frame = 1; return; }
    Node *n = top->node ? node_get(top->node) : NULL;
    if (n && n->prev_box.x1 > n->prev_box.x0) damage_add(n->prev_box);
    ui.dirty = 1;
}

/* On-screen origin of a surface's buffer (window or menu). 0 if unknown. */
static int origin(Surf *s, float *x, float *y) {
    if (s == fs_surf) {
        float fx, fy, fw, fh;
        fs_rect(&fx, &fy, &fw, &fh);
        *x = fx - (float)s->gx;
        *y = fy - (float)s->gy;
        return 1;
    }
    if (s->role == R_TOPLEVEL) {
        Node *n = s->node ? node_get(s->node) : NULL;
        if (!n || n->prev_box.x1 <= n->prev_box.x0) return 0;
        *x = n->ax - s->gx;
        *y = n->ay - s->gy;
        return 1;
    }
    if (s->role == R_POPUP && s->parent) {
        float px, py;
        if (!origin(s->parent, &px, &py)) return 0;
        *x = px + s->parent->gx + s->pop_x - s->gx;
        *y = py + s->parent->gy + s->pop_y - s->gy;
        return 1;
    }
    if (s->role == R_SUB && s->parent) {
        float px, py;
        if (!origin(s->parent, &px, &py)) return 0;
        *x = px + s->sx;
        *y = py + s->sy;
        return 1;
    }
    return 0;
}

static void popup_box_damage(Surf *s) {
    float x, y;
    if (gpu_active()) { need_frame = 1; return; }          /* menus go above the UI */
    if (s->lw > 0 && origin(s, &x, &y)) damage_add((Box){ x - 2, y - 2, x + s->lw + 2, y + s->lh + 2 });
    ui.dirty = 1;
}

/* resource of the same client in a resource list */
#define FOR_CLIENT(list, client, r) wl_resource_for_each(r, list) if (wl_resource_get_client(r) == (client))

/* ---- configure ---- */

static void send_toplevel_configure(Surf *s) {
    if (!s->top || !s->xdg) return;
    struct wl_array states;
    wl_array_init(&states);
    uint32_t *p;
    if (s->maximized && (p = wl_array_add(&states, sizeof *p))) *p = XDG_TOPLEVEL_STATE_MAXIMIZED;
    if (s->fullscreen && (p = wl_array_add(&states, sizeof *p))) *p = XDG_TOPLEVEL_STATE_FULLSCREEN;
    if (s->activated && (p = wl_array_add(&states, sizeof *p))) *p = XDG_TOPLEVEL_STATE_ACTIVATED;
    if (s->maximized && wl_resource_get_version(s->top) >= 2) {
        static const uint32_t tiled[] = { XDG_TOPLEVEL_STATE_TILED_LEFT, XDG_TOPLEVEL_STATE_TILED_RIGHT,
                                          XDG_TOPLEVEL_STATE_TILED_TOP, XDG_TOPLEVEL_STATE_TILED_BOTTOM };
        for (int i = 0; i < 4; i++) if ((p = wl_array_add(&states, sizeof *p))) *p = tiled[i];
    }
    xdg_toplevel_send_configure(s->top, s->conf_w, s->conf_h, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(s->xdg, wl_display_next_serial(dpy));
    s->sent_configure = 1;
}

/* ---- wl_buffer → own pixels ---- */

static void buffer_destroyed(struct wl_listener *l, void *data) {
    (void)data;
    Surf *s = wl_container_of(l, s, p_buf_destroy);
    s->p_buf = NULL;
    wl_list_remove(&l->link);
    wl_list_init(&l->link);
}

static void copy_buffer(Surf *s, struct wl_resource *buf) {
    struct wl_shm_buffer *b = buf ? wl_shm_buffer_get(buf) : NULL;
    if (!b) {                               /* no buffer (or one that is not shared memory) */
        free(s->px);
        s->px = NULL;
        s->w = s->h = 0;
        s->content = 0;
        if (buf) wl_buffer_send_release(buf);
        return;
    }
    s->content = 1;
    int w = wl_shm_buffer_get_width(b), h = wl_shm_buffer_get_height(b), stride = wl_shm_buffer_get_stride(b);
    uint32_t fmt = wl_shm_buffer_get_format(b);
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) { wl_buffer_send_release(buf); return; }
    if (w != s->w || h != s->h || !s->px) {
        uint32_t *np = realloc(s->px, (size_t)w * (size_t)h * 4);
        if (!np) { wl_buffer_send_release(buf); return; }
        s->px = np;
        s->w = w;
        s->h = h;
    }
    s->opaque = fmt == WL_SHM_FORMAT_XRGB8888;
    wl_shm_buffer_begin_access(b);
    const uint8_t *d = wl_shm_buffer_get_data(b);
    for (int y = 0; y < h; y++) {
        uint32_t *dst = s->px + (size_t)y * w;
        memcpy(dst, d + (size_t)y * stride, (size_t)w * 4);
        if (s->opaque) for (int x = 0; x < w; x++) dst[x] |= 0xff000000u;
    }
    wl_shm_buffer_end_access(b);
    wl_buffer_send_release(buf);
}

/* ---- dmabuf ---- */

static void dm_unref(Dmabuf *d) {
    if (!d || --d->refs > 0) return;
    gpu_tex_free(d->tex);
    for (int i = 0; i < d->d.nplanes; i++) if (d->d.fd[i] >= 0) close(d->d.fd[i]);
    free(d);
}

static const struct wl_buffer_interface dmabuf_buffer_impl;
static Dmabuf *dmabuf_of(struct wl_resource *buf) {
    return buf && wl_resource_instance_of(buf, &wl_buffer_interface, &dmabuf_buffer_impl) ? wl_resource_get_user_data(buf) : NULL;
}

/* CPU path: a linear dmabuf (ARGB/XRGB8888, one plane) is mapped and copied to px. */
static int map_linear(Surf *s, Dmabuf *d) {
    const GpuDmabuf *g = &d->d;
    if (g->nplanes != 1 || (g->modifier != DRM_FORMAT_MOD_LINEAR && g->modifier != DRM_FORMAT_MOD_INVALID) ||
        (g->fourcc != DRM_FORMAT_ARGB8888 && g->fourcc != DRM_FORMAT_XRGB8888) || g->width > 8192 || g->height > 8192 ||
        g->stride[0] < (uint32_t)g->width * 4)
        return -1;
    size_t size = (size_t)g->offset[0] + (size_t)g->stride[0] * (size_t)g->height;
    uint8_t *m = mmap(NULL, size, PROT_READ, MAP_SHARED, g->fd[0], 0);
    if (m == MAP_FAILED) return -1;
    struct dma_buf_sync sync = { .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
    ioctl(g->fd[0], DMA_BUF_IOCTL_SYNC, &sync);
    if (g->width != s->w || g->height != s->h || !s->px) {
        uint32_t *np = realloc(s->px, (size_t)g->width * (size_t)g->height * 4);
        if (!np) { munmap(m, size); return -1; }
        s->px = np;
        s->w = g->width;
        s->h = g->height;
    }
    s->opaque = g->fourcc == DRM_FORMAT_XRGB8888;
    for (int y = 0; y < g->height; y++) {
        uint32_t *dst = s->px + (size_t)y * s->w;
        memcpy(dst, m + g->offset[0] + (size_t)y * g->stride[0], (size_t)s->w * 4);
        if (s->opaque) for (int x = 0; x < s->w; x++) dst[x] |= 0xff000000u;
    }
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
    ioctl(g->fd[0], DMA_BUF_IOCTL_SYNC, &sync);
    munmap(m, size);
    return 0;
}

static void queue_release(Dmabuf *d) {
    if (nrelease < 64) { to_release[nrelease++] = d; return; }
    if (d->res && !d->scanned) wl_buffer_send_release(d->res);
    dm_unref(d);
}

/* The buffer the app just sent becomes the surface's content. */
static void take_buffer(Surf *s, struct wl_resource *buf) {
    Dmabuf *dm = dmabuf_of(buf), *old = s->dmabuf;
    s->dmabuf = NULL;
    if (dm && gpu_active() && !dm->cpu && s->role != R_CURSOR) {
        dm->refs++;
        s->dmabuf = dm;
        s->w = dm->d.width;
        s->h = dm->d.height;
        s->opaque = dm->d.fourcc == DRM_FORMAT_XRGB8888 || dm->d.fourcc == DRM_FORMAT_XBGR8888;
        s->content = 1;
        free(s->px);
        s->px = NULL;
    } else if (dm) {
        s->content = map_linear(s, dm) == 0;
        if (!s->content) { free(s->px); s->px = NULL; s->w = s->h = 0; }
        s->tex_dirty = 1;
        wl_buffer_send_release(buf);
    } else {
        copy_buffer(s, buf);
        s->tex_dirty = 1;
    }
    if (old) queue_release(old);
}

/* Logical size: the viewport destination, else the crop, else the buffer. */
static void apply_viewport(Surf *s) {
    if (s->p_vp) {
        s->vsx = s->p_vsx; s->vsy = s->p_vsy; s->vsw = s->p_vsw; s->vsh = s->p_vsh;
        s->vdw = s->p_vdw; s->vdh = s->p_vdh;
        s->p_vp = 0;
    }
    if (s->viewport && s->vdw > 0 && s->vdh > 0) { s->lw = s->vdw; s->lh = s->vdh; }
    else if (s->viewport && s->vsw > 0 && s->vsh > 0) { s->lw = (int)s->vsw; s->lh = (int)s->vsh; }
    else { int b = s->bscale > 0 ? s->bscale : 1; s->lw = s->w / b; s->lh = s->h / b; }
}

/* ---- wl_surface ---- */

static void surface_commit_role(Surf *s);

static void srf_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }

static void srf_attach(struct wl_client *c, struct wl_resource *r, struct wl_resource *buf, int32_t x, int32_t y) {
    (void)c; (void)x; (void)y;
    Surf *s = wl_resource_get_user_data(r);
    if (s->p_buf) { wl_list_remove(&s->p_buf_destroy.link); wl_list_init(&s->p_buf_destroy.link); }
    s->p_buf = buf;
    s->p_attached = 1;
    if (buf) {
        s->p_buf_destroy.notify = buffer_destroyed;
        wl_resource_add_destroy_listener(buf, &s->p_buf_destroy);
    }
}

static void srf_damage(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}

static void callback_gone(struct wl_resource *r) { wl_list_remove(wl_resource_get_link(r)); }

static void srf_frame(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    Surf *s = wl_resource_get_user_data(r);
    struct wl_resource *cb = wl_resource_create(c, &wl_callback_interface, 1, id);
    if (!cb) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(cb, NULL, NULL, callback_gone);
    wl_list_insert(s->p_frames.prev, wl_resource_get_link(cb));
}

static void srf_region(struct wl_client *c, struct wl_resource *r, struct wl_resource *region) {
    (void)c; (void)r; (void)region;
}

static void srf_commit(struct wl_client *c, struct wl_resource *r) {
    (void)c;
    Surf *s = wl_resource_get_user_data(r);
    if (s->p_attached) {
        take_buffer(s, s->p_buf);
        if (s->p_buf) { wl_list_remove(&s->p_buf_destroy.link); wl_list_init(&s->p_buf_destroy.link); }
        s->p_buf = NULL;
        s->p_attached = 0;
    }
    s->bscale = s->p_bscale;
    apply_viewport(s);
    wl_list_insert_list(s->frames.prev, &s->p_frames);
    wl_list_init(&s->p_frames);
    wl_list_insert_list(s->feedback.prev, &s->p_feedback);
    wl_list_init(&s->p_feedback);
    if (s->role == R_CURSOR) {
        s->cursor_hx = s->p_hx;
        s->cursor_hy = s->p_hy;
        if (s == cursor_surf) cursor_gen++;
        return;
    }
    if (s->p_geom) {
        s->gx = s->p_gx; s->gy = s->p_gy; s->gw = s->p_gw; s->gh = s->p_gh;
        s->has_geom = 1;
        s->p_geom = 0;
    }
    if (s->role == R_SUB) { s->sx = s->p_sx; s->sy = s->p_sy; }
    surface_commit_role(s);
}

static void srf_transform(struct wl_client *c, struct wl_resource *r, int32_t t) { (void)c; (void)r; (void)t; }
static void srf_scale(struct wl_client *c, struct wl_resource *r, int32_t sc) {
    (void)c;
    Surf *s = wl_resource_get_user_data(r);
    if (sc < 1) { wl_resource_post_error(r, WL_SURFACE_ERROR_INVALID_SCALE, "buffer scale must be positive"); return; }
    if (s) s->p_bscale = sc > 8 ? 8 : sc;
}
static void srf_offset(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y) { (void)c; (void)r; (void)x; (void)y; }

static const struct wl_surface_interface surface_impl = {
    .destroy = srf_destroy, .attach = srf_attach, .damage = srf_damage, .frame = srf_frame,
    .set_opaque_region = srf_region, .set_input_region = srf_region, .commit = srf_commit,
    .set_buffer_transform = srf_transform, .set_buffer_scale = srf_scale, .damage_buffer = srf_damage,
    .offset = srf_offset,
};

static void unmap_popup(Surf *s) {
    for (int i = 0; i < npopups; i++)
        if (popups[i] == s) {
            popup_box_damage(s);
            memmove(popups + i, popups + i + 1, sizeof *popups * (size_t)(npopups - i - 1));
            npopups--;
            break;
        }
}

static void surface_gone(struct wl_resource *r) {
    Surf *s = wl_resource_get_user_data(r);
    if (pointer_focus == s) { pointer_focus = NULL; buttons_held = 0; }
    if (key_focus == s) key_focus = NULL;
    unmap_popup(s);
    if (s->role == R_TOPLEVEL && s->mapped) emit(s, "unmap", "{}");
    if (s->parent) {
        Surf *p = s->parent;
        for (int i = 0; i < p->nkids; i++) if (p->kids[i] == s) { p->kids[i] = p->kids[--p->nkids]; break; }
        damage_node(p);
    }
    for (int i = 0; i < s->nkids; i++) s->kids[i]->parent = NULL;
    for (int i = 0; i < MAX_SURF; i++) {
        if (surfs[i] == s) surfs[i] = NULL;
        else if (surfs[i] && surfs[i]->parent == s) surfs[i]->parent = NULL;
    }
    if (s->p_buf) wl_list_remove(&s->p_buf_destroy.link);
    struct wl_resource *cb, *tmp;
    wl_resource_for_each_safe(cb, tmp, &s->p_frames) wl_resource_destroy(cb);
    wl_resource_for_each_safe(cb, tmp, &s->frames) wl_resource_destroy(cb);
    wl_resource_for_each_safe(cb, tmp, &s->p_feedback) { wp_presentation_feedback_send_discarded(cb); wl_resource_destroy(cb); }
    wl_resource_for_each_safe(cb, tmp, &s->feedback) { wp_presentation_feedback_send_discarded(cb); wl_resource_destroy(cb); }
    wl_resource_for_each_safe(cb, tmp, &s->sent_feedback) { wp_presentation_feedback_send_discarded(cb); wl_resource_destroy(cb); }
    if (s->viewport) wl_resource_set_user_data(s->viewport, NULL);
    if (s->frac) wl_resource_set_user_data(s->frac, NULL);
    if (s->kde) wl_resource_set_user_data(s->kde, NULL);
    if (s->node) damage_node(s);
    if (fs_surf == s) { fs_surf = NULL; ui.dirty = 1; damage_add((Box){ 0, 0, ui.width, ui.height }); }
    if (cursor_surf == s) { cursor_surf = NULL; cursor_mode = 0; cursor_gen++; }
    if (constraint && constraint->surf == s) { constraint->surf = NULL; constraint->active = 0; }
    if (s->dmabuf) queue_release(s->dmabuf);
    gpu_tex_free(s->tex);
    free(s->px);
    free(s);
}

/* ---- wl_compositor / wl_region ---- */

static void rgn_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void rgn_op(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}
static const struct wl_region_interface region_impl = { .destroy = rgn_destroy, .add = rgn_op, .subtract = rgn_op };

static void comp_create_surface(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    int slot = -1;
    for (int i = 0; i < MAX_SURF; i++) if (!surfs[i]) { slot = i; break; }
    Surf *s = slot >= 0 ? calloc(1, sizeof *s) : NULL;
    struct wl_resource *res = s ? wl_resource_create(c, &wl_surface_interface, wl_resource_get_version(r), id) : NULL;
    if (!res) { free(s); wl_client_post_no_memory(c); return; }
    s->res = res;
    s->client = c;
    s->id = next_id++;
    wl_list_init(&s->p_frames);
    wl_list_init(&s->frames);
    wl_list_init(&s->p_feedback);
    wl_list_init(&s->feedback);
    wl_list_init(&s->sent_feedback);
    wl_list_init(&s->p_buf_destroy.link);
    s->vsw = s->p_vsw = -1;
    s->bscale = s->p_bscale = 1;
    surfs[slot] = s;
    wl_resource_set_implementation(res, &surface_impl, s, surface_gone);
}

static void comp_create_region(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    struct wl_resource *res = wl_resource_create(c, &wl_region_interface, wl_resource_get_version(r), id);
    if (!res) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(res, &region_impl, NULL, NULL);
}

static const struct wl_compositor_interface compositor_impl = {
    .create_surface = comp_create_surface, .create_region = comp_create_region,
};

static void bind_compositor(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &wl_compositor_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &compositor_impl, NULL, NULL);
}

/* ---- wl_subcompositor ---- */

static void sub_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void sub_position(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y) {
    (void)c;
    Surf *s = wl_resource_get_user_data(r);
    if (s) { s->p_sx = x; s->p_sy = y; }
}
static void sub_place(struct wl_client *c, struct wl_resource *r, struct wl_resource *sibling) { (void)c; (void)r; (void)sibling; }
static void sub_sync(struct wl_client *c, struct wl_resource *r) { (void)c; (void)r; }
static const struct wl_subsurface_interface subsurface_impl = {
    .destroy = sub_destroy, .set_position = sub_position, .place_above = sub_place, .place_below = sub_place,
    .set_sync = sub_sync, .set_desync = sub_sync,
};

static void sub_gone(struct wl_resource *r) {
    Surf *s = wl_resource_get_user_data(r);
    if (!s) return;
    if (s->parent) {
        Surf *p = s->parent;
        for (int i = 0; i < p->nkids; i++) if (p->kids[i] == s) { p->kids[i] = p->kids[--p->nkids]; break; }
        damage_node(p);
    }
    s->parent = NULL;
    s->sub = NULL;
    s->role = R_NONE;
}

static void subc_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void subc_get(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface, struct wl_resource *parent) {
    Surf *s = wl_resource_get_user_data(surface), *p = wl_resource_get_user_data(parent);
    struct wl_resource *res = wl_resource_create(c, &wl_subsurface_interface, wl_resource_get_version(r), id);
    if (!res) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(res, &subsurface_impl, s, sub_gone);
    s->role = R_SUB;
    s->sub = res;
    s->parent = p;
    if (p->nkids < 16) p->kids[p->nkids++] = s;
}
static const struct wl_subcompositor_interface subcompositor_impl = { .destroy = subc_destroy, .get_subsurface = subc_get };

static void bind_subcompositor(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &wl_subcompositor_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &subcompositor_impl, NULL, NULL);
}

/* ---- wl_output: one per display (docs/DISPLAYS.md) ----
 * Each display is a slot with its own global. Resources keep the slot index + 1 as user data (0
 * once the display is gone). Geometry and xdg-output carry the position in the global logical
 * space; mode is the physical one; scale is the integer one for legacy clients (they render at it
 * and LordOS downscales), and fractional-scale-v1 tells the others the exact one. */
typedef struct {
    WlOutput o;
    struct wl_global *global;
    struct wl_list res, xdg;            /* wl_output and zxdg_output_v1 resources */
    int used;
} OutSlot;
static OutSlot oslots[OUT_MAX];
static int out_main;

static int int_scale(int pct) { return (pct + 99) / 100; }

static void out_release(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static const struct wl_output_interface output_impl = { .release = out_release };
static void out_gone(struct wl_resource *r) { wl_list_remove(wl_resource_get_link(r)); }

static OutSlot *slot_of(struct wl_resource *r) {
    intptr_t i = (intptr_t)wl_resource_get_user_data(r);
    return i >= 1 && i <= OUT_MAX && oslots[i - 1].used ? &oslots[i - 1] : NULL;
}

static void send_output(struct wl_resource *r, const WlOutput *o) {
    int ver = wl_resource_get_version(r);
    wl_output_send_geometry(r, o->x, o->y, o->mm_w, o->mm_h, WL_OUTPUT_SUBPIXEL_UNKNOWN, "LordOS", o->desc[0] ? o->desc : "lord-server",
                            WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, o->pw, o->ph, o->refresh > 0 ? o->refresh : 60000);
    if (ver >= 2) wl_output_send_scale(r, int_scale(o->scale));
    if (ver >= 4) { wl_output_send_name(r, o->name); wl_output_send_description(r, o->desc[0] ? o->desc : o->name); }
}

static void send_xdg_output(struct wl_resource *x, const WlOutput *o) {
    zxdg_output_v1_send_logical_position(x, o->x, o->y);
    zxdg_output_v1_send_logical_size(x, o->w, o->h);
    int ver = wl_resource_get_version(x);
    if (ver >= 2 && ver < 3) { zxdg_output_v1_send_name(x, o->name); zxdg_output_v1_send_description(x, o->desc[0] ? o->desc : o->name); }
    if (ver < 3) zxdg_output_v1_send_done(x);
}

static void bind_output(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    intptr_t i = (intptr_t)data;
    struct wl_resource *r = wl_resource_create(c, &wl_output_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &output_impl, (void *)i, out_gone);
    OutSlot *o = i >= 1 && i <= OUT_MAX ? &oslots[i - 1] : NULL;
    if (!o || !o->used) { wl_list_init(wl_resource_get_link(r)); return; }
    wl_list_insert(&o->res, wl_resource_get_link(r));
    send_output(r, &o->o);
    if (ver >= 2) wl_output_send_done(r);
    /* a surface of this client already on that display */
    for (int k = 0; k < MAX_SURF; k++)
        if (surfs[k] && surfs[k]->client == c && (surfs[k]->outmask >> (i - 1) & 1)) wl_surface_send_enter(surfs[k]->res, r);
}

/* xdg-output (zxdg_output_manager_v1 v3): logical position and size of each wl_output */
static void xo_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static const struct zxdg_output_v1_interface xdg_output_impl = { .destroy = xo_destroy };
static void xo_gone(struct wl_resource *r) { wl_list_remove(wl_resource_get_link(r)); }
static void xom_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void xom_get(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *output) {
    struct wl_resource *x = wl_resource_create(c, &zxdg_output_v1_interface, wl_resource_get_version(r), id);
    if (!x) { wl_client_post_no_memory(c); return; }
    OutSlot *o = slot_of(output);
    wl_resource_set_implementation(x, &xdg_output_impl, wl_resource_get_user_data(output), xo_gone);
    if (!o) { wl_list_init(wl_resource_get_link(x)); return; }
    wl_list_insert(&o->xdg, wl_resource_get_link(x));
    send_xdg_output(x, &o->o);
    if (wl_resource_get_version(x) >= 3 && wl_resource_get_version(output) >= 2) wl_output_send_done(output);
}
static const struct zxdg_output_manager_v1_interface xdg_output_mgr_impl = { .destroy = xom_destroy, .get_xdg_output = xom_get };
static void bind_xdg_output_mgr(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &zxdg_output_manager_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &xdg_output_mgr_impl, NULL, NULL);
}

/* fractional-scale-v1: the exact scale (×120) for each surface */
static void fs1_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static const struct wp_fractional_scale_v1_interface frac_impl = { .destroy = fs1_destroy };
static void frac_gone(struct wl_resource *r) {
    Surf *s = wl_resource_get_user_data(r);
    if (s) s->frac = NULL;
}
static int pointer_scale = 100;         /* the display under the pointer: where a new window opens */
static void fsm_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void fsm_get(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface) {
    Surf *s = wl_resource_get_user_data(surface);
    if (s && s->frac) { wl_resource_post_error(r, WP_FRACTIONAL_SCALE_MANAGER_V1_ERROR_FRACTIONAL_SCALE_EXISTS, "already has one"); return; }
    struct wl_resource *f = wl_resource_create(c, &wp_fractional_scale_v1_interface, wl_resource_get_version(r), id);
    if (!f) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(f, &frac_impl, s, frac_gone);
    if (!s) return;
    s->frac = f;
    int sc = s->pref_scale ? s->pref_scale : pointer_scale;
    wp_fractional_scale_v1_send_preferred_scale(f, (uint32_t)(sc * 120 / 100));
}
static const struct wp_fractional_scale_manager_v1_interface frac_mgr_impl = { .destroy = fsm_destroy, .get_fractional_scale = fsm_get };
static void bind_frac_mgr(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &wp_fractional_scale_manager_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &frac_mgr_impl, NULL, NULL);
}

static void surface_leave_slot(int slot);
static void fs_follow_outputs(void);
static int oslots_used(int slot) { return oslots[slot].used; }
static void oslot_rect(int slot, float *x, float *y, float *w, float *h) {
    const WlOutput *o = &oslots[slot].o;
    *x = (float)o->x; *y = (float)o->y; *w = (float)o->w; *h = (float)o->h;
}
/* The display with most of a logical box (a window going fullscreen, a menu's parent). */
static int slot_for_box(float x0, float y0, float x1, float y1) {
    int best = -1;
    float area = 0;
    for (int i = 0; i < OUT_MAX; i++) {
        if (!oslots[i].used) continue;
        const WlOutput *o = &oslots[i].o;
        float w = fminf(x1, (float)(o->x + o->w)) - fmaxf(x0, (float)o->x), h = fminf(y1, (float)(o->y + o->h)) - fmaxf(y0, (float)o->y);
        if (w > 0 && h > 0 && w * h > area) { area = w * h; best = i; }
    }
    return best;
}

void wl_set_outputs(const WlOutput *list, int n, int main) {
    if (!dpy) return;
    int keep[OUT_MAX] = { 0 };
    for (int j = 0; j < n && j < OUT_MAX; j++) {
        int slot = -1;
        for (int i = 0; i < OUT_MAX && slot < 0; i++) if (oslots[i].used && !strcmp(oslots[i].o.name, list[j].name)) slot = i;
        if (slot >= 0 && keep[slot]) slot = -1;             /* two with the same name: a new slot */
        if (slot < 0) for (int i = 0; i < OUT_MAX && slot < 0; i++) if (!oslots[i].used && !keep[i]) slot = i;
        if (slot < 0) continue;
        keep[slot] = 1;
        OutSlot *o = &oslots[slot];
        if (j == main) out_main = slot;
        if (o->used && !memcmp(&o->o, &list[j], sizeof o->o)) continue;
        o->o = list[j];
        if (!o->used) {
            o->used = 1;
            wl_list_init(&o->res);
            wl_list_init(&o->xdg);
            o->global = wl_global_create(dpy, &wl_output_interface, 4, (void *)(intptr_t)(slot + 1), bind_output);
            continue;
        }
        struct wl_resource *r;
        wl_resource_for_each(r, &o->res) send_output(r, &o->o);
        wl_resource_for_each(r, &o->xdg) send_xdg_output(r, &o->o);
        wl_resource_for_each(r, &o->res) if (wl_resource_get_version(r) >= 2) wl_output_send_done(r);
    }
    for (int i = 0; i < OUT_MAX; i++) {
        if (!oslots[i].used || keep[i]) continue;
        surface_leave_slot(i);
        struct wl_resource *r, *t;
        wl_resource_for_each_safe(r, t, &oslots[i].res) { wl_resource_set_user_data(r, NULL); wl_list_remove(wl_resource_get_link(r)); wl_list_init(wl_resource_get_link(r)); }
        wl_resource_for_each_safe(r, t, &oslots[i].xdg) { wl_resource_set_user_data(r, NULL); wl_list_remove(wl_resource_get_link(r)); wl_list_init(wl_resource_get_link(r)); }
        if (oslots[i].global) wl_global_destroy(oslots[i].global);
        memset(&oslots[i], 0, sizeof oslots[i]);
    }
    fs_follow_outputs();
}

/* ---- which displays a surface is on (enter/leave, preferred scale), once per frame ---- */

static void send_enter_leave(Surf *s, unsigned mask) {
    for (int i = 0; i < OUT_MAX; i++) {
        unsigned bit = 1u << i;
        if (!oslots[i].used || (mask & bit) == (s->outmask & bit)) continue;
        struct wl_resource *r;
        FOR_CLIENT(&oslots[i].res, s->client, r) {
            if (mask & bit) wl_surface_send_enter(s->res, r);
            else wl_surface_send_leave(s->res, r);
        }
    }
    s->outmask = mask;
}

static void set_pref_scale(Surf *s, int pct) {
    if (s->pref_scale == pct) return;
    s->pref_scale = pct;
    if (s->frac) wp_fractional_scale_v1_send_preferred_scale(s->frac, (uint32_t)(pct * 120 / 100));
    if (wl_resource_get_version(s->res) >= 6) wl_surface_send_preferred_buffer_scale(s->res, int_scale(pct));
}

static void surface_outputs(Surf *s, float x, float y, int depth) {
    if (depth > 8) return;
    unsigned mask = 0;
    int best = 0;
    for (int i = 0; i < OUT_MAX; i++) {
        const WlOutput *o = &oslots[i].o;
        if (!oslots[i].used || s->lw <= 0) continue;
        if (x < o->x + o->w && x + s->lw > o->x && y < o->y + o->h && y + s->lh > o->y) {
            mask |= 1u << i;
            if (o->scale > best) best = o->scale;           /* sharp on the densest display it touches */
        }
    }
    if (mask != s->outmask) send_enter_leave(s, mask);
    if (best) set_pref_scale(s, best);
    else if (!s->pref_scale) set_pref_scale(s, pointer_scale);
    for (int k = 0; k < s->nkids; k++) surface_outputs(s->kids[k], x + s->kids[k]->sx, y + s->kids[k]->sy, depth + 1);
}

static void update_surface_outputs(void) {
    for (int i = 0; i < MAX_SURF; i++) {
        Surf *s = surfs[i];
        if (!s || s->parent || (s->role != R_TOPLEVEL && s->role != R_POPUP) || !s->content) continue;
        float x, y;
        if (origin(s, &x, &y)) surface_outputs(s, x, y, 0);
    }
    for (int i = 0; i < npopups; i++) {
        float x, y;
        if (origin(popups[i], &x, &y)) surface_outputs(popups[i], x, y, 0);
    }
}

static void surface_leave_slot(int slot) {
    for (int i = 0; i < MAX_SURF; i++)
        if (surfs[i] && (surfs[i]->outmask >> slot & 1)) send_enter_leave(surfs[i], surfs[i]->outmask & ~(1u << slot));
}

/* ---- wl_seat: pointer and keyboard ---- */

/* App cursor: only valid if the pointer is over one of its surfaces. */
static void ptr_set_cursor(struct wl_client *c, struct wl_resource *r, uint32_t serial, struct wl_resource *surface, int32_t x, int32_t y) {
    (void)r; (void)serial;
    if (!pointer_focus || pointer_focus->client != c) return;
    Surf *s = surface ? wl_resource_get_user_data(surface) : NULL;
    if (s && s->role != R_NONE && s->role != R_CURSOR) return;
    if (s) { s->role = R_CURSOR; s->cursor_hx = s->p_hx = x; s->cursor_hy = s->p_hy = y; }
    cursor_surf = s;
    cursor_mode = s ? 1 : 2;
    cursor_gen++;
}
static void res_release(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }

/* cursor-shape-v1: the app asks for a shape by name (Chromium, GTK 4, Qt 6) and LordOS draws its own
 * (cursor.c); cursor_mode 3. Only valid if the pointer is over a surface of that app. */
static unsigned cursor_shape;
static void csd_set_shape(struct wl_client *c, struct wl_resource *r, uint32_t serial, uint32_t shape) {
    (void)serial;
    if (shape < 1 || shape > 36) {
        wl_resource_post_error(r, WP_CURSOR_SHAPE_DEVICE_V1_ERROR_INVALID_SHAPE, "invalid shape %u", shape);
        return;
    }
    if (!pointer_focus || pointer_focus->client != c) return;
    cursor_surf = NULL;
    cursor_mode = 3;
    cursor_shape = shape;
    cursor_gen++;
}
static const struct wp_cursor_shape_device_v1_interface csd_impl = { .destroy = res_release, .set_shape = csd_set_shape };
static void csm_device(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    struct wl_resource *d = wl_resource_create(c, &wp_cursor_shape_device_v1_interface, wl_resource_get_version(r), id);
    if (!d) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(d, &csd_impl, NULL, NULL);
}
static void csm_get_pointer(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *pointer) {
    (void)pointer;
    csm_device(c, r, id);
}
static void csm_get_tablet(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *tool) {
    (void)tool;
    csm_device(c, r, id);               /* no tablets: the device exists but is unused */
}
static const struct wp_cursor_shape_manager_v1_interface csm_impl = {
    .destroy = res_release, .get_pointer = csm_get_pointer, .get_tablet_tool_v2 = csm_get_tablet,
};
static void bind_csm(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &wp_cursor_shape_manager_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &csm_impl, NULL, NULL);
}
static const struct wl_pointer_interface pointer_impl = { .set_cursor = ptr_set_cursor, .release = res_release };
static const struct wl_keyboard_interface keyboard_impl = { .release = res_release };
static const struct wl_touch_interface touch_impl = { .release = res_release };
static void link_gone(struct wl_resource *r) { wl_list_remove(wl_resource_get_link(r)); }

static void send_keymap(struct wl_resource *kb) {
    char *km = kb_keymap_string();
    if (!km) return;
    size_t size = strlen(km) + 1;
    int fd = memfd_create("lord-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd >= 0 && ftruncate(fd, (off_t)size) == 0) {
        void *m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (m != MAP_FAILED) {
            memcpy(m, km, size);
            munmap(m, size);
            fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
            wl_keyboard_send_keymap(kb, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, (uint32_t)size);
        }
    }
    if (fd >= 0) close(fd);
    free(km);
}

static void send_enter_keyboard(struct wl_resource *kb, Surf *s) {
    struct wl_array keys;
    wl_array_init(&keys);
    wl_keyboard_send_enter(kb, wl_display_next_serial(dpy), s->res, &keys);
    wl_array_release(&keys);
    uint32_t d, l, k, g;
    kb_mods(&d, &l, &k, &g);
    wl_keyboard_send_modifiers(kb, wl_display_next_serial(dpy), d, l, k, g);
}

static void seat_get_pointer(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    struct wl_resource *p = wl_resource_create(c, &wl_pointer_interface, wl_resource_get_version(r), id);
    if (!p) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(p, &pointer_impl, NULL, link_gone);
    wl_list_insert(&pointers, wl_resource_get_link(p));
}

static void seat_get_keyboard(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    struct wl_resource *k = wl_resource_create(c, &wl_keyboard_interface, wl_resource_get_version(r), id);
    if (!k) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(k, &keyboard_impl, NULL, link_gone);
    wl_list_insert(&keyboards, wl_resource_get_link(k));
    if (wl_resource_get_version(k) >= 4) wl_keyboard_send_repeat_info(k, 30, 500);
    send_keymap(k);
    if (key_focus && key_focus->client == c) send_enter_keyboard(k, key_focus);
}

static void seat_get_touch(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    (void)touch_impl;
    wl_input_get_touch(c, r, id);       /* touchscreens: wl_input.c */
}

static const struct wl_seat_interface seat_impl = {
    .get_pointer = seat_get_pointer, .get_keyboard = seat_get_keyboard, .get_touch = seat_get_touch, .release = res_release,
};

static void bind_seat(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &wl_seat_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &seat_impl, NULL, NULL);
    wl_input_seat_caps(r);              /* pointer, keyboard and touch if there is a touchscreen */
    if (ver >= 2) wl_seat_send_name(r, "seat0");
}

void wl_keymap_changed(void) {
    if (!dpy) return;
    struct wl_resource *k;
    wl_resource_for_each(k, &keyboards) send_keymap(k);
}

/* ---- clipboard (wl_data_device_manager) ----
 * An app's text selection is read in full (text/plain) and goes to the LordOS
 * clipboard; if it is only an image (image/png), it is kept here, so it stays available when the app
 * closes and LordOS shows it in the history. While the owning app lives, the other apps
 * receive from it any format it offers (the request is forwarded to it). The LordOS
 * selection (terminal, text fields, an image from the history) is offered to the focused app. */

#define MAX_MIMES 32
#define CLIP_MAX (16u << 20)
typedef struct { struct wl_resource *res; char mimes[MAX_MIMES][64]; int nmimes; } Source;
static Source *selection;           /* source of the app owning the selection, or NULL (LordOS) */
static unsigned clip_seen;          /* LordOS clipboard generation already offered */
static unsigned char *clip_png;     /* last copied image */
static size_t clip_png_len;
static unsigned clip_img_gen;
static int clip_is_image;           /* the last thing copied was that image (and not text) */
static char clip_png_path[160];
static char clip_img_origin[128] = "linux";
static int clip_img_sensitive;

static int text_mime(const char *m) {
    return !strcmp(m, "text/plain;charset=utf-8") || !strcmp(m, "text/plain") || !strcmp(m, "UTF8_STRING") ||
           !strcmp(m, "STRING") || !strcmp(m, "TEXT");
}

static void src_offer(struct wl_client *c, struct wl_resource *r, const char *mime) {
    (void)c;
    Source *s = wl_resource_get_user_data(r);
    if (s->nmimes < MAX_MIMES) snprintf(s->mimes[s->nmimes++], sizeof s->mimes[0], "%s", mime);
}
static void src_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void src_actions(struct wl_client *c, struct wl_resource *r, uint32_t a) { (void)c; (void)r; (void)a; }
static const struct wl_data_source_interface source_impl = { .offer = src_offer, .destroy = src_destroy, .set_actions = src_actions };
static void offer_to_focus(void);
static void src_gone(struct wl_resource *r) {
    Source *s = wl_resource_get_user_data(r);
    if (selection == s) {
        selection = NULL;
        if (clip_is_image) offer_to_focus();    /* the image stays, now LordOS offers it */
    }
    free(s);
}

/* The image is also saved as a file in $XDG_RUNTIME_DIR/lordos (user-only),
 * for the history preview; the last 10 are kept. */
static void store_png(const unsigned char *png, size_t n) {
    unsigned char *copy = malloc(n);
    if (!copy) return;
    memcpy(copy, png, n);
    free(clip_png);
    clip_png = copy;
    clip_png_len = n;
    clip_img_gen++;
    clip_is_image = 1;
    clip_png_path[0] = 0;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt) return;
    char dir[128], old[160];
    snprintf(dir, sizeof dir, "%s/lordos", rt);
    mkdir(dir, 0700);
    if (clip_img_gen > 10) { snprintf(old, sizeof old, "%s/clip-%u.png", dir, clip_img_gen - 10); unlink(old); }
    snprintf(clip_png_path, sizeof clip_png_path, "%s/clip-%u.png", dir, clip_img_gen);
    int fd = open(clip_png_path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    size_t off = 0;
    while (fd >= 0 && off < n) { ssize_t w = write(fd, png + off, n - off); if (w <= 0) break; off += (size_t)w; }
    if (fd < 0 || close(fd) || off != n) clip_png_path[0] = 0;
}

/* reading an app's selection into LordOS */
typedef struct { int fd, image, sensitive; struct wl_event_source *ev; char *buf; size_t len, cap; char origin[128]; } Reader;

static int reader_ready(int fd, uint32_t mask, void *data) {
    Reader *rd = data;
    (void)mask;
    for (;;) {
        if (rd->len + 4096 > rd->cap) {
            size_t cap = rd->cap ? rd->cap * 2 : 8192;
            if (cap > CLIP_MAX) break;
            char *nb = realloc(rd->buf, cap);
            if (!nb) break;
            rd->buf = nb;
            rd->cap = cap;
        }
        ssize_t n = read(fd, rd->buf + rd->len, rd->cap - rd->len);
        if (n > 0) { rd->len += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
        break;                              /* end (or error): done */
    }
    if (rd->len && rd->image) {
        if (rd->len > 8 && !memcmp(rd->buf, "\x89PNG\r\n\x1a\n", 8)) {
            store_png((unsigned char *)rd->buf, rd->len);
            snprintf(clip_img_origin, sizeof clip_img_origin, "%s", rd->origin);
            clip_img_sensitive = rd->sensitive;
        }
    } else if (rd->len) {
        term_set_clipboard(rd->buf, rd->len);
        term_clipboard_mark(rd->origin, rd->sensitive);
        clip_seen = term_clipboard_gen();       /* not a change from LordOS */
        clip_is_image = 0;
    }
    wl_event_source_remove(rd->ev);
    close(rd->fd);
    free(rd->buf);
    free(rd);
    return 0;
}

static void read_selection(Source *s) {
    if (getenv("LORD_WL_DEBUG")) for (int i = 0; i < s->nmimes; i++) fprintf(stderr, "wayland: selection %s\n", s->mimes[i]);
    const char *mime = NULL;
    int image = 0;
    for (int i = 0; i < s->nmimes && !mime; i++) if (!strcmp(s->mimes[i], "text/plain;charset=utf-8")) mime = s->mimes[i];
    for (int i = 0; i < s->nmimes && !mime; i++) if (text_mime(s->mimes[i])) mime = s->mimes[i];
    for (int i = 0; i < s->nmimes && !mime; i++) if (!strcmp(s->mimes[i], "image/png")) { mime = s->mimes[i]; image = 1; }
    if (!mime) return;
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK)) return;
    wl_data_source_send_send(s->res, mime, p[1]);
    close(p[1]);
    Reader *rd = calloc(1, sizeof *rd);
    if (!rd) { close(p[0]); return; }
    rd->fd = p[0];
    rd->image = image;
    /* origin: the app_id of a window of that app; sensitive if it is a password manager
     * (KDE/GNOME convention: offers x-kde-passwordManagerHint = secret) */
    struct wl_client *cl = wl_resource_get_client(s->res);
    for (int i = 0; i < MAX_SURF && !rd->origin[0]; i++)
        if (surfs[i] && surfs[i]->client == cl && surfs[i]->app_id[0]) snprintf(rd->origin, sizeof rd->origin, "%s", surfs[i]->app_id);
    if (!rd->origin[0]) snprintf(rd->origin, sizeof rd->origin, "linux");
    for (int i = 0; i < s->nmimes; i++) if (!strcmp(s->mimes[i], "x-kde-passwordManagerHint")) rd->sensitive = 1;
    rd->ev = wl_event_loop_add_fd(loop, p[0], WL_EVENT_READABLE, reader_ready, rd);
    if (!rd->ev) { close(p[0]); free(rd); }
}

/* writing to an app, without blocking the server if the app reads slowly */
typedef struct { int fd; struct wl_event_source *ev; unsigned char *buf; size_t len, off; } Writer;

static int writer_ready(int fd, uint32_t mask, void *data) {
    Writer *w = data;
    (void)mask;
    while (w->off < w->len) {
        ssize_t n = write(fd, w->buf + w->off, w->len - w->off);
        if (n > 0) { w->off += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
        break;
    }
    wl_event_source_remove(w->ev);
    close(w->fd);
    explicit_bzero(w->buf, w->len);       /* may be a password copied from Passwords */
    free(w->buf);
    free(w);
    return 0;
}

static void send_bytes(int fd, const void *data, size_t n) {
    Writer *w = calloc(1, sizeof *w);
    if (!w || !(w->buf = malloc(n ? n : 1))) { free(w); close(fd); return; }
    memcpy(w->buf, data, n);
    w->len = n;
    w->fd = fd;
    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    w->ev = wl_event_loop_add_fd(loop, fd, WL_EVENT_WRITABLE, writer_ready, w);
    if (!w->ev) { close(fd); free(w->buf); free(w); }
}

/* offers: from another app (forwarded) or from the LordOS clipboard (written) */
static void ofr_accept(struct wl_client *c, struct wl_resource *r, uint32_t serial, const char *mime) {
    (void)c; (void)r; (void)serial; (void)mime;
}
static void ofr_receive(struct wl_client *c, struct wl_resource *r, const char *mime, int32_t fd) {
    (void)c;
    Source *src = wl_resource_get_user_data(r);
    if (src && src == selection) { wl_data_source_send_send(src->res, mime, fd); close(fd); return; }
    if (!src && clip_is_image && clip_png && !strcmp(mime, "image/png")) { send_bytes(fd, clip_png, clip_png_len); return; }
    if (!src && !clip_is_image && text_mime(mime)) { const char *t = term_clipboard(); send_bytes(fd, t, strlen(t)); return; }
    /* KDE/GNOME convention: clipboard managers skip an offer carrying this hint */
    if (!src && !clip_is_image && !strcmp(mime, "x-kde-passwordManagerHint") && term_clipboard_sensitive()) { send_bytes(fd, "secret", 6); return; }
    close(fd);
}
static void ofr_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void ofr_finish(struct wl_client *c, struct wl_resource *r) { (void)c; (void)r; }
static void ofr_actions(struct wl_client *c, struct wl_resource *r, uint32_t a, uint32_t p) { (void)c; (void)r; (void)a; (void)p; }
static const struct wl_data_offer_interface offer_impl = {
    .accept = ofr_accept, .receive = ofr_receive, .destroy = ofr_destroy, .finish = ofr_finish, .set_actions = ofr_actions,
};

static void offer_selection(struct wl_resource *dev) {
    struct wl_client *c = wl_resource_get_client(dev);
    if (selection && wl_resource_get_client(selection->res) == c) return;     /* it is its own */
    const char *t = term_clipboard();
    int image = !selection && clip_is_image && clip_png;
    if (!selection && !image && !t[0]) { wl_data_device_send_selection(dev, NULL); return; }
    struct wl_resource *o = wl_resource_create(c, &wl_data_offer_interface, wl_resource_get_version(dev), 0);
    if (!o) return;
    wl_resource_set_implementation(o, &offer_impl, selection, NULL);
    wl_data_device_send_data_offer(dev, o);
    if (selection) for (int i = 0; i < selection->nmimes; i++) wl_data_offer_send_offer(o, selection->mimes[i]);
    else if (image) wl_data_offer_send_offer(o, "image/png");
    else {
        wl_data_offer_send_offer(o, "text/plain;charset=utf-8");
        wl_data_offer_send_offer(o, "text/plain");
        wl_data_offer_send_offer(o, "UTF8_STRING");
        /* a secret copied in LordOS (Passwords, password fields): Linux clipboard managers must not keep it */
        if (term_clipboard_sensitive()) wl_data_offer_send_offer(o, "x-kde-passwordManagerHint");
    }
    wl_data_device_send_selection(dev, o);
}

/* Clipboard image for JS (history) and copying one from the history again. */
unsigned wl_clip_image_gen(void) { return clip_img_gen; }
const char *wl_clip_image_path(void) { return clip_is_image ? clip_png_path : ""; }
int wl_clip_is_image(void) { return clip_is_image; }
const unsigned char *wl_clip_png(size_t *len) {
    *len = clip_is_image && clip_png ? clip_png_len : 0;
    return *len ? clip_png : NULL;
}
const char *wl_clip_image_origin(void) { return clip_img_origin; }
int wl_clip_image_sensitive(void) { return clip_img_sensitive; }

/* Only two places: the compositor's previews ($XDG_RUNTIME_DIR/lordos/clip-N.png) and the
 * saved history (~/.local/share/lordos/clipboard/img-*.png). */
static int clip_path_ok(const char *path) {
    const char *rt = getenv("XDG_RUNTIME_DIR"), *home = getenv("HOME");
    char dir[PATH_MAX];
    size_t n = strlen(path);
    if (n < 6 || strcmp(path + n - 4, ".png") || strstr(path, "..")) return 0;
    for (int k = 0; k < 2; k++) {
        if (k == 0 && rt) snprintf(dir, sizeof dir, "%s/lordos/clip-", rt);
        else if (k == 1 && home) snprintf(dir, sizeof dir, "%s/.local/share/lordos/clipboard/img-", home);
        else continue;
        size_t dl = strlen(dir);
        if (!strncmp(path, dir, dl) && n > dl + 4 && !strchr(path + dl, '/')) return 1;
    }
    return 0;
}

int wl_clip_use_image(const char *path) {
    if (!dpy || !clip_path_ok(path)) return -1;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || (size_t)st.st_size > CLIP_MAX) { if (fd >= 0) close(fd); return -1; }
    unsigned char *buf = malloc((size_t)st.st_size + 1);
    size_t got = 0;
    while (buf && got < (size_t)st.st_size) { ssize_t r = read(fd, buf + got, (size_t)st.st_size - got); if (r <= 0) break; got += (size_t)r; }
    close(fd);
    if (!buf || got != (size_t)st.st_size) { free(buf); return -1; }
    if (selection) { wl_data_source_send_cancelled(selection->res); selection = NULL; }
    store_png(buf, got);
    snprintf(clip_img_origin, sizeof clip_img_origin, "lordos");
    clip_img_sensitive = 0;
    free(buf);
    offer_to_focus();
    return 0;
}

static void offer_to_focus(void) {
    if (!key_focus) return;
    struct wl_resource *d;
    FOR_CLIENT(&data_devices, key_focus->client, d) offer_selection(d);
}

static void dev_drag(struct wl_client *c, struct wl_resource *r, struct wl_resource *src, struct wl_resource *origin_,
                     struct wl_resource *icon, uint32_t serial) {
    (void)c; (void)r; (void)origin_; (void)icon; (void)serial;
    if (src) wl_data_source_send_cancelled(src);     /* drag and drop: not yet */
}
static void dev_set_selection(struct wl_client *c, struct wl_resource *r, struct wl_resource *src, uint32_t serial) {
    (void)c; (void)r; (void)serial;
    Source *s = src ? wl_resource_get_user_data(src) : NULL;
    if (selection && selection != s) wl_data_source_send_cancelled(selection->res);
    selection = s;
    if (s) read_selection(s);
}
static const struct wl_data_device_interface device_impl = { .start_drag = dev_drag, .set_selection = dev_set_selection, .release = res_release };

static void ddm_create_source(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    Source *s = calloc(1, sizeof *s);
    struct wl_resource *res = s ? wl_resource_create(c, &wl_data_source_interface, wl_resource_get_version(r), id) : NULL;
    if (!res) { free(s); wl_client_post_no_memory(c); return; }
    s->res = res;
    wl_resource_set_implementation(res, &source_impl, s, src_gone);
}
static void ddm_get_device(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *seat) {
    (void)seat;
    struct wl_resource *d = wl_resource_create(c, &wl_data_device_interface, wl_resource_get_version(r), id);
    if (!d) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(d, &device_impl, NULL, link_gone);
    wl_list_insert(&data_devices, wl_resource_get_link(d));
    if (key_focus && key_focus->client == c) offer_selection(d);
}
static const struct wl_data_device_manager_interface ddm_impl = { .create_data_source = ddm_create_source, .get_data_device = ddm_get_device };

static void bind_ddm(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &wl_data_device_manager_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &ddm_impl, NULL, NULL);
}

/* ---- xdg_positioner ---- */

typedef struct {
    int w, h, ax, ay, aw, ah, anchor, gravity, constraint, ox, oy;
} Positioner;

static void pos_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void pos_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h) {
    (void)c; Positioner *p = wl_resource_get_user_data(r); p->w = w; p->h = h;
}
static void pos_rect(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)c; Positioner *p = wl_resource_get_user_data(r); p->ax = x; p->ay = y; p->aw = w; p->ah = h;
}
static void pos_anchor(struct wl_client *c, struct wl_resource *r, uint32_t a) { (void)c; ((Positioner *)wl_resource_get_user_data(r))->anchor = (int)a; }
static void pos_gravity(struct wl_client *c, struct wl_resource *r, uint32_t g) { (void)c; ((Positioner *)wl_resource_get_user_data(r))->gravity = (int)g; }
static void pos_constraint(struct wl_client *c, struct wl_resource *r, uint32_t k) { (void)c; ((Positioner *)wl_resource_get_user_data(r))->constraint = (int)k; }
static void pos_offset(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y) {
    (void)c; Positioner *p = wl_resource_get_user_data(r); p->ox = x; p->oy = y;
}
static void pos_reactive(struct wl_client *c, struct wl_resource *r) { (void)c; (void)r; }
static void pos_parent_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h) { (void)c; (void)r; (void)w; (void)h; }
static void pos_parent_conf(struct wl_client *c, struct wl_resource *r, uint32_t s) { (void)c; (void)r; (void)s; }
static const struct xdg_positioner_interface positioner_impl = {
    .destroy = pos_destroy, .set_size = pos_size, .set_anchor_rect = pos_rect, .set_anchor = pos_anchor,
    .set_gravity = pos_gravity, .set_constraint_adjustment = pos_constraint, .set_offset = pos_offset,
    .set_reactive = pos_reactive, .set_parent_size = pos_parent_size, .set_parent_configure = pos_parent_conf,
};
static void pos_gone(struct wl_resource *r) { free(wl_resource_get_user_data(r)); }

static int has_left(int e) { return e == XDG_POSITIONER_ANCHOR_LEFT || e == XDG_POSITIONER_ANCHOR_TOP_LEFT || e == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT; }
static int has_right(int e) { return e == XDG_POSITIONER_ANCHOR_RIGHT || e == XDG_POSITIONER_ANCHOR_TOP_RIGHT || e == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT; }
static int has_top(int e) { return e == XDG_POSITIONER_ANCHOR_TOP || e == XDG_POSITIONER_ANCHOR_TOP_LEFT || e == XDG_POSITIONER_ANCHOR_TOP_RIGHT; }
static int has_bottom(int e) { return e == XDG_POSITIONER_ANCHOR_BOTTOM || e == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT || e == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT; }
/* (anchor and gravity use the same values) */
static int flip_v(int e) {
    switch (e) {
    case XDG_POSITIONER_ANCHOR_TOP: return XDG_POSITIONER_ANCHOR_BOTTOM;
    case XDG_POSITIONER_ANCHOR_BOTTOM: return XDG_POSITIONER_ANCHOR_TOP;
    case XDG_POSITIONER_ANCHOR_TOP_LEFT: return XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
    case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT: return XDG_POSITIONER_ANCHOR_TOP_LEFT;
    case XDG_POSITIONER_ANCHOR_TOP_RIGHT: return XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
    case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT: return XDG_POSITIONER_ANCHOR_TOP_RIGHT;
    default: return e;
    }
}
static int flip_h(int e) {
    switch (e) {
    case XDG_POSITIONER_ANCHOR_LEFT: return XDG_POSITIONER_ANCHOR_RIGHT;
    case XDG_POSITIONER_ANCHOR_RIGHT: return XDG_POSITIONER_ANCHOR_LEFT;
    case XDG_POSITIONER_ANCHOR_TOP_LEFT: return XDG_POSITIONER_ANCHOR_TOP_RIGHT;
    case XDG_POSITIONER_ANCHOR_TOP_RIGHT: return XDG_POSITIONER_ANCHOR_TOP_LEFT;
    case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT: return XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
    case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT: return XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
    default: return e;
    }
}

static void place(const Positioner *p, int anchor, int gravity, int *x, int *y) {
    int ax = p->ax + (has_left(anchor) ? 0 : has_right(anchor) ? p->aw : p->aw / 2);
    int ay = p->ay + (has_top(anchor) ? 0 : has_bottom(anchor) ? p->ah : p->ah / 2);
    *x = ax - (has_left(gravity) ? p->w : has_right(gravity) ? 0 : p->w / 2) + p->ox;
    *y = ay - (has_top(gravity) ? p->h : has_bottom(gravity) ? 0 : p->h / 2) + p->oy;
}

/* Menu position (relative to the parent's window geometry), within the screen. */
static void position_popup(Surf *s, const Positioner *p) {
    int x, y;
    place(p, p->anchor, p->gravity, &x, &y);
    float ox = 0, oy = 0;
    if (s->parent && origin(s->parent, &ox, &oy)) {
        ox += s->parent->gx;
        oy += s->parent->gy;
        /* within the display the parent is on (its rect, in the parent's terms) */
        float bx = 0, by = 0, W = ui.width, H = ui.height;
        int pslot = slot_for_box(ox, oy, ox + 1, oy + 1);
        if (pslot >= 0) { float rw, rh; oslot_rect(pslot, &bx, &by, &rw, &rh); W = bx + rw; H = by + rh; }
        ox -= bx; oy -= by; W -= bx; H -= by;
        if ((oy + y + p->h > H || oy + y < 0) && (p->constraint & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y)) {
            int fx, fy;
            place(p, flip_v(p->anchor), flip_v(p->gravity), &fx, &fy);
            if (oy + fy >= 0 && oy + fy + p->h <= H) y = fy;
        }
        if ((ox + x + p->w > W || ox + x < 0) && (p->constraint & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X)) {
            int fx, fy;
            place(p, flip_h(p->anchor), flip_h(p->gravity), &fx, &fy);
            if (ox + fx >= 0 && ox + fx + p->w <= W) x = fx;
        }
        if (ox + x + p->w > W) x = (int)(W - p->w - ox);       /* swipe */
        if (oy + y + p->h > H) y = (int)(H - p->h - oy);
        if (ox + x < 0) x = (int)-ox;
        if (oy + y < 0) y = (int)-oy;
    }
    s->pop_x = x;
    s->pop_y = y;
    s->conf_w = p->w;
    s->conf_h = p->h;
}

/* ---- xdg_toplevel ---- */

static void top_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void top_parent(struct wl_client *c, struct wl_resource *r, struct wl_resource *p) { (void)c; (void)r; (void)p; }
static void top_title(struct wl_client *c, struct wl_resource *r, const char *t) {
    (void)c;
    Surf *s = wl_resource_get_user_data(r);
    if (!s) return;
    snprintf(s->title, sizeof s->title, "%s", t);
    if (s->mapped) { char e[520]; json_str(e, sizeof e, s->title); emit(s, "title", "{\"title\":\"%s\"}", e); }
}
static void top_app_id(struct wl_client *c, struct wl_resource *r, const char *a) {
    (void)c;
    Surf *s = wl_resource_get_user_data(r);
    if (s) snprintf(s->app_id, sizeof s->app_id, "%s", a);
}
static void top_menu(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y) {
    (void)c; (void)r; (void)seat; (void)serial; (void)x; (void)y;
}
static void top_move(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial) {
    (void)c; (void)seat; (void)serial;
    Surf *s = wl_resource_get_user_data(r);
    if (s && s->mapped) { buttons_held = 0; emit(s, "move", "{\"x\":%.0f,\"y\":%.0f}", press_x, press_y); }
}
static void top_resize(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial, uint32_t edges) {
    (void)c; (void)seat; (void)serial;
    Surf *s = wl_resource_get_user_data(r);
    if (s && s->mapped) { buttons_held = 0; emit(s, "resize", "{\"edges\":%u,\"x\":%.0f,\"y\":%.0f}", edges, press_x, press_y); }
}
static void top_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h) { (void)c; (void)r; (void)w; (void)h; }
static void top_maximize(struct wl_client *c, struct wl_resource *r) {
    (void)c; Surf *s = wl_resource_get_user_data(r);
    if (s && s->mapped) emit(s, "maximize", "{}");
}
static void top_unmaximize(struct wl_client *c, struct wl_resource *r) {
    (void)c; Surf *s = wl_resource_get_user_data(r);
    if (s && s->mapped) emit(s, "unmaximize", "{}");
}
/* Fullscreen: presented by the compositor on top of everything (and straight to the card if possible). */
static void enter_fullscreen(Surf *s) {
    if (!s || !s->top || s == fs_surf) return;
    if (fs_surf) { fs_surf->fullscreen = 0; send_toplevel_configure(fs_surf); }
    fs_surf = s;
    s->fullscreen = 1;
    s->fs_saved_w = s->conf_w;
    s->fs_saved_h = s->conf_h;
    Node *fn = s->node ? node_get(s->node) : NULL;
    fs_slot = fn ? slot_for_box(fn->ax, fn->ay, fn->ax + fn->w, fn->ay + fn->h) : out_main;
    if (fs_slot < 0) fs_slot = out_main;
    float fx, fy, fw, fh;
    fs_rect(&fx, &fy, &fw, &fh);
    s->conf_w = (int)fw;
    s->conf_h = (int)fh;
    send_toplevel_configure(s);
    if (s->mapped) emit(s, "fullscreen", "{}");
    damage_add((Box){ 0, 0, ui.width, ui.height });
    ui.dirty = 1;
}

static void leave_fullscreen(Surf *s) {
    if (!s || s != fs_surf) return;
    fs_surf = NULL;
    s->fullscreen = 0;
    if (s->fs_saved_w > 16) { s->conf_w = s->fs_saved_w; s->conf_h = s->fs_saved_h; }
    send_toplevel_configure(s);
    if (s->mapped) emit(s, "unfullscreen", "{}");
    damage_add((Box){ 0, 0, ui.width, ui.height });
    ui.dirty = 1;
}

static void top_fullscreen(struct wl_client *c, struct wl_resource *r, struct wl_resource *out) {
    (void)c; (void)out;
    enter_fullscreen(wl_resource_get_user_data(r));
}
static void top_unfullscreen(struct wl_client *c, struct wl_resource *r) {
    (void)c;
    leave_fullscreen(wl_resource_get_user_data(r));
}
static void top_minimize(struct wl_client *c, struct wl_resource *r) {
    (void)c; Surf *s = wl_resource_get_user_data(r);
    if (s && s->mapped) emit(s, "minimize", "{}");
}
static const struct xdg_toplevel_interface toplevel_impl = {
    .destroy = top_destroy, .set_parent = top_parent, .set_title = top_title, .set_app_id = top_app_id,
    .show_window_menu = top_menu, .move = top_move, .resize = top_resize, .set_max_size = top_size,
    .set_min_size = top_size, .set_maximized = top_maximize, .unset_maximized = top_unmaximize,
    .set_fullscreen = top_fullscreen, .unset_fullscreen = top_unfullscreen, .set_minimized = top_minimize,
};
static void top_gone(struct wl_resource *r) {
    Surf *s = wl_resource_get_user_data(r);
    if (!s) return;
    if (s->mapped) emit(s, "unmap", "{}");
    s->mapped = 0;
    s->top = NULL;
    if (key_focus == s) key_focus = NULL;
}

/* ---- xdg_popup ---- */

static void pop_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void pop_grab(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial) {
    (void)c; (void)seat; (void)serial;
    Surf *s = wl_resource_get_user_data(r);
    if (s) s->grab = 1;
}
static void pop_reposition(struct wl_client *c, struct wl_resource *r, struct wl_resource *positioner, uint32_t token) {
    (void)c;
    Surf *s = wl_resource_get_user_data(r);
    if (!s) return;
    popup_box_damage(s);
    position_popup(s, wl_resource_get_user_data(positioner));
    xdg_popup_send_repositioned(r, token);
    xdg_popup_send_configure(r, s->pop_x, s->pop_y, s->conf_w, s->conf_h);
    xdg_surface_send_configure(s->xdg, wl_display_next_serial(dpy));
}
static const struct xdg_popup_interface popup_impl = { .destroy = pop_destroy, .grab = pop_grab, .reposition = pop_reposition };
static void pop_gone(struct wl_resource *r) {
    Surf *s = wl_resource_get_user_data(r);
    if (!s) return;
    unmap_popup(s);
    s->popup = NULL;
    s->mapped = 0;
    if (pointer_focus == s) pointer_focus = NULL;
}

/* ---- xdg_surface / xdg_wm_base ---- */

static void xs_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void xs_toplevel(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    Surf *s = wl_resource_get_user_data(r);
    struct wl_resource *t = wl_resource_create(c, &xdg_toplevel_interface, wl_resource_get_version(r), id);
    if (!t) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(t, &toplevel_impl, s, top_gone);
    s->top = t;
    s->role = R_TOPLEVEL;
    s->csd = !s->kde_ssd;                   /* until it asks for server decoration */
    if (wl_resource_get_version(t) >= 5) {
        struct wl_array caps;
        wl_array_init(&caps);
        uint32_t *p;
        if ((p = wl_array_add(&caps, sizeof *p))) *p = XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE;
        if ((p = wl_array_add(&caps, sizeof *p))) *p = XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE;
        xdg_toplevel_send_wm_capabilities(t, &caps);
        wl_array_release(&caps);
    }
}
static void xs_popup(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *parent, struct wl_resource *positioner) {
    Surf *s = wl_resource_get_user_data(r);
    struct wl_resource *p = wl_resource_create(c, &xdg_popup_interface, wl_resource_get_version(r), id);
    if (!p) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(p, &popup_impl, s, pop_gone);
    s->popup = p;
    s->role = R_POPUP;
    s->parent = parent ? wl_resource_get_user_data(parent) : NULL;
    position_popup(s, wl_resource_get_user_data(positioner));
}
static void xs_geometry(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)c;
    Surf *s = wl_resource_get_user_data(r);
    s->p_gx = x; s->p_gy = y; s->p_gw = w; s->p_gh = h;
    s->p_geom = 1;
}
static void xs_ack(struct wl_client *c, struct wl_resource *r, uint32_t serial) { (void)c; (void)r; (void)serial; }
static const struct xdg_surface_interface xdg_surface_impl = {
    .destroy = xs_destroy, .get_toplevel = xs_toplevel, .get_popup = xs_popup,
    .set_window_geometry = xs_geometry, .ack_configure = xs_ack,
};
static void xs_gone(struct wl_resource *r) {
    Surf *s = wl_resource_get_user_data(r);
    if (s) s->xdg = NULL;
}

static void wm_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void wm_positioner(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    Positioner *p = calloc(1, sizeof *p);
    struct wl_resource *res = p ? wl_resource_create(c, &xdg_positioner_interface, wl_resource_get_version(r), id) : NULL;
    if (!res) { free(p); wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(res, &positioner_impl, p, pos_gone);
}
static void wm_get_xdg_surface(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface) {
    Surf *s = wl_resource_get_user_data(surface);
    struct wl_resource *x = wl_resource_create(c, &xdg_surface_interface, wl_resource_get_version(r), id);
    if (!x) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(x, &xdg_surface_impl, s, xs_gone);
    s->xdg = x;
}
static void wm_pong(struct wl_client *c, struct wl_resource *r, uint32_t serial) { (void)c; (void)r; (void)serial; }
static const struct xdg_wm_base_interface wm_base_impl = {
    .destroy = wm_destroy, .create_positioner = wm_positioner, .get_xdg_surface = wm_get_xdg_surface, .pong = wm_pong,
};

static void bind_wm_base(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &xdg_wm_base_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &wm_base_impl, NULL, NULL);
}

/* ---- xdg-decoration: server decoration is requested (the theme's frame) ---- */

static void deco_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void deco_mode(struct wl_client *c, struct wl_resource *r, uint32_t mode) {
    (void)c; (void)mode;
    Surf *s = wl_resource_get_user_data(r);
    zxdg_toplevel_decoration_v1_send_configure(r, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    if (s) { s->csd = 0; if (s->sent_configure) send_toplevel_configure(s); }
}
static void deco_unset(struct wl_client *c, struct wl_resource *r) { deco_mode(c, r, 0); }
static const struct zxdg_toplevel_decoration_v1_interface deco_impl = { .destroy = deco_destroy, .set_mode = deco_mode, .unset_mode = deco_unset };
static void deco_gone(struct wl_resource *r) {
    Surf *s = wl_resource_get_user_data(r);
    if (s) s->deco = NULL;
}
static void decm_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void decm_get(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *toplevel) {
    Surf *s = wl_resource_get_user_data(toplevel);
    struct wl_resource *d = wl_resource_create(c, &zxdg_toplevel_decoration_v1_interface, wl_resource_get_version(r), id);
    if (!d) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(d, &deco_impl, s, deco_gone);
    if (s) { s->deco = d; s->csd = 0; }
    zxdg_toplevel_decoration_v1_send_configure(d, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}
static const struct zxdg_decoration_manager_v1_interface decm_impl = { .destroy = decm_destroy, .get_toplevel_decoration = decm_get };
static void bind_decm(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &zxdg_decoration_manager_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &decm_impl, NULL, NULL);
}

/* ---- org_kde_kwin_server_decoration (KDE): the one GTK 3 uses ----
 * GTK 3 does not speak xdg-decoration: if the compositor announces this protocol with «server» mode by
 * default, windows without their own bar (Firefox with the system title bar, GTK 3 apps
 * without a headerbar) ask for the theme's frame. It is a small protocol: the interface tables are
 * written here by hand (the wayland-scanner ones, without the plasma-wayland-protocols XML). */
enum { KDE_DECO_NONE = 0, KDE_DECO_CLIENT = 1, KDE_DECO_SERVER = 2 };
static const struct wl_interface kde_deco_interface;
static const struct wl_interface *kde_types[] = { NULL, &kde_deco_interface, &wl_surface_interface };
static const struct wl_message kde_mgr_requests[] = { { "create", "no", kde_types + 1 } };
static const struct wl_message kde_mgr_events[] = { { "default_mode", "u", kde_types } };
static const struct wl_interface kde_mgr_interface = {
    "org_kde_kwin_server_decoration_manager", 1, 1, kde_mgr_requests, 1, kde_mgr_events,
};
static const struct wl_message kde_deco_requests[] = { { "release", "", kde_types }, { "request_mode", "u", kde_types } };
static const struct wl_message kde_deco_events[] = { { "mode", "u", kde_types } };
static const struct wl_interface kde_deco_interface = {
    "org_kde_kwin_server_decoration", 1, 2, kde_deco_requests, 1, kde_deco_events,
};
struct kde_deco_impl {
    void (*release)(struct wl_client *, struct wl_resource *);
    void (*request_mode)(struct wl_client *, struct wl_resource *, uint32_t);
};
struct kde_mgr_impl { void (*create)(struct wl_client *, struct wl_resource *, uint32_t, struct wl_resource *); };

static void kde_release(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void kde_request_mode(struct wl_client *c, struct wl_resource *r, uint32_t mode) {
    (void)c;
    Surf *s = wl_resource_get_user_data(r);
    int ssd = mode == KDE_DECO_SERVER;
    wl_resource_post_event(r, 0, ssd ? KDE_DECO_SERVER : KDE_DECO_CLIENT);
    if (!s) return;
    s->kde_ssd = ssd;
    if (s->role == R_TOPLEVEL && !s->deco) s->csd = !ssd;
}
static const struct kde_deco_impl kde_deco_impl = { kde_release, kde_request_mode };
static void kde_gone(struct wl_resource *r) {
    Surf *s = wl_resource_get_user_data(r);
    if (s && s->kde == r) s->kde = NULL;
}
static void kde_create(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface) {
    Surf *s = wl_resource_get_user_data(surface);
    struct wl_resource *d = wl_resource_create(c, &kde_deco_interface, wl_resource_get_version(r), id);
    if (!d) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(d, &kde_deco_impl, s, kde_gone);
    if (s) s->kde = d;
    wl_resource_post_event(d, 0, KDE_DECO_SERVER);
}
static const struct kde_mgr_impl kde_mgr_impl = { kde_create };
static void bind_kde_deco(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &kde_mgr_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &kde_mgr_impl, NULL, NULL);
    wl_resource_post_event(r, 0, KDE_DECO_SERVER);
}

/* ---- commit by role ---- */

static void geometry(Surf *s, int *w, int *h) {
    *w = s->has_geom && s->gw > 0 ? s->gw : s->lw;
    *h = s->has_geom && s->gh > 0 ? s->gh : s->lh;
}

static void surface_commit_role(Surf *s) {
    if (s->role == R_TOPLEVEL && s->content && s->lw > 0) {   /* how the app renders for HiDPI: once per change */
        int ratio = s->w * 100 / s->lw;
        if (ratio != s->logged_ratio) {
            s->logged_ratio = ratio;
            fprintf(stderr, "wayland: %s: buffer %dx%d for %dx%d logical (%.2fx; buffer_scale %d, viewport %s, fractional %s, preferred %d%%)\n",
                    s->app_id[0] ? s->app_id : "?", s->w, s->h, s->lw, s->lh, ratio / 100.0, s->bscale, s->viewport ? "yes" : "no",
                    s->frac ? "yes" : "no", s->pref_scale);
        }
    }
    if (s->role == R_TOPLEVEL) {
        if (!s->sent_configure) { send_toplevel_configure(s); return; }     /* first commit: no buffer */
        if (!s->has_geom) { s->gx = s->gy = 0; }
        int w, h;
        geometry(s, &w, &h);
        if (s->content && !s->mapped) {
            s->mapped = 1;
            char t[520], a[260];
            json_str(t, sizeof t, s->title);
            json_str(a, sizeof a, s->app_id);
            emit(s, "map", "{\"title\":\"%s\",\"appId\":\"%s\",\"width\":%d,\"height\":%d,\"csd\":%s}", t, a, w, h, s->csd ? "true" : "false");
            s->conf_w = w;
            s->conf_h = h;
        } else if (!s->content && s->mapped) {
            s->mapped = 0;
            emit(s, "unmap", "{}");
        } else if (s->mapped && (w != s->conf_w || h != s->conf_h)) {
            emit(s, "geometry", "{\"width\":%d,\"height\":%d}", w, h);
            s->conf_w = w;
            s->conf_h = h;
        }
        damage_node(s);
    } else if (s->role == R_POPUP) {
        if (!s->sent_configure) {
            xdg_popup_send_configure(s->popup, s->pop_x, s->pop_y, s->conf_w, s->conf_h);
            xdg_surface_send_configure(s->xdg, wl_display_next_serial(dpy));
            s->sent_configure = 1;
            return;
        }
        if (!s->has_geom) { s->gx = s->gy = 0; }
        if (s->content && !s->mapped && npopups < 32) { s->mapped = 1; popups[npopups++] = s; }
        else if (!s->content && s->mapped) { s->mapped = 0; unmap_popup(s); }
        popup_box_damage(s);
    } else if (s->role == R_SUB) {
        Surf *top = s;
        while (top->parent) top = top->parent;
        if (top->role == R_POPUP) popup_box_damage(top); else damage_node(top);
    }
}

/* ---- drawing ----
 * CPU path: each surface's pixels are copied into the UI canvas.
 * GPU path: the UI leaves a transparent hole where each window goes and the surface is noted;
 * when presenting, their textures are drawn below the UI (layer 0). App menus and
 * fullscreen go on top (layer 1). This keeps the order between LordOS windows and
 * app windows, and app content does not go through the CPU. */

/* "Structural" clip of a node (overflow of its ancestors), without the frame's damage. */
static Rect node_clip(int node) {
    Rect r = { 0, 0, (int)ui.width, (int)ui.height };
    for (Node *n = node ? node_get(node) : NULL; n; n = n->parent) {
        if (!n->s.overflow_hidden) continue;
        int x0 = (int)floorf(n->ax), y0 = (int)floorf(n->ay), x1 = (int)ceilf(n->ax + n->w), y1 = (int)ceilf(n->ay + n->h);
        if (x0 > r.x0) r.x0 = x0;
        if (y0 > r.y0) r.y0 = y0;
        if (x1 < r.x1) r.x1 = x1;
        if (y1 < r.y1) r.y1 = y1;
    }
    return r;
}

static Rect rect_and(Rect a, Rect b) {
    Rect r = { a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1 };
    if (r.x1 < r.x0) r.x1 = r.x0;
    if (r.y1 < r.y0) r.y1 = r.y0;
    return r;
}

static void add_draw(int layer, Surf *s, float x, float y, Rect clip, float op) {
    if (ndraws[layer] < 128) draws[layer][ndraws[layer]++] = (Draw){ s->id, x, y, clip, op };
}

/* Is the node (or an ancestor) a protected window? (screenshots, media/capture.c) */
static int node_protected(int node) {
    for (Node *n = node ? node_get(node) : NULL; n; n = n->parent) if (n->protect) return 1;
    return 0;
}

static Surf *toplevel_of(Surf *s) {
    while (s && s->parent) s = s->parent;
    return s;
}

static int holes_off;                   /* wl_paint_holes(0): only note it (the ui/paint.c layer already has the hole) */
void wl_paint_holes(int on) { holes_off = !on; }

/* The canvas is the display's physical frame (paint_view): logical → physical. */
static inline float PVX(float x) { return (x - paint_view.x) * paint_view.k + paint_view.dx; }
static inline float PVY(float y) { return (y - paint_view.y) * paint_view.k + paint_view.dy; }
static Rect phys_rect(Rect r) {
    return (Rect){ (int)floorf(PVX((float)r.x0)), (int)floorf(PVY((float)r.y0)), (int)ceilf(PVX((float)r.x1)), (int)ceilf(PVY((float)r.y1)) };
}

/* Source rect of a surface's buffer (the viewport crop is in surface coordinates: × buffer scale). */
static void source_rect(const Surf *s, float *sx, float *sy, float *sw, float *sh) {
    *sx = 0; *sy = 0; *sw = (float)s->w; *sh = (float)s->h;
    if (s->viewport && s->vsw > 0) {
        float b = (float)(s->bscale > 0 ? s->bscale : 1);
        *sx = s->vsx * b; *sy = s->vsy * b; *sw = s->vsw * b; *sh = s->vsh * b;
    }
}

/* CPU path: the buffer into its logical box (x, y, lw × lh) at the display's resolution. A client
 * that rendered at exactly that size (fractional-scale) is copied 1:1; others are scaled. */
static void blit_surface(Canvas *c, const Surf *s, float x, float y, float sxk, float syk, float op) {
    float sx, sy, sw, sh;
    source_rect(s, &sx, &sy, &sw, &sh);
    float dx = PVX(x), dy = PVY(y), dw = s->lw * paint_view.k * sxk, dh = s->lh * paint_view.k * syk;
    if (sx == 0 && sy == 0 && sw == (float)s->w && sh == (float)s->h && fabsf(dw - sw) < 0.5f && fabsf(dh - sh) < 0.5f)
        canvas_blit(c, s->px, s->w, s->h, (int)lroundf(dx), (int)lroundf(dy), 0, op, s->opaque);
    else canvas_blit_scaled(c, s->px, s->w, s->w, s->h, sx, sy, sw, sh, dx, dy, dw, dh, op, s->opaque);
}

static void paint_tree(Canvas *c, Surf *s, float x, float y, float op, Rect clip) {
    if (gpu_active() && !paint_capture_mode) {          /* in a screenshot they are always copied */
        if (s->content && holes_off) add_draw(0, s, x, y, clip, op);
        else if (s->content) {
            add_draw(0, s, x, y, clip, op);
            /* hole in the UI: what was painted below shows the window */
            Rect old = c->clip;
            Rect h = rect_and(rect_and(old, phys_rect(clip)),
                              (Rect){ (int)floorf(PVX(x)), (int)floorf(PVY(y)), (int)ceilf(PVX(x + s->lw)), (int)ceilf(PVY(y + s->lh)) });
            canvas_set_clip(c, h);
            canvas_clear(c, 0);
            canvas_set_clip(c, old);
        }
        s->hole_sig = hole_sig(s);
    } else if (s->px) blit_surface(c, s, x, y, 1, 1, op);
    for (int i = 0; i < s->nkids; i++) paint_tree(c, s->kids[i], x + s->kids[i]->sx, y + s->kids[i]->sy, op, clip);
}

void wl_paint_begin(void) { ndraws[0] = ndraws[1] = 0; }

/* What is below a moving window (ui/paint.c) is painted once: its app windows (GPU
 * path) are saved and noted again on every frame. */
static Draw kept[128];
static int nkept;
void wl_paint_keep(void) {
    nkept = ndraws[0];
    memcpy(kept, draws[0], sizeof kept[0] * (size_t)nkept);
}
void wl_paint_kept(void) {
    for (int i = 0; i < nkept && ndraws[0] < 128; i++) draws[0][ndraws[0]++] = kept[i];
}

void wl_paint(Canvas *c, int id, int node, float x, float y, float w, float h, float op) {
    Surf *s = find(id);
    if (!s || s->role != R_TOPLEVEL) return;
    s->node = node;
    int iw = (int)(w + 0.5f), ih = (int)(h + 0.5f);
    /* the theme's frame decides the size: if it changed, the app is asked (except in fullscreen) */
    if (s->mapped && s != fs_surf && iw > 16 && ih > 16 && (iw != s->conf_w || ih != s->conf_h)) {
        s->conf_w = iw;
        s->conf_h = ih;
        send_toplevel_configure(s);
    }
    if (s == fs_surf) return;                   /* presented by the compositor, on top of everything */
    Rect clip = rect_and(node_clip(node), (Rect){ (int)floorf(x), (int)floorf(y), (int)ceilf(x + w), (int)ceilf(y + h) });
    Rect old = canvas_push_clip(c, PVX(x), PVY(y), w * paint_view.k, h * paint_view.k);
    paint_tree(c, s, x - s->gx, y - s->gy, op, clip);
    canvas_set_clip(c, old);
}

void wl_paint_popups(Canvas *c) {
    if (gpu_active() && !paint_capture_mode) return;    /* GPU path: they go in layer 1 */
    for (int i = 0; i < npopups; i++) {
        float x, y;
        if (!origin(popups[i], &x, &y)) continue;
        Surf *top = toplevel_of(popups[i]);
        if (paint_capture_mode && top && node_protected(top->node)) {   /* menu of a protected window */
            canvas_rrect(c, PVX(x), PVY(y), popups[i]->lw * paint_view.k, popups[i]->lh * paint_view.k, 0, 0xff000000u);
            continue;
        }
        paint_tree(c, popups[i], x, y, 1, (Rect){ -65536, -65536, 65536, 65536 });
    }
}

/* Texture with a surface's current content (GPU path). */
static GpuTex *surface_tex(Surf *s) {
    if (s->dmabuf) {
        if (!s->dmabuf->tex) s->dmabuf->tex = gpu_tex_dmabuf(&s->dmabuf->d);
        return s->dmabuf->tex;
    }
    if (s->px && (s->tex_dirty || !s->tex)) {
        s->tex = gpu_tex_pixels(s->tex, s->px, s->w, s->h, s->opaque);
        s->tex_dirty = 0;
    }
    return s->tex;
}

static void draw_surface(Surf *s, float x, float y, float dw, float dh, Rect clip, float op) {
    /* x, y, dw, dh and clip are logical; the GPU draws in the display's physical pixels (paint_view) */
    GpuTex *t = s->content ? surface_tex(s) : NULL;
    if (!t) return;
    float sx, sy, sw, sh;
    source_rect(s, &sx, &sy, &sw, &sh);
    gpu_draw(t, PVX(x), PVY(y), dw * paint_view.k, dh * paint_view.k, sx, sy, sw, sh, phys_rect(clip), op);
}

static void draw_tree(Surf *s, float x, float y, float scale_x, float scale_y, Rect clip) {
    draw_surface(s, x, y, s->lw * scale_x, s->lh * scale_y, clip, 1);
    for (int i = 0; i < s->nkids; i++)
        draw_tree(s->kids[i], x + s->kids[i]->sx * scale_x, y + s->kids[i]->sy * scale_y, scale_x, scale_y, clip);
}

/* The fullscreen window takes the whole output (if its size does not match, it is scaled). */
static void fs_box(float *x, float *y, float *sx, float *sy) {
    int w, h;
    geometry(fs_surf, &w, &h);
    float fx, fy, fw, fh;
    fs_rect(&fx, &fy, &fw, &fh);                /* its display */
    *sx = w > 0 ? fw / w : 1;
    *sy = h > 0 ? fh / h : 1;
    *x = fx - fs_surf->gx * *sx;
    *y = fy - fs_surf->gy * *sy;
}

void wl_gpu_draw_layer(int layer) {
    Rect all = { -65536, -65536, 65536, 65536 };
    if (layer == 0) {
        for (int i = 0; i < ndraws[0]; i++) {
            Draw *d = &draws[0][i];
            Surf *s = find(d->sid);
            if (s && s->content) draw_surface(s, d->x, d->y, (float)s->lw, (float)s->lh, d->clip, d->op);
        }
        return;
    }
    if (fs_surf && fs_surf->content) {
        float x, y, sx, sy;
        fs_box(&x, &y, &sx, &sy);
        draw_tree(fs_surf, x, y, sx, sy, all);
    }
    for (int i = 0; i < npopups; i++) {
        float x, y;
        if (origin(popups[i], &x, &y)) draw_tree(popups[i], x, y, 1, 1, all);
    }
}

/* CPU path: fullscreen and its menus are copied over the already built frame. */
void wl_paint_fullscreen(Canvas *out) {
    if (paint_capture_mode && fs_surf && fs_surf->content && node_protected(fs_surf->node)) {
        canvas_set_clip(out, (Rect){ 0, 0, out->w, out->h });
        canvas_clear(out, 0x000000);                /* protected fullscreen: black in the capture */
        return;
    }
    if (!fs_surf || !fs_surf->content || !fs_surf->px) return;
    float x, y, sx, sy, fx, fy, fw, fh;
    fs_box(&x, &y, &sx, &sy);
    fs_rect(&fx, &fy, &fw, &fh);
    /* only on the display it covers (a mirror shows the same rect) */
    double vx1 = paint_view.x + (out->w - paint_view.dx) / paint_view.k, vy1 = paint_view.y + (out->h - paint_view.dy) / paint_view.k;
    if (fx >= vx1 || fy >= vy1 || fx + fw <= paint_view.x || fy + fh <= paint_view.y) return;
    Rect oc = out->clip;
    canvas_set_clip(out, rect_and(oc, (Rect){ (int)floorf(PVX(fx)), (int)floorf(PVY(fy)), (int)ceilf(PVX(fx + fw)), (int)ceilf(PVY(fy + fh)) }));
    blit_surface(out, fs_surf, x, y, sx, sy, 1);
    canvas_set_clip(out, oc);
    for (int i = 0; i < npopups; i++) {
        float px, py;
        if (origin(popups[i], &px, &py)) paint_tree(out, popups[i], px, py, 1, (Rect){ -65536, -65536, 65536, 65536 });
    }
}

int wl_fullscreen_active(void) { return fs_surf && fs_surf->content; }
int wl_wants_frame(void) { return need_frame; }

/* Direct scanout: the fullscreen window has a dmabuf the size of the output, there are no
 * menus on top and the cursor does not need to be drawn (cursor plane or hidden). */
static void scanout_done(void *data) {
    Dmabuf *d = data;
    d->scanned = 0;
    int current = 0;
    for (int i = 0; i < MAX_SURF && !current; i++) if (surfs[i] && surfs[i]->dmabuf == d) current = 1;
    if (d->res && !current) wl_buffer_send_release(d->res);
    dm_unref(d);
}

int wl_try_scanout(int cursor_free) {
    if (!gpu_active() || !fs_surf || !fs_surf->dmabuf || npopups || fs_surf->nkids || !cursor_free) return 0;
    Dmabuf *d = fs_surf->dmabuf;
    int pw = 0, ph = 0, nused = 0;              /* one display, at its physical size */
    for (int i = 0; i < OUT_MAX; i++) if (oslots[i].used) { nused++; pw = oslots[i].o.pw; ph = oslots[i].o.ph; }
    if (nused != 1 || fs_surf->viewport || fs_surf->gx || fs_surf->gy || !gpu_can_scanout(&d->d, pw, ph)) return 0;
    d->refs++;
    d->scanned = 1;
    if (gpu_scanout(&d->d, scanout_done, d)) { d->scanned = 0; dm_unref(d); return 0; }
    return 1;
}

void wl_frame_done(unsigned t) {
    if (!dpy) return;
    need_frame = 0;
    update_surface_outputs();
    for (int i = 0; i < nrelease; i++) {
        Dmabuf *d = to_release[i];
        if (d->res && !d->scanned) wl_buffer_send_release(d->res);
        dm_unref(d);
    }
    nrelease = 0;
    for (int i = 0; i < MAX_SURF; i++) {
        Surf *s = surfs[i];
        if (!s) continue;
        struct wl_resource *cb, *tmp;
        wl_resource_for_each_safe(cb, tmp, &s->frames) {
            wl_callback_send_done(cb, t);
            wl_resource_destroy(cb);
        }
        wl_list_insert_list(s->sent_feedback.prev, &s->feedback);
        wl_list_init(&s->feedback);
    }
    wl_display_flush_clients(dpy);
}

/* ---- input ---- */

static Surf *surface_at(Surf *s, float lx, float ly, float *ox, float *oy) {
    for (int i = s->nkids - 1; i >= 0; i--) {
        Surf *k = s->kids[i];
        Surf *hit = surface_at(k, lx - k->sx, ly - k->sy, ox, oy);
        if (hit) return hit;
    }
    if (s->content && lx >= 0 && ly >= 0 && lx < s->lw && ly < s->lh) { *ox = lx; *oy = ly; return s; }
    return NULL;
}

static void constraint_update(void);
static void pointer_enter(Surf *s, float lx, float ly) {
    if (pointer_focus == s) return;
    if (!s || !pointer_focus || s->client != pointer_focus->client) { cursor_surf = NULL; cursor_mode = 0; cursor_gen++; }
    struct wl_resource *p;
    if (pointer_focus) {
        FOR_CLIENT(&pointers, pointer_focus->client, p) {
            wl_pointer_send_leave(p, wl_display_next_serial(dpy), pointer_focus->res);
            if (wl_resource_get_version(p) >= 5) wl_pointer_send_frame(p);
        }
    }
    pointer_focus = s;
    if (s) {
        FOR_CLIENT(&pointers, s->client, p) {
            wl_pointer_send_enter(p, wl_display_next_serial(dpy), s->res, wl_fixed_from_double(lx), wl_fixed_from_double(ly));
            if (wl_resource_get_version(p) >= 5) wl_pointer_send_frame(p);
        }
    }
    constraint_update();
}

static void dismiss_popups(void) {
    for (int i = npopups - 1; i >= 0; i--) if (popups[i]->popup) xdg_popup_send_popup_done(popups[i]->popup);
}

int wl_pointer(float x, float y, Node *hit, int moved, int nbuttons, const int *down, const int *button, int wheel) {
    if (!dpy) return 0;
    last_x = x;
    last_y = y;
    Surf *target = NULL;
    float lx = 0, ly = 0;
    if (buttons_held && pointer_focus) {        /* drag: stays on the same surface */
        float ox, oy;
        target = pointer_focus;
        if (origin(target, &ox, &oy)) { lx = x - ox; ly = y - oy; }
    } else {
        for (int i = npopups - 1; i >= 0 && !target; i--) {
            float ox, oy;
            if (origin(popups[i], &ox, &oy)) target = surface_at(popups[i], x - ox, y - oy, &lx, &ly);
        }
        if (!target && fs_surf && fs_surf->content) {
            float ox, oy;
            if (origin(fs_surf, &ox, &oy)) target = surface_at(fs_surf, x - ox, y - oy, &lx, &ly);
        } else if (!target && hit && hit->type == NODE_SURFACE) {
            Surf *s = find(hit->term);
            float ox, oy;
            if (s && origin(s, &ox, &oy)) target = surface_at(s, x - ox, y - oy, &lx, &ly);
        }
    }
    for (int i = 0; i < nbuttons; i++)          /* click outside the menus: they close */
        if (down[i] && npopups && (!target || target->role != R_POPUP)) {
            Surf *t = target;
            while (t && t->role == R_SUB) t = t->parent;
            if (!t || t->role != R_POPUP) dismiss_popups();
        }
    pointer_enter(target, lx, ly);
    if (!target) return 0;
    unsigned t = now_ms();
    struct wl_resource *p;
    FOR_CLIENT(&pointers, target->client, p) {
        if (moved && !(constraint && constraint->active && constraint->lock))
            wl_pointer_send_motion(p, t, wl_fixed_from_double(lx), wl_fixed_from_double(ly));
        for (int i = 0; i < nbuttons; i++) {
            uint32_t b = button[i] == 0 ? BTN_LEFT : button[i] == 1 ? BTN_MIDDLE : BTN_RIGHT;
            wl_pointer_send_button(p, wl_display_next_serial(dpy), t, b,
                                   down[i] ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        }
        if (wheel) wl_input_wheel(p, t, wheel);      /* axis + axis_source wheel + value120 (wl_input.c) */
        if (wl_resource_get_version(p) >= 5) wl_pointer_send_frame(p);
    }
    for (int i = 0; i < nbuttons; i++) {
        buttons_held += down[i] ? 1 : -1;
        if (down[i]) { press_x = x; press_y = y; }
    }
    if (buttons_held < 0) buttons_held = 0;
    Surf *top = target;
    while (top && top->role == R_SUB) top = top->parent;
    if (fs_surf && fs_surf->content) return 2;  /* fullscreen: the UI does not see the pointer */
    return top && top->role == R_POPUP ? 2 : 1;
}

void wl_pointer_grab(int nbuttons, const int *down) {
    if (!dpy) return;
    pointer_enter(NULL, 0, 0);
    for (int i = 0; i < nbuttons; i++) buttons_held += down[i] ? 1 : -1;
    if (buttons_held < 0) buttons_held = 0;
}

/* Pastes into the Linux app with the keyboard: a synthetic Ctrl+V only for that app (the LordOS
 * clipboard panel uses it after copying something again). Control is XKB core
 * modifier 2 in all keymaps. 1 if there was an app with the keyboard. */
int wl_has_key_focus(void) { return dpy && key_focus; }

int wl_send_paste(void) {
    if (!dpy || !key_focus) return 0;
    unsigned t = now_ms();
    uint32_t d, l, lk, g;
    kb_mods(&d, &l, &lk, &g);
    struct wl_resource *kb;
    FOR_CLIENT(&keyboards, key_focus->client, kb) {
        wl_keyboard_send_modifiers(kb, wl_display_next_serial(dpy), d | (1u << 2), l, lk, g);
        wl_keyboard_send_key(kb, wl_display_next_serial(dpy), t, KEY_V, WL_KEYBOARD_KEY_STATE_PRESSED);
        wl_keyboard_send_key(kb, wl_display_next_serial(dpy), t + 1, KEY_V, WL_KEYBOARD_KEY_STATE_RELEASED);
        wl_keyboard_send_modifiers(kb, wl_display_next_serial(dpy), d, l, lk, g);
    }
    mods_sent[0] = d; mods_sent[1] = l; mods_sent[2] = lk; mods_sent[3] = g;
    return 1;
}

int wl_key(int code, int value, const KeyInfo *k) {
    if (!dpy) return 0;
    if (k->meta && code == KEY_ESC && fs_surf) { if (value == 1) leave_fullscreen(fs_surf); return 1; }   /* ❖Esc exits */
    if (fs_surf && fs_surf->content && key_focus != fs_surf) wl_set_focus(fs_surf->id);
    if (!key_focus) return 0;
    if (k->meta) return 0;                      /* ❖ + key: LordOS shortcuts */
    /* media keys and Print Screen: always LordOS's (Player, Screenshots) */
    if (code == KEY_PLAYPAUSE || code == KEY_NEXTSONG || code == KEY_PREVIOUSSONG || code == KEY_STOPCD || code == KEY_PLAYCD ||
        code == KEY_PAUSECD || code == KEY_SYSRQ) return 0;
    if (value == 2) return 1;                   /* apps repeat on their own (repeat_info) */
    unsigned t = now_ms();
    uint32_t d, l, lk, g;
    kb_mods(&d, &l, &lk, &g);
    struct wl_resource *kb;
    FOR_CLIENT(&keyboards, key_focus->client, kb) {
        wl_keyboard_send_key(kb, wl_display_next_serial(dpy), t, (uint32_t)code,
                             value ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
        if (d != mods_sent[0] || l != mods_sent[1] || lk != mods_sent[2] || g != mods_sent[3])
            wl_keyboard_send_modifiers(kb, wl_display_next_serial(dpy), d, l, lk, g);
    }
    mods_sent[0] = d; mods_sent[1] = l; mods_sent[2] = lk; mods_sent[3] = g;
    return 1;
}

/* ---- zwp_linux_dmabuf_v1 (v4): apps' GPU buffers ---- */

typedef struct { int fd[4]; uint32_t off[4], stride[4]; uint64_t mod; int set[4], used; } Params;

static void dmabuf_buffer_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static const struct wl_buffer_interface dmabuf_buffer_impl = { .destroy = dmabuf_buffer_destroy };
static void dmabuf_buffer_gone(struct wl_resource *r) {
    Dmabuf *d = wl_resource_get_user_data(r);
    if (!d) return;
    d->res = NULL;
    dm_unref(d);
}

static void params_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void params_gone(struct wl_resource *r) {
    Params *p = wl_resource_get_user_data(r);
    if (!p) return;
    for (int i = 0; i < 4; i++) if (p->set[i]) close(p->fd[i]);
    free(p);
}

static void params_add(struct wl_client *c, struct wl_resource *r, int32_t fd, uint32_t idx, uint32_t off, uint32_t stride,
                       uint32_t mod_hi, uint32_t mod_lo) {
    (void)c;
    Params *p = wl_resource_get_user_data(r);
    if (p->used) { close(fd); wl_resource_post_error(r, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED, "already used"); return; }
    if (idx >= 4) { close(fd); wl_resource_post_error(r, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX, "invalid plane"); return; }
    if (p->set[idx]) { close(fd); wl_resource_post_error(r, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET, "repeated plane"); return; }
    p->fd[idx] = fd;
    p->off[idx] = off;
    p->stride[idx] = stride;
    p->mod = (uint64_t)mod_hi << 32 | mod_lo;
    p->set[idx] = 1;
}

/* Can we show it? On GPU, it must import as an EGLImage; on CPU, it must be linear and mappable. */
static int dmabuf_usable(Dmabuf *d) {
    if (gpu_active() && (d->tex = gpu_tex_dmabuf(&d->d)) != NULL) return 1;
    /* no GPU, or a dmabuf EGL does not import (for example udmabuf with a driver that does not
     * accept it): if it is linear it is mapped and copied, as in the CPU path */
    const GpuDmabuf *g = &d->d;
    d->cpu = g->nplanes == 1 && (g->modifier == DRM_FORMAT_MOD_LINEAR || g->modifier == DRM_FORMAT_MOD_INVALID) &&
             (g->fourcc == DRM_FORMAT_ARGB8888 || g->fourcc == DRM_FORMAT_XRGB8888);
    return d->cpu;
}

static void params_create_common(struct wl_client *c, struct wl_resource *r, uint32_t buffer_id, int32_t w, int32_t h,
                                 uint32_t format, uint32_t flags) {
    Params *p = wl_resource_get_user_data(r);
    if (p->used) { wl_resource_post_error(r, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED, "already used"); return; }
    p->used = 1;
    int n = 0;
    while (n < 4 && p->set[n]) n++;
    for (int i = n; i < 4; i++)
        if (p->set[i]) { wl_resource_post_error(r, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "skipped planes"); return; }
    if (!n || w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        wl_resource_post_error(r, n ? ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS : ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "invalid buffer");
        return;
    }
    Dmabuf *d = calloc(1, sizeof *d);
    if (!d) { wl_client_post_no_memory(c); return; }
    d->d = (GpuDmabuf){ .width = w, .height = h, .nplanes = n, .fourcc = format, .modifier = p->mod };
    for (int i = 0; i < 4; i++) d->d.fd[i] = -1;
    for (int i = 0; i < n; i++) {
        d->d.fd[i] = p->fd[i];
        d->d.offset[i] = p->off[i];
        d->d.stride[i] = p->stride[i];
        p->set[i] = 0;                          /* now they belong to the Dmabuf */
    }
    d->refs = 1;
    if (flags || !dmabuf_usable(d)) {
        dm_unref(d);
        if (buffer_id) wl_resource_post_error(r, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER, "could not import");
        else zwp_linux_buffer_params_v1_send_failed(r);
        return;
    }
    struct wl_resource *b = wl_resource_create(c, &wl_buffer_interface, 1, buffer_id);
    if (!b) { dm_unref(d); wl_client_post_no_memory(c); return; }
    d->res = b;
    wl_resource_set_implementation(b, &dmabuf_buffer_impl, d, dmabuf_buffer_gone);
    if (!buffer_id) zwp_linux_buffer_params_v1_send_created(r, b);
}

static void params_create(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h, uint32_t format, uint32_t flags) {
    params_create_common(c, r, 0, w, h, format, flags);
}
static void params_create_immed(struct wl_client *c, struct wl_resource *r, uint32_t id, int32_t w, int32_t h, uint32_t format, uint32_t flags) {
    params_create_common(c, r, id, w, h, format, flags);
}
static const struct zwp_linux_buffer_params_v1_interface params_impl = {
    .destroy = params_destroy, .add = params_add, .create = params_create, .create_immed = params_create_immed,
};

/* Advertised formats: those EGL imports (GPU) or the linear ones that can be mapped (CPU). */
static int dmabuf_formats(GpuFormat *out, int max) {
    int n = gpu_active() ? gpu_formats(out, max) : 0;
    if (n) return n;
    static const uint32_t f[] = { DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888 };
    for (int i = 0; i < 2 && n + 1 < max; i++) {
        out[n++] = (GpuFormat){ f[i], DRM_FORMAT_MOD_LINEAR };
        out[n++] = (GpuFormat){ f[i], DRM_FORMAT_MOD_INVALID };
    }
    return n;
}

static void feedback_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static const struct zwp_linux_dmabuf_feedback_v1_interface feedback_impl = { .destroy = feedback_destroy };

static void send_feedback(struct wl_resource *fb) {
    GpuFormat f[256];
    int n = dmabuf_formats(f, 256);
    size_t size = (size_t)n * 16;
    int fd = memfd_create("lord-dmabuf-formats", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0 || ftruncate(fd, (off_t)size)) { if (fd >= 0) close(fd); return; }
    uint8_t *m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { close(fd); return; }
    for (int i = 0; i < n; i++) {
        memcpy(m + i * 16, &f[i].fourcc, 4);
        memset(m + i * 16 + 4, 0, 4);
        memcpy(m + i * 16 + 8, &f[i].modifier, 8);
    }
    munmap(m, size);
    fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
    zwp_linux_dmabuf_feedback_v1_send_format_table(fb, fd, (uint32_t)size);
    close(fd);
    dev_t dev = drm_dev;
    if (gpu_active()) gpu_device(&dev);
    struct wl_array devs, idx;
    wl_array_init(&devs);
    dev_t *dp = wl_array_add(&devs, sizeof dev);
    if (dp) *dp = dev;
    zwp_linux_dmabuf_feedback_v1_send_main_device(fb, &devs);
    zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(fb, &devs);
    wl_array_init(&idx);
    for (int i = 0; i < n; i++) { uint16_t *ip = wl_array_add(&idx, sizeof *ip); if (ip) *ip = (uint16_t)i; }
    zwp_linux_dmabuf_feedback_v1_send_tranche_formats(fb, &idx);
    zwp_linux_dmabuf_feedback_v1_send_tranche_flags(fb, 0);
    zwp_linux_dmabuf_feedback_v1_send_tranche_done(fb);
    zwp_linux_dmabuf_feedback_v1_send_done(fb);
    wl_array_release(&devs);
    wl_array_release(&idx);
}

static void dmabuf_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void dmabuf_create_params(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    Params *p = calloc(1, sizeof *p);
    struct wl_resource *res = p ? wl_resource_create(c, &zwp_linux_buffer_params_v1_interface, wl_resource_get_version(r), id) : NULL;
    if (!res) { free(p); wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(res, &params_impl, p, params_gone);
}
static void dmabuf_get_feedback(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    struct wl_resource *fb = wl_resource_create(c, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(r), id);
    if (!fb) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(fb, &feedback_impl, NULL, NULL);
    send_feedback(fb);
}
static void dmabuf_get_surface_feedback(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface) {
    (void)surface;
    dmabuf_get_feedback(c, r, id);
}
static const struct zwp_linux_dmabuf_v1_interface dmabuf_impl = {
    .destroy = dmabuf_destroy, .create_params = dmabuf_create_params,
    .get_default_feedback = dmabuf_get_feedback, .get_surface_feedback = dmabuf_get_surface_feedback,
};

static void bind_dmabuf(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &zwp_linux_dmabuf_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &dmabuf_impl, NULL, NULL);
    if (ver >= 4) return;                       /* v4: formats go through the feedback */
    GpuFormat f[256];
    int n = dmabuf_formats(f, 256);
    for (int i = 0; i < n; i++) {
        int seen = 0;
        for (int j = 0; j < i; j++) if (f[j].fourcc == f[i].fourcc) seen = 1;
        if (ver < 3) { if (!seen) zwp_linux_dmabuf_v1_send_format(r, f[i].fourcc); }
        else zwp_linux_dmabuf_v1_send_modifier(r, f[i].fourcc, (uint32_t)(f[i].modifier >> 32), (uint32_t)f[i].modifier);
    }
}

void wl_set_drm_device(dev_t dev) { drm_dev = dev; }

/* ---- wp_presentation: when each frame was seen ---- */

static void pres_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void feedback_link_gone(struct wl_resource *r) { wl_list_remove(wl_resource_get_link(r)); }
static void pres_feedback(struct wl_client *c, struct wl_resource *r, struct wl_resource *surface, uint32_t id) {
    (void)r;
    Surf *s = wl_resource_get_user_data(surface);
    struct wl_resource *fb = wl_resource_create(c, &wp_presentation_feedback_interface, 1, id);
    if (!fb) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(fb, NULL, NULL, feedback_link_gone);
    wl_list_insert(s->p_feedback.prev, wl_resource_get_link(fb));
}
static const struct wp_presentation_interface presentation_impl = { .destroy = pres_destroy, .feedback = pres_feedback };
static void bind_presentation(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &wp_presentation_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &presentation_impl, NULL, NULL);
    wp_presentation_send_clock_id(r, CLOCK_MONOTONIC);
}

/* The flip finished (main.c, with the KMS time and vblank number). */
void wl_presented(uint64_t ns, unsigned seq, unsigned refresh_ns) {
    if (!dpy) return;
    for (int i = 0; i < MAX_SURF; i++) {
        Surf *s = surfs[i];
        if (!s) continue;
        struct wl_resource *fb, *tmp;
        wl_resource_for_each_safe(fb, tmp, &s->sent_feedback) {
            struct wl_resource *o;
            for (int k = 0; k < OUT_MAX; k++)          /* the display it is on (the main one if none) */
                if (oslots[k].used && ((s->outmask >> k & 1) || (!s->outmask && k == out_main)))
                    FOR_CLIENT(&oslots[k].res, s->client, o) wp_presentation_feedback_send_sync_output(fb, o);
            uint64_t sec = ns / 1000000000ull;
            wp_presentation_feedback_send_presented(fb, (uint32_t)(sec >> 32), (uint32_t)sec, (uint32_t)(ns % 1000000000ull),
                                                    refresh_ns, 0, seq,
                                                    WP_PRESENTATION_FEEDBACK_KIND_VSYNC | WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK |
                                                    WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION);
            wl_resource_destroy(fb);
        }
    }
}

/* ---- wp_viewporter: crop and scale of a surface ---- */

static void vp_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void vp_gone(struct wl_resource *r) {
    Surf *s = wl_resource_get_user_data(r);
    if (!s) return;
    s->viewport = NULL;
    s->p_vsw = -1;
    s->p_vdw = s->p_vdh = 0;
    s->p_vp = 1;
}
static void vp_source(struct wl_client *c, struct wl_resource *r, wl_fixed_t x, wl_fixed_t y, wl_fixed_t w, wl_fixed_t h) {
    (void)c;
    Surf *s = wl_resource_get_user_data(r);
    if (!s) { wl_resource_post_error(r, WP_VIEWPORT_ERROR_NO_SURFACE, "no surface"); return; }
    if (w == wl_fixed_from_int(-1)) { s->p_vsw = -1; s->p_vp = 1; return; }
    if (x < 0 || y < 0 || w <= 0 || h <= 0) { wl_resource_post_error(r, WP_VIEWPORT_ERROR_BAD_VALUE, "invalid source rectangle"); return; }
    s->p_vsx = (float)wl_fixed_to_double(x);
    s->p_vsy = (float)wl_fixed_to_double(y);
    s->p_vsw = (float)wl_fixed_to_double(w);
    s->p_vsh = (float)wl_fixed_to_double(h);
    s->p_vp = 1;
}
static void vp_destination(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h) {
    (void)c;
    Surf *s = wl_resource_get_user_data(r);
    if (!s) { wl_resource_post_error(r, WP_VIEWPORT_ERROR_NO_SURFACE, "no surface"); return; }
    if ((w == -1 && h == -1)) { s->p_vdw = s->p_vdh = 0; s->p_vp = 1; return; }
    if (w <= 0 || h <= 0) { wl_resource_post_error(r, WP_VIEWPORT_ERROR_BAD_VALUE, "invalid destination"); return; }
    s->p_vdw = w;
    s->p_vdh = h;
    s->p_vp = 1;
}
static const struct wp_viewport_interface viewport_impl = { .destroy = vp_destroy, .set_source = vp_source, .set_destination = vp_destination };
static void vpr_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void vpr_get(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface) {
    Surf *s = wl_resource_get_user_data(surface);
    if (s->viewport) { wl_resource_post_error(r, WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS, "already has a viewport"); return; }
    struct wl_resource *v = wl_resource_create(c, &wp_viewport_interface, 1, id);
    if (!v) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(v, &viewport_impl, s, vp_gone);
    s->viewport = v;
}
static const struct wp_viewporter_interface viewporter_impl = { .destroy = vpr_destroy, .get_viewport = vpr_get };
static void bind_viewporter(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &wp_viewporter_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &viewporter_impl, NULL, NULL);
}

/* ---- zwp_relative_pointer_v1 and zwp_pointer_constraints_v1 (games with a captured mouse) ---- */

static void relp_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static const struct zwp_relative_pointer_v1_interface relp_impl = { .destroy = relp_destroy };
static void relm_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void relm_get(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *pointer) {
    (void)pointer;
    struct wl_resource *rp = wl_resource_create(c, &zwp_relative_pointer_v1_interface, wl_resource_get_version(r), id);
    if (!rp) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(rp, &relp_impl, NULL, link_gone);
    wl_list_insert(&rel_pointers, wl_resource_get_link(rp));
}
static const struct zwp_relative_pointer_manager_v1_interface relm_impl = { .destroy = relm_destroy, .get_relative_pointer = relm_get };
static void bind_relm(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &zwp_relative_pointer_manager_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &relm_impl, NULL, NULL);
}

/* Relative motion (no acceleration or edges): main.c sends it with every motion. */
void wl_pointer_relative(double dx, double dy) {
    if (!dpy || !pointer_focus || (dx == 0 && dy == 0)) return;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    uint64_t us = (uint64_t)t.tv_sec * 1000000ull + (uint64_t)t.tv_nsec / 1000;
    struct wl_resource *rp, *p;
    int sent = 0;
    FOR_CLIENT(&rel_pointers, pointer_focus->client, rp) {
        zwp_relative_pointer_v1_send_relative_motion(rp, (uint32_t)(us >> 32), (uint32_t)us, wl_fixed_from_double(dx),
                                                     wl_fixed_from_double(dy), wl_fixed_from_double(dx), wl_fixed_from_double(dy));
        sent = 1;
    }
    if (sent) FOR_CLIENT(&pointers, pointer_focus->client, p) if (wl_resource_get_version(p) >= 5) wl_pointer_send_frame(p);
}

static void constraint_send(Constraint *k, int on) {
    if (!k->res) return;
    if (k->lock) { if (on) zwp_locked_pointer_v1_send_locked(k->res); else zwp_locked_pointer_v1_send_unlocked(k->res); }
    else { if (on) zwp_confined_pointer_v1_send_confined(k->res); else zwp_confined_pointer_v1_send_unconfined(k->res); }
}

/* Activated when the pointer enters its surface and released when it leaves. */
static void constraint_update(void) {
    Constraint *k = constraint;
    if (!k || !k->surf) return;
    Surf *top = pointer_focus;
    int want = top && (top == k->surf || top->parent == k->surf);
    if (want && !k->active && k->oneshot >= 0) { k->active = 1; constraint_send(k, 1); cursor_gen++; }
    else if (!want && k->active) {
        k->active = 0;
        constraint_send(k, 0);
        if (k->oneshot) k->oneshot = -1;          /* oneshot: does not activate again */
        cursor_gen++;
    }
}

static void cons_gone(struct wl_resource *r) {
    Constraint *k = wl_resource_get_user_data(r);
    if (!k) return;
    if (constraint == k) constraint = NULL;
    cursor_gen++;
    free(k);
}
static void cons_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void cons_hint(struct wl_client *c, struct wl_resource *r, wl_fixed_t x, wl_fixed_t y) { (void)c; (void)r; (void)x; (void)y; }
static void cons_region(struct wl_client *c, struct wl_resource *r, struct wl_resource *region) { (void)c; (void)r; (void)region; }
static const struct zwp_locked_pointer_v1_interface locked_impl = { .destroy = cons_destroy, .set_cursor_position_hint = cons_hint, .set_region = cons_region };
static const struct zwp_confined_pointer_v1_interface confined_impl = { .destroy = cons_destroy, .set_region = cons_region };

static void cons_new(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface, uint32_t lifetime, int lock) {
    Surf *s = wl_resource_get_user_data(surface);
    if (constraint && constraint->surf == s) { wl_resource_post_error(r, ZWP_POINTER_CONSTRAINTS_V1_ERROR_ALREADY_CONSTRAINED, "already constrained"); return; }
    Constraint *k = calloc(1, sizeof *k);
    struct wl_resource *res = k ? wl_resource_create(c, lock ? &zwp_locked_pointer_v1_interface : &zwp_confined_pointer_v1_interface, 1, id) : NULL;
    if (!res) { free(k); wl_client_post_no_memory(c); return; }
    k->res = res;
    k->surf = s;
    k->lock = lock;
    k->oneshot = lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT;
    if (lock) wl_resource_set_implementation(res, &locked_impl, k, cons_gone);
    else wl_resource_set_implementation(res, &confined_impl, k, cons_gone);
    if (constraint) { if (constraint->active) constraint_send(constraint, 0); constraint->surf = NULL; }
    constraint = k;
    constraint_update();
}
static void cons_lock(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface, struct wl_resource *pointer,
                      struct wl_resource *region, uint32_t lifetime) {
    (void)pointer; (void)region;
    cons_new(c, r, id, surface, lifetime, 1);
}
static void cons_confine(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface, struct wl_resource *pointer,
                         struct wl_resource *region, uint32_t lifetime) {
    (void)pointer; (void)region;
    cons_new(c, r, id, surface, lifetime, 0);
}
static void consm_destroy(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static const struct zwp_pointer_constraints_v1_interface consm_impl = { .destroy = consm_destroy, .lock_pointer = cons_lock, .confine_pointer = cons_confine };
static void bind_consm(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(c, &zwp_pointer_constraints_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &consm_impl, NULL, NULL);
}

/* For main.c: pointer locked (does not move) or confined to the surface (clipped). */
int wl_pointer_locked(void) { return constraint && constraint->active && constraint->lock; }
void wl_pointer_confine(int *x, int *y) {
    if (!constraint || !constraint->active || constraint->lock || !constraint->surf) return;
    float ox, oy;
    Surf *s = constraint->surf;
    if (!origin(s, &ox, &oy)) return;
    int x0 = (int)ox, y0 = (int)oy, x1 = x0 + s->lw - 1, y1 = y0 + s->lh - 1;
    if (*x < x0) *x = x0;
    if (*y < y0) *y = y0;
    if (*x > x1) *x = x1;
    if (*y > y1) *y = y1;
}

/* Cursor requested by the app with the pointer: 0 = LordOS arrow, 1 = image, 2 = hidden. */
void wl_set_pointer_scale(int pct) { if (pct != pointer_scale) { pointer_scale = pct; cursor_gen++; } }

int wl_cursor(const uint32_t **px, int *w, int *h, int *hx, int *hy, unsigned *gen) {
    *gen = cursor_gen;
    if (wl_pointer_locked()) return 2;
    if (cursor_mode == 1 && cursor_surf && cursor_surf->px) {
        *px = cursor_surf->px;
        *w = cursor_surf->w;
        *h = cursor_surf->h;
        *hx = cursor_surf->cursor_hx;
        *hy = cursor_surf->cursor_hy;
        return 1;
    }
    if (cursor_mode == 3) {             /* cursor-shape-v1: an own shape */
        const CursorImage *ci = cursor_image_pct(cursor_from_wayland_shape(cursor_shape), pointer_scale);
        if (ci) { *px = ci->px; *w = ci->w; *h = ci->h; *hx = ci->hx; *hy = ci->hy; return 1; }
    }
    return cursor_mode == 2 ? 2 : 0;
}

/* ---- from JS ---- */

void wl_set_focus(int id) {
    if (!dpy) return;
    Surf *s = id ? find(id) : NULL;
    if (s && s->role != R_TOPLEVEL) s = NULL;
    if (fs_surf && fs_surf->content) s = fs_surf;      /* in fullscreen the keyboard is its own */
    if (s == key_focus) return;
    struct wl_resource *kb;
    if (key_focus) {
        FOR_CLIENT(&keyboards, key_focus->client, kb) wl_keyboard_send_leave(kb, wl_display_next_serial(dpy), key_focus->res);
        key_focus->activated = 0;
        send_toplevel_configure(key_focus);
        dismiss_popups();
    }
    key_focus = s;
    if (s) {
        FOR_CLIENT(&keyboards, s->client, kb) send_enter_keyboard(kb, s);
        s->activated = 1;
        send_toplevel_configure(s);
        offer_to_focus();
        clip_seen = term_clipboard_gen();
    }
    wl_display_flush_clients(dpy);
}

void wl_close(int id) {
    Surf *s = find(id);
    if (s && s->top) xdg_toplevel_send_close(s->top);
    if (dpy) wl_display_flush_clients(dpy);
}

void wl_set_state(int id, int maximized, int fullscreen) {
    Surf *s = find(id);
    (void)fullscreen;                           /* fullscreen is handled by the compositor */
    if (!s || !s->top || s->maximized == maximized) return;
    s->maximized = maximized;
    send_toplevel_configure(s);
}

/* The screen changed size (headless session, docs/SESSIONS.md): new mode for the apps
 * and the fullscreen one adapts. */
void wl_output_resized(void) {
    if (!dpy) return;
    int w = (int)ui.width, h = (int)ui.height;
    WlOutput o = { 0, 0, w, h, w, h, 60000, 100, w * 254 / 960, h * 254 / 960, "LORD-1", "LordOS display" };
    wl_set_outputs(&o, 1, 0);
}

/* The fullscreen window follows its display (resized, or unplugged: the main one). */
static void fs_follow_outputs(void) {
    if (!fs_surf) return;
    if (fs_slot < 0 || fs_slot >= OUT_MAX || !oslots[fs_slot].used) fs_slot = out_main;
    float fx, fy, fw, fh;
    fs_rect(&fx, &fy, &fw, &fh);
    if ((int)fw == fs_surf->conf_w && (int)fh == fs_surf->conf_h) return;
    fs_surf->conf_w = (int)fw;
    fs_surf->conf_h = (int)fh;
    send_toplevel_configure(fs_surf);
    damage_add((Box){ 0, 0, ui.width, ui.height });
    ui.dirty = 1;
}

int wl_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_SURF; i++) if (surfs[i] && surfs[i]->role == R_TOPLEVEL && surfs[i]->mapped) n++;
    return n;
}

/* ---- startup and loop ---- */

int wl_start(void) {
    if (!getenv("XDG_RUNTIME_DIR")) {
        char dir[64];
        snprintf(dir, sizeof dir, "/run/user/%d", (int)getuid());
        if (access(dir, W_OK)) { fprintf(stderr, "wayland: no XDG_RUNTIME_DIR\n"); return -1; }
        setenv("XDG_RUNTIME_DIR", dir, 1);
    }
    dpy = wl_display_create();
    if (!dpy) return -1;
    const char *name = wl_display_add_socket_auto(dpy);
    if (!name) { wl_display_destroy(dpy); dpy = NULL; fprintf(stderr, "wayland: could not create the socket\n"); return -1; }
    snprintf(socket_name, sizeof socket_name, "%s", name);
    loop = wl_display_get_event_loop(dpy);
    wl_list_init(&pointers);
    wl_list_init(&keyboards);
    wl_list_init(&data_devices);
    wl_list_init(&outputs);
    wl_display_init_shm(dpy);
    wl_global_create(dpy, &wl_compositor_interface, 6, NULL, bind_compositor);
    wl_global_create(dpy, &wl_subcompositor_interface, 1, NULL, bind_subcompositor);
    wl_global_create(dpy, &zxdg_output_manager_v1_interface, 3, NULL, bind_xdg_output_mgr);
    wl_global_create(dpy, &wp_fractional_scale_manager_v1_interface, 1, NULL, bind_frac_mgr);
    wl_global_create(dpy, &wl_seat_interface, 9, NULL, bind_seat);    /* 8: axis_value120, 9: relative_direction */
    wl_global_create(dpy, &wl_data_device_manager_interface, 3, NULL, bind_ddm);
    wl_global_create(dpy, &xdg_wm_base_interface, 6, NULL, bind_wm_base);
    wl_global_create(dpy, &zxdg_decoration_manager_v1_interface, 1, NULL, bind_decm);
    wl_list_init(&rel_pointers);
    wl_global_create(dpy, &zwp_linux_dmabuf_v1_interface, 4, NULL, bind_dmabuf);
    wl_global_create(dpy, &wp_presentation_interface, 1, NULL, bind_presentation);
    wl_global_create(dpy, &wp_viewporter_interface, 1, NULL, bind_viewporter);
    wl_global_create(dpy, &zwp_relative_pointer_manager_v1_interface, 1, NULL, bind_relm);
    wl_global_create(dpy, &zwp_pointer_constraints_v1_interface, 1, NULL, bind_consm);
    wl_global_create(dpy, &wp_cursor_shape_manager_v1_interface, 1, NULL, bind_csm);   /* cursors by name */
    wl_global_create(dpy, &kde_mgr_interface, 1, NULL, bind_kde_deco);   /* theme frame for GTK 3 (Firefox) */
    wl_input_globals(dpy);              /* zwp_pointer_gestures_v1 (wl_input.c) */
    /* inherited by apps opened from the LordOS terminal */
    setenv("WAYLAND_DISPLAY", socket_name, 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_CURRENT_DESKTOP", "LordOS", 1);
    setenv("GDK_BACKEND", "wayland", 1);
    setenv("QT_QPA_PLATFORM", "wayland", 1);
    setenv("MOZ_ENABLE_WAYLAND", "1", 1);
    clip_seen = term_clipboard_gen();
    fprintf(stderr, "wayland: %s/%s\n", getenv("XDG_RUNTIME_DIR"), socket_name);
    return 0;
}

void wl_stop(void) {
    if (!dpy) return;
    wl_display_destroy_clients(dpy);
    wl_display_destroy(dpy);
    dpy = NULL;
}

int wl_fd(void) { return dpy ? wl_event_loop_get_fd(loop) : -1; }
const char *wl_socket(void) { return dpy ? socket_name : ""; }

void wl_dispatch(void) { if (dpy) wl_event_loop_dispatch(loop, 0); }

void wl_flush(void) {
    if (!dpy) return;
    /* LordOS copied something (terminal, text field): the focused app sees it as the selection */
    unsigned g = term_clipboard_gen();
    if (g != clip_seen) {
        clip_seen = g;
        clip_is_image = 0;
        if (selection) { wl_data_source_send_cancelled(selection->res); selection = NULL; }
        offer_to_focus();
    }
    wl_display_flush_clients(dpy);
}

/* ---- for wl_input.c (gestures, fine scrolling and touches) ---- */
struct wl_display *wl__display(void) { return dpy; }
struct wl_resource *wl__pointer_focus(void) { return dpy && pointer_focus ? pointer_focus->res : NULL; }
struct wl_list *wl__pointers(void) { return &pointers; }

/* Surface under a screen point (menus, fullscreen or the node's window). */
struct wl_resource *wl__surface_at(float x, float y, float *lx, float *ly) {
    if (!dpy) return NULL;
    Surf *target = NULL;
    for (int i = npopups - 1; i >= 0 && !target; i--) {
        float ox, oy;
        if (origin(popups[i], &ox, &oy)) target = surface_at(popups[i], x - ox, y - oy, lx, ly);
    }
    if (!target && fs_surf && fs_surf->content) {
        float ox, oy;
        if (origin(fs_surf, &ox, &oy)) target = surface_at(fs_surf, x - ox, y - oy, lx, ly);
    } else if (!target) {
        Node *hit = hit_test(ui.root, x, y);
        if (hit && hit->type == NODE_SURFACE) {
            Surf *s = find(hit->term);
            float ox, oy;
            if (s && origin(s, &ox, &oy)) target = surface_at(s, x - ox, y - oy, lx, ly);
        }
    }
    return target ? target->res : NULL;
}

/* kms.c — modesetting con ioctls del kernel: conector, CRTC, dumb buffers, page flip. */
#include "kms.h"

#include <errno.h>
#include <drm/drm_fourcc.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do r = ioctl(fd, req, arg);
    while (r == -1 && (errno == EINTR || errno == EAGAIN));
    return r;
}

static int create_buffer(Kms *k, KmsBuffer *b) {
    struct drm_mode_create_dumb c = { .width = k->width, .height = k->height, .bpp = 32 };
    if (xioctl(k->fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) { perror("CREATE_DUMB"); return -1; }
    b->handle = c.handle; b->pitch = c.pitch; b->size = c.size;

    struct drm_mode_fb_cmd f = { .width = k->width, .height = k->height, .pitch = c.pitch,
                                 .bpp = 32, .depth = 24, .handle = c.handle };
    if (xioctl(k->fd, DRM_IOCTL_MODE_ADDFB, &f)) { perror("ADDFB"); return -1; }
    b->fb_id = f.fb_id;

    struct drm_mode_map_dumb m = { .handle = c.handle };
    if (xioctl(k->fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) { perror("MAP_DUMB"); return -1; }
    b->map = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, k->fd, m.offset);
    if (b->map == MAP_FAILED) { b->map = NULL; perror("mmap"); return -1; }
    memset(b->map, 0, b->size);
    return 0;
}

static void destroy_buffer(Kms *k, KmsBuffer *b) {
    if (b->map) munmap(b->map, b->size);
    if (b->fb_id) xioctl(k->fd, DRM_IOCTL_MODE_RMFB, &b->fb_id);
    if (b->handle) {
        struct drm_mode_destroy_dumb d = { .handle = b->handle };
        xioctl(k->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
    memset(b, 0, sizeof *b);
}

/* Busca un conector enchufado, su modo preferido y un CRTC compatible. */
static int pick_output(Kms *k) {
    struct drm_mode_card_res res = {0};
    if (xioctl(k->fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) { perror("GETRESOURCES"); return -1; }
    uint32_t *conns = calloc(res.count_connectors + 1, 4);
    uint32_t *crtcs = calloc(res.count_crtcs + 1, 4);
    uint32_t *encs  = calloc(res.count_encoders + 1, 4);
    uint32_t *fbs   = calloc(res.count_fbs + 1, 4);
    res.connector_id_ptr = (uintptr_t)conns;
    res.crtc_id_ptr = (uintptr_t)crtcs;
    res.encoder_id_ptr = (uintptr_t)encs;
    res.fb_id_ptr = (uintptr_t)fbs;
    int ret = -1;
    if (xioctl(k->fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) { perror("GETRESOURCES"); goto out; }

    for (uint32_t i = 0; i < res.count_connectors && ret; i++) {
        struct drm_mode_get_connector c = { .connector_id = conns[i] };
        if (xioctl(k->fd, DRM_IOCTL_MODE_GETCONNECTOR, &c)) continue;
        if (c.connection != 1 || c.count_modes == 0) continue;

        struct drm_mode_modeinfo *modes = calloc(c.count_modes, sizeof *modes);
        uint32_t *cencs = calloc(c.count_encoders + 1, 4);
        c.modes_ptr = (uintptr_t)modes;
        c.encoders_ptr = (uintptr_t)cencs;
        c.count_props = 0; c.props_ptr = 0; c.prop_values_ptr = 0;
        if (xioctl(k->fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) || c.count_modes == 0) {
            free(modes); free(cencs); continue;
        }

        struct drm_mode_modeinfo *mode = &modes[0];
        for (uint32_t m = 0; m < c.count_modes; m++)
            if (modes[m].type & DRM_MODE_TYPE_PREFERRED) { mode = &modes[m]; break; }

        uint32_t crtc = 0;
        if (c.encoder_id) {
            struct drm_mode_get_encoder e = { .encoder_id = c.encoder_id };
            if (!xioctl(k->fd, DRM_IOCTL_MODE_GETENCODER, &e)) crtc = e.crtc_id;
        }
        for (uint32_t e_i = 0; !crtc && e_i < c.count_encoders; e_i++) {
            struct drm_mode_get_encoder e = { .encoder_id = cencs[e_i] };
            if (xioctl(k->fd, DRM_IOCTL_MODE_GETENCODER, &e)) continue;
            for (uint32_t j = 0; j < res.count_crtcs; j++)
                if (e.possible_crtcs & (1u << j)) { crtc = crtcs[j]; break; }
        }
        if (crtc) {
            k->conn_id = c.connector_id;
            k->crtc_id = crtc;
            k->crtc_index = 0;
            for (uint32_t j = 0; j < res.count_crtcs; j++) if (crtcs[j] == crtc) { k->crtc_index = (int)j; break; }
            k->mode = *mode;
            k->width = mode->hdisplay;
            k->height = mode->vdisplay;
            ret = 0;
        }
        free(modes); free(cencs);
    }
    if (ret) fprintf(stderr, "kms: no hay monitor conectado\n");
out:
    free(conns); free(crtcs); free(encs); free(fbs);
    return ret;
}

/* ---- formatos del plano primario (IN_FORMATS) ---- */

#define PLANE_TYPE_PRIMARY 1        /* enum drm_plane_type del kernel (no está en la uapi) */

/* Valor de la propiedad `name` de un objeto KMS. 0 si la encontró. */
static int obj_prop(int fd, uint32_t obj, uint32_t type, const char *name, uint64_t *val) {
    struct drm_mode_obj_get_properties op = { .obj_id = obj, .obj_type = type };
    if (xioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op) || !op.count_props) return -1;
    uint32_t ids[64], n = op.count_props > 64 ? 64 : op.count_props;
    uint64_t vals[64];
    op.count_props = n;
    op.props_ptr = (uintptr_t)ids;
    op.prop_values_ptr = (uintptr_t)vals;
    if (xioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op)) return -1;
    if (op.count_props < n) n = op.count_props;
    for (uint32_t i = 0; i < n; i++) {
        struct drm_mode_get_property pr = { .prop_id = ids[i] };
        if (xioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &pr)) continue;
        pr.name[DRM_PROP_NAME_LEN - 1] = 0;
        if (!strcmp(pr.name, name)) { *val = vals[i]; return 0; }
    }
    return -1;
}

static int rgb32(uint32_t f) {
    return f == DRM_FORMAT_XRGB8888 || f == DRM_FORMAT_ARGB8888 || f == DRM_FORMAT_XBGR8888 || f == DRM_FORMAT_ABGR8888;
}

/* Lee el blob IN_FORMATS (struct drm_format_modifier_blob) y guarda los pares de RGB de 32 bits. */
static void parse_in_formats(Kms *k, uint32_t blob_id) {
    struct drm_mode_get_blob gb = { .blob_id = blob_id };
    if (xioctl(k->fd, DRM_IOCTL_MODE_GETPROPBLOB, &gb) || gb.length < sizeof(struct drm_format_modifier_blob) || gb.length > (1u << 20)) return;
    uint8_t *d = malloc(gb.length);
    if (!d) return;
    gb.data = (uintptr_t)d;
    if (xioctl(k->fd, DRM_IOCTL_MODE_GETPROPBLOB, &gb)) { free(d); return; }
    struct drm_format_modifier_blob h;
    memcpy(&h, d, sizeof h);
    uint64_t len = gb.length;
    if ((uint64_t)h.formats_offset + (uint64_t)h.count_formats * 4 > len ||
        (uint64_t)h.modifiers_offset + (uint64_t)h.count_modifiers * sizeof(struct drm_format_modifier) > len) { free(d); return; }
    k->nscan = 0;
    for (uint32_t m = 0; m < h.count_modifiers; m++) {
        struct drm_format_modifier fm;
        memcpy(&fm, d + h.modifiers_offset + (size_t)m * sizeof fm, sizeof fm);
        for (int b = 0; b < 64; b++) {
            if (!(fm.formats >> b & 1)) continue;
            uint64_t idx = (uint64_t)fm.offset + (uint64_t)b;
            if (idx >= h.count_formats) break;
            uint32_t f;
            memcpy(&f, d + h.formats_offset + idx * 4, 4);
            if (!rgb32(f) || k->nscan >= (int)(sizeof k->scan / sizeof k->scan[0])) continue;
            k->scan[k->nscan].fourcc = f;
            k->scan[k->nscan].modifier = fm.modifier;
            k->nscan++;
        }
    }
    k->scan_known = 1;
    free(d);
}

/* Busca el plano primario del CRTC y lo que acepta. Sin IN_FORMATS (drivers viejos) queda
 * scan_known = 0 y el scanout directo se prueba con ADDFB2 como antes. */
static void read_scanout_formats(Kms *k) {
    struct drm_version v;
    memset(&v, 0, sizeof v);
    v.name = k->driver;
    v.name_len = sizeof k->driver - 1;
    if (xioctl(k->fd, DRM_IOCTL_VERSION, &v)) snprintf(k->driver, sizeof k->driver, "?");
    else k->driver[v.name_len < sizeof k->driver - 1 ? v.name_len : sizeof k->driver - 1] = 0;
    struct drm_set_client_cap cap = { .capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES, .value = 1 };
    if (xioctl(k->fd, DRM_IOCTL_SET_CLIENT_CAP, &cap)) return;
    struct drm_mode_get_plane_res pr = {0};
    if (xioctl(k->fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pr) || !pr.count_planes) return;
    uint32_t planes[64], np = pr.count_planes > 64 ? 64 : pr.count_planes;
    pr.count_planes = np;
    pr.plane_id_ptr = (uintptr_t)planes;
    if (xioctl(k->fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pr)) return;
    if (pr.count_planes < np) np = pr.count_planes;
    for (uint32_t i = 0; i < np; i++) {
        struct drm_mode_get_plane gp = { .plane_id = planes[i] };
        if (xioctl(k->fd, DRM_IOCTL_MODE_GETPLANE, &gp) || !(gp.possible_crtcs >> k->crtc_index & 1)) continue;
        uint64_t type = 0, blob = 0;
        if (obj_prop(k->fd, planes[i], DRM_MODE_OBJECT_PLANE, "type", &type) || type != PLANE_TYPE_PRIMARY) continue;
        if (!obj_prop(k->fd, planes[i], DRM_MODE_OBJECT_PLANE, "IN_FORMATS", &blob) && blob) parse_in_formats(k, (uint32_t)blob);
        break;
    }
    if (!k->scan_known) { fprintf(stderr, "kms: placa %s, el plano primario no informa modificadores\n", k->driver); return; }
    fprintf(stderr, "kms: placa %s, XRGB8888 en el plano primario con modificadores:", k->driver);
    for (int i = 0; i < k->nscan; i++)
        if (k->scan[i].fourcc == DRM_FORMAT_XRGB8888) fprintf(stderr, " 0x%llx", (unsigned long long)k->scan[i].modifier);
    fputc('\n', stderr);
}

int kms_scanout_ok(const Kms *k, uint32_t fourcc, uint64_t modifier) {
    if (!k->scan_known) return 1;
    uint64_t m = modifier == DRM_FORMAT_MOD_INVALID ? DRM_FORMAT_MOD_LINEAR : modifier;
    for (int i = 0; i < k->nscan; i++)
        if (k->scan[i].fourcc == fourcc && k->scan[i].modifier == m) return 1;
    return 0;
}

static int kms_setup(Kms *k) {
    if (pick_output(k)) return -1;
    read_scanout_formats(k);
    k->saved.crtc_id = k->crtc_id;
    xioctl(k->fd, DRM_IOCTL_MODE_GETCRTC, &k->saved);
    if (create_buffer(k, &k->buf[0]) || create_buffer(k, &k->buf[1])) return -1;
    /* el primer cuadro en negro (no lo que dejó la sesión anterior) */
    memset(k->buf[0].map, 0, k->buf[0].size);
    memset(k->buf[1].map, 0, k->buf[1].size);
    struct drm_mode_crtc set = {
        .crtc_id = k->crtc_id, .fb_id = k->buf[0].fb_id,
        .set_connectors_ptr = (uintptr_t)&k->conn_id, .count_connectors = 1,
        .mode = k->mode, .mode_valid = 1,
    };
    if (xioctl(k->fd, DRM_IOCTL_MODE_SETCRTC, &set)) { perror("SETCRTC"); return -1; }
    k->front = 0;
    fprintf(stderr, "kms: %ux%u@%u en conector %u, crtc %u\n",
            k->width, k->height, k->mode.vrefresh, k->conn_id, k->crtc_id);
    return 0;
}

int kms_open(Kms *k, const char *path) {
    memset(k, 0, sizeof *k);
    k->fd = open(path, O_RDWR | O_CLOEXEC);
    if (k->fd < 0) { perror(path); return -1; }
    if (xioctl(k->fd, DRM_IOCTL_SET_MASTER, NULL))
        fprintf(stderr, "kms: aviso, no somos DRM master (%s)\n", strerror(errno));
    if (kms_setup(k)) { kms_close(k); return -1; }
    return 0;
}

int kms_attach(Kms *k, int fd) {
    memset(k, 0, sizeof *k);
    k->fd = fd;
    k->borrowed = 1;
    if (kms_setup(k)) { kms_close(k); return -1; }
    return 0;
}

void kms_close(Kms *k) {
    if (k->fd <= 0) return;
    if (k->cursor_handle) {
        struct drm_mode_cursor2 c = { .flags = DRM_MODE_CURSOR_BO, .crtc_id = k->crtc_id };
        xioctl(k->fd, DRM_IOCTL_MODE_CURSOR2, &c);
        if (k->cursor_map) munmap(k->cursor_map, k->cursor_size);
        struct drm_mode_destroy_dumb d = { .handle = k->cursor_handle };
        xioctl(k->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
        k->cursor_handle = 0;
    }
    if (k->saved.mode_valid && k->saved.fb_id && !k->borrowed) {
        struct drm_mode_crtc r = k->saved;
        r.set_connectors_ptr = (uintptr_t)&k->conn_id;
        r.count_connectors = 1;
        xioctl(k->fd, DRM_IOCTL_MODE_SETCRTC, &r);
    }
    /* prestado (kms_attach): el próximo en mostrarse pone su cuadro; el master lo maneja
     * lord-sessiond. Los dumb buffers se destruyen después de que otro tomó la pantalla. */
    destroy_buffer(k, &k->buf[0]);
    destroy_buffer(k, &k->buf[1]);
    if (!k->borrowed) xioctl(k->fd, DRM_IOCTL_DROP_MASTER, NULL);
    close(k->fd);
    k->fd = -1;
}

KmsBuffer *kms_back(Kms *k) { return &k->buf[k->front ^ 1]; }

int kms_flip(Kms *k) {
    int back = k->front ^ 1;
    struct drm_mode_crtc_page_flip pf = {
        .crtc_id = k->crtc_id, .fb_id = k->buf[back].fb_id,
        .flags = DRM_MODE_PAGE_FLIP_EVENT,
    };
    if (xioctl(k->fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf) == 0) {
        k->flip_pending = 1;
        return 0;
    }
    /* Fallback síncrono si el driver no soporta page flip. */
    struct drm_mode_crtc set = {
        .crtc_id = k->crtc_id, .fb_id = k->buf[back].fb_id,
        .set_connectors_ptr = (uintptr_t)&k->conn_id, .count_connectors = 1,
        .mode = k->mode, .mode_valid = 1,
    };
    if (xioctl(k->fd, DRM_IOCTL_MODE_SETCRTC, &set)) { perror("SETCRTC"); return -1; }
    k->front = back;
    return 0;
}

void kms_dispatch(Kms *k) {
    /* alineado: los eventos llevan campos de 64 bits (user_data) y se leen con un cast */
    char buf[1024] __attribute__((aligned(8)));
    ssize_t n = read(k->fd, buf, sizeof buf);
    for (ssize_t off = 0; n > 0 && off + (ssize_t)sizeof(struct drm_event) <= n;) {
        struct drm_event *e = (struct drm_event *)(buf + off);
        if (e->type == DRM_EVENT_FLIP_COMPLETE && e->length >= sizeof(struct drm_event_vblank)) {
            const struct drm_event_vblank *v = (const struct drm_event_vblank *)e;
            k->flip_ns = (uint64_t)v->tv_sec * 1000000000ull + (uint64_t)v->tv_usec * 1000ull;
            k->flip_seq = v->sequence;
        }
        if (e->type == DRM_EVENT_FLIP_COMPLETE && k->flip_pending) {
            k->flip_pending = 0;
            if (k->custom) {
                const struct drm_event_vblank *v = (const struct drm_event_vblank *)e;
                if (k->on_flip) k->on_flip((void *)(uintptr_t)v->user_data);
            } else k->front ^= 1;
        }
        if (e->length == 0) break;
        off += e->length;
    }
}

/* ---- framebuffers propios (GPU y scanout directo) ---- */

int kms_add_fb2(Kms *k, uint32_t w, uint32_t h, uint32_t fourcc, const uint32_t handles[4],
                const uint32_t pitches[4], const uint32_t offsets[4], uint64_t modifier, uint32_t *fb) {
    struct drm_mode_fb_cmd2 f = { .width = w, .height = h, .pixel_format = fourcc };
    for (int i = 0; i < 4; i++) {
        f.handles[i] = handles[i];
        f.pitches[i] = pitches[i];
        f.offsets[i] = offsets[i];
        if (handles[i] && modifier != DRM_FORMAT_MOD_INVALID) f.modifier[i] = modifier;
    }
    if (modifier != DRM_FORMAT_MOD_INVALID) f.flags = DRM_MODE_FB_MODIFIERS;
    if (xioctl(k->fd, DRM_IOCTL_MODE_ADDFB2, &f)) return -1;
    *fb = f.fb_id;
    return 0;
}

void kms_rm_fb(Kms *k, uint32_t fb) { if (fb) xioctl(k->fd, DRM_IOCTL_MODE_RMFB, &fb); }

int kms_prime_import(Kms *k, int dmabuf_fd, uint32_t *handle) {
    struct drm_prime_handle p = { .fd = dmabuf_fd };
    if (xioctl(k->fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &p)) return -1;
    *handle = p.handle;
    return 0;
}

void kms_close_handle(Kms *k, uint32_t handle) {
    if (!handle) return;
    struct drm_gem_close c = { .handle = handle };
    xioctl(k->fd, DRM_IOCTL_GEM_CLOSE, &c);
}

int kms_set_fb(Kms *k, uint32_t fb) {
    struct drm_mode_crtc set = {
        .crtc_id = k->crtc_id, .fb_id = fb,
        .set_connectors_ptr = (uintptr_t)&k->conn_id, .count_connectors = 1,
        .mode = k->mode, .mode_valid = 1,
    };
    return xioctl(k->fd, DRM_IOCTL_MODE_SETCRTC, &set);
}

int kms_flip_fb(Kms *k, uint32_t fb, void *data) {
    struct drm_mode_crtc_page_flip pf = {
        .crtc_id = k->crtc_id, .fb_id = fb, .flags = DRM_MODE_PAGE_FLIP_EVENT, .user_data = (uintptr_t)data,
    };
    k->custom = 1;
    if (xioctl(k->fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf) == 0) { k->flip_pending = 1; return 0; }
    /* sin page flip: modeset síncrono y se avisa enseguida */
    if (kms_set_fb(k, fb)) return -1;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    k->flip_ns = (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
    k->flip_seq++;
    if (k->on_flip) k->on_flip(data);
    return 0;
}

/* ---- cursor por hardware ---- */

int kms_cursor_image(Kms *k, const uint32_t *argb, int w, int h, int hot_x, int hot_y) {
    if (!k->cursor_handle) {
        struct drm_mode_create_dumb c = { .width = 64, .height = 64, .bpp = 32 };
        if (xioctl(k->fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) return -1;
        struct drm_mode_map_dumb m = { .handle = c.handle };
        void *map = xioctl(k->fd, DRM_IOCTL_MODE_MAP_DUMB, &m) ? MAP_FAILED
                  : mmap(NULL, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, k->fd, m.offset);
        if (map == MAP_FAILED) {
            struct drm_mode_destroy_dumb d = { .handle = c.handle };
            xioctl(k->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
            return -1;
        }
        k->cursor_handle = c.handle;
        k->cursor_map = map;
        k->cursor_size = c.size;
        k->cursor_w = c.pitch / 4;
        k->cursor_h = 64;
    }
    memset(k->cursor_map, 0, k->cursor_size);
    for (int y = 0; y < h && y < 64; y++)
        for (int x = 0; x < w && x < 64; x++) k->cursor_map[(size_t)y * k->cursor_w + x] = argb[(size_t)y * w + x];
    struct drm_mode_cursor2 c = { .flags = DRM_MODE_CURSOR_BO, .crtc_id = k->crtc_id, .width = 64, .height = 64,
                                  .handle = k->cursor_handle, .hot_x = hot_x, .hot_y = hot_y };
    k->cursor_ok = xioctl(k->fd, DRM_IOCTL_MODE_CURSOR2, &c) == 0;
    k->cursor_visible = k->cursor_ok;
    return k->cursor_ok ? 0 : -1;
}

void kms_cursor_move(Kms *k, int x, int y) {
    if (!k->cursor_ok || !k->cursor_visible) return;
    struct drm_mode_cursor c = { .flags = DRM_MODE_CURSOR_MOVE, .crtc_id = k->crtc_id, .x = x, .y = y };
    xioctl(k->fd, DRM_IOCTL_MODE_CURSOR, &c);
}

void kms_cursor_show(Kms *k, int visible) {
    if (!k->cursor_ok || k->cursor_visible == visible) return;
    struct drm_mode_cursor2 c = { .flags = DRM_MODE_CURSOR_BO, .crtc_id = k->crtc_id, .width = 64, .height = 64,
                                  .handle = visible ? k->cursor_handle : 0 };
    xioctl(k->fd, DRM_IOCTL_MODE_CURSOR2, &c);
    k->cursor_visible = visible;
}

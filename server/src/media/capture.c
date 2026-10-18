/* capture.c — capturas de pantalla, grabación y lo que ve el escritorio remoto, con las ventanas
 * protegidas en negro (ver docs/MEDIOS.md, «Ventanas protegidas»).
 *
 * Una ventana se marca protegida con wm.setProtected(id, true) (el SDK pone `protected` en el
 * nodo de su contenido). En la pantalla local se ve normal; en cualquier captura se pinta
 * negra:
 *   - captura de pantalla: se vuelve a pintar todo el árbol en un lienzo aparte en «modo
 *     captura» (paint_capture), donde un nodo protegido es un rectángulo negro. Respeta el orden
 *     de las ventanas: lo que esté encima de la protegida se ve.
 *   - grabación y escritorio remoto: si hay alguna ventana protegida, se mantiene un segundo
 *     lienzo en modo captura, repintado solo en la región que cambió en cada cuadro; si no
 *     hay ninguna, se usa el cuadro tal cual (sin costo extra).
 * Grabar (como el escritorio remoto) compone por CPU, así el cuadro está en memoria. */
#include "media_int.h"
#include "../ui/ui.h"
#include "../wl/wl.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static Canvas cap;                  /* escena en modo captura (persistente) */
static int cap_valid, last_remote;
static uint32_t *fs_px;             /* cap + pantalla completa (temporal) */
static size_t fs_cap;
static MediaSrc *rec;               /* grabación de pantalla en curso (una a la vez) */
static int rec_pending;

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int count_protected(const Node *n) {
    if (!n || n->type == NODE_RAW || n->s.display_none) return 0;
    if (n->protect) return 1;
    int c = 0;
    for (const Node *k = n->first; k; k = k->next) c += count_protected(k);
    return c;
}

int media_protected_count(void) { return count_protected(ui.root); }

static int ensure_cap(int w, int h) {
    if (cap.px && cap.w == w && cap.h == h) return 0;
    free(cap.px);
    uint32_t *px = malloc((size_t)w * h * 4);
    if (!px) { cap.px = NULL; return -1; }
    canvas_init(&cap, px, w, h, w);
    cap_valid = 0;
    return 0;
}

static void repaint_cap(Rect clip) {
    canvas_set_clip(&cap, clip);
    canvas_clear(&cap, 0x000000);
    paint_capture(&cap, ui.root);
    canvas_set_clip(&cap, (Rect){ 0, 0, cap.w, cap.h });
}

void media_scene_painted(const Canvas *scene) {
    if (!(rec || last_remote) || !media_protected_count()) { cap_valid = 0; return; }
    if (ensure_cap(scene->w, scene->h)) return;
    if (!cap_valid) { repaint_cap((Rect){ 0, 0, cap.w, cap.h }); cap_valid = 1; }
    else repaint_cap(scene->clip);
}

/* ---- cursor en la grabación (la flecha de LordOS o la imagen de la app) ---- */
static const char *const arrow[] = {
    "X", "XX", "XoX", "XooX", "XoooX", "XooooX", "XoooooX", "XooooooX", "XoooooooX",
    "XooooooooX", "XoooooooooX", "XooooooooooX", "XooooooXXXXX", "XoooXooX",
    "XooX XooX", "XoX  XooX", "XX    XooX", "X     XooX", "       XX", NULL,
};

static void feed_recording(const Canvas *src, const MediaCursor *cur) {
    MediaSrc *s = rec;
    if (!s || !s->in_map || s->sock < 0) return;
    double t = now_ms();
    double gap = 1000.0 / (s->rec_fps > 0 ? s->rec_fps : 30);
    if (t - s->rec_last < gap - 2) { rec_pending = 1; return; }
    if (src->w != s->in_w || src->h != s->in_h) { s->rec_dropped++; return; }    /* cambió la pantalla: se saltea */
    int slot = -1;
    for (int i = 0; i < s->in_slots; i++) if (!s->in_busy[i]) { slot = i; break; }
    if (slot < 0) { s->rec_dropped++; rec_pending = 1; return; }              /* el codificador va atrasado */
    uint32_t *dst = (uint32_t *)(s->in_map + (size_t)slot * s->in_w * s->in_h * 4);
    for (int y = 0; y < src->h; y++) memcpy(dst + (size_t)y * s->in_w, src->px + (size_t)y * src->stride, (size_t)src->w * 4);
    if (cur) {
        Canvas c;
        canvas_init(&c, dst, s->in_w, s->in_h, s->in_w);
        if (cur->mode == 1 && cur->px) canvas_blit(&c, cur->px, cur->w, cur->h, cur->x - cur->hx, cur->y - cur->hy, 0, 1, 0);
        else if (cur->mode == 0) canvas_bitmap(&c, cur->x, cur->y, arrow, 0xff000000u, 0xffffffffu);
    }
    if (mp_sendv(s->sock, MM_INFRAME, (uint32_t)slot, (int64_t)(t - s->rec_start), 0, 0, 0)) { s->rec_dropped++; return; }
    s->in_busy[slot] = 1;
    s->rec_last = t;
    s->rec_frames++;
    rec_pending = 0;
}

const Canvas *media_composed(const Canvas *out, int remote, const MediaCursor *cur) {
    if (remote != last_remote) { last_remote = remote; cap_valid = 0; }
    const Canvas *src = out;
    if ((remote || rec) && media_protected_count() && !ensure_cap(out->w, out->h)) {
        if (!cap_valid) { repaint_cap((Rect){ 0, 0, cap.w, cap.h }); cap_valid = 1; }
        src = &cap;
        if (wl_fullscreen_active()) {
            size_t need = (size_t)cap.w * cap.h;
            if (need > fs_cap) { free(fs_px); fs_px = malloc(need * 4); fs_cap = fs_px ? need : 0; }
            if (fs_px) {
                static Canvas tmp;
                canvas_init(&tmp, fs_px, cap.w, cap.h, cap.w);
                canvas_copy(&tmp, &cap);
                paint_capture_mode = 1;
                wl_paint_fullscreen(&tmp);
                paint_capture_mode = 0;
                src = &tmp;
            }
        }
    }
    if (rec) feed_recording(src, cur);
    return src;
}

int media_wants_frame(void) {
    return rec && rec_pending && now_ms() - rec->rec_last >= 1000.0 / (rec->rec_fps > 0 ? rec->rec_fps : 30);
}

int media_needs_cpu(void) { return rec != NULL; }

int media_memfd_from_pixels(const uint32_t *px, int w, int h) {
    size_t n = (size_t)w * h * 4;
    int fd = memfd_create("lordos-capture", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)n)) { close(fd); return -1; }
    void *m = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { close(fd); return -1; }
    memcpy(m, px, n);
    munmap(m, n);
    fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
    return fd;
}

/* ---- captura de pantalla ---- */

MediaSrc *media_screenshot(const char *app, int x, int y, int w, int h) {
    int W = (int)ui.width, H = (int)ui.height;
    if (W <= 0 || H <= 0 || !ui.root) return NULL;
    uint32_t *full = malloc((size_t)W * H * 4);
    if (!full) return NULL;
    Canvas c;
    canvas_init(&c, full, W, H, W);
    canvas_clear(&c, 0x000000);
    paint_capture(&c, ui.root);
    paint_capture_mode = 1;
    wl_paint_fullscreen(&c);
    paint_capture_mode = 0;
    if (w <= 0 || h <= 0) { x = 0; y = 0; w = W; h = H; }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w < 1 || h < 1) { free(full); return NULL; }
    MediaSrc *s = media_src_new(SK_CAPTURE, app);
    if (!s) { free(full); return NULL; }
    if (x == 0 && y == 0 && w == W && h == H) s->px = full;
    else {
        s->px = malloc((size_t)w * h * 4);
        if (s->px) for (int j = 0; j < h; j++) memcpy(s->px + (size_t)j * w, full + (size_t)(y + j) * W + x, (size_t)w * 4);
        free(full);
        if (!s->px) { s->used = 0; return NULL; }
    }
    s->w = w;
    s->h = h;
    s->opaque = 1;
    for (size_t i = 0; i < (size_t)w * h; i++) s->px[i] |= 0xff000000u;      /* sin huecos (camino GPU) */
    s->frame_seq = 1;
    return s;
}

MediaSrc *media_crop(MediaSrc *src, int x, int y, int w, int h) {
    if (!src || !src->px || (src->kind != SK_CAPTURE && src->kind != SK_IMAGE)) return NULL;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > src->w) w = src->w - x;
    if (y + h > src->h) h = src->h - y;
    if (w < 1 || h < 1) return NULL;
    MediaSrc *s = media_src_new(SK_CAPTURE, src->app);
    if (!s) return NULL;
    s->px = malloc((size_t)w * h * 4);
    if (!s->px) { s->used = 0; return NULL; }
    for (int j = 0; j < h; j++) memcpy(s->px + (size_t)j * w, src->px + (size_t)(y + j) * src->w + x, (size_t)w * 4);
    s->w = w;
    s->h = h;
    s->opaque = 1;
    s->frame_seq = 1;
    return s;
}

/* ---- grabación de pantalla ---- */

int media_screenrec_start(MediaSrc *s, int fps) {
    if (rec) return -1;
    int W = (int)ui.width & ~1, H = (int)ui.height & ~1;
    int slots = 3;
    size_t n = (size_t)W * H * 4 * slots;
    int fd = memfd_create("lordos-screen", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0 || ftruncate(fd, (off_t)n) || fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL)) { if (fd >= 0) close(fd); return -1; }
    void *m = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { close(fd); return -1; }
    s->in_map = m;
    s->in_len = n;
    s->in_fd = fd;
    s->in_w = W;
    s->in_h = H;
    s->in_slots = slots;
    memset(s->in_busy, 0, sizeof s->in_busy);
    s->rec_fps = fps > 0 && fps <= 60 ? fps : 30;
    s->rec_start = now_ms();
    s->rec_last = 0;
    int64_t v[6] = { slots, W, H, 0, 0, 0 };
    if (mp_send(s->sock, MM_INRING, 0, v, NULL, 0, &fd, 1)) { munmap(m, n); close(fd); s->in_map = NULL; s->in_fd = -1; return -1; }
    rec = s;
    rec_pending = 1;
    cap_valid = 0;
    /* el próximo cuadro entero (y por CPU: main.c apaga la GPU mientras se graba) */
    damage_add((Box){ 0, 0, ui.width, ui.height });
    ui.dirty = 1;
    return 0;
}

void media_capture_src_closed(MediaSrc *s) {
    if (rec != s) return;
    rec = NULL;
    rec_pending = 0;
    fprintf(stderr, "medios: grabación de pantalla: %ld cuadros, %ld descartados\n", s->rec_frames, s->rec_dropped);
}

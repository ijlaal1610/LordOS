/* kms.h — salida de video directa al kernel (DRM/KMS), sin libdrm. */
#ifndef LORD_KMS_H
#define LORD_KMS_H

#include <stdint.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

typedef struct {
    uint32_t fb_id, handle, pitch;
    uint64_t size;
    uint32_t *map;
} KmsBuffer;

typedef struct {
    int fd;
    uint32_t conn_id, crtc_id;
    struct drm_mode_modeinfo mode;
    uint32_t width, height;
    KmsBuffer buf[2];
    int front;          /* buffer que se ve en pantalla */
    int flip_pending;   /* esperando vblank */
    struct drm_mode_crtc saved;
    /* flips de framebuffers propios (camino GPU, scanout directo): al terminar se llama a
     * on_flip con lo que se pasó a kms_flip_fb; los dumb buffers de arriba no se usan */
    void (*on_flip)(void *data);
    int custom;
    /* último flip: hora (CLOCK_MONOTONIC, ns) y número de vblank (wp_presentation) */
    uint64_t flip_ns;
    unsigned flip_seq;
    /* cursor por hardware (plano de cursor, ioctl clásico) */
    uint32_t cursor_handle, cursor_w, cursor_h;
    uint32_t *cursor_map;
    uint64_t cursor_size;
    int cursor_ok, cursor_visible;
    int borrowed;       /* el fd lo pasó lord-sessiond (master y modo previo los maneja él) */
    /* plano primario del CRTC: formato y modificadores que acepta (propiedad IN_FORMATS). En la
     * Raspberry Pi (vc4) el RGB solo se muestra lineal: los búferes con tiling UIF de v3d no van
     * directo a la pantalla. scan_known = 0: el driver no lo dice (se prueba con ADDFB2). */
    char driver[32];
    int crtc_index, scan_known, nscan;
    struct { uint32_t fourcc; uint64_t modifier; } scan[96];
} Kms;

int  kms_open(Kms *k, const char *path);
/* Con un fd de la placa que pasó lord-sessiond (ya master): misma preparación que kms_open. */
int  kms_attach(Kms *k, int fd);
void kms_close(Kms *k);
/* Buffer libre para dibujar (el que no está en pantalla). */
KmsBuffer *kms_back(Kms *k);
/* Muestra el back buffer en el próximo vblank. */
int  kms_flip(Kms *k);
/* Procesa eventos del fd DRM (fin de page flip). */
void kms_dispatch(Kms *k);

/* Framebuffer desde handles GEM del fd de KMS (BO de GBM, o un dmabuf importado). Con modifier
 * != DRM_FORMAT_MOD_INVALID se usa DRM_MODE_FB_MODIFIERS. 0 bien. */
int  kms_add_fb2(Kms *k, uint32_t w, uint32_t h, uint32_t fourcc, const uint32_t handles[4],
                 const uint32_t pitches[4], const uint32_t offsets[4], uint64_t modifier, uint32_t *fb);
void kms_rm_fb(Kms *k, uint32_t fb);
int  kms_prime_import(Kms *k, int dmabuf_fd, uint32_t *handle);    /* dmabuf → handle GEM */
void kms_close_handle(Kms *k, uint32_t handle);
/* Primer modeset con un framebuffer propio, y page flip a uno (data vuelve en on_flip). */
int  kms_set_fb(Kms *k, uint32_t fb);
int  kms_flip_fb(Kms *k, uint32_t fb, void *data);

/* ¿El plano primario muestra este formato con este modificador? 1 sí (o no se sabe), 0 no.
 * DRM_FORMAT_MOD_INVALID (implícito) cuenta como lineal. */
int  kms_scanout_ok(const Kms *k, uint32_t fourcc, uint64_t modifier);

/* Cursor por hardware: imagen ARGB (hasta 64×64) y posición. 0 si el driver lo soporta. */
int  kms_cursor_image(Kms *k, const uint32_t *argb, int w, int h, int hot_x, int hot_y);
void kms_cursor_move(Kms *k, int x, int y);
void kms_cursor_show(Kms *k, int visible);

#endif

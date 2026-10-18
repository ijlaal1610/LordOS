/* media_int.h — partes internas de los medios de lord-server (media.c, mview.c, capture.c,
 * annot.c y ui/js_media.c). */
#ifndef LORD_MEDIA_INT_H
#define LORD_MEDIA_INT_H

#include "media.h"
#include "proto.h"

#include <stddef.h>
#include <sys/types.h>

#define MEDIA_MAX_SRC 48
#define MEDIA_APP_MAX 48

typedef struct MAudio MAudio;

enum { SK_NONE = 0, SK_PLAYER, SK_CAMERA, SK_IMAGE, SK_CAPTURE, SK_SCREENREC, SK_AUDIOREC };

/* Cómo se muestra un origen en su nodo. */
typedef struct {
    int fit;                    /* 0 encajar (contain), 1 cubrir (cover) */
    float zoom;                 /* 0 = según fit; 1 = 100 % (un píxel de la imagen por píxel) */
    float cx, cy;               /* centro visible, 0..1 de la imagen (con zoom) */
    int rot;                    /* 0, 90, 180, 270 */
    int mirror;                 /* espejo horizontal (cámara) */
    uint32_t bg;                /* fondo de las bandas (ARGB; 0 transparente) */
    int bright, contrast;       /* vista previa de Fotos: -100..100 */
    unsigned gen;               /* cambia con cada setView */
} MediaView;

typedef struct {
    int used, id, kind;
    char app[MEDIA_APP_MAX];
    /* ayudante (reproductor, cámara, grabaciones) */
    int sock;
    pid_t pid;
    int state;                  /* MS_* */
    int closing;                /* se pidió cerrar: esperar el final (grabaciones) */
    /* píxeles: ranuras del ayudante, imagen (memfd) o captura (propia) */
    uint8_t *map;
    size_t map_len;
    int ring_gen, ring_w, ring_h, ring_slots, cur_slot;
    uint32_t *px;               /* captura o imagen: ARGB premultiplicado */
    int w, h, opaque;
    unsigned frame_seq;         /* cambia con cada cuadro nuevo */
    /* dibujo */
    MediaView view;
    int node, box_w, box_h, sent_w, sent_h;
    uint32_t *cache;            /* escalado/girado (cuando no alcanza con copiar) */
    int cache_w, cache_h;
    unsigned cache_seq, cache_gen;
    float cache_box[4];
    uint32_t *mip[8];           /* reducciones a la mitad (fotos grandes) */
    int mip_w[8], mip_h[8], nmip;
    /* estado del reproductor */
    int64_t pos, dur;
    int playing;
    /* grabaciones: archivo temporal → definitivo al terminar bien */
    char tmp[4096], dst[4096];
    /* grabación de pantalla: ranuras de entrada (lord-server escribe) */
    int in_fd, in_slots, in_w, in_h, in_busy[4];
    uint8_t *in_map;
    size_t in_len;
    double rec_start, rec_last;
    int rec_fps;
    long rec_frames, rec_dropped;
    int64_t stats[6];
    int job;                    /* imagen: trabajo que la decodifica */
    MAudio *audio;              /* reproductor: el stream de PipeWire (de lord-server) */
    MAudio *mic;                /* grabaciones: el micrófono (de lord-server, con permiso) */
    /* camera of an isolated app (sec/brokercam.c): its helper's messages go to the app over the
     * broker instead of the session; bk_msg returns 1 when it consumed the message */
    void *bk;
    int (*bk_msg)(void *src, const MediaMsg *m, const char *payload, int *fds);
} MediaSrc;

/* media.c */
MediaSrc *media_src_new(int kind, const char *app);
MediaSrc *media_src_get(int id);
MediaSrc *media_src_at(int i);          /* recorrer: 0..MEDIA_MAX_SRC-1 (NULL si está libre) */
void media_src_close(MediaSrc *s);
void media_src_damage(MediaSrc *s);
int  media_spawn(int kind, const char *params, const int *fds, int nfds, pid_t *pid);
int  media_pulse_fd(void);              /* socket conectado a PipeWire (del usuario) o -1 */
int  media_render_fd(void);             /* render node para VA-API o -1 */
const char *media_hwaccel(void);        /* "vaapi" | "v4l2request" | "none" (LORD_MEDIA_HWACCEL) */
void media_event(int id, const char *type, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void media_json_str(char *out, size_t n, const char *s);
const char *media_home(void);

/* trabajos de los workers */
enum { JP_EVENT = 0, JP_THUMB, JP_IMAGE, JP_PIXELS, JP_FILE, JP_CLIP };
int  media_job(int bg, int kind, int purpose, const int *fds, int nfds, int64_t a, int64_t b, const char *params,
               const char *tmp, const char *dst, int src);
/* Píxeles de un póster ya listo (lord.media.take): devuelve RGBA en *out (malloc) o -1. */
int  media_take_pixels(int job, uint8_t **out, int *w, int *h);

/* Rutas: entrada en la carpeta personal o montajes (realpath) y salida temporal + definitiva. */
int  media_open_input(const char *path, char *real, size_t n);
int  media_open_output(const char *path, char *tmp, size_t tn, char *dst, size_t dn);
int  media_finish_output(const char *tmp, const char *dst, int ok);
/* Miniatura: la ruta en ~/.cache/lordos/thumbs si ya está; si no, la pide y devuelve NULL. */
const char *media_thumb(const char *path, int size, char *out, size_t n);
int  media_cameras(char *json, size_t n);

/* maudio.c: los streams de PipeWire de los medios (lord-media no tiene el socket) */
MAudio *maudio_open(int rec, const char *app, const char *title, const char *role_or_source, int *ring_fd);
void maudio_close(MAudio *a);
void maudio_service(MAudio *a);
int  maudio_pollfd(const MAudio *a, short *events);
/* Grabación en modo "difusión" (el consumidor lee con su propio cursor y no toca rpos): el servidor
 * libera espacio avanzando rpos hasta wpos, para que el anillo nunca se trabe. Ver docs/PRIVACIDAD.md. */
void maudio_mic_drain(MAudio *a);
void maudio_playing(MAudio *a, int on);
void maudio_volume(MAudio *a, double v);
int  maudio_overruns(const MAudio *a);
int  maudio_failed(const MAudio *a);

/* mview.c */
void media_view_reset(MediaSrc *s);

/* capture.c */
MediaSrc *media_screenshot(const char *app, int x, int y, int w, int h);
MediaSrc *media_crop(MediaSrc *src, int x, int y, int w, int h);
int  media_screenrec_start(MediaSrc *s, int fps);
void media_capture_src_closed(MediaSrc *s);
int  media_memfd_from_pixels(const uint32_t *px, int w, int h);

/* annot.c: anotaciones sobre una captura */
typedef struct {
    int type;                   /* 'a' flecha, 'b' recuadro, 't' texto, 'z' desenfoque, 'l' línea */
    float x1, y1, x2, y2, width, size;
    uint32_t color;
    char text[512];
} MediaOp;
void media_annotate(uint32_t *px, int w, int h, const MediaOp *ops, int n);

#endif

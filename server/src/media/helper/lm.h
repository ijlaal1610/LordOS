/* lm.h — lord-media: el ayudante de medios de LordOS (ver docs/MEDIOS.md).
 *
 * Es el único programa de LordOS que enlaza FFmpeg (libavcodec, libavformat, libswscale,
 * libswresample) y libva, y solo para decodificar y codificar: el reproductor, la
 * sincronización y la interfaz son propios. lord-server lo lanza a pedido, con ruta fija y sin
 * argumentos; corre como el usuario, en la caja de arena compartida de los ayudantes
 * (sec/sandbox.c, sandbox_apply_helper): sin red, sin abrir archivos (solo los descriptores que
 * le pasa lord-server, incluidos PipeWire, el render node y la cámara) y con seccomp. Si se cae, el
 * escritorio sigue: lord-server solo ve que el socket se cerró. */
#ifndef LORD_MEDIA_LM_H
#define LORD_MEDIA_LM_H

#include "../proto.h"

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>

#define LM_SOCK 3                       /* el socket con lord-server */

extern int lm_debug;
#define LM_LOG(...) do { fprintf(stderr, "lord-media[%d]: ", (int)getpid()); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define LM_DBG(...) do { if (lm_debug) LM_LOG(__VA_ARGS__); } while (0)

double lm_now_ms(void);                 /* reloj monotónico */


/* ---- E/S (lm_io.c) ---- */
/* Abre un archivo de entrada ya abierto (fd) con FFmpeg, sin rutas. */
AVFormatContext *lm_open_input(int fd, char *err, size_t errlen);
void lm_close_input(AVFormatContext **fmt);
/* Contexto de salida sobre un fd (con seek si el fd lo permite). */
AVFormatContext *lm_open_output(int fd, const char *format);
int  lm_close_output(AVFormatContext *oc, int write_trailer);
/* memfd sellado (no puede achicarse) de n bytes, mapeado RW. */
int  lm_memfd(size_t n, void **map, const char *name);
int  lm_write_all(int fd, const void *p, size_t n);

/* VA-API: dispositivo de FFmpeg sobre el render node que pasó lord-server (o NULL). */
AVBufferRef *lm_vaapi_device(int render_fd);
/* Aceleración por nombre (parámetro hwaccel=vaapi|v4l2request|none): NULL = software. */
AVBufferRef *lm_hw_device(const char *accel, int fd);
/* El nombre pedido en los parámetros ("vaapi" si no dice; "none" si hw=0). */
const char *lm_hw_accel(const char *params, char *buf, size_t n);

/* JSON mínimo (escritura) */
typedef struct { char *d; size_t len, cap; } LmBuf;
void lm_buf_printf(LmBuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void lm_buf_str(LmBuf *b, const char *s);      /* cadena JSON con escapes */
void lm_buf_free(LmBuf *b);

/* Responde a lord-server. */
void lm_result(uint32_t id, int status, const char *payload, int fd);
void lm_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- imágenes (lm_image.c) ---- */
/* Decodifica el primer cuadro (imagen o video en pos_ms) a BGRA. */
AVFrame *lm_decode_still(AVFormatContext *fmt, int64_t pos_ms, int *orientation, int prefer_cover);
/* Escala/convierte un cuadro a BGRA (premultiplicado si tiene alfa) del tamaño pedido. */
AVFrame *lm_to_bgra(const AVFrame *src, int w, int h, int premultiply);
/* Codifica BGRA a png/jpeg en un fd. */
int  lm_encode_image(const AVFrame *bgra, const char *format, int quality, int out_fd);
void lm_probe_json(AVFormatContext *fmt, LmBuf *b);

/* ---- salida de cuadros a lord-server (lm_out.c) ---- */
void lm_out_init(int native);           /* native: tamaño original (sin escalar a la caja) */
int  lm_out_present(AVFrame *f, int64_t pts);   /* 0 presentado, 1 descartado (sin ranura), -1 error */
void lm_out_ack(uint32_t id);
int  lm_out_box(int w, int h);          /* MM_SIZE; 1 si cambió */
int  lm_out_redraw(void);
int  lm_out_has_box(void);
int  lm_out_has_last(void);
const AVFrame *lm_out_last(void);

/* ---- codificador H.264/AAC en MP4 (lm_enc.c) ---- */
typedef struct LmEnc LmEnc;
LmEnc *lm_enc_open(int out_fd, int w, int h, int fps, AVBufferRef *hw, int audio_rate, int audio_ch, char *err, size_t errlen);
int    lm_enc_video(LmEnc *e, const AVFrame *f, int64_t t_ms);  /* cualquier formato: se convierte */
int    lm_enc_audio(LmEnc *e, const int16_t *pcm, int samples);
int64_t lm_enc_close(LmEnc *e);         /* escribe el final; devuelve la duración en ms (o <0) */
const char *lm_enc_name(const LmEnc *e);

/* ---- micrófono: anillo que llena lord-server (lm_io.c) ---- */
struct ARingHdr;
/* Mapea y valida el anillo (mono, 48 kHz). NULL si no sirve. */
void *lm_mic_ring(int fd, size_t *len);
/* Llama a fn con lo que haya llegado (s16 mono). */
void lm_mic_drain(void *ring, void (*fn)(const int16_t *pcm, int samples));

/* ---- trabajos ---- */
void lm_worker(void);                                   /* lm_job.c */
void lm_player(const char *params, int fd, int pulse, int render);      /* lm_play.c */
void lm_screenrec(const char *params, int out, int pulse, int render);  /* lm_rec.c */
void lm_camera(const char *params, int dev, int render);                /* lm_cam.c */
void lm_audiorec(const char *params, int out, int pulse);               /* lm_rec.c */

#endif

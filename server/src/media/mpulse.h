/* mpulse.h — stream de reproducción o grabación de los medios por el protocolo de PulseAudio,
 * asíncrono, en lord-server (mpulse.c). */
#ifndef LORD_MEDIA_MPULSE_H
#define LORD_MEDIA_MPULSE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd, version, killed, underflows;
    int want, ready, failed;            /* 1 reproducir / 2 grabar; stream creado; PipeWire dijo que no */
    char app_name[64], app_id[64], media_name[128], role[32], source[256];
    uint32_t tag;
    uint8_t *in, *out;
    size_t in_len, in_cap, out_len, out_cap;
    /* esperando la respuesta de un comando */
    uint32_t wait_tag;
    int wait_what, wait_done;
    /* reproducción */
    uint32_t play_ch, play_index, frame_bytes;
    int rate, channels, corked;
    int64_t requested;                  /* bytes que pidió el servidor y no se mandaron */
    int64_t written;                    /* bytes mandados desde que se creó el stream */
    /* latencia (GET_PLAYBACK_LATENCY) */
    uint32_t lat_tag;
    int lat_pending, lat_valid, lat_playing;
    double lat_sent_ms, lat_at_ms;
    int64_t read_index, write_index;
    uint64_t sink_usec;
    /* grabación */
    uint32_t rec_ch, rec_index;
    void (*on_record)(void *ud, const uint8_t *d, size_t n);
    void *ud;
} LmPulse;

/* Arranca el saludo (no espera). Después lmp_playback o lmp_record dicen qué stream crear;
 * queda listo (ready) solo, a medida que llegan las respuestas en lmp_dispatch. */
int  lmp_connect(LmPulse *p, int fd, const char *app_name, const char *app_id);
int  lmp_playback(LmPulse *p, int rate, int ch, const char *media_name, const char *role);
int  lmp_record(LmPulse *p, const char *source, int rate, int ch, const char *media_name);
int  lmp_dispatch(LmPulse *p);          /* lee lo que llegó; -1 si se cortó */
int  lmp_write(LmPulse *p, const void *d, size_t n);
void lmp_cork(LmPulse *p, int on);
void lmp_flush(LmPulse *p);
void lmp_latency(LmPulse *p);
void lmp_volume(LmPulse *p, double v);  /* 0..1.5, en el stream (se ve en Ajustes → Sonido) */
void lmp_close(LmPulse *p);

#endif

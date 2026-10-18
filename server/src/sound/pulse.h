/* pulse.h — serializado del protocolo nativo de PulseAudio ("tagstruct" y paquetes), el que
 * habla pipewire-pulse. Sin libpulse: solo lo que usa snd.c. Ver docs/SONIDO.md. */
#ifndef LORD_PULSE_H
#define LORD_PULSE_H

#include <stddef.h>
#include <stdint.h>

#define PA_CHANNELS_MAX   32
#define PA_INVALID_INDEX  0xffffffffu
#define PA_VOLUME_NORM    0x10000u
#define PA_VOLUME_MAX     (PA_VOLUME_NORM * 3 / 2)   /* lo más que deja subir LordOS: 150 % */
#define PA_DESCRIPTOR_SIZE 20                        /* largo, canal, offset (2), flags */
#define PA_FRAME_MAX      (16u * 1024 * 1024)

/* Comandos que usamos (enum de pulsecore/native-common.h). */
enum {
    PA_CMD_ERROR = 0, PA_CMD_REPLY = 2,
    PA_CMD_CREATE_PLAYBACK_STREAM = 3, PA_CMD_DELETE_PLAYBACK_STREAM = 4,
    PA_CMD_CREATE_RECORD_STREAM = 5, PA_CMD_DELETE_RECORD_STREAM = 6,
    PA_CMD_AUTH = 8, PA_CMD_SET_CLIENT_NAME = 9,
    PA_CMD_CREATE_UPLOAD_STREAM = 15, PA_CMD_FINISH_UPLOAD_STREAM = 17, PA_CMD_PLAY_SAMPLE = 18,
    PA_CMD_GET_SERVER_INFO = 20,
    PA_CMD_GET_SINK_INFO = 21, PA_CMD_GET_SINK_INFO_LIST = 22,
    PA_CMD_GET_SOURCE_INFO = 23, PA_CMD_GET_SOURCE_INFO_LIST = 24,
    PA_CMD_GET_SINK_INPUT_INFO = 29, PA_CMD_GET_SINK_INPUT_INFO_LIST = 30,
    PA_CMD_GET_SOURCE_OUTPUT_INFO = 31, PA_CMD_GET_SOURCE_OUTPUT_INFO_LIST = 32,
    PA_CMD_GET_PLAYBACK_LATENCY = 14,
    PA_CMD_SUBSCRIBE = 35,
    PA_CMD_SET_SINK_VOLUME = 36, PA_CMD_SET_SINK_INPUT_VOLUME = 37, PA_CMD_SET_SOURCE_VOLUME = 38,
    PA_CMD_SET_SINK_MUTE = 39, PA_CMD_SET_SOURCE_MUTE = 40,
    PA_CMD_CORK_PLAYBACK_STREAM = 41, PA_CMD_FLUSH_PLAYBACK_STREAM = 42,
    PA_CMD_SET_DEFAULT_SINK = 44, PA_CMD_SET_DEFAULT_SOURCE = 45,
    PA_CMD_REQUEST = 61, PA_CMD_OVERFLOW = 62, PA_CMD_UNDERFLOW = 63,
    PA_CMD_PLAYBACK_STREAM_KILLED = 64, PA_CMD_RECORD_STREAM_KILLED = 65,
    PA_CMD_SUBSCRIBE_EVENT = 66, PA_CMD_STARTED = 86,
    PA_CMD_MOVE_SINK_INPUT = 67, PA_CMD_MOVE_SOURCE_OUTPUT = 68,
    PA_CMD_SET_SINK_INPUT_MUTE = 69,
    PA_CMD_SET_SINK_PORT = 96, PA_CMD_SET_SOURCE_PORT = 97,
    PA_CMD_SET_SOURCE_OUTPUT_VOLUME = 98, PA_CMD_SET_SOURCE_OUTPUT_MUTE = 99,
};

/* Subscripción: máscara pedida y facility/tipo del evento. */
enum {
    PA_SUB_MASK_SINK = 0x1, PA_SUB_MASK_SOURCE = 0x2, PA_SUB_MASK_SINK_INPUT = 0x4,
    PA_SUB_MASK_SOURCE_OUTPUT = 0x8, PA_SUB_MASK_SERVER = 0x80, PA_SUB_MASK_CARD = 0x200,
    PA_SUB_SINK = 0, PA_SUB_SOURCE = 1, PA_SUB_SINK_INPUT = 2, PA_SUB_SOURCE_OUTPUT = 3,
    PA_SUB_SERVER = 7, PA_SUB_CARD = 9, PA_SUB_FACILITY_MASK = 0xf,
    PA_SUB_NEW = 0x00, PA_SUB_CHANGE = 0x10, PA_SUB_REMOVE = 0x20, PA_SUB_TYPE_MASK = 0x30,
};

enum { PA_SAMPLE_S16LE = 3, PA_SAMPLE_FLOAT32LE = 5 };

/* ---- escritura ---- */
typedef struct {
    uint8_t *d;
    size_t len, cap;
    int err;                            /* sin memoria */
} TsOut;

void ts_init(TsOut *t);                 /* deja lugar para el descriptor del paquete */
void ts_free(TsOut *t);
void ts_u32(TsOut *t, uint32_t v);
void ts_u8(TsOut *t, uint8_t v);
void ts_u64(TsOut *t, uint64_t v);      /* 'U' (usec) */
void ts_bool(TsOut *t, int v);
void ts_timeval(TsOut *t, uint32_t sec, uint32_t usec);   /* 'T' (latencia, lord-media) */
void ts_str(TsOut *t, const char *s);   /* NULL → 'N' */
void ts_arb(TsOut *t, const void *p, uint32_t n);
void ts_ss(TsOut *t, uint8_t format, uint8_t channels, uint32_t rate);
void ts_cmap(TsOut *t, uint8_t n, const uint8_t *pos);
void ts_cvol(TsOut *t, uint8_t n, const uint32_t *v);
/* kv: pares clave, valor (cadenas; el valor viaja con su 0 final, como hace libpulse). */
void ts_proplist(TsOut *t, const char *const *kv, int pairs);
void ts_format_pcm(TsOut *t);           /* format info: PCM, sin propiedades */
/* Cierra el paquete de control (canal -1): completa el descriptor. Devuelve el largo total. */
size_t ts_finish(TsOut *t);
/* Paquete de datos (memblock) para un canal: descriptor + bytes, en out (malloc). */
uint8_t *ts_data_packet(uint32_t channel, const void *p, uint32_t n, size_t *total);

/* ---- lectura ---- */
typedef struct {
    const uint8_t *d;
    size_t len, pos;
    int err;                            /* tipo inesperado o fin de datos */
} TsIn;

typedef void (*TsPropFn)(void *ud, const char *key, const char *val, uint32_t len);

void     ts_in(TsIn *t, const uint8_t *d, size_t len);
int      ts_eof(const TsIn *t);
uint32_t ts_get_u32(TsIn *t);
uint8_t  ts_get_u8(TsIn *t);
uint64_t ts_get_u64(TsIn *t);           /* 'U', 'R' o 'r' */
int      ts_get_bool(TsIn *t);
const char *ts_get_str(TsIn *t);        /* apunta al búfer; NULL si 'N' o error */
const uint8_t *ts_get_arb(TsIn *t, uint32_t *n);
void     ts_get_ss(TsIn *t, uint8_t *format, uint8_t *channels, uint32_t *rate);
uint8_t  ts_get_cmap(TsIn *t, uint8_t *pos);           /* pos: PA_CHANNELS_MAX */
uint8_t  ts_get_cvol(TsIn *t, uint32_t *v);            /* v: PA_CHANNELS_MAX */
uint32_t ts_get_vol(TsIn *t);
void     ts_get_proplist(TsIn *t, TsPropFn fn, void *ud);
void     ts_skip_format(TsIn *t);
/* Cualquier valor (para saltar campos que no usamos). */
void     ts_skip(TsIn *t);

/* Descriptor: largo, canal, offset, flags (big endian). */
void pa_descriptor_read(const uint8_t *d, uint32_t *len, uint32_t *channel, uint64_t *offset, uint32_t *flags);

#endif

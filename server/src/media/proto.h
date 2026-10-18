/* proto.h — protocolo entre lord-server y su ayudante lord-media (ver docs/MEDIOS.md).
 *
 * Un socket SOCK_SEQPACKET por ayudante: cada paquete es un MediaMsg más una carga opcional
 * (texto "clave=valor" por línea o JSON corto) y hasta MP_MAX_FDS descriptores (SCM_RIGHTS).
 * lord-media nunca abre rutas: todo archivo, dispositivo o socket le llega ya abierto por acá.
 * Los cuadros de video viajan por memoria compartida (memfd sellado contra achicarse), no por
 * el socket. */
#ifndef LORD_MEDIA_PROTO_H
#define LORD_MEDIA_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define MP_MAX_FDS     4
#define MP_PAYLOAD_MAX 60000
#define MP_RING_SLOTS  3              /* triple búfer de cuadros */
#define MP_MAX_DIM     8192           /* lado máximo de un cuadro o una imagen */

typedef struct {
    uint32_t type;                    /* MM_* */
    uint32_t id;                      /* trabajo, cuadro (gen << 8 | ranura) o pista */
    int64_t  v[6];                    /* argumentos numéricos */
    uint32_t len;                     /* bytes de carga que siguen */
    uint32_t pad;
} MediaMsg;

/* Tipos de ayudante (primer mensaje, MM_JOB con v[0] = MK_*). */
enum {
    MK_PLAY = 1,        /* reproductor: fds = archivo, anillo de audio (aring.h, o -1), render node (o -1) */
    MK_WORKER,          /* trabajos sueltos en cola (MW_*), uno por mensaje MM_JOB */
    MK_SCREENREC,       /* grabación de pantalla: fds = salida mp4, anillo del micrófono (o -1), render node */
    MK_CAMERA,          /* cámara V4L2: fds = /dev/videoN, render node */
    MK_AUDIOREC,        /* grabadora: fds = salida wav, anillo del micrófono */
};

/* Trabajos del ayudante MK_WORKER (MM_JOB con v[0] = MW_*). */
enum {
    MW_PROBE = 1,       /* fds = archivo → JSON: duración, pistas, etiquetas */
    MW_THUMB,           /* fds = archivo, salida png; v[1] = lado máximo */
    MW_IMAGE,           /* fds = archivo → memfd BGRA premultiplicado (v: ancho, alto) */
    MW_EDIT,            /* fds = archivo, salida; params: recorte, giro, brillo, contraste, formato */
    MW_ENCODE,          /* fds = memfd BGRA, salida; v[1] = ancho, v[2] = alto; params: formato */
    MW_TRANSCODE,       /* fds = archivo, salida; params: formato (opus|mp3|wav), desde, hasta */
    MW_WAVEFORM,        /* fds = archivo; v[1] = cantidad de barras → JSON con picos 0..1000 */
    MW_SELFTEST,        /* prueba la caja de arena (abrir, red, procesos): todo tiene que fallar */
};

enum {
    /* lord-server → lord-media */
    MM_JOB = 1,         /* v[0] = MK_* o MW_*; carga = parámetros clave=valor */
    MM_SIZE,            /* v[0], v[1]: tamaño de la caja donde se muestra (px) */
    MM_PLAY,
    MM_PAUSE,
    MM_SEEK,            /* v[0] = ms */
    MM_VOLUME,          /* v[0] = 0..150 (%) */
    MM_TRACK,           /* v[0] = 'a' audio | 's' subtítulos, v[1] = índice de la pista (-1 ninguna) */
    MM_STOP,            /* terminar (grabaciones: cerrar el archivo bien) */
    MM_ACK,             /* id = cuadro que pasó a mostrarse: las otras ranuras quedan libres */
    MM_INRING,          /* fd = memfd de cuadros de entrada; v: ranuras, ancho, alto */
    MM_INFRAME,         /* id = ranura con un cuadro nuevo; v[0] = ms desde el inicio */
    MM_PHOTO,           /* cámara: fd = salida jpeg; v[0] = espejado */
    MM_RECORD,          /* cámara: fds = salida mp4, anillo del micrófono (o sin fd); sin fds: terminar */
    MM_LOOP,            /* v[0] = 1: volver a empezar al terminar */

    /* lord-media → lord-server */
    MM_INFO = 64,       /* carga = JSON con la información del archivo */
    MM_RING,            /* fd = memfd de cuadros; v[0] = gen, v[1] = ancho, v[2] = alto, v[3] = ranuras */
    MM_FRAME,           /* id = gen << 8 | ranura; v[0] = pts ms */
    MM_POS,             /* v[0] = posición ms, v[1] = duración ms, v[2] = reproduciendo */
    MM_STATE,           /* v[0] = MS_* */
    MM_SUB,             /* v[0] = desde ms, v[1] = hasta ms; carga = texto */
    MM_LEVEL,           /* v[0] = nivel 0..1000, v[1] = ms grabados */
    MM_STATS,           /* v: decodificados, mostrados, descartados, por hardware, ms de decodificación (x100) */
    MM_RESULT,          /* id = trabajo; v[0] = 0 bien / <0 error; carga = JSON o mensaje; fd opcional */
    MM_SLOTFREE,        /* id = ranura de entrada ya procesada */
    MM_ERROR,           /* carga = mensaje */
    MM_AUDIO,           /* hay PCM nuevo en el anillo de audio (o un seek: discard_to/serial) */
};

enum { MS_LOADING = 0, MS_PLAYING, MS_PAUSED, MS_ENDED, MS_RECORDING, MS_DONE, MS_FAILED };

/* Envía un mensaje (no bloquea: MSG_DONTWAIT). 0 bien, -1 error (EAGAIN incluido). */
int mp_send(int sock, uint32_t type, uint32_t id, const int64_t v[6], const void *payload, size_t len,
            const int *fds, int nfds);
/* Atajo sin carga ni descriptores. */
int mp_sendv(int sock, uint32_t type, uint32_t id, int64_t a, int64_t b, int64_t c, int64_t d);
/* Recibe un mensaje. payload recibe la carga terminada en 0 (cap >= MP_PAYLOAD_MAX + 1).
 * fds: hasta MP_MAX_FDS (con O_CLOEXEC), -1 en los que no llegaron. Devuelve 1 si llegó un
 * mensaje válido, 0 si el otro lado cerró, -1 si no hay nada (EAGAIN) o el mensaje es inválido
 * (en ese caso ya cerró los descriptores que hubieran llegado). block: espera. */
int mp_recv(int sock, MediaMsg *m, char *payload, size_t cap, int fds[MP_MAX_FDS], int block);

/* Parámetros "clave=valor\n": copia el valor de clave en out (0 si está). */
int mp_param(const char *params, const char *key, char *out, size_t n);
long long mp_param_int(const char *params, const char *key, long long def);
double mp_param_num(const char *params, const char *key, double def);

#endif

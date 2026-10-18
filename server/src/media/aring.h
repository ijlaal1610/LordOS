/* aring.h — audio entre lord-server y lord-media por memoria compartida (ver docs/MEDIOS.md).
 *
 * lord-media no habla con PipeWire: el socket de PipeWire (protocolo de PulseAudio) es una
 * capacidad muy amplia (cargar módulos, grabar otros streams, cambiar la salida), y un
 * decodificador comprometido no debe tenerla. Entonces:
 *   - reproducir: lord-media escribe PCM (s16, 48 kHz, estéreo) en este anillo; lord-server tiene
 *     el stream de PipeWire y copia del anillo cuando PipeWire pide (REQUEST), sin esperar.
 *   - el reloj: lord-server escribe en la misma página qué byte del anillo se está oyendo ahora
 *     (read_index − latencia de la salida) y en qué instante, con un seqlock. lord-media lo lee
 *     sin bloqueos ni idas y vueltas para sincronizar el video.
 *   - grabar (micrófono): al revés; lord-server tiene el stream de grabación (después del
 *     permiso) y escribe; lord-media lee.
 * El memfd lo crea lord-server, sellado (no se puede achicar ni agrandar). Solo un escritor y un
 * lector por anillo; las posiciones son contadores de bytes que no vuelven a cero. */
#ifndef LORD_MEDIA_ARING_H
#define LORD_MEDIA_ARING_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define AR_MAGIC  0x474e5241u          /* "ARNG" */
#define AR_RATE   48000
#define AR_HDR    512                  /* la cabecera ocupa esto; los datos van después */

typedef struct {
    uint32_t magic, rate, channels, size;       /* size: bytes de datos (potencia de 2) */
    _Atomic uint64_t wpos;                      /* bytes escritos (escritor) */
    _Atomic uint64_t rpos;                      /* bytes leídos (lector) */
    /* reproducir: lord-media pide descartar lo anterior a discard_to (seek) subiendo serial */
    _Atomic uint64_t discard_to;
    _Atomic uint32_t serial;
    _Atomic uint32_t overruns;                  /* grabar: bytes perdidos por anillo lleno (x1024) */
    /* reloj (lo escribe lord-server, seqlock: impar = escribiendo) */
    _Atomic uint32_t clk_seq;
    _Atomic int32_t  clk_valid, clk_playing;
    _Atomic int64_t  clk_pos;                   /* byte del anillo que se oye en clk_mono_us */
    _Atomic int64_t  clk_mono_us;               /* CLOCK_MONOTONIC */
    _Atomic int64_t  clk_latency_us;            /* latencia de la salida (informativo) */
} ARingHdr;

_Static_assert(sizeof(ARingHdr) <= AR_HDR, "cabecera del anillo");

static inline uint8_t *ar_data(ARingHdr *h) { return (uint8_t *)h + AR_HDR; }
static inline size_t ar_bytes(uint32_t size) { return AR_HDR + (size_t)size; }

/* Bytes disponibles para leer / lugar libre para escribir. */
static inline uint64_t ar_avail(ARingHdr *h) {
    return atomic_load_explicit(&h->wpos, memory_order_acquire) - atomic_load_explicit(&h->rpos, memory_order_acquire);
}
static inline uint64_t ar_space(ARingHdr *h) { return h->size - ar_avail(h); }

/* Escribe hasta n bytes (lo que entre). Devuelve cuántos. */
static inline size_t ar_write(ARingHdr *h, const void *p, size_t n) {
    uint64_t w = atomic_load_explicit(&h->wpos, memory_order_relaxed);
    uint64_t space = h->size - (w - atomic_load_explicit(&h->rpos, memory_order_acquire));
    if (n > space) n = (size_t)space;
    size_t off = (size_t)(w & (h->size - 1)), first = h->size - off < n ? h->size - off : n;
    memcpy(ar_data(h) + off, p, first);
    memcpy(ar_data(h), (const uint8_t *)p + first, n - first);
    atomic_store_explicit(&h->wpos, w + n, memory_order_release);
    return n;
}

/* Copia hasta n bytes desde la posición de lectura sin avanzarla. */
static inline size_t ar_peek(ARingHdr *h, void *p, size_t n) {
    uint64_t r = atomic_load_explicit(&h->rpos, memory_order_relaxed);
    uint64_t avail = atomic_load_explicit(&h->wpos, memory_order_acquire) - r;
    if (avail > h->size) avail = h->size;       /* un escritor roto no hace leer de más */
    if (n > avail) n = (size_t)avail;
    size_t off = (size_t)(r & (h->size - 1)), first = h->size - off < n ? h->size - off : n;
    memcpy(p, ar_data(h) + off, first);
    memcpy((uint8_t *)p + first, ar_data(h), n - first);
    return n;
}
static inline void ar_consume(ARingHdr *h, size_t n) { atomic_fetch_add_explicit(&h->rpos, n, memory_order_release); }
static inline size_t ar_read(ARingHdr *h, void *p, size_t n) { n = ar_peek(h, p, n); ar_consume(h, n); return n; }

/* Variantes para lord-server: el otro lado (lord-media, o una app) puede escribir la cabecera,
 * así que lord-server NO confía en size ni en las posiciones del otro: usa su propio tamaño y su
 * propio cursor, y acota todo. Lo peor que puede hacer el otro lado es perder o repetir audio. */
static inline size_t ar_write_t(ARingHdr *h, uint32_t size, uint64_t *wpos, const void *p, size_t n) {
    uint64_t r = atomic_load_explicit(&h->rpos, memory_order_acquire), w = *wpos;
    uint64_t used = (r > w || w - r > size) ? size : w - r;
    if (n > size - used) n = (size_t)(size - used);
    size_t off = (size_t)(w & (size - 1)), first = size - off < n ? size - off : n;
    memcpy(ar_data(h) + off, p, first);
    memcpy(ar_data(h), (const uint8_t *)p + first, n - first);
    *wpos = w + n;
    atomic_store_explicit(&h->wpos, *wpos, memory_order_release);
    return n;
}
static inline uint64_t ar_avail_t(ARingHdr *h, uint32_t size, uint64_t rpos) {
    uint64_t w = atomic_load_explicit(&h->wpos, memory_order_acquire);
    return (w < rpos || w - rpos > size) ? 0 : w - rpos;
}
static inline size_t ar_read_t(ARingHdr *h, uint32_t size, uint64_t *rpos, void *p, size_t n) {
    uint64_t avail = ar_avail_t(h, size, *rpos);
    if (n > avail) n = (size_t)avail;
    size_t off = (size_t)(*rpos & (size - 1)), first = size - off < n ? size - off : n;
    memcpy(p, ar_data(h) + off, first);
    memcpy((uint8_t *)p + first, ar_data(h), n - first);
    *rpos += n;
    atomic_store_explicit(&h->rpos, *rpos, memory_order_release);
    return n;
}

/* Reloj: escribe (lord-server) y lee (lord-media). */
static inline void ar_clock_write(ARingHdr *h, int valid, int playing, int64_t pos, int64_t mono_us, int64_t lat_us) {
    uint32_t s = atomic_load_explicit(&h->clk_seq, memory_order_relaxed);
    atomic_store_explicit(&h->clk_seq, s + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&h->clk_valid, valid, memory_order_relaxed);
    atomic_store_explicit(&h->clk_playing, playing, memory_order_relaxed);
    atomic_store_explicit(&h->clk_pos, pos, memory_order_relaxed);
    atomic_store_explicit(&h->clk_mono_us, mono_us, memory_order_relaxed);
    atomic_store_explicit(&h->clk_latency_us, lat_us, memory_order_relaxed);
    atomic_store_explicit(&h->clk_seq, s + 2, memory_order_release);
}

static inline int ar_clock_read(ARingHdr *h, int *playing, int64_t *pos, int64_t *mono_us) {
    for (int tries = 0; tries < 100; tries++) {
        uint32_t s0 = atomic_load_explicit(&h->clk_seq, memory_order_acquire);
        if (s0 & 1) continue;
        int valid = atomic_load_explicit(&h->clk_valid, memory_order_relaxed);
        *playing = atomic_load_explicit(&h->clk_playing, memory_order_relaxed);
        *pos = atomic_load_explicit(&h->clk_pos, memory_order_relaxed);
        *mono_us = atomic_load_explicit(&h->clk_mono_us, memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&h->clk_seq, memory_order_relaxed) == s0) return valid;
    }
    return 0;
}

#endif

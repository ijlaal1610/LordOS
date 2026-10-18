/* lm_io.c — E/S de lord-media sin rutas: FFmpeg lee y escribe por los descriptores que pasó
 * lord-server (AVIOContext propio), memfd sellados para los cuadros, VA-API sobre el render
 * node recibido y un JSON mínimo para las respuestas. */
#include "lm.h"
#include "../aring.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#ifdef LM_VAAPI
#include <libavutil/hwcontext_vaapi.h>
#include <va/va.h>
#include <va/va_drm.h>
#endif

double lm_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/* ---- entrada ---- */

typedef struct { int fd; int64_t pos, size; } FdIo;

static int io_read(void *op, uint8_t *buf, int n) {
    FdIo *f = op;
    ssize_t r;
    do r = pread(f->fd, buf, (size_t)n, f->pos);
    while (r < 0 && errno == EINTR);
    if (r < 0) return AVERROR(errno);
    if (r == 0) return AVERROR_EOF;
    f->pos += r;
    return (int)r;
}

static int io_write(void *op, const uint8_t *buf, int n) {
    FdIo *f = op;
    int done = 0;
    while (done < n) {
        ssize_t r = pwrite(f->fd, buf + done, (size_t)(n - done), f->pos);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return AVERROR(errno ? errno : EIO);
        f->pos += r;
        done += (int)r;
    }
    if (f->pos > f->size) f->size = f->pos;
    return n;
}

static int64_t io_seek(void *op, int64_t off, int whence) {
    FdIo *f = op;
    whence &= ~AVSEEK_FORCE;
    if (whence == AVSEEK_SIZE) return f->size;
    int64_t np = whence == SEEK_SET ? off : whence == SEEK_CUR ? f->pos + off : whence == SEEK_END ? f->size + off : -1;
    if (np < 0) return AVERROR(EINVAL);
    f->pos = np;
    return np;
}

AVFormatContext *lm_open_input(int fd, char *err, size_t errlen) {
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode)) { snprintf(err, errlen, "no es un archivo"); return NULL; }
    FdIo *f = av_mallocz(sizeof *f);
    unsigned char *buf = av_malloc(65536);
    if (!f || !buf) { av_free(f); av_free(buf); snprintf(err, errlen, "sin memoria"); return NULL; }
    f->fd = fd;
    f->size = st.st_size;
    AVIOContext *io = avio_alloc_context(buf, 65536, 0, f, io_read, NULL, io_seek);
    AVFormatContext *fmt = avformat_alloc_context();
    if (!io || !fmt) { snprintf(err, errlen, "sin memoria"); return NULL; }
    fmt->pb = io;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    /* nada de red ni de archivos auxiliares (listas m3u8, concat, imágenes "%d"): solo este fd */
    fmt->protocol_whitelist = av_strdup("none");
    fmt->format_whitelist = NULL;
    int r = avformat_open_input(&fmt, NULL, NULL, NULL);
    if (r < 0) {
        av_strerror(r, err, errlen);
        av_free(io->buffer);
        avio_context_free(&io);
        av_free(f);
        return NULL;
    }
    fmt->max_analyze_duration = 5 * AV_TIME_BASE;
    r = avformat_find_stream_info(fmt, NULL);
    if (r < 0) LM_DBG("find_stream_info: %d", r);
    return fmt;
}

void lm_close_input(AVFormatContext **fmt) {
    if (!*fmt) return;
    AVIOContext *io = (*fmt)->pb;
    void *op = io ? io->opaque : NULL;
    avformat_close_input(fmt);
    if (io) { av_freep(&io->buffer); avio_context_free(&io); }
    av_free(op);
}

/* ---- salida ---- */

AVFormatContext *lm_open_output(int fd, const char *format) {
    AVFormatContext *oc = NULL;
    if (avformat_alloc_output_context2(&oc, NULL, format, NULL) < 0 || !oc) return NULL;
    FdIo *f = av_mallocz(sizeof *f);
    unsigned char *buf = av_malloc(65536);
    struct stat st;
    if (!f || !buf || fstat(fd, &st)) { av_free(f); av_free(buf); avformat_free_context(oc); return NULL; }
    f->fd = fd;
    f->size = st.st_size;
    oc->pb = avio_alloc_context(buf, 65536, 1, f, NULL, io_write, S_ISREG(st.st_mode) ? io_seek : NULL);
    if (!oc->pb) { av_free(f); av_free(buf); avformat_free_context(oc); return NULL; }
    oc->flags |= AVFMT_FLAG_CUSTOM_IO;
    return oc;
}

int lm_close_output(AVFormatContext *oc, int write_trailer) {
    if (!oc) return -1;
    int r = 0;
    if (write_trailer) r = av_write_trailer(oc);
    AVIOContext *io = oc->pb;
    if (io) {
        avio_flush(io);
        FdIo *f = io->opaque;
        if (f && f->fd >= 0) { if (ftruncate(f->fd, f->size)) { /* el archivo ya tiene su largo */ } fsync(f->fd); }
        av_free(f);
        av_freep(&io->buffer);
        avio_context_free(&io);
    }
    avformat_free_context(oc);
    return r;
}

int lm_write_all(int fd, const void *p, size_t n) {
    const char *c = p;
    while (n) {
        ssize_t w = write(fd, c, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        c += w;
        n -= (size_t)w;
    }
    return 0;
}

int lm_memfd(size_t n, void **map, const char *name) {
    int fd = memfd_create(name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)n) || fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL)) { close(fd); return -1; }
    void *m = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { close(fd); return -1; }
    *map = m;
    return fd;
}

/* ---- VA-API ---- */

#ifdef LM_VAAPI
static void va_quiet(void *ctx, const char *msg) { (void)ctx; LM_DBG("va: %s", msg); }
static void va_free(AVHWDeviceContext *dc) {
    AVVAAPIDeviceContext *hw = dc->hwctx;
    if (hw->display) vaTerminate(hw->display);
}
#endif

/* Aceleración elegible: "vaapi" (Intel/AMD, render node), "v4l2request" (Raspberry Pi 5, HEVC
 * por V4L2 stateless; ver docs/RASPBERRY.md) o "none". Si no se puede, NULL: software. */
AVBufferRef *lm_hw_device(const char *accel, int fd) {
    if (!accel || !*accel || !strcmp(accel, "none") || fd < 0) return NULL;
    if (!strcmp(accel, "vaapi")) return lm_vaapi_device(fd);
    if (!strcmp(accel, "v4l2request")) {
        /* TODO(RPi5): el hwaccel v4l2-request solo existe en el FFmpeg de Raspberry Pi (+rpt) y su
         * dispositivo abre /dev/media* por ruta; lord-media no abre /dev: hace falta crearlo sobre
         * los fds que pase lord-server (DRM PRIME NV12/SAND128). Mientras tanto, software. */
        if (av_hwdevice_find_type_by_name("v4l2request") == AV_HWDEVICE_TYPE_NONE) LM_LOG("este FFmpeg no tiene v4l2request: software");
        else LM_LOG("v4l2request todavía no se crea sobre fds: software");
        return NULL;
    }
    LM_LOG("aceleración desconocida '%s': software", accel);
    return NULL;
}

AVBufferRef *lm_vaapi_device(int render_fd) {
#ifdef LM_VAAPI
    if (render_fd < 0) return NULL;
    VADisplay d = vaGetDisplayDRM(render_fd);
    if (!d) return NULL;
    vaSetErrorCallback(d, va_quiet, NULL);
    vaSetInfoCallback(d, va_quiet, NULL);
    int maj = 0, min = 0;
    if (vaInitialize(d, &maj, &min) != VA_STATUS_SUCCESS) { LM_DBG("VA-API: no hay driver para esta placa"); vaTerminate(d); return NULL; }
    AVBufferRef *ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
    if (!ref) { vaTerminate(d); return NULL; }
    AVHWDeviceContext *dc = (AVHWDeviceContext *)ref->data;
    AVVAAPIDeviceContext *hw = dc->hwctx;
    hw->display = d;
    dc->free = va_free;
    if (av_hwdevice_ctx_init(ref) < 0) { av_buffer_unref(&ref); return NULL; }
    LM_LOG("VA-API %d.%d: %s", maj, min, vaQueryVendorString(d));
    return ref;
#else
    (void)render_fd;
    return NULL;
#endif
}

/* ---- JSON ---- */

void lm_buf_printf(LmBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[512];
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (b->len + (size_t)n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 1024;
        while (cap < b->len + (size_t)n + 1) cap *= 2;
        char *d = realloc(b->d, cap);
        if (!d) return;
        b->d = d;
        b->cap = cap;
    }
    if ((size_t)n < sizeof tmp) memcpy(b->d + b->len, tmp, (size_t)n + 1);
    else { va_start(ap, fmt); vsnprintf(b->d + b->len, (size_t)n + 1, fmt, ap); va_end(ap); }
    b->len += (size_t)n;
}

void lm_buf_str(LmBuf *b, const char *s) {
    lm_buf_printf(b, "\"");
    for (; s && *s && b->len < MP_PAYLOAD_MAX - 64; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') lm_buf_printf(b, "\\%c", c);
        else if (c == '\n') lm_buf_printf(b, "\\n");
        else if (c < 0x20) lm_buf_printf(b, "\\u%04x", c);
        else lm_buf_printf(b, "%c", c);
    }
    lm_buf_printf(b, "\"");
}

void lm_buf_free(LmBuf *b) { free(b->d); b->d = NULL; b->len = b->cap = 0; }

void lm_result(uint32_t id, int status, const char *payload, int fd) {
    int64_t v[6] = { status, 0, 0, 0, 0, 0 };
    size_t len = payload ? strlen(payload) : 0;
    if (len > MP_PAYLOAD_MAX) len = MP_PAYLOAD_MAX;
    mp_send(LM_SOCK, MM_RESULT, id, v, payload, len, fd >= 0 ? &fd : NULL, fd >= 0 ? 1 : 0);
}

void lm_error(const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    LM_LOG("%s", msg);
    mp_send(LM_SOCK, MM_ERROR, 0, NULL, msg, strlen(msg), NULL, 0);
}

const char *lm_hw_accel(const char *params, char *buf, size_t n) {
    if (!mp_param_int(params, "hw", 1)) { snprintf(buf, n, "none"); return buf; }
    if (mp_param(params, "hwaccel", buf, n) || !buf[0]) snprintf(buf, n, "vaapi");
    return buf;
}

void *lm_mic_ring(int fd, size_t *len) {
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || st.st_size < AR_HDR || !(fcntl(fd, F_GET_SEALS) & F_SEAL_SHRINK)) return NULL;
    void *m = mmap(NULL, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) return NULL;
    ARingHdr *h = m;
    if (h->magic != AR_MAGIC || h->rate != AR_RATE || h->channels != 1 || !h->size || (h->size & (h->size - 1)) ||
        ar_bytes(h->size) > (size_t)st.st_size) { munmap(m, (size_t)st.st_size); return NULL; }
    *len = (size_t)st.st_size;
    return m;
}

void lm_mic_drain(void *ring, void (*fn)(const int16_t *pcm, int samples)) {
    if (!ring) return;
    static int16_t buf[8192];
    size_t n;
    while ((n = ar_read(ring, buf, sizeof buf) & ~(size_t)1) > 0) fn(buf, (int)(n / 2));
}

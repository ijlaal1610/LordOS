/* mirror.c — la sesión de lord-server vista desde el escritorio remoto.
 *
 * lord-server (ya como el usuario de la sesión) escucha en $XDG_RUNTIME_DIR/lordos/remote.sock;
 * solo acepta a root (lord-sessiond: el monitor y el puente de red) y al mismo usuario (SO_PEERCRED). Por ahí:
 *   - manda la pantalla por mosaicos de 64×64: en cada cuadro compara con la copia anterior y
 *     cada conexión acumula los mosaicos que cambiaron hasta que pide el próximo cuadro
 *     (RP_READY): así una red lenta recibe menos cuadros pero siempre el estado actual;
 *   - manda la posición y la imagen del cursor aparte, y el portapapeles de texto;
 *   - recibe mouse, teclado y portapapeles, que main.c inyecta como si fueran locales.
 * lord-sessiond (docs/SESIONES.md) se conecta como root y se marca con RP_SEAT: no recibe la
 * pantalla, solo los pedidos de la sesión (bloquear, cambiar de usuario, la pantalla de
 * ingreso), y le presta el monitor con RP_DISPLAY (la placa y la entrada como fds).
 * Protocolo: proto.h. */
#include "mirror.h"
#include "proto.h"
#include "../vault/vclient.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define TILE 64
#define MAX_CLIENTS 8
#define OUT_LIMIT (8u << 20)        /* con más pendiente que esto no se arma otro cuadro */

typedef struct {
    int fd;
    unsigned char *in, *out;
    size_t in_len, in_cap, out_len, out_off, out_cap;
    int ready, need_size, need_shape, cur_x, cur_y, cur_mode;
    unsigned clip_gen;
    unsigned char *dirty;           /* un byte por mosaico */
    char who[96];                   /* "ip usuario" (RP_HELLO) */
    int seat;                       /* lord-sessiond (RP_SEAT) */
    uid_t uid;                      /* de la otra punta (SO_PEERCRED) */
    int fds[40], nfds;              /* fds recibidos con el último mensaje (RP_DISPLAY) */
} Client;

static int lfd = -1;
static char sock_path[256];
static Client cl[MAX_CLIENTS];
static MirrorInput cb;
static uint32_t *fb;                /* última pantalla enviada (copia) */
static int W, H, TW, TH;
static int cur_x, cur_y, cur_mode;
static uint32_t *shape;
static int shape_w, shape_h, shape_hx, shape_hy;
static unsigned shape_gen;
static char *clip_text;
static unsigned clip_len, clip_gen_seen;

static void put(Client *c, const void *p, size_t n) {
    if (c->out_len + n > c->out_cap) {
        size_t cap = c->out_cap ? c->out_cap : 65536;
        while (cap < c->out_len + n) cap *= 2;
        unsigned char *nb = realloc(c->out, cap);
        if (!nb) return;
        c->out = nb;
        c->out_cap = cap;
    }
    memcpy(c->out + c->out_len, p, n);
    c->out_len += n;
}

static void msg(Client *c, int type, const void *a, size_t an, const void *b, size_t bn) {
    unsigned char h[5];
    uint32_t len = (uint32_t)(1 + an + bn);
    memcpy(h, &len, 4);
    h[4] = (unsigned char)type;
    put(c, h, 5);
    if (an) put(c, a, an);
    if (bn) put(c, b, bn);
}

static void flush(Client *c) {
    while (c->out_off < c->out_len) {
        ssize_t w = send(c->fd, c->out + c->out_off, c->out_len - c->out_off, MSG_NOSIGNAL);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        c->out_off += (size_t)w;
    }
    if (c->out_off == c->out_len) c->out_off = c->out_len = 0;
    else if (c->out_off > (1u << 20)) {               /* compactar */
        memmove(c->out, c->out + c->out_off, c->out_len - c->out_off);
        c->out_len -= c->out_off;
        c->out_off = 0;
    }
}

static void notify_clients(void) {
    if (!cb.clients) return;
    char json[1024] = "[";
    size_t o = 1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (cl[i].fd < 0 || !cl[i].who[0] || cl[i].seat) continue;
        char ip[64] = "", user[33] = "";
        sscanf(cl[i].who, "%63s %32s", ip, user);
        for (char *p = ip; *p; p++) if (*p == '"' || *p == '\\') *p = '?';
        for (char *p = user; *p; p++) if (*p == '"' || *p == '\\') *p = '?';
        o += (size_t)snprintf(json + o, sizeof json - o, "%s{\"ip\":\"%s\",\"user\":\"%s\"}", o > 1 ? "," : "", ip, user);
        if (o >= sizeof json - 80) break;
    }
    snprintf(json + o, sizeof json - o, "]");
    cb.clients(json);
}

static void close_fds(Client *c) {
    for (int i = 0; i < c->nfds; i++) if (c->fds[i] >= 0) close(c->fds[i]);
    c->nfds = 0;
}

static void drop(Client *c) {
    int had = c->who[0] != 0;
    close_fds(c);
    close(c->fd);
    free(c->in);
    free(c->out);
    free(c->dirty);
    memset(c, 0, sizeof *c);
    c->fd = -1;
    if (had) notify_clients();
}

static void all_dirty(Client *c) {
    free(c->dirty);
    c->dirty = TW && TH ? malloc((size_t)TW * TH) : NULL;
    if (c->dirty) memset(c->dirty, 1, (size_t)TW * TH);
}

/* Si la conexión pidió cuadro y hay mosaicos pendientes: los manda (corridas por fila) y RP_END. */
static void pump(Client *c) {
    if (c->fd < 0) return;
    if (c->seat) { flush(c); return; }         /* lord-sessiond: solo los pedidos (send_json) */
    if (c->need_size && W) {
        uint16_t s[2] = { (uint16_t)W, (uint16_t)H };
        msg(c, RP_SIZE, s, 4, NULL, 0);
        c->need_size = 0;
    }
    if (c->need_shape && shape) {
        uint16_t s[4] = { (uint16_t)shape_w, (uint16_t)shape_h, (uint16_t)shape_hx, (uint16_t)shape_hy };
        msg(c, RP_SHAPE, s, 8, shape, (size_t)shape_w * shape_h * 4);
        c->need_shape = 0;
    }
    if (c->cur_x != cur_x || c->cur_y != cur_y || c->cur_mode != cur_mode) {
        unsigned char m[5];
        int16_t x = (int16_t)cur_x, y = (int16_t)cur_y;
        memcpy(m, &x, 2);
        memcpy(m + 2, &y, 2);
        m[4] = (unsigned char)cur_mode;
        msg(c, RP_CURSOR, m, 5, NULL, 0);
        c->cur_x = cur_x;
        c->cur_y = cur_y;
        c->cur_mode = cur_mode;
    }
    if (clip_text && c->clip_gen != clip_gen_seen) {
        msg(c, RP_CLIP, clip_text, clip_len, NULL, 0);
        c->clip_gen = clip_gen_seen;
    }
    if (c->ready && c->dirty && fb && c->out_len - c->out_off < OUT_LIMIT) {
        int any = 0;
        for (int ty = 0; ty < TH; ty++) {
            for (int tx = 0; tx < TW;) {
                if (!c->dirty[ty * TW + tx]) { tx++; continue; }
                int t0 = tx;
                while (tx < TW && c->dirty[ty * TW + tx]) c->dirty[ty * TW + tx++] = 0;
                int x = t0 * TILE, y = ty * TILE;
                int w = tx * TILE > W ? W - x : tx * TILE - x;
                int h = y + TILE > H ? H - y : TILE;
                uint16_t r[4] = { (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h };
                unsigned char hdr[5];
                uint32_t len = (uint32_t)(1 + 8 + (size_t)w * h * 4);
                memcpy(hdr, &len, 4);
                hdr[4] = RP_RECT;
                put(c, hdr, 5);
                put(c, r, 8);
                for (int j = 0; j < h; j++) put(c, fb + (size_t)(y + j) * W + x, (size_t)w * 4);
                any = 1;
            }
        }
        if (any) {
            msg(c, RP_END, NULL, 0, NULL, 0);
            c->ready = 0;
        }
    }
    flush(c);
}

static void handle(Client *c, int type, const unsigned char *d, uint32_t n) {
    switch (type) {
    case RP_READY: c->ready = 1; break;
    case RP_PTR:
        if (n >= 4 && cb.pointer) { int16_t x, y; memcpy(&x, d, 2); memcpy(&y, d + 2, 2); cb.pointer(x, y); }
        break;
    case RP_BTN: if (n >= 2 && cb.button && d[0] <= 2) cb.button(d[0], d[1] != 0); break;
    case RP_WHEEL: if (n >= 1 && cb.wheel) cb.wheel((signed char)d[0]); break;
    case RP_SCROLL:
        if (n >= 9 && cb.scroll && d[0] >= 1 && d[0] <= 3) {
            int32_t v[2];
            memcpy(v, d + 1, 8);
            cb.scroll(d[0], v[0] / 256.0, v[1] / 256.0);
        }
        break;
    case RP_PINCH:
        if (n >= 9 && cb.pinch && d[0] >= 1 && d[0] <= 3) {
            int32_t v[2];
            memcpy(v, d + 1, 8);
            double sc = v[0] / 65536.0;
            if (sc > 0.05 && sc < 20) cb.pinch(d[0], sc, v[1] / 256.0);
        }
        break;
    case RP_KEY:
        if (n >= 3 && cb.key) {
            uint16_t code;
            memcpy(&code, d, 2);
            if (code > 0 && code < 0x2ff) cb.key(code, d[2] > 2 ? 1 : d[2]);
        }
        break;
    case RP_SETCLIP: if (cb.clipboard && n <= (1u << 20)) cb.clipboard((const char *)d, n); break;
    case RP_HELLO:
        snprintf(c->who, sizeof c->who, "%.*s", (int)(n < sizeof c->who - 1 ? n : sizeof c->who - 1), (const char *)d);
        for (char *p = c->who; *p; p++) if ((unsigned char)*p < 32) *p = ' ';
        notify_clients();
        break;
    case RP_SEAT:
        if (c->uid == 0) { c->seat = 1; c->ready = 0; notify_clients(); }
        break;
    case RP_DISPLAY:
        if (c->seat && c->nfds > 0 && cb.display) {
            cb.display(c->fds, c->nfds);            /* los fds pasan a ser de la sesión */
            c->nfds = 0;
        }
        break;
    case RP_INPUT_ADD:
        if (c->seat && c->nfds > 0 && cb.input_add) {
            cb.input_add(c->fds, c->nfds);          /* los fds pasan a ser de la sesión */
            c->nfds = 0;
        }
        break;
    case RP_UNDISPLAY:
        if (c->seat && cb.undisplay) cb.undisplay();
        break;
    case RP_RESIZE:
        if (n >= 4 && cb.resize) {
            uint16_t s[2];
            memcpy(s, d, 4);
            cb.resize(s[0], s[1]);
        }
        break;
    case RP_KEYRING:
        /* lord-sessiond: la contraseña recién verificada abre el llavero de la sesión (lord-vault) */
        if (c->uid == 0 && n > 0 && n <= 1024) {
            char p[1025];
            memcpy(p, d, n);
            p[n] = 0;
            vc_session_password(p);
            explicit_bzero(p, sizeof p);
        }
        explicit_bzero((void *)d, n);
        break;
    case RP_GREETER_IN:
        if (cb.greeter && c->seat && n < 65536) {
            char *j = malloc(n + 1);
            if (j) { memcpy(j, d, n); j[n] = 0; cb.greeter(j); free(j); }
        }
        break;
    default: break;
    }
}

static void read_client(Client *c) {
    for (;;) {
        if (c->in_cap - c->in_len < 65536) {
            size_t cap = c->in_cap ? c->in_cap * 2 : 131072;
            if (cap > (2u << 20)) cap = 2u << 20;
            if (cap <= c->in_cap) { drop(c); return; }
            unsigned char *nb = realloc(c->in, cap);
            if (!nb) { drop(c); return; }
            c->in = nb;
            c->in_cap = cap;
        }
        struct iovec iov = { c->in + c->in_len, c->in_cap - c->in_len };
        char cbuf[CMSG_SPACE(sizeof(int) * 40)];
        struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = cbuf, .msg_controllen = sizeof cbuf };
        ssize_t r = recvmsg(c->fd, &mh, MSG_CMSG_CLOEXEC);
        for (struct cmsghdr *cm = r > 0 ? CMSG_FIRSTHDR(&mh) : NULL; cm; cm = CMSG_NXTHDR(&mh, cm)) {
            if (cm->cmsg_level != SOL_SOCKET || cm->cmsg_type != SCM_RIGHTS) continue;
            int k = (int)((cm->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            for (int i = 0; i < k; i++) {
                int f;
                memcpy(&f, CMSG_DATA(cm) + i * sizeof(int), sizeof(int));
                /* solo root (lord-sessiond) pasa fds; de otros se descartan */
                if (c->uid == 0 && c->nfds < 40) c->fds[c->nfds++] = f; else close(f);
            }
        }
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && errno == EAGAIN) break;
        if (r <= 0) { drop(c); return; }
        c->in_len += (size_t)r;
        size_t off = 0;
        while (c->in_len - off >= 5) {
            uint32_t len;
            memcpy(&len, c->in + off, 4);
            if (len < 1 || len > (1u << 20) + 16) { drop(c); return; }
            if (c->in_len - off < 4 + (size_t)len) break;
            handle(c, c->in[off + 4], c->in + off + 5, len - 1);
            if (c->fd < 0) return;
            off += 4 + len;
        }
        close_fds(c);                       /* fds que no usó un RP_DISPLAY: fuera */
        memmove(c->in, c->in + off, c->in_len - off);
        c->in_len -= off;
    }
    pump(c);
}

int mirror_start(int w, int h, const MirrorInput *in) {
    if (in) cb = *in;
    for (int i = 0; i < MAX_CLIENTS; i++) cl[i].fd = -1;
    W = w;
    H = h;
    TW = (W + TILE - 1) / TILE;
    TH = (H + TILE - 1) / TILE;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt) return -1;
    char dir[200];
    snprintf(dir, sizeof dir, "%s/lordos", rt);
    mkdir(dir, 0700);
    struct stat st;
    if (lstat(dir, &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) return -1;
    snprintf(sock_path, sizeof sock_path, "%s/remote.sock", dir);
    unlink(sock_path);
    lfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    if (strlen(sock_path) >= sizeof a.sun_path) { close(lfd); lfd = -1; return -1; }
    memcpy(a.sun_path, sock_path, strlen(sock_path) + 1);
    mode_t old = umask(077);
    int r = lfd >= 0 ? bind(lfd, (struct sockaddr *)&a, sizeof a) : -1;
    umask(old);
    if (r || listen(lfd, 4)) {
        if (lfd >= 0) close(lfd);
        lfd = -1;
        return -1;
    }
    return 0;
}

void mirror_stop(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) if (cl[i].fd >= 0) drop(&cl[i]);
    if (lfd >= 0) { close(lfd); unlink(sock_path); }
    lfd = -1;
    free(fb);
    fb = NULL;
    free(shape);
    shape = NULL;
    free(clip_text);
    clip_text = NULL;
}

int mirror_clients(void) {
    int n = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) if (cl[i].fd >= 0 && !cl[i].seat) n++;
    return n;
}

int mirror_has_seat(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) if (cl[i].fd >= 0 && cl[i].seat) return 1;
    return 0;
}

static void send_json(int type, const char *json, int seat_only) {
    if (lfd < 0 || !json) return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (cl[i].fd < 0 || (seat_only && !cl[i].seat)) continue;
        msg(&cl[i], type, json, strlen(json), NULL, 0);
        pump(&cl[i]);
    }
}

void mirror_session(const char *json) {
    /* bloquear la pantalla o cambiar de usuario cierra la bóveda de Contraseñas (no el llavero) */
    if (strstr(json, "\"lock\"")) vc_lock_all("screen");
    else if (strstr(json, "\"switch\"")) vc_lock_all("switch");
    send_json(RP_SESSION, json, 0);
}
void mirror_greeter(const char *json) { send_json(RP_GREETER, json, 1); }

int mirror_pollfds(struct pollfd *p, int max) {
    int n = 0;
    if (lfd < 0 || max < 1) return 0;
    p[n++] = (struct pollfd){ lfd, POLLIN, 0 };
    for (int i = 0; i < MAX_CLIENTS && n < max; i++)
        if (cl[i].fd >= 0) p[n++] = (struct pollfd){ cl[i].fd, (short)(POLLIN | (cl[i].out_len > cl[i].out_off ? POLLOUT : 0)), 0 };
    return n;
}

static Client *by_fd(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) if (cl[i].fd == fd) return &cl[i];
    return NULL;
}

void mirror_dispatch(const struct pollfd *p, int n) {
    for (int i = 0; i < n; i++) {
        if (!p[i].revents) continue;
        if (p[i].fd == lfd) {
            int fd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (fd < 0) continue;
            struct ucred cr;
            socklen_t cl_len = sizeof cr;
            Client *slot = by_fd(-1);
            if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &cl_len) || (cr.uid != 0 && cr.uid != getuid()) || !slot) { close(fd); continue; }
            memset(slot, 0, sizeof *slot);
            slot->fd = fd;
            slot->uid = cr.uid;
            slot->ready = 1;
            slot->need_size = 1;
            slot->need_shape = shape != NULL;
            slot->cur_x = slot->cur_y = -1;
            slot->cur_mode = -1;
            all_dirty(slot);
            pump(slot);
            if (cb.refresh) cb.refresh();           /* que el próximo cuadro llegue entero */
            continue;
        }
        Client *c = by_fd(p[i].fd);
        if (!c) continue;
        if (p[i].revents & (POLLERR | POLLHUP)) { drop(c); continue; }
        if (p[i].revents & POLLOUT) { flush(c); pump(c); }
        if (c->fd >= 0 && (p[i].revents & POLLIN)) read_client(c);
    }
}

void mirror_frame(const uint32_t *px, int stride, int w, int h) {
    if (lfd < 0 || !mirror_clients()) { free(fb); fb = NULL; return; }
    int resized = !fb || w != W || h != H;
    int new_size = w != W || h != H;
    if (resized) {
        W = w;
        H = h;
        TW = (W + TILE - 1) / TILE;
        TH = (H + TILE - 1) / TILE;
        free(fb);
        fb = malloc((size_t)W * H * 4);
        if (!fb) return;
        for (int j = 0; j < H; j++) memcpy(fb + (size_t)j * W, px + (size_t)j * stride, (size_t)W * 4);
        for (int i = 0; i < MAX_CLIENTS; i++) if (cl[i].fd >= 0) { if (new_size) cl[i].need_size = 1; all_dirty(&cl[i]); }
    } else {
        for (int ty = 0; ty < TH; ty++)
            for (int tx = 0; tx < TW; tx++) {
                int x = tx * TILE, y = ty * TILE;
                int tw = x + TILE > W ? W - x : TILE, th = y + TILE > H ? H - y : TILE;
                int changed = 0;
                for (int j = 0; j < th && !changed; j++)
                    changed = memcmp(fb + (size_t)(y + j) * W + x, px + (size_t)(y + j) * stride + x, (size_t)tw * 4) != 0;
                if (!changed) continue;
                for (int j = 0; j < th; j++)
                    memcpy(fb + (size_t)(y + j) * W + x, px + (size_t)(y + j) * stride + x, (size_t)tw * 4);
                for (int i = 0; i < MAX_CLIENTS; i++) if (cl[i].fd >= 0 && cl[i].dirty) cl[i].dirty[ty * TW + tx] = 1;
            }
    }
    for (int i = 0; i < MAX_CLIENTS; i++) pump(&cl[i]);
}

void mirror_cursor(int x, int y, int mode, const uint32_t *argb, int w, int h, int hx, int hy, unsigned gen) {
    if (lfd < 0 || !mirror_clients()) return;
    cur_x = x;
    cur_y = y;
    cur_mode = mode;
    if (mode == 1 && argb && gen != shape_gen && w > 0 && h > 0 && w <= 256 && h <= 256) {
        uint32_t *s = malloc((size_t)w * h * 4);
        if (s) {
            memcpy(s, argb, (size_t)w * h * 4);
            free(shape);
            shape = s;
            shape_w = w; shape_h = h; shape_hx = hx; shape_hy = hy;
            shape_gen = gen;
            for (int i = 0; i < MAX_CLIENTS; i++) if (cl[i].fd >= 0) cl[i].need_shape = 1;
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++) pump(&cl[i]);
}

void mirror_clipboard(const char *text, unsigned gen) {
    if (lfd < 0 || gen == clip_gen_seen || !text) return;
    size_t n = strlen(text);
    if (n > (1u << 20)) n = 1u << 20;
    char *t = malloc(n + 1);
    if (!t) return;
    memcpy(t, text, n);
    t[n] = 0;
    free(clip_text);
    clip_text = t;
    clip_len = (unsigned)n;
    clip_gen_seen = gen;
    for (int i = 0; i < MAX_CLIENTS; i++) pump(&cl[i]);
}

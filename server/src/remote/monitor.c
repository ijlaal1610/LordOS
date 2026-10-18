/* monitor.c — el monitor en lord-sessiond: le presta la pantalla a UNA sesión a la vez.
 *
 * Como logind con los "session devices": lord-sessiond (root) abre la placa de video y el
 * teclado y el mouse, y le pasa los fds a la sesión que se tiene que ver (RP_DISPLAY por el
 * socket de la sesión, con SCM_RIGHTS). La sesión dibuja directo en la placa, como un
 * lord-server de siempre: composición por GPU si hay (Mesa), scanout directo de pantalla
 * completa y cursor por hardware. Para cambiar de sesión:
 *   1. a la que se va se le avisa (RP_UNDISPLAY);
 *   2. se revocan sus dispositivos de entrada (EVIOCREVOKE: sus fds dejan de servir);
 *   3. se le saca el DRM master (DROP_MASTER en nuestra copia del mismo archivo abierto);
 *   4. la nueva recibe una placa abierta de nuevo, ya master, y entrada nueva.
 * Las sesiones nunca abren dispositivos ni son root. Ver docs/SESIONES.md. */
#include "sessiond.h"
#include "proto.h"
#include "../drmcard.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <drm/drm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

/* la tarjeta con pantalla (en la Pi 5 suele ser card1: card0 es v3d, sin conectores; drmcard.h) */
static char card[64] = "/dev/dri/card0";
#define CARD card
#define MAX_IN 32

static int vt = -1, conn = -1, drm = -1;
static int ins[MAX_IN], nin;
static char inname[MAX_IN][16];        /* "eventN" de cada uno (para saber cuál se desenchufó) */
static int hp = -1;                    /* inotify de /dev/input (enchufar en caliente) */
static unsigned char *rb;
static size_t rlen, rcap, skip;        /* skip: bytes de un mensaje grande que se descarta */

#define LONG_BITS (8 * sizeof(long))
#define NLONGS(n) (((n) + LONG_BITS - 1) / LONG_BITS)
#define HAS(bits, n) (((bits)[(n) / LONG_BITS] >> ((n) % LONG_BITS)) & 1UL)

/* teclado, mouse o tablet (lo mismo que input.c) */
static int useful(int fd) {
    unsigned long ev[NLONGS(EV_MAX + 1)] = {0}, keys[NLONGS(KEY_MAX + 1)] = {0}, abs[NLONGS(ABS_MAX + 1)] = {0};
    ioctl(fd, EVIOCGBIT(0, sizeof ev), ev);
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);
    return (HAS(ev, EV_KEY) && HAS(keys, KEY_A) && HAS(keys, KEY_ENTER)) || (HAS(ev, EV_REL) && HAS(keys, BTN_LEFT)) ||
           (HAS(ev, EV_ABS) && HAS(abs, ABS_X) && HAS(abs, ABS_Y));
}

int sd_mon_open(void) {
    if (drm_pick_card(card, sizeof card)) sd_log("ninguna tarjeta con KMS; se prueba %s", card);
    else sd_log("pantalla: %s", card);
    int fd = open(CARD, O_RDWR | O_CLOEXEC);
    if (fd < 0) { sd_log("sin monitor (%s: %s)", CARD, strerror(errno)); return -1; }
    close(fd);
    hp = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (hp >= 0 && inotify_add_watch(hp, "/dev/input", IN_CREATE | IN_ATTRIB | IN_DELETE) < 0) { close(hp); hp = -1; }
    if (hp < 0) sd_log("sin inotify en /dev/input: los dispositivos nuevos se verán al cambiar de sesión");
    /* la consola de texto deja de dibujar encima */
    vt = open("/dev/tty0", O_RDWR | O_CLOEXEC);
    if (vt >= 0 && ioctl(vt, KDSETMODE, KD_GRAPHICS)) { close(vt); vt = -1; }
    return 0;
}

static int send_msg(int type, const void *d, size_t n, const int *fds, int nfds) {
    if (conn < 0) return -1;
    unsigned char h[5];
    uint32_t len = (uint32_t)(1 + n);
    memcpy(h, &len, 4);
    h[4] = (unsigned char)type;
    struct iovec iov[2] = { { h, 5 }, { (void *)d, n } };
    char cbuf[CMSG_SPACE(sizeof(int) * (MAX_IN + 1))];
    memset(cbuf, 0, sizeof cbuf);
    struct msghdr mh = { .msg_iov = iov, .msg_iovlen = n ? 2 : 1 };
    if (nfds > 0) {
        mh.msg_control = cbuf;
        mh.msg_controllen = CMSG_SPACE(sizeof(int) * (size_t)nfds);
        struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(sizeof(int) * (size_t)nfds);
        memcpy(CMSG_DATA(cm), fds, sizeof(int) * (size_t)nfds);
    }
    ssize_t r;
    while ((r = sendmsg(conn, &mh, MSG_NOSIGNAL)) < 0 && errno == EINTR) {}
    return r == (ssize_t)(5 + n) ? 0 : -1;
}

/* Le saca el monitor a la sesión que lo tiene. */
static void take_back(void) {
    if (conn >= 0) send_msg(RP_UNDISPLAY, NULL, 0, NULL, 0);
    for (int i = 0; i < nin; i++) {
        ioctl(ins[i], EVIOCREVOKE, NULL);
        close(ins[i]);
    }
    nin = 0;
    if (drm >= 0) {
        ioctl(drm, DRM_IOCTL_DROP_MASTER, NULL);
        close(drm);
    }
    drm = -1;
    if (conn >= 0) close(conn);
    conn = -1;
    rlen = skip = 0;
}

void sd_mon_close(void) {
    take_back();
    if (vt >= 0) { ioctl(vt, KDSETMODE, KD_TEXT); close(vt); }
    vt = -1;
}

int sd_mon_fd(void) { return conn; }

int sd_mon_show(int fd, const char *who) {
    take_back();
    conn = fd;
    drm = open(CARD, O_RDWR | O_CLOEXEC);
    if (drm < 0) { sd_log("%s: %s", CARD, strerror(errno)); return -1; }
    if (ioctl(drm, DRM_IOCTL_SET_MASTER, NULL)) sd_log("aviso: DRM master: %s", strerror(errno));
    for (int i = 0; i < 64 && nin < MAX_IN; i++) {
        char p[32];
        snprintf(p, sizeof p, "/dev/input/event%d", i);
        int f = open(p, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (f < 0) continue;
        if (useful(f)) { snprintf(inname[nin], sizeof inname[nin], "event%d", i); ins[nin++] = f; } else close(f);
    }
    int fds[MAX_IN + 1];
    fds[0] = drm;
    memcpy(fds + 1, ins, sizeof(int) * (size_t)nin);
    char hello[64];
    snprintf(hello, sizeof hello, "monitor %s", who);
    unsigned char count = (unsigned char)(nin + 1);
    if (send_msg(RP_SEAT, NULL, 0, NULL, 0) || send_msg(RP_HELLO, hello, strlen(hello), NULL, 0) ||
        send_msg(RP_DISPLAY, &count, 1, fds, nin + 1)) {
        sd_log("la sesión no recibió el monitor");
        return -1;
    }
    return 0;
}

void sd_mon_greeter(const char *json) { send_msg(RP_GREETER_IN, json, strlen(json), NULL, 0); }

/* Pedidos de la sesión que se ve (RP_SESSION, RP_GREETER: JSON de hasta 4 KB). Lo demás se
 * descarta sin mirarlo. -1 si la sesión se cerró. */
int sd_mon_read(void (*fn)(int type, const char *json)) {
    for (;;) {
        if (rcap - rlen < 8192) {
            size_t cap = rcap ? rcap * 2 : 65536;
            if (cap > (1u << 20)) return -1;
            unsigned char *nb = realloc(rb, cap);
            if (!nb) return -1;
            rb = nb;
            rcap = cap;
        }
        ssize_t n = recv(conn, rb + rlen, rcap - rlen, MSG_DONTWAIT);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno == EAGAIN) break;
        if (n <= 0) return -1;
        rlen += (size_t)n;
        size_t off = 0;
        if (skip) {                         /* resto de un mensaje que no interesa */
            size_t k = skip < rlen ? skip : rlen;
            skip -= k;
            off = k;
        }
        while (rlen - off >= 5) {
            uint32_t len;
            memcpy(&len, rb + off, 4);
            if (!len || len > RP_MAX_MSG) return -1;
            if (len > 8192) {               /* pantalla (antes de que la sesión lea RP_SEAT): se salta */
                size_t have = rlen - off;
                if (have >= 4 + (size_t)len) { off += 4 + len; continue; }
                skip = 4 + (size_t)len - have;
                off = rlen;
                break;
            }
            if (rlen - off < 4 + (size_t)len) break;
            int type = rb[off + 4];
            if ((type == RP_SESSION || type == RP_GREETER) && len - 1 < 4096) {
                char j[4096];
                memcpy(j, rb + off + 5, len - 1);
                j[len - 1] = 0;
                if (!memchr(j, 0, len - 1)) fn(type, j);
                explicit_bzero(j, sizeof j);
            }
            off += 4 + len;
        }
        explicit_bzero(rb, off);
        memmove(rb, rb + off, rlen - off);
        rlen -= off;
    }
    return 0;
}

void sd_mon_lost(void) { take_back(); }

/* ---- enchufar en caliente ----
 * Un teclado, mouse o touchpad nuevo se abre acá (root) y se le pasa SOLO a la sesión que está en
 * el monitor (RP_INPUT_ADD); las de segundo plano no reciben nada y lo ven al volver al monitor
 * (sd_mon_show abre todo de nuevo). Al desenchufarlo se cierra nuestra copia; la sesión ve
 * ENODEV en la suya y la cierra sola, sin dejar el monitor. */
int sd_mon_hotplug_fd(void) { return hp; }

static int is_event(const char *n) {
    if (strncmp(n, "event", 5) || !n[5]) return 0;
    for (const char *p = n + 5; *p; p++) if (*p < '0' || *p > '9') return 0;
    return strlen(n) < 16;
}

static void plug(const char *name) {
    for (int i = 0; i < nin; i++) if (!strcmp(inname[i], name)) return;     /* ya está */
    if (conn < 0 || drm < 0 || nin >= MAX_IN) return;                         /* nadie en el monitor */
    char p[40];
    snprintf(p, sizeof p, "/dev/input/%s", name);
    int f = open(p, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (f < 0) return;
    if (!useful(f)) { close(f); return; }
    unsigned char count = 1;
    if (send_msg(RP_INPUT_ADD, &count, 1, &f, 1)) { close(f); return; }
    snprintf(inname[nin], sizeof inname[nin], "%s", name);
    ins[nin++] = f;
    char dev[80] = "";
    ioctl(f, EVIOCGNAME(sizeof dev), dev);
    sd_log("entrada enchufada: %s (%s)", name, dev);
}

static void unplug(const char *name) {
    for (int i = 0; i < nin; i++) {
        if (strcmp(inname[i], name)) continue;
        close(ins[i]);
        ins[i] = ins[nin - 1];
        memcpy(inname[i], inname[nin - 1], sizeof inname[i]);
        nin--;
        sd_log("entrada quitada: %s", name);
        return;
    }
}

void sd_mon_hotplug(void) {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (;;) {
        ssize_t n = read(hp, buf, sizeof buf);
        if (n <= 0) break;
        for (char *p = buf; p < buf + n;) {
            const struct inotify_event *e = (const struct inotify_event *)p;
            if (e->len && is_event(e->name)) {
                if (e->mask & IN_DELETE) unplug(e->name);
                else plug(e->name);
            }
            p += sizeof *e + e->len;
        }
    }
}

/* En un proceso hijo (conexión web, trabajador): suelta las copias de la placa, la entrada y el
 * socket del monitor sin tocarlos (nunca quedan en un proceso que después deja de ser root). */
void sd_mon_child(void) {
    for (int i = 0; i < nin; i++) close(ins[i]);
    nin = 0;
    if (drm >= 0) close(drm);
    if (conn >= 0) close(conn);
    if (vt >= 0) close(vt);
    if (hp >= 0) close(hp);
    drm = conn = vt = hp = -1;
}

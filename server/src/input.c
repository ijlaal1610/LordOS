/* input.c — detección de dispositivos evdev y lectura de eventos. */
#include "input.h"
#include "tp/tp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define LONG_BITS (8 * sizeof(long))
#define NLONGS(n) (((n) + LONG_BITS - 1) / LONG_BITS)
#define HAS(bits, n) (((bits)[(n) / LONG_BITS] >> ((n) % LONG_BITS)) & 1UL)

/* Agrega el dispositivo si es teclado, mouse o tablet (y lo toma); si no, lo cierra. */
static void add_device(Input *in, int fd, const char *path) {
    unsigned long ev[NLONGS(EV_MAX + 1)] = {0};
    unsigned long keys[NLONGS(KEY_MAX + 1)] = {0};
    unsigned long abs[NLONGS(ABS_MAX + 1)] = {0};
    ioctl(fd, EVIOCGBIT(0, sizeof ev), ev);
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);

    int keyboard = HAS(ev, EV_KEY) && HAS(keys, KEY_A) && HAS(keys, KEY_ENTER);
    int mouse    = HAS(ev, EV_REL) && HAS(keys, BTN_LEFT);
    int tablet   = HAS(ev, EV_ABS) && HAS(abs, ABS_X) && HAS(abs, ABS_Y);
    if ((!keyboard && !mouse && !tablet) || in->count >= INPUT_MAX_DEVICES) { close(fd); return; }

    InputDevice *d = &in->dev[in->count++];
    memset(d, 0, sizeof *d);                /* el lugar pudo ser de uno que se desenchufó */
    d->fd = fd;
    ioctl(fd, EVIOCGNAME(sizeof d->name), d->name);
    unsigned long rel[NLONGS(REL_MAX + 1)] = {0};
    ioctl(fd, EVIOCGBIT(EV_REL, sizeof rel), rel);
    d->wheel_hires = HAS(rel, REL_WHEEL_HI_RES);
    d->hwheel_hires = HAS(rel, REL_HWHEEL_HI_RES);
    /* touchpad o pantalla táctil (multitouch): los maneja el driver propio (tp/tp.c) */
    d->tp = tp_open(fd, d->name);
    if (d->tp) tablet = 0;
    if (tablet) {
        d->has_abs = 1;
        ioctl(fd, EVIOCGABS(ABS_X), &d->ax);
        ioctl(fd, EVIOCGABS(ABS_Y), &d->ay);
    }
    /* Exclusivo: las teclas no llegan a la consola de texto de abajo. */
    if (ioctl(fd, EVIOCGRAB, 1))
        fprintf(stderr, "input: no se pudo tomar %s: %s\n", d->name, strerror(errno));
    fprintf(stderr, "input: %s -> %s%s%s\n", path, d->name,
            keyboard ? " [teclado]" : "", d->tp ? (tp_kind(d->tp) == TP_KIND_TOUCHSCREEN ? " [pantalla táctil]" : " [touchpad]")
            : mouse ? " [mouse]" : (tablet ? " [tablet]" : ""));
}

int input_open(Input *in) {
    memset(in, 0, sizeof *in);
    for (int i = 0; i < 64 && in->count < INPUT_MAX_DEVICES; i++) {
        char path[32];
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) add_device(in, fd, path);
    }
    return in->count > 0 ? 0 : -1;
}

int input_open_fds(Input *in, const int *fds, int n) {
    memset(in, 0, sizeof *in);
    for (int i = 0; i < n; i++) {
        fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
        add_device(in, fds[i], "(lord-sessiond)");
    }
    return in->count > 0 ? 0 : -1;
}

int input_add_fds(Input *in, const int *fds, int n) {
    int before = in->count;
    for (int i = 0; i < n; i++) {
        fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
        add_device(in, fds[i], "(enchufado)");
    }
    return in->count - before;
}

void input_remove(Input *in, int idx) {
    if (idx < 0 || idx >= in->count) return;
    InputDevice *d = &in->dev[idx];
    fprintf(stderr, "input: %s ya no está; se cierra\n", d->name);
    close(d->fd);
    tp_free(d->tp);
    memmove(&in->dev[idx], &in->dev[idx + 1], sizeof in->dev[0] * (size_t)(in->count - idx - 1));
    in->count--;
}

void input_close(Input *in) {
    for (int i = 0; i < in->count; i++) {
        ioctl(in->dev[i].fd, EVIOCGRAB, 0);
        close(in->dev[i].fd);
        tp_free(in->dev[i].tp);
        in->dev[i].tp = NULL;
    }
    in->count = 0;
}

void input_dispatch(Input *in, int idx, InputHandler cb, void *ud) {
    InputDevice *d = &in->dev[idx];
    struct input_event evs[64];
    for (;;) {
        ssize_t n = read(d->fd, evs, sizeof evs);
        if (n <= 0) break;
        for (size_t i = 0; i < (size_t)n / sizeof evs[0]; i++) cb(ud, d, &evs[i]);
    }
}

/* mirror.h — the session as seen from the remote desktop (see mirror.c and docs/REMOTE.md). */
#ifndef LORD_REMOTE_MIRROR_H
#define LORD_REMOTE_MIRROR_H

#include <poll.h>
#include <stdint.h>

/* What arrives from the browser, already translated: main.c injects it as if it were the local
 * mouse and keyboard. id identifies the connection (for the notices). */
typedef struct {
    void (*pointer)(int x, int y);
    void (*button)(int button, int down);
    void (*wheel)(int steps);
    void (*key)(int code, int value);
    void (*clipboard)(const char *text, unsigned len);
    void (*clients)(const char *json);      /* list of connected clients changed: [{"ip","user"}] */
    void (*refresh)(void);                  /* a full frame is needed (new connection) */
    void (*resize)(int w, int h);           /* request for another screen size (RP_RESIZE) */
    void (*greeter)(const char *json);      /* notice from lord-sessiond to the login screen */
    /* lord-sessiond lends the monitor: fds[0] the card (already master), the rest keyboards and mice
     * (they now belong to the session). undisplay: gives it back. */
    void (*display)(const int *fds, int n);
    void (*undisplay)(void);
    /* browser trackpad: scroll in pixels (phase 1 begins, 2 continues, 3 ends) and pinch
     * (ctrl+wheel or the gesturechange of WebKit browsers; scale relative to the start, rotation in degrees) */
    void (*scroll)(int phase, double dx, double dy);
    void (*pinch)(int phase, double scale, double rotation);
    /* lord-sessiond: input devices plugged in later (the fds now belong to the session) */
    void (*input_add)(const int *fds, int n);
} MirrorInput;

/* After dropping privileges. Creates $XDG_RUNTIME_DIR/lordos/remote.sock. 0 on success. */
int  mirror_start(int w, int h, const MirrorInput *in);
void mirror_stop(void);
int  mirror_pollfds(struct pollfd *p, int max);       /* adds its fds; returns how many */
void mirror_dispatch(const struct pollfd *p, int n);  /* the same ones, with revents */
int  mirror_clients(void);                            /* connections watching the screen (not lord-sessiond) */
int  mirror_has_seat(void);                           /* is lord-sessiond (monitor) connected? */
/* Requests from the session to whoever shows it (short JSON): RP_SESSION to every connection
 * ({"a":"lock"}: lock; {"a":"switch"}: switch user) and RP_GREETER to the monitor. */
void mirror_session(const char *json);
void mirror_greeter(const char *json);

/* A new screen frame (without the cursor). Compares it tile by tile with the previous one and sends
 * only what changed to each connection that is ready. */
void mirror_frame(const uint32_t *px, int stride, int w, int h);
/* Cursor: position and mode (0 arrow, 1 app image, 2 hidden); the image only if gen changed. */
void mirror_cursor(int x, int y, int mode, const uint32_t *argb, int w, int h, int hx, int hy, unsigned gen);
/* Session text clipboard (called on every loop iteration; sends only if it changed). */
void mirror_clipboard(const char *text, unsigned gen);

#endif

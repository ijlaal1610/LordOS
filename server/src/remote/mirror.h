/* mirror.h — la sesión vista desde el escritorio remoto (ver mirror.c y docs/REMOTO.md). */
#ifndef LORD_REMOTE_MIRROR_H
#define LORD_REMOTE_MIRROR_H

#include <poll.h>
#include <stdint.h>

/* Lo que llega del navegador, ya traducido: main.c lo inyecta como si fuera el mouse y el teclado
 * locales. id identifica la conexión (para los avisos). */
typedef struct {
    void (*pointer)(int x, int y);
    void (*button)(int button, int down);
    void (*wheel)(int steps);
    void (*key)(int code, int value);
    void (*clipboard)(const char *text, unsigned len);
    void (*clients)(const char *json);      /* lista de conectados cambió: [{"ip","user"}] */
    void (*refresh)(void);                  /* hace falta un cuadro entero (conexión nueva) */
    void (*resize)(int w, int h);           /* pedido de otro tamaño de pantalla (RP_RESIZE) */
    void (*greeter)(const char *json);      /* aviso de lord-sessiond a la pantalla de ingreso */
    /* lord-sessiond presta el monitor: fds[0] la placa (ya master), el resto teclados y mouses
     * (pasan a ser de la sesión). undisplay: lo devuelve. */
    void (*display)(const int *fds, int n);
    void (*undisplay)(void);
    /* trackpad del navegador: scroll en píxeles (fase 1 empieza, 2 sigue, 3 termina) y pellizco
     * (ctrl+rueda o gesturechange de Safari; escala relativa al comienzo, giro en grados) */
    void (*scroll)(int phase, double dx, double dy);
    void (*pinch)(int phase, double scale, double rotation);
    /* lord-sessiond: dispositivos de entrada enchufados después (los fds pasan a ser de la sesión) */
    void (*input_add)(const int *fds, int n);
} MirrorInput;

/* Después de soltar privilegios. Crea $XDG_RUNTIME_DIR/lordos/remote.sock. 0 bien. */
int  mirror_start(int w, int h, const MirrorInput *in);
void mirror_stop(void);
int  mirror_pollfds(struct pollfd *p, int max);       /* agrega sus fds; devuelve cuántos */
void mirror_dispatch(const struct pollfd *p, int n);  /* los mismos, con revents */
int  mirror_clients(void);                            /* conexiones que miran la pantalla (no lord-sessiond) */
int  mirror_has_seat(void);                           /* ¿está conectado lord-sessiond (monitor)? */
/* Pedidos de la sesión a quien la muestra (JSON corto): RP_SESSION a todas las conexiones
 * ({"a":"lock"}: bloquear; {"a":"switch"}: cambiar de usuario) y RP_GREETER al monitor. */
void mirror_session(const char *json);
void mirror_greeter(const char *json);

/* Un cuadro nuevo de la pantalla (sin el cursor). Compara por mosaicos con el anterior y manda
 * solo lo que cambió a cada conexión que esté lista. */
void mirror_frame(const uint32_t *px, int stride, int w, int h);
/* Cursor: posición y modo (0 flecha, 1 imagen de la app, 2 oculto); la imagen solo si cambió gen. */
void mirror_cursor(int x, int y, int mode, const uint32_t *argb, int w, int h, int hx, int hy, unsigned gen);
/* Portapapeles de texto de la sesión (se llama en cada vuelta; manda solo si cambió). */
void mirror_clipboard(const char *text, unsigned gen);

#endif

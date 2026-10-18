/* input.h — teclado y mouse leídos directo de evdev (/dev/input/event*), sin libinput. */
#ifndef LORD_INPUT_H
#define LORD_INPUT_H

#include <linux/input.h>

#define INPUT_MAX_DEVICES 32

typedef struct {
    int fd;
    int has_abs;                  /* tablet / touch: coordenadas absolutas */
    struct input_absinfo ax, ay;
    char name[80];
    struct TpDevice *tp;          /* touchpad o pantalla táctil (tp/tp.c), o NULL */
    int wheel_hires, hwheel_hires;  /* rueda de alta resolución (REL_*WHEEL_HI_RES): manda esa */
} InputDevice;

typedef struct {
    InputDevice dev[INPUT_MAX_DEVICES];
    int count;
} Input;

typedef void (*InputHandler)(void *ud, const InputDevice *dev, const struct input_event *ev);

int  input_open(Input *in);     /* abre y toma (EVIOCGRAB) los dispositivos útiles */
/* Igual, con fds ya abiertos que pasó lord-sessiond (los que no sirven se cierran). */
int  input_open_fds(Input *in, const int *fds, int n);
void input_close(Input *in);
/* Enchufados después (lord-sessiond, RP_INPUT_ADD): se agregan a los que hay. Devuelve cuántos sirvieron. */
int  input_add_fds(Input *in, const int *fds, int n);
/* Un dispositivo que desapareció (ENODEV, desenchufado): se cierra solo ese. */
void input_remove(Input *in, int idx);
void input_dispatch(Input *in, int idx, InputHandler cb, void *ud);

#endif

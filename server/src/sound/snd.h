/* snd.h — sonido de LordOS: cliente propio de PipeWire por el protocolo de PulseAudio que
 * sirve pipewire-pulse (ver snd.c y docs/SONIDO.md). Corre dentro de lord-server, como el
 * usuario de la sesión. */
#ifndef LORD_SND_H
#define LORD_SND_H

#include <stdint.h>

enum { SND_SINK, SND_SOURCE, SND_INPUT, SND_OUTPUT };   /* salida, entrada, app que suena, app que graba */

int  snd_start(void);                   /* después de soltar privilegios; reintenta solo si no hay servidor */
void snd_stop(void);
int  snd_fd(void);                      /* para poll (el socket, o un temporizador mientras reconecta) */
void snd_dispatch(void);

/* Eventos para JS: "state" (cambió algo; json = el estado completo, como snd_state_json) y
 * "peak" ({"source":n,"level":x}, 25 por segundo mientras el medidor está prendido). */
typedef void (*SndEventFn)(const char *type, const char *json);
void snd_events(SndEventFn fn);

char *snd_state_json(void);             /* malloc; ver docs/SONIDO.md para la forma */

/* Órdenes. Devuelven 0 si se mandaron (el resultado se ve en el próximo "state"). */
int snd_set_volume(int kind, uint32_t index, const double *v, int n);   /* 1.0 = 100 % por canal */
int snd_set_level(int kind, uint32_t index, double level);              /* el más alto queda en level, sin perder el balance */
int snd_set_mute(int kind, uint32_t index, int mute);
int snd_set_default(int kind, const char *name);                        /* SND_SINK o SND_SOURCE */
int snd_set_port(int kind, uint32_t index, const char *port);           /* SND_SINK o SND_SOURCE */
int snd_move(int kind, uint32_t index, uint32_t device);                /* SND_INPUT → salida, SND_OUTPUT → entrada */
int snd_meter(uint32_t source);         /* medidor de pico de una entrada; 0xffffffff lo apaga */
int snd_play(const char *name);         /* sonido del sistema: "pop", "notify", "error" */

#endif

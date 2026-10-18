/* ime.h — métodos de entrada para coreano, japonés y chino (ver ime.c). */
#ifndef LORD_IME_H
#define LORD_IME_H

#include <stddef.h>

#include "keyboard.h"

#define IME_MAX_CAND 64
#define IME_PAGE 9

typedef struct {
    char preedit[512];          /* texto en composición (UTF-8), "" si no hay */
    int ncand, sel;             /* candidatos y el elegido */
    char cand[IME_MAX_CAND][96];
    int converting;             /* japonés: mostrando la conversión */
    int focus_begin, focus_end; /* bytes del segmento con foco dentro de preedit (japonés) */
} ImeState;

/* "" (ninguno), "hangul", "kana", "pinyin". 0 bien; 1 bien pero sin diccionario (kana sin
 * skkdic: solo hiragana/katakana; pinyin sin diccionario no sirve); -1 desconocido. */
int  ime_set(const char *name);
const char *ime_current(void);
/* Hay un campo de texto o una terminal con foco: si no, las teclas pasan tal cual. */
void ime_context(int on);
/* Procesa una tecla. Devuelve 1 si la consumió. commit: texto terminado para entregar. */
int  ime_key(const KeyInfo *k, char *commit, size_t size);
void ime_reset(void);                       /* descarta la composición */
const ImeState *ime_state(void);
int  ime_changed(void);                     /* cambió el estado desde la última consulta */
const char *ime_dict_path(void);            /* diccionario cargado ("" si ninguno) */

#endif

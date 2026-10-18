/* bidi.h — algoritmo bidireccional de Unicode (UAX #9) para texto que mezcla escrituras de
 * izquierda a derecha (latín, CJK) con las de derecha a izquierda (árabe, hebreo). Ver bidi.c. */
#ifndef LORD_BIDI_H
#define LORD_BIDI_H

#include <stdint.h>

int bidi_class(uint32_t cp);                        /* BIDI_* de unidata.h */
uint32_t bidi_mirror(uint32_t cp);                  /* "(" ↔ ")" en tramos RTL; 0 si no tiene */

/* Dirección del primer carácter fuerte (reglas P2/P3): 0 izquierda a derecha, 1 derecha a
 * izquierda, -1 si no hay ninguno. */
int bidi_direction(const uint32_t *cp, int n);

/* Niveles de un párrafo (reglas X1–I2). para: 0 LTR, 1 RTL, -1 automático (P2/P3, LTR si no
 * hay fuertes). Devuelve el nivel del párrafo. Los caracteres que X9 quita (marcas de
 * incrustación, BN) quedan con el nivel del anterior; removed[i] = 1 si se pasa. */
int bidi_levels(const uint32_t *cp, int n, int para, uint8_t *levels, uint8_t *removed);

/* Orden visual de la línea [start, end) del párrafo: aplica L1 (espacios del final y
 * separadores al nivel del párrafo) sobre levels y L2. order[k] = índice lógico del
 * carácter que va en la posición visual k (de izquierda a derecha). */
void bidi_line(const uint32_t *cp, uint8_t *levels, int start, int end, int para, int *order);

#endif

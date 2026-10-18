/* thbrk.h — corte de palabras del tailandés con el diccionario de libthai-data (ver thbrk.c). */
#ifndef LORD_THBRK_H
#define LORD_THBRK_H

#include <stdint.h>

int thbrk_available(void);              /* ¿está /usr/share/libthai/thbrk.tri? (lo carga la primera vez) */
int thbrk_is_thai(uint32_t c);          /* letra o marca tailandesa (lo que va al diccionario) */
/* Entre dos caracteres seguidos del bloque tailandés que no son los dos letras (ๆ, dígitos, ฿):
 * 1 se puede cortar, 0 no, -1 alguno no es del bloque. */
int thbrk_pair(uint32_t a, uint32_t b);
/* Cortes dentro de un tramo tailandés ws[0..len): posiciones 1..len-1 en out (hasta max).
 * Sin diccionario, los comienzos de sílaba. */
int thbrk_chunk(const uint32_t *ws, int len, int *out, int max);

/* Un párrafo entero: para cada lugar que decide el tailandés, bk[i] = 1 (se puede cortar antes
 * de s[i]) o 2 (no); el resto queda como estaba (0). Tramos por diccionario, siglas con punto
 * y ๆ, dígitos y ฿ como libthai. */
void thbrk_text(const uint32_t *s, int n, uint8_t *bk);

#endif

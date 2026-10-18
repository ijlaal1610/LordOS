/* vgen.h — generador de contraseñas y frases de contraseña, y la estimación de fuerza que usa el
 * informe de seguridad. Todo sin red: la «filtración» se comprueba contra la lista de las 10 000
 * contraseñas más comunes que viene con LordOS (y, si el usuario lo elige, contra un archivo
 * local de Have I Been Pwned: SHA-1 ordenado por hash, búsqueda binaria en el archivo). */
#ifndef LORD_VAULT_VGEN_H
#define LORD_VAULT_VGEN_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int length;                 /* contraseña: 4–128 */
    int upper, lower, digits, symbols;
    int no_ambiguous;           /* sin Il1O0o */
    int words;                  /* frase: 3–12 palabras (0 = contraseña) */
    char sep[8];                /* separador de la frase ("-") */
    int capitalize, add_digit;  /* frase: Mayúscula Inicial, un número en una palabra */
} VGenOpts;

/* Listas embebidas (palabras de la EFF y contraseñas comunes), una por línea. */
void   vg_set_lists(const char *words, size_t wlen, const char *common, size_t clen);
int    vg_generate(const VGenOpts *o, char *out, size_t cap, double *bits);   /* 0 bien */
/* Fuerza: 0 (muy débil) – 4 (muy fuerte) y bits estimados. */
int    vg_strength(const char *pw, double *bits);
/* Posición en la lista de comunes (1 = la más común) o 0 si no está (también prueba sin
 * mayúsculas, sin números/símbolos al final y sin «leet»: p4ssw0rd → password). */
int    vg_common_rank(const char *pw);
/* Archivo HIBP local (fd de «pwned-passwords-sha1-ordered-by-hash», líneas "HASH:veces").
 * Devuelve las veces que aparece, 0 si no, -1 si el archivo no sirve. */
long   vg_hibp_lookup(int fd, const char *pw);
size_t vg_word_count(void);

#endif

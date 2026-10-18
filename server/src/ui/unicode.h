/* unicode.h — propiedades de caracteres de Unicode (tablas de unidata.h, generadas por
 * tools/gen-unicode.mjs). */
#ifndef LORD_UNICODE_H
#define LORD_UNICODE_H

#include <stdint.h>

#include "unidata.h"

int uc_bidi(uint32_t cp);                       /* BIDI_* */
uint32_t uc_mirror(uint32_t cp);                /* glifo espejado (BidiMirroring), 0 si no */
uint32_t uc_bracket(uint32_t cp, int *open);    /* par del paréntesis, 0 si no es */
int uc_join(uint32_t cp);                       /* JOIN_* (árabe, siríaco…) */
int uc_script(uint32_t cp);                     /* SCRIPT_* (índice en uc_script_tag) */
const char *uc_script_tag(int script);          /* etiqueta OpenType: "arab", "latn"… */
const char *uc_script_name(int script);         /* "Arabic", "Old Italic"… */
int uc_linebreak(uint32_t cp);                  /* LB_* (solo lo que importa para CJK) */
int uc_ccc(uint32_t cp);                        /* clase combinante canónica (0 = base) */
int uc_ignorable(uint32_t cp);                  /* invisible por defecto (ZWJ, LRM, VS…) */
int uc_indic_cat(uint32_t cp);                  /* IC_* (escrituras índicas, como HarfBuzz) */
int uc_indic_pos(uint32_t cp);                  /* IP_* */
int uc_mark(uint32_t cp);                       /* categoría general Mn, Mc o Me */
int uc_sea_cat(uint32_t cp);                    /* SEA_* (jemer y birmano, como HarfBuzz) */
int uc_use_cat(uint32_t cp);                    /* USE_* (Universal Shaping Engine) */
/* Descomposición canónica de dos partes (escrituras complejas): 1 si tiene; excluded = no se
 * recompone. */
int uc_decompose(uint32_t cp, uint32_t *a, uint32_t *b, int *excluded);
uint32_t uc_compose(uint32_t a, uint32_t b);    /* 0 si no hay o está excluida */

#endif

/* shape_syl.h — piezas comunes de los shapers por sílabas (jemer, birmano, USE): gramáticas
 * de sílabas, normalización como HarfBuzz y armado del run. Ver shape_syl.c. */
#ifndef LORD_SHAPE_SYL_H
#define LORD_SHAPE_SYL_H

#include <stdint.h>

#include "shape.h"

/* Gramática de sílabas: expresiones regulares sobre categorías, como las máquinas de Ragel de
 * HarfBuzz. defs = "nombre = expresión;" (pueden usar las anteriores); alts = las
 * alternativas de la máquina principal, en orden (gana la más larga; a igual largo, la
 * primera). Sintaxis: nombres de categoría o de defs, `any`, ( ) | * + ? y concatenación por
 * yuxtaposición o con `.`. */
typedef struct SylNfa SylNfa;
typedef struct {
    const char *const *cats;    /* nombres de las categorías (índice = valor) */
    int ncats;
    const char *defs;
    const char *const *alts;
    int nalts;
    SylNfa *nfa;                /* compilado la primera vez */
} SylGrammar;

/* Largo de la sílaba que empieza en cat[0] (al menos 1) y qué alternativa ganó (*alt). */
int syl_next(SylGrammar *g, const uint8_t *cat, int n, int *alt);

/* Clase combinante con los cambios de HarfBuzz (hebreo, árabe, télugu, tailandés, tibetano). */
int syl_modified_ccc(uint32_t cp);

/* Normalización de HarfBuzz para estos shapers ("composed diacritics, no short circuit"):
 * descompone lo que la fuente puede mostrar separado, ordena las marcas por clase y vuelve a
 * componer una marca con su base si la fuente tiene el compuesto y compose() lo permite.
 * split(cp, &a, &b) = descomposiciones propias del shaper (matras partidas del jemer).
 * Deja el resultado en s->cp / s->order; devuelve el largo o -1. */
int syl_normalize(Shaper *s, Otf *o, const uint32_t *in, int n,
                  int (*split)(uint32_t, uint32_t *, uint32_t *), int compose_after_mark);

/* aux: bits 28..31 = tipo de sílaba (los shapers de este grupo); bit 27 = parte que no es la
 * primera de una sustitución múltiple. */
#define SYL_TYPE(a) ((int)((a) >> 28 & 0xf))

/* Inserta ◌ al comienzo de cada sílaba rota (después de las de categoría repha si repha >= 0).
 * cat_dc = categoría del ◌. Devuelve 1 si insertó alguno. */
int syl_dotted_circles(Shaper *s, Otf *o, int broken_type, int cat_dc, int repha, int (*reserve)(Shaper *, int));

/* Al final: cada sílaba es un solo grupo (el dibujo no la separa ni la corta). */
void syl_merge_clusters(GlyphRun *r);

/* Mueve el glifo from a la posición to corriendo el resto (g, cl, mask, lig, aux). */
void syl_move(GlyphRun *r, int from, int to);

#endif

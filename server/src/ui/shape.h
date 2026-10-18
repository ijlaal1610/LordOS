/* shape.h — armado de un tramo de texto de una sola fuente, escritura y dirección: espejado
 * en RTL, formas de unión del árabe, GSUB y GPOS (otf.c). Ver shape.c. */
#ifndef LORD_SHAPE_H
#define LORD_SHAPE_H

#include <stdint.h>

#include "otf.h"

typedef struct {
    GlyphRun run;               /* glifos en orden lógico; run.cl = índice del carácter */
    OtfPos *pos;                /* unidades de la fuente */
    int pos_cap;
    int gpos_kern;              /* 0: el llamador aplica la tabla kern */
    uint32_t *cp;               /* copia con las marcas en orden canónico */
    int *order, cp_cap;
    const OtfPlan *plan;        /* el que se usó (índicas: dev2, bng2…) */
} Shaper;

/* script: etiqueta OpenType ("arab", "hebr", "latn"…). Devuelve la cantidad de glifos. */
int  shape_run(Shaper *s, Otf *o, const uint32_t *cp, int n, const char *script, int rtl);
void shape_free(Shaper *s);

/* shape_indic.c */
int  shape_indic_script(uint32_t cp);      /* índice de escritura índica, o -1 */
int  shape_indic(Shaper *s, Otf *o, const uint32_t *in, int n, int script, int (*reserve)(Shaper *, int));

/* shape_khmer.c, shape_myanmar.c, shape_use.c (ver shape_syl.h) */
int  shape_khmer(Shaper *s, Otf *o, const uint32_t *in, int n, int (*reserve)(Shaper *, int));
int  shape_myanmar(Shaper *s, Otf *o, const uint32_t *in, int n, int (*reserve)(Shaper *, int));
int  shape_use_script(const char *tag);    /* ¿la escritura va por el Universal Shaping Engine? */
int  shape_use(Shaper *s, Otf *o, const uint32_t *in, int n, const char *tag, int (*reserve)(Shaper *, int));
/* shape.c: formas isol/init/medi/fina del árabe y las escrituras que se unen */
void shape_joining_masks(const uint32_t *cp, int n, uint32_t *mask);

#endif

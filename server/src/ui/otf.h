/* otf.h — lo de OpenType que stb_truetype no trae, sin HarfBuzz:
 *   - sustituciones GSUB: ligaduras de fuentes de programación (JetBrains Mono, Fira Code),
 *     secuencias de emoji y, por escritura, las formas contextuales del árabe;
 *   - posicionamiento GPOS: kerning de pares, cursivo (nastaliq, kannada) y marcas (harakat del
 *     árabe, niqqud del hebreo, acentos combinantes) ancladas a la base, a ligaduras o a otras marcas;
 *   - glifos a color CBDT/CBLC (PNG dentro de la fuente, como Noto Color Emoji).
 *
 * Todas las lecturas validan límites: las fuentes pueden venir de ~/.fonts. */
#ifndef LORD_OTF_H
#define LORD_OTF_H

#include <stddef.h>
#include <stdint.h>

/* Máscaras de features por glifo: las formas del árabe van solo donde corresponden; el resto
 * de las features (ccmp, locl, rlig, calt, liga, kern, mark…) es global. */
enum { OTF_F_ISOL = 1 << 0, OTF_F_FINA = 1 << 1, OTF_F_MEDI = 1 << 2, OTF_F_INIT = 1 << 3, OTF_F_GLOBAL = 1 << 4,
       /* escrituras índicas: formas que se piden glifo por glifo según su lugar en la sílaba */
       OTF_F_RPHF = 1 << 5, OTF_F_PREF = 1 << 6, OTF_F_BLWF = 1 << 7, OTF_F_ABVF = 1 << 8, OTF_F_HALF = 1 << 9,
       OTF_F_PSTF = 1 << 10, OTF_F_CFAR = 1 << 11 };

/* GlyphRun.aux: propiedades del armado índico que siguen a cada glifo en las sustituciones */
#define OTF_AUX_CAT(a)   ((a) & 0xff)
#define OTF_AUX_POS(a)   ((a) >> 8 & 0xff)
#define OTF_AUX_SYL(a)   ((a) >> 16 & 0xff)
enum { OTF_AUX_SUBST = 1u << 24, OTF_AUX_LIGATED = 1u << 25, OTF_AUX_MULTIPLIED = 1u << 26,
       OTF_AUX_NOTFIRST = 1u << 27 };     /* parte 2, 3… de una sustitución múltiple */

/* Etapas del plan índico (como HarfBuzz): entre ellas va el reordenamiento. */
enum { OTF_ST_PRE = 0, OTF_ST_NUKT = 1, OTF_ST_RPHF = 3, OTF_ST_PREF = 5, OTF_ST_BLWF = 6, OTF_ST_PSTF = 9,
       OTF_ST_VATU = 10, OTF_ST_CJCT = 11, OTF_ST_FINAL = 12 };

typedef struct {
    uint16_t lookup;
    uint8_t stage;              /* las del árabe y las índicas van por etapas, como en HarfBuzz */
    uint8_t per_syllable;       /* índicas: no cruza de sílaba */
    uint32_t mask;
} OtfLookupRef;

/* Tipo de shaper del plan (cada uno pide sus features por etapas, como en HarfBuzz). */
enum { OTF_KIND_DEFAULT, OTF_KIND_INDIC, OTF_KIND_KHMER, OTF_KIND_MYANMAR, OTF_KIND_USE };
/* Ancho de las marcas (GDEF): se anula después de GPOS, ninguna vez, o antes de GPOS. */
enum { OTF_ZERO_LATE, OTF_ZERO_NONE, OTF_ZERO_EARLY };

/* Lookups de las features de la interfaz para una escritura (etiqueta OpenType). */
typedef struct {
    char script[4];
    int joining;                /* árabe, siríaco, n'ko, mongol…: usa isol/fina/medi/init */
    int indic;                  /* devanagari … malayalam (etiquetas dev2, bng2…) */
    int kind;                   /* OTF_KIND_* */
    int zero_marks;             /* OTF_ZERO_* */
    OtfLookupRef *gsub, *gpos;
    int ngsub, ngpos;
    uint8_t *first;             /* bitset del primer glifo por lookup de GSUB */
    int gpos_kern;              /* el GPOS trae kerning (si no, se usa la tabla kern) */
} OtfPlan;

#define OTF_MAX_PLANS 6

typedef struct {
    const uint8_t *d;
    size_t n;                   /* tamaño del archivo */
    uint32_t gsub, gsub_len;    /* offsets absolutos (0 = no hay) */
    uint32_t gpos, gpos_len;
    uint32_t cblc, cblc_len, cbdt, cbdt_len;
    uint32_t cmap;              /* subtabla de cmap elegida (formato 4 o 12) */
    uint32_t hmtx;
    unsigned nhmetrics;
    uint32_t glyph_class, mark_class, mark_sets;    /* GDEF */
    uint16_t *lookups;          /* índices de lookups por grupo de features, ordenados */
    int nlookups[2];
    uint8_t *first[2];          /* bitset: glifos que pueden empezar alguna sustitución */
    int nglyphs;
    OtfPlan plans[OTF_MAX_PLANS];
    int nplans;
} Otf;

enum { OTF_LIGA = 0, OTF_CCMP = 1 };   /* grupos de la terminal: calt+liga+clig / ccmp+rlig */

/* Buffer de glifos: cada glifo recuerda de qué carácter de entrada salió (cluster).
 * mask y lig son opcionales (NULL): los usa el armado por escritura. */
typedef struct {
    uint16_t *g;
    int *cl;
    int len, cap;
    uint32_t *mask;             /* OTF_F_* por glifo */
    uint8_t *lig;               /* ligadura: cantidad de componentes; marca: componente + 1 */
    uint32_t *aux;              /* índicas: categoría, posición, sílaba y banderas (OTF_AUX_*) */
} GlyphRun;

/* Resultado de GPOS, en unidades de la fuente. Una marca anclada (att >= 0) se dibuja en el
 * origen del glifo att más (dx, dy); las demás en la pluma más (dx, dy). */
typedef struct { int32_t adv, dx, dy; int att, cur; } OtfPos;    /* cur: padre cursivo (GPOS 3), -1 */

int  otf_init(Otf *o, const uint8_t *data, size_t size, int font_offset);
void otf_free(Otf *o);
int  otf_has_gsub(const Otf *o, int group);
/* Carácter → glifo (para fuentes que stb_truetype no abre, como las solo-bitmap). */
uint16_t otf_cmap(const Otf *o, uint32_t cp);
/* Terminal: aplica las features del grupo sobre el run (in situ). */
void otf_shape(const Otf *o, int group, GlyphRun *run);

/* Interfaz: plan para la escritura `script` ("arab", "hebr", "latn", "hani"…). */
const OtfPlan *otf_plan(Otf *o, const char *script);
const OtfPlan *otf_plan_kind(Otf *o, const char *script, int kind);
void otf_substitute(const Otf *o, const OtfPlan *p, GlyphRun *run);
/* Solo las etapas [from, to] (las índicas reordenan entre etapas). */
void otf_substitute_stages(const Otf *o, const OtfPlan *p, GlyphRun *run, int from, int to);
/* ¿Algún lookup de esa etapa sustituye exactamente esta secuencia? (sin contexto) */
int  otf_would_substitute(const Otf *o, const OtfPlan *p, int stage, const uint16_t *g, int n);
int  otf_has_script(const Otf *o, const char *tag);
int  otf_has_gsub_script(const Otf *o, const char *tag);       /* solo en GSUB (como HarfBuzz elige el shaper) */
/* pos tiene run->len lugares. */
void otf_position(const Otf *o, const OtfPlan *p, const GlyphRun *run, OtfPos *pos, int rtl);
/* Igual, pero los glifos con ign[i] (invisibles como ZWJ) no cortan un par de kerning ni una
 * secuencia de GPOS (HarfBuzz los saltea). */
void otf_position_ign(const Otf *o, const OtfPlan *p, const GlyphRun *run, OtfPos *pos, int rtl, const uint8_t *ign);
int  otf_is_mark(const Otf *o, unsigned glyph);

/* Glifo a color: PNG del strike más cercano a `ppem`. Devuelve puntero al PNG dentro de la
 * fuente y su tamaño, o NULL. `strike_ppem` = tamaño nativo del bitmap. */
const uint8_t *otf_color_png(const Otf *o, uint16_t glyph, int ppem, size_t *len, int *strike_ppem);

#endif

/* ui.h — motor de interfaz de LordOS: árbol de nodos, estilos, layout, pintado. */
#ifndef LORD_UI_H
#define LORD_UI_H

#include <stdint.h>
#include "../canvas.h"
#include "otf.h"

enum { UNIT_AUTO = 0, UNIT_PX, UNIT_PCT };
typedef struct { float v; uint8_t unit; } Dim;

enum { NODE_VIEW, NODE_TEXT, NODE_IMAGE, NODE_RAW, NODE_TERM, NODE_SURFACE, NODE_MEDIA };
enum { JUSTIFY_START, JUSTIFY_CENTER, JUSTIFY_END, JUSTIFY_BETWEEN, JUSTIFY_AROUND, JUSTIFY_EVENLY };
enum { ALIGN_AUTO, ALIGN_STRETCH, ALIGN_START, ALIGN_CENTER, ALIGN_END };
enum { TEXT_LEFT, TEXT_CENTER, TEXT_RIGHT, TEXT_START, TEXT_END };   /* start/end según direction */
enum { EDGE_L, EDGE_T, EDGE_R, EDGE_B };
enum { DIR_INHERIT, DIR_LTR, DIR_RTL };

/* Subconjunto de CSS que entiende el motor (los nombres JS están en style.c). */
typedef struct {
    uint8_t display_none, absolute, row, justify, align_items, align_self;
    uint8_t overflow_hidden, pointer_none, text_align, object_fit, nowrap;
    uint8_t dir;                /* DIR_INHERIT / DIR_LTR / DIR_RTL (CSS direction) */
    uint8_t wrap;               /* flexWrap: 'wrap' (los hijos que no entran pasan a otra línea) */
    uint8_t mono;               /* fontFamily: 'monospace' (JetBrains Mono) */
    uint8_t cursor;             /* cursor (CSS): CUR_* de cursor.h; 0 = el del padre */
    float grow, shrink;
    Dim basis, width, height, min_w, min_h, max_w, max_h;
    Dim pos[4];                 /* left, top, right, bottom */
    float margin[4], padding[4];
    float gap;
    uint32_t bg, border_color, color, shadow_color;
    uint8_t has_gradient;
    uint32_t grad_from, grad_to;
    float grad_angle;
    float radius, border_width, opacity;
    float shadow_blur, shadow_x, shadow_y, backdrop_blur;
    float font_size, line_height, font_weight;
    float translate_x, translate_y;
    float z_index;
} Style;

typedef struct Image Image;

/* Caja en coordenadas de pantalla; vacía si x1 <= x0. */
typedef struct { float x0, y0, x1, y1; } Box;

typedef struct Node {
    int id, type;
    Style s;
    char *text;                 /* NODE_RAW */
    Image *img;                 /* NODE_IMAGE */
    int term;                   /* NODE_TERM: id de la sesión; NODE_SURFACE: id de la superficie Wayland; NODE_MEDIA: origen (media/) */
    uint8_t protect;            /* ventana protegida: negra en capturas, grabaciones y escritorio remoto (media/capture.c) */
    uint8_t rtl;                /* dirección efectiva (se calcula en el layout) */
    struct Node *parent, *first, *last, *next, *prev;
    float x, y, w, h;           /* layout relativo al padre */
    float ax, ay;               /* posición absoluta en pantalla */
    int measure_valid;
    float measure_maxw, measure_w, measure_h;
    uint8_t paint_dirty;        /* cambió su estilo o texto */
    Box prev_box;               /* lo que ocupaba en pantalla en el último frame */
} Node;

typedef struct {
    Node **nodes;
    int cap;
    Node *root;
    int dirty;
    float width, height;
    char theme_dir[1024];
    double layout_ms, paint_ms;     /* último frame */
    Box damage;                     /* región a repintar */
    double damage_px;               /* píxeles repintados en el último frame */
    int quit_requested;             /* lord.quit() */
    uint8_t rtl;                    /* dirección de la interfaz (idioma árabe, hebreo…) */
    char theme_request[1024];       /* lord.themes.use(): bundle a cargar */
    char theme_failed[1024];        /* último bundle que no pudo cargarse */
} Ui;

extern Ui ui;

/* node.c */
void  style_defaults(Style *s);
Node *node_new(int type);
Node *node_get(int id);
int   node_count(void);
void  node_destroy(Node *n);
void  node_append(Node *p, Node *c);
void  node_insert_before(Node *p, Node *c, Node *before);
void  node_detach(Node *c);
void  node_set_text(Node *n, const char *s);
void  node_invalidate(Node *n);
int   node_gather_text(const Node *n, char *buf, int size);
int   node_children_ordered(Node *n, Node **out, int max);
void  damage_add(Box b);
void  damage_subtree(Node *n);
void  ui_reset(void);

/* style.c */
int style_set_number(Style *s, const char *key, double v);
int style_set_string(Style *s, const char *key, const char *v);
int style_reset(Style *s, const char *key);

/* layout.c */
void layout_root(Node *root, float w, float h);

/* text.c */
typedef struct {
    uint32_t cp;
    uint16_t font, size_q;      /* tamaño en medios píxeles */
    int used;
    int w, h, xoff, yoff;
    float adv;
    uint8_t *bmp;
    uint32_t *argb;             /* glifos a color (emoji): ARGB premultiplicado */
} Glyph;
/* El puntero vale hasta la próxima llamada (la caché puede crecer). */
const Glyph *text_glyph(uint32_t cp, int font, float size);
int   text_font_add(const char *path);
int   text_font_load(const char *path, int index);
int   text_font_has(int font, uint32_t cp);
uint32_t text_font_glyph(int font, uint32_t cp);          /* índice de glifo, 0 = no tiene */
int   text_font_is_color(int font);
int   text_font_can_shape(int font, int group);           /* OTF_LIGA / OTF_CCMP */
void  text_font_shape(int font, int group, GlyphRun *run);
const Glyph *text_glyph_id(int font, uint32_t gid, float size);
const Glyph *text_glyph_color(int font, uint32_t gid, int max_w, int max_h);
void  text_font_vmetrics(int font, float size, float *asc, float *desc);
int   text_init(const char *font_dir);
/* Peso + TEXT_MONO elige la monoespaciada (JetBrains Mono) en text_measure/text_draw y en
 * lord.measureText: así la fuente viaja con el peso sin cambiar las firmas. */
#define TEXT_MONO 1000.0f
#define TEXT_WEIGHT(s) ((s)->font_weight + ((s)->mono ? TEXT_MONO : 0))
float text_line_height(float size, float line_h);
void  text_measure(const char *s, float weight, float size, float line_h, float max_w, int nowrap, float *w, float *h);
void  text_draw(Canvas *c, const char *s, float weight, float size, float line_h,
                float x, float y, float box_w, int align, int nowrap, uint32_t argb);
Otf  *text_font_otf(int font);
float text_font_scale(int font, float size);
int   text_font_kern_glyphs(int font, uint32_t a, uint32_t b);  /* unidades de la fuente */
int   text_ui_font_bold(int font);

/* textlayout.c: escrituras complejas (bidi, árabe, CJK, emoji, respaldo de fuentes) */
int   text_needs_layout(const char *s, int font);
void  text_layout_measure(const char *s, int font, float size, float line_h, float max_w, int nowrap, float *w, float *h);
void  text_layout_draw(Canvas *c, const char *s, int font, float size, float line_h,
                       float x, float y, float box_w, int align, int nowrap, uint32_t argb);
void  text_layout_flush(void);
void  text_set_language(const char *lang);                 /* elige la fuente CJK (ja/ko/zh) */
int   text_script_fallback(uint32_t cp, int bold);         /* terminal: fuente por escritura */
int   text_direction(const char *s);                       /* 1 si el primer fuerte es RTL */

/* image.c */
Image *image_get(const char *path);
void   image_size(const Image *img, int *w, int *h);
Image *image_pixels(Image *img, int w, int h, const uint8_t *rgba);
Image *image_argb(Image *img, int w, int h, const uint32_t *argb);   /* ARGB premultiplicado (doc/doc.c) */
void   image_release(Image *img);
void   image_draw(Canvas *c, Image *img, float x, float y, float w, float h, float r, int fit, float opacity);

/* paint.c */
int   paint_damage(Node *root);     /* calcula ui.damage; 0 si no hay nada que repintar */
void  paint_root(Canvas *c, Node *root);
Node *hit_test(Node *root, float x, float y);
/* Modo captura (media/capture.c): los nodos protegidos se pintan negros y las ventanas de las apps
 * se copian aunque se componga por GPU. paint_capture pinta todo el árbol así. */
extern int paint_capture_mode;
void  paint_capture(Canvas *c, Node *root);

#endif

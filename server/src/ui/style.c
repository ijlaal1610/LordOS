/* style.c — table of supported CSS properties (React-style names). */
#include "ui.h"
#include "../cursor.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum { ST_DIM, ST_FLOAT, ST_COLOR, ST_ENUM } StyleType;

typedef struct {
    const char *name;
    StyleType type;
    size_t off;
    const char *const *values;   /* ST_ENUM */
} StyleProp;

static const char *const E_DISPLAY[]  = {"flex", "none", NULL};
static const char *const E_POSITION[] = {"relative", "absolute", NULL};
static const char *const E_DIR[]      = {"column", "row", NULL};
static const char *const E_JUSTIFY[]  = {"flex-start", "center", "flex-end", "space-between",
                                         "space-around", "space-evenly", NULL};
static const char *const E_ALIGN[]    = {"auto", "stretch", "flex-start", "center", "flex-end", NULL};
static const char *const E_OVERFLOW[] = {"visible", "hidden", NULL};
static const char *const E_POINTER[]  = {"auto", "none", NULL};
static const char *const E_TALIGN[]   = {"left", "center", "right", "start", "end", NULL};
static const char *const E_DIRECTION[] = {"inherit", "ltr", "rtl", NULL};
static const char *const E_FIT[]      = {"fill", "cover", "contain", NULL};
static const char *const E_WSPACE[]   = {"normal", "nowrap", NULL};
static const char *const E_WRAP[]     = {"nowrap", "wrap", NULL};
static const char *const E_FAMILY[]   = {"sans-serif", "monospace", NULL};
static const char *const E_CURSOR[]   = {CURSOR_CSS_NAMES, NULL};

#define P(n, t, f, e) { n, t, offsetof(Style, f), e }

static const StyleProp props[] = {
    P("display", ST_ENUM, display_none, E_DISPLAY),
    P("position", ST_ENUM, absolute, E_POSITION),
    P("flexDirection", ST_ENUM, row, E_DIR),
    P("flexWrap", ST_ENUM, wrap, E_WRAP),
    P("fontFamily", ST_ENUM, mono, E_FAMILY),
    P("cursor", ST_ENUM, cursor, E_CURSOR),
    P("justifyContent", ST_ENUM, justify, E_JUSTIFY),
    P("alignItems", ST_ENUM, align_items, E_ALIGN),
    P("alignSelf", ST_ENUM, align_self, E_ALIGN),
    P("overflow", ST_ENUM, overflow_hidden, E_OVERFLOW),
    P("pointerEvents", ST_ENUM, pointer_none, E_POINTER),
    P("textAlign", ST_ENUM, text_align, E_TALIGN),
    P("direction", ST_ENUM, dir, E_DIRECTION),
    P("objectFit", ST_ENUM, object_fit, E_FIT),
    P("whiteSpace", ST_ENUM, nowrap, E_WSPACE),
    P("flexGrow", ST_FLOAT, grow, NULL),
    P("flexShrink", ST_FLOAT, shrink, NULL),
    P("flexBasis", ST_DIM, basis, NULL),
    P("width", ST_DIM, width, NULL),
    P("height", ST_DIM, height, NULL),
    P("minWidth", ST_DIM, min_w, NULL),
    P("minHeight", ST_DIM, min_h, NULL),
    P("maxWidth", ST_DIM, max_w, NULL),
    P("maxHeight", ST_DIM, max_h, NULL),
    P("left", ST_DIM, pos[EDGE_L], NULL),
    P("top", ST_DIM, pos[EDGE_T], NULL),
    P("right", ST_DIM, pos[EDGE_R], NULL),
    P("bottom", ST_DIM, pos[EDGE_B], NULL),
    P("marginLeft", ST_FLOAT, margin[EDGE_L], NULL),
    P("marginTop", ST_FLOAT, margin[EDGE_T], NULL),
    P("marginRight", ST_FLOAT, margin[EDGE_R], NULL),
    P("marginBottom", ST_FLOAT, margin[EDGE_B], NULL),
    P("paddingLeft", ST_FLOAT, padding[EDGE_L], NULL),
    P("paddingTop", ST_FLOAT, padding[EDGE_T], NULL),
    P("paddingRight", ST_FLOAT, padding[EDGE_R], NULL),
    P("paddingBottom", ST_FLOAT, padding[EDGE_B], NULL),
    P("gap", ST_FLOAT, gap, NULL),
    P("backgroundColor", ST_COLOR, bg, NULL),
    P("borderColor", ST_COLOR, border_color, NULL),
    P("color", ST_COLOR, color, NULL),
    P("shadowColor", ST_COLOR, shadow_color, NULL),
    P("borderRadius", ST_FLOAT, radius, NULL),
    P("borderWidth", ST_FLOAT, border_width, NULL),
    P("opacity", ST_FLOAT, opacity, NULL),
    P("shadowBlur", ST_FLOAT, shadow_blur, NULL),
    P("shadowOffsetX", ST_FLOAT, shadow_x, NULL),
    P("shadowOffsetY", ST_FLOAT, shadow_y, NULL),
    P("backdropBlur", ST_FLOAT, backdrop_blur, NULL),
    P("fontSize", ST_FLOAT, font_size, NULL),
    P("lineHeight", ST_FLOAT, line_height, NULL),
    P("fontWeight", ST_FLOAT, font_weight, NULL),
    P("translateX", ST_FLOAT, translate_x, NULL),
    P("translateY", ST_FLOAT, translate_y, NULL),
    P("zIndex", ST_FLOAT, z_index, NULL),
};

static const StyleProp *find(const char *key) {
    for (size_t i = 0; i < sizeof props / sizeof props[0]; i++)
        if (!strcmp(props[i].name, key)) return &props[i];
    return NULL;
}

static size_t type_size(StyleType t) {
    return t == ST_DIM ? sizeof(Dim) : t == ST_ENUM ? 1 : 4;
}

int style_set_number(Style *s, const char *key, double v) {
    const StyleProp *p = find(key);
    if (!p) return -1;
    char *f = (char *)s + p->off;
    switch (p->type) {
    case ST_DIM:   *(Dim *)f = (Dim){ (float)v, UNIT_PX }; return 0;
    case ST_FLOAT: *(float *)f = (float)v; return 0;
    case ST_COLOR: *(uint32_t *)f = (uint32_t)(int64_t)v; return 0;
    case ST_ENUM:  return -1;
    }
    return -1;
}

int style_set_string(Style *s, const char *key, const char *v) {
    const StyleProp *p = find(key);
    if (!p) return -1;
    char *f = (char *)s + p->off;
    if (p->type == ST_ENUM) {
        for (int i = 0; p->values[i]; i++)
            if (!strcmp(p->values[i], v)) { *(uint8_t *)f = (uint8_t)i; return 0; }
        return -1;
    }
    if (p->type == ST_DIM) {
        if (!strcmp(v, "auto")) { *(Dim *)f = (Dim){0, UNIT_AUTO}; return 0; }
        char *end;
        float n = strtof(v, &end);
        if (end == v) return -1;
        *(Dim *)f = (Dim){ n, *end == '%' ? UNIT_PCT : UNIT_PX };
        return 0;
    }
    if (p->type == ST_FLOAT) { *(float *)f = strtof(v, NULL); return 0; }
    return -1;
}

int style_reset(Style *s, const char *key) {
    const StyleProp *p = find(key);
    if (!p) return -1;
    Style d;
    style_defaults(&d);
    memcpy((char *)s + p->off, (char *)&d + p->off, type_size(p->type));
    return 0;
}

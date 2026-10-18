/* text.c — fuentes TrueType/OpenType (stb_truetype + otf.c), caché de glifos, medición y
 * ajuste de línea. Las fuentes se mapean con mmap (no se copian a RAM).
 *
 * El texto que Inter cubre (latín, griego, cirílico) va por el camino rápido de acá; el resto
 * (árabe, hebreo, CJK, emoji, marcas combinantes…) por textlayout.c. */
#include "ui.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "stb_truetype.h"
#include "stb_image.h"

typedef struct {
    stbtt_fontinfo info;
    unsigned char *data;
    size_t size;
    int ascent, descent, gap, upem;
    int color;                  /* solo bitmaps CBDT (stb_truetype no la abre) */
    Otf otf;
    char path[512];
    int index;
} Font;

static const char *const font_files[] = {
    "Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf",
};
#define NFONTS 4            /* Inter: las fuentes de la interfaz */
#define MAX_FONTS 64
#define GID_BIT 0x80000000u /* en la caché: cp = GID_BIT | (ancho máx << 16) | glifo */
static Font fonts[MAX_FONTS];
static int nfonts;
static char font_dir[1024];
static int mono_font[2] = { -2, -2 };   /* JetBrains Mono regular y bold: -2 sin cargar, -1 no está */

static Glyph *cache;
static size_t cache_cap, cache_used;

static unsigned char *map_file(const char *path, size_t *size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat st;
    void *m = MAP_FAILED;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size > 12 && st.st_size < (64 << 20))
        m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return NULL;
    *size = (size_t)st.st_size;
    return m;
}

static unsigned be16(const unsigned char *d, size_t n, size_t off) { return off + 2 <= n ? (unsigned)(d[off] << 8 | d[off + 1]) : 0; }

static int load(Font *f, const char *path, int index) {
    memset(f, 0, sizeof *f);
    f->data = map_file(path, &f->size);
    if (!f->data) return -1;
    int off = stbtt_GetFontOffsetForIndex(f->data, index);
    if (off < 0) off = 0;
    otf_init(&f->otf, f->data, f->size, off);
    if (stbtt_InitFont(&f->info, f->data, off)) {
        stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->gap);
        f->upem = be16(f->data, f->size, (size_t)f->info.head + 18);
    } else if (f->otf.cbdt) {
        /* fuente solo de bitmaps a color: cmap y métricas con otf.c */
        f->color = 1;
        f->upem = 2048;
        f->ascent = 1900;
        f->descent = -500;
    } else {
        otf_free(&f->otf);
        munmap(f->data, f->size);
        f->data = NULL;
        return -1;
    }
    snprintf(f->path, sizeof f->path, "%s", path);
    f->index = index;
    return 0;
}

int text_init(const char *dir) {
    snprintf(font_dir, sizeof font_dir, "%s", dir);
    for (int i = 0; i < NFONTS; i++) {
        char path[1200];
        snprintf(path, sizeof path, "%s/%s", dir, font_files[i]);
        if (load(&fonts[i], path, 0)) {
            fprintf(stderr, "text: no se pudo cargar %s\n", path);
            return -1;
        }
    }
    nfonts = NFONTS;
    cache_cap = 4096;
    cache = calloc(cache_cap, sizeof *cache);
    return cache ? 0 : -1;
}

/* Fuentes adicionales (la de la terminal, la de emoji...). Si ya estaba cargada devuelve
 * el mismo índice. -1 si no se pudo. */
int text_font_load(const char *path, int index) {
    for (int i = 0; i < nfonts; i++) if (fonts[i].data && fonts[i].index == index && !strcmp(fonts[i].path, path)) return i;
    if (nfonts >= MAX_FONTS) return -1;
    if (load(&fonts[nfonts], path, index)) {
        fprintf(stderr, "text: no se pudo cargar %s\n", path);
        return -1;
    }
    return nfonts++;
}

int text_font_add(const char *path) { return text_font_load(path, 0); }

uint32_t text_font_glyph(int font, uint32_t cp) {
    if (font < 0 || font >= nfonts || !fonts[font].data) return 0;
    Font *f = &fonts[font];
    return f->color ? otf_cmap(&f->otf, cp) : (uint32_t)stbtt_FindGlyphIndex(&f->info, (int)cp);
}

int text_font_has(int font, uint32_t cp) { return text_font_glyph(font, cp) != 0; }
int text_font_is_color(int font) { return font >= 0 && font < nfonts && fonts[font].otf.cbdt != 0; }
int text_font_can_shape(int font, int group) { return font >= 0 && font < nfonts && otf_has_gsub(&fonts[font].otf, group); }
void text_font_shape(int font, int group, GlyphRun *run) {
    if (font >= 0 && font < nfonts) otf_shape(&fonts[font].otf, group, run);
}

Otf *text_font_otf(int font) { return font >= 0 && font < nfonts && fonts[font].data ? &fonts[font].otf : NULL; }

float text_font_scale(int font, float size) {
    Font *f = &fonts[font];
    return f->color ? size / f->upem : stbtt_ScaleForMappingEmToPixels(&f->info, size);
}

int text_font_kern_glyphs(int font, uint32_t a, uint32_t b) {
    Font *f = &fonts[font];
    return f->color ? 0 : stbtt_GetGlyphKernAdvance(&f->info, (int)a, (int)b);
}

int text_ui_font_bold(int font) { return font >= 2 && font < NFONTS; }

void text_font_vmetrics(int font, float size, float *asc, float *desc) {
    Font *f = &fonts[font];
    float scale = f->color ? size / f->upem : stbtt_ScaleForMappingEmToPixels(&f->info, size);
    *asc = f->ascent * scale;
    *desc = f->descent * scale;
}

static int font_for(float weight) {
    if (weight >= TEXT_MONO) {                  /* fontFamily: 'monospace' */
        int bold = weight - TEXT_MONO >= 600;
        if (mono_font[bold] == -2) {
            char path[1200];
            snprintf(path, sizeof path, "%s/%s", font_dir, bold ? "JetBrainsMono-Bold.ttf" : "JetBrainsMono-Regular.ttf");
            mono_font[bold] = text_font_load(path, 0);
        }
        if (mono_font[bold] >= 0) return mono_font[bold];
        weight -= TEXT_MONO;
    }
    return weight < 450 ? 0 : weight < 550 ? 1 : weight < 650 ? 2 : 3;
}

static size_t hash(uint32_t cp, int font, int size_q) {
    uint32_t h = cp * 2654435761u ^ (uint32_t)font * 40503u ^ (uint32_t)size_q * 97u;
    return h;
}

static Glyph *glyph_slot(uint32_t cp, int font, int size_q) {
    size_t i = hash(cp, font, size_q) & (cache_cap - 1);
    while (cache[i].used && !(cache[i].cp == cp && cache[i].font == font && cache[i].size_q == size_q))
        i = (i + 1) & (cache_cap - 1);
    return &cache[i];
}

static void cache_grow(void) {
    Glyph *old = cache;
    size_t old_cap = cache_cap;
    cache_cap *= 2;
    cache = calloc(cache_cap, sizeof *cache);
    cache_used = 0;
    for (size_t i = 0; i < old_cap; i++)
        if (old[i].used) { *glyph_slot(old[i].cp, old[i].font, old[i].size_q) = old[i]; cache_used++; }
    free(old);
}

static Glyph *slot_for(uint32_t key, int font, int size_q) {
    Glyph *g = glyph_slot(key, font, size_q);
    if (!g->used && (cache_used + 1) * 10 > cache_cap * 7) { cache_grow(); g = glyph_slot(key, font, size_q); }
    return g;
}

const Glyph *text_glyph(uint32_t cp, int font, float size) {
    int size_q = (int)(size * 2 + 0.5f);
    Glyph *g = slot_for(cp, font, size_q);
    if (g->used) return g;
    Font *f = &fonts[font];
    g->cp = cp; g->font = (uint16_t)font; g->size_q = (uint16_t)size_q; g->used = 1;
    cache_used++;
    if (f->color) return g;
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, size_q / 2.0f);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&f->info, (int)cp, &adv, &lsb);
    g->adv = adv * scale;
    g->bmp = stbtt_GetCodepointBitmap(&f->info, scale, scale, (int)cp, &g->w, &g->h, &g->xoff, &g->yoff);
    return g;
}

/* Glifo por índice (resultado de text_font_shape). */
const Glyph *text_glyph_id(int font, uint32_t gid, float size) {
    int size_q = (int)(size * 2 + 0.5f);
    uint32_t key = GID_BIT | (gid & 0xffff);
    Glyph *g = slot_for(key, font, size_q);
    if (g->used) return g;
    Font *f = &fonts[font];
    g->cp = key; g->font = (uint16_t)font; g->size_q = (uint16_t)size_q; g->used = 1;
    cache_used++;
    if (f->color) return g;
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, size_q / 2.0f);
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&f->info, (int)gid, &adv, &lsb);
    g->adv = adv * scale;
    g->bmp = stbtt_GetGlyphBitmap(&f->info, scale, scale, (int)gid, &g->w, &g->h, &g->xoff, &g->yoff);
    return g;
}

/* Reduce (o agranda) un bitmap ARGB premultiplicado promediando áreas. */
static uint32_t *resample(const uint32_t *src, int sw, int sh, int dw, int dh) {
    uint32_t *out = malloc((size_t)dw * dh * 4);
    if (!out) return NULL;
    for (int y = 0; y < dh; y++) {
        int y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = x * sw / dw, x1 = (x + 1) * sw / dw;
            if (x1 <= x0) x1 = x0 + 1;
            uint32_t a = 0, r = 0, gg = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    uint32_t p = src[(size_t)yy * sw + xx];
                    a += p >> 24; r += p >> 16 & 0xff; gg += p >> 8 & 0xff; b += p & 0xff; n++;
                }
            out[(size_t)y * dw + x] = (a / n) << 24 | (r / n) << 16 | (gg / n) << 8 | b / n;
        }
    }
    return out;
}

/* Glifo a color (CBDT) escalado para entrar en max_w × max_h píxeles. argb = NULL si la
 * fuente no tiene bitmap para ese glifo. */
const Glyph *text_glyph_color(int font, uint32_t gid, int max_w, int max_h) {
    if (max_w < 1) max_w = 1;
    if (max_h < 1) max_h = 1;
    if (max_w > 0x3fff) max_w = 0x3fff;
    uint32_t key = GID_BIT | (uint32_t)max_w << 16 | (gid & 0xffff);
    Glyph *g = slot_for(key, font, max_h * 2);
    if (g->used) return g;
    g->cp = key; g->font = (uint16_t)font; g->size_q = (uint16_t)(max_h * 2); g->used = 1;
    cache_used++;
    size_t len;
    int ppem;
    const uint8_t *png = otf_color_png(&fonts[font].otf, (uint16_t)gid, max_h, &len, &ppem);
    int w, h, n;
    unsigned char *rgba = png ? stbi_load_from_memory(png, (int)len, &w, &h, &n, 4) : NULL;
    if (!rgba) return g;
    uint32_t *argb = malloc((size_t)w * h * 4);
    if (argb) {
        for (int i = 0; i < w * h; i++) {
            uint32_t r = rgba[i * 4], gg = rgba[i * 4 + 1], b = rgba[i * 4 + 2], a = rgba[i * 4 + 3];
            argb[i] = a << 24 | (r * a / 255) << 16 | (gg * a / 255) << 8 | (b * a / 255);
        }
        float s = fminf((float)max_w / w, (float)max_h / h);
        int dw = (int)(w * s + 0.5f), dh = (int)(h * s + 0.5f);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        g->argb = resample(argb, w, h, dw, dh);
        free(argb);
        if (g->argb) { g->w = dw; g->h = dh; g->adv = (float)dw; }
    }
    stbi_image_free(rgba);
    return g;
}

static uint32_t utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = s[0];
    int n = 0;
    if (c < 0x80) n = 0;
    else if ((c & 0xe0) == 0xc0) { c &= 0x1f; n = 1; }
    else if ((c & 0xf0) == 0xe0) { c &= 0x0f; n = 2; }
    else if ((c & 0xf8) == 0xf0) { c &= 0x07; n = 3; }
    else { *p += 1; return 0xfffd; }
    for (int i = 1; i <= n; i++) {
        if ((s[i] & 0xc0) != 0x80) { *p += i; return 0xfffd; }
        c = (c << 6) | (s[i] & 0x3f);
    }
    *p += n + 1;
    return c;
}

static float kern(int font, float size, uint32_t a, uint32_t b) {
    if (!a) return 0;
    Font *f = &fonts[font];
    return stbtt_GetCodepointKernAdvance(&f->info, (int)a, (int)b) * stbtt_ScaleForMappingEmToPixels(&f->info, size);
}

float text_line_height(float size, float lh) {
    if (lh <= 0) return roundf(size * 1.25f);
    return lh <= 4 ? roundf(size * lh) : lh;   /* <= 4: multiplicador, como CSS sin unidad */
}

typedef struct { const char *start, *end; float width; } Line;

/* Divide en líneas por '\n' y por ancho máximo, cortando en espacios. */
static int wrap(const char *s, int font, float size, float max_w, int nowrap, Line *out, int max) {
    int n = 0;
    const char *ls = s;
    for (;;) {
        const char *p = ls, *brk = NULL, *end, *next;
        float w = 0, wbrk = 0, lw;
        uint32_t prev = 0;
        int done = 0;
        for (;;) {
            const char *q = p;
            uint32_t cp = utf8_next(&q);
            if (cp == 0) { end = p; lw = w; next = p; done = 1; break; }
            if (cp == '\n') { end = p; lw = w; next = q; break; }
            float adv = text_glyph(cp, font, size)->adv + kern(font, size, prev, cp);
            if (!nowrap && max_w > 0 && w + adv > max_w + 0.5f && p != ls) {
                if (brk) { end = brk; lw = wbrk; next = brk + 1; }
                else     { end = p; lw = w; next = p; }
                break;
            }
            if (cp == ' ') { brk = p; wbrk = w; }
            w += adv;
            prev = cp;
            p = q;
        }
        if (n < max) out[n] = (Line){ ls, end, lw };
        n++;
        if (done) break;
        ls = next;
    }
    return n < max ? n : max;
}

#define MAX_LINES 256
static Line lines[MAX_LINES];

/* Las medidas se recuerdan: el layout se rehace entero con cualquier cambio (un mapa que se mueve,
 * una lista que se desplaza) y volver a medir cada texto con el kerning era lo más caro. Clave:
 * hash de 64 bits del texto y todos los parámetros; tabla de acceso directo. */
#define TM_SETS 4096
#define TM_WAYS 4
static struct { uint64_t key; float w, h; } tm_cache[TM_SETS][TM_WAYS];
static unsigned char tm_next[TM_SETS];

static uint64_t tm_key(const char *s, float weight, float size, float line_h, float max_w, int nowrap) {
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) { h ^= *p; h *= 1099511628211ull; }
    float f[4] = { weight, size, line_h, max_w };
    const unsigned char *b = (const unsigned char *)f;
    for (size_t i = 0; i < sizeof f; i++) { h ^= b[i]; h *= 1099511628211ull; }
    h ^= (uint64_t)(nowrap + 1);
    h *= 1099511628211ull;
    return h | 1;                               /* 0 = lugar vacío */
}

static void text_measure_raw(const char *s, float weight, float size, float line_h, float max_w, int nowrap, float *w, float *h) {
    int font = font_for(weight);
    if (text_needs_layout(s, font)) { text_layout_measure(s, font, size, line_h, max_w, nowrap, w, h); return; }
    int n = wrap(s, font, size, max_w, nowrap, lines, MAX_LINES);
    float mw = 0;
    for (int i = 0; i < n; i++) if (lines[i].width > mw) mw = lines[i].width;
    *w = ceilf(mw);
    *h = n * text_line_height(size, line_h);
}

void text_measure(const char *s, float weight, float size, float line_h, float max_w, int nowrap, float *w, float *h) {
    uint64_t k = tm_key(s, weight, size, line_h, max_w, nowrap);
    unsigned set = (unsigned)(k >> 20) & (TM_SETS - 1);
    for (int i = 0; i < TM_WAYS; i++)
        if (tm_cache[set][i].key == k) { *w = tm_cache[set][i].w; *h = tm_cache[set][i].h; return; }
    text_measure_raw(s, weight, size, line_h, max_w, nowrap, w, h);
    int i = tm_next[set]++ % TM_WAYS;
    tm_cache[set][i].key = k;
    tm_cache[set][i].w = *w;
    tm_cache[set][i].h = *h;
}

void text_draw(Canvas *c, const char *s, float weight, float size, float line_h,
               float x, float y, float box_w, int align, int nowrap, uint32_t argb) {
    int font = font_for(weight);
    if (text_needs_layout(s, font)) { text_layout_draw(c, s, font, size, line_h, x, y, box_w, align, nowrap, argb); return; }
    int n = wrap(s, font, size, box_w, nowrap, lines, MAX_LINES);
    float lh = text_line_height(size, line_h);
    Font *f = &fonts[font];
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, size);
    float asc = f->ascent * scale, desc = f->descent * scale;
    for (int i = 0; i < n; i++) {
        float pen = x;
        if (align == TEXT_CENTER) pen += (box_w - lines[i].width) / 2;
        else if (align == TEXT_RIGHT) pen += box_w - lines[i].width;
        float baseline = roundf(y + i * lh + (lh - (asc - desc)) / 2 + asc);
        if (baseline - asc > c->clip.y1 || baseline - desc < c->clip.y0) continue;
        uint32_t prev = 0;
        for (const char *p = lines[i].start; p < lines[i].end;) {
            uint32_t cp = utf8_next(&p);
            pen += kern(font, size, prev, cp);
            const Glyph *g = text_glyph(cp, font, size);
            if (g->bmp)
                canvas_alpha_mask(c, (int)roundf(pen) + g->xoff, (int)baseline + g->yoff, g->bmp, g->w, g->h, g->w, argb);
            pen += g->adv;
            prev = cp;
        }
    }
}

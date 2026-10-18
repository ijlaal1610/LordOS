/* canvas.c — primitivas con alpha, antialiasing por distancia con signo y clipping. */
#include "canvas.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Alfa de salida de "over" (exacto: un destino opaco sigue en 255). El canvas guarda el alfa
 * premultiplicado: en la pantalla por CPU no importa (KMS es XRGB), y en el camino GPU marca
 * dónde se ven las ventanas de las apps debajo de la UI (src/gpu). */
static inline uint32_t over_alpha(uint32_t sa, uint32_t da) {
    return sa + (((da * (255 - sa) + 128) * 257) >> 16);
}

static inline uint32_t blend(uint32_t dst, uint32_t src, uint32_t a) {
    uint32_t ia = 255 - a;
    uint32_t rb = (((src & 0xff00ff) * a + (dst & 0xff00ff) * ia) >> 8) & 0xff00ff;
    uint32_t g  = (((src & 0x00ff00) * a + (dst & 0x00ff00) * ia) >> 8) & 0x00ff00;
    return over_alpha(a, dst >> 24) << 24 | rb | g;
}

static inline void put(uint32_t *p, uint32_t argb, uint32_t a) {
    if (a >= 255) *p = argb | 0xff000000u;
    else if (a) *p = blend(*p, argb, a);
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- filas: C portable y, en ARM64 con NEON=1 (Makefile), NEON ----
 * Las dos versiones dan el mismo resultado bit a bit para orígenes premultiplicados válidos;
 * lo comprueba tests/canvas_test.c (make build/canvas-test). Ver docs/RASPBERRY.md. */

/* Una fila de canvas_blit sin esquinas: origen premultiplicado escalado por k (0..256), "over". */
static void blit_row_c(uint32_t *row, const uint32_t *s, int n, uint32_t k) {
    if (!k) return;
    for (int i = 0; i < n; i++) {
        uint32_t p = s[i];
        if (!(p >> 24)) continue;
        if (k >= 256 && p >= 0xff000000u) { row[i] = p; continue; }     /* opaco: se copia (mismo resultado) */
        uint32_t sa = ((p >> 24) * k) >> 8;
        uint32_t srb = (((p & 0xff00ff) * k) >> 8) & 0xff00ff, sg = (((p & 0xff00) * k) >> 8) & 0xff00;
        uint32_t ia = 255 - sa, d = row[i];
        uint32_t drb = (((d & 0xff00ff) * ia) >> 8) & 0xff00ff, dg = (((d & 0xff00) * ia) >> 8) & 0xff00;
        row[i] = over_alpha(sa, d >> 24) << 24 | (srb + drb) | (sg + dg);
    }
}

/* Una fila de canvas_alpha_mask: color argb con la cobertura m (0..255) por su alfa ca. */
static void mask_row_c(uint32_t *row, const uint8_t *m, int n, uint32_t argb, uint32_t ca) {
    for (int i = 0; i < n; i++)
        if (m[i]) put(&row[i], argb, (m[i] * ca + 127) / 255);
}

#if defined(LORD_NEON) && defined(__ARM_NEON)
#include <arm_neon.h>

static void blit_row_neon(uint32_t *row, const uint32_t *s, int n, uint32_t k) {
    if (!k) return;
    const uint16x8_t kk = vdupq_n_u16((uint16_t)k);
    const uint32x4_t c255 = vdupq_n_u32(255), c128 = vdupq_n_u32(128), zero = vdupq_n_u32(0), rgb = vdupq_n_u32(0x00ffffff);
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32x4_t p = vld1q_u32(s + i), d = vld1q_u32(row + i);
        uint8x16_t p8 = vreinterpretq_u8_u32(p), d8 = vreinterpretq_u8_u32(d);
        /* (c·k) >> 8 en los cuatro canales (c·k ≤ 65280: entra en 16 bits) */
        uint16x8_t lo = vmulq_u16(vmovl_u8(vget_low_u8(p8)), kk), hi = vmulq_u16(vmovl_u8(vget_high_u8(p8)), kk);
        uint8x16_t s8 = vcombine_u8(vshrn_n_u16(lo, 8), vshrn_n_u16(hi, 8));
        uint32x4_t sa = vshrq_n_u32(vreinterpretq_u32_u8(s8), 24), ia = vsubq_u32(c255, sa);
        uint8x16_t ia8 = vreinterpretq_u8_u32(vmulq_n_u32(ia, 0x01010101u));
        uint16x8_t dl = vmull_u8(vget_low_u8(d8), vget_low_u8(ia8)), dh = vmull_u8(vget_high_u8(d8), vget_high_u8(ia8));
        uint8x16_t dd = vcombine_u8(vshrn_n_u16(dl, 8), vshrn_n_u16(dh, 8));
        uint32x4_t col = vandq_u32(vreinterpretq_u32_u8(vaddq_u8(s8, dd)), rgb);
        /* alfa: over_alpha(sa, da) en 32 bits */
        uint32x4_t x = vmlaq_u32(c128, vshrq_n_u32(d, 24), ia);
        uint32x4_t a = vaddq_u32(sa, vshrq_n_u32(vmulq_n_u32(x, 257), 16));
        uint32x4_t out = vorrq_u32(col, vshlq_n_u32(a, 24));
        out = vbslq_u32(vceqq_u32(vshrq_n_u32(p, 24), zero), d, out);     /* origen transparente: igual */
        vst1q_u32(row + i, out);
    }
    blit_row_c(row + i, s + i, n - i, k);
}

static void mask_row_neon(uint32_t *row, const uint8_t *m, int n, uint32_t argb, uint32_t ca) {
    const uint32x4_t vca = vdupq_n_u32(ca), c127 = vdupq_n_u32(127), c128 = vdupq_n_u32(128), c255 = vdupq_n_u32(255);
    const uint32x4_t zero = vdupq_n_u32(0), rgb = vdupq_n_u32(0x00ffffff), full = vdupq_n_u32(argb | 0xff000000u);
    const uint8x16_t src8 = vreinterpretq_u8_u32(vdupq_n_u32(argb));
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32_t mm;
        memcpy(&mm, m + i, 4);
        if (!mm) continue;
        uint32x4_t mv = vmovl_u16(vget_low_u16(vmovl_u8(vreinterpret_u8_u32(vdup_n_u32(mm)))));
        /* (m·ca + 127) / 255 exacto: x·0x8081 >> 23 para x < 65536 */
        uint32x4_t a = vshrq_n_u32(vmulq_n_u32(vmlaq_u32(c127, mv, vca), 0x8081u), 23), ia = vsubq_u32(c255, a);
        uint8x16_t a8 = vreinterpretq_u8_u32(vmulq_n_u32(a, 0x01010101u)), ia8 = vreinterpretq_u8_u32(vmulq_n_u32(ia, 0x01010101u));
        uint32x4_t d = vld1q_u32(row + i);
        uint8x16_t d8 = vreinterpretq_u8_u32(d);
        uint16x8_t lo = vmlal_u8(vmull_u8(vget_low_u8(src8), vget_low_u8(a8)), vget_low_u8(d8), vget_low_u8(ia8));
        uint16x8_t hi = vmlal_u8(vmull_u8(vget_high_u8(src8), vget_high_u8(a8)), vget_high_u8(d8), vget_high_u8(ia8));
        uint32x4_t col = vandq_u32(vreinterpretq_u32_u8(vcombine_u8(vshrn_n_u16(lo, 8), vshrn_n_u16(hi, 8))), rgb);
        uint32x4_t al = vaddq_u32(a, vshrq_n_u32(vmulq_n_u32(vmlaq_u32(c128, vshrq_n_u32(d, 24), ia), 257), 16));
        uint32x4_t out = vorrq_u32(col, vshlq_n_u32(al, 24));
        out = vbslq_u32(vcgeq_u32(a, c255), full, out);                   /* cobertura total */
        out = vbslq_u32(vceqq_u32(a, zero), d, out);                      /* nada */
        vst1q_u32(row + i, out);
    }
    mask_row_c(row + i, m + i, n - i, argb, ca);
}
#  define blit_row blit_row_neon
#  define mask_row mask_row_neon
#  define CANVAS_SIMD "NEON"
#else
#  define blit_row blit_row_c
#  define mask_row mask_row_c
#  define CANVAS_SIMD "C"
#endif

#ifdef LORD_CANVAS_TEST
const char *canvas_test_simd(void) { return CANVAS_SIMD; }
void canvas_test_blit_row(int simd, uint32_t *row, const uint32_t *s, int n, uint32_t k) {
    if (simd) blit_row(row, s, n, k); else blit_row_c(row, s, n, k);
}
void canvas_test_mask_row(int simd, uint32_t *row, const uint8_t *m, int n, uint32_t argb) {
    if (simd) mask_row(row, m, n, argb, argb >> 24); else mask_row_c(row, m, n, argb, argb >> 24);
}
#endif

/* Distancia con signo al rectángulo redondeado (negativa adentro). */
static inline float rr_sdf(float px, float py, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return (ox || oy ? sqrtf(ox * ox + oy * oy) : 0) + (in < 0 ? in : 0) - r;
}

static inline float cov_of(float d) { float c = 0.5f - d; return c <= 0 ? 0 : c >= 1 ? 1 : c; }

static float fix_radius(float r, float w, float h) {
    float m = (w < h ? w : h) * 0.5f;
    return r < 0 ? 0 : r > m ? m : r;
}

/* Recorta el bounding box [x, x+w) x [y, y+h) contra el clip. */
static int bounds(const Canvas *c, float x, float y, float w, float h, int *x0, int *y0, int *x1, int *y1) {
    *x0 = clampi((int)floorf(x), c->clip.x0, c->clip.x1);
    *y0 = clampi((int)floorf(y), c->clip.y0, c->clip.y1);
    *x1 = clampi((int)ceilf(x + w), c->clip.x0, c->clip.x1);
    *y1 = clampi((int)ceilf(y + h), c->clip.y0, c->clip.y1);
    return *x0 < *x1 && *y0 < *y1;
}

void canvas_init(Canvas *c, uint32_t *px, int w, int h, int stride) {
    c->px = px; c->w = w; c->h = h; c->stride = stride;
    c->clip = (Rect){0, 0, w, h};
}

Rect canvas_push_clip(Canvas *c, float x, float y, float w, float h) {
    Rect old = c->clip;
    Rect n = { (int)floorf(x), (int)floorf(y), (int)ceilf(x + w), (int)ceilf(y + h) };
    if (n.x0 < old.x0) n.x0 = old.x0;
    if (n.y0 < old.y0) n.y0 = old.y0;
    if (n.x1 > old.x1) n.x1 = old.x1;
    if (n.y1 > old.y1) n.y1 = old.y1;
    if (n.x1 < n.x0) n.x1 = n.x0;
    if (n.y1 < n.y0) n.y1 = n.y0;
    c->clip = n;
    return old;
}

void canvas_set_clip(Canvas *c, Rect r) { c->clip = r; }

void canvas_copy(Canvas *dst, const Canvas *src) {
    int w = dst->w < src->w ? dst->w : src->w, h = dst->h < src->h ? dst->h : src->h;
    for (int y = 0; y < h; y++)
        memcpy(dst->px + (size_t)y * dst->stride, src->px + (size_t)y * src->stride, (size_t)w * 4);
}

void canvas_clear(Canvas *c, uint32_t rgb) {
    for (int y = c->clip.y0; y < c->clip.y1; y++) {
        uint32_t *row = c->px + (size_t)y * c->stride;
        for (int x = c->clip.x0; x < c->clip.x1; x++) row[x] = rgb;
    }
}

void canvas_rrect(Canvas *c, float x, float y, float w, float h, float r, uint32_t argb) {
    uint32_t a = argb >> 24;
    int x0, y0, x1, y1;
    if (!a || w <= 0 || h <= 0 || !bounds(c, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    r = fix_radius(r, w, h);
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    int fx0 = (int)ceilf(x), fx1 = (int)floorf(x + w);   /* columnas totalmente cubiertas */
    for (int j = y0; j < y1; j++) {
        uint32_t *row = c->px + (size_t)j * c->stride;
        if (j >= y + r && j + 1 <= y + h - r) {
            /* Franja recta: solo los bordes izquierdo/derecho son parciales; el medio, de corrido. */
            int m0 = fx0 > x0 ? fx0 : x0, m1 = fx1 < x1 ? fx1 : x1;
            if (m1 < m0) m1 = m0;
            for (int i = x0; i < m0; i++) {
                float cov = fminf(i + 1.0f, x + w) - fmaxf((float)i, x);
                if (cov > 0) put(&row[i], argb, (uint32_t)(a * cov + 0.5f));
            }
            if (a >= 255) { uint32_t v = argb | 0xff000000u; for (int i = m0; i < m1; i++) row[i] = v; }
            else for (int i = m0; i < m1; i++) row[i] = blend(row[i], argb, a);
            for (int i = m1; i < x1; i++) {
                float cov = fminf(i + 1.0f, x + w) - fmaxf((float)i, x);
                if (cov > 0) put(&row[i], argb, (uint32_t)(a * cov + 0.5f));
            }
        } else {
            float py = j + 0.5f;
            for (int i = x0; i < x1; i++) {
                float cov = cov_of(rr_sdf(i + 0.5f, py, cx, cy, hw, hh, r));
                if (cov > 0) put(&row[i], argb, (uint32_t)(a * cov + 0.5f));
            }
        }
    }
}

static inline uint32_t lerp_color(uint32_t a, uint32_t b, float t) {
    int ti = (int)(t * 256);
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        int ca = (a >> s) & 255, cb = (b >> s) & 255;
        out |= (uint32_t)(ca + (((cb - ca) * ti) >> 8)) << s;
    }
    return out;
}

void canvas_rrect_gradient(Canvas *c, float x, float y, float w, float h, float r,
                           uint32_t from, uint32_t to, float angle_deg, float opacity) {
    int x0, y0, x1, y1;
    if (w <= 0 || h <= 0 || !bounds(c, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    r = fix_radius(r, w, h);
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    /* Igual que CSS: 180deg = de arriba hacia abajo. */
    float rad = angle_deg * 3.14159265f / 180.0f, dx = sinf(rad), dy = -cosf(rad);
    float len = fabsf(w * dx) + fabsf(h * dy);
    if (len <= 0) len = 1;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = c->px + (size_t)j * c->stride;
        float py = j + 0.5f;
        for (int i = x0; i < x1; i++) {
            float px = i + 0.5f;
            float cov = cov_of(rr_sdf(px, py, cx, cy, hw, hh, r));
            if (cov <= 0) continue;
            float t = 0.5f + ((px - cx) * dx + (py - cy) * dy) / len;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            uint32_t col = lerp_color(from, to, t);
            put(&row[i], col, (uint32_t)((col >> 24) * cov * opacity + 0.5f));
        }
    }
}

void canvas_rrect_stroke(Canvas *c, float x, float y, float w, float h, float r, float bw, uint32_t argb) {
    uint32_t a = argb >> 24;
    int x0, y0, x1, y1;
    if (!a || bw <= 0 || w <= 0 || h <= 0 || !bounds(c, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    r = fix_radius(r, w, h);
    float ir = r - bw > 0 ? r - bw : 0;
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    float edge = (r > bw ? r : bw) + 1;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = c->px + (size_t)j * c->stride;
        float py = j + 0.5f;
        int middle = py > y + edge && py < y + h - edge;
        for (int i = x0; i < x1; i++) {
            float px = i + 0.5f;
            if (middle && px > x + bw + 1 && px < x + w - bw - 1) {
                /* saltea el centro: hasta el último píxel cuyo centro queda antes del borde
                 * derecho (nunca hacia atrás: con coordenadas fraccionarias eso era un bucle) */
                int last = (int)ceilf(x + w - bw - 1 - 0.5f) - 1;
                if (last > i) i = last;
                continue;
            }
            float outer = cov_of(rr_sdf(px, py, cx, cy, hw, hh, r));
            float inner = cov_of(rr_sdf(px, py, cx, cy, hw - bw, hh - bw, ir));
            float cov = outer - inner;
            if (cov > 0) put(&row[i], argb, (uint32_t)(a * cov + 0.5f));
        }
    }
}

/* Máscaras de sombra cacheadas por tamaño: al mover una ventana la sombra no se recalcula. */
typedef struct {
    int w, h, r, blur, dx, dy;
    int x0, y0, mw, mh;
    uint8_t *a;
    unsigned long used;
} ShadowMask;

#define SHADOW_CACHE 32
static ShadowMask shadow_cache[SHADOW_CACHE];
static unsigned long shadow_stamp;

static ShadowMask *shadow_mask(int w, int h, int r, int blur, int dx, int dy) {
    ShadowMask *m = NULL, *lru = &shadow_cache[0];
    for (int i = 0; i < SHADOW_CACHE; i++) {
        ShadowMask *k = &shadow_cache[i];
        if (k->a && k->w == w && k->h == h && k->r == r && k->blur == blur && k->dx == dx && k->dy == dy) { m = k; break; }
        if (k->used < lru->used) lru = k;
    }
    if (!m) {
        m = lru;
        free(m->a);
        *m = (ShadowMask){ w, h, r, blur, dx, dy, 0, 0, 0, 0, NULL, 0 };
        m->x0 = (dx < 0 ? dx : 0) - blur;
        m->y0 = (dy < 0 ? dy : 0) - blur;
        m->mw = w + abs(dx) + 2 * blur;
        m->mh = h + abs(dy) + 2 * blur;
        m->a = calloc((size_t)m->mw * m->mh, 1);
        if (!m->a) return NULL;
        float fr = fix_radius((float)r, (float)w, (float)h), fb = (float)blur;
        float bcx = w * 0.5f, bcy = h * 0.5f, scx = bcx + dx, scy = bcy + dy, hw = w * 0.5f, hh = h * 0.5f;
        for (int j = 0; j < m->mh; j++)
            for (int i = 0; i < m->mw; i++) {
                float px = m->x0 + i + 0.5f, py = m->y0 + j + 0.5f;
                float inside = cov_of(rr_sdf(px, py, bcx, bcy, hw, hh, fr));
                if (inside >= 1) continue;
                float t = (rr_sdf(px, py, scx, scy, hw, hh, fr) + fb) / (2 * fb);
                if (t >= 1) continue;
                if (t < 0) t = 0;
                float f = (1 - t * t * (3 - 2 * t)) * (1 - inside);
                m->a[(size_t)j * m->mw + i] = (uint8_t)(f * 255 + 0.5f);
            }
    }
    m->used = ++shadow_stamp;
    return m;
}

void canvas_shadow(Canvas *c, float x, float y, float w, float h, float r,
                   float dx, float dy, float blur, uint32_t argb) {
    if (!(argb >> 24) || w <= 0 || h <= 0) return;
    int b = blur < 1 ? 1 : (int)roundf(blur);
    ShadowMask *m = shadow_mask((int)roundf(w), (int)roundf(h), (int)roundf(r), b, (int)roundf(dx), (int)roundf(dy));
    if (m) canvas_alpha_mask(c, (int)roundf(x) + m->x0, (int)roundf(y) + m->y0, m->a, m->mw, m->mh, m->mw, argb);
}

/* Box blur separable, 3 pasadas ≈ gaussiano. */
static void box_pass(uint32_t *buf, int n, int step, int rad, uint32_t *tmp) {
    for (int i = 0; i < n; i++) tmp[i] = buf[(size_t)i * step];
    int win = 2 * rad + 1;
    int sa = 0, sr = 0, sg = 0, sb = 0;          /* con alfa: ver canvas_backdrop_blur */
    for (int k = -rad; k <= rad; k++) {
        uint32_t p = tmp[clampi(k, 0, n - 1)];
        sa += p >> 24; sr += (p >> 16) & 255; sg += (p >> 8) & 255; sb += p & 255;
    }
    for (int i = 0; i < n; i++) {
        buf[(size_t)i * step] = (uint32_t)((sa / win) << 24 | (sr / win) << 16 | (sg / win) << 8 | (sb / win));
        uint32_t add = tmp[clampi(i + rad + 1, 0, n - 1)], sub = tmp[clampi(i - rad, 0, n - 1)];
        sa += (int)(add >> 24) - (int)(sub >> 24);
        sr += (int)((add >> 16) & 255) - (int)((sub >> 16) & 255);
        sg += (int)((add >> 8) & 255) - (int)((sub >> 8) & 255);
        sb += (int)(add & 255) - (int)(sub & 255);
    }
}

/* Muestra bilineal de un buffer XRGB. */
static uint32_t sample_rgb(const uint32_t *buf, int w, int h, float u, float v) {
    if (u < 0) u = 0;
    if (v < 0) v = 0;
    if (u > w - 1) u = (float)(w - 1);
    if (v > h - 1) v = (float)(h - 1);
    int x0 = (int)u, y0 = (int)v, x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
    int fx = (int)((u - x0) * 256), fy = (int)((v - y0) * 256);
    uint32_t p00 = buf[(size_t)y0 * w + x0], p01 = buf[(size_t)y0 * w + x1];
    uint32_t p10 = buf[(size_t)y1 * w + x0], p11 = buf[(size_t)y1 * w + x1];
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        int a = (p00 >> s) & 255, b = (p01 >> s) & 255, c = (p10 >> s) & 255, d = (p11 >> s) & 255;
        int top = a * 256 + (b - a) * fx, bot = c * 256 + (d - c) * fx;
        out |= (uint32_t)(((top * 256 + (bot - top) * fy) >> 16) & 255) << s;
    }
    return out;
}

void canvas_backdrop_blur(Canvas *c, float x, float y, float w, float h, float r, int radius) {
    int x0, y0, x1, y1;
    if (radius < 1 || w <= 0 || h <= 0 || !bounds(c, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    /* Margen para que el borde tome color de afuera, como en CSS. */
    int m = radius * 2;
    int bx0 = clampi(x0 - m, 0, c->w), by0 = clampi(y0 - m, 0, c->h);
    int bx1 = clampi(x1 + m, 0, c->w), by1 = clampi(y1 + m, 0, c->h);
    /* Se desenfoca a resolución reducida: mismo resultado visual, mucho más barato. */
    int f = radius >= 12 ? 4 : radius >= 6 ? 2 : 1;
    int sw = (bx1 - bx0 + f - 1) / f, sh = (by1 - by0 + f - 1) / f;
    uint32_t *buf = malloc((size_t)sw * sh * 4);
    uint32_t *tmp = malloc((size_t)(sw > sh ? sw : sh) * 4);
    if (!buf || !tmp) { free(buf); free(tmp); return; }
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sw; i++) {
            uint32_t acc[4] = {0}, n = 0;
            for (int v = 0; v < f; v++) {
                int py = by0 + j * f + v;
                if (py >= by1) break;
                const uint32_t *row = c->px + (size_t)py * c->stride;
                for (int u = 0; u < f; u++) {
                    int px = bx0 + i * f + u;
                    if (px >= bx1) break;
                    uint32_t p = row[px];
                    acc[0] += (p >> 16) & 255; acc[1] += (p >> 8) & 255; acc[2] += p & 255; acc[3] += p >> 24;
                    n++;
                }
            }
            buf[(size_t)j * sw + i] = n ? (acc[3] / n) << 24 | (acc[0] / n) << 16 | (acc[1] / n) << 8 | (acc[2] / n) : 0;
        }
    int rad = radius / (2 * f) > 0 ? radius / (2 * f) : 1;
    for (int pass = 0; pass < 3; pass++) {
        for (int j = 0; j < sh; j++) box_pass(buf + (size_t)j * sw, sw, 1, rad, tmp);
        for (int i = 0; i < sw; i++) box_pass(buf + i, sh, sw, rad, tmp);
    }
    r = fix_radius(r, w, h);
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    float inv = 1.0f / f;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = c->px + (size_t)j * c->stride;
        float v = (j - by0 + 0.5f) * inv - 0.5f;
        int corner_row = j < y + r || j + 1 > y + h - r;
        for (int i = x0; i < x1; i++) {
            uint32_t a = 255;
            if (corner_row || i < x + 1 || i + 1 > x + w - 1) {
                float cov = cov_of(rr_sdf(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r));
                if (cov <= 0) continue;
                a = (uint32_t)(cov * 255 + 0.5f);
            }
            /* premultiplicado con su alfa: sobre la UI opaca es igual que antes; sobre el hueco de
             * una ventana de app (camino GPU) queda transparente y se ve la app debajo */
            uint32_t p = sample_rgb(buf, sw, sh, (i - bx0 + 0.5f) * inv - 0.5f, v), d = row[i];
            if (a >= 255 && (p >> 24) >= 255) { row[i] = p; continue; }
            uint32_t pa = ((p >> 24) * a + 127) / 255;
            uint32_t prb = (((p & 0xff00ff) * a) / 255) & 0xff00ff, pg = (((p & 0xff00) * a) / 255) & 0xff00;
            uint32_t ia = 255 - pa;
            uint32_t drb = (((d & 0xff00ff) * ia) >> 8) & 0xff00ff, dg = (((d & 0xff00) * ia) >> 8) & 0xff00;
            row[i] = over_alpha(pa, d >> 24) << 24 | (prb + drb) | (pg + dg);
        }
    }
    free(buf);
    free(tmp);
}

void canvas_corners_save(Canvas *c, Corners *k, float x, float y, float w, float h, float r) {
    r = fix_radius(r, w, h);
    k->n = (int)ceilf(r) + 1;
    k->buf = NULL;
    if (r <= 0) return;
    k->buf = malloc((size_t)k->n * k->n * 4 * 4);
    if (!k->buf) return;
    int xs[4] = { (int)floorf(x), (int)ceilf(x + w) - k->n, (int)floorf(x), (int)ceilf(x + w) - k->n };
    int ys[4] = { (int)floorf(y), (int)floorf(y), (int)ceilf(y + h) - k->n, (int)ceilf(y + h) - k->n };
    for (int q = 0; q < 4; q++) {
        k->x[q] = xs[q]; k->y[q] = ys[q];
        for (int j = 0; j < k->n; j++)
            for (int i = 0; i < k->n; i++) {
                int px = xs[q] + i, py = ys[q] + j;
                uint32_t v = 0;
                if (px >= 0 && py >= 0 && px < c->w && py < c->h) v = c->px[(size_t)py * c->stride + px];
                k->buf[((size_t)q * k->n + j) * k->n + i] = v;
            }
    }
}

void canvas_corners_restore(Canvas *c, Corners *k, float x, float y, float w, float h, float r) {
    if (!k->buf) return;
    r = fix_radius(r, w, h);
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    for (int q = 0; q < 4; q++)
        for (int j = 0; j < k->n; j++)
            for (int i = 0; i < k->n; i++) {
                int px = k->x[q] + i, py = k->y[q] + j;
                if (px < c->clip.x0 || py < c->clip.y0 || px >= c->clip.x1 || py >= c->clip.y1) continue;
                float cov = cov_of(rr_sdf(px + 0.5f, py + 0.5f, cx, cy, hw, hh, r));
                if (cov >= 1) continue;
                uint32_t *d = &c->px[(size_t)py * c->stride + px];
                *d = blend(*d, k->buf[((size_t)q * k->n + j) * k->n + i], (uint32_t)((1 - cov) * 255 + 0.5f));
            }
    free(k->buf);
    k->buf = NULL;
}

void canvas_alpha_mask(Canvas *c, int x, int y, const uint8_t *a, int w, int h, int stride, uint32_t argb) {
    uint32_t ca = argb >> 24;
    int x0 = clampi(x, c->clip.x0, c->clip.x1), x1 = clampi(x + w, c->clip.x0, c->clip.x1);
    int y0 = clampi(y, c->clip.y0, c->clip.y1), y1 = clampi(y + h, c->clip.y0, c->clip.y1);
    for (int j = y0; j < y1; j++) {
        uint32_t *row = c->px + (size_t)j * c->stride;
        const uint8_t *m = a + (size_t)(j - y) * stride - x;
        if (x1 > x0) mask_row(row + x0, m + x0, x1 - x0, argb, ca);
    }
}

void canvas_blit(Canvas *c, const uint32_t *src, int sw, int sh, int x, int y, float r, float opacity, int opaque) {
    int x0 = clampi(x, c->clip.x0, c->clip.x1), x1 = clampi(x + sw, c->clip.x0, c->clip.x1);
    int y0 = clampi(y, c->clip.y0, c->clip.y1), y1 = clampi(y + sh, c->clip.y0, c->clip.y1);
    if (x0 >= x1 || y0 >= y1) return;
    r = fix_radius(r, (float)sw, (float)sh);
    uint32_t k_op = (uint32_t)(opacity * 256 + 0.5f);
    if (opaque && r <= 0 && k_op >= 256) {
        for (int j = y0; j < y1; j++)
            memcpy(c->px + (size_t)j * c->stride + x0, src + (size_t)(j - y) * sw + (x0 - x), (size_t)(x1 - x0) * 4);
        return;
    }
    float cx = x + sw * 0.5f, cy = y + sh * 0.5f, hw = sw * 0.5f, hh = sh * 0.5f;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = c->px + (size_t)j * c->stride;
        const uint32_t *s = src + (size_t)(j - y) * sw - x;
        int corner_row = j < y + r || j >= y + sh - r;
        if (!corner_row || r <= 0) { blit_row(row + x0, s + x0, x1 - x0, k_op); continue; }   /* sin esquinas */
        for (int i = x0; i < x1; i++) {
            uint32_t p = s[i];
            if (!(p >> 24)) continue;
            uint32_t k = k_op;
            if (corner_row && r > 0 && (i < x + r || i >= x + sw - r))
                k = (uint32_t)(k * cov_of(rr_sdf(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r)));
            if (!k) continue;
            /* src premultiplicado escalado por k, luego "over". */
            uint32_t sa = ((p >> 24) * k) >> 8;
            uint32_t srb = (((p & 0xff00ff) * k) >> 8) & 0xff00ff, sg = (((p & 0xff00) * k) >> 8) & 0xff00;
            uint32_t ia = 255 - sa, d = row[i];
            uint32_t drb = (((d & 0xff00ff) * ia) >> 8) & 0xff00ff, dg = (((d & 0xff00) * ia) >> 8) & 0xff00;
            row[i] = over_alpha(sa, d >> 24) << 24 | (srb + drb) | (sg + dg);
        }
    }
}

void canvas_bitmap(Canvas *c, int x, int y, const char *const *rows, uint32_t fg, uint32_t bg) {
    for (int j = 0; rows[j]; j++) {
        int py = y + j;
        if (py < c->clip.y0 || py >= c->clip.y1) continue;
        for (int i = 0; rows[j][i]; i++) {
            int px = x + i;
            if (px < c->clip.x0 || px >= c->clip.x1) continue;
            char ch = rows[j][i];
            if (ch == 'X') c->px[(size_t)py * c->stride + px] = fg;
            else if (ch == 'o') c->px[(size_t)py * c->stride + px] = bg;
        }
    }
}

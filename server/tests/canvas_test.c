/* canvas_test.c — las filas de canvas.c con SIMD (NEON en ARM64, NEON=1) tienen que dar lo mismo,
 * bit a bit, que las de C portable. En x86-64 (sin SIMD propio) compara C contra C y además
 * comprueba la división exacta por 255 que usa la versión NEON. Ver docs/RASPBERRY.md.
 *
 *   make -C server NEON=1 build/canvas-test && server/build/canvas-test   (en la Raspberry Pi)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *canvas_test_simd(void);
void canvas_test_blit_row(int simd, uint32_t *row, const uint32_t *s, int n, uint32_t k);
void canvas_test_mask_row(int simd, uint32_t *row, const uint8_t *m, int n, uint32_t argb);

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* píxel premultiplicado válido (cada canal ≤ alfa); a veces opaco o transparente */
static uint32_t premul(void) {
    uint32_t r = rnd() % 8, a = r == 0 ? 0 : r == 1 ? 255 : rnd() & 255;
    uint32_t c0 = a ? rnd() % (a + 1) : 0, c1 = a ? rnd() % (a + 1) : 0, c2 = a ? rnd() % (a + 1) : 0;
    return a << 24 | c0 << 16 | c1 << 8 | c2;
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(void) {
    int fails = 0;
    /* (x·0x8081) >> 23 == x / 255 en todo el rango que usa mask_row (m·ca + 127 ≤ 65152) */
    for (uint32_t x = 0; x < 65536; x++)
        if ((x * 0x8081u) >> 23 != x / 255) { printf("división por 255: falla en %u\n", x); fails++; break; }

    enum { N = 1037 };                      /* no múltiplo de 4: prueba también la cola */
    static uint32_t src[N], dst[N], a[N], b[N];
    static uint8_t mask[N];
    for (int it = 0; it < 4000; it++) {
        int n = 1 + (int)(rnd() % N);
        uint32_t k = it % 5 == 0 ? 256 : it % 7 == 0 ? 0 : rnd() % 257;
        for (int i = 0; i < n; i++) { src[i] = premul(); dst[i] = rnd(); }
        memcpy(a, dst, sizeof dst);
        memcpy(b, dst, sizeof dst);
        canvas_test_blit_row(0, a, src, n, k);
        canvas_test_blit_row(1, b, src, n, k);
        for (int i = 0; i < n; i++)
            if (a[i] != b[i]) {
                if (fails++ < 10) printf("blit: i=%d k=%u src=%08x dst=%08x C=%08x SIMD=%08x\n", i, k, src[i], dst[i], a[i], b[i]);
                break;
            }
        uint32_t argb = rnd();
        if (it % 6 == 0) argb |= 0xff000000u;
        for (int i = 0; i < n; i++) { uint32_t r = rnd() % 4; mask[i] = r == 0 ? 0 : r == 1 ? 255 : (uint8_t)rnd(); }
        memcpy(a, dst, sizeof dst);
        memcpy(b, dst, sizeof dst);
        canvas_test_mask_row(0, a, mask, n, argb);
        canvas_test_mask_row(1, b, mask, n, argb);
        for (int i = 0; i < n; i++)
            if (a[i] != b[i]) {
                if (fails++ < 10) printf("mask: i=%d argb=%08x m=%u dst=%08x C=%08x SIMD=%08x\n", i, argb, mask[i], dst[i], a[i], b[i]);
                break;
            }
    }

    /* tiempo de una pantalla 1920×1080 de cada una (no es una prueba: mide) */
    static uint32_t big_s[1920], big_d[1920];
    static uint8_t big_m[1920];
    for (int i = 0; i < 1920; i++) { big_s[i] = premul(); big_d[i] = rnd(); big_m[i] = (uint8_t)rnd(); }
    for (int simd = 0; simd < 2; simd++) {
        double t0 = now();
        for (int f = 0; f < 20; f++)
            for (int y = 0; y < 1080; y++) canvas_test_blit_row(simd, big_d, big_s, 1920, 200);
        double t1 = now();
        for (int f = 0; f < 20; f++)
            for (int y = 0; y < 1080; y++) canvas_test_mask_row(simd, big_d, big_m, 1920, 0x80336699u);
        double t2 = now();
        printf("%s: blit %.2f ms/pantalla, máscara %.2f ms/pantalla\n", simd ? canvas_test_simd() : "C",
               (t1 - t0) * 1000 / 20, (t2 - t1) * 1000 / 20);
    }
    if (fails) printf("canvas-test: %d diferencias\n", fails);
    else printf("canvas-test: %s igual a C\n", canvas_test_simd());
    return fails != 0;
}

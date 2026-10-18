/* mmdb_test.c — prueba del lector de MaxMind DB (src/monitor/mmdb.c).
 *
 *   build/mmdb-test [archivo.mmdb[.xz]] [original.mmdb]
 *   (por defecto ../assets/monitor/geoip-country.mmdb.xz; con un segundo archivo, por ejemplo el
 *   dbip-country-lite.mmdb completo, compara 200 000 direcciones al azar y muestra cuántas coinciden)
 *
 * Busca direcciones conocidas, y después rompe el archivo de varias formas (cortado, bytes al
 * azar, punteros y tamaños falsos) para comprobar que el lector nunca lee fuera del búfer: se
 * corre también con -fsanitize=address en el desarrollo. Sale con 1 si algo falla. */
#include "monitor/mmdb.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(const Mmdb *m, const char *ip, const char *want) {
    uint8_t a[16];
    int fam = strchr(ip, ':') ? 6 : 4;
    if (inet_pton(fam == 4 ? AF_INET : AF_INET6, ip, a) != 1) { printf("✗ %s: dirección inválida\n", ip); fails++; return; }
    char cc[3];
    int r = mmdb_country(m, a, fam, cc);
    const char *got = r == 1 ? cc : "";
    if (strcmp(got, want)) { printf("✗ %s → '%s' (esperado '%s', r=%d)\n", ip, got, want, r); fails++; }
    else printf("✓ %s → %s\n", ip, want[0] ? want : "(sin datos)");
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "../assets/monitor/geoip-country.mmdb.xz";
    Mmdb m;
    if (mmdb_open(&m, path)) { printf("✗ no se pudo abrir %s\n", path); return 1; }
    printf("%s: %s, %u nodos, registros de %u bits, IPv%u\n", path, m.type, m.node_count, m.record_size, m.ip_version);
    expect(&m, "8.8.8.8", "US");                  /* Google DNS */
    expect(&m, "1.1.1.1", "AU");                  /* APNIC/Cloudflare: AU en DB-IP */
    expect(&m, "200.45.0.1", "AR");               /* Telecom Argentina */
    expect(&m, "177.10.0.1", "BR");
    expect(&m, "193.0.6.139", "NL");              /* RIPE NCC, Ámsterdam */
    expect(&m, "210.130.0.1", "JP");
    expect(&m, "2800:810::1", "AR");              /* LACNIC → Argentina */
    expect(&m, "2a01:4f8::1", "DE");              /* Hetzner */
    expect(&m, "10.0.0.1", "");                   /* privada: sin datos */
    expect(&m, "127.0.0.1", "");
    expect(&m, "::1", "");

    /* archivos rotos: ninguna búsqueda puede leer fuera del búfer */
    uint8_t *copy = malloc(m.len);
    const char *probe[] = { "8.8.8.8", "200.45.0.1", "2800:810::1", "193.0.6.139" };
    srand(1234);
    int opened = 0;
    for (int round = 0; round < 400; round++) {
        size_t len = m.len;
        memcpy(copy, m.buf, len);
        if (round % 4 == 0) len = (size_t)rand() % len;                         /* cortado */
        else for (int k = 0; k < 64; k++) copy[(size_t)rand() % len] = (uint8_t)rand();
        if (round % 4 == 3) {                                                     /* metadatos falsos */
            for (size_t k = len - 200; k < len; k++) copy[k] = (uint8_t)rand();
        }
        uint8_t *exact = malloc(len ? len : 1);                                  /* sin holgura: ASan avisa */
        memcpy(exact, copy, len);
        Mmdb b;
        if (!mmdb_open_mem(&b, exact, len)) {
            opened++;
            for (int k = 0; k < 4; k++) {
                uint8_t a[16];
                int fam = strchr(probe[k], ':') ? 6 : 4;
                inet_pton(fam == 4 ? AF_INET : AF_INET6, probe[k], a);
                char cc[3];
                mmdb_country(&b, a, fam, cc);
            }
            for (int k = 0; k < 200; k++) {                                       /* direcciones al azar */
                uint8_t a[16];
                for (int j = 0; j < 16; j++) a[j] = (uint8_t)rand();
                char cc[3];
                mmdb_country(&b, a, k & 1 ? 6 : 4, cc);
            }
        }
        free(exact);
    }
    printf("✓ 400 archivos rotos sin lecturas fuera del búfer (%d todavía abrían)\n", opened);
    /* parámetros malos */
    char cc[3];
    uint8_t a[16] = { 0 };
    if (mmdb_country(&m, a, 5, cc) != -1 || mmdb_country(NULL, a, 4, cc) != -1) { printf("✗ parámetros malos aceptados\n"); fails++; }
    free(copy);
    if (argc > 2) {                                   /* precisión contra el archivo completo */
        Mmdb full;
        if (mmdb_open(&full, argv[2])) { printf("✗ no se pudo abrir %s\n", argv[2]); return 1; }
        long same[2] = { 0 }, total[2] = { 0 };
        for (int k = 0; k < 200000; k++) {
            uint8_t a[16];
            int fam = k & 1 ? 6 : 4;
            for (int j = 0; j < 16; j++) a[j] = (uint8_t)rand();
            if (fam == 6) { a[0] = 0x20 | (a[0] & 0x0f); a[1] &= 0x0f; }  /* 2000::/12: lo asignado */
            char x[3], y[3];
            int rf = mmdb_country(&full, a, fam, y);
            if (rf != 1) continue;
            total[fam == 6]++;
            if (mmdb_country(&m, a, fam, x) == 1 && !strcmp(x, y)) same[fam == 6]++;
        }
        printf("coincidencias con %s: IPv4 %.2f %% de %ld, IPv6 %.2f %% de %ld\n", argv[2],
               100.0 * same[0] / (total[0] ? total[0] : 1), total[0], 100.0 * same[1] / (total[1] ? total[1] : 1), total[1]);
        mmdb_close(&full);
    }
    mmdb_close(&m);
    printf(fails ? "mmdb: %d errores\n" : "mmdb: todo bien\n", fails);
    return fails ? 1 : 0;
}

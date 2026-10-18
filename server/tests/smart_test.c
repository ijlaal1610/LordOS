/* smart_test.c — los analizadores de SMART (src/disk/smart.c) con datos armados a mano:
 * ATA sano, ATA con sectores reasignados, ATA con un atributo pre-falla bajo el umbral, suma de
 * comprobación rota, RETURN STATUS «umbral superado», y el registro de salud de NVMe. */
#include "disk/disk.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FALLA %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* atributo i: id, banderas, valor, peor, crudo */
static void put_attr(uint8_t *d, int i, uint8_t id, uint16_t flags, uint8_t val, uint8_t worst, uint64_t raw) {
    uint8_t *a = d + 2 + i * 12;
    a[0] = id;
    a[1] = (uint8_t)flags;
    a[2] = (uint8_t)(flags >> 8);
    a[3] = val;
    a[4] = worst;
    for (int k = 0; k < 6; k++) a[5 + k] = (uint8_t)(raw >> (8 * k));
}

static void put_thresh(uint8_t *t, int i, uint8_t id, uint8_t th) {
    t[2 + i * 12] = id;
    t[3 + i * 12] = th;
}

static void checksum(uint8_t *d) {
    uint8_t s = 0;
    for (int i = 0; i < 511; i++) s = (uint8_t)(s + d[i]);
    d[511] = (uint8_t)(0x100 - s);
}

static void base(uint8_t *d, uint8_t *t) {
    memset(d, 0, 512);
    memset(t, 0, 512);
    d[0] = 0x10;                                    /* versión */
    put_attr(d, 0, 1, 0x000b, 100, 100, 0);         /* Raw_Read_Error_Rate, pre-falla */
    put_attr(d, 1, 5, 0x0033, 100, 100, 0);         /* Reallocated_Sector_Ct */
    put_attr(d, 2, 9, 0x0032, 99, 99, 12345);       /* Power_On_Hours */
    put_attr(d, 3, 12, 0x0032, 100, 100, 678);      /* Power_Cycle_Count */
    put_attr(d, 4, 194, 0x0022, 64, 50, 0x0000002a00140024ull);   /* 36 °C (+ mín/máx en los bytes de arriba) */
    put_attr(d, 5, 197, 0x0012, 100, 100, 0);
    put_attr(d, 6, 198, 0x0010, 100, 100, 0);
    put_thresh(t, 0, 1, 6);
    put_thresh(t, 1, 5, 36);
    put_thresh(t, 2, 9, 0);
    put_thresh(t, 3, 12, 0);
    put_thresh(t, 4, 194, 0);
    checksum(d);
    checksum(t);
}

int main(void) {
    uint8_t d[512], t[512];
    SmartReport r;
    char j[1024];

    /* sano */
    base(d, t);
    CHECK(smart_ata_parse(d, t, 1, &r) == 0);
    CHECK(r.kind == SMART_ATA && r.nattr == 7 && r.checksum_ok);
    CHECK(r.health == SMART_OK);
    CHECK(r.temp_c == 36);
    CHECK(r.power_on_hours == 12345 && r.power_cycles == 678);
    CHECK(r.reallocated == 0 && r.pending == 0 && r.uncorrectable == 0);
    CHECK(r.attr[1].id == 5 && r.attr[1].thresh == 36 && !r.attr[1].failing && (r.attr[1].flags & 1));
    CHECK(smart_summary_json(&r, j, sizeof j) == 0 && strstr(j, "\"health\":\"ok\"") && strstr(j, "\"temp\":36"));
    CHECK(smart_attr_json(&r.attr[2], j, sizeof j) == 0 && strstr(j, "\"raw\":12345") && strstr(j, "\"prefail\":false"));

    /* sin umbrales ni estado: igual se lee */
    CHECK(smart_ata_parse(d, NULL, -1, &r) == 0 && r.health == SMART_OK && r.attr[1].thresh == 0);

    /* sectores reasignados y pendientes: advertencia */
    base(d, t);
    put_attr(d, 1, 5, 0x0033, 98, 98, 24);
    put_attr(d, 5, 197, 0x0012, 100, 100, 3);
    checksum(d);
    CHECK(smart_ata_parse(d, t, 1, &r) == 0 && r.health == SMART_WARN && r.reallocated == 24 && r.pending == 3);

    /* atributo pre-falla bajo el umbral: falla, aunque RETURN STATUS diga bien */
    base(d, t);
    put_attr(d, 1, 5, 0x0033, 30, 30, 4000);
    checksum(d);
    CHECK(smart_ata_parse(d, t, 1, &r) == 0 && r.health == SMART_FAIL && r.attr[1].failing);

    /* un atributo que no es pre-falla bajo el umbral no hace fallar (es «de edad») */
    base(d, t);
    put_attr(d, 3, 12, 0x0032, 1, 1, 9);
    put_thresh(t, 3, 12, 20);
    checksum(d);
    checksum(t);
    CHECK(smart_ata_parse(d, t, 1, &r) == 0 && r.attr[3].failing && r.health == SMART_OK);

    /* RETURN STATUS: umbral superado */
    base(d, t);
    CHECK(smart_ata_parse(d, t, 0, &r) == 0 && r.health == SMART_FAIL);

    /* suma rota: advertencia */
    base(d, t);
    d[100] ^= 0x55;
    CHECK(smart_ata_parse(d, t, -1, &r) == 0 && !r.checksum_ok && r.health == SMART_WARN);

    /* temperatura por 190 si no hay 194 */
    base(d, t);
    put_attr(d, 4, 190, 0x0022, 60, 50, 40);
    checksum(d);
    CHECK(smart_ata_parse(d, t, 1, &r) == 0 && r.temp_c == 40);

    /* nada: ni datos ni estado */
    memset(d, 0, sizeof d);
    CHECK(smart_ata_parse(d, NULL, -1, &r) == -1 && r.health == SMART_UNKNOWN);

    /* NVMe sano */
    uint8_t n[512] = { 0 };
    n[1] = (uint8_t)(310 & 0xff); n[2] = (uint8_t)(310 >> 8);      /* 310 K = 37 °C */
    n[3] = 100; n[4] = 10; n[5] = 3;
    n[32] = 0x10; n[33] = 0x27;                                     /* 10000 unidades leídas */
    n[48] = 0x88; n[49] = 0x13;                                     /* 5000 escritas */
    n[112] = 42;                                                    /* ciclos */
    n[128] = 0x39; n[129] = 0x05;                                   /* 1337 horas */
    n[144] = 7;                                                     /* apagados inseguros */
    CHECK(smart_nvme_parse(n, &r) == 0);
    CHECK(r.kind == SMART_NVME && r.health == SMART_OK && r.temp_c == 37 && r.spare == 100 && r.used_pct == 3);
    CHECK(r.data_read == 10000ull * 512000 && r.data_written == 5000ull * 512000);
    CHECK(r.power_cycles == 42 && r.power_on_hours == 1337 && r.unsafe_shutdowns == 7 && r.media_errors == 0);
    CHECK(smart_summary_json(&r, j, sizeof j) == 0 && strstr(j, "\"kind\":\"nvme\"") && strstr(j, "\"hours\":1337"));
    /* repuesto bajo el umbral: advertencia; confiabilidad degradada (bit 2): falla */
    n[3] = 5;
    CHECK(smart_nvme_parse(n, &r) == 0 && r.health == SMART_WARN);
    n[3] = 100; n[0] = 0x04;
    CHECK(smart_nvme_parse(n, &r) == 0 && r.health == SMART_FAIL);
    /* solo la temperatura (bit 1): advertencia */
    n[0] = 0x02;
    CHECK(smart_nvme_parse(n, &r) == 0 && r.health == SMART_WARN);
    /* errores de medio */
    n[0] = 0; n[160] = 1;
    CHECK(smart_nvme_parse(n, &r) == 0 && r.health == SMART_WARN && r.media_errors == 1);
    /* contador de 128 bits enorme: se satura, no se da vuelta */
    n[160] = 0; n[32 + 9] = 1;
    CHECK(smart_nvme_parse(n, &r) == 0 && r.data_read == UINT64_MAX);

    if (fails) { fprintf(stderr, "smart-test: %d fallas\n", fails); return 1; }
    printf("smart-test: bien\n");
    return 0;
}

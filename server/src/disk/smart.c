/* smart.c — analizadores de SMART (funciones puras, sin E/S; prueba: tests/smart_test.c).
 *
 * ATA (ACS-3, «SMART READ DATA» 0xB0/0xD0 y «READ THRESHOLDS» 0xB0/0xD1, 512 bytes cada uno):
 *   bytes 0-1 versión, 2-361: 30 atributos de 12 bytes
 *     [0] id, [1-2] banderas (bit 0 = pre-falla), [3] valor normalizado, [4] peor, [5-10] crudo
 *     (48 bits, little endian), [11] reservado;
 *   byte 511: suma de comprobación (los 512 bytes suman 0 módulo 256).
 *   Umbrales: misma posición, [0] id, [1] umbral.
 * NVMe (1.4, registro 02h «SMART / Health Information», 512 bytes):
 *   [0] advertencia crítica, [1-2] temperatura (kelvin), [3] repuesto disponible %, [4] umbral del
 *   repuesto, [5] % de vida usada, [32-47] unidades leídas, [48-63] escritas (unidades de 512 000
 *   bytes), [112-127] ciclos de encendido, [128-143] horas encendido, [144-159] apagados
 *   inseguros, [160-175] errores de medio (enteros de 128 bits; acá se usan los 64 de abajo). */
#include "disk.h"

#include <stdio.h>
#include <string.h>

static uint64_t le(const uint8_t *p, int n) {
    uint64_t v = 0;
    for (int i = n - 1; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* 128 bits → 64 (si lo de arriba no es cero, se satura) */
static uint64_t le128(const uint8_t *p) {
    if (le(p + 8, 8)) return UINT64_MAX;
    return le(p, 8);
}

static void clear(SmartReport *r, int kind) {
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->status = -1;
    r->temp_c = -1;
    r->power_on_hours = r->power_cycles = -1;
    r->reallocated = r->pending = r->uncorrectable = -1;
    r->spare = r->spare_thresh = r->used_pct = -1;
}

int smart_ata_parse(const uint8_t data[512], const uint8_t *thresh, int status, SmartReport *r) {
    clear(r, SMART_ATA);
    r->status = status;
    uint8_t sum = 0;
    for (int i = 0; i < 512; i++) sum = (uint8_t)(sum + data[i]);
    r->checksum_ok = sum == 0;
    int failing = 0;
    for (int i = 0; i < 30; i++) {
        const uint8_t *a = data + 2 + i * 12;
        if (!a[0]) continue;
        SmartAttr *x = &r->attr[r->nattr++];
        x->id = a[0];
        x->flags = (uint16_t)le(a + 1, 2);
        x->value = a[3];
        x->worst = a[4];
        x->raw = le(a + 5, 6);
        if (thresh) {
            for (int k = 0; k < 30; k++) {
                const uint8_t *t = thresh + 2 + k * 12;
                if (t[0] == x->id) { x->thresh = t[1]; break; }
            }
        }
        /* valor 0 o 0xFE/0xFF = sin dato (algunos discos); el umbral 0 nunca falla */
        x->failing = x->thresh && x->value && x->value <= x->thresh && x->value < 0xfe;
        if (x->failing && (x->flags & 1)) failing = 1;
        switch (x->id) {
        case 5: r->reallocated = (int64_t)(x->raw & 0xffffffffu); break;
        case 9: r->power_on_hours = (int64_t)(x->raw & 0xffffffffu); break;
        case 12: r->power_cycles = (int64_t)(x->raw & 0xffffffffu); break;
        case 194: r->temp_c = (int)(x->raw & 0xff); break;
        case 190: if (r->temp_c < 0) r->temp_c = (int)(x->raw & 0xff); break;
        case 197: r->pending = (int64_t)(x->raw & 0xffffffffu); break;
        case 198: r->uncorrectable = (int64_t)(x->raw & 0xffffffffu); break;
        default: break;
        }
    }
    if (status == 0 || failing) r->health = SMART_FAIL;
    else if (r->reallocated > 0 || r->pending > 0 || r->uncorrectable > 0) r->health = SMART_WARN;
    else if (status == 1 || r->nattr) r->health = r->checksum_ok ? SMART_OK : SMART_WARN;
    else r->health = SMART_UNKNOWN;
    return r->nattr || status >= 0 ? 0 : -1;
}

int smart_nvme_parse(const uint8_t log[512], SmartReport *r) {
    clear(r, SMART_NVME);
    r->checksum_ok = 1;
    r->crit_warning = log[0];
    unsigned k = (unsigned)le(log + 1, 2);
    r->temp_c = k ? (int)k - 273 : -1;
    r->spare = log[3];
    r->spare_thresh = log[4];
    r->used_pct = log[5];
    uint64_t ur = le128(log + 32), uw = le128(log + 48);
    r->data_read = ur > UINT64_MAX / 512000 ? UINT64_MAX : ur * 512000;
    r->data_written = uw > UINT64_MAX / 512000 ? UINT64_MAX : uw * 512000;
    uint64_t cyc = le128(log + 112), hrs = le128(log + 128);
    r->power_cycles = cyc > INT64_MAX ? INT64_MAX : (int64_t)cyc;
    r->power_on_hours = hrs > INT64_MAX ? INT64_MAX : (int64_t)hrs;
    r->unsafe_shutdowns = le128(log + 144);
    r->media_errors = le128(log + 160);
    /* bits de la advertencia: 0 repuesto bajo, 1 temperatura, 2 confiabilidad degradada,
     * 3 solo lectura, 4 copia volátil fallida */
    if (r->crit_warning & 0x0d) r->health = SMART_FAIL;
    else if (r->crit_warning || r->used_pct >= 100 || r->spare < r->spare_thresh || r->media_errors) r->health = SMART_WARN;
    else r->health = SMART_OK;
    return 0;
}

int smart_summary_json(const SmartReport *r, char *out, size_t n) {
    static const char *const h[] = { "unknown", "ok", "warning", "failing" };
    int k = snprintf(out, n, "{\"kind\":\"%s\",\"health\":\"%s\",\"checksum\":%s,\"status\":%d,\"temp\":%d,\"hours\":%lld,\"cycles\":%lld",
                     r->kind == SMART_NVME ? "nvme" : "ata", h[r->health & 3], r->checksum_ok ? "true" : "false", r->status, r->temp_c,
                     (long long)r->power_on_hours, (long long)r->power_cycles);
    if (k < 0 || (size_t)k >= n) return -1;
    size_t o = (size_t)k;
    if (r->kind == SMART_ATA)
        k = snprintf(out + o, n - o, ",\"reallocated\":%lld,\"pending\":%lld,\"uncorrectable\":%lld,\"attributes\":%d}",
                     (long long)r->reallocated, (long long)r->pending, (long long)r->uncorrectable, r->nattr);
    else
        k = snprintf(out + o, n - o, ",\"critical\":%d,\"spare\":%d,\"spareThreshold\":%d,\"used\":%d,\"read\":%llu,\"written\":%llu,"
                     "\"mediaErrors\":%llu,\"unsafeShutdowns\":%llu}", r->crit_warning, r->spare, r->spare_thresh, r->used_pct,
                     (unsigned long long)r->data_read, (unsigned long long)r->data_written, (unsigned long long)r->media_errors,
                     (unsigned long long)r->unsafe_shutdowns);
    return k < 0 || (size_t)k >= n - o ? -1 : 0;
}

int smart_attr_json(const SmartAttr *a, char *out, size_t n) {
    int k = snprintf(out, n, "{\"id\":%u,\"value\":%u,\"worst\":%u,\"thresh\":%u,\"raw\":%llu,\"prefail\":%s,\"failing\":%s}",
                     a->id, a->value, a->worst, a->thresh, (unsigned long long)a->raw, (a->flags & 1) ? "true" : "false",
                     a->failing ? "true" : "false");
    return k < 0 || (size_t)k >= n ? -1 : 0;
}

/* tzif.h — lector propio de zonas horarias (TZif de tzdata, RFC 8536, y reglas POSIX TZ).
 * Lo usa lord.time.zoneOffset (app Reloj, docs/SISTEMA.md) para saber la hora de otra ciudad sin
 * tocar la zona del proceso (setenv TZ cambiaría el Date de todo el tema). */
#ifndef LORD_TZIF_H
#define LORD_TZIF_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t utoff;          /* segundos al este de UTC (Buenos Aires: -10800) */
    int isdst;
    char abbr[16];          /* "CEST", "-03"… */
} TzInfo;

/* Hora local en el instante t (segundos Unix) según un archivo TZif ya leído. 0 si sirve. */
int tzif_lookup(const uint8_t *buf, size_t n, int64_t t, TzInfo *out);
/* Igual, con una regla POSIX ("CET-1CEST,M3.5.0,M10.5.0/3"): la que trae el pie de los TZif. */
int tz_posix(const char *rule, int64_t t, TzInfo *out);
/* Nombre de zona válido: "Europe/Madrid", "UTC" (sin "..", sin "/" inicial, [A-Za-z0-9/_+-]). */
int tz_zone_valid(const char *zone);
/* Lee /usr/share/zoneinfo/<zone> (archivo regular de hasta 1 MB) y busca t. 0 si sirve. */
int tz_zone_offset(const char *zone, int64_t t, TzInfo *out);

#endif

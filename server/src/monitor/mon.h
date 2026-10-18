/* mon.h — métricas del sistema para la app Monitor (ver mon.c y docs/MONITOR.md).
 *
 * Un hilo propio lee /proc y /sys cuatro veces por segundo, solo mientras la app está abierta
 * y la consulta: si nadie pide una muestra durante 3 s, el hilo se duerme (cero costo). El hilo
 * de la pantalla nunca lee archivos: solo copia el último JSON ya armado. */
#ifndef LORD_MON_H
#define LORD_MON_H

#include <stdint.h>

/* Carpeta de assets (para assets/monitor/geoip-country.mmdb.xz). */
void mon_init(const char *assets_dir);
/* Un contexto JS nuevo (cambio de tema): se olvidan los start() del anterior. */
void mon_reset(void);
void mon_start(void);
void mon_stop(void);
/* Qué listas arma el hilo además de la muestra: procesos y conexiones (1 por segundo cada una);
 * geo: buscar el país de cada conexión en la base local. */
void mon_want(int procs, int conns, int geo);

/* Copias (malloc) del último JSON, o NULL si no hay uno más nuevo que *seq (que se actualiza).
 * Pedir una muestra mantiene vivo al hilo. */
char *mon_sample_json(uint64_t *seq);
char *mon_procs_json(uint64_t *seq);
char *mon_conns_json(uint64_t *seq);
/* Datos fijos: modelo de CPU, núcleos, ubicación por la zona horaria, base de países. */
char *mon_info_json(void);

#endif

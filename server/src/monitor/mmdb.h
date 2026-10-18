/* mmdb.h — lector mínimo de archivos MaxMind DB (.mmdb) para IP → país (ver mmdb.c).
 *
 * Solo lee: el archivo entero va a memoria y cada acceso se verifica contra su largo, así que
 * un archivo roto o manipulado da "no encontrado", nunca una lectura fuera del búfer. */
#ifndef LORD_MMDB_H
#define LORD_MMDB_H

#include <stddef.h>
#include <stdint.h>

#define MMDB_MAX_FILE (64u << 20)

typedef struct {
    uint8_t *buf;                       /* el archivo (propio si owned) */
    size_t len;
    int owned;
    uint32_t node_count;
    uint32_t record_size;               /* 24, 28 o 32 */
    uint32_t ip_version;                /* 4 o 6 */
    uint64_t build_epoch;
    size_t tree_size;                   /* bytes del árbol */
    size_t data_off, data_len;          /* sección de datos */
    uint32_t ipv4_start;                /* nodo de ::/96 en un árbol IPv6 */
    char type[64];                      /* database_type */
} Mmdb;

/* Abre un archivo .mmdb (máx. MMDB_MAX_FILE). 0 si es válido. */
int mmdb_open(Mmdb *m, const char *path);
/* Lo mismo desde memoria (no la copia ni la libera). */
int mmdb_open_mem(Mmdb *m, const uint8_t *buf, size_t len);
void mmdb_close(Mmdb *m);

/* País de una dirección (family 4: 4 bytes, 6: 16 bytes) en cc[3] ("AR").
 * 1 = encontrado, 0 = sin datos, -1 = archivo inválido o parámetros malos. */
int mmdb_country(const Mmdb *m, const uint8_t *addr, int family, char cc[3]);

#endif

/* http.h — descargas HTTP/1.1 propias para los repositorios.
 *
 * fetch_batch() corre en el ayudante root: crea un proceso hijo que suelta los privilegios
 * (pasa a nobody, sin grupos, sin poder volver a subir) antes de tocar la red, y que le manda
 * los bytes por un pipe. El proceso root solo escribe esos bytes en un archivo temporal de su
 * carpeta, cuenta el tamaño y calcula el SHA-256: no interpreta nada de lo que llega. */
#ifndef LORD_PKG_HTTP_H
#define LORD_PKG_HTTP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char url[1024];                     /* http://host[:puerto]/camino */
    char dest[512];                     /* archivo final (se escribe en dest.part y se renombra) */
    int64_t ims;                        /* If-Modified-Since (0: no) */
    uint64_t max;                       /* tamaño máximo aceptado */
    uint64_t expect;                    /* tamaño esperado (0: no se sabe), para el progreso */
    /* resultado */
    int status;                         /* 200, 304, 404… o 0 si falló la conexión */
    int64_t last_modified;
    uint64_t size;
    uint8_t sha256[32];
    char err[200];
} FetchItem;

typedef void (*FetchProgress)(uint64_t done, uint64_t total, void *ud);

/* Descarga los n archivos en orden (reusando la conexión al mismo servidor). Devuelve la
 * cantidad que terminó con 200 o 304; cada item dice lo suyo. */
int fetch_batch(FetchItem *items, int n, FetchProgress cb, void *ud);

/* Fechas de HTTP (RFC 1123) */
int64_t http_parse_date(const char *s);
void    http_format_date(int64_t t, char *out, size_t n);

#endif

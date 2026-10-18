/* monpriv.h — piezas internas del monitor compartidas por mon.c y conn.c. */
#ifndef LORD_MONPRIV_H
#define LORD_MONPRIV_H

#include "mmdb.h"

#include <stddef.h>
#include <stdint.h>

/* JSON que crece (los errores de memoria dejan el búfer en NULL: el resultado se descarta). */
typedef struct { char *p; size_t n, cap; int bad; } Buf;

void mon_buf_printf(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void mon_buf_str(Buf *b, const char *s);          /* "texto" con escapes; UTF-8 inválido → '?' */
void mon_buf_free(Buf *b);

/* Lee un archivo chico entero (máx. n-1 bytes, con NUL). Devuelve los bytes leídos o -1. */
long mon_read_small(const char *path, char *out, size_t n);
double mon_now_ms(void);                      /* reloj monotónico */
double mon_wall_ms(void);                     /* hora real (para el JSON) */

/* Conexiones (conn.c): arma {"t", "others", "list": [...]} con las del usuario.
 * geo puede ser NULL (sin base de países). */
void conn_scan(Buf *b, const Mmdb *geo);
void conn_reset(void);

#endif

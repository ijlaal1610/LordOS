/* csvp.h — lector de CSV (RFC 4180) para los importadores: campos entre comillas con "" adentro,
 * saltos de línea dentro de un campo, CRLF o LF, BOM de UTF-8 al principio. Los campos se copian
 * a la arena de secretos (smem.h): un CSV de contraseñas nunca pasa por el montón de la libc. */
#ifndef LORD_VAULT_CSVP_H
#define LORD_VAULT_CSVP_H

#include <stddef.h>

typedef struct { char **f; int n, cap; } CsvRow;
typedef struct { const char *s; size_t len, p; int bad; } Csv;

void csv_init(Csv *c, const char *s, size_t len);
/* Próxima fila en row (se reusa: libera la anterior). 1 = hay fila, 0 = fin, -1 = comillas sin
 * cerrar o sin memoria. Las filas vacías se saltean. */
int  csv_next(Csv *c, CsvRow *row);
void csv_row_free(CsvRow *row);
/* Columna del encabezado con ese nombre (sin distinguir mayúsculas, sin espacios alrededor), o -1. */
int  csv_col(const CsvRow *hdr, const char *name);
/* Campo i de la fila, o "" si no existe. */
const char *csv_get(const CsvRow *row, int i);

#endif

/* arcpath.h — reglas para extraer un comprimido sin salir de la carpeta de destino.
 *
 * Son funciones puras (sin tocar el disco) para poder probarlas solas (tests/arcpath_test.c). El
 * ayudante (h_arc.c) además abre cada carpeta con O_NOFOLLOW desde el fd de destino, crea los
 * archivos con O_EXCL y corre bajo Landlock con escritura solo debajo de esa carpeta: tres capas. */
#ifndef LORD_DOC_ARCPATH_H
#define LORD_DOC_ARCPATH_H

#include <stddef.h>

#define ARC_PATH_MAX  4000
#define ARC_DEPTH_MAX 64

/* Nombre de una entrada → ruta relativa limpia ("a/b/c"), sin "." ni barras dobles.
 * Rechaza (-1): vacía, absoluta, con un tramo "..", con caracteres de control, tramos de más de
 * 255 bytes, más de ARC_DEPTH_MAX niveles o más de ARC_PATH_MAX bytes. 0 si sirve. */
int arc_clean_path(const char *name, char *out, size_t n);

/* ¿Un enlace simbólico en `entry` (ya limpia) que apunta a `target` queda adentro de la raíz?
 * Solo destinos relativos que, resueltos sin mirar el disco desde la carpeta de la entrada,
 * no suben por encima de la raíz. 1 sí, 0 no. */
int arc_link_ok(const char *entry, const char *target);

/* ¿`path` (limpia) está en `prefix` o debajo? (para extraer solo una parte). */
int arc_under(const char *path, const char *prefix);

/* Permisos seguros para lo extraído: sin setuid/setgid/sticky ni escritura para otros, y
 * siempre legible y escribible por el dueño. */
unsigned arc_file_mode(unsigned mode);
unsigned arc_dir_mode(unsigned mode);

#endif

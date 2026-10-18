/* deb.h — formatos de Debian: versiones (como dpkg), campos deb822 y relaciones entre paquetes. */
#ifndef LORD_PKG_DEB_H
#define LORD_PKG_DEB_H

#include <stddef.h>

/* Compara versiones con el algoritmo de dpkg (época:versión-revisión, ~ antes que nada). */
int ver_cmp(const char *a, const char *b);
int ver_cmp_n(const char *a, size_t an, const char *b, size_t bn);

enum { REL_NONE, REL_LT, REL_LE, REL_EQ, REL_GE, REL_GT };      /* <<  <=  =  >=  >> */
int ver_satisfies(const char *have, int op, const char *want, size_t wantlen);

/* Nombre de paquete de Debian: [a-z0-9][a-z0-9+.-]+ */
int deb_valid_name(const char *s, size_t n);
int deb_valid_version(const char *s, size_t n);

/* Campos deb822: recorre un párrafo (stanza). */
typedef struct {
    const char *p, *end;
} D822;

typedef struct {
    const char *name, *value;           /* value sin el espacio inicial; puede tener \n + continuación */
    size_t nlen, vlen;
} D822Field;

/* Siguiente párrafo de buf: [*start, *stop). 0 si no hay más. */
int d822_next_stanza(const char **pos, const char *end, const char **start, const char **stop);
int d822_next_field(D822 *it, D822Field *f);
int d822_is(const D822Field *f, const char *name);   /* sin distinguir mayúsculas */

/* Una relación: "nombre[:arq] (op versión)". Recorre "a (>= 1) | b, c" grupo por grupo. */
typedef struct {
    const char *name, *ver;
    size_t nlen, vlen;
    int op;
} DebRel;

/* Lee el siguiente grupo de alternativas (hasta la coma). Devuelve la cantidad (0 = fin). */
int deb_next_group(const char **p, const char *end, DebRel *alts, int max);

#endif

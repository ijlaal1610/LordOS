/* xml.h — lector de XML propio para WebDAV (CalDAV y CardDAV) en lord-maild (ver docs/MAIL.md).
 *
 * Entiende espacios de nombres (xmlns y xmlns:p), así se buscan los elementos por (espacio, nombre)
 * sin importar el prefijo que eligió el servidor ("d:", "D:", ninguno). Es estricto y acotado:
 *   - nunca expande entidades declaradas: un <!DOCTYPE es un error (sin «billion laughs» ni
 *     entidades externas); solo las cinco predefinidas y las numéricas válidas;
 *   - profundidad máxima MX_MAX_DEPTH, MX_MAX_NODES elementos, entrada de hasta MX_MAX_INPUT;
 *   - todo índice se verifica contra el largo: nunca lee fuera de la entrada.
 * La prueba (tests/mail_test.c) lo recorre con entradas mutadas al azar. */
#ifndef LORD_MAIL_XML_H
#define LORD_MAIL_XML_H

#include <stddef.h>

#define MX_MAX_DEPTH 64
#define MX_MAX_NODES 400000
#define MX_MAX_INPUT (32u << 20)

#define MX_NS_DAV  "DAV:"
#define MX_NS_CAL  "urn:ietf:params:xml:ns:caldav"
#define MX_NS_CARD "urn:ietf:params:xml:ns:carddav"
#define MX_NS_CS   "http://calendarserver.org/ns/"
#define MX_NS_APPLE "http://apple.com/ns/ical/"

typedef struct MxNode MxNode;
struct MxNode {
    const char *ns;           /* espacio de nombres resuelto ("" si ninguno) */
    const char *name;         /* nombre local, sin prefijo */
    char *text;               /* texto directo (sin el de los hijos), ya sin entidades */
    size_t tlen;
    MxNode *kid, *last, *next, *parent;
};

/* Árbol del documento (el elemento raíz) o NULL. err (opcional) recibe un texto corto. */
MxNode *mx_parse(const char *s, size_t len, const char **err);
void    mx_free(MxNode *root);
/* primer hijo directo con ese espacio y nombre (ns NULL = cualquiera) */
MxNode *mx_child(const MxNode *n, const char *ns, const char *name);
/* siguiente hermano con ese espacio y nombre */
MxNode *mx_next(const MxNode *n, const char *ns, const char *name);
/* primer descendiente (en profundidad, sin recursión) */
MxNode *mx_find(const MxNode *n, const char *ns, const char *name);
const char *mx_text(const MxNode *n);     /* "" si no hay */
/* texto de un hijo (ns, name), sin espacios a los costados, en out; "" si no está */
void    mx_child_text(const MxNode *n, const char *ns, const char *name, char *out, size_t cap);
/* Escapa texto para meterlo en XML (&, <, >, "). Devuelve bytes escritos (sin el 0). */
size_t  mx_escape(char *out, size_t cap, const char *s);

#endif

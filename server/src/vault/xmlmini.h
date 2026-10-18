/* xmlmini.h — lector de XML mínimo, sin validar, para el XML de KeePass (KDBX) y los archivos
 * llave: arma un árbol en la arena de secretos (smem.h). Elementos, atributos, texto con las
 * entidades predefinidas y numéricas (&#N; &#xN;), CDATA, comentarios e instrucciones de
 * procesamiento. Nunca expande entidades declaradas: un <!DOCTYPE es un error (sin «billion
 * laughs» ni entidades externas). Profundidad máxima 256. */
#ifndef LORD_VAULT_XMLMINI_H
#define LORD_VAULT_XMLMINI_H

#include <stddef.h>

typedef struct { char *name, *value; } XAttr;
typedef struct XNode {
    char *name;
    char *text; size_t tlen;          /* texto directo (sin el de los hijos), sin entidades */
    XAttr *attrs; int nattrs;
    struct XNode **kids; int nkids, cap;
    struct XNode *parent;
} XNode;

/* Raíz del documento (el elemento de más arriba) o NULL; err (si no es NULL) recibe un código. */
XNode *xml_parse(const char *s, size_t len, const char **err);
void   xml_free(XNode *n);
XNode *xml_child(const XNode *n, const char *name);              /* primer hijo con ese nombre */
const char *xml_attr(const XNode *n, const char *name);           /* NULL si no está */
const char *xml_text(const XNode *n);                             /* "" si no hay */
/* Reemplaza el texto (el nuevo queda a cargo del nodo; en smem). */
void   xml_set_text(XNode *n, char *text, size_t len);

#endif

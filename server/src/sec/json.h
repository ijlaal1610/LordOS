/* json.h — lector de JSON chico y estricto (RFC 8259) para los archivos que LordOS verifica en C:
 * contenido.json y firma.json de los paquetes .lpk, el catálogo firmado y los permisos guardados
 * (ver docs/FIRMA.md y docs/PRIVACIDAD.md). Todo vive en un solo bloque de memoria. */
#ifndef LORD_SEC_JSON_H
#define LORD_SEC_JSON_H

#include <stddef.h>

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal JVal;
struct JVal {
    JType type;
    double num;                 /* J_NUM; J_BOOL: 0 o 1 */
    const char *str;            /* J_STR (UTF-8, ya sin escapes, terminado en 0) */
    size_t len;                 /* J_STR: bytes; J_ARR/J_OBJ: cantidad de elementos */
    const char *key;            /* clave, si es miembro de un objeto */
    JVal *kid, *next;           /* primer hijo y hermano siguiente */
};

typedef struct {
    JVal *root;
    void *mem;
} JDoc;

/* 0 bien. Rechaza: texto que no es JSON, más de max_depth niveles (32), claves repetidas, \u0000
 * y basura al final. err (opcional) recibe el motivo y la posición. */
int  json_parse(const char *text, size_t len, JDoc *doc, char *err, size_t errlen);
void json_free(JDoc *doc);

const JVal *json_get(const JVal *obj, const char *key);           /* NULL si no es objeto o falta */
const char *json_str(const JVal *obj, const char *key);           /* NULL si no es texto */
double      json_num(const JVal *obj, const char *key, double def);
int         json_bool(const JVal *obj, const char *key, int def);

/* Texto JSON con comillas y escapes de s (para armar JSON en C). Devuelve bytes escritos. */
size_t json_quote(char *out, size_t n, const char *s);

#endif

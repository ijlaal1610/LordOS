/* vjson.h — JSON adentro de lord-vault sin pasar por el montón de la libc: un lector que marca
 * tokens sobre el texto (sin copiarlo) y un escritor que crece en la arena de secretos (smem.h).
 * Lo usan el protocolo con lord-server/la app, la carga útil de la bóveda y los importadores.
 *
 * Lector: vj_parse arma un arreglo de tokens en orden; cada uno sabe dónde termina su subárbol
 * (next), así recorrer un objeto es: for (k = o + 1; k < t[o].next; k = t[k + 1].next) { clave k,
 * valor k + 1 }. Estricto (RFC 8259), con un tope de profundidad (64). */
#ifndef LORD_VAULT_VJSON_H
#define LORD_VAULT_VJSON_H

#include <stddef.h>
#include <stdint.h>

enum { VJ_OBJ = 1, VJ_ARR, VJ_STR, VJ_NUM, VJ_TRUE, VJ_FALSE, VJ_NULL };

typedef struct { uint8_t type; uint32_t start, end; int next; int count; } VjTok;  /* start/end: bytes (STR sin comillas) */
typedef struct { const char *s; size_t len; VjTok *t; int n, cap; } Vj;

int    vj_parse(Vj *j, const char *s, size_t len);   /* 0 bien; tokens en smem */
void   vj_free(Vj *j);
int    vj_get(const Vj *j, int obj, const char *key); /* token del valor, o -1 */
int    vj_at(const Vj *j, int arr, int i);            /* i-ésimo elemento, o -1 */
int    vj_type(const Vj *j, int tok);                 /* 0 si tok es -1 */
char  *vj_str(const Vj *j, int tok);                  /* texto sin escapes, en smem (NULL si no es STR) */
size_t vj_strbuf(const Vj *j, int tok, char *buf, size_t n);   /* copia acotada; devuelve el largo */
int    vj_streq(const Vj *j, int tok, const char *s);
double vj_num(const Vj *j, int tok, double def);
int    vj_bool(const Vj *j, int tok, int def);
/* atajos sobre un objeto */
char  *vj_gets(const Vj *j, int obj, const char *key);
size_t vj_getsbuf(const Vj *j, int obj, const char *key, char *buf, size_t n);
double vj_getn(const Vj *j, int obj, const char *key, double def);
int    vj_getb(const Vj *j, int obj, const char *key, int def);

/* Escritor (en smem). Las comas las pone quien llama, salvo con los vb_k* («"clave":valor» con
 * coma si hace falta: se lleva la cuenta por nivel con vb_open/vb_close). */
typedef struct { char *p; size_t n, cap; int oom; uint8_t first[64]; int depth; } VjB;
void vb_init(VjB *b);
void vb_free(VjB *b);                      /* borra y libera */
void vb_raw(VjB *b, const char *s);
void vb_rawn(VjB *b, const char *s, size_t n);
void vb_str(VjB *b, const char *s);        /* "texto" con escapes; NULL → null */
void vb_strn(VjB *b, const char *s, size_t n);
void vb_num(VjB *b, double v);
void vb_int(VjB *b, long long v);
void vb_open(VjB *b, char c);              /* '{' o '[' */
void vb_close(VjB *b, char c);             /* '}' o ']' */
void vb_sep(VjB *b);                       /* coma si no es el primero del nivel */
void vb_key(VjB *b, const char *k);        /* vb_sep + "k": */
void vb_ks(VjB *b, const char *k, const char *v);
void vb_kn(VjB *b, const char *k, double v);
void vb_ki(VjB *b, const char *k, long long v);
void vb_kb(VjB *b, const char *k, int v);
/* Base64 (RFC 4648) en smem */
char    *b64_encode(const uint8_t *in, size_t n);
uint8_t *b64_decode(const char *in, size_t n, size_t *outlen);   /* acepta también base64url */

#endif

/* cbor.h — CBOR (RFC 8949) chico y estricto para CTAP2: enteros, bytes, texto, arreglos, mapas,
 * true/false/null. Sin flotantes, etiquetas ni largos indefinidos.
 *
 * Escritor: sobre un búfer fijo; las claves de los mapas las escribe quien llama ya en el orden
 * canónico de CTAP2 (más cortas primero, después por bytes: 1, 2, 3 … -1, -2; "id" < "name").
 * Lector: arma un arreglo de ítems en orden (sin copiar), cada uno con dónde termina su subárbol;
 * rechaza codificaciones no mínimas («overlong»), valores simples reservados, UTF-8 inválido y
 * anidamiento de más de 16 niveles. */
#ifndef LORD_VAULT_CBOR_H
#define LORD_VAULT_CBOR_H

#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t *p; size_t n, cap; int err; } CbW;
void cbw_init(CbW *w, uint8_t *buf, size_t cap);
void cbw_uint(CbW *w, uint64_t v);
void cbw_int(CbW *w, int64_t v);
void cbw_bytes(CbW *w, const void *d, size_t n);
void cbw_text(CbW *w, const char *s);
void cbw_textn(CbW *w, const char *s, size_t n);
void cbw_array(CbW *w, size_t n);
void cbw_map(CbW *w, size_t n);
void cbw_bool(CbW *w, int v);
void cbw_null(CbW *w);
void cbw_raw(CbW *w, const void *d, size_t n);      /* un ítem ya codificado */

enum { CB_UINT = 1, CB_NINT, CB_BYTES, CB_TEXT, CB_ARRAY, CB_MAP, CB_FALSE, CB_TRUE, CB_NULL, CB_UNDEF };

typedef struct {
    uint8_t type;
    uint64_t v;            /* UINT: el valor; NINT: el valor es -1 - v; ARRAY/MAP: cantidad */
    const uint8_t *ptr;    /* BYTES/TEXT: los datos */
    size_t len;
    int next;              /* índice del ítem que sigue a este subárbol */
} CbItem;

typedef struct { const CbItem *it; int n; size_t used; } Cb;

/* Lee un ítem de d. items: arreglo del llamador (max ítems). Con allow_trailing, sobra lo que
 * venga después (c->used dice cuántos bytes ocupó); si no, tiene que terminar justo. 0 bien. */
int  cb_parse(Cb *c, const uint8_t *d, size_t len, CbItem *items, int max, int allow_trailing);
int  cb_type(const Cb *c, int i);                    /* 0 si i < 0 */
int  cb_map_int(const Cb *c, int map, int64_t key);  /* índice del valor, o -1 */
int  cb_map_text(const Cb *c, int map, const char *key);
int  cb_at(const Cb *c, int arr, int k);             /* k-ésimo elemento, o -1 */
int  cb_int(const Cb *c, int i, int64_t *out);       /* UINT/NINT que entre en int64: 0 bien */
int  cb_text_eq(const Cb *c, int i, const char *s);

#endif

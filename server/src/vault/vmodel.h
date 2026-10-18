/* vmodel.h — el contenido de una bóveda abierta, en la arena de secretos: entradas con sus campos,
 * etiquetas, adjuntos e historial, más las categorías, las etiquetas y los ajustes (que la
 * interfaz maneja como JSON y acá se guardan tal cual, validados).
 *
 * Contenido (JSON, versión 1):
 *   {"v":1,"meta":{…},"cats":[…],"tags":[…],"entries":[{
 *      "id":"<32 hex>","cat":"internet","title":"…","notes":"…","fav":true,"arch":false,
 *      "trash":0,"created":…,"modified":…,"pwch":…,"rotate":90,"tags":["…"],
 *      "f":[{"k":"username","t":"text","l":"…","v":"…"},…],
 *      "att":[{"id","name","size","mime","nonce","mac"}],
 *      "hist":[{"t":…,"title":…,"f":[…]}],"x":{…}}]}
 * Tipos de campo: text, hidden, url, date, number, file, totp, email, multiline, pin. */
#ifndef LORD_VAULT_VMODEL_H
#define LORD_VAULT_VMODEL_H

#include <stddef.h>
#include <stdint.h>
#include "vjson.h"

typedef struct { char *k, *t, *l, *v; } VField;
typedef struct { uint8_t id[16]; char *name, *mime; uint64_t size; uint8_t nonce[24], mac[16]; } VAtt;

typedef struct {
    char id[33];
    char *cat, *title, *notes;
    VField *f; int nf;
    char **tags; int ntags;
    VAtt *att; int natt;
    int fav, arch, rotate;
    int64_t trash, created, modified, pwch;
    char *hist;               /* JSON (arreglo) tal cual */
    char *x;                  /* JSON (objeto) extra: datos del llavero, etc. */
    char *fold;               /* texto de búsqueda (minúsculas, sin tildes) */
    uint8_t pwhash[16];       /* BLAKE2b de la contraseña (para las repetidas); todo cero si no hay */
} VEntry;

typedef struct {
    VEntry **e; int n, cap;
    char *meta, *cats, *tags; /* JSON tal cual */
    int dirty;
} VModel;

int   vm_load(VModel *m, const char *json, size_t len);  /* 0 bien */
void  vm_init(VModel *m);
void  vm_free(VModel *m);
char *vm_dump(const VModel *m, size_t *len);              /* contenido JSON en smem */

VEntry *vm_find(const VModel *m, const char *id);
int   vm_index(const VModel *m, const char *id);
/* Crea o actualiza desde el JSON de la interfaz (objeto en j/tok). Los campos ocultos que llegan
 * con "keep":true conservan el valor de antes. Guarda historial si cambió algo. *out = la entrada. */
int   vm_upsert(VModel *m, const Vj *j, int tok, VEntry **out, char *err, size_t errn);
int   vm_remove(VModel *m, const char *id);               /* borrar del todo (vaciar la papelera) */
void  vm_entry_free(VEntry *e);
const char *vm_field(const VEntry *e, const char *key);   /* valor del primer campo con esa clave */
const char *vm_field_type(const VEntry *e, const char *type);   /* valor del primer campo de ese tipo */
void  vm_refresh(VEntry *e);                              /* recalcula fold y pwhash */
void  vm_new_id(char out[33]);

/* JSON de una entrada (para la interfaz). reveal: 0 = los ocultos sin valor ("has":true),
 * 1 = con valor. Con hist incluye el historial (los valores ocultos del historial solo con reveal). */
void  vm_entry_json(VjB *b, const VEntry *e, int reveal, int hist);
/* Lista liviana de todas las entradas (sin secretos): id, cat, title, sub, url, fav, arch, trash,
 * modified, tags, totp (si tiene), natt, kind de la contraseña (edad), expira. */
void  vm_list_json(VjB *b, const VModel *m);
/* Búsqueda: ids que contienen todas las palabras de q (sin tildes ni mayúsculas), en el orden de
 * la lista; scope: "all" (sin papelera), "trash", "archived"… lo filtra la interfaz. */
void  vm_search_json(VjB *b, const VModel *m, const char *q, int max);
/* Pasa a minúsculas y saca tildes de un texto UTF-8 (en smem). */
char *vm_fold(const char *s);

#endif

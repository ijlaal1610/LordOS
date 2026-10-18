/* fwmap.h — tabla hash con encadenamiento, interna del firewall (rules.c, dnsmap.c). Claves de
 * bytes con largo, un valor void* por clave y hash con semilla del proceso (para que nadie pueda
 * armar a propósito claves que caigan todas en el mismo balde). Crece al duplicar. */
#ifndef LORD_FW_FWMAP_H
#define LORD_FW_FWMAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct FwMapEnt {
    struct FwMapEnt *next;
    uint64_t h;
    void *val;
    size_t klen;
    unsigned char key[];
} FwMapEnt;

typedef struct {
    FwMapEnt **b;
    size_t nb, n;
} FwMap;

uint64_t fw_hash(const void *data, size_t len);

/* Valor de la clave, o NULL. */
void  *fwmap_get(const FwMap *m, const void *key, size_t klen);
/* Lugar del valor; con create, crea la clave (valor NULL) si falta. NULL si falta o sin memoria. */
void **fwmap_slot(FwMap *m, const void *key, size_t klen, int create);
/* Saca la clave y devuelve su valor (NULL si no estaba). */
void  *fwmap_take(FwMap *m, const void *key, size_t klen);
/* Libera la tabla; free_val (opcional) recibe cada valor. */
void   fwmap_free(FwMap *m, void (*free_val)(void *));

#define FWMAP_EACH(m, e) \
    for (size_t fwm_i_ = 0; fwm_i_ < (m)->nb; fwm_i_++) \
        for (FwMapEnt *e = (m)->b[fwm_i_], *fwm_n_ = e ? e->next : NULL; e; e = fwm_n_, fwm_n_ = e ? e->next : NULL)

#endif

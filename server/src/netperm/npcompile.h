/* npcompile.h — de la tienda de reglas (fw/rules.c) a las entradas de los mapas BPF de
 * lord-netperm (np_shared.h). Es C puro, sin kernel: lo prueba tests/npcompile_test.c contra
 * fw_decide. Ver docs/FIREWALL.md, "Paso 2", "De reglas a mapas".
 *
 * Por app sale:
 *   - un NpApp: restricciones, confiable, modo y la lista genérica (lan / internet / any);
 *   - nodos para los tries LPM: uno por cada red de una regla y uno por cada dirección que el
 *     resolvedor del sistema asoció con un nombre que coincide con una regla de dominio. La lista
 *     de cada nodo trae también las reglas de las redes que lo contienen, así la coincidencia más
 *     larga del trie alcanza: en el kernel gana la primera entrada que coincide en dirección,
 *     protocolo y puerto, en el mismo orden de precedencia que fw_decide.
 *
 * Las reglas "una sola vez" no se compilan (no hay quien las borre en el kernel; sin el modo
 * preguntar no aparecen). El modo "preguntar" se compila como silencioso. */
#ifndef LORD_NP_COMPILE_H
#define LORD_NP_COMPILE_H

#include <stddef.h>
#include <stdint.h>

#include "fw/rules.h"
#include "fw/dnsmap.h"
#include "netperm/np_shared.h"

typedef struct {
    int fam;                    /* 4 o 6 */
    int prefix;                 /* 0..32 o 0..128 */
    uint8_t addr[16];
    int host;                   /* 1: sale de una respuesta DNS (reglas de dominio) */
    NpList list;
} NpNode;

typedef struct {
    NpApp app;
    NpNode *nodes;
    int nnodes, cap;
    int truncated;              /* reglas que no entraron (NP_LIST_MAX / NP_GEN_MAX); se avisa */
    int has_domains;            /* tiene reglas de dominio: hay que recompilar con cada respuesta DNS */
} NpCompiled;

/* Compila la app. names (puede ser NULL) son los nombres aprendidos del resolvedor del sistema,
 * guardados en dnsmap bajo la app names_app. 0 bien, -1 sin memoria. */
int  np_compile_app(FwStore *s, const char *app, uint32_t idx, DnsMap *names, const char *names_app,
                    long long now, NpCompiled *out);
void np_compiled_free(NpCompiled *c);

/* Solo el nodo de una dirección con sus nombres (una respuesta DNS nueva, sin recompilar todo).
 * 1 si hace falta un nodo (queda en *out), 0 si no, -1 error. */
int  np_compile_host(FwStore *s, const char *app, int fam, const uint8_t *addr,
                     const char (*names)[FW_NAME_MAX], int n, NpNode *out, int *truncated);

/* Claves de los tries para un nodo. */
void np_key4(uint32_t idx, const NpNode *n, NpLpm4Key *k);
void np_key6(uint32_t idx, const NpNode *n, NpLpm6Key *k);

/* Protocolo de una regla → NP_P_*; ¿la red de la regla contiene a la dirección/prefijo? */
int  np_cidr_contains(const FwRule *r, int fam, const uint8_t *addr, int prefix);

#endif

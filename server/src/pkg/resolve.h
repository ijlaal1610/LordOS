/* resolve.h — qué hay que instalar, actualizar o quitar para cumplir un pedido (como el
 * resolvedor de apt, más simple: goloso, con vuelta atrás acotada en las alternativas). */
#ifndef LORD_PKG_RESOLVE_H
#define LORD_PKG_RESOLVE_H

#include "idx.h"
#include "status.h"

enum { PA_INSTALL = 1, PA_UPGRADE, PA_REMOVE };

typedef struct {
    int action;
    const char *name, *version, *old_version;   /* version: la que queda (install/upgrade) */
    long rec;                           /* registro del índice a descargar, o -1 */
    int requested;                      /* lo pidió el usuario (no es dependencia) */
    int orphan;                         /* dependencia que quedó sin uso (reason "auto") */
    char reason[200];                   /* por qué (dependencia de…, conflicto con…) */
} PlanItem;

typedef struct {
    PlanItem *items;
    int n;
    char held[32][64];                  /* actualizaciones que no se pueden hacer ahora */
    int nheld;
    uint64_t download;                  /* bytes a descargar */
    int64_t isize_delta;                /* KB que cambia lo instalado */
    int ok;
    char err[512];
} Plan;

typedef struct {
    int recommends;                     /* instalar también los recomendados */
    int allow_remove;                   /* puede quitar paquetes para resolver conflictos */
    int cascade;                        /* al quitar, quitar también lo que depende */
    int upgrade_all;                    /* actualizar todo lo actualizable */
    int orphans;                        /* quitar también lo instalado como dependencia que ya no hace falta */
    const char *const *autos;           /* los instalados como dependencia (/var/lib/lordos/pkg/auto) */
    int nautos;
} PlanOpts;

/* install/remove: nombres (de paquetes reales o virtuales para install). */
int  plan_make(const Idx *x, const Status *st, const char *const *install, int ni,
               const char *const *remove, int nr, const PlanOpts *o, Plan *out);
void plan_free(Plan *p);

/* Orden para dpkg: índices de items (instalar/actualizar) en orden de dependencias, partidos en
 * tandas: cada tanda se instala con un dpkg --install (un Pre-Depends corta la tanda). group[i]
 * es la tanda del i-ésimo. Devuelve la cantidad, o -1. */
int  plan_order(const Idx *x, const Plan *p, int *order, int *group);

/* Versión candidata de un paquete (registro del índice), o -1. inst: la instalada (puede ser NULL). */
long pkg_candidate(const Idx *x, const char *name, size_t n, const StPkg *inst);

#endif

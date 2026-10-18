/* engine.h — motor de las copias de seguridad (Copias de seguridad, docs/SISTEMA.md).
 *
 * Código propio, sin rsync: una instantánea es un árbol completo de la carpeta personal en
 *   <destino>/LordOS-Copias/<equipo>-<usuario>/<AAAA-MM-DD-HHMMSS>/
 * Los archivos que no cambiaron (tamaño, fecha de modificación y permisos iguales a los de la
 * instantánea anterior) son enlaces duros a la anterior: cada instantánea se ve entera, pero solo
 * ocupa lo que cambió. Mientras se copia, el árbol está en .inprogress-<sello>; al terminar se
 * renombra (atómico). Una copia cortada se retoma: la siguiente usa ese árbol a medias como otra
 * fuente de enlaces y después lo borra.
 *
 * Este archivo no sabe de hilos ni de JS (lo usa backup.c y lo prueba tests/backup_test.c). */
#ifndef LORD_BACKUP_ENGINE_H
#define LORD_BACKUP_ENGINE_H

#include <stdint.h>
#include <time.h>

#define BK_DIRNAME "LordOS-Copias"
#define BK_MAX_EXCL 64
#define BK_MAX_SNAPS 4096
#define BK_ID_LEN 18                    /* "2026-10-17-235959" + 0 */

typedef struct BkRun BkRun;
struct BkRun {
    /* entrada */
    const char *home;                   /* carpeta de origen (ya resuelta con realpath) */
    const char *machine;                /* <destino>/LordOS-Copias/<equipo>-<usuario> (se crea) */
    const char *excl[BK_MAX_EXCL];      /* prefijos relativos a home ("Videos/grandes", ".cache") */
    int nexcl;
    const char *skip_real;              /* ruta real que nunca se copia (el destino, si está en home) */
    volatile int *cancel;               /* != 0: cortar lo antes posible (queda para retomar) */
    /* Se llama entre archivos y cada ~8 MB: puede bloquear (pausa) y devuelve != 0 para cortar. */
    int  (*checkpoint)(BkRun *r);
    void (*progress)(BkRun *r, const char *rel);   /* al pasar de archivo (rel a home) */
    void *ud;
    uint64_t min_free;                  /* bytes libres a dejar en el destino (0: 512 MiB) */
    const uint8_t *key;                 /* clave de 32 bytes si el destino está cifrado (crypt.h), o NULL */
    /* salida */
    uint64_t total_files, total_bytes;  /* del recuento previo */
    uint64_t files, bytes;              /* procesados */
    uint64_t copied, linked, copied_bytes, errors, skipped;
    char id[BK_ID_LEN];                 /* instantánea creada */
    char error[256];
};

/* 0: lista; 1: cortada (queda .inprogress para retomar); -1: error (r->error). */
int  bk_snapshot(BkRun *r, time_t now);

/* Instantáneas completas del equipo, de la más vieja a la más nueva. Devuelve la cantidad. */
int  bk_list(const char *machine, char (*ids)[BK_ID_LEN], int max);
int  bk_id_valid(const char *id);
time_t bk_id_time(const char *id);      /* hora local del nombre, o -1 */
void bk_id_make(time_t t, char out[BK_ID_LEN]);

/* Retención (función pura): marca en keep[i] qué instantáneas quedan. t[] de la más vieja a la más
 * nueva. Todas las de las últimas 24 h; la última de cada día hasta 30 días; la última de cada
 * semana después. La más nueva siempre queda. tz_off: segundos a sumar para la hora local. */
void bk_retention(const time_t *t, int n, time_t now, long tz_off, int *keep);
/* Aplica la retención y, si falta lugar (need bytes), borra las más viejas (nunca la última). */
int  bk_prune(const char *machine, time_t now, uint64_t need);
int  bk_delete(const char *machine, const char *id);

/* Restaurar rel ("" = todo) de una instantánea a base/rel (base: normalmente home).
 * replace = 0: si el destino existe y es distinto, se guarda al lado como "nombre<suffix>"
 * (suffix lo da la interfaz, sin '/'); replace = 1: se reemplaza. Nunca escribe donde lord.fs no
 * deja (fsops_protected) ni fuera de sus raíces (fsops_allowed). */
typedef struct {
    uint64_t files, bytes, kept_both, replaced, skipped, errors;
    volatile int *cancel;
    const uint8_t *key;                 /* para descifrar (destino cifrado), o NULL */
    char error[256];
} BkRestore;
int  bk_restore(const char *machine, const char *id, const char *rel, const char *base,
                int replace, const char *suffix, BkRestore *out);

/* rel seguro: relativo, sin "..", ".", "//", ni caracteres de control. "" vale. */
int  bk_rel_valid(const char *rel);

#endif

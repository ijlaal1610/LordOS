/* fsops.h — operaciones de archivos para lord.fs (Archivos): copiar/mover en segundo plano,
 * Papelera (freedesktop), información, búsquedas acotadas y volúmenes montados (ver fsops.c). */
#ifndef LORD_FSOPS_H
#define LORD_FSOPS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Raíces permitidas: la carpeta personal y los montajes del usuario (/media/$USER, /run/media/$USER). */
void fsops_init(const char *home, const char *user);
int  fsops_allowed(const char *real);          /* ruta ya resuelta (realpath) dentro de una raíz */
int  fsops_protected(const char *real);        /* carpetas donde JS no escribe (lanzador, apps, ajustes) */

/* ---- copiar / mover en un hilo ---- */
#define FSJOB_MAX 8
typedef struct {
    int id, used, move, done, failed, cancel;
    char src[4096], dst[4096];
    volatile uint64_t bytes, total, files;
    char error[256];
} FsJob;
int  fsjob_start(const char *src, const char *dst, int move, char *err, size_t errlen);   /* id > 0 */
const FsJob *fsjob_get(int i);                 /* i = 0..FSJOB_MAX-1, para listar */
void fsjob_cancel(int id);
void fsjob_forget(int id);                      /* ya leído el resultado: libera el lugar */

/* ---- Papelera ---- */
int  fstrash_put(const char *real, char *err, size_t errlen);          /* mueve a la papelera que corresponde */
/* Lista: llama a fn por cada elemento (id = ruta real dentro de .../files). */
typedef void (*FsTrashFn)(void *ud, const char *id, const char *name, const char *orig, const char *date, int dir, double size);
void fstrash_list(FsTrashFn fn, void *ud);
int  fstrash_restore(const char *id, char *err, size_t errlen);
int  fstrash_remove(const char *id, char *err, size_t errlen);          /* borrar definitivamente */
int  fstrash_empty(void);

/* ---- recorridos acotados ---- */
typedef struct { double size; long files, dirs; int complete; } FsDu;
void fs_du(const char *real, double max_ms, FsDu *out);
typedef void (*FsHitFn)(void *ud, const char *path, int dir, double size, double mtime);
int  fs_search(const char *real, const char *needle, int limit, int depth, FsHitFn fn, void *ud);
int  fs_recent(int limit, int days, FsHitFn fn, void *ud);
typedef void (*FsVolFn)(void *ud, const char *path, const char *fstype, double total, double avail);
void fs_volumes(FsVolFn fn, void *ud);

#endif

/* backup.h — Copias de seguridad de la sesión (ver backup.c, engine.h y docs/SISTEMA.md).
 *
 * Un hilo de lord-server, como el usuario y con la prioridad más baja (nice 19, E/S «idle»),
 * hace las instantáneas programadas y las restauraciones. Avisa a JS por un eventfd que el bucle
 * principal mira (backup_fd → backup_dispatch → __lordBackup(estado)). */
#ifndef LORD_BACKUP_H
#define LORD_BACKUP_H

#include <stddef.h>

int   backup_start(const char *home, const char *user);   /* después de soltar root; una vez */
int   backup_fd(void);                                    /* -1 si no corre */
void  backup_dispatch(void (*fn)(const char *json));      /* hay novedades: fn(estado JSON) */

/* Todo devuelve texto JSON en memoria nueva (free) o NULL; err recibe el motivo. */
char *backup_status_json(void);
char *backup_config_json(void);
int   backup_configure(const char *json, char *err, size_t n);
int   backup_run_now(char *err, size_t n);
void  backup_pause(int on);
void  backup_cancel(void);
char *backup_list_json(void);
char *backup_ls_json(const char *id, const char *rel, char *err, size_t n);
int   backup_restore(const char *id, const char *rel, int replace, const char *suffix, const char *to, char *err, size_t n);
int   backup_delete(const char *id, char *err, size_t n);
/* Cifrado: desbloquear (o, con create, crear en un destino sin copias) con la frase; el resultado
 * llega en el estado (unlock). La clave queda solo en memoria. lock() la borra. */
int   backup_unlock(const char *pass, int create, char *err, size_t n);
void  backup_lock(void);
/* Ruta real de una instantánea (para abrirla en Archivos), o -1. */
int   backup_snapshot_path(const char *id, char *out, size_t n);

#endif

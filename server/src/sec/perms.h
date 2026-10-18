/* perms.h — permisos por app al estilo TCC de macOS (ver perms.c y docs/PRIVACIDAD.md).
 *
 * Cada permiso de cada app tiene un estado: preguntar (la primera vez), permitido o denegado.
 * Las decisiones se guardan por usuario en ~/.config/lordos/permisos.json, un archivo que las
 * apps NO pueden escribir (está en la lista de rechazo de lord.fs, como settings.json). El
 * broker (y, mientras las apps corren en el mismo contexto, el SDK del sistema) consulta acá:
 * la búsqueda es O(1) sobre una tabla en memoria. */
#ifndef LORD_SEC_PERMS_H
#define LORD_SEC_PERMS_H

#include <stddef.h>

/* Los permisos que una app puede declarar y pedir. El orden es el de perms.c. */
typedef enum {
    PERM_FILES = 0,     /* abrir/guardar por el diálogo del sistema (powerbox) */
    PERM_CLIPBOARD,     /* leer el portapapeles */
    PERM_NOTIFICATIONS,
    PERM_NETWORK,
    PERM_CAMERA,
    PERM_MICROPHONE,
    PERM_SCREEN,        /* captura de pantalla */
    PERM_LISTEN,        /* abrir puertos para escuchar */
    PERM_USB,
    PERM_BLUETOOTH,
    PERM_APPS,          /* lanzar otras apps */
    PERM_COUNT,
} Perm;

typedef enum { PERM_ASK = 0, PERM_ALLOW = 1, PERM_DENY = 2 } PermState;

const char *perm_name(Perm p);              /* "microphone", "camera"... */
int         perm_from_name(const char *s);  /* -1 si no existe */
int         perm_declarable(const char *s); /* ¿se puede declarar en el manifest? */

/* Estado guardado (O(1)); PERM_ASK si no hay decisión. */
PermState perm_get(const char *app_id, Perm p);
/* Guarda una decisión (y persiste en permisos.json). Revocar = PERM_ASK o PERM_DENY. */
int perm_set(const char *app_id, Perm p, PermState state);
/* Olvida todos los permisos de una app (al desinstalar). */
void perm_forget(const char *app_id);

/* Para la página Privacidad: recorre las decisiones guardadas. Devuelve la cantidad escrita. */
typedef struct { char app[48]; Perm perm; PermState state; long long when; } PermEntry;
int perm_list(PermEntry *out, int max);

/* Carga permisos.json (una vez). La ruta se agrega a la lista de rechazo de lord.fs. */
void perm_init(const char *home);
const char *perm_file_suffix(void);         /* "/.config/lordos/permisos.json" para la lista de rechazo */

#endif

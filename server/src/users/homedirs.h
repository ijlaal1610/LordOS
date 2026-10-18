/* homedirs.h — carpetas comunes de la carpeta personal en el idioma del usuario (Escritorio,
 * Documentos, Descargas, Música, Imágenes, Videos) y su ~/.config/user-dirs.dirs (xdg-user-dirs).
 * Una sola lista para el instalador (install.c) y Ajustes → Usuarios (users_admin.c); es la misma
 * que usa lordos-live-prep en el ISO live. Se llama como root. */
#ifndef LORD_HOMEDIRS_H
#define LORD_HOMEDIRS_H

#include <sys/types.h>

/* Idioma de la interfaz para una región ("es_AR.UTF-8" → "es"): es, en o pt (español si no hay). */
const char *homedirs_ui_lang(const char *locale);
/* Nombre visible de cada carpeta (0..5: escritorio, documentos, descargas, música, imágenes, videos). */
const char *homedirs_name(const char *ui_lang, int i);
#define HOMEDIRS_COUNT 6
/* Crea las carpetas que falten en home (dueño uid:gid, 0755) y escribe ~/.config/user-dirs.dirs y
 * ~/.config/user-dirs.locale si no existen. No sigue enlaces simbólicos. 0 si todo salió bien. */
int homedirs_create(const char *home, uid_t uid, gid_t gid, const char *locale);

#endif

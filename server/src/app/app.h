/* app.h — «modo app» de lord-server: correr una app .lpk como su propio proceso aislado y
 * cliente Wayland del compositor (ver appclient.c, docs/PRIVACY.md). */
#ifndef LORD_APP_H
#define LORD_APP_H

#include <stddef.h>

/* `lord-server --app <id>`: corre la app instalada <id>. Devuelve el código de salida. */
int app_main(const char *id, const char *theme_bundle, const char *fonts);

/* Desde lord-server (el compositor): lanza la app <id> como proceso aislado y registra su broker.
 * 0 si lanzó. El id tiene que ser de un paquete instalado (se valida). */
int app_spawn(const char *id);

/* Package folder of app <id>. A system app shipped with LordOS (<assets>/sysapps/<id>, owned by
 * root or the user and not writable by others) wins over a user-installed .lpk
 * (lpk_root()/<id>), so an installed package can never shadow it. out receives the real path.
 * Returns 1 for a system app, 0 for a user package, -1 if there is none. */
int app_package_dir(const char *id, const char *assets, char *out, size_t n);

#endif

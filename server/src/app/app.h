/* app.h — «modo app» de lord-server: correr una app .lpk como su propio proceso aislado y
 * cliente Wayland del compositor (ver appclient.c, docs/PRIVACIDAD.md). */
#ifndef LORD_APP_H
#define LORD_APP_H

/* `lord-server --app <id>`: corre la app instalada <id>. Devuelve el código de salida. */
int app_main(const char *id, const char *theme_bundle, const char *fonts);

/* Desde lord-server (el compositor): lanza la app <id> como proceso aislado y registra su broker.
 * 0 si lanzó. El id tiene que ser de un paquete instalado (se valida). */
int app_spawn(const char *id);

#endif

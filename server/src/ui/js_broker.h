/* js_broker.h — lado app del broker (ver appclient.c, sec/broker.h). Provee el objeto `lord` del
 * proceso de la app: las primitivas de dibujo (locales) más fs/clipboard/perms, que van por el
 * broker a lord-server, que verifica los permisos. */
#ifndef LORD_JS_BROKER_H
#define LORD_JS_BROKER_H

#include "quickjs.h"

int  js_broker_fd(void);              /* fd del broker para poll; -1 si no hay */
void js_broker_poll(void);            /* mensajes del servidor sin pedir (indicadores, tema) */
void js_broker_set_pkgdir(const char *dir);   /* antes de la caja de arena */
int  js_broker_wants_network(void);   /* la app declaró `network` y lo tiene permitido */
void js_broker_register(JSContext *c, JSValue lord, const char *app_id);

#endif

/* js_logs.h — lord.logs (app Registros; ver js_logs.c y docs/SYSTEM.md). */
#ifndef LORD_JS_LOGS_H
#define LORD_JS_LOGS_H

#include "quickjs.h"

void js_logs_register(JSContext *c, JSValue lord);
/* fd de epoll para el bucle principal (resultados del hilo e inotify), o -1 */
int  logs_fd(void);
/* kind: "result" (id), "journal", "file" (name) */
void logs_dispatch(void (*fn)(const char *kind, int id, const char *name));

#endif

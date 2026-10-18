/* js_help.h — lord.help (artículos de Ayuda) y lord.time.zoneOffset (Reloj). Ver js_help.c. */
#ifndef LORD_JS_HELP_H
#define LORD_JS_HELP_H

#include "quickjs.h"

/* Después de js_sys_register (agrega zoneOffset al lord.time que crea). */
void js_help_register(JSContext *c, JSValue lord);

#endif

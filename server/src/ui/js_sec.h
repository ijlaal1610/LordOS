/* js_sec.h — APIs de privacidad y firma para el tema y las apps del sistema:
 * lord.perms (permisos por app, estilo TCC) y lord.sign (firma, confianza, «Modificada por
 * vos», restaurar y compartir). Ver server/src/sec y docs/PRIVACIDAD.md, docs/FIRMA.md. */
#ifndef LORD_JS_SEC_H
#define LORD_JS_SEC_H

#include "quickjs.h"

void js_sec_register(JSContext *c, JSValue lord);

#endif

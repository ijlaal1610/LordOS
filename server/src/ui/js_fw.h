/* js_fw.h — lord.fw: permisos de red por app (ver js_fw.c y docs/FIREWALL.md). */
#ifndef LORD_JS_FW_H
#define LORD_JS_FW_H

#include "quickjs.h"

void js_fw_register(JSContext *c, JSValue lord);

#endif

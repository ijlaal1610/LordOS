/* js_fw.h — lord.fw: per-app network permissions (see js_fw.c and docs/FIREWALL.md). */
#ifndef LORD_JS_FW_H
#define LORD_JS_FW_H

#include "quickjs.h"

void js_fw_register(JSContext *c, JSValue lord);

#endif

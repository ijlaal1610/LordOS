/* js_input.h — lord.input: touchpads y pantallas táctiles (ver js_input.c y docs/TRACKPAD.md). */
#ifndef LORD_JS_INPUT_H
#define LORD_JS_INPUT_H

#include "quickjs.h"

void js_input_register(JSContext *c, JSValue lord);

#endif

/* js_print.h — lord.print: impresoras (CUPS por IPP) y escáneres eSCL (ver js_print.c). */
#ifndef LORD_JS_PRINT_H
#define LORD_JS_PRINT_H

#include "quickjs.h"

void js_print_register(JSContext *c, JSValue lord);

#endif

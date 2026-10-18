/* js_wm.h — lord.wm: arrastre de ventanas en C (ver js_wm.c y grab.h). */
#ifndef LORD_JS_WM_H
#define LORD_JS_WM_H

#include "quickjs.h"

void js_wm_register(JSContext *c, JSValue lord);
void js_wm_end(int node, int edges, float dx, float dy, float dw, float dh);   /* → __lordWm('end', …) */

#endif

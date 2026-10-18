/* js_wasm.h — el global WebAssembly (ver js_wasm.c y docs/WASM.md). */
#ifndef LORD_JS_WASM_H
#define LORD_JS_WASM_H

#include <quickjs.h>

void js_wasm_register(JSContext *ctx, JSValueConst global);
void js_wasm_shutdown(JSContext *ctx);     /* antes de JS_FreeContext */

#endif

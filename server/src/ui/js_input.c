/* js_input.c — lord.input: qué touchpads y pantallas táctiles hay y los ajustes del driver
 * (tp/tp.c). Solo lee y cambia la configuración en memoria del driver: no abre dispositivos ni
 * toca archivos (los ajustes se guardan en settings.json desde el SDK, sdk/trackpad.js).
 *
 *   lord.input.devices()      → [{ name, kind: 'touchpad'|'touchscreen', clickpad, width, height, fingers }]
 *   lord.input.config()       → { speed, tap, tapDrag, natural, secondary, threeDrag, dwt, scroll,
 *                                 kinetic, pinch, swipe3, swipe4 }
 *   lord.input.configure(obj) → aplica los campos válidos (los demás quedan); true */
#include "js_input.h"
#include "../tp/tp.h"

#include <stddef.h>
#include <string.h>

static JSValue f_devices(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    (void)this; (void)argc; (void)argv;
    char buf[4096];
    tp_list_json(buf, sizeof buf);
    return JS_ParseJSON(c, buf, strlen(buf), "<input>");
}

static const struct { const char *name; size_t off; } flags[] = {
    { "tap", offsetof(TpConfig, tap) },           { "tapDrag", offsetof(TpConfig, tap_drag) },
    { "natural", offsetof(TpConfig, natural) },   { "threeDrag", offsetof(TpConfig, three_drag) },
    { "dwt", offsetof(TpConfig, dwt) },           { "scroll", offsetof(TpConfig, scroll) },
    { "kinetic", offsetof(TpConfig, kinetic) },   { "pinch", offsetof(TpConfig, pinch) },
    { "swipe3", offsetof(TpConfig, swipe3) },     { "swipe4", offsetof(TpConfig, swipe4) },
};
static const char *const secondary[] = { "two-fingers", "bottom-right", "bottom-left" };

static JSValue f_config(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    (void)this; (void)argc; (void)argv;
    const TpConfig *k = tp_config();
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "speed", JS_NewFloat64(c, k->speed));
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++)
        JS_SetPropertyStr(c, o, flags[i].name, JS_NewBool(c, *(const int *)((const char *)k + flags[i].off)));
    JS_SetPropertyStr(c, o, "secondary", JS_NewString(c, secondary[k->secondary]));
    return o;
}

static JSValue f_configure(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    (void)this;
    if (argc < 1 || !JS_IsObject(argv[0])) return JS_FALSE;
    TpConfig k = *tp_config();
    JSValue v = JS_GetPropertyStr(c, argv[0], "speed");
    double d;
    if (JS_IsNumber(v) && !JS_ToFloat64(c, &d, v) && d >= 0 && d <= 1) k.speed = d;
    JS_FreeValue(c, v);
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        v = JS_GetPropertyStr(c, argv[0], flags[i].name);
        if (JS_IsBool(v)) *(int *)((char *)&k + flags[i].off) = JS_ToBool(c, v);
        JS_FreeValue(c, v);
    }
    v = JS_GetPropertyStr(c, argv[0], "secondary");
    if (JS_IsString(v)) {
        const char *s = JS_ToCString(c, v);
        for (int i = 0; s && i < 3; i++) if (!strcmp(s, secondary[i])) k.secondary = i;
        JS_FreeCString(c, s);
    }
    JS_FreeValue(c, v);
    tp_set_config(&k);
    return JS_TRUE;
}

static const JSCFunctionListEntry input_funcs[] = {
    JS_CFUNC_DEF("devices", 0, f_devices),
    JS_CFUNC_DEF("config", 0, f_config),
    JS_CFUNC_DEF("configure", 1, f_configure),
};

void js_input_register(JSContext *c, JSValue lord) {
    JSValue o = JS_NewObject(c);
    JS_SetPropertyFunctionList(c, o, input_funcs, sizeof input_funcs / sizeof input_funcs[0]);
    JS_SetPropertyStr(c, lord, "input", o);
}

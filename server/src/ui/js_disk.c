/* js_disk.c — lord.disks: discos y particiones para la app Discos (ver docs/SISTEMA.md).
 *
 * Solo lectura y sin root (disk/disk.c). Lo que cambia algo va por el ayudante (acciones disk-*).
 *   list()        [{ name, model, size, transport, removable, system, table, partitions: […] }]
 *   tools()       { fat32, exfat, ext4, ntfs }: qué formatos se pueden crear en este equipo
 *   watch(on)     avisos de cambios mientras la app está abierta: __lordDisks('change') cuando
 *                 se monta, se desmonta, se enchufa o cambia un disco (mountinfo + udev, sin sondeo) */
#include "js_disk.h"
#include "../disk/disk.h"

#include <stdlib.h>
#include <string.h>

static JSValue parse(JSContext *c, char *j, const char *what) {
    if (!j) return JS_NULL;
    JSValue v = JS_ParseJSON(c, j, strlen(j), what);
    free(j);
    return v;
}

static JSValue f_list(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return parse(c, disk_list_json(), "<disks.list>");
}

static JSValue f_tools(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return parse(c, disk_tools_json(), "<disks.tools>");
}

static JSValue f_watch(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    int on = argc > 0 && JS_ToBool(c, argv[0]) > 0;
    return JS_NewBool(c, disk_watch(on) == 0);
}

static const JSCFunctionListEntry funcs[] = {
    JS_CFUNC_DEF("list", 0, f_list),
    JS_CFUNC_DEF("tools", 0, f_tools),
    JS_CFUNC_DEF("watch", 1, f_watch),
};

void js_disk_register(JSContext *c, JSValue lord) {
    JSValue o = JS_NewObject(c);
    JS_SetPropertyFunctionList(c, o, funcs, sizeof funcs / sizeof funcs[0]);
    JS_SetPropertyStr(c, lord, "disks", o);
}

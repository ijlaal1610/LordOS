/* js.c — QuickJS embebido. Expone el objeto global `lord` (crear/modificar nodos) y
 * provee setTimeout, requestAnimationFrame, console y performance, que React necesita. */
#include "js.h"
#include "../ime.h"
#include "../wl/wl.h"
#include "../keyboard.h"
#include "js_sys.h"
#include "js_term.h"
#include "js_pkg.h"
#include "js_sound.h"
#include "js_monitor.h"
#include "js_sec.h"
#include "js_doc.h"
#include "js_print.h"
#include "js_vault.h"
#include "js_mail.h"
#include "js_input.h"
#include "js_fw.h"
#include "js_backup.h"
#include "js_broker.h"
#include "js_wasm.h"
#include "js_net.h"
#include "js_media.h"
#include "js_wm.h"
#include "js_disk.h"
#include "js_logs.h"
#include "js_help.h"
#include "js_hw.h"
#include "../media/media.h"
#include "ui.h"
#include "../term/term.h"

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "quickjs.h"

static JSRuntime *rt;
static JSContext *ctx;

/* Polyfills mínimos: el event loop real es el de lord-server (main.c). */
static const char prelude[] =
    "(function(){\n"
    "const g = globalThis;\n"
    "const fmt = v => typeof v === 'string' ? v : v instanceof Error ? (v.message + '\\n' + (v.stack || '')) :\n"
    "  (() => { try { return JSON.stringify(v); } catch (e) { return String(v); } })();\n"
    "const out = p => (...a) => lord.log(p + a.map(fmt).join(' '));\n"
    "g.console = { log: out(''), info: out(''), debug: out(''), warn: out('[warn] '), error: out('[error] ') };\n"
    "g.performance = { now: () => lord.now() };\n"
    "let nextId = 1; const timers = new Map();\n"
    "const add = (fn, ms, args, repeat) => { const id = nextId++; ms = Math.max(0, +ms || 0);\n"
    "  timers.set(id, { fn, args, at: lord.now() + ms, ms, repeat }); return id; };\n"
    "g.setTimeout = (fn, ms, ...a) => add(fn, ms, a, false);\n"
    "g.setInterval = (fn, ms, ...a) => add(fn, ms, a, true);\n"
    "g.setImmediate = (fn, ...a) => add(fn, 0, a, false);\n"
    "g.clearTimeout = g.clearInterval = g.clearImmediate = id => { timers.delete(id); };\n"
    "g.queueMicrotask = fn => { Promise.resolve().then(fn); };\n"
    "let frames = new Map(), frameId = 1;\n"
    "g.requestAnimationFrame = fn => { const id = frameId++; frames.set(id, fn); return id; };\n"
    "g.cancelAnimationFrame = id => { frames.delete(id); };\n"
    "g.__lordTick = now => {\n"
    "  const due = [];\n"
    "  for (const [id, t] of timers) if (t.at <= now) due.push([id, t]);\n"
    "  due.sort((a, b) => a[1].at - b[1].at || a[0] - b[0]);\n"
    "  for (const [id, t] of due) {\n"
    "    if (!timers.has(id)) continue;\n"
    "    if (t.repeat) t.at = now + Math.max(t.ms, 1); else timers.delete(id);\n"
    "    try { t.fn(...t.args); } catch (e) { console.error('timer:', e); }\n"
    "  }\n"
    "  now = lord.now(); let next = -1;\n"
    "  for (const t of timers.values()) { const d = Math.max(0, t.at - now); if (next < 0 || d < next) next = d; }\n"
    "  return next;\n"
    "};\n"
    "g.__lordWantsFrame = () => frames.size > 0;\n"
    "g.__lordFrame = now => { const f = frames; frames = new Map();\n"
    "  for (const fn of f.values()) { try { fn(now); } catch (e) { console.error('raf:', e); } } };\n"
    "})();\n";

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void dump_exception(JSContext *c) {
    JSValue e = JS_GetException(c);
    const char *msg = JS_ToCString(c, e);
    fprintf(stderr, "js: %s\n", msg ? msg : "(excepción)");
    if (msg) JS_FreeCString(c, msg);
    if (JS_IsObject(e)) {
        JSValue st = JS_GetPropertyStr(c, e, "stack");
        const char *s = JS_IsUndefined(st) ? NULL : JS_ToCString(c, st);
        if (s) { fprintf(stderr, "%s\n", s); JS_FreeCString(c, s); }
        JS_FreeValue(c, st);
    }
    JS_FreeValue(c, e);
}

static void drain_jobs(void) {
    JSContext *c;
    int r;
    while ((r = JS_ExecutePendingJob(rt, &c)) != 0)
        if (r < 0) dump_exception(c);
}

static Node *arg_node(JSContext *c, JSValueConst v) {
    int32_t id;
    if (JS_ToInt32(c, &id, v)) return NULL;
    return node_get(id);
}

/* ---- funciones nativas: lord.* ---- */

static JSValue f_create_node(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *t = JS_ToCString(c, argv[0]);
    if (!t) return JS_EXCEPTION;
    int type = !strcmp(t, "text") ? NODE_TEXT : !strcmp(t, "image") ? NODE_IMAGE
             : !strcmp(t, "raw") ? NODE_RAW : !strcmp(t, "terminal") ? NODE_TERM
             : !strcmp(t, "surface") ? NODE_SURFACE : !strcmp(t, "media") ? NODE_MEDIA : NODE_VIEW;
    JS_FreeCString(c, t);
    Node *n = node_new(type);
    return n ? JS_NewInt32(c, n->id) : JS_ThrowOutOfMemory(c);
}

static JSValue f_set_text(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    Node *n = arg_node(c, argv[0]);
    const char *s = JS_ToCString(c, argv[1]);
    if (n && s) node_set_text(n, s);
    if (s) JS_FreeCString(c, s);
    return JS_UNDEFINED;
}

static JSValue f_set_style(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    Node *n = arg_node(c, argv[0]);
    const char *key = JS_ToCString(c, argv[1]);
    if (!n || !key) { if (key) JS_FreeCString(c, key); return JS_UNDEFINED; }
    JSValueConst v = argv[2];
    int err = 0;
    Style before = n->s;
    if (!strcmp(key, "gradient")) {
        if (JS_IsArray(c, v)) {
            double a[3] = {180, 0, 0};
            for (int i = 0; i < 3; i++) {
                JSValue e = JS_GetPropertyUint32(c, v, (uint32_t)i);
                JS_ToFloat64(c, &a[i], e);
                JS_FreeValue(c, e);
            }
            n->s.has_gradient = 1;
            n->s.grad_angle = (float)a[0];
            n->s.grad_from = (uint32_t)(int64_t)a[1];
            n->s.grad_to = (uint32_t)(int64_t)a[2];
        } else n->s.has_gradient = 0;
    } else if (JS_IsUndefined(v) || JS_IsNull(v)) {
        err = style_reset(&n->s, key);
    } else if (JS_IsNumber(v)) {
        double d;
        JS_ToFloat64(c, &d, v);
        err = style_set_number(&n->s, key, d);
    } else if (JS_IsString(v)) {
        const char *s = JS_ToCString(c, v);
        err = s ? style_set_string(&n->s, key, s) : -1;
        if (s) JS_FreeCString(c, s);
    }
    if (err) fprintf(stderr, "js: estilo no soportado '%s'\n", key);
    JS_FreeCString(c, key);
    /* el mismo valor (ej.: React confirma la posición que dejó un arrastre en C): nada que repintar */
    if (memcmp(&before, &n->s, sizeof before)) node_invalidate(n);
    return JS_UNDEFINED;
}

static JSValue f_set_prop(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    Node *n = arg_node(c, argv[0]);
    const char *key = JS_ToCString(c, argv[1]);
    if (n && key && !strcmp(key, "src")) {
        image_release(n->img);
        n->img = NULL;
        const char *src = JS_IsString(argv[2]) ? JS_ToCString(c, argv[2]) : NULL;
        if (src) {
            char path[2048];
            if (src[0] == '/') snprintf(path, sizeof path, "%s", src);
            else if (!strncmp(src, "asset:", 6) && !strstr(src, "..")) snprintf(path, sizeof path, "%s/%s", js_sys_assets(), src + 6);
            else snprintf(path, sizeof path, "%s/%s", ui.theme_dir, src);
            n->img = image_get(path);
            JS_FreeCString(c, src);
        }
        node_invalidate(n);
    } else if (n && key && !strcmp(key, "pixels") && n->type == NODE_IMAGE) {
        /* { width, height, data: ArrayBuffer | Uint8Array | Uint8ClampedArray (RGBA) } */
        int32_t w = 0, h = 0;
        JSValue wv = JS_GetPropertyStr(c, argv[2], "width"), hv = JS_GetPropertyStr(c, argv[2], "height");
        JSValue dv = JS_GetPropertyStr(c, argv[2], "data");
        JS_ToInt32(c, &w, wv);
        JS_ToInt32(c, &h, hv);
        size_t len = 0, off = 0, el = 0;
        uint8_t *data = JS_GetArrayBuffer(c, &len, dv);
        if (!data) {
            JS_FreeValue(c, JS_GetException(c));
            JSValue ab = JS_GetTypedArrayBuffer(c, dv, &off, &len, &el);
            if (JS_IsException(ab)) JS_FreeValue(c, JS_GetException(c));
            else {
                size_t abl = 0;
                uint8_t *base = JS_GetArrayBuffer(c, &abl, ab);
                if (base && off + len <= abl) data = base + off;
                JS_FreeValue(c, ab);
            }
        }
        if (data && w > 0 && h > 0 && w <= 4096 && h <= 4096 && len >= (size_t)w * h * 4) {
            Image *img = image_pixels(n->img, w, h, data);
            n->img = img;
        } else if (!JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2])) fprintf(stderr, "js: pixels inválidos (%dx%d, %zu bytes)\n", w, h, len);
        JS_FreeValue(c, wv);
        JS_FreeValue(c, hv);
        JS_FreeValue(c, dv);
        node_invalidate(n);
    } else if (n && key && !strcmp(key, "session") && n->type == NODE_TERM) {
        int32_t id = 0;
        if (JS_IsNumber(argv[2])) JS_ToInt32(c, &id, argv[2]);
        n->term = id;
        Session *s = term_get(id);
        if (s) s->node = n->id;
        node_invalidate(n);
    } else if (n && key && !strcmp(key, "media") && n->type == NODE_MEDIA) {
        int32_t id = 0;                     /* origen de lord.media (video, foto, captura, cámara) */
        if (JS_IsNumber(argv[2])) JS_ToInt32(c, &id, argv[2]);
        js_media_node(n, id);
        node_invalidate(n);
    } else if (n && key && !strcmp(key, "protected")) {
        n->protect = JS_ToBool(c, argv[2]) ? 1 : 0;     /* ventana protegida (wm.setProtected) */
        node_invalidate(n);
    } else if (n && key && !strcmp(key, "surface") && n->type == NODE_SURFACE) {
        int32_t id = 0;                     /* ventana de una app Wayland (wl/wl.c) */
        if (JS_IsNumber(argv[2])) JS_ToInt32(c, &id, argv[2]);
        n->term = id;
        node_invalidate(n);
    }
    if (key) JS_FreeCString(c, key);
    return JS_UNDEFINED;
}

static JSValue f_append(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    Node *p = arg_node(c, argv[0]), *ch = arg_node(c, argv[1]);
    if (p && ch) node_append(p, ch);
    return JS_UNDEFINED;
}

static JSValue f_insert_before(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    Node *p = arg_node(c, argv[0]), *ch = arg_node(c, argv[1]), *b = arg_node(c, argv[2]);
    if (p && ch) node_insert_before(p, ch, b);
    return JS_UNDEFINED;
}

static JSValue f_remove(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    Node *ch = arg_node(c, argv[1]);
    if (ch) node_detach(ch);
    return JS_UNDEFINED;
}

static JSValue f_destroy(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    Node *n = arg_node(c, argv[0]);
    if (n) node_destroy(n);
    return JS_UNDEFINED;
}

static JSValue f_set_root(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    ui.root = arg_node(c, argv[0]);
    ui.dirty = 1;
    return JS_UNDEFINED;
}

static JSValue f_get_layout(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    Node *n = arg_node(c, argv[0]);
    JSValue a = JS_NewArray(c);
    if (n) {
        JS_SetPropertyUint32(c, a, 0, JS_NewFloat64(c, n->ax));
        JS_SetPropertyUint32(c, a, 1, JS_NewFloat64(c, n->ay));
        JS_SetPropertyUint32(c, a, 2, JS_NewFloat64(c, n->w));
        JS_SetPropertyUint32(c, a, 3, JS_NewFloat64(c, n->h));
    }
    return a;
}

static JSValue f_now(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewFloat64(c, now_ms());
}

static JSValue f_log(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *s = JS_ToCString(c, argv[0]);
    if (s) { fprintf(stderr, "js: %s\n", s); JS_FreeCString(c, s); }
    return JS_UNDEFINED;
}

/* Métricas para el tema (ej.: ventana "Acerca de"). */
static JSValue f_stats(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    long pages = 0, rss = 0;
    FILE *f = fopen("/proc/self/statm", "r");
    if (f) { if (fscanf(f, "%ld %ld", &pages, &rss) != 2) rss = 0; fclose(f); }
    JSMemoryUsage mu;
    JS_ComputeMemoryUsage(rt, &mu);
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "rssKB", JS_NewInt64(c, rss * 4));
    JS_SetPropertyStr(c, o, "jsKB", JS_NewInt64(c, mu.malloc_size / 1024));
    JS_SetPropertyStr(c, o, "nodes", JS_NewInt32(c, node_count()));
    JS_SetPropertyStr(c, o, "layoutMs", JS_NewFloat64(c, ui.layout_ms));
    JS_SetPropertyStr(c, o, "paintMs", JS_NewFloat64(c, ui.paint_ms));
    return o;
}

static const JSCFunctionListEntry lord_funcs[] = {
    JS_CFUNC_DEF("createNode", 1, f_create_node),
    JS_CFUNC_DEF("setText", 2, f_set_text),
    JS_CFUNC_DEF("setStyle", 3, f_set_style),
    JS_CFUNC_DEF("setProp", 3, f_set_prop),
    JS_CFUNC_DEF("appendChild", 2, f_append),
    JS_CFUNC_DEF("insertBefore", 3, f_insert_before),
    JS_CFUNC_DEF("removeChild", 2, f_remove),
    JS_CFUNC_DEF("destroy", 1, f_destroy),
    JS_CFUNC_DEF("setRoot", 1, f_set_root),
    JS_CFUNC_DEF("getLayout", 1, f_get_layout),
    JS_CFUNC_DEF("now", 0, f_now),
    JS_CFUNC_DEF("log", 1, f_log),
    JS_CFUNC_DEF("stats", 0, f_stats),
};

/* Ejecuta código JS. Solo se usa con el prelude interno y con el bundle del tema que
 * se pasa por línea de comandos (archivo local de confianza); nunca con datos externos. */
static int eval(const char *src, size_t len, const char *name) {
    JSValue r = JS_Eval(ctx, src, len, name, JS_EVAL_TYPE_GLOBAL);
    int ok = !JS_IsException(r);
    if (!ok) dump_exception(ctx);
    JS_FreeValue(ctx, r);
    drain_jobs();
    return ok ? 0 : -1;
}

static JSValue call_global(const char *name, int argc, JSValueConst *argv) {
    JSValue g = JS_GetGlobalObject(ctx), f = JS_GetPropertyStr(ctx, g, name), r = JS_UNDEFINED;
    if (JS_IsFunction(ctx, f)) {
        r = JS_Call(ctx, f, g, argc, argv);
        if (JS_IsException(r)) { dump_exception(ctx); r = JS_UNDEFINED; }
    }
    JS_FreeValue(ctx, f);
    JS_FreeValue(ctx, g);
    return r;
}

int js_init(const char *bundle_path) {
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", bundle_path);
    snprintf(ui.theme_dir, sizeof ui.theme_dir, "%s", dirname(dir));

    FILE *f = fopen(bundle_path, "rb");
    if (!f) { perror(bundle_path); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc((size_t)len + 1);
    if (!src || fread(src, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(src); return -1; }
    src[len] = 0;
    fclose(f);

    rt = JS_NewRuntime();
    JS_SetMemoryLimit(rt, 256u << 20);
    JS_SetMaxStackSize(rt, 4u << 20);
    ctx = JS_NewContext(rt);

    JSValue g = JS_GetGlobalObject(ctx);
    JSValue lord = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, lord, lord_funcs, sizeof lord_funcs / sizeof lord_funcs[0]);
    JSValue screen = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, screen, "width", JS_NewFloat64(ctx, ui.width));
    JS_SetPropertyStr(ctx, screen, "height", JS_NewFloat64(ctx, ui.height));
    JS_SetPropertyStr(ctx, lord, "screen", screen);
    JS_SetPropertyStr(ctx, lord, "version", JS_NewString(ctx, "0.7.0"));
    js_sys_init();
    js_sys_register(ctx, lord);
    js_term_register(ctx, lord);
    js_pkg_register(ctx, lord);
    js_sound_register(ctx, lord);
    js_wasm_register(ctx, g);           /* el global WebAssembly (intérprete propio, src/wasm) */
    js_net_register(ctx, lord);
    js_monitor_register(ctx, lord, js_sys_assets());   /* lord.monitor (app Monitor) */
    js_sec_register(ctx, lord);        /* lord.perms y lord.sign (privacidad y firma) */
    js_media_register(ctx, lord);      /* lord.media (video, fotos, capturas, cámara, grabadora) */
    js_doc_register(ctx, lord);        /* lord.doc: PDF, comprimidos y fuentes por el ayudante lord-doc */
    js_print_register(ctx, lord);      /* lord.print: CUPS por IPP y escáneres eSCL */
    js_vault_register(ctx, lord);      /* lord.vault: Contraseñas y el llavero por el ayudante lord-vault */
    js_mail_register(ctx, lord);       /* lord.mail: Mail, Calendar, Contacts, Reminders via lord-maild (docs/MAIL.md) */
    js_wm_register(ctx, lord);         /* lord.wm: mover y cambiar el tamaño de ventanas en C (grab.c) */
    js_input_register(ctx, lord);      /* lord.input: touchpads y pantallas táctiles (tp/) */
    js_fw_register(ctx, lord);         /* lord.fw: permisos de red por app (lord-netperm, docs/FIREWALL.md) */
    js_disk_register(ctx, lord);       /* lord.disks: disks and partitions (Disks app) */
    js_backup_register(ctx, lord);     /* lord.backup: Backups (backup/) */
    js_logs_register(ctx, lord);       /* lord.logs: journal y registros de LordOS (app Registros) */
    js_help_register(ctx, lord);       /* lord.help (Help) and lord.time.zoneOffset (Clock, tzif.c) */
    js_hw_register(ctx, lord);         /* lord.display/power/bt/mime/timedate (Settings, src/hw) */
    JS_SetPropertyStr(ctx, g, "lord", lord);
    JS_FreeValue(ctx, g);

    double t0 = now_ms();
    int r = eval(prelude, sizeof prelude - 1, "<prelude>");
    if (!r) r = eval(src, (size_t)len, bundle_path);
    fprintf(stderr, "js: tema %s cargado en %.1f ms (%ld KB)\n", bundle_path, now_ms() - t0, len / 1024);
    free(src);
    return r;
}

/* «Modo app» (lord-server --app <id>): el mismo motor JS, pero el objeto `lord` solo tiene las
 * primitivas de dibujo (locales) más el broker (fs/clipboard/perms por lord-server, que verifica
 * los permisos). Carga el «app host» (React + SDK + la app), ver app/appclient.c. */
int js_init_app(const char *host_bundle, const char *app_id) {
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", host_bundle);
    snprintf(ui.theme_dir, sizeof ui.theme_dir, "%s", dirname(dir));
    FILE *f = fopen(host_bundle, "rb");
    if (!f) { perror(host_bundle); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc((size_t)len + 1);
    if (!src || fread(src, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(src); return -1; }
    src[len] = 0;
    fclose(f);

    rt = JS_NewRuntime();
    JS_SetMemoryLimit(rt, 256u << 20);
    JS_SetMaxStackSize(rt, 4u << 20);
    ctx = JS_NewContext(rt);
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue lord = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, lord, lord_funcs, sizeof lord_funcs / sizeof lord_funcs[0]);
    JSValue screen = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, screen, "width", JS_NewFloat64(ctx, ui.width));
    JS_SetPropertyStr(ctx, screen, "height", JS_NewFloat64(ctx, ui.height));
    JS_SetPropertyStr(ctx, lord, "screen", screen);
    JS_SetPropertyStr(ctx, lord, "version", JS_NewString(ctx, "0.7.0"));
    js_broker_register(ctx, lord, app_id);         /* fs/clipboard/perms por el broker + appId */
    js_sys_register_app(ctx, lord);                /* lord.text (idioma, dirección) y lord.image */
    js_wasm_register(ctx, g);                      /* WebAssembly para loadWasm de la app */
    JS_SetPropertyStr(ctx, g, "lord", lord);
    JS_FreeValue(ctx, g);

    double t0 = now_ms();
    int r = eval(prelude, sizeof prelude - 1, "<prelude>");
    if (!r) r = eval(src, (size_t)len, host_bundle);
    fprintf(stderr, "js: app %s cargada en %.1f ms\n", app_id, now_ms() - t0);
    free(src);
    return r;
}

void js_shutdown(void) {
    media_shutdown();                   /* los ayudantes lord-media son de este contexto */
    if (ctx) js_wasm_shutdown(ctx);
    if (ctx) JS_FreeContext(ctx);
    if (rt) JS_FreeRuntime(rt);
    ctx = NULL;
    rt = NULL;
}

double js_tick(double now) {
    JSValue arg = JS_NewFloat64(ctx, now);
    JSValue r = call_global("__lordTick", 1, &arg);
    double next = -1;
    JS_ToFloat64(ctx, &next, r);
    JS_FreeValue(ctx, r);
    drain_jobs();
    return next;
}

int js_wants_frame(void) {
    JSValue r = call_global("__lordWantsFrame", 0, NULL);
    int w = JS_ToBool(ctx, r);
    JS_FreeValue(ctx, r);
    return w;
}

void js_frame(double now) {
    JSValue arg = JS_NewFloat64(ctx, now);
    JS_FreeValue(ctx, call_global("__lordFrame", 1, &arg));
    drain_jobs();
}

void js_pointer(const char *type, int target, float x, float y, int button) {
    JSValueConst args[5] = {
        JS_NewString(ctx, type), JS_NewInt32(ctx, target),
        JS_NewFloat64(ctx, x), JS_NewFloat64(ctx, y), JS_NewInt32(ctx, button),
    };
    JS_FreeValue(ctx, call_global("__lordEvent", 5, args));
    JS_FreeValue(ctx, args[0]);
    drain_jobs();
}

void js_gesture(const char *kind, const char *phase, int target, float x, float y, double a, double b, double c, double d) {
    if (!ctx) return;
    JSValueConst args[9] = {
        JS_NewString(ctx, kind), JS_NewString(ctx, phase), JS_NewInt32(ctx, target), JS_NewFloat64(ctx, x), JS_NewFloat64(ctx, y),
        JS_NewFloat64(ctx, a), JS_NewFloat64(ctx, b), JS_NewFloat64(ctx, c), JS_NewFloat64(ctx, d),
    };
    JS_FreeValue(ctx, call_global("__lordGesture", 9, args));
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    drain_jobs();
}

/* Estado del método de entrada → __lordIme({engine, preedit, candidates, selected, converting}). */
static void ime_event(void) {
    const ImeState *st = ime_state();
    JSValue o = JS_NewObject(ctx), list = JS_NewArray(ctx);
    for (int i = 0; i < st->ncand; i++) JS_SetPropertyUint32(ctx, list, (uint32_t)i, JS_NewString(ctx, st->cand[i]));
    JS_SetPropertyStr(ctx, o, "engine", JS_NewString(ctx, ime_current()));
    JS_SetPropertyStr(ctx, o, "preedit", JS_NewString(ctx, st->preedit));
    JS_SetPropertyStr(ctx, o, "candidates", list);
    JS_SetPropertyStr(ctx, o, "selected", JS_NewInt32(ctx, st->sel));
    JS_SetPropertyStr(ctx, o, "converting", JS_NewBool(ctx, st->converting));
    /* japonés: [antes, segmento con foco, después] para resaltarlo */
    int b = st->focus_begin, e = st->focus_end, len = (int)strlen(st->preedit);
    if (e > b && e <= len) {
        JSValue parts = JS_NewArray(ctx);
        JS_SetPropertyUint32(ctx, parts, 0, JS_NewStringLen(ctx, st->preedit, (size_t)b));
        JS_SetPropertyUint32(ctx, parts, 1, JS_NewStringLen(ctx, st->preedit + b, (size_t)(e - b)));
        JS_SetPropertyUint32(ctx, parts, 2, JS_NewString(ctx, st->preedit + e));
        JS_SetPropertyStr(ctx, o, "segments", parts);
    }
    JS_FreeValue(ctx, call_global("__lordIme", 1, (JSValueConst *)&o));
    JS_FreeValue(ctx, o);
}

/* Texto que confirmó el método de entrada: llega como una tecla que escribe ese texto. */
static void ime_commit(const char *text) {
    JSValueConst args[6] = { JS_NewInt32(ctx, 0), JS_NewInt32(ctx, 1), JS_NewString(ctx, text),
                             JS_NewString(ctx, text), JS_NewString(ctx, ""), JS_NewInt32(ctx, 0) };
    JS_FreeValue(ctx, call_global("__lordKey", 6, args));
    for (int i = 2; i < 5; i++) JS_FreeValue(ctx, args[i]);
}

/* Tecla → __lordKey(código, valor, key, texto, base, flags). Con libxkbcommon (keyboard.c)
 * la traducción la hace C; si no hay distribución cargada, solo código y valor. Si hay un
 * método de entrada activo (ime.c) y un campo de texto con foco, la tecla pasa primero por él. */
void js_key(int code, int value) {
    KeyInfo k;
    kb_key(code, value, &k);
    if (!kb_ready()) {
        JSValueConst args[2] = { JS_NewInt32(ctx, code), JS_NewInt32(ctx, value) };
        JS_FreeValue(ctx, call_global("__lordKey", 2, args));
        drain_jobs();
        return;
    }
    if (wl_key(code, value, &k)) { drain_jobs(); return; }      /* la app Wayland con foco */
    if (value) {
        char commit[512];
        int used = ime_key(&k, commit, sizeof commit);
        if (commit[0]) ime_commit(commit);
        if (ime_changed()) ime_event();
        if (used) { drain_jobs(); return; }
    }
    int flags = k.shift | k.ctrl << 1 | k.alt << 2 | k.altgr << 3 | k.meta << 4 | k.caps << 5 | k.dead << 6;
    JSValueConst args[6] = { JS_NewInt32(ctx, code), JS_NewInt32(ctx, value), JS_NewString(ctx, k.key),
                             JS_NewString(ctx, k.chr), JS_NewString(ctx, k.base), JS_NewInt32(ctx, flags) };
    JS_FreeValue(ctx, call_global("__lordKey", 6, args));
    for (int i = 2; i < 5; i++) JS_FreeValue(ctx, args[i]);
    drain_jobs();
}

/* Medios (media/) → __lordMedia(id, tipo, datos JSON). */
void js_media_event(int id, const char *type, const char *json) {
    if (!ctx) return;
    JSValueConst args[3] = { JS_NewInt32(ctx, id), JS_NewString(ctx, type), JS_ParseJSON(ctx, json, strlen(json), "<media>") };
    JS_FreeValue(ctx, call_global("__lordMedia", 3, args));
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, args[2]);
    drain_jobs();
}

/* Ventanas de apps Wayland (wl/wl.c) → __lordWayland(id, tipo, datos JSON). */
void js_wl_event(int id, const char *type, const char *json) {
    if (!ctx) return;
    JSValueConst args[3] = { JS_NewInt32(ctx, id), JS_NewString(ctx, type), JS_ParseJSON(ctx, json, strlen(json), "<wayland>") };
    JS_FreeValue(ctx, call_global("__lordWayland", 3, args));
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, args[2]);
    drain_jobs();
}

/* Sonido (sound/snd.c): "state" con el estado completo, "peak" con el medidor → __lordSound(tipo, dato). */
void js_sound_event(const char *type, const char *json) {
    if (!ctx) return;
    JSValueConst args[2] = { JS_NewString(ctx, type), JS_ParseJSON(ctx, json, strlen(json), "<sound>") };
    JS_FreeValue(ctx, call_global("__lordSound", 2, args));
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    drain_jobs();
}

/* Backups (backup/backup.c): the status changed → __lordBackup(status). */
void js_backup_event(const char *json) {
    if (!ctx) return;
    JSValueConst arg = JS_ParseJSON(ctx, json, strlen(json), "<backup>");
    JS_FreeValue(ctx, call_global("__lordBackup", 1, &arg));
    JS_FreeValue(ctx, arg);
    drain_jobs();
}

/* Red (net/state.c): cambió algo de enlaces, direcciones o rutas → __lordNet("link"|"addr"|"route"). */
/* Disks (disk/disk.c): something was mounted, unmounted, plugged in or changed → __lordDisks(what). */
void js_disk_event(const char *what) {
    if (!ctx) return;
    JSValueConst arg = JS_NewString(ctx, what);
    JS_FreeValue(ctx, call_global("__lordDisks", 1, &arg));
    JS_FreeValue(ctx, arg);
    drain_jobs();
}

void js_net_event(const char *what) {
    if (!ctx) return;
    JSValueConst arg = JS_NewString(ctx, what);
    JS_FreeValue(ctx, call_global("__lordNet", 1, &arg));
    JS_FreeValue(ctx, arg);
    drain_jobs();
}

/* Eventos de las terminales (título, directorio, campana, fin...) → __lordTerm(id, tipo, dato). */
/* Registros (js_logs.c): "result" (id de una consulta lista), "journal" o "file" (nombre)
 * → __lordLogs(tipo, id, nombre). */
void js_logs_event(const char *kind, int id, const char *name) {
    if (!ctx) return;
    JSValueConst args[3] = { JS_NewString(ctx, kind), JS_NewInt32(ctx, id), JS_NewString(ctx, name) };
    JS_FreeValue(ctx, call_global("__lordLogs", 3, args));
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[2]);
    drain_jobs();
}

/* Settings' hardware modules (src/hw): "display", "idle", "power", "bt", "bt-prompt", "bt-result",
 * "ppd", "ppd-result", "timedate" → __lordHw(kind, data). */
void js_hw_event(const char *kind, const char *json) {
    if (!ctx) return;
    JSValueConst args[2] = { JS_NewString(ctx, kind), JS_ParseJSON(ctx, json, strlen(json), "<hw>") };
    if (JS_IsException(args[1])) { JS_FreeValue(ctx, JS_GetException(ctx)); args[1] = JS_NULL; }
    JS_FreeValue(ctx, call_global("__lordHw", 2, args));
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    drain_jobs();
}

/* Respuestas del ayudante privilegiado: kind = LOG | OK | ERR. */
void js_admin_event(int id, const char *kind, const char *text) {
    if (!ctx) return;
    JSValueConst args[3] = { JS_NewInt32(ctx, id), JS_NewString(ctx, kind), JS_NewString(ctx, text) };
    JS_FreeValue(ctx, call_global("__lordAdmin", 3, args));
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, args[2]);
    drain_jobs();
}

/* El portapapeles cambió (copia en la terminal, en un campo, en una app Linux): avisa a JS
 * (historial). Se llama en cada vuelta del bucle; solo compara dos números. */
void js_clipboard_poll(void) {
    static unsigned seen_text, seen_img;
    unsigned g = term_clipboard_gen(), gi = wl_clip_image_gen();
    if (!ctx || (g == seen_text && gi == seen_img)) return;
    seen_text = g;
    seen_img = gi;
    JS_FreeValue(ctx, call_global("__lordClipboard", 0, NULL));
    drain_jobs();
}

/* Escritorio remoto: quién mira esta sesión desde la red (JSON [{ip, user}]). */
void js_remote_event(const char *json) {
    if (!ctx) return;
    JSValueConst args[1] = { JS_NewString(ctx, json) };
    JS_FreeValue(ctx, call_global("__lordRemote", 1, args));
    JS_FreeValue(ctx, args[0]);
    drain_jobs();
}

/* Broker de las apps .lpk aisladas: "prompt" (pedir un permiso) o "device" (indicador) →
 * __lordBroker(kind, json). El tema lo maneja en sdk/privacy.js. */
void js_broker_event(const char *kind, const char *json) {
    if (!ctx) return;
    JSValueConst args[2] = { JS_NewString(ctx, kind), JS_ParseJSON(ctx, json, strlen(json), "<broker>") };
    JS_FreeValue(ctx, call_global("__lordBroker", 2, args));
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    drain_jobs();
}

/* Proceso de una app aislada: eventos que manda lord-server por el broker (tema, idioma…) →
 * __lordBrokerEvent(tipo, datos). Ver runtime/app-host.js. */
void js_broker_app_event(const char *kind, const char *json, size_t len) {
    if (!ctx) return;
    JSValueConst args[2] = { JS_NewString(ctx, kind), JS_ParseJSON(ctx, json, len, "<broker>") };
    if (JS_IsException(args[1])) { JS_FreeValue(ctx, JS_GetException(ctx)); args[1] = JS_NULL; }
    JS_FreeValue(ctx, call_global("__lordBrokerEvent", 2, args));
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    drain_jobs();
}

/* Pantalla de ingreso (tema greeter): avisos y respuestas de lord-sessiond. */
void js_greeter_event(const char *json) {
    if (!ctx) return;
    JSValueConst args[1] = { JS_NewString(ctx, json) };
    JS_FreeValue(ctx, call_global("__lordGreeter", 1, args));
    JS_FreeValue(ctx, args[0]);
    drain_jobs();
}

/* La pantalla cambió de tamaño: se actualiza lord.screen y se avisa al tema (__lordScreen). */
void js_screen_resized(void) {
    if (!ctx) return;
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue lord = JS_GetPropertyStr(ctx, g, "lord");
    JSValue screen = JS_GetPropertyStr(ctx, lord, "screen");
    if (JS_IsObject(screen)) {
        JS_SetPropertyStr(ctx, screen, "width", JS_NewFloat64(ctx, ui.width));
        JS_SetPropertyStr(ctx, screen, "height", JS_NewFloat64(ctx, ui.height));
    }
    JS_FreeValue(ctx, screen);
    JS_FreeValue(ctx, lord);
    JS_FreeValue(ctx, g);
    JS_FreeValue(ctx, call_global("__lordScreen", 0, NULL));
    drain_jobs();
}

void js_term_event(int id, const char *type, const char *data) {
    if (!ctx) return;
    JSValueConst args[3] = { JS_NewInt32(ctx, id), JS_NewString(ctx, type), JS_NewString(ctx, data) };
    JS_FreeValue(ctx, call_global("__lordTerm", 3, args));
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, args[2]);
    drain_jobs();
}

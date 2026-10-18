/* js_monitor.c — lord.monitor: métricas del sistema para la app Monitor (ver docs/MONITOR.md).
 *
 * Todo es de solo lectura. Las lecturas de /proc, /sys y sock_diag las hace un hilo propio
 * (monitor/mon.c) solo mientras alguien llama a start() y consulta; estas funciones nunca
 * tocan archivos (salvo info() y /proc/<pid>/cgroup de cada proceso nuevo, con caché: scope_of), así
 * que no frenan el frame del compositor.
 *
 *   start() / stop()            cuenta de interesados (el hilo se duerme a los 3 s sin consultas)
 *   want(procesos, conexiones, países)  qué listas arma además de la muestra (países: base local)
 *   sample()                    última muestra, o null si no hay una nueva desde la anterior
 *   processes(idioma)           procesos, cada uno con su app (si se reconoce) — o null
 *   connections(idioma)         conexiones del usuario, con país y app — o null
 *   info()                      modelo de CPU, núcleos, ubicación por zona horaria, base de países
 *   map(vista)                  pide un cuadro del mapa vectorial (lo dibuja otro hilo; map.c)
 *   mapFrame()                  el último cuadro listo { seq, width, height, lon, lat, ppd, ms, data } o null
 */
#include "js_monitor.h"
#include "../monitor/mon.h"
#include "../monitor/map.h"
#include "../apps.h"
#include "js_sys.h"

#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint64_t seq_sample, seq_procs, seq_conns;

static JSValue parse(JSContext *c, char *j, const char *what) {
    if (!j) return JS_NULL;
    JSValue v = JS_ParseJSON(c, j, strlen(j), what);
    free(j);
    return v;
}

static JSValue f_start(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)c; (void)t; (void)argc; (void)argv;
    mon_start();
    return JS_TRUE;
}

static JSValue f_stop(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)c; (void)t; (void)argc; (void)argv;
    mon_stop();
    return JS_TRUE;
}

static JSValue f_want(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    int p = argc > 0 && JS_ToBool(c, argv[0]) > 0, k = argc > 1 && JS_ToBool(c, argv[1]) > 0, g = argc > 2 && JS_ToBool(c, argv[2]) > 0;
    mon_want(p, k, g);
    return JS_TRUE;
}

static JSValue f_sample(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return parse(c, mon_sample_json(&seq_sample), "<monitor.sample>");
}

static JSValue f_info(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    JSValue o = parse(c, mon_info_json(), "<monitor.info>");
    /* el modelo de CPU sale del mismo lugar que Acerca de (en ARM no hay "model name") */
    char cpu[160];
    js_sys_cpu_name(cpu, sizeof cpu);
    if (JS_IsObject(o) && cpu[0] && strcmp(cpu, "desconocido")) JS_SetPropertyStr(c, o, "cpuModel", JS_NewString(c, cpu));
    return o;
}

/* Idioma para los nombres de las apps: "es", "pt-BR"… (letras, dígitos, '-' y '_', máx. 15). */
static int arg_lang(JSContext *c, int argc, JSValueConst *argv, char *out, size_t n) {
    snprintf(out, n, "en");
    if (argc < 1 || !JS_IsString(argv[0])) return 0;
    const char *s = JS_ToCString(c, argv[0]);
    if (!s) return -1;
    size_t len = strlen(s);
    int ok = len > 0 && len < 16;
    for (size_t i = 0; ok && i < len; i++) ok = isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_';
    if (ok) snprintf(out, n, "%s", s);
    JS_FreeCString(c, s);
    return 0;
}

static const char *base(const char *p) { const char *b = strrchr(p, '/'); return b ? b + 1 : p; }

/* App de un proceso: el propio lord-server es LordOS; si no, la del .desktop cuyo programa
 * coincide con el ejecutable, el argv[0] o el nombre del proceso (comm, cortado a 15). */
static const DesktopApp *match(const DesktopApp *apps, int n, const char *exe, const char *cmd, const char *name) {
    const char *eb = exe[0] ? base(exe) : "";
    for (int i = 0; i < n; i++) {
        const char *b = apps[i].bin;
        if (!b[0]) continue;
        if ((eb[0] && !strcmp(b, eb)) || (cmd[0] && !strcmp(b, cmd)) || (name[0] && (strlen(name) >= 15 ? !strncmp(b, name, 15) : !strcmp(b, name))))
            return &apps[i];
    }
    for (int i = 0; i < n; i++)                         /* StartupWMClass == nombre (sin mayúsculas) */
        if (apps[i].wmclass[0] && name[0] && !strcasecmp(apps[i].wmclass, name)) return &apps[i];
    return NULL;
}

/* La scope de cgroup de un proceso (app-lordos-<id>.scope / app-lordos-lpk-<id>.scope), la misma
 * identidad que usa el firewall en el kernel (docs/FIREWALL.md): así un curl corriendo dentro de
 * foot cuenta como foot. Una lectura chica de /proc por pid, con una caché de 64 pids. */
static int scope_of(int pid, char *kind, char *id, size_t n) {
    static struct { int pid; char kind; char id[160]; } cache[64];
    int slot = pid & 63;
    if (cache[slot].pid == pid) { *kind = cache[slot].kind; snprintf(id, n, "%s", cache[slot].id); return *kind ? 0 : -1; }
    char path[64], line[512];
    snprintf(path, sizeof path, "/proc/%d/cgroup", pid);
    FILE *f = fopen(path, "re");
    char k = 0, buf[160] = "";
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (strncmp(line, "0::", 3)) continue;
            line[strcspn(line, "\n")] = 0;
            const char *leaf = strrchr(line, '/');
            leaf = leaf ? leaf + 1 : line + 3;
            size_t l = strlen(leaf);
            if (l > 17 && !strncmp(leaf, "app-lordos-", 11) && !strcmp(leaf + l - 6, ".scope") && l - 17 < sizeof buf) {
                const char *b = leaf + 11;
                size_t bl = l - 17;
                if (bl > 4 && !strncmp(b, "lpk-", 4)) { k = 'l'; b += 4; bl -= 4; } else k = 'd';
                memcpy(buf, b, bl);
                buf[bl] = 0;
            }
        }
        fclose(f);
    }
    cache[slot].pid = pid;
    cache[slot].kind = k;
    snprintf(cache[slot].id, sizeof cache[slot].id, "%s", buf);
    *kind = k;
    snprintf(id, n, "%s", buf);
    return k ? 0 : -1;
}

static void str_prop(JSContext *c, JSValueConst o, const char *key, char *out, size_t n) {
    JSValue v = JS_GetPropertyStr(c, o, key);
    const char *s = JS_IsString(v) ? JS_ToCString(c, v) : NULL;
    snprintf(out, n, "%s", s ? s : "");
    if (s) JS_FreeCString(c, s);
    JS_FreeValue(c, v);
}

/* Agrega app, appName y appIcon a cada elemento de obj.list. */
static JSValue annotate(JSContext *c, JSValue obj, const char *lang) {
    if (!JS_IsObject(obj)) return obj;
    const DesktopApp *apps = NULL;
    int napps = apps_list(lang, &apps);
    JSValue list = JS_GetPropertyStr(c, obj, "list");
    uint32_t len = 0;
    JSValue lv = JS_GetPropertyStr(c, list, "length");
    JS_ToUint32(c, &len, lv);
    JS_FreeValue(c, lv);
    for (uint32_t i = 0; i < len && i < 20000; i++) {
        JSValue e = JS_GetPropertyUint32(c, list, i);
        JSValue self = JS_GetPropertyStr(c, e, "self");
        int is_self = JS_ToBool(c, self) > 0;
        JS_FreeValue(c, self);
        if (is_self) JS_SetPropertyStr(c, e, "app", JS_NewString(c, "lordos"));
        else {
            char exe[4096], cmd[256], name[64];
            str_prop(c, e, "exe", exe, sizeof exe);
            str_prop(c, e, "cmd", cmd, sizeof cmd);
            str_prop(c, e, "name", name, sizeof name);
            const DesktopApp *a = NULL;
            int32_t pid = 0;
            JSValue pv = JS_GetPropertyStr(c, e, "pid");
            JS_ToInt32(c, &pid, pv);
            JS_FreeValue(c, pv);
            char sk, sid[160];
            if (pid > 0 && !scope_of(pid, &sk, sid, sizeof sid)) {
                if (sk == 'l') {
                    char id[200];
                    snprintf(id, sizeof id, "lpk:%s", sid);
                    JS_SetPropertyStr(c, e, "app", JS_NewString(c, id));
                    JS_FreeValue(c, e);
                    continue;
                }
                for (int k = 0; k < napps && !a; k++) if (!strcmp(apps[k].id, sid)) a = &apps[k];
                if (!a) {                               /* la scope manda aunque no se encuentre el .desktop */
                    char id[200];
                    snprintf(id, sizeof id, "desktop:%s", sid);
                    JS_SetPropertyStr(c, e, "app", JS_NewString(c, id));
                    JS_FreeValue(c, e);
                    continue;
                }
            }
            if (!a) a = match(apps, napps, exe, cmd, name);
            if (a) {
                char id[200];
                snprintf(id, sizeof id, "desktop:%s", a->id);
                JS_SetPropertyStr(c, e, "app", JS_NewString(c, id));
                JS_SetPropertyStr(c, e, "appName", JS_NewString(c, a->name));
                JS_SetPropertyStr(c, e, "appIcon", JS_NewString(c, a->icon));
            }
        }
        JS_FreeValue(c, e);
    }
    JS_FreeValue(c, list);
    return obj;
}

static JSValue f_processes(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char lang[16];
    if (arg_lang(c, argc, argv, lang, sizeof lang)) return JS_EXCEPTION;
    return annotate(c, parse(c, mon_procs_json(&seq_procs), "<monitor.processes>"), lang);
}

static JSValue f_connections(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char lang[16];
    if (arg_lang(c, argc, argv, lang, sizeof lang)) return JS_EXCEPTION;
    return annotate(c, parse(c, mon_conns_json(&seq_conns), "<monitor.connections>"), lang);
}

/* ---- mapa ---- */

static int num_prop(JSContext *c, JSValueConst o, const char *k, double lo, double hi, double *out) {
    JSValue v = JS_GetPropertyStr(c, o, k);
    double d = 0;
    int ok = JS_IsNumber(v) && !JS_ToFloat64(c, &d, v) && isfinite(d) && d >= lo && d <= hi;
    JS_FreeValue(c, v);
    if (ok) *out = d;
    return ok ? 0 : -1;
}

static int color_prop(JSContext *c, JSValueConst o, const char *k, uint32_t *out) {
    double d;
    if (num_prop(c, o, k, 0, 4294967295.0, &d)) return -1;
    *out = (uint32_t)d;
    return 0;
}

/* map({ width, height, lon, lat, ppd, land, border, accent, homeColor, selColor,
 *       home: { lon, lat } | null, dots: [{ lon, lat, r, sel }] }) → número de pedido */
static JSValue f_map(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    if (argc < 1 || !JS_IsObject(argv[0])) return JS_ThrowTypeError(c, "falta la vista");
    JSValueConst o = argv[0];
    static MapReq q;
    memset(&q, 0, sizeof q);
    double w, h;
    if (num_prop(c, o, "width", 1, MAP_MAX_W, &w) || num_prop(c, o, "height", 1, MAP_MAX_H, &h) ||
        num_prop(c, o, "lon", -720, 720, &q.lon0) || num_prop(c, o, "lat", -360, 360, &q.lat0) || num_prop(c, o, "ppd", 0.1, 2000, &q.ppd) ||
        color_prop(c, o, "land", &q.land) || color_prop(c, o, "border", &q.border) || color_prop(c, o, "accent", &q.accent) ||
        color_prop(c, o, "homeColor", &q.home_color) || color_prop(c, o, "selColor", &q.sel_color))
        return JS_ThrowRangeError(c, "vista inválida");
    q.w = (int)w;
    q.h = (int)h;
    JSValue home = JS_GetPropertyStr(c, o, "home");
    if (JS_IsObject(home)) q.has_home = !num_prop(c, home, "lon", -180, 180, &q.hlon) && !num_prop(c, home, "lat", -90, 90, &q.hlat);
    JS_FreeValue(c, home);
    JSValue dots = JS_GetPropertyStr(c, o, "dots");
    uint32_t n = 0;
    if (JS_IsArray(c, dots)) {
        JSValue lv = JS_GetPropertyStr(c, dots, "length");
        JS_ToUint32(c, &n, lv);
        JS_FreeValue(c, lv);
    }
    for (uint32_t i = 0; i < n && q.ndots < MAP_MAX_DOTS; i++) {
        JSValue d = JS_GetPropertyUint32(c, dots, i);
        double lon, lat, r;
        if (JS_IsObject(d) && !num_prop(c, d, "lon", -180, 180, &lon) && !num_prop(c, d, "lat", -90, 90, &lat) && !num_prop(c, d, "r", 0, 40, &r)) {
            JSValue sv = JS_GetPropertyStr(c, d, "sel");
            MapDot *m = &q.dots[q.ndots++];
            m->lon = (float)lon;
            m->lat = (float)lat;
            m->r = (float)r;
            m->sel = JS_ToBool(c, sv) > 0;
            JS_FreeValue(c, sv);
        }
        JS_FreeValue(c, d);
    }
    JS_FreeValue(c, dots);
    return JS_NewFloat64(c, (double)map_request(&q));
}

static uint64_t seq_map;

static JSValue f_map_frame(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    uint8_t *rgba = NULL;
    static MapReq v;
    double ms = 0;
    if (!map_take(&seq_map, &rgba, &v, &ms)) return JS_NULL;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "seq", JS_NewFloat64(c, (double)seq_map));
    JS_SetPropertyStr(c, o, "width", JS_NewInt32(c, v.w));
    JS_SetPropertyStr(c, o, "height", JS_NewInt32(c, v.h));
    JS_SetPropertyStr(c, o, "lon", JS_NewFloat64(c, v.lon0));
    JS_SetPropertyStr(c, o, "lat", JS_NewFloat64(c, v.lat0));
    JS_SetPropertyStr(c, o, "ppd", JS_NewFloat64(c, v.ppd));
    JS_SetPropertyStr(c, o, "ms", JS_NewFloat64(c, ms));
    JS_SetPropertyStr(c, o, "data", JS_NewArrayBufferCopy(c, rgba, (size_t)v.w * v.h * 4));
    free(rgba);
    return o;
}

static const JSCFunctionListEntry monitor_funcs[] = {
    JS_CFUNC_DEF("start", 0, f_start),
    JS_CFUNC_DEF("stop", 0, f_stop),
    JS_CFUNC_DEF("want", 3, f_want),
    JS_CFUNC_DEF("sample", 0, f_sample),
    JS_CFUNC_DEF("processes", 1, f_processes),
    JS_CFUNC_DEF("connections", 1, f_connections),
    JS_CFUNC_DEF("info", 0, f_info),
    JS_CFUNC_DEF("map", 1, f_map),
    JS_CFUNC_DEF("mapFrame", 0, f_map_frame),
};

void js_monitor_register(JSContext *c, JSValue lord, const char *assets_dir) {
    mon_init(assets_dir);
    map_init(assets_dir);
    mon_reset();                       /* contexto nuevo: los start() del anterior no cuentan */
    seq_sample = seq_procs = seq_conns = 0;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyFunctionList(c, o, monitor_funcs, sizeof monitor_funcs / sizeof monitor_funcs[0]);
    JS_SetPropertyStr(c, lord, "monitor", o);
}

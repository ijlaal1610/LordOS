/* js_fw.c — lord.fw: los permisos de red por app para el Monitor y Ajustes → Privacidad
 * (docs/FIREWALL.md, pasos 2 a 4).
 *
 * lord-server es el dueño de las reglas del usuario (~/.config/lordos/firewall.json, la tienda de
 * fw/rules.c): las lee al arrancar, las cambia con estas llamadas y las guarda. Después le avisa a
 * lord-netperm (el servicio root que las aplica en el kernel) con un único mensaje sin datos,
 * "volvé a leer" (netperm/npproto.h); lord-netperm lee el archivo él mismo. De vuelta llegan el
 * estado, las conexiones nuevas y los contadores. Ninguna llamada recibe rutas: la del archivo la
 * fija el servidor, y firewall.json está en la lista de rechazo de lord.fs (fsops.c).
 *
 *   status()                     { daemon, bpf, dns, error }
 *   state()                      { mode, trusted: [app], restricted: { app: [restricción] } }
 *   appRule(app)                 'allow' | 'deny' | null   (la regla {app, out, any})
 *   setAppRule(app, v)           v: 'allow' | 'deny' | null (la saca); el interruptor del Monitor
 *   setTrusted(app, on) / setRestricted(app, [restricción]) / setMode('silent'|'ask'|'off')
 *   rules(app)                   [{ id, dir, dst, port, proto, action }]
 *   addRule({ app, dir, dst, port, proto, action }) → id    removeRule(id)
 *   poll()                       { counts: { app: [permitidas, denegadas] }, events: [...] }
 *
 * Todas validan: ids de app con fw_app_canon, destinos con fw_parse_dest, puertos 1–65535. */
#include "js_fw.h"
#include "../fw/rules.h"
#include "../netperm/npproto.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAX_EVENTS 256

static FwStore *store;
static char path[PATH_MAX + 64];
static int sock = -1;
static long long last_try;
static unsigned char *inbuf;
static size_t inlen;
static char *status_json, *counts_json;
static char *events[MAX_EVENTS];
static int nevents;

static long long now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

static void sock_close(void) {
    if (sock >= 0) close(sock);
    sock = -1;
    inlen = 0;
    free(status_json);
    status_json = NULL;
}

static void send_reload(void) {
    static const unsigned char msg[5] = { 1, 0, 0, 0, NP_MSG_RELOAD };
    if (sock >= 0 && send(sock, msg, sizeof msg, MSG_NOSIGNAL | MSG_DONTWAIT) != (ssize_t)sizeof msg) sock_close();
}

/* Se conecta (como mucho un intento cada 2 s) y pide que lea las reglas. */
static void ensure(void) {
    if (sock >= 0) return;
    long long t = now_s();
    if (last_try && t - last_try < 2) return;
    last_try = t;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return;
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", NP_SOCK_PATH);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa)) { close(fd); return; }
    sock = fd;
    send_reload();
}

static void keep(char **slot, const unsigned char *p, size_t n) {
    char *s = malloc(n + 1);
    if (!s) return;
    memcpy(s, p, n);
    s[n] = 0;
    free(*slot);
    *slot = s;
}

/* Lee lo que haya llegado, sin esperar. */
static void pump(void) {
    ensure();
    if (sock < 0) return;
    if (!inbuf && !(inbuf = malloc(NP_OUT_MAX + 8))) return;
    for (;;) {
        ssize_t n = recv(sock, inbuf + inlen, NP_OUT_MAX + 8 - inlen, MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) { sock_close(); return; }
        if (n < 0) break;
        inlen += (size_t)n;
        size_t off = 0;
        while (inlen - off >= 5) {
            const unsigned char *h = inbuf + off;
            uint32_t l = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
            if (l < 1 || l > NP_OUT_MAX) { sock_close(); return; }
            if (inlen - off < 4 + (size_t)l) break;
            const unsigned char *body = h + 5;
            size_t bl = l - 1;
            if (h[4] == NP_MSG_STATUS) keep(&status_json, body, bl);
            else if (h[4] == NP_MSG_COUNTS) keep(&counts_json, body, bl);
            else if (h[4] == NP_MSG_EVENT) {
                if (nevents == MAX_EVENTS) { free(events[0]); memmove(events, events + 1, (MAX_EVENTS - 1) * sizeof *events); nevents--; }
                events[nevents] = NULL;
                keep(&events[nevents], body, bl);
                if (events[nevents]) nevents++;
            }
            off += 4 + (size_t)l;
        }
        memmove(inbuf, inbuf + off, inlen - off);
        inlen -= off;
    }
}

static int save(void) {
    char dir[sizeof path];
    snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, '/');
    if (sl) { *sl = 0; mkdir(dir, 0700); }
    int r = fw_save(store, path);
    if (r) fprintf(stderr, "lord.fw: no se pudo guardar %s\n", path);
    ensure();
    send_reload();
    return r;
}

/* ---- validación de argumentos ---------------------------------------------------------------- */

static int arg_app(JSContext *c, JSValueConst v, char *out) {
    if (!JS_IsString(v)) return -1;
    const char *s = JS_ToCString(c, v);
    if (!s) return -1;
    int r = strlen(s) < FW_APP_MAX ? fw_app_canon(s, out, FW_APP_MAX) : -1;
    JS_FreeCString(c, s);
    return r;
}

static int str_prop(JSContext *c, JSValueConst o, const char *k, char *out, size_t n) {
    JSValue v = JS_GetPropertyStr(c, o, k);
    int r = -1;
    out[0] = 0;
    if (JS_IsUndefined(v) || JS_IsNull(v)) r = 1;
    else if (JS_IsString(v)) {
        const char *s = JS_ToCString(c, v);
        if (s && strlen(s) < n) { memcpy(out, s, strlen(s) + 1); r = 0; }
        if (s) JS_FreeCString(c, s);
    }
    JS_FreeValue(c, v);
    return r;
}

/* La regla {app, out, any, cualquier puerto y protocolo}: el interruptor del Monitor. */
static const FwRule *app_rule(const char *app, FwRule *buf, int max) {
    int n = fw_rule_list(store, app, buf, max);
    for (int i = 0; i < n && i < max; i++)
        if (buf[i].dir == FW_OUT && buf[i].kind == FW_DST_ANY && buf[i].proto == FW_PROTO_ANY && buf[i].port_lo == 0 &&
            buf[i].port_hi == 65535 && buf[i].life == FW_LIFE_FOREVER) return &buf[i];
    return NULL;
}

/* ---- funciones ------------------------------------------------------------------------------- */

static JSValue f_status(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    pump();
    JSValue o = status_json ? JS_ParseJSON(c, status_json, strlen(status_json), "<fw.status>") : JS_NewObject(c);
    if (JS_IsException(o) || !JS_IsObject(o)) { JS_FreeValue(c, o); o = JS_NewObject(c); }
    JS_SetPropertyStr(c, o, "daemon", JS_NewBool(c, sock >= 0));
    return o;
}

typedef struct { JSContext *c; JSValue trusted, restricted; uint32_t nt; } EachCtx;

static int each_app(void *u, const char *app, int trusted, uint32_t flags) {
    EachCtx *e = u;
    if (trusted) JS_SetPropertyUint32(e->c, e->trusted, e->nt++, JS_NewString(e->c, app));
    if (flags) {
        JSValue arr = JS_NewArray(e->c);
        uint32_t k = 0;
        for (uint32_t b = 1; b && b <= FW_R_ALL; b <<= 1)
            if (flags & b) JS_SetPropertyUint32(e->c, arr, k++, JS_NewString(e->c, fw_flag_name(b)));
        JS_SetPropertyStr(e->c, e->restricted, app, arr);
    }
    return 0;
}

static const char *mode_name(FwMode m) { return m == FW_MODE_ASK ? "ask" : m == FW_MODE_OFF ? "off" : "silent"; }

static JSValue f_state(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    JSValue o = JS_NewObject(c);
    EachCtx e = { c, JS_NewArray(c), JS_NewObject(c), 0 };
    fw_app_each(store, each_app, &e);
    JS_SetPropertyStr(c, o, "mode", JS_NewString(c, mode_name(fw_mode(store))));
    JS_SetPropertyStr(c, o, "trusted", e.trusted);
    JS_SetPropertyStr(c, o, "restricted", e.restricted);
    return o;
}

static JSValue f_app_rule(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char app[FW_APP_MAX];
    if (argc < 1 || arg_app(c, argv[0], app)) return JS_ThrowTypeError(c, "app inválida");
    FwRule buf[256];
    const FwRule *r = app_rule(app, buf, 256);
    return r ? JS_NewString(c, r->action == FW_DENY ? "deny" : "allow") : JS_NULL;
}

static JSValue f_set_app_rule(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char app[FW_APP_MAX];
    if (argc < 2 || arg_app(c, argv[0], app)) return JS_ThrowTypeError(c, "app inválida");
    char v[8];
    if (JS_IsNull(argv[1])) v[0] = 0;
    else {
        const char *s = JS_IsString(argv[1]) ? JS_ToCString(c, argv[1]) : NULL;
        if (!s || (strcmp(s, "allow") && strcmp(s, "deny"))) { if (s) JS_FreeCString(c, s); return JS_ThrowTypeError(c, "'allow', 'deny' o null"); }
        snprintf(v, sizeof v, "%s", s);
        JS_FreeCString(c, s);
    }
    if (!v[0]) {
        FwRule buf[256];
        const FwRule *r = app_rule(app, buf, 256);
        if (r) fw_rule_remove(store, r->id);
    } else {
        FwRule r;
        memset(&r, 0, sizeof r);
        snprintf(r.app, sizeof r.app, "%s", app);
        r.dir = FW_OUT;
        r.kind = FW_DST_ANY;
        r.port_lo = 0;
        r.port_hi = 65535;
        r.action = v[0] == 'd' ? FW_DENY : FW_ALLOW;
        r.life = FW_LIFE_FOREVER;
        r.when = (long long)time(NULL);
        if (fw_rule_add(store, &r, NULL)) return JS_FALSE;
    }
    return JS_NewBool(c, save() == 0);
}

static JSValue f_set_trusted(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char app[FW_APP_MAX];
    if (argc < 2 || arg_app(c, argv[0], app)) return JS_ThrowTypeError(c, "app inválida");
    if (fw_set_trusted(store, app, JS_ToBool(c, argv[1]) > 0)) return JS_FALSE;
    return JS_NewBool(c, save() == 0);
}

static JSValue f_set_restricted(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char app[FW_APP_MAX];
    if (argc < 2 || arg_app(c, argv[0], app)) return JS_ThrowTypeError(c, "app inválida");
    uint32_t flags = 0, len = 0;
    if (!JS_IsArray(c, argv[1])) return JS_ThrowTypeError(c, "lista de restricciones");
    JSValue lv = JS_GetPropertyStr(c, argv[1], "length");
    JS_ToUint32(c, &len, lv);
    JS_FreeValue(c, lv);
    if (len > 32) return JS_ThrowRangeError(c, "demasiadas restricciones");
    for (uint32_t i = 0; i < len; i++) {
        JSValue e = JS_GetPropertyUint32(c, argv[1], i);
        const char *s = JS_IsString(e) ? JS_ToCString(c, e) : NULL;
        uint32_t f = s ? fw_flag_from_name(s) : 0;
        if (s) JS_FreeCString(c, s);
        JS_FreeValue(c, e);
        if (!f) return JS_ThrowTypeError(c, "restricción desconocida");
        flags |= f;
    }
    if (fw_set_restricted(store, app, flags)) return JS_FALSE;
    return JS_NewBool(c, save() == 0);
}

static JSValue f_set_mode(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char m[16];
    if (argc < 1 || !JS_IsString(argv[0])) return JS_ThrowTypeError(c, "modo");
    const char *s = JS_ToCString(c, argv[0]);
    snprintf(m, sizeof m, "%s", s ? s : "");
    if (s) JS_FreeCString(c, s);
    FwMode mode = !strcmp(m, "silent") ? FW_MODE_SILENT : !strcmp(m, "ask") ? FW_MODE_ASK : !strcmp(m, "off") ? FW_MODE_OFF : (FwMode)-1;
    if ((int)mode < 0) return JS_ThrowTypeError(c, "'silent', 'ask' u 'off'");
    fw_set_mode(store, mode);
    return JS_NewBool(c, save() == 0);
}

static JSValue rule_obj(JSContext *c, const FwRule *r) {
    JSValue o = JS_NewObject(c);
    char dst[FW_NAME_MAX + 8], port[16] = "";
    fw_format_dest(r, dst, sizeof dst);
    if (!(r->port_lo == 0 && r->port_hi == 65535)) {
        if (r->port_lo == r->port_hi) snprintf(port, sizeof port, "%u", r->port_lo);
        else snprintf(port, sizeof port, "%u-%u", r->port_lo, r->port_hi);
    }
    JS_SetPropertyStr(c, o, "id", JS_NewUint32(c, r->id));
    JS_SetPropertyStr(c, o, "app", JS_NewString(c, r->app));
    JS_SetPropertyStr(c, o, "dir", JS_NewString(c, r->dir == FW_IN ? "in" : "out"));
    JS_SetPropertyStr(c, o, "dst", JS_NewString(c, dst));
    JS_SetPropertyStr(c, o, "port", JS_NewString(c, port));
    JS_SetPropertyStr(c, o, "proto", JS_NewString(c, r->proto == FW_PROTO_TCP ? "tcp" : r->proto == FW_PROTO_UDP ? "udp" : ""));
    JS_SetPropertyStr(c, o, "action", JS_NewString(c, r->action == FW_DENY ? "deny" : "allow"));
    return o;
}

static JSValue f_rules(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char app[FW_APP_MAX];
    if (argc < 1 || arg_app(c, argv[0], app)) return JS_ThrowTypeError(c, "app inválida");
    JSValue arr = JS_NewArray(c);
    int n = fw_rule_list(store, app, NULL, 0);
    if (n <= 0) return arr;
    if (n > 4096) n = 4096;
    FwRule *v = malloc((size_t)n * sizeof *v);
    if (!v) return arr;
    int got = fw_rule_list(store, app, v, n);
    if (got < n) n = got < 0 ? 0 : got;
    for (int i = 0; i < n; i++) JS_SetPropertyUint32(c, arr, (uint32_t)i, rule_obj(c, &v[i]));
    free(v);
    return arr;
}

static int parse_port(const char *s, FwRule *r) {
    r->port_lo = 0; r->port_hi = 65535;
    if (!s[0]) return 0;
    unsigned lo = 0, hi = 0;
    char extra;
    int n = sscanf(s, "%u-%u%c", &lo, &hi, &extra);
    if (n == 1) hi = lo;
    else if (n != 2) return -1;
    if (lo < 1 || hi > 65535 || lo > hi) return -1;
    r->port_lo = (uint16_t)lo;
    r->port_hi = (uint16_t)hi;
    return 0;
}

static JSValue f_add_rule(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    if (argc < 1 || !JS_IsObject(argv[0])) return JS_ThrowTypeError(c, "regla");
    char app[FW_APP_MAX], canon[FW_APP_MAX], dir[8], dst[300], port[16], proto[8], action[8];
    if (str_prop(c, argv[0], "app", app, sizeof app) || fw_app_canon(app, canon, sizeof canon)) return JS_ThrowTypeError(c, "app inválida");
    if (str_prop(c, argv[0], "dst", dst, sizeof dst)) return JS_ThrowTypeError(c, "destino");
    if (str_prop(c, argv[0], "dir", dir, sizeof dir) < 0 || str_prop(c, argv[0], "port", port, sizeof port) < 0 ||
        str_prop(c, argv[0], "proto", proto, sizeof proto) < 0 || str_prop(c, argv[0], "action", action, sizeof action))
        return JS_ThrowTypeError(c, "regla");
    FwRule r;
    memset(&r, 0, sizeof r);
    snprintf(r.app, sizeof r.app, "%s", canon);
    if (fw_parse_dest(dst, &r)) return JS_ThrowTypeError(c, "destino inválido");
    if (!dir[0] || !strcmp(dir, "out")) r.dir = FW_OUT;
    else if (!strcmp(dir, "in")) r.dir = FW_IN;
    else return JS_ThrowTypeError(c, "dirección");
    if (parse_port(port, &r)) return JS_ThrowTypeError(c, "puerto");
    if (!proto[0]) r.proto = FW_PROTO_ANY;
    else if (!strcmp(proto, "tcp")) r.proto = FW_PROTO_TCP;
    else if (!strcmp(proto, "udp")) r.proto = FW_PROTO_UDP;
    else return JS_ThrowTypeError(c, "protocolo");
    if (!strcmp(action, "allow")) r.action = FW_ALLOW;
    else if (!strcmp(action, "deny")) r.action = FW_DENY;
    else return JS_ThrowTypeError(c, "acción");
    r.life = FW_LIFE_FOREVER;
    r.when = (long long)time(NULL);
    uint32_t id = 0;
    if (fw_rule_add(store, &r, &id)) return JS_FALSE;
    save();
    return JS_NewUint32(c, id);
}

static JSValue f_remove_rule(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    uint32_t id = 0;
    if (argc < 1 || JS_ToUint32(c, &id, argv[0]) || !id) return JS_ThrowTypeError(c, "id");
    if (fw_rule_remove(store, id)) return JS_FALSE;
    return JS_NewBool(c, save() == 0);
}

static JSValue f_poll(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    pump();
    JSValue o = JS_NewObject(c);
    JSValue counts = counts_json ? JS_ParseJSON(c, counts_json, strlen(counts_json), "<fw.counts>") : JS_NewObject(c);
    if (JS_IsException(counts)) counts = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "counts", counts);
    JSValue arr = JS_NewArray(c);
    uint32_t k = 0;
    for (int i = 0; i < nevents; i++) {
        JSValue e = JS_ParseJSON(c, events[i], strlen(events[i]), "<fw.event>");
        if (JS_IsException(e)) { JSValue x = JS_GetException(c); JS_FreeValue(c, x); }
        else JS_SetPropertyUint32(c, arr, k++, e);
        free(events[i]);
    }
    nevents = 0;
    JS_SetPropertyStr(c, o, "events", arr);
    return o;
}

static const JSCFunctionListEntry fw_funcs[] = {
    JS_CFUNC_DEF("status", 0, f_status),
    JS_CFUNC_DEF("state", 0, f_state),
    JS_CFUNC_DEF("appRule", 1, f_app_rule),
    JS_CFUNC_DEF("setAppRule", 2, f_set_app_rule),
    JS_CFUNC_DEF("setTrusted", 2, f_set_trusted),
    JS_CFUNC_DEF("setRestricted", 2, f_set_restricted),
    JS_CFUNC_DEF("setMode", 1, f_set_mode),
    JS_CFUNC_DEF("rules", 1, f_rules),
    JS_CFUNC_DEF("addRule", 1, f_add_rule),
    JS_CFUNC_DEF("removeRule", 1, f_remove_rule),
    JS_CFUNC_DEF("poll", 0, f_poll),
};

void js_fw_register(JSContext *c, JSValue lord) {
    if (!store) {
        store = fw_store_new();
        const char *h = getenv("HOME");
        char real[PATH_MAX];
        if (!h || !realpath(h, real)) snprintf(real, sizeof real, "/nonexistent");
        snprintf(path, sizeof path, "%s/.config/lordos/firewall.json", real);
        char err[200] = "";
        int r = store ? fw_load(store, path, err, sizeof err) : -1;
        if (r < 0) fprintf(stderr, "lord.fw: %s inválido (%s): se empieza sin reglas\n", path, err);
        ensure();                                   /* lord-netperm lee las reglas de esta sesión */
    }
    if (!store) return;
    JSValue fw = JS_NewObject(c);
    JS_SetPropertyFunctionList(c, fw, fw_funcs, sizeof fw_funcs / sizeof fw_funcs[0]);
    JS_SetPropertyStr(c, lord, "fw", fw);
}

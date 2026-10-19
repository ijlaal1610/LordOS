/* js_hw.c — lord.display, lord.power, lord.bt, lord.mime and lord.timedate for Settings'
 * hardware pages (src/hw, docs/SETTINGS.md). Every input is validated here; nothing runs a
 * program (root work goes through the admin helper's closed actions, hw/hw_admin.c). Notices
 * arrive in JS as __lordHw(kind, data).
 *
 *   lord.display.list()                         outputs, modes, Night Light, backlight
 *   lord.display.apply(conn, w, h, mHz, rot, scale)   live (queued between frames); scale in % (100–200,
 *                                               25 % steps); true if accepted
 *   lord.display.arrange(conn, x, y, apply)     drop a display at global logical (x, y): { x, y } snapped
 *   lord.display.setMirror(on) / setMain(conn) / setCaptureAll(on)   (docs/DISPLAYS.md)
 *   lord.display.save(conn)                     keep the current configuration of every display
 *   lord.display.setTemperature(K) / temperature()
 *   lord.display.sun(zone, y, m, d)             { rise, set } minutes UTC, or null
 *   lord.display.setIdle(offS, lockS, sleepS) / wake()
 *   lord.power.state() / profiles() / setProfile(p) / buttons()
 *   lord.bt.state() / power(on) / discover(on) / pair(path) / connect(path) / disconnect(path)
 *          / forget(path) / answer(id, accept, value)
 *   lord.mime.get(mime) / set(mime, id) / guess(path) / terminal() / setTerminal(id)
 *   lord.timedate.refresh()                     answer as __lordHw('timedate', …)
 *   lord.notifyd.closed(id, reason) / action(id, key)   back to the Linux app that notified
 *   lord.hwinfo.scan()                          read-only hardware facts for the Store's suggestions
 *                                               (hw/hwscan.c: DMI, device tree, PCI/USB IDs, virtio
 *                                               ports, missing firmware when the kernel log is readable)
 *   lord.fan.status() / setProfile(p) / setCurve(pts) / force(v) / caseConfig()   (js_fan.c, lord-fand)
 */
#include "js_hw.h"
#include "js_fan.h"
#include "../hw/bt.h"
#include "../hw/defaults.h"
#include "../hw/display.h"
#include "../hw/hw.h"
#include "../hw/hwscan.h"
#include "../hw/mimeapps.h"
#include "../hw/notifyd.h"
#include "../hw/ppd.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static JSValue json_or_null(JSContext *c, char *j) {
    if (!j) return JS_NULL;
    JSValue v = JS_ParseJSON(c, j, strlen(j), "<hw>");
    free(j);
    return v;
}

static int arg_int(JSContext *c, JSValueConst v, int def) {
    int32_t x;
    return JS_ToInt32(c, &x, v) ? def : x;
}

/* A short string argument (≤ max bytes) copied into out. 0 if valid. */
static int arg_str(JSContext *c, JSValueConst v, char *out, size_t max) {
    size_t n;
    const char *s = JS_ToCStringLen(c, &n, v);
    if (!s) return -1;
    int ok = n < max && !memchr(s, 0, n);
    if (ok) memcpy(out, s, n + 1);
    JS_FreeCString(c, s);
    return ok ? 0 : -1;
}

/* ---- lord.display ---- */

static JSValue f_display_list(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return json_or_null(c, display_list_json());
}

static JSValue f_display_apply(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    if (argc < 5) return JS_FALSE;
    int conn = arg_int(c, argv[0], 0), w = arg_int(c, argv[1], 0), h = arg_int(c, argv[2], 0);
    int hz = arg_int(c, argv[3], 0), rot = arg_int(c, argv[4], 0), scale = argc > 5 ? arg_int(c, argv[5], 100) : 100;
    if (scale == 1 || scale == 2) scale *= 100;
    if (conn <= 0 || w < 0 || h < 0 || w > 16384 || h > 16384 || hz < 0 || hz > 1000000 || rot < 0 || rot > 3 || !out_scale_valid(scale)) return JS_FALSE;
    return JS_NewBool(c, display_request((uint32_t)conn, w, h, hz, rot, scale) == 0);
}

static JSValue f_display_arrange(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    if (argc < 3) return JS_NULL;
    int conn = arg_int(c, argv[0], 0), x = arg_int(c, argv[1], 0), y = arg_int(c, argv[2], 0);
    int apply = argc > 3 && JS_ToBool(c, argv[3]) > 0, sx = 0, sy = 0;
    if (conn <= 0 || x < -65536 || x > 65536 || y < -65536 || y > 65536) return JS_NULL;
    if (display_arrange((uint32_t)conn, x, y, apply, &sx, &sy)) return JS_NULL;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "x", JS_NewInt32(c, sx));
    JS_SetPropertyStr(c, o, "y", JS_NewInt32(c, sy));
    return o;
}

static JSValue f_display_mirror(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    return JS_NewBool(c, argc > 0 && display_set_mirror(JS_ToBool(c, argv[0]) > 0) == 0);
}

static JSValue f_display_main(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    int conn = argc ? arg_int(c, argv[0], 0) : 0;
    return JS_NewBool(c, conn > 0 && display_set_main((uint32_t)conn) == 0);
}

static JSValue f_display_capture(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    if (argc > 0) display_set_capture_all(JS_ToBool(c, argv[0]) > 0);
    return JS_NewBool(c, display_capture_all());
}

static JSValue f_display_save(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    int conn = argc ? arg_int(c, argv[0], 0) : 0;
    return JS_NewBool(c, conn >= 0 && display_save((uint32_t)conn) == 0);
}

static JSValue f_display_set_temp(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    int k = argc ? arg_int(c, argv[0], 6500) : 6500;
    display_set_temperature(k);
    return JS_UNDEFINED;
}

static JSValue f_display_temp(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return JS_NewInt32(c, display_temperature());
}

static JSValue f_display_sun(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char zone[96];
    if (argc < 4 || arg_str(c, argv[0], zone, sizeof zone)) return JS_NULL;
    int y = arg_int(c, argv[1], 0), m = arg_int(c, argv[2], 0), d = arg_int(c, argv[3], 0);
    double rise, set;
    if (y < 1970 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31 || display_sun(zone, y, m, d, &rise, &set)) return JS_NULL;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "rise", JS_NewFloat64(c, rise));
    JS_SetPropertyStr(c, o, "set", JS_NewFloat64(c, set));
    return o;
}

static JSValue f_display_idle(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    int v[3] = { 0, 0, 0 };
    for (int i = 0; i < 3 && i < argc; i++) { v[i] = arg_int(c, argv[i], 0); if (v[i] < 0 || v[i] > 86400) v[i] = 0; }
    display_set_idle(v[0], v[1], v[2]);
    return JS_UNDEFINED;
}

static JSValue f_display_wake(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)c; (void)t; (void)argc; (void)argv;
    display_wake();
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry display_funcs[] = {
    JS_CFUNC_DEF("list", 0, f_display_list),
    JS_CFUNC_DEF("apply", 6, f_display_apply),
    JS_CFUNC_DEF("save", 1, f_display_save),
    JS_CFUNC_DEF("arrange", 4, f_display_arrange),
    JS_CFUNC_DEF("setMirror", 1, f_display_mirror),
    JS_CFUNC_DEF("setMain", 1, f_display_main),
    JS_CFUNC_DEF("setCaptureAll", 1, f_display_capture),
    JS_CFUNC_DEF("setTemperature", 1, f_display_set_temp),
    JS_CFUNC_DEF("temperature", 0, f_display_temp),
    JS_CFUNC_DEF("sun", 4, f_display_sun),
    JS_CFUNC_DEF("setIdle", 3, f_display_idle),
    JS_CFUNC_DEF("wake", 0, f_display_wake),
};

/* ---- lord.power ---- */

static JSValue f_power_state(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    char j[4096];
    power_json(hw_power(), j, sizeof j);
    return JS_ParseJSON(c, j, strlen(j), "<power>");
}

static JSValue f_power_profiles(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return json_or_null(c, ppd_state_json());
}

static JSValue f_power_set_profile(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char p[24];
    if (!argc || arg_str(c, argv[0], p, sizeof p) || !ppd_profile_valid(p)) return JS_FALSE;
    return JS_NewBool(c, ppd_set(p) == 0);
}

/* What logind does with the power button and the lid: our drop-in if it exists, else logind's
 * defaults (poweroff, suspend, suspend). Read-only; changed by the power-buttons action. */
static JSValue f_power_buttons(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    char power[16] = "poweroff", lid[16] = "suspend", lidac[16] = "", buf[1024];
    const char *files[] = { "/etc/systemd/logind.conf", "/etc/systemd/logind.conf.d/lordos.conf" };
    for (int f = 0; f < 2; f++) {
        int fd = open(files[f], O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ssize_t r = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (r <= 0) continue;
        buf[r] = 0;
        for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
            char v[16];
            if (sscanf(line, "HandlePowerKey=%15s", v) == 1) snprintf(power, sizeof power, "%s", v);
            else if (sscanf(line, "HandleLidSwitchExternalPower=%15s", v) == 1) snprintf(lidac, sizeof lidac, "%s", v);
            else if (sscanf(line, "HandleLidSwitch=%15s", v) == 1) snprintf(lid, sizeof lid, "%s", v);
        }
    }
    if (!lidac[0]) snprintf(lidac, sizeof lidac, "%s", lid);     /* logind: unset = same as HandleLidSwitch */
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "power", JS_NewString(c, power));
    JS_SetPropertyStr(c, o, "lid", JS_NewString(c, lid));
    JS_SetPropertyStr(c, o, "lidAC", JS_NewString(c, lidac));
    return o;
}

static const JSCFunctionListEntry power_funcs[] = {
    JS_CFUNC_DEF("state", 0, f_power_state),
    JS_CFUNC_DEF("profiles", 0, f_power_profiles),
    JS_CFUNC_DEF("setProfile", 1, f_power_set_profile),
    JS_CFUNC_DEF("buttons", 0, f_power_buttons),
};

/* ---- lord.bt ---- */

static JSValue f_bt_state(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return json_or_null(c, bt_state_json());
}

static JSValue f_bt_flag(JSContext *c, JSValueConst t, int argc, JSValueConst *argv, int magic) {
    (void)t;
    int on = argc && JS_ToBool(c, argv[0]) == 1;
    return JS_NewBool(c, (magic ? bt_discover(on) : bt_power(on)) == 0);
}

static JSValue f_bt_dev(JSContext *c, JSValueConst t, int argc, JSValueConst *argv, int magic) {
    (void)t;
    char path[128];
    if (!argc || arg_str(c, argv[0], path, sizeof path) || !bt_path_valid(path)) return JS_FALSE;
    int r = magic == 0 ? bt_pair(path) : magic == 1 ? bt_connect(path) : magic == 2 ? bt_disconnect(path) : bt_forget(path);
    return JS_NewBool(c, r == 0);
}

static JSValue f_bt_answer(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    if (argc < 2) return JS_FALSE;
    int id = arg_int(c, argv[0], -1), accept = JS_ToBool(c, argv[1]) == 1;
    char value[24] = "";
    if (argc > 2 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]) && arg_str(c, argv[2], value, sizeof value)) return JS_FALSE;
    return JS_NewBool(c, bt_answer(id, accept, value) == 0);
}

static const JSCFunctionListEntry bt_funcs[] = {
    JS_CFUNC_DEF("state", 0, f_bt_state),
    JS_CFUNC_MAGIC_DEF("power", 1, f_bt_flag, 0),
    JS_CFUNC_MAGIC_DEF("discover", 1, f_bt_flag, 1),
    JS_CFUNC_MAGIC_DEF("pair", 1, f_bt_dev, 0),
    JS_CFUNC_MAGIC_DEF("connect", 1, f_bt_dev, 1),
    JS_CFUNC_MAGIC_DEF("disconnect", 1, f_bt_dev, 2),
    JS_CFUNC_MAGIC_DEF("forget", 1, f_bt_dev, 3),
    JS_CFUNC_DEF("answer", 3, f_bt_answer),
};

/* ---- lord.mime ---- */

static JSValue f_mime_get(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char mime[MIME_MAX], id[DESKTOP_ID_MAX];
    if (!argc || arg_str(c, argv[0], mime, sizeof mime) || !mime_valid(mime) || defaults_lookup(mime, id, sizeof id)) return JS_NULL;
    return JS_NewString(c, id);
}

static JSValue f_mime_set(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char mime[MIME_MAX], id[DESKTOP_ID_MAX];
    if (argc < 2 || arg_str(c, argv[0], mime, sizeof mime) || arg_str(c, argv[1], id, sizeof id)) return JS_FALSE;
    return JS_NewBool(c, mime_valid(mime) && desktop_id_valid(id) && defaults_set(mime, id) == 0);
}

static JSValue f_mime_guess(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char path[1024], mime[MIME_MAX];
    if (!argc || arg_str(c, argv[0], path, sizeof path) || defaults_guess(path, mime, sizeof mime)) return JS_NULL;
    return JS_NewString(c, mime);
}

static JSValue f_mime_terminal(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    char id[DESKTOP_ID_MAX];
    return defaults_terminal(id, sizeof id) ? JS_NULL : JS_NewString(c, id);
}

static JSValue f_mime_set_terminal(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char id[DESKTOP_ID_MAX];
    if (!argc || arg_str(c, argv[0], id, sizeof id) || !desktop_id_valid(id)) return JS_FALSE;
    return JS_NewBool(c, defaults_set_terminal(id) == 0);
}

static const JSCFunctionListEntry mime_funcs[] = {
    JS_CFUNC_DEF("get", 1, f_mime_get),
    JS_CFUNC_DEF("set", 2, f_mime_set),
    JS_CFUNC_DEF("guess", 1, f_mime_guess),
    JS_CFUNC_DEF("terminal", 0, f_mime_terminal),
    JS_CFUNC_DEF("setTerminal", 1, f_mime_set_terminal),
};

/* ---- lord.timedate ---- */

static JSValue f_td_refresh(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)c; (void)t; (void)argc; (void)argv;
    hw_timedate_refresh();
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry td_funcs[] = { JS_CFUNC_DEF("refresh", 0, f_td_refresh) };

/* ---- lord.notifyd (Linux apps' notifications, hw/notifyd.c) ---- */

static JSValue f_nd_closed(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    int id = argc ? arg_int(c, argv[0], 0) : 0, reason = argc > 1 ? arg_int(c, argv[1], 2) : 2;
    if (id > 0) notifyd_closed((uint32_t)id, (uint32_t)reason);
    return JS_UNDEFINED;
}

static JSValue f_nd_action(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    char key[129];
    int id = argc ? arg_int(c, argv[0], 0) : 0;
    if (id > 0 && argc > 1 && !arg_str(c, argv[1], key, sizeof key)) notifyd_action((uint32_t)id, key);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry nd_funcs[] = { JS_CFUNC_DEF("closed", 2, f_nd_closed), JS_CFUNC_DEF("action", 2, f_nd_action) };

/* ---- lord.hwinfo (the Store's "Suggested for your computer", docs/PACKAGES.md) ---- */

/* LORD_HWSCAN_ROOT (development and screenshots only): read a fake tree instead of the real
 * system; the facts then say fixture: true and the Store marks them as test data. Only an absolute
 * path without ".." is taken. */
static const char *hwscan_root(void) {
    const char *r = getenv("LORD_HWSCAN_ROOT");
    if (!r || r[0] != '/' || strstr(r, "..") || strlen(r) > 200) return "";
    return r;
}

static JSValue f_hwinfo_scan(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return json_or_null(c, hwscan_json(hwscan_root()));
}

static const JSCFunctionListEntry hwinfo_funcs[] = { JS_CFUNC_DEF("scan", 0, f_hwinfo_scan) };

static void add(JSContext *c, JSValue lord, const char *name, const JSCFunctionListEntry *f, int n) {
    JSValue o = JS_NewObject(c);
    JS_SetPropertyFunctionList(c, o, f, n);
    JS_SetPropertyStr(c, lord, name, o);
}

void js_hw_register(JSContext *c, JSValue lord) {
    add(c, lord, "display", display_funcs, sizeof display_funcs / sizeof display_funcs[0]);
    add(c, lord, "power", power_funcs, sizeof power_funcs / sizeof power_funcs[0]);
    add(c, lord, "bt", bt_funcs, sizeof bt_funcs / sizeof bt_funcs[0]);
    add(c, lord, "mime", mime_funcs, sizeof mime_funcs / sizeof mime_funcs[0]);
    add(c, lord, "timedate", td_funcs, sizeof td_funcs / sizeof td_funcs[0]);
    add(c, lord, "notifyd", nd_funcs, sizeof nd_funcs / sizeof nd_funcs[0]);
    add(c, lord, "hwinfo", hwinfo_funcs, sizeof hwinfo_funcs / sizeof hwinfo_funcs[0]);
    js_fan_register(c, lord);           /* lord.fan: Settings → Cooling and Monitor (js_fan.c) */
}

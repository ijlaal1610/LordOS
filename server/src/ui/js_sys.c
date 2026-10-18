/* js_sys.c — APIs de sistema para temas y apps: lord.fs, lord.system, lord.settings,
 * lord.themes, lord.quit y lord.measureText.
 *
 * Reglas de seguridad: el servidor ya corre como el usuario de la sesión (no root);
 * lord.fs trabaja solo dentro de la carpeta personal y de los discos montados del usuario
 * (/media/$USER, /run/media/$USER; fsops_allowed): lista y lee (hasta 8 MB), escribe (hasta
 * 32 MB), crea carpetas, renombra, copia y mueve (en un hilo, fsops.c), y manda a la Papelera de
 * freedesktop; borrar de verdad solo se puede adentro de la Papelera. La escritura es atómica (temporal + rename),
 * no sigue enlaces simbólicos en el archivo final y NO puede tocar ~/.local/share/applications,
 * ~/.config/autostart, ~/.local/share/lordos/apps ni ~/.config/lordos/settings.json: desde JS no
 * se puede crear un .desktop (lo único que lord.apps.launch ejecuta), ni instalar apps sin
 * lpk.c, ni pisar los ajustes. lord.settings solo escribe en ~/.config/lordos/settings.json;
 * lord.themes solo acepta nombres simples. */
#include "js_sys.h"
#include "ui.h"
#include "fonts.h"
#include "../admin.h"
#include "../apps.h"
#include "../lpk.h"
#include "../sec/sign.h"
#include "../sec/pkgstate.h"
#include "../sec/perms.h"
#include "../app/app.h"
#include "../ime.h"
#include "../wl/wl.h"
#include "../keyboard.h"
#include "../fsops.h"
#include "../remote/mirror.h"

#include <ctype.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <locale.h>
#include <time.h>
#include <unistd.h>

#define SETTINGS_MAX (1024 * 1024)
#define FS_READ_MAX (8 * 1024 * 1024)

static char home[PATH_MAX];
static char assets[PATH_MAX];

void js_sys_set_assets(const char *dir) { if (!realpath(dir, assets)) assets[0] = 0; }
const char *js_sys_assets(void) { return assets; }

static void apply_locale_conf(void);
static int lpk_reserved(const char *id);
static char *lpk_read(const char *id, const char *rel, size_t max, size_t *len);

void js_sys_init(void) {
    const char *h = getenv("HOME");
    if (!h || !realpath(h, home)) snprintf(home, sizeof home, "/nonexistent");
    fsops_init(home, getenv("USER"));
    perm_init(home);
    apply_locale_conf();
}

/* Resuelve una ruta (relativa a la carpeta personal o absoluta) y verifica que no salga de las
 * raíces permitidas: la carpeta personal y los montajes del usuario (/media/$USER, /run/media/$USER). */
static int resolve_in_home(const char *path, char *out) {
    char full[PATH_MAX * 2];
    if (path[0] == '/') snprintf(full, sizeof full, "%s", path);
    else snprintf(full, sizeof full, "%s/%s", home, path);
    if (!realpath(full, out)) return -1;
    return fsops_allowed(out) ? 0 : -1;
}

static char *read_small(const char *path, size_t max, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = malloc(max + 1);
    size_t n = buf ? fread(buf, 1, max, f) : 0;
    fclose(f);
    if (!buf) return NULL;
    buf[n] = 0;
    if (len) *len = n;
    return buf;
}

/* Valor de "clave: valor" / "clave=valor" en un archivo de texto de /proc o /etc. */
static int field(const char *text, const char *key, char *out, size_t size) {
    size_t kl = strlen(key);
    for (const char *p = text; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL) {
        if (strncmp(p, key, kl)) continue;
        const char *v = p + kl;
        while (*v == ' ' || *v == '\t' || *v == ':' || *v == '=' || *v == '"') v++;
        size_t i = 0;
        while (v[i] && v[i] != '\n' && v[i] != '"' && i + 1 < size) { out[i] = v[i]; i++; }
        out[i] = 0;
        return 0;
    }
    out[0] = 0;
    return -1;
}

/* ---- lord.fs ---- */

static JSValue f_fs_home(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewString(c, home);
}

static JSValue f_fs_list(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    if (!p) return JS_EXCEPTION;
    char dir[PATH_MAX];
    int bad = resolve_in_home(p, dir);
    JS_FreeCString(c, p);
    if (bad) return JS_ThrowRangeError(c, "ruta fuera de la carpeta personal o inexistente");
    DIR *d = opendir(dir);
    if (!d) return JS_ThrowInternalError(c, "%s", strerror(errno));
    JSValue arr = JS_NewArray(c);
    uint32_t i = 0;
    struct dirent *e;
    while ((e = readdir(d)) && i < 5000) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char fp[PATH_MAX * 2];
        struct stat st;
        snprintf(fp, sizeof fp, "%s/%s", dir, e->d_name);
        struct stat lst;
        int link = !lstat(fp, &lst) && S_ISLNK(lst.st_mode);
        if (stat(fp, &st)) { if (!link) continue; st = lst; }     /* enlace roto: se muestra igual */
        JSValue o = JS_NewObject(c);
        JS_SetPropertyStr(c, o, "name", JS_NewString(c, e->d_name));
        JS_SetPropertyStr(c, o, "link", JS_NewBool(c, link));
        JS_SetPropertyStr(c, o, "dir", JS_NewBool(c, S_ISDIR(st.st_mode)));
        JS_SetPropertyStr(c, o, "size", JS_NewFloat64(c, (double)st.st_size));
        JS_SetPropertyStr(c, o, "mtime", JS_NewFloat64(c, (double)st.st_mtime * 1000));
        JS_SetPropertyUint32(c, arr, i++, o);
    }
    closedir(d);
    return arr;
}

/* lord.fs.read(ruta, 'text' | 'binary') — contenido de un archivo regular de $HOME
 * (texto UTF-8 o ArrayBuffer). Lo usan, por ejemplo, el importador de perfiles de iTerm2. */
static JSValue f_fs_read(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    const char *p = JS_ToCString(c, argv[0]);
    if (!p) return JS_EXCEPTION;
    const char *mode = argc > 1 ? JS_ToCString(c, argv[1]) : NULL;
    int binary = mode && !strcmp(mode, "binary");
    if (mode) JS_FreeCString(c, mode);
    char path[PATH_MAX];
    int bad = resolve_in_home(p, path);
    JS_FreeCString(c, p);
    if (bad) return JS_ThrowRangeError(c, "ruta fuera de la carpeta personal o inexistente");
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode)) return JS_ThrowTypeError(c, "no es un archivo");
    if (st.st_size > FS_READ_MAX) return JS_ThrowRangeError(c, "archivo demasiado grande (máx. 8 MB)");
    size_t len;
    char *buf = read_small(path, FS_READ_MAX, &len);
    if (!buf) return JS_ThrowInternalError(c, "%s", strerror(errno));
    JSValue v = binary ? JS_NewArrayBufferCopy(c, (uint8_t *)buf, len) : JS_NewStringLen(c, buf, len);
    free(buf);
    return v;
}

/* ---- lord.system ---- */

/* En ARM64 /proc/cpuinfo no tiene "model name": el núcleo sale de "CPU part" y la placa del device
 * tree ("Cortex-A76 · Raspberry Pi 5 Model B Rev 1.0"). Ver docs/RASPBERRY.md. */
static void arm_cpu_name(const char *txt, char *out, size_t size) {
    static const struct { unsigned part; const char *name; } parts[] = {
        { 0xd03, "Cortex-A53" }, { 0xd04, "Cortex-A35" }, { 0xd05, "Cortex-A55" }, { 0xd07, "Cortex-A57" },
        { 0xd08, "Cortex-A72" }, { 0xd09, "Cortex-A73" }, { 0xd0a, "Cortex-A75" }, { 0xd0b, "Cortex-A76" },
        { 0xd0c, "Neoverse-N1" }, { 0xd0d, "Cortex-A77" }, { 0xd41, "Cortex-A78" }, { 0xd44, "Cortex-X1" },
        { 0xd46, "Cortex-A510" }, { 0xd47, "Cortex-A710" }, { 0xd80, "Cortex-A520" }, { 0xd81, "Cortex-A720" },
    };
    char part[32] = "", core[32] = "";
    if (txt && !field(txt, "CPU part", part, sizeof part)) {
        unsigned p = (unsigned)strtoul(part, NULL, 0);
        for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++)
            if (parts[i].part == p) snprintf(core, sizeof core, "%s", parts[i].name);
    }
    char *board = read_small("/proc/device-tree/model", 128, NULL);
    if (core[0] && board && board[0]) snprintf(out, size, "%s · %s", core, board);
    else if (core[0]) snprintf(out, size, "%s", core);
    else if (board && board[0]) snprintf(out, size, "%s", board);
    else snprintf(out, size, "desconocido");
    free(board);
}

/* Nombre del procesador ("model name" en x86; en ARM, el núcleo por "CPU part" y la placa). */
void js_sys_cpu_name(char *out, size_t size) {
    char *txt = read_small("/proc/cpuinfo", 1 << 20, NULL);
    if (!txt || field(txt, "model name", out, size)) arm_cpu_name(txt, out, size);
    free(txt);
}

static JSValue f_sys_info(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    struct utsname u;
    uname(&u);
    char os[128], cpu[160], buf[64];
    char *txt = read_small("/etc/os-release", 8192, NULL);
    if (!txt || field(txt, "PRETTY_NAME", os, sizeof os)) snprintf(os, sizeof os, "Linux");
    free(txt);

    int cores = 0;
    txt = read_small("/proc/cpuinfo", 1 << 20, NULL);
    if (!txt || field(txt, "model name", cpu, sizeof cpu)) arm_cpu_name(txt, cpu, sizeof cpu);
    for (const char *p = txt; p && (p = strstr(p, "processor")); p += 9) cores++;
    free(txt);

    double mem_total = 0, mem_avail = 0, uptime = 0;
    txt = read_small("/proc/meminfo", 8192, NULL);
    if (txt && !field(txt, "MemTotal", buf, sizeof buf)) mem_total = atof(buf);
    if (txt && !field(txt, "MemAvailable", buf, sizeof buf)) mem_avail = atof(buf);
    free(txt);
    txt = read_small("/proc/uptime", 128, NULL);
    if (txt) uptime = atof(txt);
    free(txt);

    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "hostname", JS_NewString(c, u.nodename));
    JS_SetPropertyStr(c, o, "kernel", JS_NewString(c, u.release));
    JS_SetPropertyStr(c, o, "arch", JS_NewString(c, u.machine));
    JS_SetPropertyStr(c, o, "os", JS_NewString(c, os));
    JS_SetPropertyStr(c, o, "cpu", JS_NewString(c, cpu));
    JS_SetPropertyStr(c, o, "cores", JS_NewInt32(c, cores));
    JS_SetPropertyStr(c, o, "memTotalKB", JS_NewFloat64(c, mem_total));
    JS_SetPropertyStr(c, o, "memAvailableKB", JS_NewFloat64(c, mem_avail));
    JS_SetPropertyStr(c, o, "uptime", JS_NewFloat64(c, uptime));
    JS_SetPropertyStr(c, o, "user", JS_NewString(c, getenv("USER") ? getenv("USER") : ""));
    /* nombre completo (primer campo de GECOS) y si es administrador (sudo/admin/wheel) */
    struct passwd *pw = getpwuid(getuid());
    char full[128] = "";
    if (pw && pw->pw_gecos) { snprintf(full, sizeof full, "%s", pw->pw_gecos); full[strcspn(full, ",")] = 0; }
    JS_SetPropertyStr(c, o, "fullName", JS_NewString(c, full));
    int is_admin = 0;
    gid_t groups[256];
    int ng = getgroups(256, groups);
    for (int i = 0; i < ng && !is_admin; i++) {
        struct group *g = getgrgid(groups[i]);
        if (g && (!strcmp(g->gr_name, "sudo") || !strcmp(g->gr_name, "admin") || !strcmp(g->gr_name, "wheel"))) is_admin = 1;
    }
    JS_SetPropertyStr(c, o, "admin", JS_NewBool(c, is_admin));
    /* sistema en vivo (ISO): ahí aparece el instalador */
    JS_SetPropertyStr(c, o, "live", JS_NewBool(c, access("/run/live/medium/live/filesystem.squashfs", F_OK) == 0));
    struct statvfs vs;                      /* disco del sistema (/) */
    if (!statvfs("/", &vs)) {
        JS_SetPropertyStr(c, o, "diskTotal", JS_NewFloat64(c, (double)vs.f_blocks * vs.f_frsize));
        JS_SetPropertyStr(c, o, "diskFree", JS_NewFloat64(c, (double)vs.f_bavail * vs.f_frsize));
    }
    return o;
}

/* [total, ocioso] en jiffies; el uso de CPU se calcula con la diferencia entre dos lecturas. */
static JSValue f_sys_cpu(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    double v[8] = {0}, total = 0;
    FILE *f = fopen("/proc/stat", "r");
    if (f) {
        if (fscanf(f, "cpu %lf %lf %lf %lf %lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) != 8)
            memset(v, 0, sizeof v);
        fclose(f);
    }
    for (int i = 0; i < 8; i++) total += v[i];
    JSValue a = JS_NewArray(c);
    JS_SetPropertyUint32(c, a, 0, JS_NewFloat64(c, total));
    JS_SetPropertyUint32(c, a, 1, JS_NewFloat64(c, v[3] + v[4]));
    return a;
}

/* ---- lord.settings ---- */

static void settings_path(char *out, size_t size, const char *suffix) {
    snprintf(out, size, "%s/.config/lordos/settings.json%s", home, suffix);
}

static JSValue f_settings_load(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    char path[PATH_MAX + 64];
    settings_path(path, sizeof path, "");
    size_t len;
    char *txt = read_small(path, SETTINGS_MAX, &len);
    if (!txt) return JS_NULL;
    JSValue s = JS_NewStringLen(c, txt, len);
    free(txt);
    return s;
}

static JSValue f_settings_save(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    size_t len;
    const char *txt = JS_ToCStringLen(c, &len, argv[0]);
    if (!txt) return JS_EXCEPTION;
    if (len > SETTINGS_MAX) { JS_FreeCString(c, txt); return JS_ThrowRangeError(c, "ajustes demasiado grandes"); }
    char dir[PATH_MAX + 64], tmp[PATH_MAX + 64], path[PATH_MAX + 64];
    snprintf(dir, sizeof dir, "%s/.config", home);
    mkdir(dir, 0700);
    snprintf(dir, sizeof dir, "%s/.config/lordos", home);
    mkdir(dir, 0700);
    settings_path(tmp, sizeof tmp, ".tmp");
    settings_path(path, sizeof path, "");
    FILE *f = fopen(tmp, "wb");
    int ok = f && fwrite(txt, 1, len, f) == len;
    if (f) ok = (fclose(f) == 0) && ok;
    if (ok) ok = rename(tmp, path) == 0;
    JS_FreeCString(c, txt);
    return JS_NewBool(c, ok);
}

/* ---- lord.themes ---- */

static int valid_id(const char *s) {
    if (!*s || strlen(s) > 64) return 0;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_') return 0;
    return 1;
}

static void themes_root(char *out, size_t size) {
    char tmp[sizeof ui.theme_dir];
    snprintf(tmp, sizeof tmp, "%s", ui.theme_dir);
    snprintf(out, size, "%s", dirname(tmp));
}

static JSValue f_themes_list(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    char root[PATH_MAX], cur[PATH_MAX];
    themes_root(root, sizeof root);
    if (!realpath(ui.theme_dir, cur)) cur[0] = 0;
    JSValue arr = JS_NewArray(c);
    DIR *d = opendir(root);
    if (!d) return arr;
    uint32_t i = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !valid_id(e->d_name)) continue;
        char bundle[PATH_MAX * 2], meta[PATH_MAX * 2], real[PATH_MAX];
        snprintf(bundle, sizeof bundle, "%s/%s/theme.bundle.js", root, e->d_name);
        if (access(bundle, R_OK)) continue;
        snprintf(meta, sizeof meta, "%s/%s/theme.json", root, e->d_name);
        char *json = read_small(meta, 4096, NULL);
        snprintf(meta, sizeof meta, "%s/%s", root, e->d_name);
        JSValue o = JS_NewObject(c);
        JS_SetPropertyStr(c, o, "id", JS_NewString(c, e->d_name));
        JS_SetPropertyStr(c, o, "meta", json ? JS_NewString(c, json) : JS_NULL);
        JS_SetPropertyStr(c, o, "current", JS_NewBool(c, realpath(meta, real) && !strcmp(real, cur)));
        JS_SetPropertyUint32(c, arr, i++, o);
        free(json);
    }
    closedir(d);
    return arr;
}

static JSValue f_themes_use(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *id = JS_ToCString(c, argv[0]);
    if (!id) return JS_EXCEPTION;
    int ok = valid_id(id);
    if (ok) {
        char root[PATH_MAX], bundle[PATH_MAX * 2];
        themes_root(root, sizeof root);
        snprintf(bundle, sizeof bundle, "%s/%s/theme.bundle.js", root, id);
        ok = access(bundle, R_OK) == 0 && strlen(bundle) < sizeof ui.theme_request &&
             strcmp(bundle, ui.theme_failed) != 0;
        if (ok) snprintf(ui.theme_request, sizeof ui.theme_request, "%s", bundle);
    }
    JS_FreeCString(c, id);
    return JS_NewBool(c, ok);
}

/* ---- lord.fonts ---- */

/* lord.fonts.list({ rescan }) → [{ family, style, ps, weight, italic, mono, color }] */
static JSValue f_fonts_list(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    int force = 0;
    if (argc > 0 && JS_IsObject(argv[0])) {
        JSValue v = JS_GetPropertyStr(c, argv[0], "rescan");
        force = JS_ToBool(c, v);
        JS_FreeValue(c, v);
    }
    int n;
    const FontEntry *l = fonts_list(force, &n);
    JSValue arr = JS_NewArray(c);
    for (int i = 0; i < n; i++) {
        JSValue o = JS_NewObject(c);
        JS_SetPropertyStr(c, o, "family", JS_NewString(c, l[i].family));
        JS_SetPropertyStr(c, o, "style", JS_NewString(c, l[i].style));
        JS_SetPropertyStr(c, o, "ps", JS_NewString(c, l[i].ps));
        JS_SetPropertyStr(c, o, "weight", JS_NewInt32(c, l[i].weight));
        JS_SetPropertyStr(c, o, "italic", JS_NewBool(c, l[i].italic));
        JS_SetPropertyStr(c, o, "mono", JS_NewBool(c, l[i].mono));
        JS_SetPropertyStr(c, o, "color", JS_NewBool(c, l[i].color));
        JS_SetPropertyStr(c, o, "path", JS_NewString(c, l[i].path));      /* Tipografías (M9) */
        JS_SetPropertyStr(c, o, "index", JS_NewInt32(c, l[i].index));
        JS_SetPropertyUint32(c, arr, (uint32_t)i, o);
    }
    return arr;
}

/* ---- lord.admin: acciones del ayudante privilegiado (admin.c) ---- */

static JSValue f_admin_available(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewBool(c, admin_fd() >= 0);
}

/* lord.admin.request(acción, argumento): devuelve el id del pedido (0 si no se pudo mandar);
 * las respuestas llegan a __lordAdmin(id, kind, texto). Solo acciones de la lista cerrada de
 * admin.c, que además valida cada argumento del lado root. */
static JSValue f_admin_request(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    size_t len = 0;
    const char *a = JS_ToCString(c, argv[0]), *arg = JS_ToCStringLen(c, &len, argv[1]);
    int id = a && arg && admin_known(a) ? admin_send(a, arg, len) : -1;
    if (a) JS_FreeCString(c, a);
    if (arg) JS_FreeCString(c, arg);
    return JS_NewInt32(c, id > 0 ? id : 0);
}

/* ---- lord.locale: región del usuario e idiomas de la interfaz ---- */

static int valid_locale_name(const char *s) {
    size_t n = strlen(s);
    if (n > 64) return 0;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && !strchr("_.-@", *s)) return 0;
    return 1;
}

static const char *const lc_vars[] = { "LANG", "LANGUAGE", "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY",
                                       "LC_MESSAGES", "LC_PAPER", "LC_MEASUREMENT", NULL };

static void locale_conf(char *out, size_t n) { snprintf(out, n, "%s/.config/locale.conf", home); }

/* Valores de un archivo "VAR=valor" (locale.conf / /etc/default/locale) en el objeto. */
static void read_vars(JSContext *c, JSValue o, const char *path) {
    char *txt = read_small(path, 8192, NULL);
    for (char *line = txt ? strtok(txt, "\n") : NULL; line; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        if (*v == '"') { v++; char *q = strchr(v, '"'); if (q) *q = 0; }
        for (int i = 0; lc_vars[i]; i++)
            if (!strcmp(line, lc_vars[i]) && valid_locale_name(v)) JS_SetPropertyStr(c, o, line, JS_NewString(c, v));
    }
    free(txt);
}

/* Al arrancar: la región del usuario (o la del sistema) pasa al entorno, para que la
 * hereden los shells de la Terminal y las apps que se lancen. */
static void apply_locale_conf(void) {
    char path[PATH_MAX + 32];
    locale_conf(path, sizeof path);
    const char *files[2] = { "/etc/default/locale", path };
    for (int k = 0; k < 2; k++) {
        char *txt = read_small(files[k], 8192, NULL);
        for (char *line = txt ? strtok(txt, "\n") : NULL; line; line = strtok(NULL, "\n")) {
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            char *v = eq + 1;
            if (*v == '"') { v++; char *q = strchr(v, '"'); if (q) *q = 0; }
            for (int i = 0; lc_vars[i]; i++) if (!strcmp(line, lc_vars[i]) && valid_locale_name(v)) setenv(line, v, 1);
        }
        free(txt);
    }
}

/* lord.locale.get() → { user: {LANG…}, system: {LANG…} } */
static JSValue f_locale_get(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue o = JS_NewObject(c), u = JS_NewObject(c), sys = JS_NewObject(c);
    char path[PATH_MAX + 32];
    locale_conf(path, sizeof path);
    read_vars(c, u, path);
    read_vars(c, sys, "/etc/default/locale");
    JS_SetPropertyStr(c, o, "user", u);
    JS_SetPropertyStr(c, o, "system", sys);
    return o;
}

/* lord.locale.set({ LANG, LC_TIME, … }) escribe ~/.config/locale.conf y lo aplica a los
 * procesos que se lancen desde ahora (shells de la Terminal, y apps en el futuro). */
static JSValue f_locale_set(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    if (!JS_IsObject(argv[0])) return JS_FALSE;
    char buf[2048] = "";
    size_t n = 0;
    for (int i = 0; lc_vars[i]; i++) {
        JSValue v = JS_GetPropertyStr(c, argv[0], lc_vars[i]);
        const char *s = JS_IsString(v) ? JS_ToCString(c, v) : NULL;
        if (s && s[0] && valid_locale_name(s)) {
            n += (size_t)snprintf(buf + n, sizeof buf - n, "%s=%s\n", lc_vars[i], s);
            setenv(lc_vars[i], s, 1);
        } else unsetenv(lc_vars[i]);
        if (s) JS_FreeCString(c, s);
        JS_FreeValue(c, v);
    }
    char dir[PATH_MAX + 16], path[PATH_MAX + 32], tmp[PATH_MAX + 40];
    snprintf(dir, sizeof dir, "%s/.config", home);
    mkdir(dir, 0700);
    locale_conf(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    int ok = f && fwrite(buf, 1, n, f) == n;
    if (f) ok = fclose(f) == 0 && ok;
    if (ok) ok = rename(tmp, path) == 0;
    return JS_NewBool(c, ok);
}

/* lord.locale.available() → [{ name: "es_AR.UTF-8", generated }] (UTF-8 de glibc) */
static JSValue f_locale_available(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue arr = JS_NewArray(c);
    FILE *f = fopen("/usr/share/i18n/SUPPORTED", "r");
    char line[256];
    uint32_t i = 0;
    while (f && fgets(line, sizeof line, f)) {
        char loc[128], cs[64];
        if (sscanf(line, "%127s %63s", loc, cs) != 2 || strcmp(cs, "UTF-8")) continue;
        locale_t l = newlocale(LC_ALL_MASK, loc, (locale_t)0);
        if (l) freelocale(l);
        JSValue o = JS_NewObject(c);
        JS_SetPropertyStr(c, o, "name", JS_NewString(c, loc));
        JS_SetPropertyStr(c, o, "generated", JS_NewBool(c, l != 0));
        JS_SetPropertyUint32(c, arr, i++, o);
    }
    if (f) fclose(f);
    return arr;
}

static int valid_id_dot(const char *s) {
    size_t n = strlen(s);
    if (!n || n > 32) return 0;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_') return 0;
    return 1;
}

/* Carpetas de catálogos: assets/locales (incluidos) y ~/.local/share/lordos/locales
 * (paquetes de idioma del usuario; ganan si existen los dos). */
static void locale_root(int user, char *out, size_t n) {
    if (user) snprintf(out, n, "%s/.local/share/lordos/locales", home);
    else snprintf(out, n, "%s/locales", assets);
}

/* lord.locale.catalog(idioma, espacio) → texto JSON o null */
static JSValue f_locale_catalog(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *lang = JS_ToCString(c, argv[0]), *ns = JS_ToCString(c, argv[1]);
    JSValue r = JS_NULL;
    if (lang && ns && valid_id_dot(lang) && valid_id_dot(ns)) {
        for (int user = 1; user >= 0 && JS_IsNull(r); user--) {
            char root[PATH_MAX + 64], path[PATH_MAX * 2];
            locale_root(user, root, sizeof root);
            snprintf(path, sizeof path, "%s/%s/%s.json", root, lang, ns);
            size_t len;
            char *txt = read_small(path, 1 << 20, &len);
            if (txt) { r = JS_NewStringLen(c, txt, len); free(txt); }
        }
        /* textos de una app instalada como paquete: locales/<idioma>.json dentro de ella */
        if (JS_IsNull(r) && lpk_valid_id(ns) && !lpk_reserved(ns)) {
            char rel[64];
            snprintf(rel, sizeof rel, "locales/%s.json", lang);
            size_t len;
            char *txt = lpk_read(ns, rel, 1 << 20, &len);
            if (txt) { r = JS_NewStringLen(c, txt, len); free(txt); }
        }
    }
    if (lang) JS_FreeCString(c, lang);
    if (ns) JS_FreeCString(c, ns);
    return r;
}

/* lord.locale.languages() → [{ id, meta (texto de meta.json), user }] */
static JSValue f_locale_languages(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue arr = JS_NewArray(c);
    uint32_t n = 0;
    for (int user = 0; user < 2; user++) {
        char root[PATH_MAX + 64];
        locale_root(user, root, sizeof root);
        DIR *d = opendir(root);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            if (!valid_id_dot(e->d_name)) continue;
            char meta[PATH_MAX * 2];
            snprintf(meta, sizeof meta, "%s/%s/meta.json", root, e->d_name);
            char *txt = read_small(meta, 4096, NULL);
            if (!txt) continue;
            JSValue o = JS_NewObject(c);
            JS_SetPropertyStr(c, o, "id", JS_NewString(c, e->d_name));
            JS_SetPropertyStr(c, o, "meta", JS_NewString(c, txt));
            JS_SetPropertyStr(c, o, "user", JS_NewBool(c, user));
            JS_SetPropertyUint32(c, arr, n++, o);
            free(txt);
        }
        if (d) closedir(d);
    }
    return arr;
}

/* lord.locale.installPack(idioma, { espacio: textoJSON }) → copia un paquete de idioma de LordOS
 * a ~/.local/share/lordos/locales/<idioma>/ (no necesita root). */
static JSValue f_locale_install(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *lang = JS_ToCString(c, argv[0]);
    int ok = lang && valid_id_dot(lang) && JS_IsObject(argv[1]);
    char dir[PATH_MAX + 128];
    if (ok) {
        char p[PATH_MAX + 64];
        snprintf(p, sizeof p, "%s/.local", home); mkdir(p, 0700);
        snprintf(p, sizeof p, "%s/.local/share", home); mkdir(p, 0700);
        snprintf(p, sizeof p, "%s/.local/share/lordos", home); mkdir(p, 0700);
        snprintf(p, sizeof p, "%s/.local/share/lordos/locales", home); mkdir(p, 0700);
        snprintf(dir, sizeof dir, "%s/%s", p, lang);
        mkdir(dir, 0700);
        JSPropertyEnum *props;
        uint32_t np;
        if (JS_GetOwnPropertyNames(c, &props, &np, argv[1], JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) ok = 0;
        for (uint32_t i = 0; ok && i < np; i++) {
            const char *ns = JS_AtomToCString(c, props[i].atom);
            JSValue v = JS_GetProperty(c, argv[1], props[i].atom);
            size_t len;
            const char *txt = JS_IsString(v) ? JS_ToCStringLen(c, &len, v) : NULL;
            if (ns && txt && valid_id_dot(ns) && len < (1 << 20)) {
                char path[PATH_MAX * 2];
                snprintf(path, sizeof path, "%s/%s.json", dir, ns);
                FILE *f = fopen(path, "w");
                if (!f || fwrite(txt, 1, len, f) != len) ok = 0;
                if (f) fclose(f);
            } else ok = 0;
            if (txt) JS_FreeCString(c, txt);
            if (ns) JS_FreeCString(c, ns);
            JS_FreeValue(c, v);
        }
        js_free(c, props);
    }
    if (lang) JS_FreeCString(c, lang);
    return JS_NewBool(c, ok);
}

/* ---- lord.keyboard: distribuciones (libxkbcommon, keyboard.c) ---- */

static JSValue f_kb_set(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *l = JS_ToCString(c, argv[0]), *v = JS_ToCString(c, argv[1]);
    int ok = l && v && kb_set(l, v) == 0;
    if (l) JS_FreeCString(c, l);
    if (v) JS_FreeCString(c, v);
    if (ok) wl_keymap_changed();            /* las apps Wayland reciben el mapa nuevo */
    return JS_NewBool(c, ok);
}

static JSValue f_kb_current(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "layouts", JS_NewString(c, kb_layouts()));
    JS_SetPropertyStr(c, o, "variants", JS_NewString(c, kb_variants()));
    JS_SetPropertyStr(c, o, "group", JS_NewInt32(c, kb_group()));
    JS_SetPropertyStr(c, o, "xkb", JS_NewBool(c, kb_ready()));
    return o;
}

static JSValue f_kb_group(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    int32_t g = 0;
    JS_ToInt32(c, &g, argv[0]);
    kb_set_group(g);
    return JS_UNDEFINED;
}

/* ---- lord.ime: métodos de entrada (ime.c) ---- */

static JSValue f_ime_set(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *n = JS_ToCString(c, argv[0]);
    if (!n) return JS_EXCEPTION;
    int r = ime_set(n);
    JS_FreeCString(c, n);
    return JS_NewInt32(c, r);
}

static JSValue f_ime_context(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    ime_context(JS_ToBool(c, argv[0]));
    return JS_UNDEFINED;
}

static JSValue f_ime_reset(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)c; (void)this_val; (void)argc; (void)argv;
    ime_reset();
    return JS_UNDEFINED;
}

static JSValue f_ime_info(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "engine", JS_NewString(c, ime_current()));
    JS_SetPropertyStr(c, o, "dictionary", JS_NewString(c, ime_dict_path()));
    return o;
}

static JSValue f_kb_compose(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)c; (void)this_val; (void)argc; (void)argv;
    kb_reload_compose();
    return JS_UNDEFINED;
}

/* lord.keyboard.list() → [{ id, name, variants: [{ id, name }] }] de xkb-data (evdev.lst) */
static JSValue f_kb_list(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue arr = JS_NewArray(c);
    FILE *f = fopen("/usr/share/X11/xkb/rules/evdev.lst", "r");
    char line[512];
    int section = 0;
    uint32_t n = 0;
    while (f && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '!') { section = strstr(line, "! layout") ? 1 : strstr(line, "! variant") ? 2 : 0; continue; }
        char id[64], *p = line;
        while (*p == ' ') p++;
        int k = 0;
        while (*p && *p != ' ' && k < 63) id[k++] = *p++;
        id[k] = 0;
        while (*p == ' ') p++;
        if (!id[0]) continue;
        if (section == 1) {
            JSValue o = JS_NewObject(c);
            JS_SetPropertyStr(c, o, "id", JS_NewString(c, id));
            JS_SetPropertyStr(c, o, "name", JS_NewString(c, p));
            JS_SetPropertyStr(c, o, "variants", JS_NewArray(c));
            JS_SetPropertyUint32(c, arr, n++, o);
        } else if (section == 2) {
            /* "  abnt2           br: Portuguese (Brazil, ABNT2)" */
            char *colon = strchr(p, ':');
            if (!colon) continue;
            *colon = 0;
            for (uint32_t i = 0; i < n; i++) {
                JSValue o = JS_GetPropertyUint32(c, arr, i);
                JSValue lid = JS_GetPropertyStr(c, o, "id");
                const char *s = JS_ToCString(c, lid);
                if (s && !strcmp(s, p)) {
                    JSValue vs = JS_GetPropertyStr(c, o, "variants");
                    JSValue v = JS_NewObject(c);
                    JS_SetPropertyStr(c, v, "id", JS_NewString(c, id));
                    JS_SetPropertyStr(c, v, "name", JS_NewString(c, colon + 2));
                    int64_t len = 0;
                    JSValue lv = JS_GetPropertyStr(c, vs, "length");
                    JS_ToInt64(c, &len, lv);
                    JS_FreeValue(c, lv);
                    JS_SetPropertyUint32(c, vs, (uint32_t)len, v);
                    JS_FreeValue(c, vs);
                }
                if (s) JS_FreeCString(c, s);
                JS_FreeValue(c, lid);
                JS_FreeValue(c, o);
                if (s && !strcmp(s, p)) break;
            }
        }
    }
    if (f) fclose(f);
    return arr;
}

/* ---- lord.time: zona horaria ---- */

static JSValue f_time_zone(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    char link[PATH_MAX];
    ssize_t n = readlink("/etc/localtime", link, sizeof link - 1);
    link[n > 0 ? n : 0] = 0;
    const char *z = strstr(link, "zoneinfo/");
    return JS_NewString(c, z ? z + 9 : "UTC");
}

/* lord.time.zones() → ["America/Argentina/Buenos_Aires", …] (zone1970.tab de tzdata) */
static JSValue f_time_zones(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue arr = JS_NewArray(c);
    FILE *f = fopen("/usr/share/zoneinfo/zone1970.tab", "r");
    char line[512];
    uint32_t i = 0;
    JS_SetPropertyUint32(c, arr, i++, JS_NewString(c, "UTC"));
    while (f && fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char cc[64], coord[64], zone[128];
        if (sscanf(line, "%63s %63s %127s", cc, coord, zone) == 3) JS_SetPropertyUint32(c, arr, i++, JS_NewString(c, zone));
    }
    if (f) fclose(f);
    return arr;
}

/* Después de cambiar la zona: que localtime() (y Date de JS) la tomen. */
static JSValue f_time_reload(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)c; (void)this_val; (void)argc; (void)argv;
    unsetenv("TZ");
    tzset();
    return JS_UNDEFINED;
}

/* ---- varios ---- */

static JSValue f_quit(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)c; (void)this_val; (void)argc; (void)argv;
    ui.quit_requested = 1;
    return JS_UNDEFINED;
}

static JSValue f_measure_text(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *s = JS_ToCString(c, argv[0]);
    double size = 14, weight = 400;
    JS_ToFloat64(c, &size, argv[1]);
    JS_ToFloat64(c, &weight, argv[2]);
    float w = 0, h = 0;
    if (s) { text_measure(s, (float)weight, (float)size, 0, 0, 1, &w, &h); JS_FreeCString(c, s); }
    return JS_NewFloat64(c, w);
}

/* ---- lord.text: idioma (fuente CJK), dirección de la interfaz y de un texto ---- */

static JSValue f_text_language(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *l = JS_ToCString(c, argv[0]);
    if (!l) return JS_EXCEPTION;
    int ok = strlen(l) < 16;
    for (const char *p = l; ok && *p; p++) ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '-' || *p == '_';
    if (ok) { text_set_language(l); ui.dirty = 1; }
    JS_FreeCString(c, l);
    return JS_NewBool(c, ok);
}

static JSValue f_text_set_direction(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    int rtl = JS_ToBool(c, argv[0]);
    if (ui.rtl != rtl) { ui.rtl = (uint8_t)rtl; ui.dirty = 1; }
    return JS_UNDEFINED;
}

/* 'rtl' si el primer carácter fuerte es de derecha a izquierda, si no 'ltr'. */
static JSValue f_text_direction(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *s = JS_ToCString(c, argv[0]);
    if (!s) return JS_EXCEPTION;
    int rtl = text_direction(s);
    JS_FreeCString(c, s);
    return JS_NewString(c, rtl ? "rtl" : "ltr");
}

/* lord.image.size(ruta) → { width, height } de un PNG/JPEG de la carpeta personal (o null).
 * Lo usa el markdown de las notas para no agrandar imágenes chicas. */
static JSValue f_image_size(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    if (!p) return JS_EXCEPTION;
    char path[PATH_MAX];
    int bad = resolve_in_home(p, path);
    JS_FreeCString(c, p);
    Image *img = bad ? NULL : image_get(path);
    if (!img) return JS_NULL;
    int w = 0, h = 0;
    image_size(img, &w, &h);
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "width", JS_NewInt32(c, w));
    JS_SetPropertyStr(c, o, "height", JS_NewInt32(c, h));
    return o;
}

static const JSCFunctionListEntry image_funcs[] = {
    JS_CFUNC_DEF("size", 1, f_image_size),
};

static const JSCFunctionListEntry text_funcs[] = {
    JS_CFUNC_DEF("setLanguage", 1, f_text_language),
    JS_CFUNC_DEF("setDirection", 1, f_text_set_direction),
    JS_CFUNC_DEF("direction", 1, f_text_direction),
};

/* ---- lord.fs: escritura (ver las reglas al comienzo del archivo) ---- */

#define FS_WRITE_MAX (32 * 1024 * 1024)

/* Ruta de destino dentro de la carpeta personal, aunque el archivo todavía no exista: se
 * resuelve la carpeta padre (realpath) y el último componente tiene que ser un nombre común.
 * Rechaza las carpetas protegidas. out = carpeta real + "/" + nombre. */
static int resolve_target(const char *path, char *out, size_t n, char *dir, size_t dn) {
    char full[PATH_MAX * 2];
    if (path[0] == '/') snprintf(full, sizeof full, "%s", path);
    else snprintf(full, sizeof full, "%s/%s", home, path);
    size_t len = strlen(full);
    while (len > 1 && full[len - 1] == '/') full[--len] = 0;
    char *slash = strrchr(full, '/');
    if (!slash) return -1;
    const char *base = slash + 1;
    if (!base[0] || !strcmp(base, ".") || !strcmp(base, "..") || strlen(base) > 255) return -1;
    *slash = 0;
    char rdir[PATH_MAX];
    if (!realpath(full[0] ? full : "/", rdir)) return -1;
    if (!fsops_allowed(rdir)) return -1;
    if ((size_t)snprintf(out, n, "%s/%s", rdir, base) >= n) return -1;
    if (dir && (size_t)snprintf(dir, dn, "%s", rdir) >= dn) return -1;
    if (fsops_protected(out)) return -2;
    return 0;
}

int js_sys_resolve_target(const char *path, char *out, size_t n) { return resolve_target(path, out, n, NULL, 0); }

static JSValue fs_path_error(JSContext *c, int r) {
    return r == -2 ? JS_ThrowRangeError(c, "esa carpeta está protegida") : JS_ThrowRangeError(c, "ruta fuera de la carpeta personal o inválida");
}

/* lord.fs.write(ruta, texto | ArrayBuffer): crea o reemplaza el archivo de forma atómica. */
static JSValue f_fs_write(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    if (!p) return JS_EXCEPTION;
    char target[PATH_MAX + 300], dir[PATH_MAX];
    int r = resolve_target(p, target, sizeof target, dir, sizeof dir);
    JS_FreeCString(c, p);
    if (r) return fs_path_error(c, r);
    size_t len = 0;
    const char *text = NULL;
    uint8_t *bin = JS_IsString(argv[1]) ? NULL : JS_GetArrayBuffer(c, &len, argv[1]);
    if (!bin) {
        JS_FreeValue(c, JS_GetException(c));
        text = JS_ToCStringLen(c, &len, argv[1]);
        if (!text) return JS_EXCEPTION;
    }
    const uint8_t *data = bin ? bin : (const uint8_t *)text;
    if (len > FS_WRITE_MAX) { if (text) JS_FreeCString(c, text); return JS_ThrowRangeError(c, "archivo demasiado grande (máx. 32 MB)"); }
    struct stat st;
    mode_t mode = 0644;
    if (!lstat(target, &st)) {
        if (!S_ISREG(st.st_mode)) { if (text) JS_FreeCString(c, text); return JS_ThrowTypeError(c, "no es un archivo común (¿un enlace?)"); }
        mode = st.st_mode & 07777;
    }
    char tmp[PATH_MAX + 64];
    snprintf(tmp, sizeof tmp, "%s/.lordos-write-XXXXXX", dir);
    int fd = mkstemp(tmp);
    int ok = fd >= 0;
    if (ok) {
        fchmod(fd, mode);
        for (size_t off = 0; ok && off < len;) {
            ssize_t w = write(fd, data + off, len - off);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) ok = 0; else off += (size_t)w;
        }
        if (ok && fsync(fd)) ok = 0;
        if (close(fd)) ok = 0;
        if (ok && rename(tmp, target)) ok = 0;
        if (!ok) unlink(tmp);
    }
    int err = errno;
    if (text) JS_FreeCString(c, text);
    if (!ok) return JS_ThrowInternalError(c, "%s", strerror(err));
    return JS_TRUE;
}

/* lord.fs.mkdir(ruta): una carpeta nueva (la de arriba tiene que existir). */
static JSValue f_fs_mkdir(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    if (!p) return JS_EXCEPTION;
    char target[PATH_MAX + 300];
    int r = resolve_target(p, target, sizeof target, NULL, 0);
    JS_FreeCString(c, p);
    if (r) return fs_path_error(c, r);
    if (mkdir(target, 0755) && errno != EEXIST) return JS_ThrowInternalError(c, "%s", strerror(errno));
    return JS_TRUE;
}

/* lord.fs.rename(de, a): mover o renombrar dentro de la carpeta personal (el enlace mismo,
 * si es un enlace: rename no lo sigue). No pisa un destino existente. */
static JSValue f_fs_rename(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *a = JS_ToCString(c, argv[0]), *b = JS_ToCString(c, argv[1]);
    char from[PATH_MAX + 300], to[PATH_MAX + 300];
    int r = !a || !b ? -1 : resolve_target(a, from, sizeof from, NULL, 0);
    if (!r) r = resolve_target(b, to, sizeof to, NULL, 0);
    if (a) JS_FreeCString(c, a);
    if (b) JS_FreeCString(c, b);
    if (r) return fs_path_error(c, r);
    struct stat st;
    if (lstat(from, &st)) return JS_ThrowInternalError(c, "%s", strerror(errno));
    if (!lstat(to, &st)) return JS_ThrowRangeError(c, "ya existe");
    if (rename(from, to)) return JS_ThrowInternalError(c, "%s", strerror(errno));
    return JS_TRUE;
}

/* ---- lord.fs: copiar, mover, Papelera, información y búsquedas (fsops.c) ---- */

/* lord.fs.copy(de, a, mover) → id de la tarea (en un hilo; progreso con lord.fs.jobs()). */
static JSValue f_fs_copy(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    const char *a = JS_ToCString(c, argv[0]), *b = JS_ToCString(c, argv[1]);
    int move = argc > 2 && JS_ToBool(c, argv[2]);
    char from[PATH_MAX + 300], to[PATH_MAX + 300], err[256] = "";
    /* el origen se resuelve como destino: el enlace mismo, no a lo que apunta */
    int r = !a || !b ? -1 : resolve_target(a, from, sizeof from, NULL, 0);
    if (r == -2 && !move) r = 0;                          /* copiar DESDE una carpeta protegida sí */
    if (!r) r = resolve_target(b, to, sizeof to, NULL, 0);
    if (a) JS_FreeCString(c, a);
    if (b) JS_FreeCString(c, b);
    if (r) return fs_path_error(c, r);
    int id = fsjob_start(from, to, move, err, sizeof err);
    if (id < 0) return JS_ThrowInternalError(c, "%s", err);
    return JS_NewInt32(c, id);
}

/* lord.fs.jobs() → [{ id, src, dst, move, done, failed, error, bytes, total, files }] */
static JSValue f_fs_jobs(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue arr = JS_NewArray(c);
    uint32_t n = 0;
    for (int i = 0; i < FSJOB_MAX; i++) {
        const FsJob *j = fsjob_get(i);
        if (!j) continue;
        JSValue o = JS_NewObject(c);
        int done = __atomic_load_n(&j->done, __ATOMIC_ACQUIRE);
        JS_SetPropertyStr(c, o, "id", JS_NewInt32(c, j->id));
        JS_SetPropertyStr(c, o, "src", JS_NewString(c, j->src));
        JS_SetPropertyStr(c, o, "dst", JS_NewString(c, j->dst));
        JS_SetPropertyStr(c, o, "move", JS_NewBool(c, j->move));
        JS_SetPropertyStr(c, o, "done", JS_NewBool(c, done));
        JS_SetPropertyStr(c, o, "failed", JS_NewBool(c, done && j->failed));
        JS_SetPropertyStr(c, o, "error", JS_NewString(c, done ? j->error : ""));
        JS_SetPropertyStr(c, o, "bytes", JS_NewFloat64(c, (double)j->bytes));
        JS_SetPropertyStr(c, o, "total", JS_NewFloat64(c, (double)j->total));
        JS_SetPropertyStr(c, o, "files", JS_NewFloat64(c, (double)j->files));
        JS_SetPropertyUint32(c, arr, n++, o);
    }
    return arr;
}

static JSValue f_fs_job_cancel(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    int32_t id = 0;
    JS_ToInt32(c, &id, argv[0]);
    fsjob_cancel(id);
    return JS_UNDEFINED;
}

static JSValue f_fs_job_forget(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    int32_t id = 0;
    JS_ToInt32(c, &id, argv[0]);
    fsjob_forget(id);
    return JS_UNDEFINED;
}

/* lord.fs.trash(ruta): a la Papelera de freedesktop (la de home o la del disco montado). */
static JSValue f_fs_trash(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    if (!p) return JS_EXCEPTION;
    char target[PATH_MAX + 300], err[256] = "";
    int r = resolve_target(p, target, sizeof target, NULL, 0);
    JS_FreeCString(c, p);
    if (r) return fs_path_error(c, r);
    if (fstrash_put(target, err, sizeof err)) return JS_ThrowInternalError(c, "%s", err);
    return JS_TRUE;
}

typedef struct { JSContext *c; JSValue arr; uint32_t n; } Acc;

static void trash_cb(void *ud, const char *id, const char *name, const char *orig, const char *date, int dir, double size) {
    Acc *a = ud;
    JSValue o = JS_NewObject(a->c);
    JS_SetPropertyStr(a->c, o, "id", JS_NewString(a->c, id));
    JS_SetPropertyStr(a->c, o, "name", JS_NewString(a->c, name));
    JS_SetPropertyStr(a->c, o, "original", JS_NewString(a->c, orig));
    JS_SetPropertyStr(a->c, o, "deleted", JS_NewString(a->c, date));
    JS_SetPropertyStr(a->c, o, "dir", JS_NewBool(a->c, dir));
    JS_SetPropertyStr(a->c, o, "size", JS_NewFloat64(a->c, size));
    JS_SetPropertyUint32(a->c, a->arr, a->n++, o);
}

/* lord.fs.trashList() → [{ id, name, original, deleted, dir, size }] */
static JSValue f_fs_trash_list(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    Acc a = { c, JS_NewArray(c), 0 };
    fstrash_list(trash_cb, &a);
    return a.arr;
}

static JSValue trash_id_op(JSContext *c, JSValueConst arg, int (*op)(const char *, char *, size_t)) {
    const char *id = JS_ToCString(c, arg);
    if (!id) return JS_EXCEPTION;
    char err[256] = "";
    int r = op(id, err, sizeof err);
    JS_FreeCString(c, id);
    if (r) return JS_ThrowInternalError(c, "%s", err);
    return JS_TRUE;
}
static JSValue f_fs_restore(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    return trash_id_op(c, argv[0], fstrash_restore);
}
static JSValue f_fs_trash_remove(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    return trash_id_op(c, argv[0], fstrash_remove);
}
static JSValue f_fs_trash_empty(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(c, fstrash_empty());
}

/* lord.fs.stat(ruta) → { name, path, dir, link, target, size, mode, user, group, mtime, atime, ctime, btime } */
static JSValue f_fs_stat(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    if (!p) return JS_EXCEPTION;
    char target[PATH_MAX + 300];
    int r = resolve_target(p, target, sizeof target, NULL, 0);
    JS_FreeCString(c, p);
    if (r == -1) return fs_path_error(c, r);
    struct statx sx;
    if (statx(AT_FDCWD, target, AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_BTIME, &sx)) return JS_ThrowInternalError(c, "%s", strerror(errno));
    JSValue o = JS_NewObject(c);
    const char *base = strrchr(target, '/');
    JS_SetPropertyStr(c, o, "name", JS_NewString(c, base ? base + 1 : target));
    JS_SetPropertyStr(c, o, "path", JS_NewString(c, target));
    int link = S_ISLNK(sx.stx_mode);
    struct stat st;
    int dir = S_ISDIR(sx.stx_mode) || (link && !stat(target, &st) && S_ISDIR(st.st_mode));
    JS_SetPropertyStr(c, o, "dir", JS_NewBool(c, dir));
    JS_SetPropertyStr(c, o, "link", JS_NewBool(c, link));
    if (link) {
        char t[PATH_MAX];
        ssize_t n = readlink(target, t, sizeof t - 1);
        if (n >= 0) { t[n] = 0; JS_SetPropertyStr(c, o, "target", JS_NewString(c, t)); }
    }
    char mode[8];
    snprintf(mode, sizeof mode, "%04o", sx.stx_mode & 07777);
    JS_SetPropertyStr(c, o, "mode", JS_NewString(c, mode));
    JS_SetPropertyStr(c, o, "size", JS_NewFloat64(c, (double)sx.stx_size));
    struct passwd *pw = getpwuid(sx.stx_uid);
    struct group *gr = getgrgid(sx.stx_gid);
    JS_SetPropertyStr(c, o, "user", JS_NewString(c, pw ? pw->pw_name : "?"));
    JS_SetPropertyStr(c, o, "group", JS_NewString(c, gr ? gr->gr_name : "?"));
#define TS(k, f) JS_SetPropertyStr(c, o, k, JS_NewFloat64(c, (double)sx.f.tv_sec * 1000 + sx.f.tv_nsec / 1e6))
    TS("mtime", stx_mtime);
    TS("atime", stx_atime);
    TS("ctime", stx_ctime);
    if (sx.stx_mask & STATX_BTIME) TS("btime", stx_btime);
#undef TS
    JS_SetPropertyStr(c, o, "writable", JS_NewBool(c, r == 0 && access(target, W_OK) == 0));
    return o;
}

/* lord.fs.du(carpeta, ms) → { size, files, dirs, complete } (corta a los ms, por defecto 800) */
static JSValue f_fs_du(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    const char *p = JS_ToCString(c, argv[0]);
    if (!p) return JS_EXCEPTION;
    double ms = 800;
    if (argc > 1 && JS_IsNumber(argv[1])) JS_ToFloat64(c, &ms, argv[1]);
    if (ms > 3000) ms = 3000;
    char path[PATH_MAX];
    int bad = resolve_in_home(p, path);
    JS_FreeCString(c, p);
    if (bad) return fs_path_error(c, -1);
    FsDu d;
    fs_du(path, ms, &d);
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "size", JS_NewFloat64(c, d.size));
    JS_SetPropertyStr(c, o, "files", JS_NewFloat64(c, (double)d.files));
    JS_SetPropertyStr(c, o, "dirs", JS_NewFloat64(c, (double)d.dirs));
    JS_SetPropertyStr(c, o, "complete", JS_NewBool(c, d.complete));
    return o;
}

/* lord.fs.free(ruta) → { total, free } del disco donde está */
static JSValue f_fs_free(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    if (!p) return JS_EXCEPTION;
    char path[PATH_MAX];
    int bad = resolve_in_home(p, path);
    JS_FreeCString(c, p);
    if (bad) return fs_path_error(c, -1);
    struct statvfs vs;
    if (statvfs(path, &vs)) return JS_ThrowInternalError(c, "%s", strerror(errno));
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "total", JS_NewFloat64(c, (double)vs.f_blocks * vs.f_frsize));
    JS_SetPropertyStr(c, o, "free", JS_NewFloat64(c, (double)vs.f_bavail * vs.f_frsize));
    return o;
}

static void hit_cb(void *ud, const char *path, int dir, double size, double mtime) {
    Acc *a = ud;
    JSValue o = JS_NewObject(a->c);
    const char *base = strrchr(path, '/');
    JS_SetPropertyStr(a->c, o, "path", JS_NewString(a->c, path));
    JS_SetPropertyStr(a->c, o, "name", JS_NewString(a->c, base ? base + 1 : path));
    JS_SetPropertyStr(a->c, o, "dir", JS_NewBool(a->c, dir));
    JS_SetPropertyStr(a->c, o, "size", JS_NewFloat64(a->c, size));
    JS_SetPropertyStr(a->c, o, "mtime", JS_NewFloat64(a->c, mtime));
    JS_SetPropertyUint32(a->c, a->arr, a->n++, o);
}

static int32_t opt_int(JSContext *c, JSValueConst o, const char *k, int32_t def, int32_t max) {
    int32_t v = def;
    if (JS_IsObject(o)) {
        JSValue x = JS_GetPropertyStr(c, o, k);
        if (JS_IsNumber(x)) JS_ToInt32(c, &v, x);
        JS_FreeValue(c, x);
    }
    return v < 1 ? 1 : v > max ? max : v;
}

/* lord.fs.search(carpeta, texto, { limit, depth }) → [{ path, name, dir, size, mtime }] (nombre, sin ocultos) */
static JSValue f_fs_search(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    const char *p = JS_ToCString(c, argv[0]), *q = argc > 1 ? JS_ToCString(c, argv[1]) : NULL;
    char path[PATH_MAX];
    int bad = !p || !q || resolve_in_home(p, path);
    Acc a = { c, JS_NewArray(c), 0 };
    if (!bad) fs_search(path, q, opt_int(c, argc > 2 ? argv[2] : JS_UNDEFINED, "limit", 200, 1000),
                        opt_int(c, argc > 2 ? argv[2] : JS_UNDEFINED, "depth", 8, 16), hit_cb, &a);
    if (p) JS_FreeCString(c, p);
    if (q) JS_FreeCString(c, q);
    if (bad) { JS_FreeValue(c, a.arr); return fs_path_error(c, -1); }
    return a.arr;
}

/* lord.fs.recent(limit, días) → los archivos de la carpeta personal modificados hace poco */
static JSValue f_fs_recent(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    int32_t limit = 60, days = 30;
    if (argc > 0 && JS_IsNumber(argv[0])) JS_ToInt32(c, &limit, argv[0]);
    if (argc > 1 && JS_IsNumber(argv[1])) JS_ToInt32(c, &days, argv[1]);
    if (days < 1) days = 1;
    if (days > 365) days = 365;
    Acc a = { c, JS_NewArray(c), 0 };
    fs_recent(limit, days, hit_cb, &a);
    return a.arr;
}

static void vol_cb(void *ud, const char *path, const char *fstype, double total, double avail) {
    Acc *a = ud;
    JSValue o = JS_NewObject(a->c);
    const char *base = strrchr(path, '/');
    JS_SetPropertyStr(a->c, o, "path", JS_NewString(a->c, path));
    JS_SetPropertyStr(a->c, o, "name", JS_NewString(a->c, base ? base + 1 : path));
    JS_SetPropertyStr(a->c, o, "fs", JS_NewString(a->c, fstype));
    JS_SetPropertyStr(a->c, o, "total", JS_NewFloat64(a->c, total));
    JS_SetPropertyStr(a->c, o, "free", JS_NewFloat64(a->c, avail));
    JS_SetPropertyUint32(a->c, a->arr, a->n++, o);
}

/* lord.fs.volumes() → discos y pendrives montados en /media/$USER o /run/media/$USER */
static JSValue f_fs_volumes(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    Acc a = { c, JS_NewArray(c), 0 };
    fs_volumes(vol_cb, &a);
    return a.arr;
}

static const JSCFunctionListEntry fs_funcs[] = {
    JS_CFUNC_DEF("home", 0, f_fs_home),
    JS_CFUNC_DEF("list", 1, f_fs_list),
    JS_CFUNC_DEF("read", 2, f_fs_read),
    JS_CFUNC_DEF("write", 2, f_fs_write),
    JS_CFUNC_DEF("mkdir", 1, f_fs_mkdir),
    JS_CFUNC_DEF("rename", 2, f_fs_rename),
    JS_CFUNC_DEF("copy", 3, f_fs_copy),
    JS_CFUNC_DEF("jobs", 0, f_fs_jobs),
    JS_CFUNC_DEF("jobCancel", 1, f_fs_job_cancel),
    JS_CFUNC_DEF("jobForget", 1, f_fs_job_forget),
    JS_CFUNC_DEF("trash", 1, f_fs_trash),
    JS_CFUNC_DEF("trashList", 0, f_fs_trash_list),
    JS_CFUNC_DEF("restore", 1, f_fs_restore),
    JS_CFUNC_DEF("removeFromTrash", 1, f_fs_trash_remove),
    JS_CFUNC_DEF("emptyTrash", 0, f_fs_trash_empty),
    JS_CFUNC_DEF("stat", 1, f_fs_stat),
    JS_CFUNC_DEF("du", 2, f_fs_du),
    JS_CFUNC_DEF("free", 1, f_fs_free),
    JS_CFUNC_DEF("search", 3, f_fs_search),
    JS_CFUNC_DEF("recent", 2, f_fs_recent),
    JS_CFUNC_DEF("volumes", 0, f_fs_volumes),
};
static const JSCFunctionListEntry system_funcs[] = {
    JS_CFUNC_DEF("info", 0, f_sys_info),
    JS_CFUNC_DEF("cpuTimes", 0, f_sys_cpu),
};
static const JSCFunctionListEntry settings_funcs[] = {
    JS_CFUNC_DEF("load", 0, f_settings_load),
    JS_CFUNC_DEF("save", 1, f_settings_save),
};
static const JSCFunctionListEntry admin_funcs[] = {
    JS_CFUNC_DEF("available", 0, f_admin_available),
    JS_CFUNC_DEF("request", 2, f_admin_request),
};
static const JSCFunctionListEntry locale_funcs[] = {
    JS_CFUNC_DEF("get", 0, f_locale_get),
    JS_CFUNC_DEF("set", 1, f_locale_set),
    JS_CFUNC_DEF("available", 0, f_locale_available),
    JS_CFUNC_DEF("catalog", 2, f_locale_catalog),
    JS_CFUNC_DEF("languages", 0, f_locale_languages),
    JS_CFUNC_DEF("installPack", 2, f_locale_install),
};
static const JSCFunctionListEntry keyboard_funcs[] = {
    JS_CFUNC_DEF("set", 2, f_kb_set),
    JS_CFUNC_DEF("current", 0, f_kb_current),
    JS_CFUNC_DEF("setGroup", 1, f_kb_group),
    JS_CFUNC_DEF("reloadCompose", 0, f_kb_compose),
    JS_CFUNC_DEF("list", 0, f_kb_list),
};
/* ---- lord.wayland: ventanas de apps Linux (wl/wl.c) ---- */

static JSValue f_wl_focus(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    int32_t id = 0;
    JS_ToInt32(c, &id, argv[0]);
    wl_set_focus(id);
    return JS_UNDEFINED;
}

static JSValue f_wl_close(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    int32_t id = 0;
    JS_ToInt32(c, &id, argv[0]);
    wl_close(id);
    return JS_UNDEFINED;
}

static JSValue f_wl_state(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    int32_t id = 0;
    JS_ToInt32(c, &id, argv[0]);
    wl_set_state(id, argc > 1 && JS_ToBool(c, argv[1]), argc > 2 && JS_ToBool(c, argv[2]));
    return JS_UNDEFINED;
}

static JSValue f_wl_info(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "socket", JS_NewString(c, wl_socket()));
    JS_SetPropertyStr(c, o, "windows", JS_NewInt32(c, wl_count()));
    return o;
}

/* lord.wayland.paste(): Ctrl+V sintético a la app Linux con el teclado (solo a esa app). */
static JSValue f_wl_paste(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewBool(c, wl_send_paste());
}
static JSValue f_wl_has_focus(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewBool(c, wl_has_key_focus());
}

static const JSCFunctionListEntry wayland_funcs[] = {
    JS_CFUNC_DEF("paste", 0, f_wl_paste),
    JS_CFUNC_DEF("hasKeyboard", 0, f_wl_has_focus),
    JS_CFUNC_DEF("focus", 1, f_wl_focus),
    JS_CFUNC_DEF("close", 1, f_wl_close),
    JS_CFUNC_DEF("state", 3, f_wl_state),
    JS_CFUNC_DEF("info", 0, f_wl_info),
};

/* ---- lord.apps: apps Linux instaladas (.desktop, apps.c) ---- */

/* lord.apps.linux(idioma) → [{ id, name, genericName, comment, keywords, categories, wmClass, icon, user }] */
static JSValue f_apps_linux(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    const char *lang = argc > 0 && JS_IsString(argv[0]) ? JS_ToCString(c, argv[0]) : NULL;
    char l[32] = "en";
    if (lang) {
        if (strlen(lang) < sizeof l && valid_id_dot(lang)) snprintf(l, sizeof l, "%s", lang);
        JS_FreeCString(c, lang);
    }
    const DesktopApp *list;
    int n = apps_list(l, &list);
    JSValue arr = JS_NewArray(c);
    for (int i = 0; i < n; i++) {
        const DesktopApp *a = &list[i];
        JSValue o = JS_NewObject(c);
        JS_SetPropertyStr(c, o, "id", JS_NewString(c, a->id));
        JS_SetPropertyStr(c, o, "name", JS_NewString(c, a->name));
        JS_SetPropertyStr(c, o, "genericName", JS_NewString(c, a->generic));
        JS_SetPropertyStr(c, o, "comment", JS_NewString(c, a->comment));
        JS_SetPropertyStr(c, o, "keywords", JS_NewString(c, a->keywords));
        JS_SetPropertyStr(c, o, "categories", JS_NewString(c, a->categories));
        JS_SetPropertyStr(c, o, "wmClass", JS_NewString(c, a->wmclass));
        JS_SetPropertyStr(c, o, "mimeTypes", JS_NewString(c, a->mime));
        JS_SetPropertyStr(c, o, "icon", JS_NewString(c, a->icon));
        JS_SetPropertyStr(c, o, "user", JS_NewBool(c, a->user));
        JS_SetPropertyUint32(c, arr, (uint32_t)i, o);
    }
    return arr;
}

/* lord.apps.launch(id): solo el id de un .desktop de la lista (ver apps.c); sin argumentos. */
static JSValue f_apps_launch(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *id = JS_ToCString(c, argv[0]);
    int ok = id && apps_launch(id) == 0;
    if (id) JS_FreeCString(c, id);
    return JS_NewBool(c, ok);
}

/* ---- lord.apps: paquetes de apps nativas LordOS (.lpk, lpk.c) ---- */

#define LPK_JS_MAX (16 * 1024 * 1024)

/* Un id de paquete no puede pisar el espacio de textos de LordOS (shell, files, terminal…). */
static int lpk_reserved(const char *id) {
    static const char *const fixed[] = { "shell", "apps", "common", "linux", "desktop", "lordos", "sdk", "store", NULL };
    for (int i = 0; fixed[i]; i++) if (!strcmp(id, fixed[i])) return 1;
    char path[PATH_MAX * 2];
    struct stat st;
    snprintf(path, sizeof path, "%s/locales/en/%s.json", assets, id);
    return stat(path, &st) == 0;
}

static int valid_version(const char *v) {
    size_t n = strlen(v);
    if (!n || n > 32 || !isdigit((unsigned char)v[0])) return 0;
    for (; *v; v++) if (!isalnum((unsigned char)*v) && !strchr(".+~-", *v)) return 0;
    return 1;
}

static void copy_str(JSContext *c, JSValueConst v, char *out, size_t n) {
    const char *s = JS_IsString(v) ? JS_ToCString(c, v) : NULL;
    if (s) { snprintf(out, n, "%s", s); JS_FreeCString(c, s); }
}

/* Texto del manifest; con tr, también un objeto por idioma ({ "es": "…", "en": "…" }): vale el
 * inglés o el primero. */
static const char *str_prop_tr(JSContext *c, JSValueConst o, const char *k, char *out, size_t n, int tr) {
    JSValue v = JS_GetPropertyStr(c, o, k);
    out[0] = 0;
    if (JS_IsString(v)) copy_str(c, v, out, n);
    else if (tr && JS_IsObject(v)) {
        JSValue en = JS_GetPropertyStr(c, v, "en");
        copy_str(c, en, out, n);
        JS_FreeValue(c, en);
        JSPropertyEnum *tab = NULL;
        uint32_t len = 0;
        if (!out[0] && !JS_GetOwnPropertyNames(c, &tab, &len, v, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
            for (uint32_t i = 0; i < len && !out[0]; i++) {
                JSValue x = JS_GetProperty(c, v, tab[i].atom);
                copy_str(c, x, out, n);
                JS_FreeValue(c, x);
            }
            for (uint32_t i = 0; i < len; i++) JS_FreeAtom(c, tab[i].atom);
            js_free(c, tab);
        }
    }
    JS_FreeValue(c, v);
    return out[0] ? out : NULL;
}
#define str_prop(c, o, k, out, n) str_prop_tr(c, o, k, out, n, 0)

/* Abre y valida un .lpk de la carpeta personal: devuelve { manifest (objeto), files, size } o
 * lanza un Error con el motivo. Con install = 1, además lo instala. */
static JSValue lpk_check(JSContext *c, JSValueConst arg, int install) {
    const char *p = JS_ToCString(c, arg);
    if (!p) return JS_EXCEPTION;
    char path[PATH_MAX];
    int bad = resolve_in_home(p, path);
    JS_FreeCString(c, p);
    if (bad) return JS_ThrowRangeError(c, "ruta fuera de la carpeta personal o inexistente");
    Lpk pk;
    char err[160];
    if (lpk_open(path, &pk, err, sizeof err)) return JS_ThrowTypeError(c, "%s", err);
    const LpkFile *mf = lpk_find(&pk, "manifest.json");
    if (!mf || !lpk_find(&pk, "app.js")) { lpk_free(&pk); return JS_ThrowTypeError(c, "no es un paquete de LordOS (faltan manifest.json o app.js)"); }
    char *txt = malloc(mf->size + 1);
    if (!txt) { lpk_free(&pk); return JS_ThrowOutOfMemory(c); }
    memcpy(txt, mf->data, mf->size);
    txt[mf->size] = 0;
    JSValue m = JS_ParseJSON(c, txt, mf->size, "manifest.json");
    free(txt);
    if (JS_IsException(m) || !JS_IsObject(m)) {
        JS_FreeValue(c, JS_GetException(c));
        JS_FreeValue(c, m);
        lpk_free(&pk);
        return JS_ThrowTypeError(c, "manifest.json inválido");
    }
    char id[64], ver[48], name[128], icon[224];
    const char *why = NULL;
    if (!str_prop(c, m, "id", id, sizeof id) || !lpk_valid_id(id)) why = "el id de la app es inválido (a-z, 0-9 y -)";
    else if (lpk_reserved(id)) why = "el id de la app está reservado por LordOS";
    else if (!str_prop_tr(c, m, "name", name, sizeof name, 1)) why = "falta el nombre de la app";
    else if (!str_prop(c, m, "version", ver, sizeof ver) || !valid_version(ver)) why = "la versión es inválida";
    else if (!str_prop(c, m, "icon", icon, sizeof icon) || !lpk_find(&pk, icon) || strlen(icon) < 5 || strcmp(icon + strlen(icon) - 4, ".png"))
        why = "la app tiene que traer su ícono propio (un .png del paquete en \"icon\")";
    if (why) { JS_FreeValue(c, m); lpk_free(&pk); return JS_ThrowTypeError(c, "%s", why); }
    /* Firma: verifica hashes y firma; rechaza un paquete alterado o revocado, y al instalar
     * exige la regla de actualización (misma clave que la versión instalada). Ver docs/FIRMA.md. */
    LpkSigInfo si;
    if (sig_verify_package(&pk, id, ver, &si, err, sizeof err)) { JS_FreeValue(c, m); lpk_free(&pk); return JS_ThrowTypeError(c, "el paquete está dañado o mal firmado: %s", err); }
    if (si.revoked) { JS_FreeValue(c, m); lpk_free(&pk); return JS_ThrowTypeError(c, "%s", si.revoked_why); }
    if (install) {
        if (pkg_check_update(id, &si, err, sizeof err)) { JS_FreeValue(c, m); lpk_free(&pk); return JS_ThrowTypeError(c, "%s", err); }
        if (lpk_install(&pk, id, err, sizeof err)) { JS_FreeValue(c, m); lpk_free(&pk); return JS_ThrowInternalError(c, "%s", err); }
        pkg_keep_original(id, path);        /* el .lpk original, para «Restaurar» y detectar cambios */
    }
    size_t total = 0;
    for (int i = 0; i < pk.nfiles; i++) total += pk.files[i].size;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "manifest", m);
    JS_SetPropertyStr(c, o, "files", JS_NewInt32(c, pk.nfiles));
    JS_SetPropertyStr(c, o, "size", JS_NewFloat64(c, (double)total));
    JSValue sg = JS_NewObject(c);
    JS_SetPropertyStr(c, sg, "trust", JS_NewString(c, sig_trust_name(si.trust)));
    JS_SetPropertyStr(c, sg, "signer", JS_NewString(c, si.signer));
    JS_SetPropertyStr(c, sg, "key", JS_NewString(c, si.key));
    JS_SetPropertyStr(c, sg, "signed", JS_NewBool(c, si.has_signature));
    JS_SetPropertyStr(c, o, "signature", sg);
    lpk_free(&pk);
    return o;
}

/* lord.apps.inspectPackage(ruta) / installPackage(ruta): ver lpk_check. */
static JSValue f_apps_inspect(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    return lpk_check(c, argv[0], 0);
}

static JSValue f_apps_install(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    return lpk_check(c, argv[0], 1);
}

static JSValue f_apps_remove(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *id = JS_ToCString(c, argv[0]);
    int ok = id && lpk_remove(id) == 0;
    if (ok) { pkg_forget(id); perm_forget(id); }       /* el original guardado y los permisos */
    if (id) JS_FreeCString(c, id);
    return JS_NewBool(c, ok);
}

/* lord.apps.packages() → [{ id, dir, manifest (texto) }] de las apps instaladas. */
static JSValue f_apps_packages(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue arr = JS_NewArray(c);
    const char *root = lpk_root();
    DIR *d = root ? opendir(root) : NULL;
    uint32_t n = 0;
    struct dirent *e;
    while (d && (e = readdir(d)) && n < 500) {
        if (!lpk_valid_id(e->d_name)) continue;
        char path[PATH_MAX * 2];
        snprintf(path, sizeof path, "%s/%s/manifest.json", root, e->d_name);
        size_t len;
        char *txt = read_small(path, 256 * 1024, &len);
        if (!txt) continue;
        JSValue o = JS_NewObject(c);
        snprintf(path, sizeof path, "%s/%s", root, e->d_name);
        JS_SetPropertyStr(c, o, "id", JS_NewString(c, e->d_name));
        JS_SetPropertyStr(c, o, "dir", JS_NewString(c, path));
        JS_SetPropertyStr(c, o, "manifest", JS_NewStringLen(c, txt, len));
        free(txt);
        JS_SetPropertyUint32(c, arr, n++, o);
    }
    if (d) closedir(d);
    return arr;
}

/* Archivo de una app instalada (rel validado, sin salir de su carpeta). */
static char *lpk_read(const char *id, const char *rel, size_t max, size_t *len) {
    const char *root = lpk_root();
    if (!root || !lpk_valid_id(id) || !lpk_valid_path(rel)) return NULL;
    char path[PATH_MAX * 2], real[PATH_MAX], base[PATH_MAX * 2];
    snprintf(base, sizeof base, "%s/%s/", root, id);
    snprintf(path, sizeof path, "%s%s", base, rel);
    struct stat st;
    if (!realpath(path, real) || strncmp(real, base, strlen(base)) || stat(real, &st) || !S_ISREG(st.st_mode) || (size_t)st.st_size > max) return NULL;
    return read_small(real, max, len);
}

/* lord.apps.loadPackage(id): evalúa su app.js (que deja globalThis.__lordosApp) y devuelve
 * ese objeto { default, Icon }. Corre en el mismo contexto que el tema (ver APPS-NATIVAS.md). */
static JSValue f_apps_load(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *id = JS_ToCString(c, argv[0]);
    if (!id) return JS_EXCEPTION;
    size_t len;
    char *src = lpk_read(id, "app.js", LPK_JS_MAX, &len);
    char name[96];
    snprintf(name, sizeof name, "lpk:%s/app.js", id);
    JS_FreeCString(c, id);
    if (!src) return JS_ThrowTypeError(c, "la app no está instalada");
    JSValue r = JS_Eval(c, src, len, name, JS_EVAL_TYPE_GLOBAL);
    free(src);
    if (JS_IsException(r)) return r;
    JS_FreeValue(c, r);
    JSValue g = JS_GetGlobalObject(c);
    JSValue app = JS_GetPropertyStr(c, g, "__lordosApp");
    JSAtom a = JS_NewAtom(c, "__lordosApp");
    JS_DeleteProperty(c, g, a, 0);
    JS_FreeAtom(c, a);
    JS_FreeValue(c, g);
    return app;
}

/* lord.apps.packageFile(id, ruta) → ArrayBuffer (hasta 32 MB): los .wasm y datos de la app. */
static JSValue f_apps_file(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *id = JS_ToCString(c, argv[0]), *rel = JS_ToCString(c, argv[1]);
    size_t len = 0;
    char *buf = id && rel ? lpk_read(id, rel, 32 * 1024 * 1024, &len) : NULL;
    if (id) JS_FreeCString(c, id);
    if (rel) JS_FreeCString(c, rel);
    if (!buf) return JS_ThrowTypeError(c, "archivo inexistente en la app");
    JSValue v = JS_NewArrayBufferCopy(c, (uint8_t *)buf, len);
    free(buf);
    return v;
}

/* lord.apps.launchWith(id, ruta): abre un archivo con una app Linux que lo declare (MimeType). */
static JSValue f_apps_launch_with(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *id = JS_ToCString(c, argv[0]), *p = JS_ToCString(c, argv[1]);
    char path[PATH_MAX];
    int ok = id && p && !resolve_in_home(p, path) && apps_launch_with(id, path) == 0;
    if (id) JS_FreeCString(c, id);
    if (p) JS_FreeCString(c, p);
    return JS_NewBool(c, ok);
}

/* ---- lord.remote: estado de las sesiones y del escritorio remoto (lord-sessiond; solo lectura) ---- */

static JSValue text_or_null(JSContext *c, const char *path, size_t max) {
    size_t len;
    char *t = read_small(path, max, &len);
    if (!t) return JS_NULL;
    while (len && (t[len - 1] == '\n' || t[len - 1] == '\r')) t[--len] = 0;
    JSValue v = JS_NewStringLen(c, t, len);
    free(t);
    return v;
}

/* lord.remote.status() → { config (texto), fingerprint, status (JSON de lord-sessiond o null),
 *                          installed (corre lord-sessiond) } */
static JSValue f_remote_status(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "config", text_or_null(c, "/etc/lordos/remote/config", 4096));
    JS_SetPropertyStr(c, o, "fingerprint", text_or_null(c, "/etc/lordos/remote/fingerprint", 256));
    JS_SetPropertyStr(c, o, "status", text_or_null(c, "/run/lordos/status.json", 65536));
    struct stat st;
    int installed = !stat("/run/lordos/status.json", &st) && st.st_uid == 0;
    JS_SetPropertyStr(c, o, "installed", JS_NewBool(c, installed));
    return o;
}

static const JSCFunctionListEntry remote_funcs[] = {
    JS_CFUNC_DEF("status", 0, f_remote_status),
};

/* ---- lord.users: cuentas de personas del equipo (Ajustes → Usuarios; solo lectura) ---- */

static int user_in_group(const struct passwd *pw, const char *const *names) {
    gid_t groups[256];
    int n = 256;
    if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &n) < 0) return 0;
    for (int k = 0; names[k]; k++) {
        struct group *g = getgrnam(names[k]);
        if (g) for (int i = 0; i < n; i++) if (groups[i] == g->gr_gid) return 1;
    }
    return 0;
}

/* lord.users.list() → { users: [{ name, full, uid, admin, color, image, self }], autologin } */
static JSValue f_users_list(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    static const char *const admin_groups[] = { "sudo", "admin", "wheel", NULL };
    JSValue o = JS_NewObject(c), arr = JS_NewArray(c);
    uint32_t n = 0;
    uid_t me = getuid();
    setpwent();
    struct passwd *pw;
    while ((pw = getpwent()) && n < 256) {
        if (pw->pw_uid < 1000 || pw->pw_uid >= 60000 || !pw->pw_shell || strstr(pw->pw_shell, "nologin") || strstr(pw->pw_shell, "/false")) continue;
        JSValue u = JS_NewObject(c);
        char full[128];
        snprintf(full, sizeof full, "%s", pw->pw_gecos ? pw->pw_gecos : "");
        char *comma = strchr(full, ',');
        if (comma) *comma = 0;
        JS_SetPropertyStr(c, u, "name", JS_NewString(c, pw->pw_name));
        JS_SetPropertyStr(c, u, "full", JS_NewString(c, full));
        JS_SetPropertyStr(c, u, "uid", JS_NewInt32(c, (int32_t)pw->pw_uid));
        JS_SetPropertyStr(c, u, "admin", JS_NewBool(c, user_in_group(pw, admin_groups)));
        JS_SetPropertyStr(c, u, "self", JS_NewBool(c, pw->pw_uid == me));
        char p[160], color[16] = "";
        snprintf(p, sizeof p, "/var/lib/lordos/users/%s.json", pw->pw_name);
        size_t len;
        char *j = read_small(p, 512, &len);
        const char *cp = j ? strstr(j, "\"color\":\"#") : NULL;
        if (cp && strspn(cp + 10, "0123456789abcdefABCDEF") >= 6) snprintf(color, sizeof color, "%.7s", cp + 9);
        free(j);
        JS_SetPropertyStr(c, u, "color", JS_NewString(c, color));
        snprintf(p, sizeof p, "/var/lib/lordos/users/%s.png", pw->pw_name);
        struct stat st;
        JS_SetPropertyStr(c, u, "image", !lstat(p, &st) && S_ISREG(st.st_mode) ? JS_NewString(c, p) : JS_NULL);
        JS_SetPropertyUint32(c, arr, n++, u);
    }
    endpwent();
    JS_SetPropertyStr(c, o, "users", arr);
    char au[40] = "";
    size_t len;
    char *conf = read_small("/etc/lordos/session.conf", 4096, &len);
    const char *a = conf ? strstr(conf, "AUTOLOGIN=") : NULL;
    if (a) { a += 10; size_t k = strspn(a, "abcdefghijklmnopqrstuvwxyz0123456789_-"); if (k < sizeof au) { memcpy(au, a, k); au[k] = 0; } }
    free(conf);
    JS_SetPropertyStr(c, o, "autologin", JS_NewString(c, au));
    return o;
}

static const JSCFunctionListEntry users_funcs[] = {
    JS_CFUNC_DEF("list", 0, f_users_list),
};

/* ---- lord.session: bloquear y cambiar de usuario (lord-sessiond, docs/SESIONES.md) ---- */

static int greeter_mode;
void js_sys_set_greeter(int on) { greeter_mode = on; }

/* ¿la sesión la maneja lord-sessiond (hay pantalla de ingreso a la que volver)? */
static JSValue f_session_managed(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewBool(c, getenv("LORDOS_SESSIOND") != NULL);
}

static JSValue f_session_lock(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    mirror_session("{\"a\":\"lock\"}");
    return JS_NewBool(c, mirror_clients() > 0);
}

static JSValue f_session_switch(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    mirror_session("{\"a\":\"switch\"}");
    return JS_NewBool(c, mirror_has_seat());
}

static const JSCFunctionListEntry session_funcs[] = {
    JS_CFUNC_DEF("managed", 0, f_session_managed),
    JS_CFUNC_DEF("lock", 0, f_session_lock),
    JS_CFUNC_DEF("switchUser", 0, f_session_switch),
};

/* ---- lord.greeter (solo el tema de la pantalla de ingreso): pedidos a lord-sessiond ----
 * La contraseña viaja de acá al proceso root, que la verifica; el JS nunca ve /etc/shadow. */
static JSValue f_greeter_send(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    size_t len;
    const char *j = argc ? JS_ToCStringLen(c, &len, argv[0]) : NULL;
    if (!j) return JS_EXCEPTION;
    int ok = len > 1 && len < 4096 && j[0] == '{' && !memchr(j, 0, len);
    if (ok) mirror_greeter(j);
    JS_FreeCString(c, j);
    return JS_NewBool(c, ok && mirror_has_seat());
}

static const JSCFunctionListEntry greeter_funcs[] = {
    JS_CFUNC_DEF("send", 1, f_greeter_send),
};

/* lord.apps.launchPackage(id): lanza una app .lpk como su propio proceso aislado (app_spawn,
 * la tercera excepción acotada; ver AGENTS.md y docs/PRIVACIDAD.md). */
static JSValue f_apps_launch_package(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *id = JS_ToCString(c, argv[0]);
    int ok = id && app_spawn(id) == 0;
    if (id) JS_FreeCString(c, id);
    return JS_NewBool(c, ok);
}

static const JSCFunctionListEntry apps_funcs[] = {
    JS_CFUNC_DEF("linux", 1, f_apps_linux),
    JS_CFUNC_DEF("launch", 1, f_apps_launch),
    JS_CFUNC_DEF("launchWith", 2, f_apps_launch_with),
    JS_CFUNC_DEF("packages", 0, f_apps_packages),
    JS_CFUNC_DEF("inspectPackage", 1, f_apps_inspect),
    JS_CFUNC_DEF("installPackage", 1, f_apps_install),
    JS_CFUNC_DEF("removePackage", 1, f_apps_remove),
    JS_CFUNC_DEF("loadPackage", 1, f_apps_load),
    JS_CFUNC_DEF("packageFile", 2, f_apps_file),
    JS_CFUNC_DEF("launchPackage", 1, f_apps_launch_package),
};

static const JSCFunctionListEntry ime_funcs[] = {
    JS_CFUNC_DEF("set", 1, f_ime_set),
    JS_CFUNC_DEF("setContext", 1, f_ime_context),
    JS_CFUNC_DEF("reset", 0, f_ime_reset),
    JS_CFUNC_DEF("info", 0, f_ime_info),
};
static const JSCFunctionListEntry time_funcs[] = {
    JS_CFUNC_DEF("zone", 0, f_time_zone),
    JS_CFUNC_DEF("zones", 0, f_time_zones),
    JS_CFUNC_DEF("reload", 0, f_time_reload),
};
static const JSCFunctionListEntry fonts_funcs[] = {
    JS_CFUNC_DEF("list", 1, f_fonts_list),
};
static const JSCFunctionListEntry themes_funcs[] = {
    JS_CFUNC_DEF("list", 0, f_themes_list),
    JS_CFUNC_DEF("use", 1, f_themes_use),
};

static void add_object(JSContext *c, JSValue parent, const char *name, const JSCFunctionListEntry *f, int n) {
    JSValue o = JS_NewObject(c);
    JS_SetPropertyFunctionList(c, o, f, n);
    JS_SetPropertyStr(c, parent, name, o);
}

void js_sys_register_app(JSContext *c, JSValue lord) {
    add_object(c, lord, "text", text_funcs, sizeof text_funcs / sizeof text_funcs[0]);
    add_object(c, lord, "image", image_funcs, sizeof image_funcs / sizeof image_funcs[0]);
}

void js_sys_register(JSContext *c, JSValue lord) {
    add_object(c, lord, "fs", fs_funcs, sizeof fs_funcs / sizeof fs_funcs[0]);
    add_object(c, lord, "system", system_funcs, sizeof system_funcs / sizeof system_funcs[0]);
    add_object(c, lord, "settings", settings_funcs, sizeof settings_funcs / sizeof settings_funcs[0]);
    add_object(c, lord, "themes", themes_funcs, sizeof themes_funcs / sizeof themes_funcs[0]);
    add_object(c, lord, "fonts", fonts_funcs, sizeof fonts_funcs / sizeof fonts_funcs[0]);
    add_object(c, lord, "admin", admin_funcs, sizeof admin_funcs / sizeof admin_funcs[0]);
    add_object(c, lord, "locale", locale_funcs, sizeof locale_funcs / sizeof locale_funcs[0]);
    add_object(c, lord, "time", time_funcs, sizeof time_funcs / sizeof time_funcs[0]);
    add_object(c, lord, "keyboard", keyboard_funcs, sizeof keyboard_funcs / sizeof keyboard_funcs[0]);
    add_object(c, lord, "text", text_funcs, sizeof text_funcs / sizeof text_funcs[0]);
    add_object(c, lord, "image", image_funcs, sizeof image_funcs / sizeof image_funcs[0]);
    add_object(c, lord, "ime", ime_funcs, sizeof ime_funcs / sizeof ime_funcs[0]);
    add_object(c, lord, "wayland", wayland_funcs, sizeof wayland_funcs / sizeof wayland_funcs[0]);
    add_object(c, lord, "apps", apps_funcs, sizeof apps_funcs / sizeof apps_funcs[0]);
    add_object(c, lord, "remote", remote_funcs, sizeof remote_funcs / sizeof remote_funcs[0]);
    add_object(c, lord, "session", session_funcs, sizeof session_funcs / sizeof session_funcs[0]);
    add_object(c, lord, "users", users_funcs, sizeof users_funcs / sizeof users_funcs[0]);
    if (greeter_mode) add_object(c, lord, "greeter", greeter_funcs, sizeof greeter_funcs / sizeof greeter_funcs[0]);
    JS_SetPropertyStr(c, lord, "quit", JS_NewCFunction(c, f_quit, "quit", 0));
    JS_SetPropertyStr(c, lord, "measureText", JS_NewCFunction(c, f_measure_text, "measureText", 3));
}

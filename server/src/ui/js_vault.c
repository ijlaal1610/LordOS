/* js_vault.c — lord.vault: Contraseñas y el llavero por el ayudante encerrado lord-vault (ver
 * src/vault/vclient.c y docs/CONTRASENAS.md).
 *
 *   lord.vault.available()                     → bool
 *   lord.vault.request(json, { files })        → id; files: [{ path, mode: 'r' | 'w' }]
 *       'r' = archivo existente de la carpeta personal o un disco del usuario (realpath + raíces);
 *       'w' = archivo NUEVO (O_EXCL) en un destino válido de lord.fs. lord-server los abre y le
 *       pasa solo los fds a lord-vault, que no puede abrir rutas.
 *   lord.vault.poll()                          → texto JSON [{ id, kind: 'rep'|'evt', data }] o null
 *   lord.vault.pending()                       → pedidos sin respuesta
 *   lord.vault.lock(why)                       → cerrar la bóveda (pantalla, cambio de usuario)
 *   lord.vault.fidoDevices()                   → [{ path, name, vid, pid }] (sin abrirlas)
 *   lord.vault.fidoAttach(app)                 → { ok, name } — solo con el permiso usb de la app:
 *       lord-server abre /dev/hidrawN y le pasa el fd a lord-vault; la app nunca lo ve.
 *   lord.vault.promptDone(prompt, ok)          → respuesta al aviso de desbloqueo de Secret Service
 * Todo validado acá; nada de esto ejecuta programas ni abre rutas del llamador sin validarlas. */
#include "js_vault.h"
#include "js_sys.h"
#include "../fsops.h"
#include "../sec/perms.h"
#include "../vault/vclient.h"
#include "../vault/fidodev.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int open_read(const char *path) {
    char full[PATH_MAX * 2], real[PATH_MAX];
    const char *home = getenv("HOME");
    if (!path || !path[0]) return -1;
    if (path[0] == '/') snprintf(full, sizeof full, "%s", path);
    else if (home) snprintf(full, sizeof full, "%s/%s", home, path);
    else return -1;
    if (!realpath(full, real) || !fsops_allowed(real)) return -1;
    int fd = open(real, O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    struct stat st;
    if (fd < 0) return -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); return -1; }
    fcntl(fd, F_SETFL, 0);
    return fd;
}

static int open_write(const char *path) {
    char target[PATH_MAX + 300];
    if (!path || js_sys_resolve_target(path, target, sizeof target)) return -1;
    return open(target, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
}

static JSValue f_available(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return JS_NewBool(c, vc_available());
}

static JSValue f_request(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    if (argc < 1 || !JS_IsString(argv[0])) return JS_ThrowTypeError(c, "vault.request(json)");
    size_t len;
    const char *j = JS_ToCStringLen(c, &len, argv[0]);
    if (!j) return JS_EXCEPTION;
    if (len > (32u << 20)) { JS_FreeCString(c, j); return JS_ThrowRangeError(c, "pedido muy grande"); }
    int fds[4], nf = 0;
    if (argc > 1 && JS_IsObject(argv[1])) {
        JSValue files = JS_GetPropertyStr(c, argv[1], "files");
        if (JS_IsArray(c, files)) {
            JSValue lv = JS_GetPropertyStr(c, files, "length");
            int32_t n = 0;
            JS_ToInt32(c, &n, lv);
            JS_FreeValue(c, lv);
            for (int i = 0; i < n && i < 4; i++) {
                JSValue f = JS_GetPropertyUint32(c, files, (uint32_t)i);
                JSValue pv = JS_GetPropertyStr(c, f, "path"), mv = JS_GetPropertyStr(c, f, "mode");
                const char *p = JS_IsString(pv) ? JS_ToCString(c, pv) : NULL;
                const char *m = JS_IsString(mv) ? JS_ToCString(c, mv) : NULL;
                int fd = !p ? -1 : (m && !strcmp(m, "w")) ? open_write(p) : open_read(p);
                int err = errno;
                if (p) JS_FreeCString(c, p);
                if (m) JS_FreeCString(c, m);
                JS_FreeValue(c, pv); JS_FreeValue(c, mv); JS_FreeValue(c, f);
                if (fd < 0) {
                    for (int k = 0; k < nf; k++) close(fds[k]);
                    JS_FreeValue(c, files);
                    JS_FreeCString(c, j);
                    return JS_ThrowRangeError(c, err == EEXIST ? "ya existe un archivo con ese nombre" : "archivo inválido o fuera de la carpeta personal");
                }
                fds[nf++] = fd;
            }
        }
        JS_FreeValue(c, files);
    }
    int id = vc_request(j, len, fds, nf);
    JS_FreeCString(c, j);
    if (id < 0) return JS_ThrowInternalError(c, "lord-vault no está disponible");
    return JS_NewInt32(c, id);
}

static JSValue f_poll(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    char *s = vc_poll();
    if (!s) return JS_NULL;
    JSValue r = JS_NewString(c, s);
    vc_poll_free(s);
    return r;
}

static JSValue f_pending(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return JS_NewInt32(c, vc_pending());
}

static JSValue f_lock(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    const char *w = argc > 0 && JS_IsString(argv[0]) ? JS_ToCString(c, argv[0]) : NULL;
    vc_lock_all(w ? w : "screen");
    if (w) JS_FreeCString(c, w);
    return JS_UNDEFINED;
}

static JSValue f_fido_devices(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    FidoDev d[8];
    int n = fidodev_list(d, 8);
    JSValue a = JS_NewArray(c);
    for (int i = 0; i < n; i++) {
        JSValue o = JS_NewObject(c);
        JS_SetPropertyStr(c, o, "path", JS_NewString(c, d[i].path));
        JS_SetPropertyStr(c, o, "name", JS_NewString(c, d[i].name));
        JS_SetPropertyStr(c, o, "vid", JS_NewInt32(c, (int)d[i].vid));
        JS_SetPropertyStr(c, o, "pid", JS_NewInt32(c, (int)d[i].pid));
        JS_SetPropertyUint32(c, a, (uint32_t)i, o);
    }
    return a;
}

static JSValue f_fido_attach(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    const char *app = argc > 0 && JS_IsString(argv[0]) ? JS_ToCString(c, argv[0]) : NULL;
    int ok = app && perm_get(app, PERM_USB) == PERM_ALLOW;
    if (app) JS_FreeCString(c, app);
    if (!ok) return JS_ThrowTypeError(c, "permiso usb denegado");
    char name[128] = "";
    int r = vc_fido_attach(name, sizeof name);
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "ok", JS_NewBool(c, r == 0));
    if (r) JS_SetPropertyStr(c, o, "error", JS_NewString(c, r == -1 ? "no-device" : "no-access"));
    else JS_SetPropertyStr(c, o, "name", JS_NewString(c, name));
    return o;
}

static JSValue f_prompt_done(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    uint32_t p = 0;
    if (argc > 0) JS_ToUint32(c, &p, argv[0]);
    vc_prompt_done(p, argc > 1 && JS_ToBool(c, argv[1]));
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry vault_funcs[] = {
    JS_CFUNC_DEF("available", 0, f_available),
    JS_CFUNC_DEF("request", 2, f_request),
    JS_CFUNC_DEF("poll", 0, f_poll),
    JS_CFUNC_DEF("pending", 0, f_pending),
    JS_CFUNC_DEF("lock", 1, f_lock),
    JS_CFUNC_DEF("fidoDevices", 0, f_fido_devices),
    JS_CFUNC_DEF("fidoAttach", 1, f_fido_attach),
    JS_CFUNC_DEF("promptDone", 2, f_prompt_done),
};

void js_vault_register(JSContext *c, JSValue lord) {
    JSValue o = JS_NewObject(c);
    JS_SetPropertyFunctionList(c, o, vault_funcs, sizeof vault_funcs / sizeof vault_funcs[0]);
    JS_SetPropertyStr(c, lord, "vault", o);
    vc_start();               /* el llavero y Secret Service andan desde el comienzo de la sesión */
}

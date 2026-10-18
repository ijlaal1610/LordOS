/* users_admin.c — usuarios del equipo, como root en el ayudante de administración (admin.c).
 * Las usa Ajustes → Usuarios (docs/SESIONES.md). Cada argumento son líneas:
 *
 *   user-add       nombre \n nombre completo \n contraseña \n admin (0|1)
 *   user-del       nombre \n conservar la carpeta personal (0|1)
 *   user-passwd    nombre \n contraseña nueva \n contraseña actual
 *                  (la propia: cualquiera, con la actual; la de otro: solo un administrador)
 *   user-admin     nombre \n 0|1                       (grupo sudo)
 *   user-avatar    nombre \n color (#rrggbb o vacío) \n imagen ("" igual, "-" quitar, o una ruta
 *                  de la carpeta personal de quien lo pide; PNG o JPEG de hasta 4 MB)
 *                  (el propio: cualquiera; el de otro: solo un administrador)
 *   user-autologin nombre o vacío                      (/etc/lordos/session.conf)
 *
 * Validación del lado root: nombres [a-z_][a-z0-9_-]*, solo cuentas de personas (uid 1000 a
 * 59999), nunca la propia cuenta al borrar ni el último administrador. Las contraseñas van por
 * la entrada estándar de chpasswd, nunca en argv. Los avatares quedan en /var/lib/lordos/users
 * (los lee la pantalla de ingreso, que no puede entrar a las carpetas personales). */
#include "users_admin.h"
#include "homedirs.h"

#include <crypt.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/stat.h>
#include <unistd.h>

#define USERS_DIR "/var/lib/lordos/users"
#define SESSION_CONF "/etc/lordos/session.conf"
#define ADMIN_GROUP "sudo"
/* Impresoras (M9, docs/IMPRESION.md): CUPS deja administrar impresoras al grupo lpadmin. Los
 * administradores de LordOS también lo tienen; si CUPS no está instalado el grupo no existe y se
 * saltea sin error. */
#define PRINT_GROUP "lpadmin"

static int in_group(const char *user, const char *group);
static void print_group(const char *name, int on) {
    if (!getgrnam(PRINT_GROUP)) return;
    const char *const add[] = { "/usr/sbin/usermod", "--append", "--groups", PRINT_GROUP, name, NULL };
    const char *const del[] = { "/usr/bin/gpasswd", "--delete", name, PRINT_GROUP, NULL };
    if (on != in_group(name, PRINT_GROUP)) admin_run(on ? add : del);
}

/* ---- validación ---- */

static int valid_name(const char *u) {
    size_t n = strlen(u);
    if (!n || n > 32 || !((u[0] >= 'a' && u[0] <= 'z') || u[0] == '_')) return 0;
    for (const char *p = u; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return 0;
    return 1;
}

/* nombre completo: hasta 64 bytes, sin controles ni los separadores de /etc/passwd (: ,) */
static int valid_full(const char *s) {
    size_t n = strlen(s);
    if (n > 64) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) if (*p < 32 || *p == 127 || *p == ':' || *p == ',' || *p == '=') return 0;
    return 1;
}

static int valid_pass(const char *s) {
    size_t n = strlen(s);
    if (!n || n > 256) return 0;
    for (const char *p = s; *p; p++) if (*p == '\n' || *p == '\r') return 0;
    return 1;
}

/* Cuenta de una persona (no de sistema) */
static struct passwd *person(const char *name) {
    if (!valid_name(name)) return NULL;
    struct passwd *pw = getpwnam(name);
    return pw && pw->pw_uid >= 1000 && pw->pw_uid < 60000 ? pw : NULL;
}

/* Parte el argumento en n líneas (la última puede faltar: queda ""). 0 si hay de más. */
static int split(char *arg, char **f, int n) {
    for (int i = 0; i < n; i++) f[i] = "";
    char *p = arg;
    for (int i = 0; i < n && p; i++) {
        f[i] = p;
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        p = nl ? nl + 1 : NULL;
    }
    return p == NULL || !*p;
}

static int in_group(const char *user, const char *group) {
    struct passwd *pw = getpwnam(user);
    struct group *g = getgrnam(group);
    if (!pw || !g) return 0;
    gid_t groups[256];
    int n = 256;
    if (getgrouplist(user, pw->pw_gid, groups, &n) < 0) return 0;
    for (int i = 0; i < n; i++) if (groups[i] == g->gr_gid) return 1;
    return 0;
}

static int admins(void) {
    int n = 0;
    setpwent();
    struct passwd *pw;
    while ((pw = getpwent()))
        if (pw->pw_uid >= 1000 && pw->pw_uid < 60000 && in_group(pw->pw_name, ADMIN_GROUP)) n++;
    endpwent();
    return n;
}

static int set_password(const char *user, const char *pass) {
    size_t n = strlen(user) + strlen(pass) + 3;
    char *line = malloc(n);
    if (!line) return -1;
    snprintf(line, n, "%s:%s\n", user, pass);
    const char *const a[] = { "/usr/sbin/chpasswd", NULL };
    int rc = admin_run_input(a, line);
    explicit_bzero(line, n);
    free(line);
    return rc;
}

static int check_password(const char *user, const char *pass) {
    struct spwd *sp = getspnam(user);
    if (!sp || !sp->sp_pwdp || !sp->sp_pwdp[0] || sp->sp_pwdp[0] == '!' || sp->sp_pwdp[0] == '*') return 0;
    struct crypt_data cd;
    memset(&cd, 0, sizeof cd);
    const char *h = crypt_r(pass, sp->sp_pwdp, &cd);
    int ok = h && h[0] != '*' && strlen(h) == strlen(sp->sp_pwdp);
    unsigned char d = 0;
    for (size_t i = 0; ok && h[i]; i++) d |= (unsigned char)(h[i] ^ sp->sp_pwdp[i]);
    explicit_bzero(&cd, sizeof cd);
    return ok && d == 0;
}

static int write_file(const char *path, const void *data, size_t n, mode_t mode) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s.lordos", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, mode);
    if (fd < 0) return -1;
    int ok = write(fd, data, n) == (ssize_t)n;
    if (fchmod(fd, mode)) ok = 0;
    if (close(fd)) ok = 0;
    if (ok && !rename(tmp, path)) return 0;
    unlink(tmp);
    return -1;
}

static void autologin_get(char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(SESSION_CONF, "r");
    char line[128];
    while (f && fgets(line, sizeof line, f))
        if (sscanf(line, "AUTOLOGIN=%32[a-z0-9_-]", out) == 1) break;
    if (f) fclose(f);
    (void)n;
}

static int autologin_set(const char *user) {
    mkdir("/etc/lordos", 0755);
    char text[256];
    snprintf(text, sizeof text, "# Lo escribe LordOS (Ajustes → Usuarios). Ver docs/SESIONES.md\n"
             "# AUTOLOGIN=usuario entra solo al arrancar; vacío: pantalla de ingreso\nAUTOLOGIN=%s\n", user);
    return write_file(SESSION_CONF, text, strlen(text), 0644);
}

/* ---- acciones ---- */

/* Carpetas comunes en el idioma del sistema (las muestra Archivos) y su idioma. */
static void prepare_home(const struct passwd *pw) {
    char lang[64] = "es_ES.UTF-8";
    FILE *f = fopen("/etc/default/locale", "r");
    char line[128];
    while (f && fgets(line, sizeof line, f)) if (sscanf(line, "LANG=%63[A-Za-z_.@0-9-]", lang) == 1) break;
    if (f) fclose(f);
    char path[PATH_MAX];
    /* Escritorio, Documentos… y ~/.config/user-dirs.dirs en el idioma del sistema (homedirs.c) */
    if (homedirs_create(pw->pw_dir, pw->pw_uid, pw->pw_gid, lang)) admin_say("LOG", "aviso: no se pudieron crear todas las carpetas");
    snprintf(path, sizeof path, "%s/.config", pw->pw_dir);
    if (!mkdir(path, 0755) && chown(path, pw->pw_uid, pw->pw_gid)) { /* sigue */ }
    snprintf(path, sizeof path, "%s/.config/lordos", pw->pw_dir);
    if (!mkdir(path, 0755) && chown(path, pw->pw_uid, pw->pw_gid)) { /* sigue */ }
    char text[96];
    snprintf(path, sizeof path, "%s/.config/locale.conf", pw->pw_dir);
    snprintf(text, sizeof text, "LANG=%s\n", lang);
    if (!write_file(path, text, strlen(text), 0644) && chown(path, pw->pw_uid, pw->pw_gid)) { /* sigue */ }
    snprintf(path, sizeof path, "%s/.config/lordos/settings.json", pw->pw_dir);
    /* idioma de la interfaz: el del sistema si hay catálogo (es, en, pt); si no, español */
    const char *ui = homedirs_ui_lang(lang);
    snprintf(text, sizeof text, "{\n  \"language\": \"%s\"\n}\n", ui);
    if (!write_file(path, text, strlen(text), 0644) && chown(path, pw->pw_uid, pw->pw_gid)) { /* sigue */ }
    snprintf(path, sizeof path, "%s/.config/lordos/.dirs", pw->pw_dir);
    if (!write_file(path, "", 0, 0644) && chown(path, pw->pw_uid, pw->pw_gid)) { /* sigue */ }
}

void users_act_add(const AdminReq *r, const char *arg, size_t len) {
    (void)r;
    char *buf = strndup(arg, len), *f[4];
    if (!buf || !split(buf, f, 4)) { admin_say("ERR", "argumento inválido"); free(buf); return; }
    const char *name = f[0], *full = f[1], *pass = f[2];
    int adm = !strcmp(f[3], "1");
    if (!valid_name(name) || !valid_full(full) || !valid_pass(pass)) admin_say("ERR", "nombre o contraseña inválidos");
    else if (getpwnam(name) || getgrnam(name)) admin_say("ERR", "ya existe «%s»", name);
    else {
        const char *const a[] = { "/usr/sbin/useradd", "--create-home", "--shell", "/bin/bash", "--user-group", "--comment", full, name, NULL };
        struct passwd *pw;
        if (admin_run(a) || !(pw = getpwnam(name))) admin_say("ERR", "useradd falló");
        else if (set_password(name, pass)) admin_say("ERR", "no se pudo poner la contraseña");
        else {
            if (adm) { const char *const g[] = { "/usr/sbin/usermod", "--append", "--groups", ADMIN_GROUP, name, NULL }; admin_run(g); print_group(name, 1); }
            if (getgrnam("pipewire")) { const char *const g[] = { "/usr/sbin/usermod", "--append", "--groups", "pipewire", name, NULL }; admin_run(g); }   /* sonido en tiempo real (docs/SONIDO.md) */
            prepare_home(pw);
            admin_say("OK", "usuario %s creado", name);
        }
    }
    explicit_bzero(buf, len);
    free(buf);
}

void users_act_del(const AdminReq *r, const char *arg, size_t len) {
    char *buf = strndup(arg, len), *f[2];
    if (!buf || !split(buf, f, 2)) { admin_say("ERR", "argumento inválido"); free(buf); return; }
    const char *name = f[0];
    int keep = !strcmp(f[1], "1");
    struct passwd *pw = person(name);
    if (!pw) admin_say("ERR", "usuario inválido");
    else if (!strcmp(name, r->user)) admin_say("ERR", "no se puede borrar la cuenta con la que estás");
    else if (in_group(name, ADMIN_GROUP) && admins() <= 1) admin_say("ERR", "es el único administrador");
    else {
        /* su sesión (y todo lo suyo) termina antes */
        const char *const k[] = { "/usr/bin/pkill", "--signal", "KILL", "--uid", name, NULL };
        admin_run(k);
        usleep(300000);
        const char *const a_keep[] = { "/usr/sbin/userdel", name, NULL };
        const char *const a_rm[] = { "/usr/sbin/userdel", "--remove", name, NULL };
        if (admin_run(keep ? a_keep : a_rm)) admin_say("ERR", "userdel falló");
        else {
            char p[128], au[40];
            snprintf(p, sizeof p, USERS_DIR "/%s.json", name);
            unlink(p);
            snprintf(p, sizeof p, USERS_DIR "/%s.png", name);
            unlink(p);
            autologin_get(au, sizeof au);
            if (!strcmp(au, name)) autologin_set("");
            admin_say("OK", "usuario %s borrado", name);
        }
    }
    free(buf);
}

void users_act_passwd(const AdminReq *r, const char *arg, size_t len) {
    char *buf = strndup(arg, len), *f[3];
    if (!buf || !split(buf, f, 3)) { admin_say("ERR", "argumento inválido"); free(buf); return; }
    const char *name = f[0], *pass = f[1], *old = f[2];
    int self = !strcmp(name, r->user);
    if (!person(name) || !valid_pass(pass)) admin_say("ERR", "usuario o contraseña inválidos");
    else if (!self && !r->admin) admin_say("ERR", "solo un administrador cambia la contraseña de otro usuario");
    else if (self && !check_password(name, old)) { sleep(1); admin_say("ERR", "la contraseña actual no es correcta"); }
    else if (set_password(name, pass)) admin_say("ERR", "no se pudo cambiar la contraseña");
    else admin_say("OK", "contraseña cambiada");
    explicit_bzero(buf, len);
    free(buf);
}

void users_act_admin(const AdminReq *r, const char *arg, size_t len) {
    char *buf = strndup(arg, len), *f[2];
    if (!buf || !split(buf, f, 2)) { admin_say("ERR", "argumento inválido"); free(buf); return; }
    const char *name = f[0];
    int on = !strcmp(f[1], "1");
    (void)r;
    if (!person(name) || (strcmp(f[1], "0") && strcmp(f[1], "1"))) admin_say("ERR", "argumento inválido");
    else if (!on && in_group(name, ADMIN_GROUP) && admins() <= 1) admin_say("ERR", "es el único administrador");
    else {
        const char *const add[] = { "/usr/sbin/usermod", "--append", "--groups", ADMIN_GROUP, name, NULL };
        const char *const del[] = { "/usr/bin/gpasswd", "--delete", name, ADMIN_GROUP, NULL };
        if (on == in_group(name, ADMIN_GROUP)) admin_say("OK", "sin cambios");
        else if (admin_run(on ? add : del)) admin_say("ERR", "no se pudo cambiar el grupo");
        else { print_group(name, on); admin_say("OK", "%s", on ? "ahora es administrador" : "ya no es administrador"); }
    }
    free(buf);
}

/* Lee una imagen de la carpeta personal de quien lo pide, CON SUS PERMISOS (setfsuid): root no
 * lee nada que el usuario no pudiera leer. */
static unsigned char *read_user_image(const AdminReq *r, const char *path, size_t *n) {
    char real[PATH_MAX], home[PATH_MAX];
    if (path[0] != '/' || !realpath(r->home, home)) return NULL;
    setfsgid(r->gid);
    setfsuid(r->uid);
    unsigned char *data = NULL;
    size_t hl = strlen(home);
    int fd = -1;
    struct stat st;
    if (realpath(path, real) && !strncmp(real, home, hl) && real[hl] == '/' && (fd = open(real, O_RDONLY | O_NOFOLLOW | O_CLOEXEC)) >= 0 &&
        !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size > 8 && st.st_size <= (4 << 20) && (data = malloc((size_t)st.st_size))) {
        if (read(fd, data, (size_t)st.st_size) != st.st_size) { free(data); data = NULL; }
        else *n = (size_t)st.st_size;
    }
    if (fd >= 0) close(fd);
    setfsuid(0);
    setfsgid(0);
    static const unsigned char png[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    if (data && memcmp(data, png, 8) && !(data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff)) { free(data); data = NULL; }
    return data;
}

void users_act_avatar(const AdminReq *r, const char *arg, size_t len) {
    char *buf = strndup(arg, len), *f[3];
    if (!buf || !split(buf, f, 3)) { admin_say("ERR", "argumento inválido"); free(buf); return; }
    const char *name = f[0], *color = f[1], *img = f[2];
    int ok_color = !color[0] || (strlen(color) == 7 && color[0] == '#' && strspn(color + 1, "0123456789abcdefABCDEF") == 6);
    if (!person(name) || !ok_color) admin_say("ERR", "argumento inválido");
    else if (strcmp(name, r->user) && !r->admin) admin_say("ERR", "solo un administrador cambia el avatar de otro usuario");
    else {
        mkdir("/var/lib/lordos", 0755);
        mkdir(USERS_DIR, 0755);
        char p[128], json[64];
        snprintf(p, sizeof p, USERS_DIR "/%s.json", name);
        snprintf(json, sizeof json, "{\"color\":\"%s\"}\n", color);
        int bad = write_file(p, json, strlen(json), 0644);
        snprintf(p, sizeof p, USERS_DIR "/%s.png", name);
        if (!strcmp(img, "-")) unlink(p);
        else if (img[0]) {
            size_t n = 0;
            unsigned char *data = read_user_image(r, img, &n);
            if (!data) { admin_say("ERR", "la imagen tiene que ser PNG o JPEG de tu carpeta personal (hasta 4 MB)"); free(buf); return; }
            bad |= write_file(p, data, n, 0644);
            free(data);
        }
        if (bad) admin_say("ERR", "no se pudo guardar el avatar");
        else admin_say("OK", "avatar guardado");
    }
    free(buf);
}

void users_act_autologin(const AdminReq *r, const char *arg, size_t len) {
    (void)r;
    (void)len;
    if (arg[0] && !person(arg)) { admin_say("ERR", "usuario inválido"); return; }
    if (autologin_set(arg)) admin_say("ERR", "no se pudo escribir %s", SESSION_CONF);
    else admin_say("OK", "%s", arg[0] ? "inicio automático prendido" : "inicio automático apagado");
}

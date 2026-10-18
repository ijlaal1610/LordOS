/* sessiond.c — lord-sessiond: el administrador de sesiones de LordOS (docs/SESIONES.md).
 *
 * Lo arranca lordos.service como root en el arranque, en lugar del viejo inicio automático. Hace
 * de "display manager" propio:
 *   - el monitor (monitor.c): le presta la placa de video y la entrada a la sesión que se ve
 *     (la sesión dibuja directo, con GPU si hay);
 *   - la pantalla de ingreso: un lord-server --greeter (tema themes/greeter) que corre como el
 *     usuario de sistema lordos-greeter; sus pedidos (ingresar, reiniciar, apagar) llegan acá y
 *     las contraseñas se verifican acá, contra /etc/shadow;
 *   - las sesiones: un lord-server --headless por usuario, que sigue vivo en segundo plano
 *     cuando otro usuario usa el monitor (cambio rápido de usuario) y que se ve igual desde el
 *     monitor y desde el navegador (sess.c);
 *   - el escritorio remoto en la LAN (conn.c), solo si está prendido en Ajustes → Compartir:
 *     un hijo por conexión que suelta root ni bien sabe de quién es la sesión.
 *
 *   /etc/lordos/session.conf         AUTOLOGIN=usuario (vacío o sin la línea: pantalla de ingreso)
 *   /etc/lordos/remote/config        enabled=0|1, port=5673, allow=all|group, insecure_dev=0 (0644)
 *   /etc/lordos/remote/cert.pem      certificado autofirmado (se crea al prender el remoto)
 *   /etc/lordos/remote/key.pem       su clave (0600)
 *   /etc/lordos/remote/fingerprint   huella SHA-256 del certificado (la muestra Ajustes)
 *   /run/lordos/status.json          sesiones, quién está en el monitor y quién conectado (Ajustes)
 *
 * SIGHUP vuelve a leer la configuración (prende o apaga el remoto sin tocar las sesiones).
 * Es el único programa de LordOS que enlaza OpenSSL (TLS): lord-server no. */
#include "sessiond.h"
#include "../sec/cgroup.h"
#include "proto.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <grp.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

RdShared *rd_shared;
RdConfig rd_conf;

double rd_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}
void rd_lock(void) {
    if (pthread_mutex_lock(&rd_shared->lock) == EOWNERDEAD) pthread_mutex_consistent(&rd_shared->lock);
}
void rd_unlock(void) { pthread_mutex_unlock(&rd_shared->lock); }

static int path_ok(const char *p) { struct stat st; return stat(p, &st) == 0; }

/* snprintf que, si no entra, deja el destino vacío (las rutas cortadas no sirven) */
#define SP(dst, ...) do { int r_ = snprintf(dst, sizeof dst, __VA_ARGS__); if (r_ < 0 || (size_t)r_ >= sizeof dst) dst[0] = 0; } while (0)

/* Rutas de lord-server y de los datos: al lado de este binario (instalado: /usr/lib/lordos/bin,
 * datos en /usr/lib/lordos; desarrollo: server/build, datos en la raíz del repositorio). */
static void find_paths(void) {
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    exe[n] = 0;
    char *slash = strrchr(exe, '/');
    if (slash) *slash = 0;
    SP(rd_conf.server, "%s/lord-server", exe);
    char root[PATH_MAX], probe[PATH_MAX + 32];
    SP(root, "%s/..", exe);
    SP(probe, "%s/themes", root);
    if (!path_ok(probe)) SP(root, "%s/../..", exe);
    char real[PATH_MAX];
    if (realpath(root, real)) SP(root, "%s", real);
    SP(rd_conf.theme, "%s/themes/default/theme.bundle.js", root);
    SP(rd_conf.greeter_theme, "%s/themes/greeter/theme.bundle.js", root);
    SP(rd_conf.fonts, "%s/assets/fonts", root);
    SP(rd_conf.shell, "%s/assets/shell", root);
    SP(rd_conf.workdir, "%s", exe);
}

static void load_config(void) {
    rd_conf.port = RD_DEFAULT_PORT;
    rd_conf.enabled = rd_conf.group_only = rd_conf.insecure_dev = 0;
    FILE *f = fopen(RD_CONF_DIR "/config", "r");
    char line[256];
    while (f && fgets(line, sizeof line, f)) {
        int v;
        char s[32];
        if (sscanf(line, "enabled=%d", &v) == 1) rd_conf.enabled = v == 1;
        else if (sscanf(line, "port=%d", &v) == 1 && v > 0 && v < 65536) rd_conf.port = v;
        else if (sscanf(line, "allow=%31s", s) == 1) rd_conf.group_only = !strcmp(s, "group");
        else if (sscanf(line, "insecure_dev=%d", &v) == 1) rd_conf.insecure_dev = v == 1;
    }
    if (f) fclose(f);
}

/* Certificado autofirmado P-256 por 10 años (CN = nombre del equipo). */
static int ensure_cert(void) {
    if (path_ok(RD_CONF_DIR "/cert.pem") && path_ok(RD_CONF_DIR "/key.pem")) return 0;
    mkdir("/etc/lordos", 0755);
    mkdir(RD_CONF_DIR, 0755);
    EVP_PKEY *key = EVP_EC_gen("P-256");
    X509 *x = X509_new();
    if (!key || !x) return -1;
    X509_set_version(x, 2);
    unsigned char serial[16];
    FILE *ur = fopen("/dev/urandom", "r");
    if (!ur || fread(serial, 1, sizeof serial, ur) != sizeof serial) { if (ur) fclose(ur); return -1; }
    fclose(ur);
    serial[0] &= 0x7f;
    BIGNUM *bn = BN_bin2bn(serial, sizeof serial, NULL);
    BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(x));
    BN_free(bn);
    X509_gmtime_adj(X509_getm_notBefore(x), -3600);
    X509_gmtime_adj(X509_getm_notAfter(x), 10L * 365 * 24 * 3600);
    X509_set_pubkey(x, key);
    char host[256] = "lordos";
    gethostname(host, sizeof host - 1);
    X509_NAME *name = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (const unsigned char *)"LordOS", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)host, -1, -1, 0);
    X509_set_issuer_name(x, name);
    char san[300];
    snprintf(san, sizeof san, "DNS:%s,DNS:%s.local", host, host);
    X509V3_CTX v3;
    X509V3_set_ctx_nodb(&v3);
    X509V3_set_ctx(&v3, x, x, NULL, NULL, 0);
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(NULL, &v3, NID_subject_alt_name, san);
    if (ext) { X509_add_ext(x, ext, -1); X509_EXTENSION_free(ext); }
    if (!X509_sign(x, key, EVP_sha256())) return -1;
    mode_t old = umask(077);
    FILE *kf = fopen(RD_CONF_DIR "/key.pem.new", "w");
    umask(old);
    FILE *cf = fopen(RD_CONF_DIR "/cert.pem.new", "w");
    int ok = kf && cf && PEM_write_PrivateKey(kf, key, NULL, NULL, 0, NULL, NULL) && PEM_write_X509(cf, x);
    if (kf) fclose(kf);
    if (cf) fclose(cf);
    chmod(RD_CONF_DIR "/cert.pem.new", 0644);
    if (ok) ok = !rename(RD_CONF_DIR "/key.pem.new", RD_CONF_DIR "/key.pem") && !rename(RD_CONF_DIR "/cert.pem.new", RD_CONF_DIR "/cert.pem");
    X509_free(x);
    EVP_PKEY_free(key);
    fprintf(stderr, "lord-sessiond: certificado nuevo para %s\n", host);
    return ok ? 0 : -1;
}

static void write_fingerprint(void) {
    FILE *f = fopen(RD_CONF_DIR "/cert.pem", "r");
    X509 *x = f ? PEM_read_X509(f, NULL, NULL, NULL) : NULL;
    if (f) fclose(f);
    if (!x) return;
    unsigned char md[32];
    unsigned int n = 0;
    X509_digest(x, EVP_sha256(), md, &n);
    X509_free(x);
    FILE *o = fopen(RD_CONF_DIR "/fingerprint.new", "w");
    if (!o) return;
    for (unsigned i = 0; i < n; i++) fprintf(o, "%02X%s", md[i], i + 1 < n ? ":" : "\n");
    fclose(o);
    chmod(RD_CONF_DIR "/fingerprint.new", 0644);
    rename(RD_CONF_DIR "/fingerprint.new", RD_CONF_DIR "/fingerprint");
}

/* ---- JSON ---- */

typedef struct { char *p; size_t n, cap; } Str;

static void sput(Str *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sput(Str *s, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int k = vsnprintf(s->p ? s->p + s->n : NULL, s->p ? s->cap - s->n : 0, fmt, ap);
        va_end(ap);
        if (k < 0) return;
        if (s->p && s->n + (size_t)k < s->cap) { s->n += (size_t)k; return; }
        size_t cap = s->cap ? s->cap * 2 : 4096;
        while (cap < s->n + (size_t)k + 1) cap *= 2;
        char *nb = realloc(s->p, cap);
        if (!nb) return;
        s->p = nb;
        s->cap = cap;
    }
}

/* texto como string JSON (sin comillas alrededor; control y comillas escapados) */
static void sput_esc(Str *s, const char *t, size_t max) {
    for (size_t i = 0; t[i] && i < max; i++) {
        unsigned char c = (unsigned char)t[i];
        if (c == '"' || c == '\\') sput(s, "\\%c", c);
        else if (c < 0x20) sput(s, "\\u%04x", c);
        else sput(s, "%c", c);
    }
}

/* Valor de "clave":"…" en un JSON plano de la pantalla de ingreso (sin escapes salvo \" y \\). */
static int jstr(const char *j, const char *key, char *out, size_t n) {
    char k[40];
    snprintf(k, sizeof k, "\"%s\":\"", key);
    const char *p = strstr(j, k);
    if (!p) return -1;
    p += strlen(k);
    size_t o = 0;
    for (; *p && *p != '"' && o + 1 < n; p++) {
        if (*p == '\\' && p[1]) p++;
        out[o++] = *p;
    }
    out[o] = 0;
    return 0;
}

/* ---- estado (Ajustes lo lee) ---- */

static int listening, lfd = -1;

static void write_status(void) {
    mkdir(SD_RUN_DIR, 0755);
    Str s = { 0 };
    sput(&s, "{\"running\":%s,\"pid\":%d,\"port\":%d,\"allow\":\"%s\",\"clients\":[", listening ? "true" : "false",
         (int)getpid(), rd_conf.port, rd_conf.group_only ? "group" : "all");
    rd_lock();
    int first = 1;
    for (int i = 0; i < RD_MAX_CONNS; i++) {
        RdConn *c = &rd_shared->conns[i];
        if (!c->pid) continue;
        sput(&s, "%s{\"pid\":%d,\"ip\":\"%s\",\"user\":\"%s\",\"since\":%.0f}", first ? "" : ",", (int)c->pid, c->ip, c->user, c->since);
        first = 0;
    }
    sput(&s, "],\"monitor\":\"%s\",\"sessions\":[", rd_shared->monitor);
    first = 1;
    for (int i = 0; i < SD_MAX_SESSIONS; i++) {
        SdSession *x = &rd_shared->sessions[i];
        if (!x->pid) continue;
        if (kill(x->pid, 0)) { memset(x, 0, sizeof *x); continue; }
        sput(&s, "%s{\"pid\":%d,\"user\":\"%s\",\"since\":%.0f}", first ? "" : ",", (int)x->pid, x->user, x->since);
        first = 0;
    }
    rd_unlock();
    sput(&s, "]}\n");
    FILE *f = fopen(SD_STATUS ".new", "w");
    if (f && s.p) { fputs(s.p, f); }
    if (f) {
        fclose(f);
        chmod(SD_STATUS ".new", 0644);
        rename(SD_STATUS ".new", SD_STATUS);
    }
    free(s.p);
}

static void bump(void) { rd_lock(); rd_shared->version++; rd_unlock(); }

/* ---- pantalla de ingreso: usuarios y equipo ---- */

static int is_admin(const struct passwd *pw) {
    gid_t groups[256];
    int n = 256;
    if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &n) < 0) return 0;
    const char *const names[] = { "sudo", "admin", "wheel", NULL };
    for (int k = 0; names[k]; k++) {
        struct group *g = getgrnam(names[k]);
        if (g) for (int i = 0; i < n; i++) if (groups[i] == g->gr_gid) return 1;
    }
    return 0;
}

/* "#rrggbb" del avatar (/var/lib/lordos/users/<u>.json), validado */
static void avatar_color(const char *user, char out[8]) {
    out[0] = 0;
    char p[128], buf[512];
    snprintf(p, sizeof p, SD_USERS_DIR "/%s.json", user);
    int fd = open(p, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = 0;
    char c[16];
    if (jstr(buf, "color", c, sizeof c) || strlen(c) != 7 || c[0] != '#') return;
    for (int i = 1; i < 7; i++) if (!strchr("0123456789abcdefABCDEF", c[i])) return;
    memcpy(out, c, 8);
}

static void file_value(const char *path, const char *key, char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    char line[256];
    size_t kl = strlen(key);
    while (f && fgets(line, sizeof line, f)) {
        if (strncmp(line, key, kl) || line[kl] != '=') continue;
        char *v = line + kl + 1;
        if (*v == '"') v++;
        size_t k = 0;
        while (v[k] && v[k] != '"' && v[k] != '\n' && (isalnum((unsigned char)v[k]) || strchr(",._@-", v[k])) && k + 1 < n) { out[k] = v[k]; k++; }
        out[k] = 0;
    }
    if (f) fclose(f);
}

static char greeter_mode[8] = "login", greeter_user[33];

static void greeter_reply_hello(void) {
    Str s = { 0 };
    sput(&s, "{\"t\":\"hello\",\"mode\":\"%s\",\"user\":\"%s\",\"host\":\"", greeter_mode, greeter_user);
    char host[128] = "lordos";
    gethostname(host, sizeof host - 1);
    sput_esc(&s, host, 64);
    char layouts[96], variants[96], lang[48];
    file_value("/etc/default/keyboard", "XKBLAYOUT", layouts, sizeof layouts);
    file_value("/etc/default/keyboard", "XKBVARIANT", variants, sizeof variants);
    file_value("/etc/default/locale", "LANG", lang, sizeof lang);
    sput(&s, "\",\"layouts\":\"%s\",\"variants\":\"%s\",\"lang\":\"%s\",\"users\":[", layouts, variants, lang);
    setpwent();
    int first = 1, count = 0;
    struct passwd *pw;
    while ((pw = getpwent()) && count < 64) {
        if (!sd_user_allowed(pw, 0)) continue;
        char full[128];
        snprintf(full, sizeof full, "%s", pw->pw_gecos ? pw->pw_gecos : "");
        char *comma = strchr(full, ',');
        if (comma) *comma = 0;
        char color[8], img[160];
        avatar_color(pw->pw_name, color);
        snprintf(img, sizeof img, SD_USERS_DIR "/%s.png", pw->pw_name);
        struct stat st;
        int has_img = !lstat(img, &st) && S_ISREG(st.st_mode) && st.st_size < (4 << 20);
        sput(&s, "%s{\"name\":\"%s\",\"full\":\"", first ? "" : ",", pw->pw_name);
        sput_esc(&s, full, 100);
        sput(&s, "\",\"color\":\"%s\",\"image\":%s,\"admin\":%s,\"open\":%s}", color, has_img ? "true" : "false",
             is_admin(pw) ? "true" : "false", sd_session_running(pw->pw_uid) ? "true" : "false");
        first = 0;
        count++;
    }
    endpwent();
    sput(&s, "]}");
    if (s.p && s.n < 60000) sd_mon_greeter(s.p);
    free(s.p);
}

/* ---- el monitor ---- */

static pid_t worker_pid = -1;
static int wk_r = -1, wk_w = -1;        /* trabajadores → principal (SOCK_SEQPACKET, con fds) */
static int has_monitor;
static char shown = '-', shown_user[33];

/* Acción pendiente mientras trabaja otro hijo (crear una sesión puede tardar unos segundos). */
typedef enum { W_NONE, W_GREETER, W_LOGIN, W_AUTOLOGIN } WorkKind;
static WorkKind pending;
static char pending_user[33];

static void set_mode(const char *mode, const char *user) {
    snprintf(greeter_mode, sizeof greeter_mode, "%s", mode);
    snprintf(greeter_user, sizeof greeter_user, "%s", user ? user : "");
}

/* En un hijo: la pantalla de ingreso (la crea si hace falta) al monitor. */
static void work_greeter(void) {
    int fd = sd_greeter_get(1280, 800);         /* toma el tamaño del monitor al mostrarse */
    if (fd < 0) _exit(1);
    char msg[160];
    snprintf(msg, sizeof msg, "greeter-in {\"t\":\"mode\",\"mode\":\"%s\",\"user\":\"%s\"}", greeter_mode, greeter_user);
    sd_ctl_send(wk_w, "show g", fd);
    sd_ctl_send(wk_w, msg, -1);
    _exit(0);
}

/* En un hijo: la sesión de un usuario (ya verificado) al monitor. */
/* La contraseña recién verificada abre el llavero de la sesión (docs/CONTRASENAS.md): se manda
 * una sola vez por el socket de la sesión (RP_KEYRING, solo root) y se borra. */
static void send_keyring(int fd, char *pass) {
    size_t n = pass ? strlen(pass) : 0;
    if (!n || n > 1024) return;
    unsigned char buf[1 + 4 + 1024];
    uint32_t len = (uint32_t)(1 + n);
    memcpy(buf, &len, 4);
    buf[4] = RP_KEYRING;
    memcpy(buf + 5, pass, n);
    ssize_t w = send(fd, buf, 5 + n, MSG_NOSIGNAL);
    (void)w;
    explicit_bzero(buf, sizeof buf);
}

static void work_user_pw(const char *user, char *pass) {
    struct passwd *pw = getpwnam(user);
    int fd = pw ? sd_session_get(pw, 1280, 800) : -1;
    if (fd < 0) { if (pass) explicit_bzero(pass, strlen(pass)); sd_ctl_send(wk_w, "greeter-in {\"t\":\"error\"}", -1); _exit(1); }
    send_keyring(fd, pass);
    if (pass) explicit_bzero(pass, strlen(pass));
    char msg[80];
    snprintf(msg, sizeof msg, "show u %s", user);
    sd_ctl_send(wk_w, "greeter-in {\"t\":\"ok\"}", -1);
    sd_ctl_send(wk_w, msg, fd);
    _exit(0);
}

static void work_user(const char *user) { work_user_pw(user, NULL); }

/* En un hijo: verifica la contraseña escrita en el monitor y entra. */
static void work_login(const char *user, char *pass) {
    char key[56];
    snprintf(key, sizeof key, "monitor:%.32s", user);
    double wait = sd_fail_wait(key);
    if (wait > 0) {
        explicit_bzero(pass, strlen(pass));
        char msg[96];
        snprintf(msg, sizeof msg, "greeter-in {\"t\":\"fail\",\"wait\":%.0f}", wait + 0.5);
        sd_ctl_send(wk_w, msg, -1);
        _exit(1);
    }
    int ok = sd_check_password(user, pass, 0);
    if (!ok) {
        explicit_bzero(pass, strlen(pass));
        sd_fail_note(key, 1);
        sd_log("ingreso fallido en el monitor para «%s»", sd_valid_username(user) ? user : "?");
        sleep(1);
        double w2 = sd_fail_wait(key);
        char msg[96];
        snprintf(msg, sizeof msg, "greeter-in {\"t\":\"fail\",\"wait\":%.0f}", w2 > 0 ? w2 + 0.5 : 0);
        sd_ctl_send(wk_w, msg, -1);
        _exit(1);
    }
    sd_fail_note(key, 0);
    sd_log("ingresó %s en el monitor", user);
    work_user_pw(user, pass);          /* abre también el llavero de la sesión */
}

static int start_worker(WorkKind kind, const char *user, char *pass) {
    if (worker_pid > 0 || wk_w < 0 || !has_monitor) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (lfd >= 0) close(lfd);
        sd_mon_child();                 /* sin la placa ni la entrada del monitor */
        if (wk_r >= 0) close(wk_r);
        if (kind == W_GREETER) work_greeter();
        else if (kind == W_LOGIN) work_login(user, pass);
        else work_user(user);
        _exit(0);
    }
    if (pid > 0) worker_pid = pid;
    return pid > 0 ? 0 : -1;
}

static void show_greeter(const char *mode, const char *user) {
    set_mode(mode, user);
    if (start_worker(W_GREETER, NULL, NULL)) pending = W_GREETER;
}

static void power(const char *what) {
    const char *verb = !strcmp(what, "reboot") ? "reboot" : !strcmp(what, "poweroff") ? "poweroff" : NULL;
    if (!verb) return;
    sd_log("%s pedido desde la pantalla de ingreso", verb);
    pid_t pid = fork();
    if (pid == 0) {
        const char *const a[] = { "/usr/bin/systemctl", verb, NULL };
        char *const env[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL };
        execve(a[0], (char *const *)a, env);
        _exit(127);
    }
}

static void autologin_user(char *out, size_t n) {
    out[0] = 0;
    char u[64];
    file_value(SD_SESSION_CONF, "AUTOLOGIN", u, sizeof u);
    struct passwd *pw = u[0] && sd_valid_username(u) ? getpwnam(u) : NULL;
    if (pw && sd_user_allowed(pw, 0)) snprintf(out, n, "%.32s", u);
}

/* Al arrancar: el inicio automático (si está elegido) o la pantalla de ingreso. Solo una vez
 * por arranque del equipo (/run se vacía al reiniciar): si el servicio se reinicia, se vuelve
 * por la pantalla de ingreso aunque la sesión hubiera estado bloqueada. */
static void boot_monitor(void) {
    char au[40];
    autologin_user(au, sizeof au);
    struct stat st;
    int first = stat(SD_RUN_DIR "/autologin-done", &st) != 0;
    if (first) close(open(SD_RUN_DIR "/autologin-done", O_WRONLY | O_CREAT | O_CLOEXEC, 0644));
    if (first && au[0]) {
        sd_log("inicio automático: %s", au);
        snprintf(pending_user, sizeof pending_user, "%s", au);
        if (start_worker(W_AUTOLOGIN, au, NULL)) pending = W_AUTOLOGIN;
    } else show_greeter("login", NULL);
}

static void set_shown(char k, const char *user) {
    shown = k;
    snprintf(shown_user, sizeof shown_user, "%.32s", k == 'u' ? user : "");
    rd_lock();
    snprintf(rd_shared->monitor, sizeof rd_shared->monitor, "%s", shown_user);
    rd_shared->version++;
    rd_unlock();
}

/* Lo que mandan los trabajadores: qué sesión mostrar y mensajes para la pantalla de ingreso. */
static void from_worker(void) {
    char buf[4200];
    int fd;
    int n = sd_ctl_recv(wk_r, buf, sizeof buf, &fd);
    if (n <= 0) { if (fd >= 0) close(fd); return; }
    char user[40];
    if (!strcmp(buf, "show g") && fd >= 0) {
        set_shown('g', NULL);
        if (sd_mon_show(fd, "-")) { sd_mon_lost(); set_shown('-', NULL); }
        return;
    }
    if (sscanf(buf, "show u %32s", user) == 1 && fd >= 0 && sd_valid_username(user)) {
        sd_log("monitor: sesión de %s", user);
        set_shown('u', user);
        if (sd_mon_show(fd, user)) { sd_mon_lost(); set_shown('-', NULL); show_greeter("login", NULL); }
        return;
    }
    if (fd >= 0) close(fd);
    if (!strncmp(buf, "greeter-in ", 11) && shown == 'g') sd_mon_greeter(buf + 11);
}

/* Pedidos de la sesión que se ve: bloquear o cambiar de usuario; de la pantalla de ingreso:
 * ingresar, reiniciar o apagar. */
static void from_shown(int type, const char *j) {
    if (type == RP_SESSION && shown == 'u') {
        if (strstr(j, "\"lock\"")) { sd_log("%s bloqueó la pantalla", shown_user); show_greeter("lock", shown_user); }
        else if (strstr(j, "\"switch\"")) { sd_log("%s cambia de usuario", shown_user); show_greeter("switch", shown_user); }
        return;
    }
    if (type != RP_GREETER || shown != 'g') return;
    char t[16] = "";
    jstr(j, "t", t, sizeof t);
    if (!strcmp(t, "hello")) greeter_reply_hello();
    else if (!strcmp(t, "login")) {
        char u[40] = "", pass[300] = "";
        jstr(j, "user", u, sizeof u);
        jstr(j, "pass", pass, sizeof pass);
        if (!sd_valid_username(u) || start_worker(W_LOGIN, u, pass)) sd_mon_greeter("{\"t\":\"busy\"}");
        explicit_bzero(pass, sizeof pass);
    } else if (!strcmp(t, "power")) {
        char what[16] = "";
        jstr(j, "a", what, sizeof what);
        power(what);
    }
}

static void monitor_event(void) {
    if (sd_mon_read(from_shown) == 0) return;
    /* la sesión que se veía terminó: cerró su sesión, o se cayó la pantalla de ingreso */
    char was = shown;
    sd_mon_lost();
    set_shown('-', NULL);
    if (was == 'u') { sd_log("la sesión del monitor terminó"); show_greeter("login", NULL); }
    else show_greeter(greeter_mode, greeter_user);
}

/* ---- escritorio remoto ---- */

static SSL_CTX *tls;

static void stop_remote(void) {
    if (lfd >= 0) close(lfd);
    lfd = -1;
    listening = 0;
    rd_lock();
    for (int i = 0; i < RD_MAX_CONNS; i++) if (rd_shared->conns[i].pid) kill(rd_shared->conns[i].pid, SIGTERM);
    rd_unlock();
    if (tls) SSL_CTX_free(tls);
    tls = NULL;
}

static void start_remote(void) {
    if (ensure_cert()) { sd_log("no se pudo crear el certificado"); return; }
    write_fingerprint();
    tls = SSL_CTX_new(TLS_server_method());
    if (!tls || SSL_CTX_set_min_proto_version(tls, TLS1_2_VERSION) != 1 ||
        SSL_CTX_use_certificate_file(tls, RD_CONF_DIR "/cert.pem", SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(tls, RD_CONF_DIR "/key.pem", SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        if (tls) SSL_CTX_free(tls);
        tls = NULL;
        return;
    }
    lfd = socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int one = 1, zero = 0;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
    struct sockaddr_in6 a = { .sin6_family = AF_INET6, .sin6_port = htons((uint16_t)rd_conf.port), .sin6_addr = in6addr_any };
    if (lfd < 0 || bind(lfd, (struct sockaddr *)&a, sizeof a) || listen(lfd, 16)) {
        sd_log("no se pudo escuchar en el puerto %d: %s", rd_conf.port, strerror(errno));
        if (lfd >= 0) close(lfd);
        lfd = -1;
        return;
    }
    listening = 1;
    sd_log("escritorio remoto en https://*:%d", rd_conf.port);
}

static void accept_remote(void) {
    struct sockaddr_in6 peer;
    socklen_t pl = sizeof peer;
    int fd = accept4(lfd, (struct sockaddr *)&peer, &pl, SOCK_CLOEXEC);
    if (fd < 0) return;
    char ip[64];
    inet_ntop(AF_INET6, &peer.sin6_addr, ip, sizeof ip);
    const char *shown_ip = !strncmp(ip, "::ffff:", 7) ? ip + 7 : ip;
    int busy = 0;
    rd_lock();
    for (int i = 0; i < RD_MAX_CONNS; i++) if (rd_shared->conns[i].pid) busy++;
    rd_unlock();
    if (busy >= RD_MAX_CONNS) { close(fd); return; }
    pid_t c = fork();
    if (c == 0) {
        close(lfd);
        sd_mon_child();                 /* sin la placa ni la entrada del monitor */
        if (wk_r >= 0) close(wk_r);
        if (wk_w >= 0) close(wk_w);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        rd_serve(tls, fd, shown_ip);
        _exit(0);
    }
    close(fd);
}

/* ---- principal ---- */

static volatile sig_atomic_t stop, reload;
static void on_term(int s) { (void)s; stop = 1; }
static void on_hup(int s) { (void)s; reload = 1; }

int main(int argc, char **argv) {
    int no_monitor = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-monitor")) no_monitor = 1;   /* solo el escritorio remoto (pruebas) */
        else { fprintf(stderr, "uso: lord-sessiond [--no-monitor]\n"); return 2; }
    }
    if (geteuid() != 0) { fprintf(stderr, "lord-sessiond: tiene que correr como root (lordos.service)\n"); return 1; }
    char prefix[64];
    snprintf(prefix, sizeof prefix, "lord-sessiond[%d]", (int)getpid());
    sd_log_prefix(prefix);
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa = { .sa_handler = on_term };
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    struct sigaction sh = { .sa_handler = on_hup };
    sigaction(SIGHUP, &sh, NULL);
    find_paths();
    load_config();
    mkdir("/run/lordos", 0755);
    /* cgroup delegado (Delegate=yes): sessiond pasa a la hoja «supervisor» y cada sesión recibe
     * su subárbol (cgroup_delegate_session en sess.c). Sin delegación sigue igual que antes. */
    if (!cgroup_service_init()) {
        sd_log("cgroup: servicio delegado; cada sesión tendrá su subárbol");
        sd_cgroup_start();          /* app scopes are root's: placed on request (cgsup.c) */
    }

    rd_shared = mmap(NULL, sizeof *rd_shared, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (rd_shared == MAP_FAILED) return 1;
    memset(rd_shared, 0, sizeof *rd_shared);
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&ma, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&rd_shared->lock, &ma);
    /* sesiones que siguieron vivas desde antes de un reinicio del servicio */
    for (uid_t u = 1000; u < 1100; u++) {
        int fd = sd_session_connect(u);
        if (fd < 0) continue;
        struct ucred cr;
        socklen_t cl = sizeof cr;
        struct passwd *pw = getpwuid(u);
        if (pw && !getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &cl) && cr.uid == u) {
            for (int i = 0; i < SD_MAX_SESSIONS; i++) {
                if (rd_shared->sessions[i].pid) continue;
                rd_shared->sessions[i] = (SdSession){ .pid = cr.pid, .uid = u, .since = rd_now() };
                snprintf(rd_shared->sessions[i].user, sizeof rd_shared->sessions[i].user, "%s", pw->pw_name);
                break;
            }
            sd_log("la sesión de %s sigue abierta", pw->pw_name);
        }
        close(fd);
    }

    /* GPU de las sesiones: lordos.gpu=0 en la línea del kernel (modo seguro) o el LORD_GPU del
     * servicio; si no, cada sesión la usa cuando está en el monitor */
    {
        char cmd[2048] = "";
        FILE *f = fopen("/proc/cmdline", "r");
        if (f) { if (!fgets(cmd, sizeof cmd, f)) cmd[0] = 0; fclose(f); }
        const char *g = getenv("LORD_GPU");
        if (strstr(cmd, "lordos.gpu=0")) snprintf(sd_gpu_env, sizeof sd_gpu_env, "LORD_GPU=0");
        else if (g && (!strcmp(g, "0") || !strcmp(g, "1"))) snprintf(sd_gpu_env, sizeof sd_gpu_env, "LORD_GPU=%s", g);
    }
    sd_log("sesiones con %s%s%s", rd_conf.server, sd_gpu_env[0] ? ", " : "", sd_gpu_env);
    int wk[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, wk) == 0) { wk_r = wk[0]; wk_w = wk[1]; }
    /* the network belongs to the system: lord-netd brings it up before the greeter (netsup.c) */
    sd_netd_start();
    has_monitor = !no_monitor && sd_mon_open() == 0;
    if (has_monitor) boot_monitor();
    if (rd_conf.enabled) start_remote();
    write_status();

    unsigned seen = 0;
    while (!stop) {
        if (reload) {
            reload = 0;
            int was = rd_conf.enabled, port = rd_conf.port;
            load_config();
            sd_log("configuración: remoto %s, puerto %d", rd_conf.enabled ? "prendido" : "apagado", rd_conf.port);
            if (listening && (!rd_conf.enabled || port != rd_conf.port)) stop_remote();
            if (rd_conf.enabled && (!listening || !was)) start_remote();
            bump();
        }
        struct pollfd p[5] = { { wk_r, POLLIN, 0 }, { lfd, POLLIN, 0 }, { sd_mon_fd(), POLLIN, 0 }, { sd_mon_hotplug_fd(), POLLIN, 0 },
                               { sd_netd_fd(), POLLIN, 0 } };
        int r = poll(p, 5, 1000);
        pid_t pid;
        int st;
        while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
            if (sd_netd_reaped(pid, st)) continue;     /* lord-netd (netsup.c) */
            if (sd_cgroup_reaped(pid, st)) continue;   /* cgroup placement service (cgsup.c) */
            if (pid == worker_pid) {
                worker_pid = -1;
                WorkKind k = pending;
                pending = W_NONE;
                if (k == W_GREETER) show_greeter(greeter_mode, greeter_user);
                else if (k == W_AUTOLOGIN && start_worker(W_AUTOLOGIN, pending_user, NULL)) pending = k;
                else if (WIFEXITED(st) && WEXITSTATUS(st) && shown != 'u' && shown != 'g') show_greeter("login", NULL);
            } else {
                rd_lock();
                for (int i = 0; i < RD_MAX_CONNS; i++)
                    if (rd_shared->conns[i].pid == pid) { memset(&rd_shared->conns[i], 0, sizeof rd_shared->conns[i]); rd_shared->version++; }
                rd_unlock();
            }
        }
        if (r > 0 && p[4].fd >= 0 && p[4].revents) sd_netd_event();
        sd_netd_tick();
        sd_cgroup_tick();
        if (r > 0 && (p[0].revents & POLLIN)) from_worker();
        if (r > 0 && lfd >= 0 && (p[1].revents & POLLIN)) accept_remote();
        if (r > 0 && p[2].fd >= 0 && p[2].fd == sd_mon_fd() && p[2].revents) monitor_event();
        if (r > 0 && p[3].fd >= 0 && (p[3].revents & POLLIN)) sd_mon_hotplug();     /* enchufar en caliente */
        if (rd_shared->version != seen) { seen = rd_shared->version; write_status(); }
        else {
            static double last;
            if (rd_now() - last > 5) { last = rd_now(); write_status(); }   /* sesiones que terminaron */
        }
    }
    /* al apagarse: corta el remoto y suelta el monitor; las sesiones siguen vivas (al volver se
     * entra de nuevo por la pantalla de ingreso) */
    stop_remote();
    if (has_monitor) sd_mon_close();
    if (worker_pid > 0) kill(worker_pid, SIGTERM);
    listening = 0;
    rd_lock();
    rd_shared->monitor[0] = 0;
    rd_unlock();
    write_status();
    return 0;
}

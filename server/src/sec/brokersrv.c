/* brokersrv.c — lado servidor del broker (ver broker.h). Corre dentro de lord-server (el
 * compositor). Atiende los pedidos de cada app .lpk aislada, verifica los permisos
 * (perms.c) y, cuando hace falta consentimiento, le pide a la interfaz que dibuje el aviso del
 * sistema (nunca lo dibuja la app). */
#include "broker_int.h"
#include "perms.h"
#include "json.h"
#include "../lpk.h"
#include "../term/term.h"
#include "../media/media_int.h"
#include "../media/aring.h"
#include "../keyboard.h"
#include "../vault/vclient.h"
#include "../pkg/crypto.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_APPS 32
#define MAX_PENDING 32

typedef struct { int fd, pid, used, resync; char id[48]; MAudio *mic; int mic_ringfd; int vault; } AppConn;
static AppConn apps[MAX_APPS];

/* avisos pendientes de respuesta del usuario (reqid → app + fd + permiso) */
typedef struct { int used; uint32_t reqid; int fd; char id[48]; Perm perm; } Pending;
static Pending pend[MAX_PENDING];

/* cola de eventos para JS (prompts a mostrar, cambios de indicador) */
typedef struct { char kind[16]; char json[256]; } Evt;
static Evt evq[64];
static int nevt;

static void queue_evt(const char *kind, const char *fmt, ...) {
    if (nevt >= 64) return;
    Evt *e = &evq[nevt++];
    snprintf(e->kind, sizeof e->kind, "%s", kind);
    va_list ap; va_start(ap, fmt); vsnprintf(e->json, sizeof e->json, fmt, ap); va_end(ap);
}

/* ---- estado publicado por el tema (tokens, idioma) y teclado: en vivo hacia las apps ---- */

#define STATE_MAX (64 * 1024)
static char last_theme[STATE_MAX], last_locale[4096];
static char sent_kb[512];

static void keyboard_json(char *out, size_t n) {
    char l[160], v[160];
    json_quote(l, sizeof l, kb_layouts() ? kb_layouts() : "");
    json_quote(v, sizeof v, kb_variants() ? kb_variants() : "");
    snprintf(out, n, "{\"layouts\":%s,\"variants\":%s,\"group\":%d}", l, v, kb_group());
}

/* Todo el estado en un JSON: { theme, locale, keyboard } (null lo que todavía no se publicó). */
static size_t state_json(char *out, size_t n) {
    char kb[512];
    keyboard_json(kb, sizeof kb);
    int r = snprintf(out, n, "{\"theme\":%s,\"locale\":%s,\"keyboard\":%s}",
                     last_theme[0] ? last_theme : "null", last_locale[0] ? last_locale : "null", kb);
    return r > 0 && (size_t)r < n ? (size_t)r : 0;
}

/* Evento sin pedido. Nunca bloquea: si el socket está lleno, la app queda para resincronizar. */
static int send_event(AppConn *a, const char *kind, const char *json, size_t len) {
    BrokerHdr h = { .reqid = 0, .op = BOP_EVENT, .status = 0, .len = (uint32_t)(strlen(kind) + 1 + len) };
    struct iovec iov[3] = { { &h, sizeof h }, { (void *)kind, strlen(kind) + 1 }, { (void *)json, len } };
    struct msghdr m = { .msg_iov = iov, .msg_iovlen = 3 };
    if (sendmsg(a->fd, &m, MSG_NOSIGNAL | MSG_DONTWAIT) < 0) { a->resync = 1; return -1; }
    return 0;
}

int brokersrv_publish(const char *kind, const char *json, size_t len) {
    char *dst;
    size_t cap;
    if (!strcmp(kind, "theme")) { dst = last_theme; cap = sizeof last_theme; }
    else if (!strcmp(kind, "locale")) { dst = last_locale; cap = sizeof last_locale; }
    else return -1;
    if (len + 1 > cap) return -1;
    if (!strncmp(dst, json, len) && dst[len] == 0) return 0;          /* sin cambios */
    memcpy(dst, json, len);
    dst[len] = 0;
    for (int i = 0; i < MAX_APPS; i++) if (apps[i].used) send_event(&apps[i], kind, json, len);
    return 0;
}

static int send_reply_fd(int fd, uint32_t reqid, int status, const void *data, uint32_t len, int passfd);
static void clip_secret_tick(void);
static int send_reply(int fd, uint32_t reqid, int status, const void *data, uint32_t len);

/* picker (powerbox): avisos pendientes de que el usuario elija un archivo */
typedef struct { int used, save; uint32_t reqid; int fd; char id[48]; } Pick;
static Pick picks[MAX_PENDING];

void brokersrv_pick_result(uint32_t reqid, const char *path) {
    if (brokercam_pick_result(reqid, path)) return;     /* a camera photo/video save */
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!picks[i].used || picks[i].reqid != reqid) continue;
        int f = -1;
        const char *name = path ? strrchr(path, '/') : NULL;
        name = name ? name + 1 : path;
        if (path && path[0]) {
            /* lord-server abre el archivo con los permisos del usuario; la app recibe solo el fd */
            f = open(path, picks[i].save ? (O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC) : (O_RDONLY | O_CLOEXEC), 0644);
        }
        if (f >= 0) { send_reply_fd(picks[i].fd, reqid, BST_OK, name, (uint32_t)strlen(name), f); close(f); }
        else send_reply(picks[i].fd, reqid, BST_DENIED, NULL, 0);
        picks[i].used = 0;
        return;
    }
}

/* últimos menús publicados por una app (el tema los lee con brokersrv_menus) */
static char last_menu[32 * 1024], last_menu_app[48];
const char *brokersrv_menus(const char *app) { return !strcmp(app, last_menu_app) ? last_menu : "[]"; }

void brokersrv_menu_click(const char *app, const char *item) {
    char json[256], q[200];
    json_quote(q, sizeof q, item);
    snprintf(json, sizeof json, "{\"item\":%s}", q);
    for (int i = 0; i < MAX_APPS; i++) if (apps[i].used && !strcmp(apps[i].id, app)) send_event(&apps[i], "menu", json, strlen(json));
}

void brokersrv_revoke(const char *app, const char *perm) {
    char json[128];
    snprintf(json, sizeof json, "{\"perm\":\"%s\"}", perm);
    for (int i = 0; i < MAX_APPS; i++) if (apps[i].used && !strcmp(apps[i].id, app)) send_event(&apps[i], "revoke", json, strlen(json));
    if (!strcmp(perm, "microphone")) for (int i = 0; i < MAX_APPS; i++)
        if (apps[i].used && !strcmp(apps[i].id, app) && apps[i].mic) { maudio_close(apps[i].mic); apps[i].mic = NULL; }
    if (!strcmp(perm, "camera")) brokercam_revoke(app);     /* kills its lord-media: the stream stops now */
    if (!strcmp(perm, "camera") || !strcmp(perm, "microphone"))
        queue_evt("device", "{\"app\":\"%s\",\"kind\":\"%s\",\"on\":false}", app, perm);
}

void brokersrv_tick(void) {
    brokercam_tick();
    clip_secret_tick();
    char kb[512];
    keyboard_json(kb, sizeof kb);
    int kb_changed = strcmp(kb, sent_kb) != 0;
    if (kb_changed) snprintf(sent_kb, sizeof sent_kb, "%s", kb);
    for (int i = 0; i < MAX_APPS; i++) {
        AppConn *a = &apps[i];
        if (!a->used) continue;
        if (a->resync) {
            static char st[STATE_MAX + 8192];
            size_t n = state_json(st, sizeof st);
            a->resync = 0;
            if (n) send_event(a, "state", st, n);
        } else if (kb_changed) send_event(a, "keyboard", kb, strlen(kb));
    }
}

int brokersrv_add(int fd, const char *id, int pid) {
    for (int i = 0; i < MAX_APPS; i++) if (!apps[i].used) {
        apps[i] = (AppConn){ .fd = fd, .pid = pid, .used = 1 };
        snprintf(apps[i].id, sizeof apps[i].id, "%s", id);
        return 0;
    }
    return -1;
}

/* The app is already running: ask its process for another window (Dock → «New window»). */
int brokersrv_new_window(const char *id) {
    static const char json[] = "{\"new\":true}";
    for (int i = 0; i < MAX_APPS; i++)
        if (apps[i].used && !strcmp(apps[i].id, id)) return send_event(&apps[i], "window", json, sizeof json - 1) ? -1 : 0;
    return -1;
}

void brokersrv_set_vault(int fd) { for (int i = 0; i < MAX_APPS; i++) if (apps[i].used && apps[i].fd == fd) apps[i].vault = 1; }

/* A secret an isolated app put on the clipboard: cleared at `deadline` if it is still there
 * (compared by hash, the text itself is not kept). */
static struct { double deadline; uint8_t hash[32]; } clip_secret;

static double mono_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static void clip_secret_tick(void) {
    if (!clip_secret.deadline || mono_s() < clip_secret.deadline) return;
    clip_secret.deadline = 0;
    const char *cur = term_clipboard();
    uint8_t h[32];
    sha256(cur ? cur : "", cur ? strlen(cur) : 0, h);
    if (!memcmp(h, clip_secret.hash, 32)) { term_set_clipboard("", 0); term_clipboard_mark("lordos", 0); }
    explicit_bzero(h, sizeof h);
}

int brokersrv_count(void) { int n = 0; for (int i = 0; i < MAX_APPS; i++) n += apps[i].used; return n; }

static void drop(AppConn *a) {
    brokercam_conn_closed((int)(a - apps));
    /* los indicadores de esta app se apagan */
    queue_evt("device", "{\"app\":\"%s\",\"off\":true}", a->id);
    if (a->mic) { maudio_close(a->mic); a->mic = NULL; }
    close(a->fd);
    a->used = 0;
}

static int send_reply_fd(int fd, uint32_t reqid, int status, const void *data, uint32_t len, int passfd) {
    BrokerHdr h = { .reqid = reqid, .op = 0, .status = (uint16_t)status, .len = len };
    struct iovec iov[2] = { { &h, sizeof h }, { (void *)data, len } };
    struct msghdr m = { .msg_iov = iov, .msg_iovlen = len ? 2 : 1 };
    char cmsg[CMSG_SPACE(sizeof(int))];
    if (passfd >= 0) {
        memset(cmsg, 0, sizeof cmsg);
        m.msg_control = cmsg;
        m.msg_controllen = sizeof cmsg;
        struct cmsghdr *c = CMSG_FIRSTHDR(&m);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &passfd, sizeof(int));
    }
    return sendmsg(fd, &m, MSG_NOSIGNAL | MSG_DONTWAIT) < 0 ? -1 : 0;     /* nunca esperar a una app */
}
static int send_reply(int fd, uint32_t reqid, int status, const void *data, uint32_t len) {
    return send_reply_fd(fd, reqid, status, data, len, -1);
}

/* Ruta segura dentro de la carpeta de datos de la app. */
static int data_path(const char *id, const char *rel, char *out, size_t n) {
    const char *home = getenv("HOME");
    if (!home || !rel || rel[0] == '/' || strstr(rel, "..")) return -1;
    char base[PATH_MAX], real[PATH_MAX];
    if ((size_t)snprintf(base, sizeof base, "%s/.local/share/lordos/appdata/%s", home, id) >= sizeof base) return -1;
    if ((size_t)snprintf(out, n, "%s/%s", base, rel) >= n) return -1;
    /* el padre tiene que caer dentro de base (sin symlinks que escapen) */
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", out);
    char *slash = strrchr(tmp, '/');
    if (slash) { *slash = 0; if (realpath(tmp, real) && strncmp(real, base, strlen(base))) return -1; }
    return 0;
}

static void handle(AppConn *a, BrokerHdr *h, char *payload) {
    int fd = a->fd;
    switch (h->op) {
    case BOP_FS_HOME: {
        char p[PATH_MAX];
        snprintf(p, sizeof p, "%s/.local/share/lordos/appdata/%s", getenv("HOME"), a->id);
        send_reply(fd, h->reqid, BST_OK, p, (uint32_t)strlen(p));
        break;
    }
    case BOP_FS_READ: {
        char path[PATH_MAX];
        if (data_path(a->id, payload, path, sizeof path)) { send_reply(fd, h->reqid, BST_DENIED, NULL, 0); break; }
        int f = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat st;
        if (f < 0 || fstat(f, &st) || !S_ISREG(st.st_mode) || st.st_size > BROKER_MAX_MSG) { if (f >= 0) close(f); send_reply(fd, h->reqid, BST_ERROR, NULL, 0); break; }
        char *buf = malloc((size_t)st.st_size);
        ssize_t got = buf ? read(f, buf, (size_t)st.st_size) : -1;
        close(f);
        if (got == st.st_size) send_reply(fd, h->reqid, BST_OK, buf, (uint32_t)got); else send_reply(fd, h->reqid, BST_ERROR, NULL, 0);
        free(buf);
        break;
    }
    case BOP_FS_WRITE: {
        char *nul = memchr(payload, 0, h->len);
        if (!nul) { send_reply(fd, h->reqid, BST_ERROR, NULL, 0); break; }
        char path[PATH_MAX];
        if (data_path(a->id, payload, path, sizeof path)) { send_reply(fd, h->reqid, BST_DENIED, NULL, 0); break; }
        const char *data = nul + 1;
        size_t dlen = h->len - (size_t)(nul + 1 - payload);
        int f = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
        int ok = f >= 0 && write(f, data, dlen) == (ssize_t)dlen;
        if (f >= 0) close(f);
        send_reply(fd, h->reqid, ok ? BST_OK : BST_ERROR, NULL, 0);
        break;
    }
    case BOP_FS_LIST: {
        /* [{ "name", "dir" }] de una carpeta dentro de la carpeta de datos de la app */
        char path[PATH_MAX];
        const char *rel = payload[0] && strcmp(payload, ".") ? payload : "";
        if (rel[0] ? data_path(a->id, rel, path, sizeof path) : data_path(a->id, ".", path, sizeof path)) { send_reply(fd, h->reqid, BST_DENIED, NULL, 0); break; }
        DIR *d = opendir(path);
        if (!d) { send_reply(fd, h->reqid, BST_ERROR, NULL, 0); break; }
        size_t cap = 64 * 1024, len = 0;
        char *out = malloc(cap);
        if (!out) { closedir(d); send_reply(fd, h->reqid, BST_ERROR, NULL, 0); break; }
        out[len++] = '[';
        struct dirent *e;
        int n = 0;
        while ((e = readdir(d)) && len + 600 < cap && n < 2000) {
            if (e->d_name[0] == '.') continue;
            char q[560];
            json_quote(q, sizeof q, e->d_name);
            len += (size_t)snprintf(out + len, cap - len, "%s{\"name\":%s,\"dir\":%s}", n ? "," : "", q, e->d_type == DT_DIR ? "true" : "false");
            n++;
        }
        closedir(d);
        out[len++] = ']';
        send_reply(fd, h->reqid, BST_OK, out, (uint32_t)len);
        free(out);
        break;
    }
    case BOP_CLIP_GET: {
        if (perm_get(a->id, PERM_CLIPBOARD) != PERM_ALLOW) { send_reply(fd, h->reqid, BST_DENIED, NULL, 0); break; }
        const char *txt = term_clipboard();
        send_reply(fd, h->reqid, BST_OK, txt ? txt : "", txt ? (uint32_t)strlen(txt) : 0);
        break;
    }
    case BOP_CLIP_SET:
        term_set_clipboard(payload, h->len);
        send_reply(fd, h->reqid, BST_OK, NULL, 0);
        break;
    case BOP_PERM: {
        int p = perm_from_name(payload);
        if (p < 0) { send_reply(fd, h->reqid, BST_DENIED, NULL, 0); break; }
        PermState st = perm_get(a->id, (Perm)p);
        fprintf(stderr, "broker[%s]: permiso %s = %s\n", a->id, payload, st == PERM_ALLOW ? "allow" : st == PERM_DENY ? "deny" : "ask→aviso");
        if (st == PERM_ALLOW) { send_reply(fd, h->reqid, BST_OK, "1", 1); break; }
        if (st == PERM_DENY) { send_reply(fd, h->reqid, BST_OK, "0", 1); break; }
        /* preguntar: el aviso lo dibuja el sistema; se responde cuando el usuario decide */
        for (int i = 0; i < MAX_PENDING; i++) if (!pend[i].used) {
            pend[i] = (Pending){ .used = 1, .reqid = h->reqid, .fd = fd, .perm = (Perm)p };
            snprintf(pend[i].id, sizeof pend[i].id, "%s", a->id);
            queue_evt("prompt", "{\"reqid\":%u,\"app\":\"%s\",\"perm\":\"%s\"}", h->reqid, a->id, perm_name((Perm)p));
            return;
        }
        send_reply(fd, h->reqid, BST_DENIED, NULL, 0);
        break;
    }
    case BOP_DEVICE: {
        /* "camera on" / "microphone off": solo si tiene el permiso */
        char kind[16] = ""; int on = 0;
        sscanf(payload, "%15s %d", kind, &on);
        int p = perm_from_name(kind);
        if (p >= 0 && perm_get(a->id, (Perm)p) == PERM_ALLOW)
            queue_evt("device", "{\"app\":\"%s\",\"kind\":\"%s\",\"on\":%s}", a->id, kind, on ? "true" : "false");
        send_reply(fd, h->reqid, BST_OK, NULL, 0);
        break;
    }
    case BOP_PICK: {
        /* payload: "save\0nombreSugerido"; el diálogo lo dibuja lord-server, no la app */
        int save = payload[0] == '1';
        const char *suggest = memchr(payload, 0, h->len);
        suggest = suggest ? suggest + 1 : "";
        for (int i = 0; i < MAX_PENDING; i++) if (!picks[i].used) {
            picks[i] = (Pick){ .used = 1, .save = save, .reqid = h->reqid, .fd = fd };
            snprintf(picks[i].id, sizeof picks[i].id, "%s", a->id);
            char sq[300];
            json_quote(sq, sizeof sq, suggest);
            queue_evt("pick", "{\"reqid\":%u,\"app\":\"%s\",\"save\":%s,\"suggest\":%s}", h->reqid, a->id, save ? "true" : "false", sq);
            return;
        }
        send_reply(fd, h->reqid, BST_DENIED, NULL, 0);
        break;
    }
    case BOP_MENU:
        queue_evt("menu", "{\"app\":\"%s\"}", a->id);          /* los menús van aparte (payload grande) */
        /* guardamos el JSON de los menús para que el tema lo lea */
        {
            size_t n = h->len < sizeof last_menu - 1 ? h->len : sizeof last_menu - 1;
            memcpy(last_menu, payload, n); last_menu[n] = 0;
            snprintf(last_menu_app, sizeof last_menu_app, "%s", a->id);
        }
        send_reply(fd, h->reqid, BST_OK, NULL, 0);
        break;
    case BOP_STATE: {
        static char st[STATE_MAX + 8192];
        size_t n = state_json(st, sizeof st);
        send_reply(fd, h->reqid, n ? BST_OK : BST_ERROR, st, (uint32_t)n);
        break;
    }
    case BOP_MIC: {
        int want = payload[0] == '1';
        if (!want) { if (a->mic) { maudio_close(a->mic); a->mic = NULL; } send_reply(fd, h->reqid, BST_OK, NULL, 0); break; }
        if (perm_get(a->id, PERM_MICROPHONE) != PERM_ALLOW) { send_reply(fd, h->reqid, BST_DENIED, NULL, 0); break; }
        if (!a->mic) {
            int rfd = -1;
            a->mic = maudio_open(1, a->id, a->id, NULL, &rfd);
            if (!a->mic) { send_reply(fd, h->reqid, BST_ERROR, NULL, 0); break; }
            a->mic_ringfd = rfd;
        }
        /* fd de SOLO LECTURA del anillo: la app solo lo puede mapear PROT_READ (no toca rpos) */
        char proc[64];
        snprintf(proc, sizeof proc, "/proc/self/fd/%d", a->mic_ringfd);
        int ro = open(proc, O_RDONLY | O_CLOEXEC);
        if (ro < 0) { send_reply(fd, h->reqid, BST_ERROR, NULL, 0); break; }
        /* tamaño y formato del anillo (datos de confianza del servidor, no de la cabecera) */
        char info[64];
        int n = snprintf(info, sizeof info, "%u %u %u", 1u << 18, (unsigned)AR_RATE, 1u);
        send_reply_fd(fd, h->reqid, BST_OK, info, (uint32_t)n, ro);
        close(ro);
        break;
    }
    case BOP_CLIP_SECRET: {
        /* "seconds\0text": sensitive copy (Passwords), cleared later if unchanged */
        char *nul = memchr(payload, 0, h->len);
        if (!nul) { send_reply(fd, h->reqid, BST_ERROR, NULL, 0); break; }
        int secs = atoi(payload);
        const char *txt = nul + 1;
        size_t tl = h->len - (size_t)(txt - payload);
        term_set_clipboard(txt, tl);
        term_clipboard_mark(a->id, 1);
        if (secs > 0 && secs <= 600) {
            sha256(txt, tl, clip_secret.hash);
            clip_secret.deadline = mono_s() + secs;
        }
        explicit_bzero(payload, h->len);
        send_reply(fd, h->reqid, BST_OK, NULL, 0);
        break;
    }
    case BOP_VAULT_FIDO: {
        /* only the system Passwords app (spawned with a vault channel), only with `usb` allowed; the
         * hidraw fd goes from here straight to lord-vault */
        if (!a->vault || perm_get(a->id, PERM_USB) != PERM_ALLOW) { send_reply(fd, h->reqid, BST_DENIED, NULL, 0); break; }
        char name[128] = "";
        int r = vc_fido_attach(name, sizeof name);
        if (r) send_reply(fd, h->reqid, BST_ERROR, r == -1 ? "no-device" : "no-access", r == -1 ? 9 : 9);
        else send_reply(fd, h->reqid, BST_OK, name, (uint32_t)strlen(name));
        break;
    }
    case BOP_QUIT:
        send_reply(fd, h->reqid, BST_OK, NULL, 0);
        break;
    case BOP_CAMERA:
        brokercam_handle((int)(a - apps), fd, h->reqid, payload, h->len);
        break;
    default:
        send_reply(fd, h->reqid, BST_ERROR, NULL, 0);
        break;
    }
}

void brokersrv_resolve(uint32_t reqid, int allow) {
    for (int i = 0; i < MAX_PENDING; i++) if (pend[i].used && pend[i].reqid == reqid) {
        perm_set(pend[i].id, pend[i].perm, allow ? PERM_ALLOW : PERM_DENY);
        send_reply(pend[i].fd, reqid, BST_OK, allow ? "1" : "0", 1);
        pend[i].used = 0;
        return;
    }
}

int brokersrv_pollfds(struct pollfd *pfd, int max) {
    int n = 0;
    for (int i = 0; i < MAX_APPS && n < max; i++) if (apps[i].used) { pfd[n].fd = apps[i].fd; pfd[n].events = POLLIN; pfd[n].revents = 0; n++; }
    return n;
}

void brokersrv_dispatch(struct pollfd *pfd, int n) {
    int k = 0;
    for (int i = 0; i < MAX_APPS && k < n; i++) {
        if (!apps[i].used) continue;
        struct pollfd *p = &pfd[k++];
        if (!(p->revents & (POLLIN | POLLHUP | POLLERR))) continue;
        BrokerHdr h;
        char *buf = malloc(BROKER_MAX_MSG + 1);
        if (!buf) continue;
        struct iovec iov[2] = { { &h, sizeof h }, { buf, BROKER_MAX_MSG } };
        struct msghdr m = { .msg_iov = iov, .msg_iovlen = 2 };
        ssize_t r = recvmsg(apps[i].fd, &m, 0);
        if (r < (ssize_t)sizeof h) { free(buf); drop(&apps[i]); continue; }
        uint32_t len = (uint32_t)r - sizeof h;
        if (h.len != len || len > BROKER_MAX_MSG) { free(buf); drop(&apps[i]); continue; }
        buf[len] = 0;
        handle(&apps[i], &h, buf);
        free(buf);
    }
}

int brokersrv_audio_pollfds(struct pollfd *pfd, int max) {
    int n = 0;
    for (int i = 0; i < MAX_APPS && n < max; i++) {
        short ev;
        int f = apps[i].used && apps[i].mic ? maudio_pollfd(apps[i].mic, &ev) : -1;
        if (f >= 0) { pfd[n].fd = f; pfd[n].events = ev; pfd[n].revents = 0; n++; }
    }
    return n;
}
void brokersrv_audio_dispatch(struct pollfd *pfd, int n) {
    int k = 0;
    for (int i = 0; i < MAX_APPS && k < n; i++) {
        if (!apps[i].used || !apps[i].mic) continue;
        short ev;
        int f = maudio_pollfd(apps[i].mic, &ev);
        if (f >= 0 && (pfd[k].revents || 1)) { maudio_service(apps[i].mic); maudio_mic_drain(apps[i].mic); }
        if (f >= 0) k++;
    }
}

/* ---- for brokercam.c (broker_int.h) ---- */

const char *brokersrv_conn_app(int conn) { return conn >= 0 && conn < MAX_APPS && apps[conn].used ? apps[conn].id : NULL; }

int brokersrv_conn_event(int conn, const char *kind, const char *data, size_t len) {
    if (conn < 0 || conn >= MAX_APPS || !apps[conn].used) return -1;
    BrokerHdr h = { .reqid = 0, .op = BOP_EVENT, .status = 0, .len = (uint32_t)(strlen(kind) + 1 + len) };
    struct iovec iov[3] = { { &h, sizeof h }, { (void *)kind, strlen(kind) + 1 }, { (void *)data, len } };
    struct msghdr m = { .msg_iov = iov, .msg_iovlen = 3 };
    return sendmsg(apps[conn].fd, &m, MSG_NOSIGNAL | MSG_DONTWAIT) < 0 ? -1 : 0;   /* no resync for frames */
}

int brokersrv_reply(int fd, uint32_t reqid, int status, const void *data, uint32_t len, int passfd) {
    return send_reply_fd(fd, reqid, status, data, len, passfd);
}

void brokersrv_device(const char *app, const char *kind, int on) {
    queue_evt("device", "{\"app\":\"%s\",\"kind\":\"%s\",\"on\":%s}", app, kind, on ? "true" : "false");
}

void brokersrv_powerbox_save(uint32_t reqid, const char *app, const char *suggest) {
    char sq[300];
    json_quote(sq, sizeof sq, suggest);
    queue_evt("pick", "{\"reqid\":%u,\"app\":\"%s\",\"save\":true,\"suggest\":%s}", reqid, app, sq);
}

void brokersrv_events(BrokerEventFn fn) {
    for (int i = 0; i < nevt; i++) fn(evq[i].kind, evq[i].json);
    nevt = 0;
}

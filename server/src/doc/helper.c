/* helper.c — lord-doc: ayudante de documentos de lord-server (ver proto.h y docs/DOCUMENTOS.md).
 *
 * Lo lanza lord-server (doc.c) con una ruta fija (al lado de su propio binario), sin argumentos y
 * con el canal en el fd 3. Corre como el usuario, y antes de leer un solo byte de un documento:
 *   - muere si muere lord-server (PR_SET_PDEATHSIG) y no deja core;
 *   - límite de memoria (RLIMIT_AS) y cgroup propio (lordos-doc.scope) si se puede;
 *   - recibe los fds que va a usar (DQ_INIT_FD) y se encierra (DQ_INIT_GO) con el módulo común
 *     sandbox_apply_helper (server/src/sec/sandbox.c):
 *       Landlock: solo lectura de /usr y las fuentes; escritura solo debajo de la carpeta de
 *       destino de una extracción (rw_dir_fds); ninguna conexión TCP;
 *       seccomp: lista blanca sin socket, connect, bind, execve, ptrace, fork (solo hilos) ni mount;
 *       sin capacidades y con no_new_privs.
 * Si algo de eso falta en el kernel, sigue con lo que haya y lo dice en el registro.
 */
#include "helper.h"
#include "../sec/cgroup.h"
#include "../sec/sandbox.h"

#include <errno.h>
#include <locale.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sched.h>
#include <unistd.h>


#define SOCK 3

Slot hslots[DOC_INIT_FDS];
int hnslots;
static const char *mode = "";

int hsock(void) { return SOCK; }

int slot_fd(const JVal *args, const char *key) {
    double d = json_num(args, key, -1);
    if (d < 0 || d >= hnslots || d != (double)(int)d) return -1;
    return hslots[(int)d].fd;
}

/* ---- respuestas ---- */

void reply_json(uint32_t kind, uint32_t id, JBuf *b) {
    if (b->oom) { jb_free(b); reply_err(id, "too-big", ""); return; }
    doc_send(SOCK, kind, id, b->p ? b->p : "{}", b->p ? b->n : 2, NULL, 0);
    jb_free(b);
}

void reply_err(uint32_t id, const char *code, const char *detail) {
    JBuf b;
    jb_init(&b);
    jb_raw(&b, "{\"error\":");
    jb_str(&b, code);
    jb_raw(&b, ",\"detail\":");
    jb_str(&b, detail ? detail : "");
    jb_raw(&b, "}");
    doc_send(SOCK, DR_ERR, id, b.p, b.n, NULL, 0);
    jb_free(&b);
}

void reply_done_empty(uint32_t id) { doc_send(SOCK, DR_DONE, id, "{}", 2, NULL, 0); }

void reply_image_fd(uint32_t id, int fd, int w, int h, const char *extra) {
    if (fd < 0 || doc_seal(fd)) { if (fd >= 0) close(fd); reply_err(id, "memory", ""); return; }
    JBuf b;
    jb_init(&b);
    jb_printf(&b, "{\"w\":%d,\"h\":%d%s}", w, h, extra ? extra : "");
    doc_send(SOCK, DR_IMG, id, b.p, b.n, &fd, 1);
    jb_free(&b);
    close(fd);
}

void reply_image(uint32_t id, const uint32_t *px, int w, int h, const char *extra) {
    int fd = doc_memfd_new("lord-doc-img", (size_t)w * h * 4);
    if (fd < 0) { reply_err(id, "memory", ""); return; }
    size_t len = (size_t)w * h * 4, off = 0;
    const char *p = (const char *)px;
    while (off < len) {
        ssize_t k = pwrite(fd, p + off, len - off, (off_t)off);
        if (k <= 0) { if (k < 0 && errno == EINTR) continue; close(fd); reply_err(id, "memory", ""); return; }
        off += (size_t)k;
    }
    reply_image_fd(id, fd, w, h, extra);
}

void reply_blob(uint32_t id, int fd, size_t size) {
    JBuf b;
    jb_init(&b);
    jb_printf(&b, "{\"size\":%zu}", size);
    doc_send(SOCK, DR_BLOB, id, b.p, b.n, &fd, 1);
    jb_free(&b);
    close(fd);
}

/* Luminancia invertida con el mismo matiz: cada canal se corre (255 - 2Y). El blanco queda
 * negro, el negro blanco y un rojo sigue siendo rojo (más claro o más oscuro). */
void invert_lightness(uint32_t *px, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint32_t p = px[i];
        int a = (int)(p >> 24), r = (int)(p >> 16 & 255), g = (int)(p >> 8 & 255), b = (int)(p & 255);
        if (!a) continue;
        int y = (r * 77 + g * 150 + b * 29) >> 8;
        int d = a - 2 * y;                         /* premultiplicado: el máximo es a */
        r += d; g += d; b += d;
        r = r < 0 ? 0 : r > a ? a : r;
        g = g < 0 ? 0 : g > a ? a : g;
        b = b < 0 ? 0 : b > a ? a : b;
        px[i] = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
}

/* Encierro con el módulo común de aislamiento (server/src/sec/sandbox.c): sin red, sin archivos
 * salvo los fds recibidos y las carpetas de solo lectura de abajo, lista blanca de syscalls por
 * arquitectura, sin capacidades y con tope de memoria. */
static void lockdown(void) {
    static const char *ro[16];
    static char extra[3][1024];
    int n = 0;
    ro[n++] = "/usr";                      /* bibliotecas, poppler-data, fuentes, locales */
    ro[n++] = "/etc/fonts";
    ro[n++] = "/var/cache/fontconfig";
    const char *home = getenv("HOME");
    if (home && home[0] == '/') {
        static const char *const rel[] = { ".local/share/fonts", ".fonts", ".cache/fontconfig" };
        for (int i = 0; i < 3; i++) { snprintf(extra[i], sizeof extra[i], "%s/%s", home, rel[i]); ro[n++] = extra[i]; }
    }
    const char *assets = getenv("LORD_DOC_FONTS");
    if (assets && assets[0] == '/') ro[n++] = assets;
    ro[n] = NULL;
    static const int sys[] = {
        SYS_mkdirat, SYS_symlinkat, SYS_linkat, SYS_unlinkat, SYS_fchmod, SYS_utimensat, SYS_fstatfs, SYS_eventfd2,
        SYS_pipe2, SYS_getcwd, SYS_umask, SYS_fadvise64, SYS_memfd_create, SYS_ftruncate, SYS_readlinkat, -1 };
    static int rofd[DOC_INIT_FDS + 1], rwfd[DOC_INIT_FDS + 1];
    int nro = 0, nrw = 0;
    for (int i = 0; i < hnslots; i++) {
        struct stat st;
        if (fstat(hslots[i].fd, &st) || !S_ISDIR(st.st_mode)) continue;
        if (!strcmp(hslots[i].role, "rw")) rwfd[nrw++] = hslots[i].fd;
        else if (!strcmp(hslots[i].role, "ro")) rofd[nro++] = hslots[i].fd;
    }
    rofd[nro] = rwfd[nrw] = -1;
    SandboxHelperCfg cfg = { .name = "lordos-doc", .ro_paths = ro, .extra_syscalls = sys, .mem_max_mb = 1024,
                             .ro_dir_fds = rofd, .rw_dir_fds = rwfd };
    int mask = sandbox_apply_helper(&cfg);
    if (getenv("LORD_DOC_DEBUG")) fprintf(stderr, "lord-doc[%d]: modo=%s caja=%#x fds=%d\n", getpid(), mode, (unsigned)mask, hnslots);
}

/* ---- arranque y bucle ---- */

/* Lo que el encierro tiene que impedir. Cada intento devuelve el errno (0 = se pudo: falla). */
static void selftest(uint32_t id) {
    JBuf b;
    jb_init(&b);
    jb_raw(&b, "{");
    char p[1024];
    const char *home = getenv("HOME");
    snprintf(p, sizeof p, "%s/.ssh", home ? home : "/root");
    int fd = open(p, O_RDONLY | O_DIRECTORY);
    jb_printf(&b, "\"ssh\":%d", fd >= 0 ? 0 : errno);
    if (fd >= 0) close(fd);
    snprintf(p, sizeof p, "%s/.bashrc", home ? home : "/root");
    fd = open(p, O_RDONLY);
    jb_printf(&b, ",\"home_file\":%d", fd >= 0 ? 0 : errno);
    if (fd >= 0) close(fd);
    fd = open("/etc/shadow", O_RDONLY);
    jb_printf(&b, ",\"shadow\":%d", fd >= 0 ? 0 : errno);
    if (fd >= 0) close(fd);
    fd = open("/tmp/lord-doc-selftest", O_WRONLY | O_CREAT, 0600);
    jb_printf(&b, ",\"write_tmp\":%d", fd >= 0 ? 0 : errno);
    if (fd >= 0) { close(fd); unlink("/tmp/lord-doc-selftest"); }
    int s4 = socket(AF_INET, SOCK_STREAM, 0);
    jb_printf(&b, ",\"inet\":%d", s4 >= 0 ? 0 : errno);
    if (s4 >= 0) close(s4);
    int su = socket(AF_UNIX, SOCK_STREAM, 0);
    int cu = -1;
    if (su >= 0) {
        struct sockaddr_un a = { .sun_family = AF_UNIX };
        snprintf(a.sun_path, sizeof a.sun_path, "/run/cups/cups.sock");
        cu = connect(su, (struct sockaddr *)&a, sizeof a) ? errno : 0;
        close(su);
    }
    jb_printf(&b, ",\"unix_socket\":%d,\"unix_connect\":%d", su >= 0 ? 0 : errno, su >= 0 ? cu : -1);
    pid_t pid = (pid_t)syscall(SYS_clone, SIGCHLD, 0, 0, 0, 0);
    if (pid == 0) _exit(0);
    jb_printf(&b, ",\"fork\":%d", pid >= 0 ? 0 : errno);
    if (pid > 0) waitpid(pid, NULL, 0);
    fd = open("/usr/share/fonts", O_RDONLY | O_DIRECTORY);
    jb_printf(&b, ",\"fonts_ok\":%d", fd >= 0 ? 0 : errno);
    if (fd >= 0) close(fd);
    jb_raw(&b, "}");
    reply_json(DR_DONE, id, &b);
}

static int handle(const DocMsg *m) {
    if (m->h.op == DQ_SELFTEST) { selftest(m->h.id); return 0; }
    JDoc doc = { 0 };
    const JVal *args = NULL;
    if (m->json && m->len) {
        char err[96];
        if (json_parse(m->json, m->len, &doc, err, sizeof err)) { reply_err(m->h.id, "bad-request", err); return 0; }
        args = doc.root;
    }
    static const JVal empty = { .type = J_OBJ };
    if (!args) args = &empty;
    int done = 0;
    if (!strcmp(mode, "pdf")) done = pdf_handle(m, args);
    else if (!strcmp(mode, "util")) done = font_handle(m, args);
    else if (!strcmp(mode, "arc")) done = arc_handle(m, args);
    else if (!strcmp(mode, "font")) done = font_handle(m, args);
    else if (!strcmp(mode, "make")) done = make_handle(m, args);
    if (!done) reply_err(m->h.id, "unknown-op", "");
    json_free(&doc);
    return 0;
}

int main(int argc, char **argv) {
    (void)argv;
    if (argc != 1) return 2;                         /* nunca recibe argumentos */
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1) return 1;
    prctl(PR_SET_DUMPABLE, 0);
    struct rlimit core = { 0, 0 };
    setrlimit(RLIMIT_CORE, &core);
    struct rlimit as = { (rlim_t)1536 << 20, (rlim_t)1536 << 20 };
    setrlimit(RLIMIT_AS, &as);
    struct stat st;
    if (fstat(SOCK, &st) || !S_ISSOCK(st.st_mode)) { fprintf(stderr, "lord-doc: lo lanza lord-server\n"); return 2; }
    syscall(SYS_close_range, SOCK + 1, ~0U, 0);
    signal(SIGPIPE, SIG_IGN);
    /* nombres de los comprimidos en UTF-8 (libarchive convierte según la región) */
    if (!setlocale(LC_ALL, "C.UTF-8")) setlocale(LC_ALL, "");

    /* fds de arranque */
    for (int guard = 0; guard < DOC_INIT_FDS + 4; guard++) {
        DocMsg m;
        int r = doc_recv(SOCK, &m);
        if (r) return r > 0 ? 0 : 1;
        if (m.h.op == DQ_INIT_FD) {
            JDoc d = { 0 };
            const char *role = "file";
            if (m.json && !json_parse(m.json, m.len, &d, NULL, 0)) { const char *s = json_str(d.root, "role"); if (s) role = s; }
            for (int i = 0; i < m.nfds && hnslots < DOC_INIT_FDS; i++) {
                hslots[hnslots].fd = m.fds[i];
                snprintf(hslots[hnslots].role, sizeof hslots[hnslots].role, "%s", role);
                hnslots++;
                m.fds[i] = -1;
            }
            json_free(&d);
            doc_msg_free(&m);
            continue;
        }
        if (m.h.op == DQ_INIT_GO) {
            JDoc d = { 0 };
            static char mbuf[16];
            if (m.json && !json_parse(m.json, m.len, &d, NULL, 0)) {
                const char *s = json_str(d.root, "mode");
                if (s) snprintf(mbuf, sizeof mbuf, "%s", s);
            }
            mode = mbuf;
            json_free(&d);
            doc_msg_free(&m);
            break;
        }
        doc_msg_free(&m);
        return 1;
    }
    lockdown();
    doc_send(SOCK, DR_DONE, 0, "{\"ready\":true}", 14, NULL, 0);
    for (int bad = 0; bad < 3;) {
        DocMsg m;
        int r = doc_recv(SOCK, &m);
        if (r > 0) return 0;
        if (r < 0) { bad++; continue; }
        bad = 0;
        handle(&m);
        doc_msg_free(&m);
    }
}

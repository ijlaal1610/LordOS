/* cgroup.c — scope de cgroup v2 por app (ver cgroup.h). Todo con archivos de /sys/fs/cgroup:
 * sin libsystemd ni D-Bus. Degrada con un aviso si falta la delegación o un controlador. */
#include "cgroup.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>

#define CG_ROOT "/sys/fs/cgroup"

static int write_file(const char *path, const char *val) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t n = strlen(val);
    ssize_t w = write(fd, val, n);
    int saved = errno;
    close(fd);
    errno = saved;
    return w == (ssize_t)n ? 0 : -1;
}

/* Ruta del cgroup v2 del proceso (línea "0::<ruta>" de /proc/self/cgroup). */
const char *cgroup_self(void) {
    static char path[PATH_MAX];
    if (path[0]) return path;
    FILE *f = fopen("/proc/self/cgroup", "r");
    if (!f) return "";
    char line[PATH_MAX];
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "0::", 3)) continue;
        char *p = line + 3, *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        snprintf(path, sizeof path, "%s", p);
        break;
    }
    fclose(f);
    return path;
}

/* Directorio donde crear la scope de la app. En la sesión delegada, lord-server corre en la hoja
 * «.../lordos-session-<uid>/server»: las scopes van como hermanas, en el directorio padre (que
 * tiene los controladores habilitados). Si el padre no es escribible, se prueba el cgroup propio
 * (sirve si no tiene controladores). Si ninguno, no hay subárbol delegado. */
static int base_dir(char *out, size_t n) {
    const char *rel = cgroup_self();
    if (!rel[0]) return -1;
    char self[PATH_MAX], parent[PATH_MAX];
    if ((size_t)snprintf(self, sizeof self, CG_ROOT "%s", rel) >= sizeof self) return -1;
    snprintf(parent, sizeof parent, "%s", self);
    char *slash = strrchr(parent, '/');
    if (slash && slash != parent + strlen(CG_ROOT)) {
        *slash = 0;
        if (!access(parent, W_OK)) { snprintf(out, n, "%s", parent); return 0; }
    }
    if (!access(self, W_OK)) { snprintf(out, n, "%s", self); return 0; }
    errno = EACCES;
    return -1;
}

/* Asegura que el controlador esté en cgroup.subtree_control del directorio base (para poder
 * limitar memoria y CPU en las scopes hijas). Mejor esfuerzo. */
static void enable_controllers(const char *base) {
    char path[PATH_MAX];
    if ((size_t)snprintf(path, sizeof path, "%s/cgroup.subtree_control", base) < sizeof path)
        write_file(path, "+memory +cpu +pids");
}

/* Escribe val en el archivo `leaf` dentro del directorio dirfd (openat evita armar rutas largas). */
static int write_at(int dirfd, const char *leaf, const char *val) {
    int fd = openat(dirfd, leaf, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t n = strlen(val);
    ssize_t w = write(fd, val, n);
    int saved = errno;
    close(fd);
    errno = saved;
    return w == (ssize_t)n ? 0 : -1;
}

int cgroup_enter(const char *name, const CgroupLimits *lim) {
    char base[PATH_MAX], scope[PATH_MAX], val[64];
    for (const char *p = name; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '.' || *p == '_' || *p == '@'))
            { fprintf(stderr, "cgroup: nombre inválido %s\n", name); return -1; }
    if (base_dir(base, sizeof base)) { fprintf(stderr, "cgroup: sin subárbol delegado (%s); la app corre sin scope\n", errno == EACCES ? "no escribible" : strerror(errno)); return -1; }
    enable_controllers(base);
    if ((size_t)snprintf(scope, sizeof scope, "%s/%s", base, name) >= sizeof scope) return -1;
    if (mkdir(scope, 0755) && errno != EEXIST) { fprintf(stderr, "cgroup: no se pudo crear %s: %s\n", name, strerror(errno)); return -1; }
    int dir = open(scope, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) { fprintf(stderr, "cgroup: no se pudo abrir %s: %s\n", name, strerror(errno)); return -1; }
    /* límites antes de mover el proceso (mejor esfuerzo: puede faltar el controlador) */
    if (lim) {
        if (lim->mem_high_mb) { snprintf(val, sizeof val, "%ld", lim->mem_high_mb * 1024L * 1024L); write_at(dir, "memory.high", val); }
        if (lim->mem_max_mb) { snprintf(val, sizeof val, "%ld", lim->mem_max_mb * 1024L * 1024L); write_at(dir, "memory.max", val); }
        if (lim->cpu_percent) { snprintf(val, sizeof val, "%ld 100000", (long)lim->cpu_percent * 1000L); write_at(dir, "cpu.max", val); }
    }
    int r = write_at(dir, "cgroup.procs", "0");
    close(dir);
    if (r) { fprintf(stderr, "cgroup: no se pudo entrar a %s: %s\n", name, strerror(errno)); return -1; }
    return 0;
}

/* chown del directorio del cgroup y de sus controles, relativo a dirfd (sin armar rutas largas). */
static void chown_ctl_at(int dirfd, unsigned uid, unsigned gid) {
    static const char *const f[] = { ".", "cgroup.procs", "cgroup.subtree_control", "cgroup.threads", NULL };
    for (int i = 0; f[i]; i++) fchownat(dirfd, f[i], uid, gid, 0);
}

/* Raíz del servicio: el cgroup propio, o su padre si estamos en la hoja «supervisor» que crea
 * cgroup_service_init(). */
static int service_root(char *out, size_t n) {
    const char *rel = cgroup_self();
    if (!rel[0]) return -1;
    if ((size_t)snprintf(out, n, CG_ROOT "%s", rel) >= n) return -1;
    size_t l = strlen(out);
    static const char leaf[] = "/supervisor";
    if (l > sizeof leaf - 1 && !strcmp(out + l - (sizeof leaf - 1), leaf)) out[l - (sizeof leaf - 1)] = 0;
    return 0;
}

int cgroup_service_init(void) {
    char svc[PATH_MAX];
    if (geteuid() != 0 || service_root(svc, sizeof svc) || access(svc, W_OK)) return -1;
    int fd = open(svc, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    if (mkdirat(fd, "supervisor", 0755) && errno != EEXIST) { close(fd); return -1; }
    int lfd = openat(fd, "supervisor", O_DIRECTORY | O_CLOEXEC);
    int r = lfd >= 0 ? write_at(lfd, "cgroup.procs", "0") : -1;
    if (lfd >= 0) close(lfd);
    close(fd);
    if (r) { fprintf(stderr, "cgroup: lordos.service sin Delegate=yes (no se pudo usar la hoja supervisor)\n"); return -1; }
    enable_controllers(svc);        /* ahora la raíz no tiene procesos: puede habilitar controladores */
    return 0;
}

int cgroup_delegate_session(unsigned uid, unsigned gid) {
    char svc[PATH_MAX], sess[PATH_MAX];
    if (service_root(svc, sizeof svc)) return -1;
    if (access(svc, W_OK)) { fprintf(stderr, "cgroup: lordos.service sin Delegate=yes; la sesión de %u corre sin subárbol\n", uid); return -1; }
    enable_controllers(svc);
    if ((size_t)snprintf(sess, sizeof sess, "%s/lordos-session-%u", svc, uid) >= sizeof sess) return -1;
    if (mkdir(sess, 0755) && errno != EEXIST) { fprintf(stderr, "cgroup: no se pudo crear el subárbol de la sesión %u: %s\n", uid, strerror(errno)); return -1; }
    enable_controllers(sess);
    int sfd = open(sess, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (sfd < 0) return -1;
    if (mkdirat(sfd, "server", 0755) && errno != EEXIST) { close(sfd); return -1; }
    int lfd = openat(sfd, "server", O_DIRECTORY | O_CLOEXEC);
    if (lfd < 0 || write_at(lfd, "cgroup.procs", "0")) { fprintf(stderr, "cgroup: no se pudo mover la sesión %u a su hoja: %s\n", uid, strerror(errno)); if (lfd >= 0) close(lfd); close(sfd); return -1; }
    /* el usuario es dueño del subárbol: lord-server (ya sin root) crea las scopes de las apps */
    chown_ctl_at(sfd, uid, gid);
    chown_ctl_at(lfd, uid, gid);
    close(lfd);
    close(sfd);
    fprintf(stderr, "cgroup: sesión %u delegada en lordos-session-%u\n", uid, uid);
    return 0;
}

void app_scope_enter(const char *id) {
    char name[80];
    if (snprintf(name, sizeof name, "app-lordos-%s.scope", id) >= (int)sizeof name) return;
    CgroupLimits lim = { .mem_high_mb = 0, .mem_max_mb = 0, .cpu_percent = 0 };   /* Linux apps: sin límite, solo atribución */
    cgroup_enter(name, &lim);
}

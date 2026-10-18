/* netsup.c — lord-sessiond vigila a lord-netd, la red del sistema (docs/RED.md, «La red es del
 * sistema»; el proceso en sí está en admin.c con LORD_NETD y net/netd.c).
 *
 *   - Lo lanza al arrancar, antes que la pantalla de ingreso: así hay red (y la IP) antes de que
 *     alguien entre, y el escritorio remoto se alcanza.
 *   - Ruta fija: <carpeta de lord-sessiond>/lord-netd, de root y no escribible por otros (salvo
 *     insecure_dev, como lord-server). Sin argumentos y con un entorno mínimo.
 *   - Si termina, lo vuelve a lanzar con una espera creciente (1, 2, 4… hasta 30 s).
 *   - Un reinicio de lord-sessiond (KillMode=process) no corta la red: lord-netd sigue vivo y el
 *     lord-sessiond nuevo lo adopta (pidfd sobre el pid de /run/lordos/netd.pid), sin sondear.
 *   - Deja /run/lordos/netd.enabled: con esa marca, los ayudantes de las sesiones nunca levantan
 *     la red por su cuenta (si lord-netd se está reiniciando, la acción responde un error). */
#include "sessiond.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define NETD_PIDFILE SD_RUN_DIR "/netd.pid"
#define NETD_MARK    SD_RUN_DIR "/netd.enabled"

static pid_t child = -1;                /* lord-netd hijo nuestro */
static int pidfd = -1;                  /* uno adoptado (de un lord-sessiond anterior) */
static double next_try;                 /* cuándo volver a lanzarlo */
static int fails;
static double started;                  /* cuándo se lanzó o adoptó (si duró, la espera vuelve a 1 s) */
static char path[PATH_MAX];

static void netd_path(void) {
    if (path[0]) return;
    snprintf(path, sizeof path, "%s", rd_conf.server);
    char *slash = strrchr(path, '/');
    if (slash) snprintf(slash + 1, sizeof path - (size_t)(slash + 1 - path), "lord-netd");
}

/* ¿Sigue vivo un lord-netd de antes (de un lord-sessiond anterior)? */
static int adopt(void) {
    FILE *f = fopen(NETD_PIDFILE, "r");
    int p = 0;
    if (f) { if (fscanf(f, "%d", &p) != 1) p = 0; fclose(f); }
    if (p <= 1) return -1;
    /* el pidfile es de root; el proceso tiene que ser de root y llamarse lord-netd (prctl). No se
     * compara /proc/<pid>/exe: si el binario se actualizó mientras corría, apunta a otro nombre */
    char proc[64], comm[32] = "";
    struct stat st;
    snprintf(proc, sizeof proc, "/proc/%d", p);
    if (stat(proc, &st) || st.st_uid != 0) return -1;
    snprintf(proc, sizeof proc, "/proc/%d/comm", p);
    f = fopen(proc, "r");
    if (!f) return -1;
    if (!fgets(comm, sizeof comm, f)) comm[0] = 0;
    fclose(f);
    comm[strcspn(comm, "\n")] = 0;
    if (strcmp(comm, "lord-netd")) return -1;
    int fd = (int)syscall(SYS_pidfd_open, p, 0);
    if (fd < 0) return -1;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    pidfd = fd;
    started = rd_now();
    sd_log("lord-netd sigue corriendo (pid %d): la red no se cortó", p);
    return 0;
}

void sd_netd_start(void) {
    netd_path();
    if (child > 0 || pidfd >= 0) return;
    struct stat st;
    if (stat(path, &st)) { if (!fails++) sd_log("sin %s: la red la levanta cada sesión, como antes", path); next_try = rd_now() + 3600; return; }
    if (!rd_conf.insecure_dev && (st.st_uid != 0 || (st.st_mode & 022))) {
        sd_log("%s no es de root o lo pueden escribir otros: no se lanza", path);
        next_try = rd_now() + 3600;
        return;
    }
    mkdir(SD_RUN_DIR, 0755);
    int m = open(NETD_MARK, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (m >= 0) close(m);
    if (!adopt()) return;
    pid_t p = fork();
    if (p == 0) {
        syscall(SYS_close_range, 3, ~0u, 0);    /* nada del monitor ni de las conexiones */
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) dup2(null, 0);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        setsid();                               /* no muere con lord-sessiond (KillMode=process) */
        char *const argv[] = { path, NULL };
        char *const env[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", "LANG=C.UTF-8", NULL };
        execve(path, argv, env);
        _exit(127);
    }
    if (p < 0) { next_try = rd_now() + 5; return; }
    child = p;
    started = rd_now();
    sd_log("lord-netd lanzado (pid %d)", (int)p);
}

int sd_netd_fd(void) { return pidfd; }

static void schedule(const char *why) {
    if (rd_now() - started > 60) fails = 0;
    double wait = fails < 5 ? (double)(1 << fails) : 30;
    fails++;
    next_try = rd_now() + wait;
    sd_log("lord-netd %s: se vuelve a lanzar en %.0f s", why, wait);
}

void sd_netd_event(void) {                      /* el pidfd del adoptado avisó que terminó */
    close(pidfd);
    pidfd = -1;
    schedule("(adoptado) terminó");
}

int sd_netd_reaped(pid_t pid, int status) {
    if (pid != child) return 0;
    child = -1;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 3) { next_try = rd_now() + 1; adopt(); return 1; }   /* ya corría otro */
    char why[48];
    if (WIFEXITED(status)) snprintf(why, sizeof why, "terminó con código %d", WEXITSTATUS(status));
    else snprintf(why, sizeof why, "terminó por la señal %d", WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    schedule(why);
    return 1;
}

void sd_netd_tick(void) {
    if (child > 0 || pidfd >= 0 || rd_now() < next_try) return;
    sd_netd_start();
}

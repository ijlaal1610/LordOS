/* appspawn.c — lord-server lanza una app .lpk como su propio proceso aislado
 * (`lord-server --app <id>`), la tercera excepción acotada a «el servidor no ejecuta comandos»
 * (ver AGENTS.md). El único argumento es un id VALIDADO de un paquete instalado; nunca argumentos
 * del llamador. Se crea el socket del broker (socketpair) y se le pasa al hijo; el extremo del
 * servidor queda registrado en brokersrv.c. */
#include "app.h"
#include "../lpk.h"
#include "../sec/broker.h"
#include "../ui/js_sys.h"
#include "../vault/vclient.h"

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

int app_package_dir(const char *id, const char *assets, char *out, size_t n) {
    if (!lpk_valid_id(id)) return -1;
    struct stat st;
    if (assets && assets[0]) {
        char d[PATH_MAX], real[PATH_MAX], m[PATH_MAX + 16];
        if ((size_t)snprintf(d, sizeof d, "%s/sysapps/%s", assets, id) < sizeof d && realpath(d, real) &&
            !stat(real, &st) && S_ISDIR(st.st_mode) && !(st.st_mode & S_IWOTH) && (st.st_uid == 0 || st.st_uid == getuid()) &&
            (size_t)snprintf(m, sizeof m, "%s/manifest.json", real) < sizeof m && !stat(m, &st) && S_ISREG(st.st_mode) &&
            (size_t)snprintf(out, n, "%s", real) < n)
            return 1;
    }
    const char *root = lpk_root();
    if (root && (size_t)snprintf(out, n, "%s/%s", root, id) < n && !stat(out, &st) && S_ISDIR(st.st_mode)) return 0;
    return -1;
}

/* System apps that get their own channel to lord-vault (vc_attach_app): only Passwords. */
static int wants_vault(const char *id, int system) { return system == 1 && !strcmp(id, "passwords"); }

/* 0 si lanzó el proceso (y registró el broker). El id tiene que ser de un paquete instalado. */
int app_spawn(const char *id) {
    if (!lpk_valid_id(id)) return -1;
    char dir[PATH_MAX];
    int sys = app_package_dir(id, js_sys_assets(), dir, sizeof dir);
    if (sys < 0) return -1;
    /* one process per app: if it is already running, it opens another window instead */
    if (!brokersrv_new_window(id)) { fprintf(stderr, "lord-server: app '%s' already running: new window\n", id); return 0; }

    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) return -1;
    self[n] = 0;

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv)) return -1;
    /* el extremo del hijo NO es CLOEXEC: lo hereda el proceso de la app */
    fcntl(sv[1], F_SETFD, 0);
    /* Passwords talks to lord-vault on its own channel: decrypted data never passes through here */
    int vfd = wants_vault(id, sys) ? vc_attach_app() : -1;

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() != 0) _exit(0);          /* nieto huérfano, como el lanzador de apps Linux */
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        sigset_t none;                       /* lord-server blocks SIGINT/SIGTERM (signalfd): not the app */
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        char fdstr[16];
        snprintf(fdstr, sizeof fdstr, "%d", sv[1]);
        setenv(BROKER_FD_ENV, fdstr, 1);
        if (vfd >= 0) {
            char vstr[16];
            fcntl(vfd, F_SETFD, 0);          /* inherited by the app process (LORD_VAULT_FD) */
            snprintf(vstr, sizeof vstr, "%d", vfd);
            setenv("LORD_VAULT_FD", vstr, 1);
        } else unsetenv("LORD_VAULT_FD");
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); if (null > 2) close(null); }
        /* el id lo pasa lord-server, ya validado; no hay argumentos del llamador */
        char *const argv[] = { self, (char *)"--app", (char *)id, NULL };
        execve(self, argv, environ);
        _exit(127);
    }
    close(sv[1]);
    if (vfd >= 0) close(vfd);
    if (pid < 0) { close(sv[0]); return -1; }
    waitpid(pid, NULL, 0);                   /* el hijo intermedio sale enseguida */
    if (brokersrv_add(sv[0], id, pid)) { close(sv[0]); return -1; }
    if (vfd >= 0) brokersrv_set_vault(sv[0]);   /* this app may also ask for the FIDO2 device */
    fprintf(stderr, "lord-server: app '%s' lanzada como proceso aislado\n", id);
    return 0;
}

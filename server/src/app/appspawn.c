/* appspawn.c — lord-server lanza una app .lpk como su propio proceso aislado
 * (`lord-server --app <id>`), la tercera excepción acotada a «el servidor no ejecuta comandos»
 * (ver AGENTS.md). El único argumento es un id VALIDADO de un paquete instalado; nunca argumentos
 * del llamador. Se crea el socket del broker (socketpair) y se le pasa al hijo; el extremo del
 * servidor queda registrado en brokersrv.c. */
#include "app.h"
#include "../lpk.h"
#include "../sec/broker.h"

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

/* 0 si lanzó el proceso (y registró el broker). El id tiene que ser de un paquete instalado. */
int app_spawn(const char *id) {
    if (!lpk_valid_id(id)) return -1;
    const char *root = lpk_root();
    char dir[PATH_MAX];
    struct stat st;
    if (!root || (size_t)snprintf(dir, sizeof dir, "%s/%s", root, id) >= sizeof dir || stat(dir, &st) || !S_ISDIR(st.st_mode)) return -1;

    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) return -1;
    self[n] = 0;

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv)) return -1;
    /* el extremo del hijo NO es CLOEXEC: lo hereda el proceso de la app */
    fcntl(sv[1], F_SETFD, 0);

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() != 0) _exit(0);          /* nieto huérfano, como el lanzador de apps Linux */
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        char fdstr[16];
        snprintf(fdstr, sizeof fdstr, "%d", sv[1]);
        setenv(BROKER_FD_ENV, fdstr, 1);
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); if (null > 2) close(null); }
        /* el id lo pasa lord-server, ya validado; no hay argumentos del llamador */
        char *const argv[] = { self, (char *)"--app", (char *)id, NULL };
        execve(self, argv, environ);
        _exit(127);
    }
    close(sv[1]);
    if (pid < 0) { close(sv[0]); return -1; }
    waitpid(pid, NULL, 0);                   /* el hijo intermedio sale enseguida */
    if (brokersrv_add(sv[0], id, pid)) { close(sv[0]); return -1; }
    fprintf(stderr, "lord-server: app '%s' lanzada como proceso aislado\n", id);
    return 0;
}

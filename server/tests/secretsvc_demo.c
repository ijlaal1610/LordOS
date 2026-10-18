/* secretsvc_demo.c — Secret Service propio con un llavero en memoria, sobre el bus de la sesión
 * real (sin caja de arena). Para probar con secret-tool/libsecret: tests/secretsvc/run-pw-116.sh.
 *   build/secretsvc-demo [--locked] [--seconds N]
 * Con --locked arranca bloqueado: Unlock muestra un «aviso» que se acepta solo al segundo.
 * Sale con 0 al terminar, 3 si org.freedesktop.secrets ya tenía dueño. */
#include "vault/dbus.h"
#include "vault/secretsvc.h"
#include "ss_memback.h"

#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }
static MemBack mb;

int main(int argc, char **argv) {
    int locked = 0, seconds = 120;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--locked")) locked = 1;
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atoi(argv[++i]);
    }
    signal(SIGTERM, on_sig);
    signal(SIGINT, on_sig);
    setenv("LORD_SS_DEBUG", "1", 0);
    memback_init(&mb, locked);
    int fd = dbus_connect_session();
    if (fd < 0) { fprintf(stderr, "demo: no hay bus de la sesión\n"); return 1; }
    int r = ss_start(fd, &memback_ops, &mb);
    if (r == 1) { fprintf(stderr, "demo: org.freedesktop.secrets ya tiene dueño\n"); return 3; }
    if (r) { fprintf(stderr, "demo: no se pudo arrancar\n"); return 1; }
    fprintf(stderr, "demo: listo como %s (%s)\n", ss_unique_name(), locked ? "bloqueado" : "desbloqueado");
    time_t end = time(NULL) + seconds;
    while (!stop && time(NULL) < end) {
        struct pollfd p = { ss_fd(), POLLIN | (ss_pending_out() ? POLLOUT : 0), 0 };
        poll(&p, 1, 100);
        if (ss_dispatch() < 0) { fprintf(stderr, "demo: el bus se cerró\n"); return 1; }
        memback_tick(&mb);
    }
    ss_stop();
    fprintf(stderr, "demo: fin\n");
    return 0;
}

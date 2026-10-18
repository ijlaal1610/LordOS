/* lm_main.c — punto de entrada de lord-media (ver lm.h y docs/MEDIOS.md).
 *
 * Lo lanza lord-server sin argumentos, con el socket en el fd 3. El primer mensaje (MM_JOB)
 * dice qué tipo de ayudante es y trae los descriptores. Antes de leer un solo byte del archivo
 * se aplica la caja de arena: Landlock (sin carpeta personal ni red) y, después de iniciar
 * VA-API si hace falta, seccomp. */
#include "lm.h"
#include "../../sec/sandbox.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>

int lm_debug;

static void av_log_cb(void *avcl, int level, const char *fmt, va_list vl) {
    (void)avcl;
    if (level > (lm_debug ? AV_LOG_WARNING : AV_LOG_ERROR)) return;
    static int count;
    if (++count > 200) return;                  /* un archivo roto no llena el registro */
    char line[512];
    vsnprintf(line, sizeof line, fmt, vl);
    fprintf(stderr, "lord-media[%d]: ffmpeg: %s", (int)getpid(), line);
}

int main(int argc, char **argv) {
    (void)argv;
    if (argc != 1) { fprintf(stderr, "lord-media: lo lanza lord-server (sin argumentos)\n"); return 2; }
    signal(SIGPIPE, SIG_IGN);
    prctl(PR_SET_PDEATHSIG, SIGKILL);           /* si lord-server se va, este también */
    if (getppid() == 1) return 1;
    prctl(PR_SET_NAME, "lord-media");
    lm_debug = getenv("LORD_MEDIA_DEBUG") != NULL;
    /* solo el socket: cualquier otro descriptor heredado se cierra */
#ifdef SYS_close_range
    syscall(SYS_close_range, LM_SOCK + 1, ~0U, 0);
#endif
    struct rlimit core = { 0, 0 };
    setrlimit(RLIMIT_CORE, &core);
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);         /* otro proceso del usuario no lo depura */
    av_log_set_callback(av_log_cb);

    static char payload[MP_PAYLOAD_MAX + 1];
    MediaMsg m;
    int fds[MP_MAX_FDS];
    if (mp_recv(LM_SOCK, &m, payload, sizeof payload, fds, 1) != 1 || m.type != MM_JOB) {
        LM_LOG("no llegó el trabajo");
        return 1;
    }
    /* La caja de arena compartida (sec/sandbox.c, la misma de lord-doc), antes de leer un byte
     * del archivo y antes de iniciar VA-API: sin red, sin abrir nada fuera de las bibliotecas y
     * datos del sistema (solo lectura), sin /dev (los dispositivos llegan como fds) y con los
     * ioctl de DRM/V4L2/dma-buf solo si el trabajo los necesita. El driver de VA-API se carga
     * después, de /usr/lib, que queda de solo lectura. */
    static const char *const ro[] = { "/usr/lib", "/usr/lib64", "/lib", "/lib64", "/usr/share", "/etc/ld.so.cache",
                                      "/sys/dev/char", "/sys/devices", NULL };
    static const int extra[] = { SYS_getcpu, SYS_kcmp, SYS_membarrier, SYS_sched_getparam, SYS_sched_getscheduler,
#ifdef SYS_futex_waitv
                                 SYS_futex_waitv,
#endif
                                 -1 };
    int ioctls = 0;
    if ((m.v[0] == MK_PLAY || m.v[0] == MK_SCREENREC) && fds[2] >= 0) ioctls |= SANDBOX_IOCTL_DRM | SANDBOX_IOCTL_DMABUF;
    if (m.v[0] == MK_CAMERA) ioctls |= SANDBOX_IOCTL_V4L2 | (fds[1] >= 0 ? SANDBOX_IOCTL_DRM | SANDBOX_IOCTL_DMABUF : 0);
    SandboxHelperCfg cfg = { .name = "app-lordos-media", .ro_paths = ro, .extra_syscalls = extra, .allow_ioctl_classes = ioctls,
                             .mem_max_mb = m.v[0] == MK_WORKER ? 1024 : 1536 };
    sandbox_apply_helper(&cfg);
    switch (m.v[0]) {
    case MK_PLAY: lm_player(payload, fds[0], fds[1], fds[2]); break;
    case MK_WORKER:
        setpriority(PRIO_PROCESS, 0, 10);       /* miniaturas y probes: en segundo plano */
        lm_worker();
        break;
    case MK_SCREENREC: lm_screenrec(payload, fds[0], fds[1], fds[2]); break;
    case MK_CAMERA: lm_camera(payload, fds[0], fds[1]); break;
    case MK_AUDIOREC: lm_audiorec(payload, fds[0], fds[1]); break;
    default: LM_LOG("tipo desconocido %lld", (long long)m.v[0]); return 1;
    }
    return 0;
}

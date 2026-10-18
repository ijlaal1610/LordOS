/* sandbox.h — caja de arena propia para las apps nativas .lpk (ver sandbox.c y docs/PRIVACIDAD.md).
 *
 * Código propio: sin bubblewrap, flatpak ni firejail. Usa lo del kernel 6.12 directo:
 *   - Landlock (ABI hasta 6): solo lectura de la carpeta del paquete, la runtime y las fuentes;
 *     lectura y escritura de ~/.local/share/lordos/appdata/<id>; nada más de $HOME. Reglas de
 *     TCP y aislamiento de sockets unix abstractos.
 *   - seccomp-bpf: una lista blanca de syscalls; sin ptrace, mount, kexec, bpf, perf, keyctl ni
 *     carga de módulos. Sin el permiso `network`, además bloquea los sockets de red.
 *   - no_new_privs y todas las capacidades soltadas.
 *   - cgroup v2 propio (cgroup.c) con memoria y CPU acotadas.
 * Si falta algo en el kernel, avisa por el registro y sigue sin esa parte (no deja la app sin
 * arrancar). Se aplica en el proceso de la app (lord-server --app <id>), después de soltar
 * privilegios y antes de cargar el código de la app. */
#ifndef LORD_SEC_SANDBOX_H
#define LORD_SEC_SANDBOX_H

#include <stddef.h>

typedef struct {
    const char *id;             /* id del paquete (ya validado) */
    const char *pkg_dir;        /* ~/.local/share/lordos/apps/<id>  (solo lectura) */
    const char *data_dir;       /* ~/.local/share/lordos/appdata/<id> (lectura y escritura) */
    const char *runtime_dir;    /* XDG_RUNTIME_DIR (para el socket de Wayland; solo lo justo) */
    const char *const *ro_extra;/* carpetas de solo lectura extra (fuentes, assets); NULL al final */
    int network;                /* 1 si la app declaró y tiene el permiso `network` */
} SandboxConfig;

/* Aplica todo lo posible. Devuelve una máscara de lo que quedó activo (SB_*). Nunca falla de
 * forma que impida arrancar: lo que no se pudo, se saltea con un aviso. */
enum { SB_CGROUP = 1, SB_LANDLOCK_FS = 2, SB_LANDLOCK_NET = 4, SB_SECCOMP = 8, SB_NOPRIV = 16, SB_NOCAPS = 32 };
int sandbox_apply(const SandboxConfig *cfg);

/* Nombre de la scope de cgroup de una app .lpk: "app-lordos-lpk-<id>.scope". */
void sandbox_scope_name(const char *id, char *out, size_t n);

/* ---- variante para ayudantes de decodificación (lord-media, lord-doc) ----
 * Un ayudante que decodifica datos que no son de confianza (video, PDF, archivos comprimidos)
 * no necesita ni red ni acceso al sistema de archivos: recibe los fds ya abiertos y escribe en
 * otro fd. Esta variante aplica: sin red, sin archivos salvo las carpetas de solo lectura que se
 * le pasen (fuentes, las bibliotecas que la lib carga al iniciar), la lista blanca de syscalls
 * más las que el ayudante declare, un tope de memoria, no_new_privs y sin capacidades. Misma
 * caja de arena que las apps .lpk, sin forks. */
/* Clases de ioctl que un ayudante puede necesitar para decodificar por hardware. Se filtra por el
 * byte de tipo del número de ioctl (_IOC_TYPE): solo esos tipos; nunca un ioctl genérico ni
 * TIOCSTI. El ayudante NO puede abrir /dev (Landlock): recibe los fds de los dispositivos ya
 * abiertos por lord-server. */
enum { SANDBOX_IOCTL_DRM = 1, SANDBOX_IOCTL_V4L2 = 2, SANDBOX_IOCTL_DMABUF = 4 };

typedef struct {
    const char *name;               /* nombre de la scope de cgroup (ej. "lord-media") */
    const char *const *ro_paths;    /* carpetas de solo lectura (fuentes, /usr/lib…); NULL al final */
    const int *extra_syscalls;      /* syscalls extra permitidas; -1 al final */
    int  allow_ioctl_classes;       /* OR de SANDBOX_IOCTL_* (decodificación por hardware) */
    long mem_max_mb;                /* tope de memoria (0 = sin tope) */
    /* carpetas ya abiertas por lord-server (fds de directorio), -1 al final; NULL = ninguna.
     * ro: lectura; rw: lectura + escribir/crear archivos, carpetas y enlaces simbólicos y borrar
     * archivos (sin truncar). Así el ayudante no necesita ninguna ruta. */
    const int *ro_dir_fds;
    const int *rw_dir_fds;
} SandboxHelperCfg;

/* Los llamadores pueden detectar los campos ro_dir_fds/rw_dir_fds. */
#define SANDBOX_HELPER_DIR_FDS 1

/* Endurecida respecto de las apps: sin socket/socketpair/connect/bind/listen/accept (el canal
 * llega heredado; sendmsg/recvmsg siguen), clone solo con CLONE_THREAD, clone3 → ENOSYS (glibc
 * cae a clone), sin execve/execveat, y el filtro se aplica a TODOS los hilos (TSYNC). Si seccomp
 * no se puede aplicar, el proceso termina (falla cerrado). kcmp, si se pide en extra_syscalls,
 * solo vale para comparar fds del propio proceso (pid1 == pid2 == getpid()). */
int sandbox_apply_helper(const SandboxHelperCfg *cfg);

#endif

/* sandbox.c — caja de arena de las apps .lpk (ver sandbox.h y docs/PRIVACIDAD.md).
 *
 * Decisión sobre la red: lord-server corre sin root, así que no puede crear un network namespace
 * (unshare(CLONE_NEWNET) pide CAP_SYS_ADMIN). En vez de eso, una app sin el permiso `network`
 * se queda en el netns del equipo pero no puede abrir sockets de red: seccomp rechaza
 * socket(AF_INET/AF_INET6/AF_PACKET) y Landlock (ABI ≥ 4) le niega bind/connect de TCP. El
 * agente de monitoreo pone, además, reglas de nftables por cgroup (defensa en capas). Los
 * sockets AF_UNIX siguen permitidos: los necesitan Wayland y el broker (que llega como fd ya
 * conectado, sin connect). */
#include "sandbox.h"
#include "cgroup.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/capability.h>

#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/socket.h>

#ifdef __has_include
#  if __has_include(<linux/landlock.h>)
#    include <linux/landlock.h>
#    define HAVE_LANDLOCK 1
#  endif
#endif

void sandbox_scope_name(const char *id, char *out, size_t n) { snprintf(out, n, "app-lordos-lpk-%s.scope", id); }

/* ---- no_new_privs y capacidades ---- */

static int apply_nopriv(void) { return prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 ? 0 : -1; }

static int drop_caps(void) {
    struct __user_cap_header_struct hdr = { 0x20080522 /* _LINUX_CAPABILITY_VERSION_3 */, 0 };
    struct __user_cap_data_struct data[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
    /* soltar también el bounding set y los ambientes (mejor esfuerzo) */
    for (int c = 0; c <= 40; c++) prctl(PR_CAPBSET_DROP, c, 0, 0, 0);
    prctl(PR_SET_SECUREBITS, 0, 0, 0, 0);
    return syscall(SYS_capset, &hdr, data) == 0 ? 0 : -1;
}

/* ---- Landlock ---- */

#ifdef HAVE_LANDLOCK
static int ll_create(struct landlock_ruleset_attr *a, size_t n) { return (int)syscall(SYS_landlock_create_ruleset, a, n, 0U); }
static int ll_add(int fd, enum landlock_rule_type t, const void *attr) { return (int)syscall(SYS_landlock_add_rule, fd, t, attr, 0U); }
static int ll_enforce(int fd) { return (int)syscall(SYS_landlock_restrict_self, fd, 0U); }
static int ll_abi(void) { return (int)syscall(SYS_landlock_create_ruleset, NULL, 0U, LANDLOCK_CREATE_RULESET_VERSION); }

static int ll_path(int rs, const char *path, uint64_t allow) {
    struct landlock_path_beneath_attr pb = { .allowed_access = allow };
    pb.parent_fd = open(path, O_PATH | O_CLOEXEC);
    if (pb.parent_fd < 0) return -1;
    int r = ll_add(rs, LANDLOCK_RULE_PATH_BENEATH, &pb);
    close(pb.parent_fd);
    return r;
}

/* Regla sobre una carpeta que ya está abierta (fd de lord-server). */
static int ll_fd(int rs, int fd, uint64_t allow) {
    struct landlock_path_beneath_attr pb = { .allowed_access = allow, .parent_fd = fd };
    return ll_add(rs, LANDLOCK_RULE_PATH_BENEATH, &pb);
}
#endif


/* ro_fds / rw_fds: carpetas ya abiertas (ayudantes); NULL para las apps. */
static int apply_landlock(const SandboxConfig *cfg, const int *ro_fds, const int *rw_fds, int *fs_ok, int *net_ok) {
    *fs_ok = *net_ok = 0;
#ifdef HAVE_LANDLOCK
    int abi = ll_abi();
    if (abi < 1) { fprintf(stderr, "sandbox: sin Landlock (ABI %d); solo seccomp\n", abi); return -1; }

    uint64_t ro = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_EXECUTE;
    uint64_t rw = ro | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG |
                  LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_MAKE_SYM |
                  LANDLOCK_ACCESS_FS_TRUNCATE;
    uint64_t fs_handled = ro | rw;
    /* accesos que un ABI viejo no conoce: se quitan de "handled" para no fallar al crear el set */
    if (abi < 2) fs_handled &= ~(uint64_t)LANDLOCK_ACCESS_FS_REFER;
    if (abi < 3) fs_handled &= ~(uint64_t)LANDLOCK_ACCESS_FS_TRUNCATE;
#ifdef LANDLOCK_ACCESS_FS_IOCTL_DEV
    if (abi >= 5) fs_handled |= LANDLOCK_ACCESS_FS_IOCTL_DEV;
#endif
    rw &= fs_handled; ro &= fs_handled;
    /* carpetas rw de los ayudantes: escribir, crear y borrar archivos, pero NO truncar */
    uint64_t rwfd = (ro | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG |
                     LANDLOCK_ACCESS_FS_MAKE_SYM | LANDLOCK_ACCESS_FS_REMOVE_FILE) & fs_handled;

    struct landlock_ruleset_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.handled_access_fs = fs_handled;
#ifdef LANDLOCK_ACCESS_NET_CONNECT_TCP
    uint64_t net_handled = (abi >= 4) ? (LANDLOCK_ACCESS_NET_BIND_TCP | LANDLOCK_ACCESS_NET_CONNECT_TCP) : 0;
    attr.handled_access_net = net_handled;
#endif
#ifdef LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET
    if (abi >= 6) attr.scoped = LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET | LANDLOCK_SCOPE_SIGNAL;
#endif
    int rs = ll_create(&attr, sizeof attr);
    if (rs < 0) { fprintf(stderr, "sandbox: Landlock no se pudo crear: %s\n", strerror(errno)); return -1; }

    /* solo lectura: runtime (fuentes, assets) y la carpeta del paquete */
    if (cfg->pkg_dir) ll_path(rs, cfg->pkg_dir, ro);
    for (const char *const *p = cfg->ro_extra; p && *p; p++) ll_path(rs, *p, ro);
    /* lectura y escritura: solo la carpeta de datos de la app */
    if (cfg->data_dir) ll_path(rs, cfg->data_dir, rw);
    /* carpetas pasadas como fds ya abiertos (ayudantes) */
    for (const int *p = ro_fds; p && *p >= 0; p++) ll_fd(rs, *p, ro);
    for (const int *p = rw_fds; p && *p >= 0; p++) ll_fd(rs, *p, rwfd);
    /* el socket de Wayland y el del broker viven en XDG_RUNTIME_DIR: lectura (conexión por fd ya abierto) */
    if (cfg->runtime_dir) ll_path(rs, cfg->runtime_dir, LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR);
    /* /dev/null, /dev/urandom y las bibliotecas del sistema: solo lectura y ejecución */
    ll_path(rs, "/dev/null", LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_WRITE_FILE);
    ll_path(rs, "/dev/urandom", LANDLOCK_ACCESS_FS_READ_FILE);
    ll_path(rs, "/usr/lib", ro);
    ll_path(rs, "/lib", ro);

    *fs_ok = 1;
    /* red: si la app NO tiene el permiso, no se agrega ninguna regla de red → todo TCP queda
     * prohibido. Si lo tiene, se permiten bind y connect a cualquier puerto. */
#ifdef LANDLOCK_ACCESS_NET_CONNECT_TCP
    if (net_handled) {
        if (cfg->network) {
            struct landlock_net_port_attr np = { .allowed_access = LANDLOCK_ACCESS_NET_CONNECT_TCP | LANDLOCK_ACCESS_NET_BIND_TCP };
            for (int port = 0; port <= 65535; port++) { np.port = (uint64_t)port; ll_add(rs, LANDLOCK_RULE_NET_PORT, &np); }
        }
        *net_ok = 1;
    }
#endif
    if (apply_nopriv()) { close(rs); return -1; }     /* Landlock exige no_new_privs */
    int r = ll_enforce(rs);
    close(rs);
    if (r) { fprintf(stderr, "sandbox: Landlock no se pudo aplicar: %s\n", strerror(errno)); *fs_ok = *net_ok = 0; return -1; }
    return 0;
#else
    (void)cfg; (void)ro_fds; (void)rw_fds;
    fprintf(stderr, "sandbox: compilado sin Landlock\n");
    return -1;
#endif
}

/* ---- seccomp-bpf: lista blanca ---- */

#if defined(__x86_64__)
#  define SB_AUDIT_ARCH AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#  define SB_AUDIT_ARCH AUDIT_ARCH_AARCH64
#else
#  define SB_AUDIT_ARCH 0
#endif

#define NR(x) (int)(x)

/* Syscalls comunes a las apps y a los ayudantes. Todo lo que no esté acá (ni en las listas de
 * abajo) devuelve EPERM. Los peligrosos (ptrace, mount, kexec, bpf, perf_event_open, keyctl,
 * add_key, init_module, execve…) quedan afuera por definición. */
static const int allow[] = {
    NR(SYS_read), NR(SYS_write), NR(SYS_readv), NR(SYS_writev), NR(SYS_pread64), NR(SYS_pwrite64),
    NR(SYS_close), NR(SYS_lseek), NR(SYS_fstat),
#ifdef SYS_stat
    NR(SYS_stat), NR(SYS_lstat),
#endif
    NR(SYS_newfstatat), NR(SYS_statx), NR(SYS_fstatfs), NR(SYS_statfs),
    NR(SYS_openat), NR(SYS_openat2), NR(SYS_getdents64), NR(SYS_readlinkat), NR(SYS_faccessat),
#ifdef SYS_access
    NR(SYS_access), NR(SYS_readlink), NR(SYS_open),          /* solo x86-64 */
#endif
    NR(SYS_faccessat2), NR(SYS_fcntl), NR(SYS_dup), NR(SYS_dup3),
#ifdef SYS_dup2
    NR(SYS_dup2), NR(SYS_pipe), NR(SYS_poll), NR(SYS_epoll_create), NR(SYS_eventfd),    /* solo x86-64 */
#endif
    NR(SYS_pipe2), NR(SYS_ppoll), NR(SYS_pselect6), NR(SYS_epoll_create1), NR(SYS_epoll_ctl), NR(SYS_epoll_pwait), NR(SYS_eventfd2),
    NR(SYS_mmap), NR(SYS_munmap), NR(SYS_mremap), NR(SYS_mprotect), NR(SYS_madvise), NR(SYS_brk),
    NR(SYS_rt_sigaction), NR(SYS_rt_sigprocmask), NR(SYS_rt_sigreturn), NR(SYS_sigaltstack),
    NR(SYS_nanosleep), NR(SYS_clock_nanosleep), NR(SYS_clock_gettime), NR(SYS_clock_getres), NR(SYS_gettimeofday),
    NR(SYS_getpid), NR(SYS_gettid), NR(SYS_getppid), NR(SYS_getuid), NR(SYS_geteuid), NR(SYS_getgid), NR(SYS_getegid),
    NR(SYS_getrandom), NR(SYS_getrusage), NR(SYS_sysinfo), NR(SYS_uname), NR(SYS_sched_getaffinity), NR(SYS_sched_yield),
    NR(SYS_futex),
#ifdef SYS_futex_time64
    NR(SYS_futex_time64),
#endif
    NR(SYS_set_robust_list), NR(SYS_get_robust_list), NR(SYS_set_tid_address), NR(SYS_rseq),
    NR(SYS_exit), NR(SYS_exit_group), NR(SYS_tgkill), NR(SYS_restart_syscall),
    NR(SYS_memfd_create), NR(SYS_ftruncate), NR(SYS_fsync), NR(SYS_fdatasync),
    NR(SYS_mkdirat), NR(SYS_unlinkat), NR(SYS_renameat2), NR(SYS_renameat), NR(SYS_fchmod), NR(SYS_fchmodat), NR(SYS_utimensat),
    NR(SYS_symlinkat), NR(SYS_linkat), NR(SYS_getcwd), NR(SYS_umask),
#ifdef SYS_fadvise64
    NR(SYS_fadvise64),
#endif
    NR(SYS_recvmsg), NR(SYS_sendmsg), NR(SYS_recvfrom), NR(SYS_sendto), NR(SYS_shutdown),
    NR(SYS_getsockopt), NR(SYS_setsockopt), NR(SYS_getsockname), NR(SYS_getpeername),
    NR(SYS_prctl), NR(SYS_prlimit64), NR(SYS_getrlimit),
    NR(SYS_rt_sigtimedwait), NR(SYS_wait4), NR(SYS_setpriority), NR(SYS_getpriority),
#ifdef SYS_epoll_wait
    NR(SYS_epoll_wait),      /* x86-64: existe; aarch64 solo tiene epoll_pwait (ya está arriba) */
#endif
};

/* Solo para las apps (no para los ayudantes): connect/bind (sockets de red con el permiso),
 * y clone/clone3 sin restricción. Nunca socketpair. */
static const int allow_app[] = { NR(SYS_bind), NR(SYS_connect), NR(SYS_clone), NR(SYS_clone3) };

/* helper = 1: perfil endurecido de los ayudantes. self = getpid() (para kcmp). */
static int build_filter(struct sock_filter *f, int cap, int net, const int *extra, int ioctl_classes, int helper, unsigned self) {
    int n = 0;
#define EMIT(stmt) do { if (n < cap) f[n] = (struct sock_filter)stmt; n++; } while (0)
#define RELOAD_NR() EMIT(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)))
    /* arquitectura correcta o se mata el proceso */
    EMIT(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)));
    EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SB_AUDIT_ARCH, 1, 0));
    EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
    RELOAD_NR();
    /* ioctl(2): solo para los ayudantes de decodificación, y solo por clase de dispositivo
     * (byte de tipo _IOC_TYPE = (request >> 8) & 0xff). Nunca un ioctl genérico ni TIOCSTI. */
    if (ioctl_classes) {
        int types[3], nt = 0;
        if (ioctl_classes & SANDBOX_IOCTL_DRM) types[nt++] = 'd';      /* DRM / VA-API */
        if (ioctl_classes & SANDBOX_IOCTL_V4L2) types[nt++] = 'V';     /* V4L2 (Pi 5) */
        if (ioctl_classes & SANDBOX_IOCTL_DMABUF) types[nt++] = 'b';   /* DMA-BUF sync */
        EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, NR(SYS_ioctl), 0, (unsigned char)(nt + 4)));
        EMIT(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1])));   /* request (32 bits bajos) */
        EMIT(BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 0x0000ff00u));                             /* _IOC_TYPE << 8 */
        for (int i = 0; i < nt; i++)
            EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)(types[i] << 8), (unsigned char)(nt - i), 0));
        EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));    /* tipo no permitido */
        EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));                                 /* tipo permitido */
        RELOAD_NR();
    }
    if (helper) {
        /* clone: solo hilos ([0]ld flags [1]and [2]jeq [3]EPERM [4]ALLOW) */
        EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, NR(SYS_clone), 0, 5));
        EMIT(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])));
        EMIT(BPF_STMT(BPF_ALU | BPF_AND | BPF_K, (unsigned)CLONE_THREAD));
        EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)CLONE_THREAD, 1, 0));
        EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
        EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
        RELOAD_NR();
        /* clone3: ENOSYS, así glibc cae a clone (que sí se puede filtrar por flags) */
        EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, NR(SYS_clone3), 0, 1));
        EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)));
#ifdef SYS_kcmp
        /* kcmp: solo si el ayudante lo pide, y solo para sí mismo (pid1 == pid2 == getpid()).
         * Mesa lo usa (os_same_file_description) para saber si dos fds DRM son el mismo. */
        int want_kcmp = 0;
        for (const int *e = extra; e && *e >= 0; e++) if (*e == NR(SYS_kcmp)) want_kcmp = 1;
        if (want_kcmp) {
            EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, NR(SYS_kcmp), 0, 6));
            EMIT(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])));
            EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, self, 0, 2));
            EMIT(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1])));
            EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, self, 1, 0));
            EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
            EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
            RELOAD_NR();
        }
#endif
        /* socket, socketpair, connect, bind, listen, accept, execve y execveat: ninguno está en
         * las listas, así que caen en EPERM */
    } else {
        /* socket(2) de las apps: solo red (con el permiso). AF_UNIX tampoco: Wayland y el broker
         * ya están conectados antes de la caja de arena, y un socket unix nuevo podría llegar al
         * bus de sesión o a otros sockets con nombre (Landlock ≤ 6 no filtra connect unix).
         * [0]ld domain [1]jeq inet [2]jeq inet6 [3]EACCES [4]ALLOW */
        EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, NR(SYS_socket), 0, 5));
        EMIT(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])));
        EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_INET, net ? 2 : 1, 0));
        EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_INET6, net ? 1 : 0, 0));
        EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EACCES & SECCOMP_RET_DATA)));
        EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
        RELOAD_NR();
    }
    /* lista blanca */
    for (size_t i = 0; i < sizeof allow / sizeof allow[0]; i++) {
        EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)allow[i], 0, 1));
        EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    }
    if (!helper) for (size_t i = 0; i < sizeof allow_app / sizeof allow_app[0]; i++) {
        EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)allow_app[i], 0, 1));
        EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    }
    for (const int *e = extra; e && *e >= 0; e++) {
#ifdef SYS_kcmp
        if (helper && *e == NR(SYS_kcmp)) continue;      /* ya filtrado arriba */
#endif
        /* nunca se agregan por esta vía los que el perfil prohíbe */
        if (helper && (*e == NR(SYS_clone) || *e == NR(SYS_clone3) || *e == NR(SYS_socket) || *e == NR(SYS_connect) ||
                       *e == NR(SYS_bind) || *e == NR(SYS_execve) || *e == NR(SYS_execveat) || *e == NR(SYS_ioctl))) continue;
        EMIT(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)*e, 0, 1));
        EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    }
    /* el resto: EPERM (no mata, así una libc que tantea un syscall nuevo no muere) */
    EMIT(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
#undef RELOAD_NR
#undef EMIT
    return n;
}

/* Aplica el filtro a todos los hilos (TSYNC). Devuelve 0 bien. */
static int apply_seccomp_extra(int net, const int *extra, int ioctl_classes, int helper) {
    if (!SB_AUDIT_ARCH) { fprintf(stderr, "sandbox: seccomp no soportado en esta arquitectura\n"); return -1; }
    static struct sock_filter filt[1024];
    int n = build_filter(filt, (int)(sizeof filt / sizeof filt[0]), net, extra, ioctl_classes, helper, (unsigned)getpid());
    if (n > (int)(sizeof filt / sizeof filt[0])) { fprintf(stderr, "sandbox: filtro seccomp demasiado grande (%d)\n", n); return -1; }
    struct sock_fprog prog = { .len = (unsigned short)n, .filter = filt };
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) return -1;
    long r = syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &prog);
    if (r == 0) return 0;
    fprintf(stderr, "sandbox: seccomp con TSYNC falló (%s)\n", r > 0 ? "un hilo no se pudo sincronizar" : strerror(errno));
    if (helper) return -1;                     /* el ayudante falla cerrado (ver sandbox_apply_helper) */
    /* app: al aplicarse todavía tiene un solo hilo; se reintenta sin TSYNC */
    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog)) { fprintf(stderr, "sandbox: seccomp no se pudo aplicar: %s\n", strerror(errno)); return -1; }
    return 0;
}

int sandbox_apply(const SandboxConfig *cfg) {
    int mask = 0;
    char scope[80];
    sandbox_scope_name(cfg->id, scope, sizeof scope);
    CgroupLimits lim = { .mem_high_mb = 192, .mem_max_mb = 512, .cpu_percent = 100 };
    if (!cgroup_enter(scope, &lim)) mask |= SB_CGROUP;

    /* no_new_privs y soltar capacidades antes de seccomp (capset no está en la lista blanca) */
    if (!apply_nopriv()) mask |= SB_NOPRIV;
    if (!drop_caps()) mask |= SB_NOCAPS;

    int fs_ok = 0, net_ok = 0;
    if (!apply_landlock(cfg, NULL, NULL, &fs_ok, &net_ok)) { if (fs_ok) mask |= SB_LANDLOCK_FS; if (net_ok) mask |= SB_LANDLOCK_NET; }

    /* seccomp al final: una vez puesto, ya no se abren carpetas para Landlock */
    if (!apply_seccomp_extra(cfg->network, NULL, 0, 0)) mask |= SB_SECCOMP;

    fprintf(stderr, "sandbox[%s]: cgroup=%d landlock-fs=%d landlock-net=%d seccomp=%d nopriv=%d nocaps=%d red=%s\n",
            cfg->id, !!(mask & SB_CGROUP), !!(mask & SB_LANDLOCK_FS), !!(mask & SB_LANDLOCK_NET),
            !!(mask & SB_SECCOMP), !!(mask & SB_NOPRIV), !!(mask & SB_NOCAPS), cfg->network ? "sí" : "no");
    return mask;
}

int sandbox_apply_helper(const SandboxHelperCfg *cfg) {
    int mask = 0;
    if (cfg->name) {
        char scope[96];
        snprintf(scope, sizeof scope, "%.80s.scope", cfg->name);
        CgroupLimits lim = { .mem_high_mb = cfg->mem_max_mb ? cfg->mem_max_mb * 3 / 4 : 0, .mem_max_mb = cfg->mem_max_mb, .cpu_percent = 0 };
        if (!cgroup_enter(scope, &lim)) mask |= SB_CGROUP;
    }
    if (!apply_nopriv()) mask |= SB_NOPRIV;
    if (!drop_caps()) mask |= SB_NOCAPS;
    /* sin red; solo lectura de las carpetas declaradas, más las carpetas que llegan como fds */
    SandboxConfig ro = { .id = cfg->name ? cfg->name : "helper", .pkg_dir = NULL, .data_dir = NULL,
                         .runtime_dir = NULL, .ro_extra = cfg->ro_paths, .network = 0 };
    int fs_ok = 0, net_ok = 0;
    if (!apply_landlock(&ro, cfg->ro_dir_fds, cfg->rw_dir_fds, &fs_ok, &net_ok)) { if (fs_ok) mask |= SB_LANDLOCK_FS; if (net_ok) mask |= SB_LANDLOCK_NET; }
    if (apply_seccomp_extra(0, cfg->extra_syscalls, cfg->allow_ioctl_classes, 1)) {
        /* falla cerrado: un ayudante que decodifica datos ajenos no corre sin seccomp */
        fprintf(stderr, "sandbox-helper[%s]: sin seccomp; el ayudante termina\n", ro.id);
        _exit(70);
    }
    mask |= SB_SECCOMP;
    fprintf(stderr, "sandbox-helper[%s]: cgroup=%d landlock-fs=%d seccomp=1(tsync) nopriv=%d nocaps=%d (sin red ni sockets nuevos, sin exec, solo hilos)\n",
            ro.id, !!(mask & SB_CGROUP), !!(mask & SB_LANDLOCK_FS), !!(mask & SB_NOPRIV), !!(mask & SB_NOCAPS));
    return mask;
}

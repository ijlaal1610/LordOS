/* sandbox.h — our own sandbox for native .lpk apps (see sandbox.c and docs/PRIVACY.md).
 *
 * Our own code: no bubblewrap, flatpak or firejail. Uses kernel 6.12 features directly:
 *   - Landlock (ABI up to 6): read-only access to the package folder, the runtime and the fonts;
 *     read and write of ~/.local/share/lordos/appdata/<id>; nothing else from $HOME. TCP
 *     rules and abstract unix socket scoping.
 *   - seccomp-bpf: a syscall allowlist; no ptrace, mount, kexec, bpf, perf, keyctl or
 *     module loading. Without the `network` permission, it also blocks network sockets.
 *   - no_new_privs and all capabilities dropped.
 *   - its own cgroup v2 (cgroup.c) with bounded memory and CPU.
 * If something is missing in the kernel, it warns in the log and goes on without that part (it doesn't keep the app from
 * starting). Applied in the app process (lord-server --app <id>), after dropping
 * privileges and before loading the app's code. */
#ifndef LORD_SEC_SANDBOX_H
#define LORD_SEC_SANDBOX_H

#include <stddef.h>

typedef struct {
    const char *id;             /* package id (already validated) */
    const char *pkg_dir;        /* ~/.local/share/lordos/apps/<id>  (read-only) */
    const char *data_dir;       /* ~/.local/share/lordos/appdata/<id> (read and write) */
    const char *runtime_dir;    /* XDG_RUNTIME_DIR (for the Wayland socket; just what's needed) */
    const char *const *ro_extra;/* extra read-only folders (fonts, assets); NULL-terminated */
    int network;                /* 1 if the app declared and has the `network` permission */
} SandboxConfig;

/* Applies everything possible. Returns a mask of what ended up active (SB_*). Never fails in
 * a way that prevents startup: whatever couldn't be done is skipped with a warning. */
enum { SB_CGROUP = 1, SB_LANDLOCK_FS = 2, SB_LANDLOCK_NET = 4, SB_SECCOMP = 8, SB_NOPRIV = 16, SB_NOCAPS = 32 };
int sandbox_apply(const SandboxConfig *cfg);

/* cgroup scope name of a .lpk app: "app-lordos-lpk-<id>.scope". */
void sandbox_scope_name(const char *id, char *out, size_t n);

/* ---- variant for decoding helpers (lord-media, lord-doc) ----
 * A helper that decodes untrusted data (video, PDF, archives)
 * needs neither network nor filesystem access: it gets fds already open and writes to
 * another fd. This variant applies: no network, no files except the read-only folders
 * passed to it (fonts, the libraries the lib loads at startup), the syscall allowlist
 * plus the ones the helper declares, a memory cap, no_new_privs and no capabilities. Same
 * sandbox as .lpk apps, without forks. */
/* ioctl classes a helper may need for hardware decoding. Filtered by the
 * type byte of the ioctl number (_IOC_TYPE): only those types; never a generic ioctl nor
 * TIOCSTI. The helper can NOT open /dev (Landlock): it gets the device fds already
 * opened by lord-server. */
enum { SANDBOX_IOCTL_DRM = 1, SANDBOX_IOCTL_V4L2 = 2, SANDBOX_IOCTL_DMABUF = 4 };

typedef struct {
    const char *name;               /* cgroup scope name (e.g. "lord-media") */
    const char *const *ro_paths;    /* read-only folders (fonts, /usr/lib…); NULL-terminated */
    const int *extra_syscalls;      /* extra allowed syscalls; -1-terminated */
    int  allow_ioctl_classes;       /* OR of SANDBOX_IOCTL_* (hardware decoding) */
    long mem_max_mb;                /* memory cap (0 = no cap) */
    /* folders already opened by lord-server (directory fds), -1-terminated; NULL = none.
     * ro: read; rw: read + write/create files, folders and symlinks and delete
     * files (no truncation). That way the helper needs no path at all. */
    const int *ro_dir_fds;
    const int *rw_dir_fds;
} SandboxHelperCfg;

/* Callers can detect the ro_dir_fds/rw_dir_fds fields. */
#define SANDBOX_HELPER_DIR_FDS 1

/* Hardened compared to apps: no socket/socketpair/connect/bind/listen/accept (the channel
 * is inherited; sendmsg/recvmsg still work), clone only with CLONE_THREAD, clone3 → ENOSYS (glibc
 * falls back to clone), no execve/execveat, and the filter applies to ALL threads (TSYNC). If seccomp
 * can't be applied, the process exits (fails closed). kcmp, if requested in extra_syscalls,
 * only works for comparing fds of the process itself (pid1 == pid2 == getpid()). */
int sandbox_apply_helper(const SandboxHelperCfg *cfg);

#endif

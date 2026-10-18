/* cgroup.h — cada app va a su propio cgroup v2 (una «scope»), para que el agente de monitoreo
 * atribuya el tráfico y el hardware por ese nombre y para ponerle límites de memoria y CPU.
 *
 *   app-lordos-lpk-<id>.scope   apps nativas .lpk aisladas (sandbox.c)
 *   app-lordos-<id>.scope       apps Linux (.desktop) lanzadas por apps.c
 *
 * Reutilizable: se llama en el hijo recién bifurcado, justo antes del execve. lord-server corre
 * sin root; necesita que su subárbol de cgroup esté delegado (lord-sessiond lo prepara, ver
 * docs/SESIONES.md y docs/PRIVACIDAD.md). Si no se puede (sin delegación, controlador faltante,
 * kernel viejo), avisa por el registro y sigue sin cgroup: la app corre igual, sin límites. */
#ifndef LORD_SEC_CGROUP_H
#define LORD_SEC_CGROUP_H

#include <stddef.h>

typedef struct {
    long mem_high_mb;       /* memory.high en MB (0 = sin límite) */
    long mem_max_mb;        /* memory.max en MB (0 = sin límite) */
    int  cpu_percent;       /* cpu.max como % de un núcleo (0 = sin límite; 100 = un núcleo) */
} CgroupLimits;

/* Mueve el proceso actual a la scope `name` dentro del subárbol delegado de la sesión y le
 * aplica los límites. 0 si quedó en la scope; -1 si no se pudo (ya avisó, el llamador sigue). */
int cgroup_enter(const char *name, const CgroupLimits *lim);

/* Atajo para el hijo antes de execve: entra a app-lordos-<id>.scope con límites holgados. Solo
 * registra un aviso si falla. */
void app_scope_enter(const char *id);

/* La ruta de cgroup v2 del proceso actual, relativa a /sys/fs/cgroup (ej. ".../app.slice/..."),
 * para que el monitor sepa en qué scope está. "" si no se pudo leer. */
const char *cgroup_self(void);

/* Called by lord-sessiond (root) in a session's child, before lord-server's execve: creates the
 * session's cgroup subtree with memory/CPU/pids enabled and moves the process into its «server»
 * leaf. The subtree stays root-owned (cgroup_harden_session); app scopes are created later by
 * lord-sessiond on request (cgroup_place_serve). Best effort: without delegation (Delegate=yes in
 * lordos.service) it warns and goes on. Returns 0 on success. See docs/SESIONES.md. */
int cgroup_delegate_session(unsigned uid, unsigned gid);

/* Lo llama lord-sessiond al arrancar (root, lordos.service con Delegate=yes): se mueve a la hoja
 * «supervisor» para que la raíz del servicio pueda habilitar memoria/CPU/pids para los
 * subárboles de las sesiones (regla «sin procesos internos» de cgroup v2). 0 si quedó. */
int cgroup_service_init(void);

/* ---- Root-owned app scopes (lord-sessiond) ----
 * With lord-sessiond, a session's subtree stays root-owned: the scopes are created by root, with a
 * root-owned cgroup.procs, so an app cannot move itself to another cgroup (it would escape its
 * per-app firewall rules and accounting). cgroup_enter() asks lord-sessiond over CGROUP_SOCK to
 * place the calling process; without lord-sessiond (development) it self-migrates as before. */
#define CGROUP_SOCK "/run/lordos/cgroup.sock"

/* 1 if lord-sessiond accepts creating this scope name. */
int cgroup_scope_name_ok(const char *name);

/* chown root:root a session subtree (lordos-session-<uid>, its leaves and scopes). Root only. */
void cgroup_harden_session(int sessfd);

/* lord-sessiond: serve one connection on CGROUP_SOCK ("enter <scope> <memhigh> <memmax> <cpu%>").
 * Only a peer that is in its session's «server» leaf and runs a binary from bindir can be placed,
 * and only itself (SO_PEERCRED pid). Answers "ok" or "err <reason>". 0 if placed. */
int cgroup_place_serve(int cfd, const char *bindir, int insecure);

#endif

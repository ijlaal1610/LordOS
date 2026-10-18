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

/* Lo llama lord-sessiond (root) en el hijo de una sesión, antes del execve de lord-server:
 * crea un subárbol de cgroup para la sesión, mete al proceso en una hoja «server» y le cede el
 * subárbol al usuario (chown) con memoria/CPU/pids habilitados, para que lord-server pueda crear
 * las scopes de las apps sin root. Mejor esfuerzo: si no hay delegación (Delegate=yes en
 * lordos.service), avisa y sigue. Devuelve 0 si quedó delegado. Ver docs/SESIONES.md. */
int cgroup_delegate_session(unsigned uid, unsigned gid);

/* Lo llama lord-sessiond al arrancar (root, lordos.service con Delegate=yes): se mueve a la hoja
 * «supervisor» para que la raíz del servicio pueda habilitar memoria/CPU/pids para los
 * subárboles de las sesiones (regla «sin procesos internos» de cgroup v2). 0 si quedó. */
int cgroup_service_init(void);

#endif

/* admin.h — ayudante privilegiado de LordOS (ver admin.c). */
#ifndef LORD_ADMIN_H
#define LORD_ADMIN_H

#include <stddef.h>
#include <sys/types.h>

#define ADMIN_ARG_MAX 65536             /* argumento de una acción (puede tener varias líneas) */

/* ---- lado del servidor (lord-server, sin root) ---- */

/* Se llama como root, antes de soltar privilegios. user = el usuario de la sesión. */
int  admin_start(const char *user);
int  admin_fd(void);                                  /* -1 si no hay ayudante */
int  admin_known(const char *action);                 /* ¿está en la lista cerrada? */
/* Manda una acción; devuelve su id (> 0) o -1. Las respuestas llegan con ese id: LOG (progreso),
 * y al final OK o ERR. Los avisos que no responden a nada (estado de la red, VPN) llegan con
 * id 0 y kind EVENT. Varias acciones pueden correr a la vez. */
int  admin_send(const char *action, const char *arg, size_t len);
void admin_dispatch(void (*fn)(int id, const char *kind, const char *text));

/* ---- lord-netd (root, started by lord-sessiond): the system network; see admin.c, docs/RED.md ---- */
int  admin_netd_serve(void);                          /* returns only on error */

/* ---- lado del ayudante (root): para los módulos que agregan acciones ---- */

typedef struct {
    int id;                             /* de la acción en curso */
    const char *user, *home;            /* el usuario de la sesión */
    uid_t uid;
    gid_t gid;
    int admin;                          /* el usuario de la sesión es administrador */
} AdminReq;

/* Respuestas de la acción en curso: "LOG", "OK" o "ERR" (una línea; los \n se vuelven espacios). */
void admin_say(const char *kind, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Aviso sin pedido (id 0, EVENT): "net link eth0 up", "vpn wg0 conectada"... */
void admin_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Programa de ruta fija con entorno limpio; su salida va como LOG. Devuelve el código de salida. */
int  admin_run(const char *const argv[]);
/* Igual, con `input` por la entrada estándar (p. ej. una contraseña para chpasswd: nunca en argv). */
int  admin_run_input(const char *const argv[], const char *input);
/* Igual, pero guarda la salida (stdout) en out (terminada en 0). */
int  admin_run_capture(const char *const argv[], char *out, size_t cap);

/* Procesos residentes (wpa_supplicant, openvpn, el cliente DHCP...), con un nombre único. Viven
 * hasta que se los para o termina la sesión. La salida de un programa llega como EVENT
 * "daemon <nombre> <línea>"; un proceso con código nuestro (fn) usa admin_event. Al terminar,
 * EVENT "daemon <nombre> exit <código>". */
int  admin_daemon_exec(const char *name, const char *const argv[], const char *const env[]);
int  admin_daemon_fork(const char *name, void (*fn)(void *), void *arg);
int  admin_daemon_stop(const char *name);             /* SIGTERM; 0 si estaba corriendo */
int  admin_daemon_running(const char *name);

#endif

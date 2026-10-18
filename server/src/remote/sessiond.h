/* sessiond.h — lord-sessiond: el administrador de sesiones de LordOS (ver sessiond.c,
 * docs/SESIONES.md y docs/REMOTO.md). Un proceso root con tres partes:
 *   - el principal (sessiond.c): configuración, lista de sesiones, pantalla de ingreso;
 *   - el monitor (monitor.c): le presta la placa y la entrada a la sesión que se ve;
 *   - el escritorio remoto (conn.c, si está prendido): un hijo por cada conexión de la red. */
#ifndef LORD_SESSIOND_H
#define LORD_SESSIOND_H

#include <pthread.h>
#include <pwd.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <openssl/ssl.h>

#define RD_CONF_DIR "/etc/lordos/remote"
#define SD_RUN_DIR "/run/lordos"
#define SD_STATUS SD_RUN_DIR "/status.json"
#define SD_GREETER_RUN SD_RUN_DIR "/greeter"        /* XDG_RUNTIME_DIR de la pantalla de ingreso */
#define SD_SESSION_CONF "/etc/lordos/session.conf"   /* AUTOLOGIN=usuario (vacío: pantalla de ingreso) */
#define SD_USERS_DIR "/var/lib/lordos/users"         /* avatares: <usuario>.json y <usuario>.png */
#define SD_GREETER_USER "lordos-greeter"
#define RD_DEFAULT_PORT 5673          /* "LORD" en el teclado de un teléfono */
#define RD_MAX_CONNS 32
#define SD_MAX_SESSIONS 32

/* Estado compartido entre el proceso principal y sus hijos. */
typedef struct { char key[56]; int fails; double until; } RdFail;          /* key: IP o "monitor:<usuario>" */
typedef struct { char tok[65]; uid_t uid; char user[33]; double expires; } RdToken;
typedef struct { pid_t pid; char ip[48]; char user[33]; double since; } RdConn;
typedef struct { pid_t pid; uid_t uid; char user[33]; double since; } SdSession;    /* lord-server de un usuario */
typedef struct {
    pthread_mutex_t lock;
    RdFail fails[64];
    RdToken tokens[64];
    RdConn conns[RD_MAX_CONNS];
    SdSession sessions[SD_MAX_SESSIONS];
    char monitor[33];           /* usuario que se ve en el monitor ("" = pantalla de ingreso) */
    unsigned version;           /* cambia cuando cambia algo de lo de arriba (status.json) */
} RdShared;

typedef struct {
    int enabled;                /* escritorio remoto prendido (Ajustes → Compartir) */
    int port;
    int group_only;             /* solo el grupo lordos-remote */
    int insecure_dev;           /* permite binarios que no son de root (solo VMs de desarrollo) */
    char server[512], theme[512], greeter_theme[512], fonts[512], shell[512], workdir[512];
} RdConfig;

extern RdShared *rd_shared;
extern RdConfig rd_conf;

double rd_now(void);
void   rd_lock(void);
void   rd_unlock(void);

/* ---- sess.c: usuarios, contraseñas y sesiones (compartido por el monitor y la red) ---- */

void sd_log_prefix(const char *prefix);             /* "lord-sessiond[pid] 1.2.3.4" */
void sd_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  sd_valid_username(const char *u);
/* Cuenta común de una persona: uid 1000..59999, con shell, y (remote + allow=group) del grupo. */
int  sd_user_allowed(const struct passwd *pw, int remote);
int  sd_check_password(const char *user, const char *pass, int remote);
double sd_fail_wait(const char *key);               /* segundos de espera para esa clave */
void sd_fail_note(const char *key, int failed);
int  sd_run_wait(const char *const argv[]);
/* Socket de la sesión de ese usuario si ya corre (lord-server --headless), o -1. */
int  sd_session_connect(uid_t uid);
/* Conecta con la sesión del usuario o la crea (w×h); con un lock por usuario para que el monitor
 * y la red no creen dos a la vez. Devuelve el fd (como root) o -1. */
int  sd_session_get(const struct passwd *pw, int w, int h);
/* La pantalla de ingreso (lord-server --greeter como lordos-greeter): la crea si no corre. */
int  sd_greeter_get(int w, int h);
int  sd_session_running(uid_t uid);                 /* ¿hay un lord-server vivo de ese usuario? */
/* Usuario de sistema sin login para la pantalla de ingreso o el monitor; lo crea si falta. */
struct passwd *sd_system_user(const char *name, const char *home);

/* ---- netsup.c: lord-netd, la red del sistema (docs/RED.md) ---- */
void sd_netd_start(void);                           /* al arrancar; después, sd_netd_tick */
int  sd_netd_fd(void);                              /* pidfd de un lord-netd adoptado, o -1 */
void sd_netd_event(void);                           /* ese pidfd avisó que terminó */
int  sd_netd_reaped(pid_t pid, int status);         /* 1 si el hijo era lord-netd */
void sd_netd_tick(void);                            /* lo vuelve a lanzar cuando toca */

/* "LORD_GPU=0" con lordos.gpu=0 en la línea del kernel (o el LORD_GPU del servicio), o vacío */
extern char sd_gpu_env[24];

/* ---- conn.c: una conexión del escritorio remoto (en un proceso hijo) ---- */
void rd_serve(SSL_CTX *ctx, int fd, const char *ip);

/* ---- enc.c: rectángulo (0xAARRGGBB) → PNG o JPEG. Devuelve malloc; *jpeg = 1 si eligió JPEG. */
unsigned char *rd_encode(const uint32_t *px, int stride, int w, int h, int allow_jpeg, int *len, int *jpeg);
unsigned char *rd_encode_rgba(const uint32_t *px, int w, int h, int *len);   /* cursor, con alfa */

/* ---- monitor.c: prestarle el monitor a una sesión ---- */
int  sd_mon_open(void);                             /* 0 si hay placa de video (y pone la consola en modo gráfico) */
void sd_mon_close(void);
int  sd_mon_show(int session_fd, const char *who);  /* le da la placa y la entrada; se queda con el fd */
int  sd_mon_fd(void);                               /* socket de la sesión que se ve, o -1 */
int  sd_mon_read(void (*fn)(int type, const char *json));   /* -1 si la sesión se cerró */
void sd_mon_greeter(const char *json);              /* RP_GREETER_IN a la pantalla de ingreso */
void sd_mon_lost(void);                             /* se la saca (revoca) */
void sd_mon_child(void);                            /* en un hijo: cierra las copias sin revocar */
int  sd_mon_hotplug_fd(void);                       /* inotify de /dev/input, o -1 */
void sd_mon_hotplug(void);                          /* dispositivos enchufados o quitados */

/* Mensajes de los trabajadores al principal (socketpair SOCK_SEQPACKET; una línea, a veces con
 * un fd): "show g" + fd · "show u <usuario>" + fd · "greeter-in <json>" */
int  sd_ctl_send(int ctl, const char *text, int fd);
int  sd_ctl_recv(int ctl, char *buf, size_t cap, int *fd);   /* largo, 0 si se cerró, -1 error */

#endif

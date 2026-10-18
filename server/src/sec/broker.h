/* broker.h — el «broker»: el único canal entre una app .lpk aislada (lord-server --app) y
 * lord-server. El SDK dentro del proceso de la app proxyea las llamadas lord.* por este socket;
 * lord-server verifica el permiso de cada una (server/src/sec/perms.c) y, si hace falta, dibuja
 * el aviso del sistema. Ver appclient.c (lado app), brokersrv.c (lado servidor) y
 * docs/PRIVACIDAD.md.
 *
 * Protocolo: mensajes SOCK_SEQPACKET. Cada mensaje = BrokerHdr + payload (texto/JSON). Las
 * respuestas llevan el mismo reqid. El picker de archivos (powerbox) y los dispositivos pasan
 * fds por SCM_RIGHTS. */
#ifndef LORD_SEC_BROKER_H
#define LORD_SEC_BROKER_H

#include <stddef.h>
#include <stdint.h>
#include <poll.h>

#define BROKER_FD_ENV "LORD_BROKER_FD"
#define BROKER_MAX_MSG (1024 * 1024)

enum BrokerOp {
    BOP_FS_HOME = 1,    /* → ruta de la carpeta de datos de la app */
    BOP_FS_READ,        /* payload: ruta relativa a la carpeta de datos → contenido */
    BOP_FS_WRITE,       /* payload: "ruta\0contenido" dentro de la carpeta de datos */
    BOP_FS_LIST,        /* payload: ruta → JSON [{name,dir}] */
    BOP_CLIP_GET,       /* (permiso clipboard) → texto del portapapeles */
    BOP_CLIP_SET,       /* payload: texto → al portapapeles */
    BOP_PERM,           /* payload: nombre del permiso → "1"/"0" (pregunta si hace falta) */
    BOP_DEVICE,         /* payload: "camera on" | "microphone off" → marca el indicador */
    BOP_PICK,           /* (powerbox) abre el diálogo del sistema → fd del archivo elegido */
    BOP_THEME,          /* → JSON de los tokens del tema */
    BOP_QUIT,           /* la app se cierra */
    BOP_STATE,          /* → JSON { theme, locale, keyboard }: el estado actual, al arrancar */
    BOP_MENU,           /* payload: JSON de los menús de la app (para la barra de menú del sistema) */
    BOP_MIC,            /* payload "1"/"0": pedir/soltar el micrófono. Con permiso, → fd de solo lectura del anillo */
    BOP_EVENT = 100,    /* servidor → app, sin pedido (reqid 0): payload "tipo\0json" */
};

enum BrokerStatus { BST_OK = 0, BST_DENIED = 1, BST_ERROR = 2, BST_PENDING = 3 };

typedef struct {
    uint32_t reqid;
    uint16_t op;        /* BrokerOp (pedido) */
    uint16_t status;    /* BrokerStatus (respuesta); 0xffff en un pedido */
    uint32_t len;       /* bytes de payload que siguen */
} BrokerHdr;

#define BROKER_REQ 0xffff

/* ---- lado servidor (lord-server, brokersrv.c) ---- */

/* Registra un broker nuevo para la app <id> (pid, para el cgroup/monitor). Devuelve -1 si no hay
 * lugar. El fd lo poll­ea main.c. */
int  brokersrv_add(int fd, const char *id, int pid);
/* fds para poll(); devuelve cuántos escribió y sus índices. */
int  brokersrv_pollfds(struct pollfd *pfd, int max);
void brokersrv_dispatch(struct pollfd *pfd, int n);
/* Entrega a JS los avisos pendientes (prompts) y los cambios de indicador. */
typedef void (*BrokerEventFn)(const char *kind, const char *json);
void brokersrv_events(BrokerEventFn fn);
/* Respuesta del usuario a un aviso (desde JS: lord.broker.resolve). */
void brokersrv_resolve(uint32_t reqid, int allow);
int  brokersrv_count(void);
/* El tema publica su estado (tipo "theme" o "locale", JSON) y se reenvía en vivo a todas las
 * apps aisladas; también se guarda para las que arranquen después (BOP_STATE). */
int  brokersrv_publish(const char *kind, const char *json, size_t len);
/* En cada vuelta del bucle: reenvía la distribución de teclado si cambió y reintenta los envíos
 * que no entraron (el servidor nunca se bloquea esperando a una app). */
void brokersrv_tick(void);
/* El usuario revocó un permiso en Ajustes → Privacidad: se le avisa a la app (que suelta el
 * dispositivo) y se apaga su indicador. */
void brokersrv_revoke(const char *app, const char *perm);
/* El usuario eligió un archivo en el diálogo del sistema (powerbox): lord-server lo abre y le pasa
 * el fd a la app. path vacío o NULL = canceló. */
void brokersrv_pick_result(uint32_t reqid, const char *path);
/* El usuario eligió un ítem del menú de una app (desde la barra de menú del sistema): se le avisa
 * a la app por el broker. */
void brokersrv_menu_click(const char *app, const char *item);
/* JSON de los menús que publicó una app (para la barra de menú del sistema); "[]" si no hay. */
const char *brokersrv_menus(const char *app);
/* El micrófono (y después la cámara) corre en lord-server: hay que atender su socket de PipeWire
 * en el bucle principal. */
int  brokersrv_audio_pollfds(struct pollfd *pfd, int max);
void brokersrv_audio_dispatch(struct pollfd *pfd, int n);


#endif

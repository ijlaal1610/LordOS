/* secretsvc.h — la API freedesktop Secret Service (org.freedesktop.secrets) sobre D-Bus, con un
 * cliente D-Bus propio y mínimo (dbus.h: sin libdbus ni sd-bus). Ver docs/CONTRASENAS.md,
 * «Secret Service».
 *
 * Así Chromium, Firefox, libsecret/secret-tool y las demás apps Linux guardan sus contraseñas en
 * el llavero de LordOS sin gnome-keyring ni kwallet. Corre en lord-vault: lord-server conecta el
 * socket del bus de la sesión y se lo pasa ya abierto (el ayudante no puede abrir sockets).
 *
 * Objetos:
 *   /org/freedesktop/secrets                       Service (OpenSession, SearchItems, Unlock,
 *                                                  Lock, GetSecrets, ReadAlias, SetAlias,
 *                                                  CreateCollection; prop. Collections)
 *   /org/freedesktop/secrets/collection/login      Collection (alias "default")
 *   /org/freedesktop/secrets/collection/login/<id> Item
 *   /org/freedesktop/secrets/session/<n>           Session (algoritmos "plain" y
 *                                                  "dh-ietf1024-sha256-aes128-cbc-pkcs7")
 *   /org/freedesktop/secrets/prompt/<n>            Prompt (desbloquear con aviso del sistema)
 * El almacenamiento es del llavero (keyring.c), por la interfaz de abajo. */
#ifndef LORD_VAULT_SECRETSVC_H
#define LORD_VAULT_SECRETSVC_H

#include <stddef.h>
#include <stdint.h>

typedef struct { const char *name, *value; } SsAttr;

typedef struct {
    char id[40];               /* id del ítem (hex, sin '/') */
    char *label;               /* del backend; liberar con ss_info_free */
    SsAttr *attrs; int nattrs;
    uint64_t created, modified;
} SsItemInfo;

/* Lo que pone el llavero (keyring.c). Devuelven 0 bien, -1 error; los punteros que entregan
 * (ss_info, secretos) se liberan con free_info/free_secret del mismo backend. */
typedef struct {
    int  (*locked)(void *ud);                                           /* 1 si el llavero está bloqueado */
    /* Pedido de desbloqueo de una app: 0 = ya está desbloqueado, 1 = se mostró un aviso al
     * usuario (contestar después con ss_prompt_done), -1 = no se puede. */
    int  (*request_unlock)(void *ud, unsigned prompt_id);
    int  (*lock)(void *ud);
    /* busca ítems cuyos atributos contienen todos los pedidos; ids[i] de hasta 40 bytes */
    int  (*search)(void *ud, const SsAttr *attrs, int nattrs, char (*ids)[40], int max);
    int  (*info)(void *ud, const char *id, SsItemInfo *out);
    void (*free_info)(void *ud, SsItemInfo *in);
    int  (*get_secret)(void *ud, const char *id, uint8_t **secret, size_t *len, char ctype[64]);
    void (*free_secret)(void *ud, uint8_t *secret, size_t len);
    /* crea (o reemplaza si replace y hay uno con los mismos atributos) → out_id */
    int  (*create)(void *ud, const char *label, const SsAttr *attrs, int nattrs,
                   const uint8_t *secret, size_t len, const char *ctype, int replace, char out_id[40]);
    int  (*set_secret)(void *ud, const char *id, const uint8_t *secret, size_t len, const char *ctype);
    int  (*set_label)(void *ud, const char *id, const char *label);
    int  (*set_attrs)(void *ud, const char *id, const SsAttr *attrs, int nattrs);
    int  (*remove)(void *ud, const char *id);
    /* quién llama (nombre único del bus y su pid, si el bus lo dio): para el registro y avisos */
    void (*caller)(void *ud, const char *sender, uint32_t pid);
} SsBackend;

/* Arranca sobre un socket ya conectado al bus de la sesión: SASL EXTERNAL, Hello y
 * RequestName("org.freedesktop.secrets", DO_NOT_QUEUE). 0 bien; 1 si el nombre ya es de otro
 * (gnome-keyring o kwallet andando): no se atiende; -1 error. */
int  ss_start(int busfd, const SsBackend *be, void *ud);
int  ss_fd(void);                 /* para poll(); -1 si no arrancó */
/* Atiende lo que haya en el socket (no bloquea). -1 si el bus se cerró. */
int  ss_dispatch(void);
/* Resultado del aviso de desbloqueo prompt_id (1 desbloqueado, 0 cancelado): emite Completed. */
void ss_prompt_done(unsigned prompt_id, int unlocked);
/* Avisar a los clientes que cambió algo (señales ItemCreated/Changed/Deleted, prop. Locked). */
void ss_notify_locked(int locked);
void ss_notify_item(const char *id, int what);  /* 0 creado, 1 cambiado, 2 borrado */
void ss_stop(void);

/* ---- agregados ---- */
int  ss_pending_out(void);        /* 1 si quedó algo por mandar: sumar POLLOUT al poll() */
const char *ss_unique_name(void); /* ":1.42" del bus, o "" */
/* Notas:
 *   - El estado de la conexión usa malloc (borrado con explicit_bzero), no smem: sm_wipe_all() al
 *     bloquear no la rompe. Los secretos de cada pedido pasan por smem y se borran en el acto.
 *   - No llamar a ss_dispatch() al mismo tiempo que sm_wipe_all() desde otro hilo.
 *   - ss_notify_item() es para cambios hechos desde otro lado (la app); los que llegan por D-Bus ya
 *     emiten su señal. */
/* Aritmética de la sesión DH (también para las pruebas): base^exp mod mod, todo big endian de
 * 128 bytes, mod impar y base < mod. 0 bien. */
int  ss_dh_modexp(const uint8_t base[128], const uint8_t exp[128], const uint8_t mod[128], uint8_t out[128]);
const uint8_t *ss_dh_prime(void); /* el primo de 1024 bits del grupo 2 de RFC 2409 */

#endif

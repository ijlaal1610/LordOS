/* vault_int.h — lo que comparten las partes de lord-vault (vault.c: proceso, canales y pedidos;
 * keyring.c: el llavero del sistema y Secret Service). */
#ifndef LORD_VAULT_INT_H
#define LORD_VAULT_INT_H

#include <stddef.h>
#include <stdint.h>
#include "vfile.h"
#include "vmodel.h"
#include "vjson.h"
#include "vwire.h"

enum { V_MAIN = 0, V_LOGIN = 1, V_COUNT };
enum { ROLE_SYSTEM = 1, ROLE_APP = 2 };

/* adjunto agregado y todavía no guardado (cifrado, en memoria) */
typedef struct { uint8_t id[16], nonce[24], mac[16]; uint8_t *cipher; size_t len; } PendAtt;

typedef struct {
    const char *file;          /* nombre en la carpeta de bóvedas */
    int unlocked;
    VHeader h;                 /* ranuras (también bloqueada, si el archivo existe) */
    int exists;
    uint8_t *vk;               /* 32 bytes en smem; NULL bloqueada */
    VModel m;
    int fd;                    /* archivo actual abierto (origen de los adjuntos) o -1 */
    PendAtt *pend; int npend;
    double last_use;
    int autolock_s;            /* 0 = nunca (el llavero de la sesión) */
} Vault;

typedef struct { int fd, role; VwRx rx; } Chan;

extern Vault vaults[V_COUNT];
extern int vault_dirfd;

/* respuestas */
void reply(Chan *c, uint32_t id, VjB *b);             /* libera b */
void reply_err(Chan *c, uint32_t id, const char *code);
void reply_ok(Chan *c, uint32_t id);
void broadcast_event(const char *json);               /* a todos los canales */

/* bóvedas */
int  vault_save(Vault *v);                            /* VF_OK o error */
void vault_lock(Vault *v);
int  vault_unlock_with(Vault *v, int type, const VFactors *f, char *err, size_t errn);
int  vault_create(Vault *v, int type, const VFactors *f, const VKdf *kdf, const char *label, const char *meta_json, char *recovery_out);

/* llavero (keyring.c) */
int  keyring_handle(Chan *c, uint32_t id, const char *op, const Vj *j);   /* 1 si atendió el op */
void keyring_session_password(const char *pass);      /* al ingresar: abrir o crear login.lvault */
void keyring_start_bus(int busfd);
int  keyring_bus_fd(void);
int  keyring_bus_wants_out(void);
void keyring_bus_dispatch(void);
void keyring_lock_changed(void);
void keyring_prompt_result(unsigned prompt, int ok);

#endif

/* vwire.h — protocolo entre lord-vault y sus clientes (lord-server y la app Contraseñas).
 *
 * Socket SOCK_SEQPACKET. Cada mensaje: VHdr + JSON (UTF-8). Lo que no entra en un paquete
 * (VW_CHUNK) se manda en varios con VWF_MORE, todos con el mismo id; el que recibe los junta. Los
 * fds (SCM_RIGHTS, hasta VW_MAX_FDS) van en el primer paquete. No se usan memfd: así el texto con
 * secretos nunca queda en memoria compartida (que podría ir al swap); del lado de lord-vault se
 * recibe directo en la arena de secretos (smem.h).
 *
 * Tipos (VHdr.kind):
 *   VW_REQ  cliente → lord-vault: {"op":"…", …}             (id lo elige el cliente)
 *   VW_REP  lord-vault → cliente: {"ok":true,…} | {"error":"código"}   (mismo id)
 *   VW_EVT  lord-vault → cliente: {"ev":"locked"|"changed"|"touch"|"keyring"…}  (id 0)
 *   VW_CTL  lord-server → lord-vault, solo por el canal de control (fd 3): init, attach, lock-all,
 *           session-password, fido-device, bus. La app nunca puede mandarlos. */
#ifndef LORD_VAULT_VWIRE_H
#define LORD_VAULT_VWIRE_H

#include <stddef.h>
#include <stdint.h>

#define VW_CHUNK   (32 * 1024)
#define VW_MAX     (64u << 20)         /* mensaje más grande que se acepta */
#define VW_MAX_FDS 4

enum { VW_REQ = 1, VW_REP = 2, VW_EVT = 3, VW_CTL = 4 };
enum { VWF_MORE = 1 };

typedef struct { uint32_t kind, id, len, flags; } VHdr;

typedef struct {
    uint32_t kind, id;
    char *json; size_t len;            /* terminado en 0 */
    int fds[VW_MAX_FDS]; int nfds;
} VMsg;

/* Asignador para lo recibido: en lord-vault, la arena de secretos; en lord-server, malloc. */
typedef struct { void *(*alloc)(size_t); void *(*realloc)(void *, size_t); void (*free)(void *); } VwAlloc;
extern const VwAlloc vw_malloc;

int  vw_send(int sock, uint32_t kind, uint32_t id, const char *json, size_t len, const int *fds, int nfds);
/* Recibe un mensaje completo (bloquea hasta tenerlo). 0 bien, 1 fin, -1 error/inválido. */
int  vw_recv(int sock, VMsg *m, const VwAlloc *a);
/* Recepción incremental para sockets no bloqueantes (lord-vault atiende varios canales):
 * vw_feed lee lo que haya; devuelve 1 si quedó un mensaje completo en *m, 0 si falta, -1 cerrado. */
typedef struct { VMsg cur; size_t cap; int active; } VwRx;
int  vw_feed(int sock, VwRx *rx, VMsg *m, const VwAlloc *a);
void vw_rx_free(VwRx *rx, const VwAlloc *a);
void vw_msg_free(VMsg *m, const VwAlloc *a);   /* borra el texto (puede tener secretos) y cierra los fds */

#endif

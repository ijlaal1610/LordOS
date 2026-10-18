/* vclient.h — lado de lord-server de lord-vault (ver vault.c y docs/PASSWORDS.md).
 *
 * lord-server lanza el ayudante una vez por sesión (ruta fija: <carpeta de este binario>/lord-vault,
 * sin argumentos, entorno mínimo), le pasa la carpeta de las bóvedas
 * (~/.local/share/lordos/vault) y el socket ya conectado al bus de D-Bus de la sesión (para
 * Secret Service), y después le habla por el canal de control. Un hilo propio lee las respuestas
 * y los eventos y los deja en una cola que JS lee con vc_poll() (lord.vault.poll): el compositor
 * nunca espera a lord-vault. Lo descifrado que pasa por acá (lo que la interfaz muestra) se borra
 * de los búferes de C apenas se entrega. */
#ifndef LORD_VAULT_VCLIENT_H
#define LORD_VAULT_VCLIENT_H

#include <stddef.h>

int   vc_available(void);                     /* 1 si está el binario */
int   vc_start(void);                         /* lo lanza si no corre; 0 bien */
/* Pedido (JSON) con fds opcionales (se quedan con ellos). Devuelve el id (> 0) o -1. */
int   vc_request(const char *json, size_t len, int *fds, int nfds);
/* Respuestas y eventos pendientes: [{"id":n,"kind":"rep"|"evt","data":{…}}] (malloc) o NULL. */
char *vc_poll(void);
void  vc_poll_free(char *s);                  /* borra y libera lo que devolvió vc_poll */
int   vc_pending(void);                       /* pedidos sin respuesta */
/* Control (solo lord-server) */
void  vc_lock_all(const char *why);           /* "screen", "switch", "logout", "idle" */
void  vc_session_password(const char *pass);  /* de lord-sessiond: abrir el llavero de la sesión */
int   vc_fido_attach(char *name, size_t n);   /* abre la primera llave FIDO2 y se la pasa a lord-vault */
void  vc_prompt_done(unsigned prompt, int ok);/* resultado del aviso de desbloqueo de Secret Service */
/* Canal propio para el proceso aislado de la app Contraseñas: devuelve el fd del otro extremo. */
int   vc_attach_app(void);

#endif

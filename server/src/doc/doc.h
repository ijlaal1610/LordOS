/* doc.h — lado de lord-server del ayudante de documentos lord-doc (ver proto.h).
 *
 * Una «sesión» es un proceso lord-doc: un PDF abierto, una extracción, una compresión o el
 * ayudante de utilidades (fuentes, PDF para imprimir, imágenes). Todo es asincrónico: un hilo
 * propio habla con los ayudantes y deja los resultados en una cola que JS lee con doc_poll()
 * (lord.doc.poll). El compositor nunca espera a un ayudante; si uno se cuelga, se lo mata al
 * vencer el plazo del pedido, y si se cae, sus pedidos terminan con un error "crash". */
#ifndef LORD_DOC_DOC_H
#define LORD_DOC_DOC_H

#include <stddef.h>
#include <stdint.h>

typedef struct { int fd; const char *role; } DocInitFd;      /* role: "file" | "ro" | "rw" | "out" */

/* Ruta fija del ayudante: <carpeta de este binario>/lord-doc. 0 si existe. */
int  doc_available(void);
/* Nueva sesión con esos fds de arranque (doc_session se queda con ellos, también si falla).
 * mode: "pdf" | "arc" | "util". Devuelve el id (> 0) o -1. */
int  doc_session(const char *mode, DocInitFd *fds, int n);
/* Pedido a una sesión: op DQ_*, JSON de argumentos, fds extra (se los queda), prioridad (más alta
 * primero) y plazo en ms (0 = sin plazo, pero se mata si pasan 60 s sin progreso). id > 0 o -1. */
int  doc_request(int sid, uint32_t op, const char *json, size_t len, int *fds, int nfds, int prio, int timeout_ms);
int  doc_cancel(int sid, int req);         /* saca de la cola un pedido que no empezó: 1 si estaba */
void doc_close(int sid);
int  doc_alive(int sid);                   /* 1 si la sesión existe y no se cayó */
/* Eventos pendientes como un arreglo JSON (malloc) o NULL si no hay. Cada uno:
 * {sid, req, type: ready|done|err|prog|part|img|blob|crash, data, image, w, h, blob, size} */
char *doc_poll(void);
int  doc_pending(void);                     /* pedidos sin terminar (para saber si seguir leyendo) */

/* Imágenes devueltas (ARGB32 premultiplicado). doc_image_lock deja el puntero válido hasta
 * doc_image_unlock. */
const uint32_t *doc_image_lock(int handle, int *w, int *h);
void doc_image_unlock(void);
void doc_image_release(int handle);
size_t doc_image_bytes(void);

/* Bytes devueltos (un PDF armado o guardado): memfd sellado. */
int  doc_blob_dup(int handle, size_t *size);   /* fd nuevo (CLOEXEC) o -1 */
void doc_blob_release(int handle);
/* Guarda un blob como archivo nuevo (fd ya abierto para escribir). 0 bien. */
int  doc_blob_copy(int handle, int outfd);

#endif

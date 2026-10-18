/* wire.h — mensajes del protocolo de lord-doc (proto.h): enviar y recibir un DocHdr + JSON + fds.
 * Lo usan los dos lados: lord-server (doc.c) y el ayudante (helper.c). */
#ifndef LORD_DOC_WIRE_H
#define LORD_DOC_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include "proto.h"

typedef struct {
    DocHdr h;
    char *json;             /* terminado en 0 (malloc); NULL si no vino */
    size_t len;
    int fds[DOC_MAX_FDS];
    int nfds;
} DocMsg;

/* Envía op/id + JSON (len bytes; si supera DOC_INLINE_MAX va en un memfd sellado) + fds.
 * 0 bien, -1 error (errno). */
int  doc_send(int sock, uint32_t op, uint32_t id, const char *json, size_t len, const int *fds, int nfds);
/* Recibe un mensaje completo (bloqueante). 0 bien, 1 fin del canal, -1 error o mensaje inválido.
 * Los fds recibidos quedan en m->fds (con CLOEXEC); liberar con doc_msg_free. */
int  doc_recv(int sock, DocMsg *m);
void doc_msg_free(DocMsg *m);

/* memfd sellado (SHRINK/GROW/WRITE) con los bytes dados; -1 si falla. */
int  doc_memfd(const char *name, const void *data, size_t len);
/* memfd de len bytes para escribir con mmap y sellar después con doc_seal. */
int  doc_memfd_new(const char *name, size_t len);
int  doc_seal(int fd);
/* Verifica que un memfd ajeno esté sellado contra achicarse (si no, un ftruncate del otro lado
 * provocaría SIGBUS al leerlo) y tenga al menos min bytes. Devuelve el tamaño o -1. */
long doc_check_sealed(int fd, size_t min);

/* Constructor de texto JSON (crece solo). */
typedef struct { char *p; size_t n, cap; int oom; } JBuf;
void jb_init(JBuf *b);
void jb_free(JBuf *b);
void jb_raw(JBuf *b, const char *s);
void jb_printf(JBuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void jb_str(JBuf *b, const char *s);        /* "texto" con escapes (UTF-8 inválido → U+FFFD) */
void jb_strn(JBuf *b, const char *s, size_t n);
void jb_num(JBuf *b, double v);             /* número JSON (sin NaN/Inf) */

#endif

/* helper.h — lo que comparten las partes del ayudante lord-doc (helper.c, h_pdf.c, h_arc.c,
 * h_font.c, h_make.c). Corre ya encerrado: no abre archivos por ruta, solo usa los fds que le
 * pasó lord-server. */
#ifndef LORD_DOC_HELPER_H
#define LORD_DOC_HELPER_H

#include <stddef.h>
#include <stdint.h>
#include "proto.h"
#include "wire.h"
#include "../sec/json.h"

/* fds de arranque (DQ_INIT_FD), por número de casilla */
typedef struct { int fd; char role[8]; } Slot;
extern Slot hslots[DOC_INIT_FDS];
extern int hnslots;
int slot_fd(const JVal *args, const char *key);     /* fd de la casilla args[key] o -1 */

/* Respuestas al pedido id */
void reply_json(uint32_t kind, uint32_t id, JBuf *b);        /* libera b */
void reply_err(uint32_t id, const char *code, const char *detail);
void reply_done_empty(uint32_t id);
/* Imagen ARGB32 premultiplicada (stride = w*4) en un memfd sellado; extra = JSON adicional
 * ya armado sin llaves (",\"present\":[...]") o NULL. */
void reply_image(uint32_t id, const uint32_t *px, int w, int h, const char *extra);
/* Píxeles en un memfd ya escrito (lo cierra). */
void reply_image_fd(uint32_t id, int fd, int w, int h, const char *extra);
void reply_blob(uint32_t id, int fd, size_t size);           /* memfd sellado; lo cierra */
int  hsock(void);

/* Cada parte atiende sus pedidos; devuelven 0 si el op no es suyo. */
int pdf_handle(const DocMsg *m, const JVal *args);
int arc_handle(const DocMsg *m, const JVal *args);
int font_handle(const DocMsg *m, const JVal *args);
int make_handle(const DocMsg *m, const JVal *args);
void pdf_thumb(const DocMsg *m, const JVal *args);     /* sesión util: miniatura de un PDF */
void pdf_warm(uint32_t id);

/* Invierte la luminosidad de una imagen conservando el color (modo oscuro del visor). */
void invert_lightness(uint32_t *px, size_t n);

#endif

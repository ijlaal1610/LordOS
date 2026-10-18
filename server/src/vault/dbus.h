/* dbus.h — cliente D-Bus propio y mínimo (sin libdbus ni sd-bus), lo justo para atender el
 * Secret Service (secretsvc.c) desde lord-vault. Ver docs/CONTRASENAS.md, «Secret Service».
 *
 *   - Mensajes (especificación D-Bus 0.41): cabecera fija + campos a(yv) + cuerpo, alineación
 *     por tipo, little endian al escribir y los dos órdenes al leer. Tipos: y b n q i u x t d s
 *     o g a ( ) { } v (sin h: no se pasan fds).
 *   - Lectura estricta: dbus_msg_parse valida TODO antes de entregar el mensaje (largos, firmas,
 *     profundidad, textos terminados en NUL y sin NUL adentro, booleanos 0/1, rutas de objeto,
 *     alineación y relleno en cero, tope de 128 MiB). Después el lector tipado (DRd) solo lee
 *     lo ya validado y igual controla los límites.
 *   - Conexión: SASL EXTERNAL sobre un socket YA conectado (lord-vault no puede abrir sockets:
 *     se lo pasa lord-server), Hello, RequestName; envío sin bloquear con una cola de salida.
 *
 * Los búferes de la conexión son de malloc (no de la arena de secretos, que se borra entera al
 * bloquear) y se borran con explicit_bzero antes de soltarse o reusarse. */
#ifndef LORD_VAULT_DBUS_H
#define LORD_VAULT_DBUS_H

#include <stddef.h>
#include <stdint.h>

#define DBUS_MAX_MSG    (128u * 1024 * 1024)
#define DBUS_MAX_DEPTH  32               /* contenedores anidados (la especificación permite 64) */

enum { DBUS_CALL = 1, DBUS_RETURN = 2, DBUS_ERROR = 3, DBUS_SIGNAL = 4 };
enum { DBUS_NO_REPLY = 1, DBUS_NO_AUTOSTART = 2 };

/* Un mensaje recibido. Los textos apuntan adentro de raw (terminados en NUL); NULL si no vino. */
typedef struct {
    uint8_t type, flags, big_endian;
    uint32_t serial, reply_serial;
    const char *path, *iface, *member, *error, *dest, *sender, *sig;
    const uint8_t *body;
    uint32_t body_len;
    uint8_t *raw;                        /* malloc; dbus_msg_free lo borra */
    size_t raw_len;
} DMsg;

/* Largo total del mensaje que empieza en buf (al menos 16 bytes), 0 si faltan bytes para saberlo
 * o -1 si la cabecera es inválida o pasa el tope. */
long dbus_msg_size(const uint8_t *buf, size_t len);
/* Valida y arma m a partir de una COPIA de buf[0..len) (len = dbus_msg_size). 0 bien. */
int  dbus_msg_parse(const uint8_t *buf, size_t len, DMsg *m);
void dbus_msg_free(DMsg *m);

/* Validaciones sueltas */
int  dbus_sig_valid(const char *sig);            /* firma completa (varios tipos), ≤ 255 */
int  dbus_sig_single(const char *sig, size_t *n);/* largo de UN tipo completo al comienzo; 0 bien */
int  dbus_path_valid(const char *p);
int  dbus_name_valid(const char *n);             /* nombre de interfaz/bus (sin ':' único) */
/* Valida un cuerpo con esa firma (alineación relativa a 'base', que tiene que estar alineado a 8). */
int  dbus_body_valid(const char *sig, const uint8_t *body, size_t len, int big_endian);

/* ---- lector tipado del cuerpo ---- */
typedef struct {
    const uint8_t *b;
    size_t len, pos;
    int be, err;
} DRd;
void     dr_init(DRd *r, const DMsg *m);
uint8_t  dr_u8(DRd *r);
int      dr_bool(DRd *r);
uint32_t dr_u32(DRd *r);
int32_t  dr_i32(DRd *r);
uint64_t dr_u64(DRd *r);
const char *dr_str(DRd *r, uint32_t *len);       /* s, o (y g con dr_sig) */
const char *dr_sig(DRd *r);
/* ay: puntero a los bytes y largo */
const uint8_t *dr_bytes(DRd *r, uint32_t *len);
/* arreglo: dr_arr devuelve el fin; mientras dr_more(r, fin) hay elementos. elem_align: 1/4/8 */
size_t   dr_arr(DRd *r, int elem_align);
int      dr_more(DRd *r, size_t end);
void     dr_struct(DRd *r);                      /* alinea a 8 */
/* v: firma del contenido (para comparar) */
const char *dr_variant(DRd *r);
/* Saltea un valor de tipo completo sig (ya validado). */
void     dr_skip(DRd *r, const char *sig);

/* ---- escritor ---- */
typedef struct { uint8_t *p; size_t n, cap; int oom; } DBuf;
void   dw_init(DBuf *w);
void   dw_free(DBuf *w);                         /* borra y libera */
void   dw_align(DBuf *w, int a);
void   dw_u8(DBuf *w, uint8_t v);
void   dw_bool(DBuf *w, int v);
void   dw_u32(DBuf *w, uint32_t v);
void   dw_i32(DBuf *w, int32_t v);
void   dw_u64(DBuf *w, uint64_t v);
void   dw_str(DBuf *w, const char *s);           /* s u o */
void   dw_strn(DBuf *w, const char *s, size_t n);
void   dw_sig(DBuf *w, const char *s);           /* g */
void   dw_bytes(DBuf *w, const uint8_t *p, size_t n);   /* ay */
size_t dw_arr_begin(DBuf *w, int elem_align);   /* devuelve una marca para dw_arr_end */
void   dw_arr_end(DBuf *w, size_t mark);
void   dw_struct(DBuf *w);                       /* alinea a 8 */
void   dw_variant(DBuf *w, const char *sig);     /* escribe la firma; después el valor */

/* Arma un mensaje completo (cabecera + cuerpo). Devuelve 0 bien; out en malloc. */
typedef struct {
    uint8_t type, flags;
    uint32_t serial, reply_serial;
    const char *path, *iface, *member, *error, *dest, *sig;
    const char *sender;                  /* solo lo pone el bus (y las pruebas que hacen de bus) */
} DHdr;
int dbus_build(const DHdr *h, const DBuf *body, DBuf *out);

/* ---- conexión ---- */
typedef struct {
    int fd;
    uint32_t serial;
    char unique[256];
    uint8_t *in; size_t in_n, in_cap;
    uint8_t *out; size_t out_n, out_off, out_cap;
    int dead;
} DConn;

/* Conecta (sin autenticar) al bus de la sesión: DBUS_SESSION_BUS_ADDRESS (unix:path= o
 * unix:abstract=) o $XDG_RUNTIME_DIR/bus. Para lord-server, fuera de la caja de arena. fd o -1. */
int  dbus_connect_session(void);
/* System bus: DBUS_SYSTEM_BUS_ADDRESS (unix:path=) or /run/dbus/system_bus_socket. fd or -1. */
int  dbus_connect_system(void);
/* SASL EXTERNAL + Hello sobre un socket conectado (espera hasta timeout_ms). Deja el fd sin
 * bloquear. 0 bien. */
int  dbus_open(DConn *c, int fd, int timeout_ms);
void dbus_close(DConn *c);                        /* cierra el fd y borra los búferes */
/* Encola un mensaje (h->serial se completa si es 0) y trata de mandarlo. Devuelve el serial o 0. */
uint32_t dbus_send(DConn *c, DHdr *h, const DBuf *body);
int  dbus_flush(DConn *c);                        /* manda lo que se pueda; -1 si se cortó */
int  dbus_pending_out(const DConn *c);
/* Lee lo que haya (sin bloquear) y entrega el próximo mensaje completo en m: 1 hay mensaje,
 * 0 no hay (por ahora), -1 se cortó o llegó algo inválido. */
int  dbus_next(DConn *c, DMsg *m);
/* Espera bloqueando (hasta timeout_ms) la respuesta a serial; los demás mensajes se descartan.
 * Solo para el arranque. 1 respuesta (en m), 0 error D-Bus (en m), -1 corte o tiempo. */
int  dbus_wait_reply(DConn *c, uint32_t serial, DMsg *m, int timeout_ms);

/* Respuestas y señales de conveniencia */
uint32_t dbus_reply(DConn *c, const DMsg *call, const char *sig, const DBuf *body);
uint32_t dbus_error(DConn *c, const DMsg *call, const char *name, const char *text);
uint32_t dbus_signal(DConn *c, const char *path, const char *iface, const char *member, const char *sig, const DBuf *body);

#endif

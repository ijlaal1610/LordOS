/* mime.h — mensajes de correo (RFC 5322, MIME 2045-2049, 2047, 2231) para lord-maild.
 *
 * Todo trabaja sobre el mensaje crudo en memoria y verifica cada índice contra el largo:
 * un mensaje roto o malicioso da partes vacías, nunca una lectura fuera del búfer. Lo que se
 * entrega a la interfaz siempre es UTF-8 válido (lo inválido pasa a U+FFFD). */
#ifndef LORD_MAIL_MIME_H
#define LORD_MAIL_MIME_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MIME_MAX_PARTS 256
#define MIME_MAX_DEPTH 16

typedef struct {
    int parent;               /* índice de la parte madre (-1 la raíz) */
    int depth;
    char type[48], subtype[64];       /* en minúsculas; "text"/"plain" si no hay Content-Type */
    char charset[40];
    char boundary[128];
    char encoding[24];                /* content-transfer-encoding en minúsculas */
    char disposition[16];             /* "inline" | "attachment" | "" */
    char filename[256];               /* UTF-8 (2231/2047 ya decodificados) */
    char cid[160];                    /* Content-ID sin <> */
    char method[24];                  /* text/calendar; method= */
    const char *hdr; size_t hlen;     /* encabezado crudo de la parte */
    const char *body; size_t blen;    /* cuerpo crudo (codificado) */
} MimePart;

typedef struct {
    MimePart parts[MIME_MAX_PARTS];
    int nparts;
    int truncated;                    /* había más partes de las que entran */
} MimeMsg;

/* Separa encabezado y cuerpo de un mensaje o parte (CRLF o LF). */
void mime_split(const char *m, size_t len, const char **hdr, size_t *hlen, const char **body, size_t *blen);
/* Valor de un encabezado (primera aparición), desplegado (sin CRLF+espacios), crudo. 1 si estaba. */
int  mime_header(const char *hdr, size_t hlen, const char *name, char *out, size_t cap);
/* Igual pero en memoria nueva (malloc), para encabezados largos (References). NULL si no está. */
char *mime_header_dup(const char *hdr, size_t hlen, const char *name);
/* Decodifica palabras codificadas (=?cs?B|Q?...?=) a UTF-8 y limpia controles. */
void mime_decode_words(const char *in, char *out, size_t cap);
/* Parámetro de un valor estructurado ("text/plain; charset=utf-8"), con RFC 2231 (name*=, name*0*=). */
int  mime_param(const char *value, const char *name, char *out, size_t cap);
/* Arma el árbol de partes. Siempre deja al menos la raíz. */
void mime_parse(const char *m, size_t len, MimeMsg *out);
/* Cuerpo decodificado (base64, quoted-printable, 7bit/8bit/binary) en memoria nueva. */
char *mime_decode_body(const MimePart *p, size_t *olen);
/* Texto de una parte text/* en UTF-8 (charset convertido). */
char *mime_part_text(const MimePart *p, size_t *olen);

/* Juegos de caracteres → UTF-8 (UTF-8, ASCII, Latin-1, la familia ISO-8859 y windows-125x, KOI8,
 * UTF-16/32 con o sin BOM). Desconocido: se toma como UTF-8 con reemplazo. malloc. */
char *charset_to_utf8(const char *cs, const char *in, size_t len, size_t *olen);
int  charset_known(const char *cs);
/* Copia UTF-8 reemplazando las secuencias inválidas por U+FFFD. malloc. */
char *utf8_clean(const char *in, size_t len, size_t *olen);

/* base64 y quoted-printable */
size_t b64_decode(const char *in, size_t len, unsigned char *out);   /* out ≥ len*3/4+3 */
char  *b64_encode(const unsigned char *in, size_t len, int wrap, size_t *olen);  /* wrap 76 con CRLF */
size_t qp_decode(const char *in, size_t len, char *out, int header); /* out ≥ len */
char  *qp_encode(const char *in, size_t len, size_t *olen);          /* cuerpo, líneas ≤ 76, CRLF */

/* Una dirección de una lista (From, To, Cc). */
typedef struct { char name[160]; char email[256]; } MimeAddr;
/* Separa una lista de direcciones (con nombres entre comillas, comentarios y grupos). Devuelve
 * cuántas dejó en out (como mucho max). El valor ya tiene que venir con las palabras decodificadas
 * o no: se decodifican los nombres acá. */
int  mime_addrs(const char *value, MimeAddr *out, int max);
/* Fecha RFC 5322 ("Tue, 3 Jun 2025 10:22:01 -0300") → segundos UNIX, 0 si no se entiende. */
time_t mime_date(const char *value);
/* Fecha para el encabezado Date (hora local con su desplazamiento). */
void mime_format_date(time_t t, char *out, size_t cap);
/* Encabezado con palabras codificadas si hace falta (UTF-8 → =?UTF-8?B?...?=, ≤ 75 por palabra). */
char *mime_encode_header(const char *utf8);
/* Una dirección para un encabezado: "Nombre" <a@b> con el nombre codificado si hace falta. */
char *mime_format_addr(const char *name, const char *email);
/* Asunto sin "Re:", "RE:", "Fwd:", "RV:", "AW:"… repetidos, para agrupar conversaciones. */
void mime_base_subject(const char *subj, char *out, size_t cap);

#endif

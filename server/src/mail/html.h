/* html.h — el HTML del correo, sin navegador (ver docs/MAIL.md, «HTML sin ejecutar nada»).
 *
 * LordOS no tiene motor web: el HTML de un mensaje nunca se ejecuta ni se «carga». Este
 * sanitizador lo lee con un tokenizador propio y tolerante y arma un árbol chico, en JSON, que
 * Mail dibuja con sus vistas nativas (View/Text/Image):
 *
 *   {"v":1,"bg":"#fff","blocks":[nodo…],"remote":["https://…"],"links":[{"h":"…","w":"deceptive"}],
 *    "stats":{"remote":3,"trackers":1,"warn":1,"cid":2,"data":0}}
 *   nodo: {"t":"blk","s":{…},"c":[…]}  bloque (div, p, h1-6, blockquote, pre, center, li)
 *         {"t":"tbl","s":{…},"c":[{"t":"tr","c":[{"t":"td","s":{…},"c":[…]}]}]}
 *         {"t":"img","src":"cid:x"|"r:N"|"d:N","w":600,"h":200,"alt":"…","l":N}
 *         {"t":"hr"}  {"t":"br"}
 *         {"t":"x","x":"texto","s":{b,i,u,st,mono,c,bg,fs},"l":N}   texto con estilo; l = índice del enlace
 *
 * Qué queda afuera, siempre: script, style, iframe, object, embed, form y sus controles, svg,
 * math, audio/video, comentarios, <!DOCTYPE>, eventos (on*), url() de CSS, javascript:/vbscript:/
 * data: en los enlaces (los enlaces solo pueden ser http, https y mailto) y todo lo de display:none.
 * Las imágenes remotas no se piden acá: quedan en "remote" y Mail decide (bloqueadas de entrada).
 * Las de 1×1 o 2×2 (píxeles de seguimiento) se cuentan en "trackers" y no se muestran nunca.
 * Enlaces engañosos: si el texto visible parece una dirección web y su dominio no es el del href
 * (o el href lleva usuario@, una IP o un esquema raro), el enlace sale con "w". */
#ifndef LORD_MAIL_HTML_H
#define LORD_MAIL_HTML_H

#include <stddef.h>

#define HTML_MAX_INPUT (8u << 20)
#define HTML_MAX_NODES 40000
#define HTML_MAX_DEPTH 48
#define HTML_MAX_REMOTE 200
#define HTML_MAX_LINKS 1000

typedef struct { int remote, trackers, warn, cid, data, nodes, truncated; } HtmlStats;

/* JSON del árbol (malloc, siempre válido) o NULL sin memoria. */
char *html_sanitize(const char *html, size_t len, size_t *olen, HtmlStats *st);
/* Texto plano del HTML (para la vista previa, la búsqueda y la parte text/plain al responder). */
char *html_to_text(const char *html, size_t len, size_t *olen);
/* Dominio (host en minúsculas, sin "www.") de una URL http(s); "" si no tiene. */
void  html_url_host(const char *url, char *out, size_t cap);
/* ¿El texto visible de un enlace engaña sobre su destino? Devuelve un motivo o NULL. */
const char *html_link_warning(const char *text, const char *href);

#endif

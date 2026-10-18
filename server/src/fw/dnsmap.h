/* dnsmap.h — qué nombres corresponden a una IP, por app, aprendidos de las respuestas DNS que el
 * resolvedor del sistema ya recibió (ver docs/FIREWALL.md, "Nombres de una dirección").
 *
 * Una conexión trae solo una IP; las reglas de dominio ("*.ejemplo.com") necesitan el nombre.
 * Cuando la app resolvió www.ejemplo.com → CNAME cdn.ejemplo.net → A 192.0.2.7, acá queda
 * (app, 192.0.2.7) → { www.ejemplo.com, cdn.ejemplo.net } hasta que venza el TTL, y fw_decide
 * recibe esos nombres en FwQuery.hosts. No se consulta a nadie: ni DNS inverso ni servicios.
 *
 * Es una LRU acotada (max_entries pares app+IP+nombre en total, DNSMAP_PER_IP nombres por IP):
 * con la tabla llena se olvida lo usado hace más tiempo. */
#ifndef LORD_FW_DNSMAP_H
#define LORD_FW_DNSMAP_H

#include <stddef.h>
#include <stdint.h>

#include "fw/rules.h"

#define DNS_MAX_ADDRS   64          /* registros A/AAAA que se toman de una respuesta */
#define DNS_MAX_CNAMES  16          /* CNAME de una respuesta (y largo máximo de la cadena) */
#define DNSMAP_PER_IP   8           /* nombres por (app, IP) */
#define DNSMAP_TTL_MIN  60          /* los TTL se llevan a [60 s, 1 día] */
#define DNSMAP_TTL_MAX  86400

typedef struct {
    int fam;                        /* 4 o 6 */
    uint8_t addr[16];
    uint32_t ttl;
    char owner[FW_NAME_MAX];
} DnsAddrRec;

typedef struct {
    char from[FW_NAME_MAX], to[FW_NAME_MAX];
    uint32_t ttl;
} DnsCname;

typedef struct {
    char qname[FW_NAME_MAX];        /* la pregunta, normalizada */
    uint16_t qtype;
    int rcode;
    int naddr, ncname;
    DnsAddrRec addr[DNS_MAX_ADDRS];
    DnsCname cname[DNS_MAX_CNAMES];
} DnsResponse;

/* Lee una respuesta DNS (el mensaje, sin el largo de 2 bytes de TCP). Solo la sección de
 * respuestas, clase IN, tipos A, AAAA y CNAME; ignora autoridad y adicionales. Cada acceso está
 * controlado: etiquetas de hasta 63, nombres de hasta 255 bytes, punteros de compresión solo hacia
 * atrás del tramo que se está leyendo (no hay bucles) y a lo sumo 64 saltos por nombre.
 *   0  bien (puede no traer nada útil: rcode != 0, nombres que no son de host, otra clase);
 *  -1  mal formada, cortada, o no es una respuesta estándar con una sola pregunta. */
int dns_parse_response(const uint8_t *pkt, size_t len, DnsResponse *out);

typedef struct DnsMap DnsMap;

DnsMap *dnsmap_new(size_t max_entries);     /* 0 = 4096 */
void    dnsmap_free(DnsMap *m);

/* Los tiempos (now) son segundos de un reloj monótono que da el llamador. */
int dnsmap_add(DnsMap *m, const char *app, const char *name, int fam, const uint8_t *addr, uint32_t ttl, long long now);
/* Lee la respuesta y asocia cada dirección con la pregunta y todos los nombres de la cadena de
 * CNAME que llega a ella (los registros que no salen de la pregunta se ignoran). Devuelve cuántas
 * asociaciones agregó, o -1 si el paquete es inválido. */
int dnsmap_learn(DnsMap *m, const char *app, const uint8_t *pkt, size_t len, long long now);
/* Nombres vigentes de (app, IP), del más reciente al más viejo, copiados en out. Devuelve cuántos. */
int dnsmap_lookup(DnsMap *m, const char *app, int fam, const uint8_t *addr, long long now, char (*out)[FW_NAME_MAX], int max);

/* Recorre las direcciones de una app con sus nombres vigentes (sin cambiar la LRU); cb devuelve
 * != 0 para cortar. Devuelve cuántas direcciones recorrió. Lo usa lord-netperm para compilar las
 * reglas de dominio a mapas BPF (docs/FIREWALL.md, paso 2). */
int dnsmap_each(DnsMap *m, const char *app, long long now,
                int (*cb)(void *u, int fam, const uint8_t *addr, const char (*names)[FW_NAME_MAX], int n), void *u);

size_t dnsmap_count(const DnsMap *m);
void   dnsmap_expire(DnsMap *m, long long now);         /* borra lo vencido */
void   dnsmap_forget_app(DnsMap *m, const char *app);

#endif

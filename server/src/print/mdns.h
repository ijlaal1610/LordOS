/* mdns.h — búsqueda DNS-SD por mDNS propia (RFC 6762/6763) para encontrar impresoras IPP
 * Everywhere/AirPrint (_ipp._tcp, _ipps._tcp) y escáneres eSCL (_uscan._tcp, _uscans._tcp) en la
 * red local. Pregunta desde un puerto cualquiera («legacy unicast»): no hace falta el 5353 ni
 * avahi, y no cambia nada de la red. Las respuestas se validan como datos no confiables. */
#ifndef LORD_PRINT_MDNS_H
#define LORD_PRINT_MDNS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char type[24];          /* "_ipp._tcp" … */
    char name[128];         /* nombre de la instancia ("Oficina (HP)") */
    char host[128];         /* "impresora.local" */
    char addr[16];          /* IPv4 */
    int  port;
    char txt[1536];         /* "clave=valor\n…" */
} MdnsSvc;

/* Pregunta por los tipos dados durante ms milisegundos. Devuelve cuántos servicios encontró. */
int mdns_browse(const char *const *types, int ntypes, int ms, MdnsSvc *out, int max);

/* --- partes para las pruebas --- */
typedef struct {
    struct { char name[256], target[256], first[128]; } ptr[64];
    struct { char name[256], host[256]; int port; } srv[64];
    struct { char name[256]; char txt[1536]; } txt[64];
    struct { char host[256]; char addr[16]; } a[64];
    int nptr, nsrv, ntxt, na;
} MdnsCache;
/* Agrega los registros de un paquete de respuesta. 0 bien, -1 paquete inválido. */
int  mdns_parse(const uint8_t *pkt, size_t len, MdnsCache *c);
/* Arma los servicios de los tipos pedidos a partir de lo juntado. */
int  mdns_collect(const MdnsCache *c, const char *const *types, int ntypes, MdnsSvc *out, int max);
/* Valor de una clave del TXT ("" si no está). */
const char *mdns_txt(const MdnsSvc *s, const char *key, char *out, size_t n);

#endif

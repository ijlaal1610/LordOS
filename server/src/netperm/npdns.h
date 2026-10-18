/* npdns.h — de dónde aprende lord-netperm los nombres de las direcciones (para las reglas de
 * dominio). Solo de respuestas que produjo el resolvedor del sistema (docs/FIREWALL.md, paso 2,
 * "De dónde salen los nombres"):
 *
 *   1. systemd-resolved: su interfaz de monitoreo por varlink
 *      (/run/systemd/resolve/io.systemd.Resolve.Monitor, SubscribeQueryResults; solo root). Llega
 *      cada respuesta que resolved le dio a cualquier programa (NSS, el stub 127.0.0.53, D-Bus),
 *      con los registros en formato de red ("raw").
 *   2. Sin resolved: las respuestas UDP que llegan DESDE el puerto 53 de los servidores de
 *      /etc/resolv.conf (archivo de root, lo escribe LordOS: docs/RED.md), leídas con un socket
 *      AF_PACKET que solo recibe (filtro BPF clásico: UDP desde el puerto 53). Lo que una app le
 *      pregunta a otro servidor no se aprende.
 *
 * Las dos fuentes entregan un mensaje DNS de respuesta completo a cb, que lo pasa a dnsmap_learn. */
#ifndef LORD_NP_DNS_H
#define LORD_NP_DNS_H

#include <stddef.h>
#include <stdint.h>

typedef void (*NpDnsCb)(void *u, const uint8_t *pkt, size_t len);
typedef struct NpDns NpDns;

NpDns      *npdns_new(NpDnsCb cb, void *u);
void        npdns_free(NpDns *d);
int         npdns_fd(const NpDns *d);           /* el fd a esperar (-1 si no hay) */
void        npdns_ready(NpDns *d);              /* el fd tiene datos */
void        npdns_tick(NpDns *d);               /* cada segundo: reconectar, elegir fuente */
const char *npdns_source(const NpDns *d);       /* "resolved", "packet" o "none" */

/* Una respuesta del monitor de resolved (JSON de varlink, sin el 0 final) → un mensaje DNS por
 * pregunta, a cb. Devuelve cuántos armó, 0 si no era una respuesta exitosa, -1 si es inválida.
 * Separada para probarla sin resolved (tests/npcompile_test.c). */
int npdns_from_varlink(const char *json, size_t len, NpDnsCb cb, void *u);

/* Un paquete de red (desde la cabecera IP, como lo da AF_PACKET/SOCK_DGRAM) → el mensaje DNS si es
 * UDP desde el puerto 53 de alguna de las direcciones dadas (servers: fam 4/6 + 16 bytes). */
typedef struct { int fam; uint8_t addr[16]; } NpAddr;
int npdns_from_packet(const uint8_t *p, size_t len, const NpAddr *servers, int nservers, const uint8_t **dns, size_t *dnslen);

#endif

/* mdns.h — our own DNS-SD over mDNS lookup (RFC 6762/6763) to find IPP
 * Everywhere (driverless printing) printers (_ipp._tcp, _ipps._tcp) and eSCL scanners (_uscan._tcp, _uscans._tcp) on the
 * local network. It asks from an arbitrary port («legacy unicast»): it doesn't need 5353 nor
 * avahi, and it changes nothing on the network. Replies are validated as untrusted data. */
#ifndef LORD_PRINT_MDNS_H
#define LORD_PRINT_MDNS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char type[24];          /* "_ipp._tcp" … */
    char name[128];         /* instance name ("Office (HP)") */
    char host[128];         /* "printer.local" */
    char addr[16];          /* IPv4 */
    int  port;
    char txt[1536];         /* "key=value\n…" */
} MdnsSvc;

/* Asks for the given types for ms milliseconds. Returns how many services it found. */
int mdns_browse(const char *const *types, int ntypes, int ms, MdnsSvc *out, int max);

/* --- pieces for the tests --- */
typedef struct {
    struct { char name[256], target[256], first[128]; } ptr[64];
    struct { char name[256], host[256]; int port; } srv[64];
    struct { char name[256]; char txt[1536]; } txt[64];
    struct { char host[256]; char addr[16]; } a[64];
    int nptr, nsrv, ntxt, na;
} MdnsCache;
/* Adds the records of a reply packet. 0 on success, -1 invalid packet. */
int  mdns_parse(const uint8_t *pkt, size_t len, MdnsCache *c);
/* Builds the services of the requested types from what was gathered. */
int  mdns_collect(const MdnsCache *c, const char *const *types, int ntypes, MdnsSvc *out, int max);
/* Value of a TXT key ("" if it's not there). */
const char *mdns_txt(const MdnsSvc *s, const char *key, char *out, size_t n);

#endif

/* phttp.h — cliente HTTP/1.1 mínimo para la impresión: IPP a CUPS por su socket local y eSCL a
 * los escáneres de la red. Una conexión por pedido (Connection: close), plazos con poll y topes
 * de tamaño; las respuestas de la red se tratan como no confiables. */
#ifndef LORD_PRINT_PHTTP_H
#define LORD_PRINT_PHTTP_H

#include <stddef.h>

typedef struct {
    int status;
    char *body;
    size_t len;
    char location[512];
    char www_auth[256];
    char content_type[128];
    int retry_after;
} PResp;

int  phttp_unix(const char *path);                         /* fd conectado o -1 */
int  phttp_tcp(const char *ipv4, int port, int timeout_ms); /* fd conectado o -1 */
/* Manda el pedido (body y, si docfd >= 0, doclen bytes de ese fd después) y lee la respuesta
 * completa (hasta max bytes). 0 si llegó una respuesta HTTP; -1 si falló la conexión. */
int  phttp_request(int fd, const char *method, const char *host, const char *path, const char *headers,
                   const char *ctype, const void *body, size_t blen, int docfd, size_t doclen,
                   size_t max, int timeout_ms, PResp *r);
void presp_free(PResp *r);

/* ¿Es una dirección IPv4 de la red local (privada, enlace local o esta máquina)? */
int  phttp_lan_ipv4(const char *ip);

#endif

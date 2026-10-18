/* escl.h — cliente eSCL propio (escáneres «sin controlador», AirScan) por HTTP en la red local.
 * Sin SANE. Solo direcciones IPv4 de la LAN y HTTP (sin TLS: _uscans no está soportado). */
#ifndef LORD_PRINT_ESCL_H
#define LORD_PRINT_ESCL_H

#include "../doc/wire.h"
#include "../sec/json.h"

/* rs: el camino del TXT «rs» (normalmente "eSCL"). */
int escl_caps(const char *ip, int port, const char *rs, JBuf *out);
/* s: {source, color, res, format, x, y, w, h (en 1/300 pulgada)}; el resultado va a un archivo
 * nuevo en dir (fd de carpeta), con nombre base; out = {file, format, bytes}. */
int escl_scan(const char *ip, int port, const char *rs, const JVal *s, const char *dir, JBuf *out);
int escl_valid_rs(const char *rs);

#endif

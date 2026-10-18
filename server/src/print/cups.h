/* cups.h — cliente IPP propio para CUPS por su socket local (/run/cups/cups.sock), sin libcups.
 *
 * CUPS administra la cola y los controladores; LordOS solo le habla IPP. Las operaciones de
 * administración (agregar, quitar, elegir la predeterminada) las autoriza la política del propio
 * CUPS: el usuario tiene que estar en el grupo lpadmin (los administradores de LordOS lo están) y
 * se identifica con «PeerCred» (CUPS mira las credenciales del socket). No hay acción de root en
 * admin.c para esto. Ver docs/IMPRESION.md.
 *
 * Cada función arma su resultado como JSON en *out (JBuf de doc/wire.h) y devuelve 0, o -1 con
 * {"error": "..."} en *out. Son bloqueantes: se llaman desde los hilos de print.c. */
#ifndef LORD_PRINT_CUPS_H
#define LORD_PRINT_CUPS_H

#include <stddef.h>
#include "../doc/wire.h"
#include "../sec/json.h"

extern const char *cups_socket;            /* "/run/cups/cups.sock" (las pruebas lo cambian) */

int cups_printers(JBuf *out);
int cups_jobs(JBuf *out);
int cups_cancel(int job, JBuf *out);
/* opts: {name, copies, pages:"1-3,5", sides, color, media} */
int cups_submit(const char *printer, int docfd, size_t len, const JVal *opts, JBuf *out);
int cups_add(const char *name, const char *uri, const char *info, const char *location, JBuf *out);
int cups_remove(const char *name, JBuf *out);
int cups_set_default(const char *name, JBuf *out);

/* validaciones (también las usa js_print.c) */
int cups_valid_name(const char *s);         /* letras, dígitos, _ y -, hasta 127 */
int cups_valid_uri(const char *s);          /* ipp:// ipps:// http:// https:// dnssd:// sin espacios */
int cups_valid_text(const char *s, size_t max);
/* "1-3,5,8-9" → rangos; devuelve cuántos (máx. 32) o -1 */
int cups_page_ranges(const char *s, int lo[], int hi[], int max);

#endif

/* print.h — impresoras y escáneres para lord.print (ui/js_print.c): cada pedido corre en un hilo
 * propio (IPP a CUPS, mDNS, eSCL) y su resultado llega a JS con print_poll(). El compositor no
 * espera nunca. Ver docs/IMPRESION.md. */
#ifndef LORD_PRINT_PRINT_H
#define LORD_PRINT_PRINT_H

#include <stddef.h>

/* kind: printers | jobs | cancel | submit | add | remove | default | discover | scancaps | scan.
 * json: argumentos (ya validados por js_print.c; se vuelven a validar acá). docfd (submit): el
 * PDF a imprimir; print_task se queda con él. Devuelve el id (> 0) o -1. */
int   print_task(const char *kind, const char *json, size_t len, int docfd, size_t doclen);
char *print_poll(void);             /* [{id, kind, ok, data}] (malloc) o NULL */
int   print_pending(void);

#endif

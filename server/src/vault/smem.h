/* smem.h — memoria para secretos de lord-vault (ver docs/PASSWORDS.md, «Memoria»).
 *
 * Todo lo descifrado (la bóveda, los campos, los pedidos y respuestas que llevan secretos) vive
 * en bloques de esta arena y no en el montón de la libc:
 *   - mmap anónimo propio, con mlock (no va al swap) si RLIMIT_MEMLOCK alcanza;
 *   - MADV_DONTDUMP (nunca en un core) y MADV_WIPEONFORK;
 *   - sm_free borra el bloque antes de devolverlo; sm_wipe_all (al bloquear) borra TODO.
 * sm_harden_process() además apaga los core dumps del proceso (RLIMIT_CORE 0) y lo marca no
 * «dumpable» (PR_SET_DUMPABLE 0: tampoco se le puede hacer ptrace desde el mismo usuario).
 *
 * Bloques chicos (hasta 4 KB) por clases de tamaño con listas libres, O(1); los grandes van en
 * su propio mmap. Seguro con hilos (un mutex). */
#ifndef LORD_VAULT_SMEM_H
#define LORD_VAULT_SMEM_H

#include <stddef.h>

void  sm_harden_process(void);
void *sm_alloc(size_t n);                 /* en cero; NULL si no hay memoria */
void *sm_realloc(void *p, size_t n);
void  sm_free(void *p);                   /* borra y libera (NULL no hace nada) */
char *sm_strdup(const char *s);           /* NULL → NULL */
char *sm_strndup(const char *s, size_t n);
size_t sm_size(const void *p);            /* capacidad útil del bloque */
void  sm_wipe_all(void);                  /* bloquear: borra y suelta toda la arena */

typedef struct { size_t in_use, mapped, locked; int all_locked; } SmStats;
void  sm_stats(SmStats *st);

/* borrar memoria sin que el compilador lo saque (explicit_bzero) */
void  sm_wipe(void *p, size_t n);

#endif

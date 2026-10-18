/* ss_memback.h — llavero en memoria para probar secretsvc.c (dbus_test y secretsvc-demo). */
#ifndef SS_MEMBACK_H
#define SS_MEMBACK_H
#include "vault/secretsvc.h"

#define MB_MAX 512
typedef struct {
    int used;
    char id[40];
    char *label;
    char **names, **values;
    int n;
    uint8_t *sec;
    size_t len;
    char ctype[64];
    uint64_t created, modified;
} MbItem;

typedef struct {
    MbItem it[MB_MAX];
    int locked, counter;
    unsigned pending;        /* aviso de desbloqueo en curso (0 = ninguno) */
    double pending_at;       /* ms monotónicos en que se completa */
    char last_caller[256];
    unsigned last_pid;
} MemBack;

extern const SsBackend memback_ops;
void memback_init(MemBack *b, int locked);
/* completa el aviso pendiente si pasó 1 s: desbloquea y llama a ss_prompt_done */
void memback_tick(MemBack *b);
#endif

/* journal.h — lector propio de los archivos del journal de systemd (ver journal.c y
 * docs/SISTEMA.md, «Registros»). Sin libsystemd: se leen los archivos con pread y se valida cada
 * desplazamiento, así un archivo roto o cortado no tira abajo a lord-server. */
#ifndef LORD_LOGS_JOURNAL_H
#define LORD_LOGS_JOURNAL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define JMSG_MAX 16384                  /* largo máximo de un mensaje (se corta con «…») */

typedef struct {
    uint64_t realtime;                  /* µs desde 1970 (de la cabecera de la entrada) */
    uint64_t seqnum;
    int priority;                       /* 0 (emerg) … 7 (debug); -1 si no tiene */
    int pid, uid;                       /* -1 si no tiene */
    int kernel;                         /* _TRANSPORT=kernel */
    char ident[64];                     /* SYSLOG_IDENTIFIER */
    char comm[32];                      /* _COMM */
    char unit[96];                      /* _SYSTEMD_UNIT o _SYSTEMD_USER_UNIT */
    char boot[33];                      /* _BOOT_ID (hex) */
    char *message;                      /* malloc; "" si no tiene */
} JEntry;

typedef struct JFile JFile;

/* Abre un archivo del journal (solo lectura). NULL si no es un journal o no se puede leer. */
JFile   *jf_open(const char *path);
void     jf_close(JFile *f);
size_t   jf_count(const JFile *f);                        /* entradas encontradas */
int      jf_compact(const JFile *f);
/* realtime de la entrada idx (0 = la más vieja), sin decodificar sus campos; 0 si está rota */
uint64_t jf_realtime(JFile *f, size_t idx);
/* Decodifica la entrada idx. 0 bien (liberar con jentry_free), -1 rota. */
int      jf_entry(JFile *f, size_t idx, JEntry *out);
void     jentry_free(JEntry *e);

/* ---- consultas (varios archivos, mezclados por hora) ---- */

typedef struct {
    uint64_t since, until;              /* µs; 0 = sin límite. since es exclusivo, until también */
    int prio;                           /* prioridad máxima (0..7), -1 = todas */
    int kernel;                         /* solo las del kernel */
    char match[96];                     /* SYSLOG_IDENTIFIER, _COMM o unidad (vacío = todas) */
    char grep[128];                     /* texto del mensaje, sin mayúsculas (vacío = todos) */
    size_t limit;                       /* máximo de entradas (las más nuevas) */
} JQuery;

enum { JSCOPE_USER = 1, JSCOPE_SYSTEM = 2 };

/* Archivos del journal de ese alcance (del usuario uid, o del sistema) en /var/log/journal y
 * /run/log/journal, que este proceso puede leer. Devuelve cuántos (rutas con malloc). */
int  jq_files(int scope, uid_t uid, char ***paths);
void jq_files_free(char **paths, int n);
/* Corre la consulta. Las entradas quedan en orden cronológico (la más vieja primero). Devuelve
 * la cantidad, o -1. Liberar con jq_free. */
long jq_run(char *const *paths, int npaths, const JQuery *q, JEntry **out);
void jq_free(JEntry *e, long n);
/* Una entrada en JSON: {"t":µs,"p":n,"id":"","u":"","pid":n,"uid":n,"k":0|1,"b":"","m":""}.
 * El mensaje se corta a msgmax bytes. Devuelve el largo escrito (0 si no entra). */
size_t jentry_json(const JEntry *e, char *buf, size_t cap, size_t msgmax);
/* Valida un nombre de unidad o identificador: [A-Za-z0-9@._:-], hasta 95. */
int  jq_valid_match(const char *s);
/* Lee una consulta en texto «clave=valor» por línea (since, until, prio, match, grep, kernel,
 * limit). since/until en segundos o µs (> 1e12). Devuelve 0 si todo es válido. */
int  jq_parse(const char *text, JQuery *q, size_t max_limit);

/* LZ4 en bloque (el formato que usa journald, sin el prefijo de tamaño). Devuelve el largo
 * escrito o -1. Expuesto para la prueba. */
long lz4_block_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap);

#endif

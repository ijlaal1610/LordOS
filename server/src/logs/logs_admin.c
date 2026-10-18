/* logs_admin.c — logs-system: el journal del sistema para un administrador que no está en los
 * grupos systemd-journal/adm (app Registros, docs/SYSTEM.md).
 *
 *   logs-system <consulta>   líneas clave=valor: since, until (segundos o µs), prio (0..7),
 *                            kernel (0|1), match (unidad o identificador: [A-Za-z0-9@._:-]),
 *                            grep (texto sin caracteres de control), limit (1..5000)
 *
 * Responde un LOG por entrada (JSON de jentry_json, con el mensaje cortado para que entre en una
 * línea del protocolo) y al final OK <cantidad>. Lee los archivos system*.journal* con el lector
 * propio (logs/journal.c): no ejecuta journalctl ni ningún otro programa. Las rutas no vienen del
 * usuario: salen de /etc/machine-id y de los nombres fijos de journald. */
#include "logs_admin.h"
#include "journal.h"

#include <stdlib.h>
#include <string.h>

#define LINE_MAX_JSON 1000              /* admin_say corta a 1024 */

void logs_act_system(const AdminReq *r, const char *arg, size_t len) {
    (void)r; (void)len;
    JQuery q;
    if (jq_parse(arg, &q, 5000)) { admin_say("ERR", "consulta inválida"); return; }
    char **paths;
    int np = jq_files(JSCOPE_SYSTEM, 0, &paths);
    if (np <= 0) { admin_say("ERR", "no hay journal del sistema"); return; }
    JEntry *e = NULL;
    long n = jq_run(paths, np, &q, &e);
    jq_files_free(paths, np);
    if (n < 0) { admin_say("ERR", "no se pudo leer el journal"); return; }
    char buf[LINE_MAX_JSON];
    long sent = 0;
    for (long i = 0; i < n; i++) {
        static const size_t tries[] = { 700, 300, 80 };
        for (size_t k = 0; k < 3; k++)
            if (jentry_json(&e[i], buf, sizeof buf, tries[k])) { admin_say("LOG", "%s", buf); sent++; break; }
    }
    jq_free(e, n);
    admin_say("OK", "%ld", sent);
}

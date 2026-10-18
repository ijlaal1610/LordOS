/* vault_e2e.c — lord-vault de punta a punta, por el mismo cliente que usa lord-server
 * (src/vault/vclient.c): crear, abrir, contraseña incorrecta, entradas, búsqueda, informe, TOTP,
 * QR, generador, importar/exportar, adjuntos, ranuras (archivo llave, recuperación), llavero,
 * bloqueo automático, y que la memoria del proceso esté protegida (VmFlags "dd" = MADV_DONTDUMP,
 * "lo" = mlock; Dumpable 0). Usa una HOME temporal; no toca el bus de D-Bus.
 * Se corre con `make build/vault-e2e && build/vault-e2e` (necesita build/lord-vault al lado). */
#include "vault/vclient.h"
#include "vault/vjson.h"
#include "vault/vwire.h"
#include "vault/smem.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fails, total;
static void check(int ok, const char *what) { total++; if (!ok) { fails++; printf("FALLA: %s\n", what); } }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static char last[1 << 22];        /* última respuesta */
static int events_locked;

/* manda y espera la respuesta (texto JSON en `last`) */
static const char *req(const char *json, int *fds, int nfds) {
    int id = vc_request(json, strlen(json), fds, nfds);
    if (id < 0) { snprintf(last, sizeof last, "{\"error\":\"send\"}"); return last; }
    double t0 = now_ms();
    while (now_ms() - t0 < 60000) {
        char *s = vc_poll();
        if (!s) { usleep(2000); continue; }
        Vj j;
        int found = 0;
        if (!vj_parse(&j, s, strlen(s))) {
            for (int i = 0, k = 1; i < j.t[0].count; i++, k = j.t[k].next) {
                int rid = (int)vj_getn(&j, k, "id", -1);
                int d = vj_get(&j, k, "data");
                if (vj_streq(&j, vj_get(&j, k, "kind"), "evt")) {
                    if (d >= 0 && strstr(j.s + j.t[d].start, "\"locked\"") && strstr(j.s + j.t[d].start, "\"locked\"") < j.s + j.t[d].end) events_locked++;
                    continue;
                }
                if (rid == id && d >= 0) {
                    size_t n = j.t[d].end - j.t[d].start;
                    if (n >= sizeof last) n = sizeof last - 1;
                    memcpy(last, j.s + j.t[d].start, n);
                    last[n] = 0;
                    found = 1;
                }
            }
            vj_free(&j);
        }
        vc_poll_free(s);
        if (found) return last;
    }
    snprintf(last, sizeof last, "{\"error\":\"timeout\"}");
    return last;
}

static int ok(const char *r) { return strstr(r, "\"ok\":true") != NULL; }
static int err(const char *r, const char *code) { char b[80]; snprintf(b, sizeof b, "\"error\":\"%s\"", code); return strstr(r, b) != NULL; }

static void jstr(const char *r, const char *key, char *out, size_t n) {
    out[0] = 0;
    Vj j;
    if (!vj_parse(&j, r, strlen(r))) { vj_getsbuf(&j, 0, key, out, n); vj_free(&j); }
}

static int openr(const char *p) { return open(p, O_RDONLY | O_CLOEXEC); }
static int openw(const char *p) { return open(p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); }

int main(void) {
    char home[] = "/tmp/vault-e2e-pw-116-XXXXXX";
    if (!mkdtemp(home)) return 1;
    setenv("HOME", home, 1);
    setenv("XDG_RUNTIME_DIR", "/nonexistent", 1);
    unsetenv("DBUS_SESSION_BUS_ADDRESS");
    check(vc_available(), "lord-vault al lado del binario");
    check(vc_start() == 0, "lanzar lord-vault");
    const char *r;
    char p[512], rec[64];

    r = req("{\"op\":\"status\"}", NULL, 0);
    check(ok(r) && strstr(r, "\"exists\":false"), "estado inicial: sin bóveda");
    double t0 = now_ms();
    r = req("{\"op\":\"create\",\"method\":\"password\",\"password\":\"Contraseña maestra 1\",\"targetMs\":600}", NULL, 0);
    double tcreate = now_ms() - t0;
    check(ok(r), "crear bóveda con contraseña");
    jstr(r, "recovery", rec, sizeof rec);
    check(strlen(rec) == 47, "clave de recuperación de 8 grupos");
    printf("crear: %.0f ms (%s)\n", tcreate, r);

    /* entradas */
    r = req("{\"op\":\"save\",\"entry\":{\"cat\":\"internet\",\"title\":\"Banco Ejemplo\",\"tags\":[\"finanzas\"],\"f\":[{\"k\":\"username\",\"t\":\"text\",\"v\":\"ana\"},"
            "{\"k\":\"password\",\"t\":\"hidden\",\"v\":\"password\"},{\"k\":\"url\",\"t\":\"url\",\"v\":\"https://banco.example\"},"
            "{\"k\":\"totp\",\"t\":\"totp\",\"v\":\"otpauth://totp/Banco:ana?secret=JBSWY3DPEHPK3PXP&issuer=Banco\"}]}}", NULL, 0);
    check(ok(r), "guardar entrada");
    char id1[40];
    jstr(r, "id", id1, sizeof id1);
    r = req("{\"op\":\"save\",\"entry\":{\"cat\":\"wifi\",\"title\":\"Casa\",\"f\":[{\"k\":\"ssid\",\"t\":\"text\",\"v\":\"MiRed\"},{\"k\":\"security\",\"t\":\"text\",\"v\":\"WPA2\"},{\"k\":\"password\",\"t\":\"hidden\",\"v\":\"password\"}]}}", NULL, 0);
    check(ok(r), "guardar Wi-Fi");
    char id2[40];
    jstr(r, "id", id2, sizeof id2);
    r = req("{\"op\":\"list\"}", NULL, 0);
    check(ok(r) && strstr(r, "Banco Ejemplo") && !strstr(r, "JBSWY3DP") && !strstr(r, "\"password\""), "lista sin secretos");
    snprintf(p, sizeof p, "{\"op\":\"get\",\"id\":\"%s\"}", id1);
    r = req(p, NULL, 0);
    check(ok(r) && strstr(r, "\"has\":true") && !strstr(r, "JBSWY3DP"), "get sin reveal");
    snprintf(p, sizeof p, "{\"op\":\"reveal\",\"id\":\"%s\",\"k\":\"password\"}", id1);
    r = req(p, NULL, 0);
    check(ok(r) && strstr(r, "\"value\":\"password\""), "reveal de un campo");
    r = req("{\"op\":\"search\",\"q\":\"banco finanzas\"}", NULL, 0);
    check(ok(r) && strstr(r, id1), "búsqueda");
    r = req("{\"op\":\"totp\"}", NULL, 0);
    check(ok(r) && strstr(r, "\"code\":\"") && strstr(r, "\"issuer\":\"Banco\"") && !strstr(r, "JBSWY3DP"), "códigos TOTP sin el secreto");
    r = req("{\"op\":\"report\"}", NULL, 0);
    check(ok(r) && strstr(r, "\"common\":true") && strstr(r, "\"reused\":[[\""), "informe: débil y repetida");
    snprintf(p, sizeof p, "{\"op\":\"qr\",\"id\":\"%s\"}", id2);
    r = req(p, NULL, 0);
    check(ok(r) && strstr(r, "\"rows\":[\""), "QR del Wi-Fi");
    r = req("{\"op\":\"generate\",\"length\":32,\"symbols\":true}", NULL, 0);
    check(ok(r) && strstr(r, "\"score\":4"), "generador");
    r = req("{\"op\":\"generate\",\"words\":5,\"sep\":\" \"}", NULL, 0);
    check(ok(r), "frase");

    /* adjuntos */
    char f[600];
    snprintf(f, sizeof f, "%s/lic.txt", home);
    FILE *fp = fopen(f, "w");
    fputs("CLAVE-DE-LICENCIA-1234", fp);
    fclose(fp);
    int fd = openr(f);
    snprintf(p, sizeof p, "{\"op\":\"att-add\",\"id\":\"%s\",\"name\":\"lic.txt\",\"mime\":\"text/plain\"}", id1);
    r = req(p, &fd, 1);
    check(ok(r) && strstr(r, "\"natt\":1"), "agregar adjunto");
    snprintf(p, sizeof p, "{\"op\":\"get\",\"id\":\"%s\"}", id1);
    r = req(p, NULL, 0);
    char *a = strstr(r, "\"att\":[{\"id\":\"");
    char attid[40] = "";
    if (a) { memcpy(attid, a + 14, 32); attid[32] = 0; }
    snprintf(p, sizeof p, "{\"op\":\"att-text\",\"id\":\"%s\",\"att\":\"%s\"}", id1, attid);
    r = req(p, NULL, 0);
    check(ok(r) && strstr(r, "CLAVE-DE-LICENCIA-1234"), "leer adjunto");

    /* bloquear, contraseña incorrecta, abrir */
    r = req("{\"op\":\"lock\"}", NULL, 0);
    check(ok(r), "bloquear");
    r = req("{\"op\":\"list\"}", NULL, 0);
    check(err(r, "locked"), "bloqueada no lista");
    r = req("{\"op\":\"unlock\",\"method\":\"password\",\"password\":\"otra\"}", NULL, 0);
    check(err(r, "bad-key"), "contraseña incorrecta");
    t0 = now_ms();
    r = req("{\"op\":\"unlock\",\"method\":\"password\",\"password\":\"Contraseña maestra 1\"}", NULL, 0);
    double tun = now_ms() - t0;
    check(ok(r), "abrir");
    printf("abrir con contraseña: %.0f ms (%s)\n", tun, r);
    snprintf(p, sizeof p, "{\"op\":\"att-text\",\"id\":\"%s\",\"att\":\"%s\"}", id1, attid);
    r = req(p, NULL, 0);
    check(ok(r) && strstr(r, "CLAVE-DE-LICENCIA-1234"), "adjunto después de guardar y abrir");

    /* App channel (vc_attach_app): what the sandboxed Passwords process (lord-server --app) uses.
     * Same API on its own socket; a lock from the control channel reaches it as an event. */
    {
        int afd = vc_attach_app();
        check(afd >= 0, "app channel: attach");
        if (afd >= 0) {
            VMsg m;
            int got = 0, locked_ev = 0;
            vw_send(afd, VW_REQ, 7, "{\"op\":\"list\"}", 13, NULL, 0);
            while (!got && vw_recv(afd, &m, &vw_malloc) == 0) {
                if (m.kind == VW_REP && m.id == 7) got = ok(m.json) && strstr(m.json, "Banco Ejemplo") != NULL;
                vw_msg_free(&m, &vw_malloc);
            }
            check(got, "app channel: list over its own socket");
            r = req("{\"op\":\"lock\"}", NULL, 0);
            for (int k = 0; k < 8 && !locked_ev && vw_recv(afd, &m, &vw_malloc) == 0; k++) {
                if (m.kind == VW_EVT && strstr(m.json, "\"locked\"")) locked_ev = 1;
                vw_msg_free(&m, &vw_malloc);
            }
            check(locked_ev, "app channel: lock event");
            close(afd);
            r = req("{\"op\":\"unlock\",\"method\":\"password\",\"password\":\"Contraseña maestra 1\"}", NULL, 0);
            check(ok(r), "unlock again after the app channel closed");
        }
    }

    /* archivo llave nuevo (como en un pendrive) + abrir con él */
    snprintf(f, sizeof f, "%s/pendrive.key", home);
    fd = openw(f);
    r = req("{\"op\":\"slot-add\",\"method\":\"keyfile\",\"newkey\":true,\"label\":\"Pendrive\"}", &fd, 1);
    check(ok(r), "ranura de archivo llave");
    struct stat st;
    check(stat(f, &st) == 0 && st.st_size == 64, "archivo llave de 64 bytes");
    req("{\"op\":\"lock\"}", NULL, 0);
    fd = openr(f);
    r = req("{\"op\":\"unlock\",\"method\":\"keyfile\"}", &fd, 1);
    check(ok(r), "abrir con el archivo llave");
    req("{\"op\":\"lock\"}", NULL, 0);
    snprintf(p, sizeof p, "{\"op\":\"unlock\",\"method\":\"recovery\",\"recovery\":\"%s\"}", rec);
    r = req(p, NULL, 0);
    check(ok(r), "abrir con la clave de recuperación");

    /* contraseña + archivo llave (dos factores): reemplaza a la contraseña sola */
    snprintf(f, sizeof f, "%s/segundo.key", home);
    fd = openw(f);
    r = req("{\"op\":\"slot-add\",\"method\":\"passkey\",\"newkey\":true,\"password\":\"Dos factores 2\"}", &fd, 1);
    check(ok(r), "ranura contraseña + archivo llave");
    req("{\"op\":\"lock\"}", NULL, 0);
    r = req("{\"op\":\"unlock\",\"method\":\"password\",\"password\":\"Contraseña maestra 1\"}", NULL, 0);
    check(!ok(r), "la contraseña sola ya no abre");
    fd = openr(f);
    r = req("{\"op\":\"unlock\",\"method\":\"passkey\",\"password\":\"Dos factores 2\"}", &fd, 1);
    check(ok(r), "abrir con contraseña + archivo llave");

    /* importar Chrome CSV y exportar CSV */
    snprintf(f, sizeof f, "%s/chrome.csv", home);
    fp = fopen(f, "w");
    fputs("name,url,username,password,note\nEjemplo,https://ejemplo.com/,bob,Xy7#kd92!aQ,\n\"Con, coma\",https://otro.com/,ana,\"pa\"\"ss\",nota\n", fp);
    fclose(fp);
    fd = openr(f);
    r = req("{\"op\":\"import\",\"name\":\"chrome.csv\"}", &fd, 1);
    check(ok(r) && strstr(r, "\"count\":2") && strstr(r, "chrome-csv"), "importar CSV de Chrome");
    snprintf(f, sizeof f, "%s/export.csv", home);
    fd = openw(f);
    r = req("{\"op\":\"export-csv\"}", &fd, 1);
    check(ok(r) && strstr(r, "\"count\":4"), "exportar CSV");
    snprintf(f, sizeof f, "%s/copia.lvault", home);
    fd = openw(f);
    r = req("{\"op\":\"export-vault\"}", &fd, 1);
    check(ok(r), "exportar copia .lvault");

    /* llavero: no existe sin la contraseña de la sesión; se crea con ella */
    r = req("{\"op\":\"kr-set\",\"owner\":\"mail\",\"service\":\"imap.example.com\",\"account\":\"ana\",\"secret\":\"s\"}", NULL, 0);
    check(err(r, "no-keyring"), "sin llavero de sesión todavía");
    vc_session_password("clave de la sesión");
    usleep(1500000);
    r = req("{\"op\":\"status\"}", NULL, 0);
    check(strstr(r, "\"login\":{\"exists\":true,\"unreadable\":false,\"unlocked\":true") != NULL, "llavero creado y abierto con la sesión");
    r = req("{\"op\":\"kr-set\",\"owner\":\"mail\",\"service\":\"imap.example.com\",\"account\":\"ana\",\"secret\":\"secreto-imap\"}", NULL, 0);
    check(ok(r), "kr-set");
    r = req("{\"op\":\"kr-get\",\"owner\":\"mail\",\"service\":\"imap.example.com\",\"account\":\"ana\"}", NULL, 0);
    check(ok(r) && strstr(r, "secreto-imap"), "kr-get de la misma app");
    r = req("{\"op\":\"kr-get\",\"owner\":\"settings\",\"service\":\"imap.example.com\",\"account\":\"ana\"}", NULL, 0);
    check(err(r, "not-shared"), "otra app no lo ve");
    r = req("{\"op\":\"kr-list\",\"owner\":\"settings\"}", NULL, 0);
    check(ok(r) && !strstr(r, "imap"), "kr-list de otra app vacío");
    char kid[40] = "";
    r = req("{\"op\":\"kr-list\",\"owner\":\"mail\"}", NULL, 0);
    char *ki = strstr(r, "\"id\":\"");
    if (ki) { memcpy(kid, ki + 6, 32); kid[32] = 0; }
    snprintf(p, sizeof p, "{\"op\":\"kr-share\",\"admin\":true,\"id\":\"%s\",\"app\":\"settings\"}", kid);
    r = req(p, NULL, 0);
    check(ok(r), "compartir con permiso del usuario");
    r = req("{\"op\":\"kr-get\",\"owner\":\"settings\",\"service\":\"imap.example.com\",\"account\":\"ana\"}", NULL, 0);
    check(ok(r) && strstr(r, "secreto-imap"), "ahora sí lo ve");
    vc_lock_all("screen");
    usleep(300000);
    r = req("{\"op\":\"status\"}", NULL, 0);
    check(strstr(r, "\"main\":{\"exists\":true,\"unreadable\":false,\"unlocked\":false") && strstr(r, "\"login\":{\"exists\":true,\"unreadable\":false,\"unlocked\":true"),
          "bloquear la pantalla cierra Contraseñas y no el llavero");

    /* memoria protegida del proceso */
    {
        FILE *ps = popen("pgrep -n -x lord-vault", "r");
        int vpid = 0;
        if (ps) { if (fscanf(ps, "%d", &vpid) != 1) vpid = 0; pclose(ps); }
        char path[64], line[256];
        snprintf(path, sizeof path, "/proc/%d/smaps", vpid);
        FILE *sm = fopen(path, "r");
        int dd = 0, lo = 0;
        while (sm && fgets(line, sizeof line, sm)) if (!strncmp(line, "VmFlags:", 8)) { if (strstr(line, " dd")) dd++; if (strstr(line, " lo")) lo++; }
        if (sm) fclose(sm);
        printf("lord-vault %d: %d regiones MADV_DONTDUMP, %d con mlock (smaps %s)\n", vpid, dd, lo, sm ? "legible" : "no legible: no dumpable");
        check(vpid > 0, "lord-vault corre");
        check(!sm || (dd > 0 && lo > 0), "memoria con DONTDUMP y mlock (o el proceso no es dumpable)");
        snprintf(path, sizeof path, "/proc/%d/status", vpid);
        FILE *ss = fopen(path, "r");
        int nnp = 0, secc = 0;
        while (ss && fgets(line, sizeof line, ss)) { if (!strncmp(line, "NoNewPrivs:\t1", 13)) nnp = 1; if (!strncmp(line, "Seccomp:\t2", 10)) secc = 1; }
        if (ss) fclose(ss);
        check(nnp && secc, "encerrado: no_new_privs y seccomp");
    }

    /* 10 000 entradas por importación y búsqueda medida dentro de lord-vault */
    {
        snprintf(f, sizeof f, "%s/grande.csv", home);
        fp = fopen(f, "w");
        fputs("name,url,username,password\n", fp);
        for (int i = 0; i < 10000; i++) fprintf(fp, "Sitio %d,https://sitio%d.example.org/,usuario%d,Clave-%d-%d\n", i, i, i, i, i * 7);
        fclose(fp);
        r = req("{\"op\":\"unlock\",\"method\":\"recovery\",\"recovery\":\"x\"}", NULL, 0);
        snprintf(p, sizeof p, "{\"op\":\"unlock\",\"method\":\"recovery\",\"recovery\":\"%s\"}", rec);
        req(p, NULL, 0);
        fd = openr(f);
        t0 = now_ms();
        r = req("{\"op\":\"import\",\"name\":\"grande.csv\"}", &fd, 1);
        printf("importar 10 000: %.0f ms (%s)\n", now_ms() - t0, r);
        check(ok(r) && strstr(r, "\"count\":10000"), "importar 10 000");
        double worst = 0;
        for (int k = 0; k < 5; k++) {
            r = req(k % 2 ? "{\"op\":\"search\",\"q\":\"usuario9999\"}" : "{\"op\":\"search\",\"q\":\"sitio 42\"}", NULL, 0);
            Vj j;
            if (!vj_parse(&j, r, strlen(r))) { double ms = vj_getn(&j, 0, "ms", 99); if (ms > worst) worst = ms; vj_free(&j); }
        }
        printf("búsqueda en lord-vault con 10 004 entradas: peor %.2f ms\n", worst);
        check(worst < 10, "búsqueda < 10 ms en lord-vault");
        t0 = now_ms();
        r = req("{\"op\":\"list\"}", NULL, 0);
        printf("lista de 10 004 entradas: %zu KB en %.0f ms (ida y vuelta)\n", strlen(r) / 1024, now_ms() - t0);
        req("{\"op\":\"lock\"}", NULL, 0);
        t0 = now_ms();
        r = req(p, NULL, 0);
        printf("abrir 10 004 entradas con la clave de recuperación: %.0f ms\n", now_ms() - t0);
    }

    char cmd[200];
    snprintf(cmd, sizeof cmd, "rm -rf %s", home);
    if (system(cmd)) { /* */ }
    printf("vault-e2e: %d/%d\n", total - fails, total);
    return fails ? 1 : 0;
}

/* sys.c — DNS (lado root): por interfaz con systemd-resolved (resolvectl, ruta fija), o
 * escribiendo /etc/resolv.conf si resolved no está.
 *
 * Solo sobre interfaces de LordOS (nunca una que maneja el sistema) y solo desde el network
 * namespace del sistema: resolvectl le habla al systemd-resolved del sistema con el número de
 * interfaz de NUESTRO namespace, así que desde otro netns el número podría ser el de eth0
 * (pasó en una prueba: el DNS del túnel terminó en eth0). /etc/resolv.conf también es del
 * sistema. */
#include "netpriv.h"
#include "../admin.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define RESOLVECTL "/usr/bin/resolvectl"

static int in_host_netns(void) {
    struct stat a, b;
    return stat("/proc/self/ns/net", &a) == 0 && stat("/proc/1/ns/net", &b) == 0 && a.st_ino == b.st_ino && a.st_dev == b.st_dev;
}

static int dns_allowed(const char *ifname) {
    return valid_ifname(ifname) && rt_ifindex(ifname) && !managed_by_system(ifname) && in_host_netns();
}

/* servers y domains: listas separadas por espacios, ya validadas (IPs y nombres de dominio). */
int dns_set(const char *ifname, const char *servers, const char *domains_in, int mode) {
    char domains[600];
    snprintf(domains, sizeof domains, "%s%s%s", domains_in ? domains_in : "", mode == DNS_FULL && domains_in && *domains_in ? " " : "", mode == DNS_FULL ? "~." : "");
    char out[512];
    if (!dns_allowed(ifname)) return -1;
    if (resolved_active() && access(RESOLVECTL, X_OK) == 0) {
        const char *argv[24] = { RESOLVECTL, "dns", ifname };
        char copy[512];
        snprintf(copy, sizeof copy, "%s", servers ? servers : "");
        int a = 3;
        for (char *t = strtok(copy, " "); t && a < 20; t = strtok(NULL, " ")) argv[a++] = t;
        argv[a] = NULL;
        int r = admin_run_capture(argv, out, sizeof out);
        if (*domains) {
            const char *dv[24] = { RESOLVECTL, "domain", ifname };
            char dcopy[600];
            snprintf(dcopy, sizeof dcopy, "%s", domains);
            int d = 3;
            for (char *t = strtok(dcopy, " "); t && d < 20; t = strtok(NULL, " ")) dv[d++] = t;
            dv[d] = NULL;
            admin_run_capture(dv, out, sizeof out);
        }
        /* solo el túnel completo ("~.") recibe las consultas de cualquier dominio; uno
         * parcial, solo las de sus dominios (si no, resolved también le mandaría las demás) */
        const char *rv[] = { RESOLVECTL, "default-route", ifname, mode == DNS_FULL || !*domains ? "yes" : "no", NULL };
        if (mode != DNS_PLAIN) admin_run_capture(rv, out, sizeof out);
        return r;
    }
    /* sin resolved: /etc/resolv.conf propio (solo si es un archivo normal o no existe) */
    char link[8];
    if (readlink("/etc/resolv.conf", link, sizeof link) > 0) return -1;
    char buf[1024];
    int n = snprintf(buf, sizeof buf, "# Generado por LordOS (%s)\n", ifname);
    char copy[512];
    snprintf(copy, sizeof copy, "%s", servers ? servers : "");
    for (char *t = strtok(copy, " "); t && n < (int)sizeof buf - 64; t = strtok(NULL, " ")) n += snprintf(buf + n, sizeof buf - (size_t)n, "nameserver %s\n", t);
    if (*domains && !strstr(domains, "~.") && n < (int)sizeof buf - 300) n += snprintf(buf + n, sizeof buf - (size_t)n, "search %s\n", domains);
    return write_file_atomic("/etc/resolv.conf", buf, (size_t)n, 0644);
}

void dns_revert(const char *ifname) {
    if (!dns_allowed(ifname)) return;
    if (resolved_active() && access(RESOLVECTL, X_OK) == 0) {
        char out[256];
        const char *argv[] = { RESOLVECTL, "revert", ifname, NULL };
        admin_run_capture(argv, out, sizeof out);
    }
}

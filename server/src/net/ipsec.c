/* ipsec.c — IPsec/IKEv2 con strongSwan (charon de Debian, ruta fija; sin swanctl), lado root.
 *
 * Importar (net-vpn-import ipsec\n<nombre>\n...), clave=valor:
 *   server=vpn.ejemplo.com        obligatoria
 *   remote_id=vpn.ejemplo.com     identidad del servidor (si falta, server)
 *   auth=eap | psk | cert         eap = usuario y contraseña con EAP-MSCHAPv2 (lo típico)
 *   user=… password=…             (eap)
 *   psk=… local_id=…              (psk; local_id opcional)
 *   cert=<PEM> key=<PEM>          (cert; local_id opcional, si no el sujeto del certificado)
 *   ca=<PEM>                      opcional: si falta, las autoridades del sistema (/etc/ssl/certs)
 *   ike=aes256-sha256-modp2048 esp=aes256gcm16   opcionales
 *   routes=10.0.0.0/8,…           opcional: túnel parcial; si falta, completo
 *
 * Conectar: un proceso residente "vpn:<nombre>" crea la interfaz XFRM ips-<nombre> (if_id),
 * escribe el strongswan.conf de esa VPN (0600) en /etc/strongswan.d/lordos/ y lanza su propio
 * charon (puertos al azar, socket VICI propio, sin updown ni scripts). Por VICI (vici.c, sin
 * swanctl) le pasa claves, secretos, certificados y la conexión: nada de eso queda en archivos
 * que lea otro programa. Pide IP virtual (vips), que charon pone en ips-<nombre>; rutas por esa
 * interfaz y el DNS que empuja el servidor (del registro de charon) solo en ella.
 * Las rutas de AppArmor (el perfil de charon de Debian) mandan: config en /etc/strongswan.d y
 * sockets en /run/charon.*. Un solo IPsec a la vez: charon usa /run/charon.pid. */
#include "vpn.h"
#include "vici.h"
#include "../admin.h"

#include <net/if.h>                 /* antes que los de linux/ */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHARON  "/usr/lib/ipsec/charon"
#define SCONF_DIR "/etc/strongswan.d/lordos"     /* strongswan.conf de cada VPN (AppArmor) */
#define VICI_PREFIX "/run/charon.lordos-"        /* socket VICI y DNS (AppArmor: /run/charon.*) */

static const char *const keys[] = { "server", "remote_id", "auth", "user", "password", "psk", "local_id", "ike", "esp", "routes", NULL };
static const char *const pems[] = { "ca", "cert", "key", NULL };

/* Identidades (van por VICI, pero igual se limitan a lo que tiene sentido en una identidad). */
static int id_ok(const char *s) {
    if (!*s || strlen(s) > 200) return 0;
    for (const char *p = s; *p; p++) if (!isalnum((unsigned char)*p) && !strchr("@._:+=-/, *", *p)) return 0;
    return 1;
}

static int proposal_ok(const char *s) {
    if (!*s || strlen(s) > 200) return 0;
    for (const char *p = s; *p; p++) if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p) && !strchr("-_,", *p)) return 0;
    return 1;
}

int ipsec_import(const char *name, const char *text, char *err, size_t errn) {
    VpnKv kv;
    if (vpn_kv_parse(text, keys, pems, &kv, err, errn)) return -1;
    const char *server = vpn_kv(&kv, "server"), *auth = vpn_kv(&kv, "auth");
    int rc = 0;
    if (!server || !vpn_host_ok(server)) { snprintf(err, errn, "falta server o es inválido"); rc = -1; }
    else if (!auth || (strcmp(auth, "eap") && strcmp(auth, "psk") && strcmp(auth, "cert"))) { snprintf(err, errn, "auth tiene que ser eap, psk o cert"); rc = -1; }
    else if (!strcmp(auth, "eap") && (!vpn_kv(&kv, "user") || !id_ok(vpn_kv(&kv, "user")) || !vpn_kv(&kv, "password"))) { snprintf(err, errn, "eap necesita user y password"); rc = -1; }
    else if (!strcmp(auth, "psk") && (!vpn_kv(&kv, "psk") || strlen(vpn_kv(&kv, "psk")) < 8)) { snprintf(err, errn, "psk necesita una clave de al menos 8 caracteres"); rc = -1; }
    else if (!strcmp(auth, "cert") && (!vpn_kv_pem(&kv, "cert") || !vpn_kv_pem(&kv, "key"))) { snprintf(err, errn, "cert necesita cert y key (PEM)"); rc = -1; }
    else if (vpn_kv(&kv, "remote_id") && !id_ok(vpn_kv(&kv, "remote_id"))) { snprintf(err, errn, "remote_id inválido"); rc = -1; }
    else if (vpn_kv(&kv, "local_id") && !id_ok(vpn_kv(&kv, "local_id"))) { snprintf(err, errn, "local_id inválido"); rc = -1; }
    else if (vpn_kv(&kv, "ike") && !proposal_ok(vpn_kv(&kv, "ike"))) { snprintf(err, errn, "ike inválido"); rc = -1; }
    else if (vpn_kv(&kv, "esp") && !proposal_ok(vpn_kv(&kv, "esp"))) { snprintf(err, errn, "esp inválido"); rc = -1; }
    else if (vpn_kv(&kv, "routes") && !vpn_routes_valid(vpn_kv(&kv, "routes"))) { snprintf(err, errn, "routes inválido"); rc = -1; }
    if (!rc && vpn_kv_save(name, "ipsec", &kv)) { snprintf(err, errn, "no se pudo guardar"); rc = -1; }
    if (!rc) {
        char ifn[IFNAMSIZ];
        vpn_ifname("ipsec", name, ifn, sizeof ifn);
        Buf m = {0};
        buf_printf(&m, "{\"name\":");
        buf_json_str(&m, name);
        buf_printf(&m, ",\"type\":\"ipsec\",\"ifname\":\"%s\",\"server\":", ifn);
        buf_json_str(&m, server);
        buf_printf(&m, ",\"auth\":\"%s\",\"user\":", auth);
        buf_json_str(&m, vpn_kv(&kv, "user") ? vpn_kv(&kv, "user") : "");
        buf_printf(&m, ",\"fullTunnel\":%s,\"insecure\":false}\n", vpn_kv(&kv, "routes") ? "false" : "true");
        if (!m.p || vpn_meta_save(name, m.p)) { snprintf(err, errn, "no se pudo guardar"); rc = -1; }
        buf_free(&m);
    }
    vpn_kv_free(&kv);
    return rc;
}

/* strongswan.conf de esta VPN. Va en /etc/strongswan.d/lordos/ (el perfil de AppArmor de charon
 * solo lo deja leer /etc/strongswan.d y escribir /run/charon.<algo>); el strongswan.conf del
 * sistema no lo incluye (solo incluye *.conf del primer nivel y charon/). */
static int write_sconf(const char *name, const char *ifn, const char *path) {
    Buf s = {0};
    buf_printf(&s,
        "# Generado por LordOS para la VPN %s (no editar)\n"
        "charon {\n"
        "  port = 0\n  port_nat_t = 0\n"
        "  install_routes = no\n  install_virtual_ip = yes\n  install_virtual_ip_on = %s\n"
        "  retransmit_tries = 4\n"
        "  load = random nonce openssl pem pkcs1 pkcs8 x509 revocation constraints pubkey kernel-netlink socket-default vici resolve attr eap-identity eap-mschapv2\n"
        "  plugins {\n"
        "    vici {\n      socket = unix://" VICI_PREFIX "%s.vici\n    }\n"
        "    resolve {\n      resolvconf {\n        path = /nonexistent\n      }\n    }\n"
        "    openssl {\n      load_legacy = yes\n    }\n"
        "  }\n}\n", name, ifn, name);
    int ok = !s.oom && write_file_atomic(path, s.p, s.len, 0600) == 0;
    buf_free(&s);
    return ok ? 0 : -1;
}

static int load_pem(int fd, const char *cmd, const char *type, const char *flag, const char *pem, char *err, size_t errn) {
    ViciMsg m = {0};
    vici_kv(&m, "type", type);
    if (flag) vici_kv(&m, "flag", flag);
    vici_kv(&m, "data", pem);
    ViciResult r;
    int rc = vici_simple(fd, cmd, &m, &r, 5000);
    vici_free(&m);
    if (rc && err) snprintf(err, errn, "%s: %s", cmd, r.errmsg);
    return rc;
}

/* Autoridades del sistema (si la importación no trae ca). */
static int load_system_cas(int fd) {
    DIR *d = opendir("/etc/ssl/certs");
    struct dirent *e;
    int n = 0;
    while (d && (e = readdir(d)) && n < 500) {
        size_t l = strlen(e->d_name);
        if (l < 5 || strcmp(e->d_name + l - 4, ".pem")) continue;
        char p[400];
        snprintf(p, sizeof p, "/etc/ssl/certs/%s", e->d_name);
        char *pem = read_file(p, 65536, NULL);
        if (!pem) continue;
        if (!load_pem(fd, "load-cert", "X509", "CA", pem, NULL, 0)) n++;
        free(pem);
    }
    if (d) closedir(d);
    return n;
}

static void list_items(ViciMsg *m, const char *name, const char *csv) {
    char copy[300];
    snprintf(copy, sizeof copy, "%s", csv);
    vici_list_begin(m, name);
    for (char *t = strtok(copy, ", "); t; t = strtok(NULL, ", ")) vici_item(m, t);
    vici_list_end(m);
}

/* Carga claves, secretos y la conexión "lordos" en el charon de esta VPN. */
static int vici_load(int fd, const VpnKv *kv, uint32_t if_id, char *err, size_t errn) {
    const char *auth = vpn_kv(kv, "auth"), *server = vpn_kv(kv, "server");
    const char *rid = vpn_kv(kv, "remote_id") ? vpn_kv(kv, "remote_id") : server;
    const char *lid = vpn_kv(kv, "local_id"), *routes = vpn_kv(kv, "routes");
    ViciResult r;
    if (!vpn_kv_pem(kv, "ca") && load_system_cas(fd) == 0 && strcmp(auth, "psk")) { snprintf(err, errn, "no hay autoridades en /etc/ssl/certs"); return -1; }
    if (!strcmp(auth, "cert") && load_pem(fd, "load-key", "any", NULL, vpn_kv_pem(kv, "key"), err, errn)) return -1;
    if (!strcmp(auth, "eap") || !strcmp(auth, "psk")) {
        ViciMsg m = {0};
        vici_kv(&m, "type", !strcmp(auth, "eap") ? "EAP" : "IKE");
        vici_kv(&m, "data", vpn_kv(kv, !strcmp(auth, "eap") ? "password" : "psk"));
        vici_list_begin(&m, "owners");
        if (!strcmp(auth, "eap")) vici_item(&m, vpn_kv(kv, "user"));
        else { vici_item(&m, rid); if (lid) vici_item(&m, lid); }
        vici_list_end(&m);
        int rc = vici_simple(fd, "load-shared", &m, &r, 5000);
        vici_free(&m);
        if (rc) { snprintf(err, errn, "load-shared: %s", r.errmsg); return -1; }
    }
    char hex[16];
    snprintf(hex, sizeof hex, "0x%x", if_id);
    ViciMsg m = {0};
    vici_begin(&m, "lordos");
    vici_kv(&m, "version", "2");
    list_items(&m, "remote_addrs", server);
    list_items(&m, "vips", "0.0.0.0,::");
    vici_kv(&m, "if_id_in", hex);
    vici_kv(&m, "if_id_out", hex);
    vici_kv(&m, "dpd_delay", "30s");
    if (vpn_kv(kv, "ike")) list_items(&m, "proposals", vpn_kv(kv, "ike"));
    vici_begin(&m, "local");
    if (!strcmp(auth, "eap")) {
        vici_kv(&m, "auth", "eap-mschapv2");
        vici_kv(&m, "eap_id", vpn_kv(kv, "user"));
        vici_kv(&m, "id", lid ? lid : vpn_kv(kv, "user"));
    } else if (!strcmp(auth, "psk")) {
        vici_kv(&m, "auth", "psk");
        if (lid) vici_kv(&m, "id", lid);
    } else {
        vici_kv(&m, "auth", "pubkey");
        vici_list_begin(&m, "certs");
        vici_item(&m, vpn_kv_pem(kv, "cert"));
        vici_list_end(&m);
        if (lid) vici_kv(&m, "id", lid);
    }
    vici_end(&m);
    vici_begin(&m, "remote");
    vici_kv(&m, "auth", !strcmp(auth, "psk") ? "psk" : "pubkey");
    vici_kv(&m, "id", rid);
    if (vpn_kv_pem(kv, "ca")) { vici_list_begin(&m, "cacerts"); vici_item(&m, vpn_kv_pem(kv, "ca")); vici_list_end(&m); }
    vici_end(&m);
    vici_begin(&m, "children");
    vici_begin(&m, "net");
    list_items(&m, "remote_ts", routes ? routes : "0.0.0.0/0,::/0");
    vici_kv(&m, "if_id_in", hex);
    vici_kv(&m, "if_id_out", hex);
    vici_kv(&m, "dpd_action", "clear");
    if (vpn_kv(kv, "esp")) list_items(&m, "esp_proposals", vpn_kv(kv, "esp"));
    vici_end(&m);
    vici_end(&m);
    vici_end(&m);
    int rc = vici_simple(fd, "load-conn", &m, &r, 5000);
    vici_free(&m);
    if (rc) { snprintf(err, errn, "load-conn: %s", r.errmsg); return -1; }
    return 0;
}

typedef struct { char log[2048]; ViciResult res; } InitCtx;

static void init_log(const char *path, const char *key, const char *val, void *ud) {
    InitCtx *c = ud;
    (void)path;
    if (!strcmp(key, "msg") && strlen(c->log) + strlen(val) + 2 < sizeof c->log)
        snprintf(c->log + strlen(c->log), sizeof c->log - strlen(c->log), "%s\n", val);
}

static void init_reply(const char *path, const char *key, const char *val, void *ud) { vici_result_cb(path, key, val, &((InitCtx *)ud)->res); }

typedef struct { char vip[64]; int established; } SaCtx;

static void sa_cb(const char *path, const char *key, const char *val, void *ud) {
    SaCtx *s = ud;
    if (strcmp(path, "lordos")) return;
    if (!strcmp(key, "state") && !strcmp(val, "ESTABLISHED")) s->established = 1;
    if (!strcmp(key, "local-vips") && !s->vip[0] && strchr(val, '.')) snprintf(s->vip, sizeof s->vip, "%s", val);
}

static int list_sas(int fd, SaCtx *s) {
    memset(s, 0, sizeof *s);
    ViciMsg m = {0};
    vici_kv(&m, "ike", "lordos");
    int rc = vici_call(fd, "list-sas", &m, "list-sa", sa_cb, NULL, s, 5000);
    vici_free(&m);
    return rc;
}

void ipsec_run(const char *name) {
    vpn_signals();
    VpnKv kv;
    if (vpn_kv_load(name, "ipsec", &kv)) { admin_event("net vpn %s error no-config", name); admin_event("net vpn %s disconnected", name); return; }
    char ifn[IFNAMSIZ], sconf[200], sock[160], dnsf[160], err[300] = "";
    vpn_ifname("ipsec", name, ifn, sizeof ifn);
    snprintf(sconf, sizeof sconf, SCONF_DIR "/%s.conf", name);
    snprintf(sock, sizeof sock, VICI_PREFIX "%s.vici", name);
    snprintf(dnsf, sizeof dnsf, VICI_PREFIX "%s.dns", name);
    mkdir("/etc/strongswan.d", 0755);
    mkdir(SCONF_DIR, 0700);
    unlink(dnsf);
    unlink(sock);
    admin_event("net vpn %s connecting", name);
    int sfam = 0;
    uint8_t sip[16];
    if (vpn_resolve(vpn_kv(&kv, "server"), &sfam, sip)) { admin_event("net vpn %s error no-resolve", name); admin_event("net vpn %s disconnected", name); vpn_kv_free(&kv); return; }
    /* if_id: del nombre, nunca 0 */
    uint32_t if_id = 0x4c000000u;
    for (const char *p = name; *p; p++) if_id = (if_id * 31 + (unsigned char)*p) | 0x4c000000u;
    if_id &= 0x4cffffffu;
    int fd = nl_open(NETLINK_ROUTE, 0);
    if (fd < 0) { vpn_kv_free(&kv); return; }
    if (rt_ifindex(ifn)) rt_link_del(fd, rt_ifindex(ifn));
    int r = rt_link_new_xfrm(fd, ifn, if_id);
    if (r) { close(fd); admin_event("net vpn %s error xfrm %s", name, strerror(-r)); admin_event("net vpn %s disconnected", name); vpn_kv_free(&kv); return; }
    rt_link_set(fd, rt_ifindex(ifn), 1);
    close(fd);
    vpn_mark(ifn, name, 1);
    pid_t charon = -1;
    int cout = -1, vfd = -1, connected = 0;
    if (write_sconf(name, ifn, sconf)) { admin_event("net vpn %s error write", name); goto out; }
    char envs[300];
    snprintf(envs, sizeof envs, "STRONGSWAN_CONF=%s", sconf);
    const char *cargv[] = { CHARON, NULL };
    charon = vpn_spawn(cargv, &cout, NULL, envs);
    if (charon < 0) { admin_event("net vpn %s error charon", name); goto out; }
    for (int i = 0; i < 100 && (vfd = vici_connect(sock)) < 0; i++) usleep(100000);
    if (vfd < 0) { admin_event("net vpn %s error charon-no-vici", name); goto out; }
    if (vici_load(vfd, &kv, if_id, err, sizeof err)) { admin_event("net vpn %s error %.200s", name, err); goto out; }
    InitCtx ic = { .log = "" };
    ViciMsg m = {0};
    vici_kv(&m, "child", "net");
    vici_kv(&m, "ike", "lordos");
    vici_kv(&m, "timeout", "30000");
    vici_kv(&m, "loglevel", "1");
    r = vici_call(vfd, "initiate", &m, "control-log", init_log, init_reply, &ic, 40000);
    vici_free(&m);
    if (r || !ic.res.success) {
        if (strstr(ic.log, "AUTHENTICATION_FAILED") || strstr(ic.log, "EAP_FAILURE") || strstr(ic.log, "MSCHAPV2") || strstr(ic.log, "authentication of"))
            admin_event("net vpn %s auth-failed", name);
        else {
            char *last = NULL;
            for (char *l = strtok(ic.log, "\n"); l; l = strtok(NULL, "\n")) if (strlen(l) > 8) last = l;
            admin_event("net vpn %s error %.160s", name, last ? last : ic.res.errmsg);
        }
        goto out;
    }
    SaCtx sa;
    list_sas(vfd, &sa);
    /* Los DNS que empujó el servidor: el plugin resolve de charon los quiere instalar con
     * resolvconf (que apuntamos a /nonexistent, así no toca nada) y antes los anota en su
     * registro: "installing DNS server 10.21.0.1 via resolvconf". Los aplica LordOS. */
    char servers[400] = "";
    {
        char lb[16384];
        ssize_t k;
        fcntl(cout, F_SETFL, fcntl(cout, F_GETFL) | O_NONBLOCK);
        while ((k = read(cout, lb, sizeof lb - 1)) > 0) {
            lb[k] = 0;
            for (char *l = strtok(lb, "\n"); l; l = strtok(NULL, "\n")) {
                const char *p = strstr(l, "installing DNS server ");
                char ip[64];
                int fam;
                uint8_t a[16];
                if (p && sscanf(p + 22, "%63s", ip) == 1 && !parse_ip(ip, &fam, a) && !strstr(servers, ip) && strlen(servers) + strlen(ip) + 2 < sizeof servers)
                    snprintf(servers + strlen(servers), sizeof servers - strlen(servers), "%s%s", servers[0] ? " " : "", ip);
            }
        }
    }
    const char *routes = vpn_kv(&kv, "routes");
    if (routes) vpn_routes_list(ifn, routes); else vpn_routes_full(ifn, sfam, sip);
    if (servers[0]) dns_set(ifn, servers, "", routes ? DNS_SPLIT : DNS_FULL);
    connected = 1;
    admin_event("net vpn %s connected %s", name, sa.vip);
    /* vigilar: charon vivo y la SA establecida */
    time_t next = time(NULL) + 10;
    while (!vpn_stop) {
        struct pollfd pf = { cout, POLLIN, 0 };
        if (poll(&pf, 1, 1000) > 0) {
            char junk[4096];
            if (read(cout, junk, sizeof junk) <= 0) { admin_event("net vpn %s error charon-exit", name); break; }
        }
        if (time(NULL) >= next) {
            next = time(NULL) + 10;
            if (list_sas(vfd, &sa) || !sa.established) { admin_event("net vpn %s error lost", name); break; }
        }
    }
    {
        ViciMsg t = {0};
        vici_kv(&t, "ike", "lordos");
        vici_kv(&t, "timeout", "5000");
        ViciResult tr;
        vici_simple(vfd, "terminate", &t, &tr, 8000);
        vici_free(&t);
    }
out:
    if (connected) dns_revert(ifn);
    if (vfd >= 0) close(vfd);
    vpn_reap(charon);
    if (cout >= 0) close(cout);
    fd = nl_open(NETLINK_ROUTE, 0);
    if (fd >= 0) { if (rt_ifindex(ifn)) rt_link_del(fd, rt_ifindex(ifn)); close(fd); }
    vpn_routes_clear(sfam, sip);
    vpn_mark(ifn, name, 0);
    unlink(dnsf);
    unlink(sock);
    unlink(sconf);
    vpn_kv_free(&kv);
    admin_event("net vpn %s disconnected", name);
}

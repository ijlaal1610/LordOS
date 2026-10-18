/* net.h — red y VPN propias de LordOS (ver docs/RED.md).
 *
 * Dos lados:
 *  - lord-server (sin root): lee el estado con rtnetlink (interfaces, direcciones, rutas,
 *    contadores) y escucha los cambios; lord.net (ui/js_net.c) lo expone a JS.
 *  - el ayudante privilegiado (admin.c, root): las acciones "net-*" de la tabla de abajo:
 *    configurar interfaces, cliente DHCP propio, Wi-Fi (wpa_supplicant), WireGuard (netlink
 *    propio) y OpenVPN. */
#ifndef LORD_NET_H
#define LORD_NET_H

#include <stddef.h>
#include "../admin.h"

/* ---- lado de lord-server ---- */

int  net_open(void);                    /* socket de avisos de rtnetlink; 0 bien */
int  net_fd(void);                      /* para poll; -1 si no está */
/* Hubo avisos: los consume y, si algo cambió, llama a fn una vez (JS __lordNet). */
void net_dispatch(void (*fn)(const char *what));

/* JSON (malloc; el que llama libera) para lord.net. NULL si falla. */
char *net_interfaces_json(void);
char *net_routes_json(void);
char *net_dns_json(void);
char *net_vpn_list_json(void);          /* metadatos públicos de /etc/lordos/net/vpn */
char *net_wifi_known_json(void);
/* [{type, available, optional, package, insecure}] de los tipos de VPN */
char *net_vpn_types_json(void);
char *net_if_config_json(const char *ifname);
/* Lee un .ovpn del usuario e incrusta los archivos que nombra (ca, cert, key, tls-auth...)
 * que estén en la misma carpeta. NULL + err si no se puede. */
char *net_ovpn_inline(const char *path, char *err, size_t errn);

/* ---- lado del ayudante (root): acciones ---- */

void net_act_restore(const AdminReq *r, const char *arg, size_t len);
void net_act_up(const AdminReq *r, const char *arg, size_t len);
void net_act_down(const AdminReq *r, const char *arg, size_t len);
void net_act_config(const AdminReq *r, const char *arg, size_t len);
void net_act_manage(const AdminReq *r, const char *arg, size_t len);
void net_act_unmanage(const AdminReq *r, const char *arg, size_t len);
void net_act_wifi_scan(const AdminReq *r, const char *arg, size_t len);
void net_act_wifi_connect(const AdminReq *r, const char *arg, size_t len);
void net_act_wifi_disconnect(const AdminReq *r, const char *arg, size_t len);
void net_act_wifi_forget(const AdminReq *r, const char *arg, size_t len);
void net_act_vpn_import(const AdminReq *r, const char *arg, size_t len);
void net_act_vpn_credentials(const AdminReq *r, const char *arg, size_t len);
void net_act_vpn_up(const AdminReq *r, const char *arg, size_t len);
void net_act_vpn_down(const AdminReq *r, const char *arg, size_t len);
void net_act_vpn_delete(const AdminReq *r, const char *arg, size_t len);
void net_act_vpn_status(const AdminReq *r, const char *arg, size_t len);
void net_act_vpn_enable(const AdminReq *r, const char *arg, size_t len);

/* Acciones del ayudante privilegiado (admin.c) que agrega este módulo. A_ANYONE: también un
 * usuario que no es administrador (conectarse a una red o a una VPN ya configurada). */
#define NET_ADMIN_ACTIONS \
    { "net-restore", net_act_restore, A_ANYONE }, \
    { "net-up", net_act_up, 0 }, \
    { "net-down", net_act_down, 0 }, \
    { "net-config", net_act_config, 0 }, \
    { "net-manage", net_act_manage, 0 }, \
    { "net-unmanage", net_act_unmanage, 0 }, \
    { "net-wifi-scan", net_act_wifi_scan, A_FORK | A_ANYONE }, \
    { "net-wifi-connect", net_act_wifi_connect, A_ANYONE }, \
    { "net-wifi-disconnect", net_act_wifi_disconnect, A_ANYONE }, \
    { "net-wifi-forget", net_act_wifi_forget, 0 }, \
    { "net-vpn-import", net_act_vpn_import, 0 }, \
    { "net-vpn-credentials", net_act_vpn_credentials, 0 }, \
    { "net-vpn-up", net_act_vpn_up, A_ANYONE }, \
    { "net-vpn-down", net_act_vpn_down, A_ANYONE }, \
    { "net-vpn-delete", net_act_vpn_delete, 0 }, \
    { "net-vpn-status", net_act_vpn_status, A_ANYONE }, \
    { "net-vpn-enable", net_act_vpn_enable, A_FORK },

#endif

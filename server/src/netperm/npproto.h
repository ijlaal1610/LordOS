/* npproto.h — el protocolo cerrado entre lord-server (cada sesión, sin root) y lord-netperm (root).
 * Ver docs/FIREWALL.md, paso 2, "El socket".
 *
 * Socket unix /run/lordos/netperm.sock (0666: cualquiera puede conectarse; lord-netperm sabe quién
 * es por SO_PEERCRED y solo le habla de las apps de ese usuario). Cada mensaje es:
 *
 *   u32 largo (little endian, cuenta el tipo y los datos) · u8 tipo · datos
 *
 * De lord-server a lord-netperm hay UN solo mensaje, sin datos:
 *   NP_MSG_RELOAD   "volvé a leer ~/.config/lordos/firewall.json de mi usuario"
 * Las reglas nunca viajan por el socket: lord-netperm lee el archivo del usuario él mismo, con una
 * ruta fija y validada (sin enlaces, del usuario, archivo común, hasta 4 MB) y el lector estricto
 * de fw/rules.c. Cualquier otro mensaje, o uno de más de NP_IN_MAX bytes, corta la conexión.
 *
 * De lord-netperm a lord-server (JSON compacto en los datos):
 *   NP_MSG_STATUS   {"bpf":true,"dns":"resolved","error":""}             al conectarse y si cambia
 *   NP_MSG_EVENT    {"app":"desktop:x","v":"deny","dir":"out","proto":"tcp","addr":"1.2.3.4",
 *                    "port":443,"by":"rule","rule":7,"flag":"","record":0}  una conexión nueva
 *   NP_MSG_COUNTS   {"desktop:x":[permitidas,denegadas],...}              cada segundo, si cambió */
#ifndef LORD_NP_PROTO_H
#define LORD_NP_PROTO_H

#define NP_SOCK_PATH  "/run/lordos/netperm.sock"
#define NP_MSG_RELOAD 1
#define NP_MSG_STATUS 10
#define NP_MSG_EVENT  11
#define NP_MSG_COUNTS 12
#define NP_IN_MAX     64            /* lo más largo que acepta lord-netperm */
#define NP_OUT_MAX    (1u << 20)    /* lo más largo que acepta lord-server */

#endif

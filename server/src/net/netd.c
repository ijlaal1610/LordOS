/* netd.c — lord-netd: la red de LordOS es del sistema, no de una sesión (docs/RED.md, «La red es
 * del sistema»).
 *
 * Lo lanza lord-sessiond al arrancar (ruta fija, al lado de su binario, de root y no escribible
 * por otros), sin argumentos, y lo vuelve a lanzar si termina. Levanta la red antes de que alguien
 * entre (DHCP, Wi-Fi guardadas, configs de /etc/lordos/net/if, DNS), así la pantalla de ingreso
 * muestra la IP y el escritorio remoto se alcanza; y la mantiene aunque las sesiones entren y
 * salgan. Es el mismo admin.c compilado con LORD_NETD: su tabla tiene solo las acciones net-*
 * (las mismas validaciones), y las atiende por /run/lordos/netd.sock, un socket 0600 al que solo
 * se conectan los ayudantes root de las sesiones. No ejecuta nada fuera de lo que ya ejecutan
 * esas acciones (wpa_supplicant, openvpn, charon, pppd, resolvectl, networkctl… con ruta fija). */
#include "../admin.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    (void)argv;
    if (argc != 1) { fprintf(stderr, "uso: lord-netd (sin argumentos; lo lanza lord-sessiond)\n"); return 2; }
    if (geteuid() != 0) { fprintf(stderr, "lord-netd: tiene que correr como root\n"); return 1; }
    setvbuf(stderr, NULL, _IOLBF, 0);
    return admin_netd_serve();
}

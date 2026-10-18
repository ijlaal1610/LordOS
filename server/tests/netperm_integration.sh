#!/bin/bash
# netperm_integration.sh — prueba de lord-netperm con el kernel de verdad (docs/FIREWALL.md, paso 2).
# Se corre en la VM de prueba como el usuario de la sesión (con sudo sin clave), con lord-sessiond
# (Delegate=yes) y lord-netperm corriendo:
#
#   bash server/tests/netperm_integration.sh [puerta-de-enlace]
#
# Arma una scope de app a mano (como lo hace el lanzador de lord-server), escribe reglas en
# ~/.config/lordos/firewall.json, le pide a lord-netperm que las relea por su socket y prueba:
# denegar y permitir, una regla de dominio que deja un solo sitio, "solo LAN", "sin conexiones
# entrantes" y "sin red". Solo usa la puerta de enlace, el propio equipo y example.com/debian.org.
# Al final deja el firewall.json como estaba.
set -u
GW=${1:-172.17.0.1}
U=$(id -u)
SESS=/sys/fs/cgroup/system.slice/lordos.service/lordos-session-$U
SC=$SESS/app-lordos-nptest.scope
FW=~/.config/lordos/firewall.json
pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  bien: $*"; }
bad() { fail=$((fail+1)); echo "  MAL:  $*"; }
[ -d "$SESS" ] || { echo "no hay sesión de LordOS para el uid $U ($SESS)"; exit 2; }
[ -S /run/lordos/netperm.sock ] || { echo "lord-netperm no está corriendo"; exit 2; }
mkdir -p ~/.config/lordos
BACKUP=$(mktemp); HAD=0
[ -f "$FW" ] && { cp "$FW" "$BACKUP"; HAD=1; }
trap 'if [ $HAD = 1 ]; then cp "$BACKUP" "$FW"; else rm -f "$FW"; fi; reload; rm -f "$BACKUP"' EXIT
sudo mkdir -p "$SC" && sudo chown "$U" "$SC" "$SC/cgroup.procs" 2>/dev/null

reload() {
    python3 - <<'PY'
import socket, struct, time
s = socket.socket(socket.AF_UNIX); s.connect('/run/lordos/netperm.sock')
s.sendall(struct.pack('<IB', 1, 1)); time.sleep(0.3); s.close()
PY
}
rules() { echo "$1" > "$FW"; reload; }

# corre python adentro de la scope, como el usuario
py() { sudo bash -c "echo \$\$ > $SC/cgroup.procs; exec setpriv --reuid=$U --regid=$(id -g) --init-groups python3 -c \"\$0\" \"\$@\"" "$@"; }
TRY='
import socket, sys, errno, ssl
kind, host, port = sys.argv[1], sys.argv[2], int(sys.argv[3])
try:
    if kind == "bind":
        s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind((host, port)); s.listen(); print("ok")
    elif kind == "udp":
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.sendto(b"x", (host, port)); print("ok")
    elif kind == "sock":
        socket.socket(); print("ok")
    elif kind == "https":
        ai = socket.getaddrinfo(host, port, socket.AF_INET, socket.SOCK_STREAM)[0][4]
        s = socket.create_connection(ai, timeout=6)
        c = ssl.create_default_context().wrap_socket(s, server_hostname=host)
        c.sendall(b"HEAD / HTTP/1.1\r\nHost: " + host.encode() + b"\r\nConnection: close\r\n\r\n")
        print("ok", c.recv(12).decode(errors="replace"))
    else:
        s = socket.create_connection((host, port), timeout=4); print("ok")
except OSError as e:
    print(errno.errorcode.get(e.errno, str(e.errno)))
'
try() { py "$TRY" "$@" 2>&1 | tail -1; }
expect() {      # expect <resultado> <tipo> <host> <puerto> <texto>
    local want=$1; shift; local what=$4; local got
    got=$(try "$1" "$2" "$3")
    case "$got" in $want*) ok "$what → $got";; *) bad "$what → $got (se esperaba $want)";; esac
}
APP=desktop:nptest
R='{"v":1,"mode":"silent","trusted":[],"restricted":{},"rules":[]}'

echo "1. sin reglas (silencioso): se permite"
rules "$R"
expect ok tcp "$GW" 53 "TCP a la puerta de enlace"

echo "2. denegar la app → EPERM; permitirla → anda"
rules '{"v":1,"mode":"silent","trusted":[],"restricted":{},"rules":[{"app":"'$APP'","dir":"out","dst":"any","action":"deny","when":0}]}'
expect EPERM tcp "$GW" 53 "TCP denegado"
expect EPERM udp "$GW" 53 "UDP (sendto) denegado"
expect EPERM tcp ::1 22 "TCP por IPv6 denegado"
rules '{"v":1,"mode":"silent","trusted":[],"restricted":{},"rules":[{"app":"'$APP'","dir":"out","dst":"any","action":"allow","when":0}]}'
expect ok tcp "$GW" 53 "TCP permitido otra vez"

echo "3. regla de dominio: solo example.com"
rules '{"v":1,"mode":"silent","trusted":[],"restricted":{},"rules":[{"app":"'$APP'","dir":"out","dst":"any","action":"deny","when":0},{"app":"'$APP'","dir":"out","dst":"example.com","action":"allow","when":0}]}'
expect "ok HTTP" https example.com 443 "https://example.com (permitido por nombre)"
expect EPERM https www.debian.org 443 "https://www.debian.org (no está en la regla)"
IP=$(getent ahostsv4 example.com | awk 'NR==1{print $1}')
expect ok tcp "$IP" 443 "la IP de example.com que dio el resolvedor"
expect EPERM tcp 93.184.215.14 443 "una IP que el resolvedor no dio para example.com"

echo "4. solo LAN (sin internet)"
rules '{"v":1,"mode":"silent","trusted":[],"restricted":{"'$APP'":["no-internet"]},"rules":[]}'
expect ok tcp "$GW" 53 "la puerta de enlace"
expect EPERM https example.com 443 "internet"

echo "5. sin conexiones entrantes"
rules '{"v":1,"mode":"silent","trusted":[],"restricted":{"'$APP'":["no-incoming"]},"rules":[]}'
expect EPERM bind 0.0.0.0 18080 "bind/listen en 18080"
expect ok bind 0.0.0.0 0 "bind a un puerto efímero (cliente)"
expect ok tcp "$GW" 53 "salir sigue andando"

echo "6. sin red"
rules '{"v":1,"mode":"silent","trusted":[],"restricted":{"'$APP'":["no-network"]},"rules":[]}'
expect EPERM sock 0 0 "crear un socket"

echo "7. apagado: solo valen las denegaciones explícitas"
rules '{"v":1,"mode":"off","trusted":[],"restricted":{},"rules":[{"app":"'$APP'","dir":"out","dst":"'$GW'","port":"53","proto":"tcp","action":"deny","when":0}]}'
expect EPERM tcp "$GW" 53 "la denegación explícita"
expect ok tcp "$GW" 22 "lo demás"

echo "8. otra app no hereda las reglas"
rules '{"v":1,"mode":"silent","trusted":[],"restricted":{},"rules":[{"app":"'$APP'","dir":"out","dst":"any","action":"deny","when":0}]}'
got=$(python3 -c 'import socket;socket.create_connection(("'$GW'",53),timeout=4);print("ok")' 2>&1 | tail -1)
[ "$got" = ok ] && ok "fuera de la scope → ok" || bad "fuera de la scope → $got"

echo "netperm-integration: $pass bien, $fail mal"
sudo rmdir "$SC" 2>/dev/null
[ $fail = 0 ]

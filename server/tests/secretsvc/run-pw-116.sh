#!/bin/sh
# run-pw-116.sh — Secret Service propio contra el bus real y libsecret (secret-tool).
# Uso (en la VM, desde server/, con build/secretsvc-demo ya compilado):
#   sh tests/secretsvc/run-pw-116.sh [--capture tests/secretsvc/libsecret-capture.bin]
# Necesita libsecret-tools (secret-tool) y dbus-bin (dbus-send, dbus-monitor).
# Deja el bus como estaba: ningún demo queda con el nombre org.freedesktop.secrets.
set -u
cd "$(dirname "$0")/../.." || exit 1
: "${XDG_RUNTIME_DIR:=/run/user/$(id -u)}"
export XDG_RUNTIME_DIR
: "${DBUS_SESSION_BUS_ADDRESS:=unix:path=$XDG_RUNTIME_DIR/bus}"
export DBUS_SESSION_BUS_ADDRESS
CAP=""
[ "${1:-}" = "--capture" ] && CAP="$2"
LOG=$(mktemp /tmp/ss-demo-XXXXXX.log)
ok=0; bad=0
pass() { ok=$((ok+1)); echo "ok   $1"; }
fail() { bad=$((bad+1)); echo "FALLA $1"; }
owner() { dbus-send --session --print-reply --dest=org.freedesktop.DBus / org.freedesktop.DBus.NameHasOwner string:org.freedesktop.secrets 2>/dev/null | grep -q "boolean true"; }

if owner; then echo "org.freedesktop.secrets ya tiene dueño: no se prueba"; exit 2; fi
pgrep -f "[g]nome-keyring-daemon" >/dev/null && echo "aviso: gnome-keyring-daemon está corriendo"
[ -x build/secretsvc-demo ] || { echo "falta build/secretsvc-demo"; exit 2; }

start() { build/secretsvc-demo "$@" --seconds 60 >>"$LOG" 2>&1 & DEMO=$!; i=0; while ! owner && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done; }
stop() { kill "$DEMO" 2>/dev/null; wait "$DEMO" 2>/dev/null; }
MON=""
if [ -n "$CAP" ]; then dbus-monitor --session --binary "type='method_call',destination='org.freedesktop.secrets'" >"$CAP" 2>/dev/null & MON=$!; sleep 0.3; fi

# --- desbloqueado ---
start
owner && pass "el demo tiene org.freedesktop.secrets" || fail "el demo no tomó el nombre"
build/secretsvc-demo --seconds 2 >>"$LOG" 2>&1; [ $? -eq 3 ] && pass "un segundo servicio ve el nombre ocupado (ss_start = 1)" || fail "segundo servicio"
printf 'contraseña ñandú 123' | secret-tool store --label="Prueba LordOS" service lordos-demo user ana && pass "secret-tool store" || fail "secret-tool store"
v=$(secret-tool lookup service lordos-demo user ana); [ "$v" = "contraseña ñandú 123" ] && pass "secret-tool lookup" || fail "secret-tool lookup ($v)"
printf 'otra' | secret-tool store --label="Segunda" service lordos-demo user beto
secret-tool search --all service lordos-demo >/tmp/ss-search.$$ 2>&1
grep -q "label = Prueba LordOS" /tmp/ss-search.$$ && grep -q "label = Segunda" /tmp/ss-search.$$ && pass "secret-tool search --all (2 ítems)" || { fail "secret-tool search"; cat /tmp/ss-search.$$; }
grep -q "secret = contraseña ñandú 123" /tmp/ss-search.$$ && pass "search trae los secretos" || fail "search sin secretos"
printf 'cambiada' | secret-tool store --label="Prueba LordOS" service lordos-demo user ana
v=$(secret-tool lookup service lordos-demo user ana); [ "$v" = "cambiada" ] && pass "store de nuevo reemplaza" || fail "reemplazo ($v)"
secret-tool clear service lordos-demo user ana; v=$(secret-tool lookup service lordos-demo user ana); [ -z "$v" ] && pass "secret-tool clear" || fail "clear ($v)"
secret-tool lock --collection=login && pass "secret-tool lock" || fail "secret-tool lock"
v=$(timeout 15 secret-tool lookup service lordos-demo user beto); [ "$v" = "otra" ] && pass "lookup bloqueado: Unlock con aviso y después el secreto" || fail "lookup tras lock ($v)"
stop

# --- bloqueado: el aviso se acepta solo al segundo ---
start --locked
printf 'con aviso' | timeout 15 secret-tool store --label="Bloqueado" service lordos-demo user carla && pass "store con el llavero bloqueado (aviso de desbloqueo)" || fail "store bloqueado"
v=$(timeout 15 secret-tool lookup service lordos-demo user carla); [ "$v" = "con aviso" ] && pass "lookup después del aviso" || fail "lookup bloqueado ($v)"
stop
[ -n "$MON" ] && { kill "$MON" 2>/dev/null; wait "$MON" 2>/dev/null; }

grep -q "OpenSession dh-ietf1024-sha256-aes128-cbc-pkcs7" "$LOG" && pass "libsecret negoció dh-ietf1024-sha256-aes128-cbc-pkcs7" || echo "nota: libsecret no usó DH"
grep -q "OpenSession plain" "$LOG" && echo "nota: también hubo sesiones plain"
grep -q "Unlock → aviso" "$LOG" && pass "Unlock pasó por un Prompt" || echo "nota: no hubo Prompt"
owner && fail "quedó alguien con el nombre" || pass "el nombre quedó libre"
echo "registro: $LOG"
rm -f /tmp/ss-search.$$
echo "secretsvc con secret-tool: $ok bien, $bad fallas"
[ $bad -eq 0 ]

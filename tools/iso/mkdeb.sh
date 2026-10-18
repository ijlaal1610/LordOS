#!/bin/sh
# mkdeb.sh — arma lordos_<versión>_amd64.deb desde el árbol ya compilado.
# Uso: tools/iso/mkdeb.sh <salida.deb>     (desde la raíz del repositorio; necesita dpkg-deb)
#
# Contenido:
#   /usr/lib/lordos/bin/lord-sessiond     sesiones, pantalla de ingreso y escritorio remoto (root)
#   /usr/lib/lordos/bin/lord-netperm      permisos de red por app con BPF de cgroup (root, servicio propio)
#   /usr/lib/lordos/bin/lord-netd         la red del sistema (root; lo lanza y vigila lord-sessiond)
#   /usr/lib/lordos/bin/lord-server       el servidor de cada sesión (busca ../themes y ../assets)
#   /usr/lib/lordos/themes/<tema>/        theme.json, theme.bundle.js y sus recursos (greeter: la
#                                         pantalla de ingreso)
#   /usr/lib/lordos/assets/               fuentes, textos (locales), íconos, integración del shell
#   /usr/lib/lordos/lordos-live-prep      preparación del live (idioma de grub, carpetas)
#   /usr/lib/systemd/system/lordos.service  lord-sessiond en tty1
#   /usr/share/doc/lordos/                README, copyright, changelog
set -eu
OUT=${1:?uso: mkdeb.sh salida.deb}
ROOT=$(pwd)
VERSION=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' package.json | head -1)
[ -x server/build/lord-server ] || { echo "falta server/build/lord-server (make -C server)" >&2; exit 1; }
[ -x server/build/lord-netperm ] || { echo "falta server/build/lord-netperm (make -C server; clang para BPF)" >&2; exit 1; }
[ -x server/build/lord-netd ] || { echo "falta server/build/lord-netd (make -C server)" >&2; exit 1; }
[ -x server/build/lord-sessiond ] || { echo "falta server/build/lord-sessiond (make -C server, con libssl-dev)" >&2; exit 1; }
[ -f themes/greeter/theme.bundle.js ] || { echo "falta la pantalla de ingreso (npm run build)" >&2; exit 1; }
for t in themes/*/theme.bundle.js; do [ -f "$t" ] || { echo "faltan los temas construidos (npm run build)" >&2; exit 1; }; done

PKG=$(mktemp -d)
trap 'rm -rf "$PKG"' EXIT
L="$PKG/usr/lib/lordos"
install -d "$L/bin" "$L/themes" "$L/assets" "$PKG/usr/lib/systemd/system" "$PKG/usr/share/doc/lordos" "$PKG/DEBIAN"
install -m 755 server/build/lord-server "$L/bin/lord-server"
install -m 755 server/build/lord-sessiond "$L/bin/lord-sessiond"
install -m 755 server/build/lord-netd "$L/bin/lord-netd"
install -m 755 server/build/lord-netperm "$L/bin/lord-netperm"
# ayudante aislado de PDF y comprimidos (opcional: make lo omite sin libpoppler-glib-dev/libarchive-dev);
# lord-server solo lanza el lord-doc de su misma carpeta
[ -x server/build/lord-doc ] && install -m 755 server/build/lord-doc "$L/bin/lord-doc"
strip --strip-unneeded "$L/bin/lord-server" "$L/bin/lord-sessiond" "$L/bin/lord-netd" "$L/bin/lord-netperm" 2>/dev/null || true
[ -x "$L/bin/lord-doc" ] && { strip --strip-unneeded "$L/bin/lord-doc" 2>/dev/null || true; }
# lord-server solo lanza el lord-media de su misma carpeta (medios: video, fotos, cámara, grabadora)
[ -x server/build/lord-media ] && install -m 755 server/build/lord-media "$L/bin/lord-media"
[ -x "$L/bin/lord-media" ] && { strip --strip-unneeded "$L/bin/lord-media" 2>/dev/null || true; }
for d in themes/*/; do
    n=$(basename "$d")
    [ -f "$d/theme.bundle.js" ] || continue
    install -d "$L/themes/$n"
    install -m 644 "$d/theme.json" "$d/theme.bundle.js" "$L/themes/$n/"
    [ -d "$d/assets" ] && cp -r "$d/assets" "$L/themes/$n/"
done
cp -r assets/fonts assets/locales assets/shell assets/icons assets/monitor "$L/assets/"
# el proceso de cada app aislada (lord-server --app) carga su runtime de acá
[ -f assets/app-host.bundle.js ] || { echo "falta assets/app-host.bundle.js (npm run build)" >&2; exit 1; }
install -m 644 assets/app-host.bundle.js "$L/assets/app-host.bundle.js"
install -m 755 tools/iso/rootfs/usr/lib/lordos/lordos-live-prep "$L/lordos-live-prep"
install -m 644 tools/iso/rootfs/etc/systemd/system/lordos.service "$PKG/usr/lib/systemd/system/lordos.service"
# temas GTK3 de LordOS (los elige el portal de ajustes según claro/oscuro)
install -d "$PKG/usr/share/themes"
cp -r assets/gtk/LordOS assets/gtk/LordOS-dark "$PKG/usr/share/themes/"
find "$PKG/usr/share/themes" -type d -exec chmod 755 {} +; find "$PKG/usr/share/themes" -type f -exec chmod 644 {} +
install -m 644 tools/iso/rootfs/etc/systemd/system/lord-netperm.service "$PKG/usr/lib/systemd/system/lord-netperm.service"
find "$L" -type d -exec chmod 755 {} +
find "$L/themes" "$L/assets" -type f -exec chmod 644 {} +

D="$PKG/usr/share/doc/lordos"
install -m 644 README.md "$D/README.md"
cat > "$D/copyright" <<'C'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: LordOS
Source: https://github.com/lordbasex/LordOS

Files: *
Copyright: LordOS
License: proprietary-or-tbd
 La licencia de LordOS todavía no está definida (ver el repositorio).

Files: usr/lib/lordos/assets/fonts/*
License: OFL-1.1
 Inter y JetBrains Mono: SIL Open Font License 1.1.
 En Debian: /usr/share/common-licenses no la incluye; ver https://openfontlicense.org

Files: usr/lib/lordos/assets/monitor/geoip-country.mmdb.xz
Copyright: db-ip.com
License: CC-BY-4.0
 DB-IP Lite Country (https://db-ip.com), convertida por tools/gen-monitor-data.py.
 https://creativecommons.org/licenses/by/4.0/

Files: usr/lib/lordos/assets/monitor/land.bin usr/lib/lordos/assets/locales/*/countries.json
License: public-domain
 Natural Earth (https://www.naturalearthdata.com).

Files: third_party/quickjs/* third_party/stb/*
License: MIT and public-domain
 QuickJS (MIT) y stb (dominio público / MIT), compilados dentro de lord-server.
C
printf 'lordos (%s) trixie; urgency=medium\n\n  * Paquete armado por tools/iso/mkdeb.sh.\n\n -- LordOS <lord.basex@gmail.com>  %s\n' \
    "$VERSION" "$(date -R)" | gzip -9n > "$D/changelog.gz"
chmod 644 "$D/changelog.gz"

# con el camino por GPU (make GPU=1), las librerías de Mesa también son dependencias
GPUDEPS=""
# readelf (no ldd): también funciona al armar para otra arquitectura (tools/rpi)
if readelf -d "$L/bin/lord-server" 2>/dev/null | grep -q "NEEDED.*libgbm"; then GPUDEPS=", libgbm1, libegl1, libgles2"; fi
DOCDEPS=""
if [ -x "$L/bin/lord-doc" ]; then DOCDEPS=", libpoppler-glib8t64, libarchive13t64, libcairo2, libfreetype6, libfontconfig1, poppler-data"; fi
MEDIADEPS=""
if [ -x "$L/bin/lord-media" ]; then MEDIADEPS=", libavcodec61, libavformat61, libavutil59, libswscale8, libswresample5, libva2, libva-drm2"; fi
SIZE=$(du -sk "$PKG/usr" | awk '{s += $1} END {print s}')
cat > "$PKG/DEBIAN/control" <<C
Package: lordos
Version: $VERSION
Architecture: ${DEB_ARCH:-$(dpkg --print-architecture)}
Maintainer: LordOS <lord.basex@gmail.com>
Installed-Size: $SIZE
Depends: libc6 (>= 2.38), libwayland-server0 (>= 1.22), libwayland-client0 (>= 1.22), libxkbcommon0 (>= 1.5), libcrypt1, libssl3t64 | libssl3, passwd, procps, xkb-data$GPUDEPS$DOCDEPS$MEDIADEPS
Recommends: pipewire-pulse, wireplumber, wpasupplicant, openvpn, fonts-noto-core, locales, tzdata, sudo, cups, cups-client, avahi-daemon, mesa-va-drivers
Section: x11
Priority: optional
Homepage: https://github.com/lordbasex/LordOS
Description: entorno de escritorio LordOS (servidor gráfico propio)
 LordOS es un entorno de escritorio con servidor gráfico propio en C (DRM/KMS,
 evdev, compositor Wayland) y la interfaz en React sobre un motor propio.
 Incluye las apps nativas, los temas, la pantalla de ingreso y el servicio
 lordos.service (lord-sessiond) que lo arranca en tty1. El escritorio remoto
 (navegador, puerto 5673) viene apagado.
C
# lordos.service se habilita a mano (el ISO lo hace; en un sistema instalado lo decide el usuario)
cat > "$PKG/DEBIAN/postinst" <<'C'
#!/bin/sh
set -e
if [ "$1" = configure ] && command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload >/dev/null 2>&1 || true
fi
C
chmod 755 "$PKG/DEBIAN/postinst"
dpkg-deb --root-owner-group --build "$PKG" "$OUT" >/dev/null
echo "$OUT ($(du -h "$OUT" | cut -f1))"

#!/bin/sh
# build.sh — arma el ISO live de LordOS (Debian 13 + LordOS), híbrido para USB y CD,
# con arranque BIOS y UEFI. Ver docs/ISO.md.
#
# Uso (como root, en un Debian 13, desde la raíz del repositorio ya compilado):
#   make -C server -j"$(nproc)"          # lord-server y lord-sessiond (necesita libssl-dev; clang para el BPF de lord-netperm)
#   (los temas: npm run build en la máquina de desarrollo; los .bundle.js viajan con el árbol)
#   sudo tools/iso/build.sh [carpeta de trabajo]   # por defecto /var/tmp/lordos-iso
#
# Resultado: <trabajo>/out/lordos-live-<versión>-amd64.iso y lordos_<versión>_amd64.deb
set -eu

[ "$(id -u)" = 0 ] || { echo "build.sh: correr como root (sudo)" >&2; exit 1; }
ROOT=$(pwd)
[ -f "$ROOT/package.json" ] && [ -d "$ROOT/server" ] || { echo "build.sh: correr desde la raíz del repositorio" >&2; exit 1; }
VERSION=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' package.json | head -1)
WORK=${1:-/var/tmp/lordos-iso}
MIRROR=${MIRROR:-http://deb.debian.org/debian}
SUITE=trixie
CH="$WORK/chroot"
IMG="$WORK/image"
OUT="$WORK/out"
ISO="$OUT/lordos-live-$VERSION-amd64.iso"
DEB="$OUT/lordos_${VERSION}_amd64.deb"
T0=$(date +%s)

for t in debootstrap mksquashfs xorriso grub-mkrescue mformat dpkg-deb; do
    command -v "$t" >/dev/null || { echo "build.sh: falta $t (ver docs/ISO.md, paquetes de build)" >&2; exit 1; }
done

# Paquetes del sistema live. LordOS no usa X11, GNOME ni KDE: solo lo de abajo.
PKGS_BASE="linux-image-amd64 initramfs-tools live-boot live-boot-initramfs-tools systemd-sysv udev kmod dbus dbus-user-session sudo locales tzdata
 iproute2 iputils-ping less nano ca-certificates apt dpkg passwd procps"
PKGS_LORDOS="libwayland-server0 libwayland-client0 libxkbcommon0 xkb-data pipewire pipewire-pulse pipewire-alsa wireplumber rtkit
 alsa-ucm-conf wpasupplicant openvpn fonts-noto-core fonts-noto-cjk fonts-noto-color-emoji libthai-data
 skkdic rime-data-pinyin-simp fonts-dejavu-core
 libpoppler-glib8t64 libarchive13t64 poppler-data cups avahi-daemon
 libavcodec61 libavformat61 libavutil59 libswscale8 libswresample5 libva2 libva-drm2 mesa-va-drivers"
PKGS_FIRMWARE="firmware-amd-graphics firmware-misc-nonfree firmware-iwlwifi firmware-realtek firmware-atheros
 firmware-sof-signed"
PKGS_GPU="libgl1-mesa-dri libegl1 libgles2 libgbm1 mesa-vulkan-drivers mesa-utils vulkan-tools glmark2-es2-wayland"
# navegadores preinstalados (apps Linux; usan el marco de LordOS por defecto, ver docs/WAYLAND.md)
PKGS_APPS="foot featherpad qt6-wayland chromium firefox-esr"
# el instalador al disco (server/src/install): particionar, formatear, copiar y grub para UEFI y BIOS
PKGS_INSTALLER="gdisk dosfstools e2fsprogs squashfs-tools util-linux grub-common grub2-common grub-efi-amd64-bin grub-pc-bin"

echo "== LordOS $VERSION: ISO live en $WORK"
umount_all() {
    for m in dev/pts dev proc sys run; do mountpoint -q "$CH/$m" 2>/dev/null && umount -l "$CH/$m" || true; done
}
trap umount_all EXIT
umount_all
rm -rf "$CH" "$IMG"
mkdir -p "$CH" "$IMG/live" "$IMG/boot/grub" "$OUT"

# 1. el paquete de LordOS
sh tools/iso/mkdeb.sh "$DEB"

# 2. sistema base
echo "== debootstrap $SUITE (minbase)"
debootstrap --variant=minbase --components=main,contrib,non-free,non-free-firmware \
    "$SUITE" "$CH" "$MIRROR" > "$WORK/debootstrap.log" 2>&1 || { tail -20 "$WORK/debootstrap.log"; exit 1; }

cat > "$CH/etc/apt/sources.list" <<EOF
deb $MIRROR $SUITE main contrib non-free non-free-firmware
deb $MIRROR $SUITE-updates main contrib non-free non-free-firmware
deb http://security.debian.org/debian-security $SUITE-security main contrib non-free non-free-firmware
EOF
echo lordos-live > "$CH/etc/hostname"
# la red la maneja LordOS (DHCP propio): sin ifupdown ni interfaces del sistema (live-boot escribe
# la suya al arrancar; lordos-live-prep la vuelve a dejar solo con lo)
mkdir -p "$CH/etc/network"; printf 'auto lo\niface lo inet loopback\n' > "$CH/etc/network/interfaces"
printf '127.0.0.1\tlocalhost\n127.0.1.1\tlordos-live\n::1\tlocalhost ip6-localhost ip6-loopback\n' > "$CH/etc/hosts"
# sin systemd-resolved: la red de LordOS escribe /etc/resolv.conf (un archivo común, no un enlace)
rm -f "$CH/etc/resolv.conf"; cp /etc/resolv.conf "$CH/etc/resolv.conf" 2>/dev/null || : > "$CH/etc/resolv.conf"
# que dpkg no arranque servicios dentro del chroot
printf '#!/bin/sh\nexit 101\n' > "$CH/usr/sbin/policy-rc.d"; chmod 755 "$CH/usr/sbin/policy-rc.d"

mount --bind /dev "$CH/dev"; mount -t devpts devpts "$CH/dev/pts"
mount -t proc proc "$CH/proc"; mount -t sysfs sysfs "$CH/sys"; mount -t tmpfs tmpfs "$CH/run"
in_ch() { chroot "$CH" env DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 PATH=/usr/sbin:/usr/bin:/sbin:/bin "$@"; }

echo "== paquetes"
in_ch apt-get update -q > "$WORK/apt.log" 2>&1
# shellcheck disable=SC2086
in_ch apt-get install -y -q --no-install-recommends $PKGS_BASE $PKGS_LORDOS $PKGS_FIRMWARE $PKGS_GPU $PKGS_APPS $PKGS_INSTALLER \
    >> "$WORK/apt.log" 2>&1 || { tail -30 "$WORK/apt.log"; exit 1; }
cp "$DEB" "$CH/tmp/lordos.deb"
in_ch apt-get install -y -q --no-install-recommends /tmp/lordos.deb >> "$WORK/apt.log" 2>&1 || { tail -30 "$WORK/apt.log"; exit 1; }
# Chromium: perfiles nuevos con el marco del sistema (el de LordOS) en vez de su barra propia
if [ -f "$CH/etc/chromium/master_preferences" ]; then
python3 - "$CH/etc/chromium/master_preferences" <<'PY'
import json, sys
p = sys.argv[1]; d = json.load(open(p))
d.setdefault("browser", {})["custom_chrome_frame"] = False
json.dump(d, open(p, "w"), indent=2)
PY
fi
# Firefox ESR: igual (el usuario lo cambia en Personalizar barra de herramientas > Barra de título)
install -d "$CH/etc/firefox-esr"
printf '%s\n' '// LordOS: las ventanas de Firefox usan el marco del sistema (el del tema); se cambia en' \
  '// Personalizar barra de herramientas > Barra de título, o en about:config.' \
  'pref("browser.tabs.inTitlebar", 0);' > "$CH/etc/firefox-esr/lordos.js"
rm -f "$CH/tmp/lordos.deb"

# 3. configuración
echo "== configuración"
cat > "$CH/etc/locale.gen" <<'EOF'
es_ES.UTF-8 UTF-8
es_AR.UTF-8 UTF-8
es_MX.UTF-8 UTF-8
en_US.UTF-8 UTF-8
pt_BR.UTF-8 UTF-8
EOF
in_ch locale-gen > /dev/null
echo 'LANG=es_ES.UTF-8' > "$CH/etc/default/locale"
ln -sf /usr/share/zoneinfo/UTC "$CH/etc/localtime"; echo UTC > "$CH/etc/timezone"

# ping sin root (en el sistema live no sobrevive la capacidad cap_net_raw del ejecutable)
echo 'net.ipv4.ping_group_range = 0 2147483647' > "$CH/etc/sysctl.d/50-lordos-ping.conf"

# usuario de la sesión live: sin contraseña, sudo sin contraseña. Entra solo (inicio automático de
# lord-sessiond); al bloquear la pantalla se vuelve con la contraseña vacía (solo en el monitor y
# solo para el usuario de inicio automático; ver docs/SESIONES.md)
in_ch useradd -m -s /bin/bash -c "LordOS" -G sudo,audio,video,input,render,lpadmin,pipewire lordos
in_ch passwd -d lordos > /dev/null
mkdir -p "$CH/etc/lordos"
printf '# ISO live: entra solo con el usuario lordos. El sistema instalado usa la pantalla de ingreso.\nAUTOLOGIN=lordos\n' > "$CH/etc/lordos/session.conf"
# el usuario de sistema de la pantalla de ingreso (lord-sessiond también lo crea si falta)
in_ch useradd --system --home-dir /var/lib/lordos-greeter --create-home --shell /usr/sbin/nologin --comment LordOS lordos-greeter
install -m 440 tools/iso/rootfs/etc/sudoers.d/lordos-live "$CH/etc/sudoers.d/lordos-live"
mkdir -p "$CH/var/lib/systemd/linger"; : > "$CH/var/lib/systemd/linger/lordos"
# PipeWire de usuario (los paquetes ya habilitan los units globales; por las dudas)
in_ch systemctl --global enable pipewire.socket pipewire-pulse.socket wireplumber.service > /dev/null 2>&1 || true
# LordOS en tty1 en lugar del login de texto; las otras consolas quedan (Ctrl+Alt+F2)
in_ch systemctl enable lordos.service > /dev/null
in_ch systemctl enable lord-netperm.service > /dev/null
in_ch systemctl mask getty@tty1.service > /dev/null
# arranque silencioso y más rápido
in_ch systemctl mask systemd-networkd-wait-online.service > /dev/null 2>&1 || true
printf 'root:lordos\n' | in_ch chpasswd     # consola de emergencia: root / lordos (documentado)

rm -f "$CH/usr/sbin/policy-rc.d"
in_ch apt-get clean
rm -rf "$CH/var/lib/apt/lists/"* "$CH/var/cache/apt/"*.bin "$CH/tmp/"* "$CH/var/log/"*.log
: > "$CH/etc/resolv.conf"
: > "$CH/etc/machine-id"     # se genera en cada arranque
in_ch update-initramfs -u > /dev/null 2>&1
umount_all

# 4. kernel, initramfs y squashfs
echo "== squashfs"
KVER=$(ls "$CH/boot" | sed -n 's/^vmlinuz-//p' | sort -V | tail -1)
cp "$CH/boot/vmlinuz-$KVER" "$IMG/live/vmlinuz"
cp "$CH/boot/initrd.img-$KVER" "$IMG/live/initrd.img"
rm -f "$IMG/live/filesystem.squashfs"
mksquashfs "$CH" "$IMG/live/filesystem.squashfs" -comp xz -Xbcj x86 -b 1M -noappend -quiet > /dev/null

# 5. grub (BIOS + UEFI en el mismo ISO híbrido)
cat > "$IMG/boot/grub/grub.cfg" <<EOF
set default=0
set timeout=5
insmod all_video
insmod gfxterm
loadfont unicode
terminal_output gfxterm
menuentry "LordOS $VERSION (en vivo)" {
    linux /live/vmlinuz boot=live components quiet loglevel=3 splash lordos.lang=es
    initrd /live/initrd.img
}
menuentry "LordOS $VERSION (modo seguro: sin GPU)" {
    linux /live/vmlinuz boot=live components quiet loglevel=3 lordos.gpu=0 lordos.lang=es
    initrd /live/initrd.img
}
menuentry "LordOS $VERSION (English)" {
    linux /live/vmlinuz boot=live components quiet loglevel=3 splash lordos.lang=en
    initrd /live/initrd.img
}
menuentry "LordOS $VERSION (copiar a la RAM: se puede sacar el USB)" {
    linux /live/vmlinuz boot=live components quiet loglevel=3 splash toram lordos.lang=es
    initrd /live/initrd.img
}
EOF
echo "== ISO"
rm -f "$ISO"
grub-mkrescue -o "$ISO" "$IMG" -- -volid LORDOS_LIVE > "$WORK/grub-mkrescue.log" 2>&1 || { tail -20 "$WORK/grub-mkrescue.log"; exit 1; }
(cd "$OUT" && sha256sum "$(basename "$ISO")" > "$(basename "$ISO").sha256")

T1=$(date +%s)
echo "== listo en $(( (T1 - T0) / 60 )) min $(( (T1 - T0) % 60 )) s"
ls -l "$ISO" "$DEB"
echo "squashfs: $(du -h "$IMG/live/filesystem.squashfs" | cut -f1) · sistema descomprimido: $(du -sh --exclude=proc --exclude=sys "$CH" | cut -f1)"

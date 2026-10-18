#!/bin/sh
# build-img.sh — imagen de LordOS para Raspberry Pi 5 (arm64; Pi 4 en lo posible), para grabar en
# una tarjeta SD o un NVMe con Raspberry Pi Imager, dd o balenaEtcher. Ver docs/RASPBERRY.md.
#
# Uso (como root, en un Debian 13, desde la raíz del repositorio con los temas ya construidos):
#   sudo tools/rpi/build-img.sh [carpeta de trabajo]          # por defecto /var/tmp/lordos-rpi
#
# Resultado: <trabajo>/out/lordos-<versión>-rpi5-arm64.img.xz (+ .sha256) y lordos_<versión>_arm64.deb
#
# - En un host arm64 (la propia Pi 5, un servidor ARM) todo es nativo. Si server/build/lord-server ya
#   es aarch64 (make -C server en la Pi) se usa ese; si no, se compila en el chroot de build.
# - En un host x86-64 hace falta qemu-user-static (binfmt para aarch64): debootstrap y apt corren
#   emulados, y lord-server se compila en un chroot arm64 aparte (lento: ~15–30 min la primera vez).
#
# Variables: MIRROR (Debian), RPI_MIRROR (archivo de Raspberry Pi), IMG_EXTRA_MB (espacio libre en la
# imagen antes de agrandarse al arrancar, 512), RPI_SSH=1 (agrega openssh-server, para desarrollar),
# RPI_KEY_NOCHECK=1 (no verificar la huella de la clave del archivo de Raspberry Pi; no recomendado).
set -eu

[ "$(id -u)" = 0 ] || { echo "build-img.sh: correr como root (sudo)" >&2; exit 1; }
ROOT=$(pwd)
[ -f "$ROOT/package.json" ] && [ -d "$ROOT/server" ] && [ -f "$ROOT/tools/iso/mkdeb.sh" ] || {
    echo "build-img.sh: correr desde la raíz del repositorio" >&2; exit 1; }
VERSION=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' package.json | head -1)
WORK=${1:-/var/tmp/lordos-rpi}
MIRROR=${MIRROR:-http://deb.debian.org/debian}
RPI_MIRROR=${RPI_MIRROR:-http://archive.raspberrypi.com/debian}
# Clave del archivo de Raspberry Pi (raspberrypi.gpg.key). Verificarla contra
# https://www.raspberrypi.com/documentation/ antes del primer build (ver docs/RASPBERRY.md).
RPI_KEY_URL=${RPI_KEY_URL:-$RPI_MIRROR/raspberrypi.gpg.key}
RPI_KEY_FPR=${RPI_KEY_FPR:-CF8A1AF502A2AA2D763BAE7E82B129927FA3303E}
IMG_EXTRA_MB=${IMG_EXTRA_MB:-512}
SUITE=trixie
ARCH=arm64
CH="$WORK/rootfs"           # el sistema de la imagen
BR="$WORK/buildroot"        # chroot arm64 para compilar lord-server (si hace falta)
OUT="$WORK/out"
NAME="lordos-$VERSION-rpi5-arm64"
IMG="$OUT/$NAME.img"
DEB="$OUT/lordos_${VERSION}_arm64.deb"
RPI="$ROOT/tools/rpi/rootfs"
T0=$(date +%s)

for t in debootstrap dpkg-deb sfdisk losetup mkfs.vfat mkfs.ext4 xz curl gpg partx; do
    command -v "$t" >/dev/null || { echo "build-img.sh: falta $t (ver docs/RASPBERRY.md, paquetes de build)" >&2; exit 1; }
done
case "$(uname -m)" in
    aarch64|arm64) NATIVE=1 ;;
    *) NATIVE=0
       [ -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ] || {
           echo "build-img.sh: host $(uname -m) sin binfmt de aarch64: instalar qemu-user-static (y binfmt-support)" >&2; exit 1; } ;;
esac

# Paquetes. Los de LordOS y las apps de ejemplo son los mismos del ISO (tools/iso/build.sh); cambia
# el kernel y el firmware (Raspberry Pi) y no hay live-boot, grub ni el instalador al disco.
PKGS_BASE="initramfs-tools systemd-sysv udev kmod dbus dbus-user-session sudo locales tzdata
 iproute2 iputils-ping less nano ca-certificates apt dpkg passwd procps e2fsprogs fdisk dosfstools util-linux"
PKGS_LORDOS="libwayland-server0 libxkbcommon0 xkb-data pipewire pipewire-pulse pipewire-alsa wireplumber rtkit
 alsa-ucm-conf wpasupplicant openvpn fonts-noto-core fonts-noto-cjk fonts-noto-color-emoji libthai-data
 skkdic rime-data-pinyin-simp fonts-dejavu-core"
# Raspberry Pi: kernel de la Pi 5 (2712: páginas de 16K) y de la Pi 4/400/CM4 (v8: 4K), firmware de
# arranque (start*.elf no se usan en la Pi 5: su firmware está en la EEPROM) y Wi-Fi/BT (CYW43455)
PKGS_RPI="raspi-firmware linux-image-rpi-2712 linux-image-rpi-v8 firmware-brcm80211"
PKGS_RPI_EXTRA="rpi-eeprom raspi-utils"
# Mesa de Debian 13: v3d (OpenGL ES 3.1 en la Pi 5), v3dv (Vulkan 1.2/1.3) y kmsro (vc4 + v3d)
PKGS_GPU="libgl1-mesa-dri libegl1 libgles2 libgbm1 mesa-vulkan-drivers mesa-utils vulkan-tools glmark2-es2-wayland"
PKGS_APPS="foot featherpad qt6-wayland"
PKGS_DEV=""
[ "${RPI_SSH:-0}" = 1 ] && PKGS_DEV="openssh-server"
# para compilar lord-server (solo en el chroot de build, no en la imagen)
PKGS_BUILD="build-essential pkg-config libxkbcommon-dev libwayland-dev wayland-protocols libgbm-dev libegl-dev libgles-dev
 libssl-dev libcrypt-dev dpkg-dev file"

echo "== LordOS $VERSION: imagen para Raspberry Pi en $WORK (host $(uname -m))"
umount_tree() {
    for d in "$CH" "$BR"; do
        for m in dev/pts dev proc sys run; do mountpoint -q "$d/$m" 2>/dev/null && umount -l "$d/$m" || true; done
    done
    for m in "$WORK/mnt/boot/firmware" "$WORK/mnt"; do mountpoint -q "$m" 2>/dev/null && umount "$m" || true; done
    [ -n "${LOOP:-}" ] && losetup -d "$LOOP" 2>/dev/null || true
}
trap umount_tree EXIT
umount_tree
mkdir -p "$OUT" "$WORK"

mount_ch() {
    mount --bind /dev "$1/dev"; mount -t devpts devpts "$1/dev/pts"
    mount -t proc proc "$1/proc"; mount -t sysfs sysfs "$1/sys"; mount -t tmpfs tmpfs "$1/run"
}
umount_ch() { for m in dev/pts dev proc sys run; do mountpoint -q "$1/$m" && umount -l "$1/$m" || true; done; }
in_root() { r=$1; shift; chroot "$r" env DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 PATH=/usr/sbin:/usr/bin:/sbin:/bin "$@"; }

# debootstrap arm64 (nativo, o en dos etapas con qemu en x86)
bootstrap() {
    dir=$1; log=$2
    rm -rf "$dir"; mkdir -p "$dir"
    if [ "$NATIVE" = 1 ]; then
        debootstrap --arch=$ARCH --variant=minbase --components=main,contrib,non-free,non-free-firmware \
            "$SUITE" "$dir" "$MIRROR" > "$log" 2>&1 || { tail -20 "$log"; exit 1; }
    else
        debootstrap --arch=$ARCH --foreign --variant=minbase --components=main,contrib,non-free,non-free-firmware \
            "$SUITE" "$dir" "$MIRROR" > "$log" 2>&1 || { tail -20 "$log"; exit 1; }
        # con binfmt "F" (fix-binary, el de Debian) no hace falta copiar qemu; si está, por las dudas
        Q=$(command -v qemu-aarch64-static || true)
        [ -n "$Q" ] && cp "$Q" "$dir/usr/bin/"
        chroot "$dir" /debootstrap/debootstrap --second-stage >> "$log" 2>&1 || { tail -20 "$log"; exit 1; }
    fi
    cat > "$dir/etc/apt/sources.list" <<EOF
deb $MIRROR $SUITE main contrib non-free non-free-firmware
deb $MIRROR $SUITE-updates main contrib non-free non-free-firmware
deb http://security.debian.org/debian-security $SUITE-security main contrib non-free non-free-firmware
EOF
    printf '#!/bin/sh\nexit 101\n' > "$dir/usr/sbin/policy-rc.d"; chmod 755 "$dir/usr/sbin/policy-rc.d"
    cp /etc/resolv.conf "$dir/etc/resolv.conf" 2>/dev/null || true
}

is_aarch64() {      # ELF de aarch64 (e_machine = 0xb7)
    [ -f "$1" ] && [ "$(od -An -tx1 -j18 -N2 "$1" | tr -d ' \n')" = "b700" ]
}

# ---- 1. lord-server y lord-sessiond para arm64, y el paquete ----
[ -f themes/greeter/theme.bundle.js ] || { echo "build-img.sh: faltan los temas (npm run build en la máquina de desarrollo)" >&2; exit 1; }
if is_aarch64 server/build/lord-server && is_aarch64 server/build/lord-sessiond; then
    echo "== lord-server: se usa server/build (ya es aarch64)"
    DEB_ARCH=$ARCH sh tools/iso/mkdeb.sh "$DEB"
else
    echo "== lord-server: se compila en un chroot arm64 ($BR)"
    if [ ! -x "$BR/usr/bin/gcc" ]; then
        bootstrap "$BR" "$WORK/buildroot-debootstrap.log"
        mount_ch "$BR"
        in_root "$BR" apt-get update -q > "$WORK/buildroot-apt.log" 2>&1
        # shellcheck disable=SC2086
        in_root "$BR" apt-get install -y -q --no-install-recommends $PKGS_BUILD >> "$WORK/buildroot-apt.log" 2>&1 || {
            tail -30 "$WORK/buildroot-apt.log"; exit 1; }
    else
        mount_ch "$BR"
    fi
    rm -rf "$BR/src/LordOS"; mkdir -p "$BR/src/LordOS"
    tar -C "$ROOT" --exclude=server/build --exclude=node_modules -cf - \
        package.json README.md server third_party assets themes tools | tar -C "$BR/src/LordOS" -xf -
    J=$(nproc)
    in_root "$BR" sh -c "cd /src/LordOS && make -C server -j$J GPU=1 && make -C server build/canvas-test" > "$WORK/build.log" 2>&1 || {
        tail -40 "$WORK/build.log"; exit 1; }
    is_aarch64 "$BR/src/LordOS/server/build/lord-server" || { echo "build-img.sh: el binario no es aarch64" >&2; exit 1; }
    # canvas-test: NEON contra C, bit a bit (bajo qemu no mide nada, pero comprueba)
    in_root "$BR" /src/LordOS/server/build/canvas-test > "$WORK/canvas-test.log" 2>&1 || {
        cat "$WORK/canvas-test.log"; echo "build-img.sh: canvas-test falló (NEON ≠ C); compilar con NEON=0" >&2; exit 1; }
    in_root "$BR" sh -c "cd /src/LordOS && DEB_ARCH=$ARCH sh tools/iso/mkdeb.sh /tmp/lordos.deb" >> "$WORK/build.log" 2>&1 || {
        tail -20 "$WORK/build.log"; exit 1; }
    cp "$BR/tmp/lordos.deb" "$DEB"
    umount_ch "$BR"
fi
# mkdeb.sh (el mismo del ISO) toma la arquitectura de DEB_ARCH
echo "   $(basename "$DEB") ($(du -h "$DEB" | cut -f1))"

# ---- 2. el sistema de la imagen ----
echo "== debootstrap $SUITE $ARCH (minbase)"
bootstrap "$CH" "$WORK/debootstrap.log"

# archivo de Raspberry Pi: solo kernel, firmware y utilidades (preferences.d/raspberrypi)
echo "== clave del archivo de Raspberry Pi"
KEY="$WORK/raspberrypi.asc"
curl -fsSL "$RPI_KEY_URL" -o "$KEY"
FPR=$(gpg --show-keys --with-colons "$KEY" 2>/dev/null | awk -F: '$1 == "fpr" { print $10; exit }')
if [ "$FPR" != "$RPI_KEY_FPR" ] && [ "${RPI_KEY_NOCHECK:-0}" != 1 ]; then
    echo "build-img.sh: la clave de $RPI_KEY_URL tiene la huella $FPR (se esperaba $RPI_KEY_FPR)" >&2
    echo "  verificarla y pasar RPI_KEY_FPR=<huella> (ver docs/RASPBERRY.md)" >&2
    exit 1
fi
install -d "$CH/etc/apt/keyrings"
install -m 644 "$KEY" "$CH/etc/apt/keyrings/raspberrypi.asc"
echo "deb [signed-by=/etc/apt/keyrings/raspberrypi.asc] $RPI_MIRROR $SUITE main" > "$CH/etc/apt/sources.list.d/raspberrypi.list"
install -m 644 "$RPI/etc/apt/preferences.d/raspberrypi" "$CH/etc/apt/preferences.d/raspberrypi"

echo lordos-pi > "$CH/etc/hostname"
mkdir -p "$CH/etc/network"; printf 'auto lo\niface lo inet loopback\n' > "$CH/etc/network/interfaces"
printf '127.0.0.1\tlocalhost\n127.0.1.1\tlordos-pi\n::1\tlocalhost ip6-localhost ip6-loopback\n' > "$CH/etc/hosts"
# los hooks del kernel de Raspberry Pi escriben en /boot/firmware (acá, una carpeta común que después
# se copia a la partición FAT)
mkdir -p "$CH/boot/firmware"

mount_ch "$CH"
echo "== paquetes"
in_root "$CH" apt-get update -q > "$WORK/apt.log" 2>&1 || { tail -30 "$WORK/apt.log"; exit 1; }
# shellcheck disable=SC2086
in_root "$CH" apt-get install -y -q --no-install-recommends $PKGS_BASE $PKGS_LORDOS $PKGS_RPI $PKGS_GPU $PKGS_APPS $PKGS_DEV \
    >> "$WORK/apt.log" 2>&1 || { tail -30 "$WORK/apt.log"; exit 1; }
# shellcheck disable=SC2086
in_root "$CH" apt-get install -y -q --no-install-recommends $PKGS_RPI_EXTRA >> "$WORK/apt.log" 2>&1 ||
    echo "   aviso: no se instalaron $PKGS_RPI_EXTRA (ver $WORK/apt.log)"
cp "$DEB" "$CH/tmp/lordos.deb"
in_root "$CH" apt-get install -y -q --no-install-recommends /tmp/lordos.deb >> "$WORK/apt.log" 2>&1 || { tail -30 "$WORK/apt.log"; exit 1; }
rm -f "$CH/tmp/lordos.deb"

# ---- 3. configuración (lo mismo que el ISO, menos lo del live) ----
echo "== configuración"
cat > "$CH/etc/locale.gen" <<'EOF'
es_ES.UTF-8 UTF-8
es_AR.UTF-8 UTF-8
es_MX.UTF-8 UTF-8
en_US.UTF-8 UTF-8
pt_BR.UTF-8 UTF-8
EOF
in_root "$CH" locale-gen > /dev/null
echo 'LANG=es_ES.UTF-8' > "$CH/etc/default/locale"
ln -sf /usr/share/zoneinfo/UTC "$CH/etc/localtime"; echo UTC > "$CH/etc/timezone"
echo 'net.ipv4.ping_group_range = 0 2147483647' > "$CH/etc/sysctl.d/50-lordos-ping.conf"

# usuario de configuración (modo de configuración del primer arranque, ver rpi/firstboot): igual al
# usuario del live. Si en el primer arranque hay lordos-user.txt/userconf.txt, se borra enseguida.
in_root "$CH" useradd -m -s /bin/bash -c "LordOS" -G sudo,audio,video,input,render lordos
in_root "$CH" passwd -d lordos > /dev/null
printf '# LordOS en Raspberry Pi: solo durante el modo de configuración (lo borra rpi/firstboot)\nlordos ALL=(ALL) NOPASSWD: ALL\n' > "$CH/etc/sudoers.d/lordos-setup"
chmod 440 "$CH/etc/sudoers.d/lordos-setup"
mkdir -p "$CH/var/lib/systemd/linger"; : > "$CH/var/lib/systemd/linger/lordos"
mkdir -p "$CH/etc/lordos"
# hasta el primer arranque no entra nadie solo; rpi/firstboot decide (pantalla de ingreso o lordos)
printf '# Raspberry Pi: lo ajusta /usr/lib/lordos/rpi/firstboot en el primer arranque.\nAUTOLOGIN=\n' > "$CH/etc/lordos/session.conf"
in_root "$CH" useradd --system --home-dir /var/lib/lordos-greeter --create-home --shell /usr/sbin/nologin --comment LordOS lordos-greeter
in_root "$CH" systemctl --global enable pipewire.socket pipewire-pulse.socket wireplumber.service > /dev/null 2>&1 || true

# primer arranque y espera de vc4
install -d "$CH/usr/lib/lordos/rpi" "$CH/etc/systemd/system/lordos.service.d"
install -m 755 "$RPI/usr/lib/lordos/rpi/firstboot" "$RPI/usr/lib/lordos/rpi/wait-kms" "$CH/usr/lib/lordos/rpi/"
install -m 644 "$RPI/etc/systemd/system/lordos-rpi-firstboot.service" "$CH/etc/systemd/system/"
install -m 644 "$RPI/etc/systemd/system/lordos.service.d/rpi.conf" "$CH/etc/systemd/system/lordos.service.d/"
mkdir -p "$CH/var/lib/lordos/rpi"
: > "$CH/var/lib/lordos/rpi/resize-pending"
: > "$CH/var/lib/lordos/rpi/firstboot-pending"
in_root "$CH" systemctl enable lordos-rpi-firstboot.service lordos.service > /dev/null
in_root "$CH" systemctl mask getty@tty1.service > /dev/null
in_root "$CH" systemctl mask systemd-networkd-wait-online.service > /dev/null 2>&1 || true
# sin contraseña de root: la consola de emergencia queda cerrada (como el sistema instalado)
in_root "$CH" usermod -p '*' root

# el kernel de la Pi en /boot/firmware: lo hacen los hooks de raspi-firmware; si no, a mano
KV2712=$(ls "$CH/boot" | sed -n 's/^vmlinuz-\(.*-rpi-2712\)$/\1/p' | sort -V | tail -1)
KV8=$(ls "$CH/boot" | sed -n 's/^vmlinuz-\(.*-rpi-v8\)$/\1/p' | sort -V | tail -1)
[ -n "$KV2712" ] || { echo "build-img.sh: no se instaló el kernel rpi-2712" >&2; exit 1; }
in_root "$CH" update-initramfs -u -k all > /dev/null 2>&1 || true
place_kernel() {     # versión, nombre del kernel en la FAT, nombre del initramfs
    [ -n "$1" ] || return 0
    [ -f "$CH/boot/firmware/$2" ] || cp "$CH/boot/vmlinuz-$1" "$CH/boot/firmware/$2"
    [ -f "$CH/boot/initrd.img-$1" ] && cp "$CH/boot/initrd.img-$1" "$CH/boot/firmware/$3"
    if ! ls "$CH/boot/firmware/"bcm2712*.dtb > /dev/null 2>&1; then
        D=$(ls -d "$CH/usr/lib/linux-image-$1" 2>/dev/null || true)
        [ -n "$D" ] && cp "$D"/broadcom/bcm27*.dtb "$CH/boot/firmware/" && cp -r "$D/overlays" "$CH/boot/firmware/"
    fi
}
place_kernel "$KV2712" kernel_2712.img initramfs_2712
place_kernel "$KV8" kernel8.img initramfs8
install -m 644 "$RPI/boot/firmware/config.txt" "$CH/boot/firmware/config.txt"
install -m 644 "$RPI/boot/firmware/lordos-user.txt.example" "$CH/boot/firmware/lordos-user.txt.example"

rm -f "$CH/usr/sbin/policy-rc.d" "$CH/usr/bin/qemu-aarch64-static"
in_root "$CH" apt-get clean
rm -rf "$CH/var/lib/apt/lists/"* "$CH/var/cache/apt/"*.bin "$CH/tmp/"* "$CH/var/log/"*.log
rm -f "$CH/etc/ssh/ssh_host_"*      # se generan en el primer arranque (si hay openssh-server)
[ "${RPI_SSH:-0}" = 1 ] && in_root "$CH" systemctl enable ssh > /dev/null 2>&1 || true
: > "$CH/etc/resolv.conf"
: > "$CH/etc/machine-id"
umount_ch "$CH"

# ---- 4. la imagen: MBR, FAT32 de 512 MiB (bootfs) + ext4 (rootfs) ----
echo "== imagen"
ROOT_MB=$(( $(du -sm "$CH" | cut -f1) - $(du -sm "$CH/boot/firmware" | cut -f1) ))
BOOT_MB=512
SIZE_MB=$((4 + BOOT_MB + ROOT_MB * 115 / 100 + IMG_EXTRA_MB))
rm -f "$IMG" "$IMG.xz"
truncate -s "${SIZE_MB}M" "$IMG"
DISKID=$(od -An -tx4 -N4 /dev/urandom | tr -d ' \n')
sfdisk --quiet "$IMG" <<EOF
label: dos
label-id: 0x$DISKID
unit: sectors
start=8192, size=$((BOOT_MB * 2048)), type=c, bootable
start=$((8192 + BOOT_MB * 2048)), type=83
EOF
LOOP=$(losetup --show -f -P "$IMG")
mkfs.vfat -F 32 -n bootfs "${LOOP}p1" > /dev/null
mkfs.ext4 -q -L rootfs -O ^orphan_file "${LOOP}p2"     # sin orphan_file: así lo leen también e2fsprogs viejos
mkdir -p "$WORK/mnt"
mount "${LOOP}p2" "$WORK/mnt"
tar -C "$CH" --exclude=./boot/firmware -cf - . | tar -C "$WORK/mnt" -xpf -
mkdir -p "$WORK/mnt/boot/firmware"
mount "${LOOP}p1" "$WORK/mnt/boot/firmware"
cp -r "$CH/boot/firmware/." "$WORK/mnt/boot/firmware/"
PARTUUID_BOOT="$DISKID-01"
PARTUUID_ROOT="$DISKID-02"
cat > "$WORK/mnt/etc/fstab" <<EOF
# LordOS en Raspberry Pi (tools/rpi/build-img.sh)
PARTUUID=$PARTUUID_ROOT  /              ext4  defaults,noatime  0 1
PARTUUID=$PARTUUID_BOOT  /boot/firmware vfat  defaults,umask=0077  0 2
EOF
# lordos.gpu=0 (modo seguro, todo por CPU) y lordos.lang=en|pt se pueden agregar acá, como en el ISO.
# lsm=: Landlock para el aislamiento de apps (server/src/sec); los que el kernel no tenga se ignoran.
printf 'console=serial0,115200 console=tty3 root=PARTUUID=%s rootfstype=ext4 fsck.repair=yes rootwait quiet loglevel=3 cgroup_enable=memory lsm=landlock,lockdown,yama,integrity,apparmor,bpf lordos.lang=es\n' \
    "$PARTUUID_ROOT" > "$WORK/mnt/boot/firmware/cmdline.txt"
sync
umount "$WORK/mnt/boot/firmware"
umount "$WORK/mnt"
losetup -d "$LOOP"; LOOP=""

echo "== xz"
xz -T0 -6 -f "$IMG"
(cd "$OUT" && sha256sum "$(basename "$IMG").xz" > "$(basename "$IMG").xz.sha256")

T1=$(date +%s)
echo "== listo en $(( (T1 - T0) / 60 )) min $(( (T1 - T0) % 60 )) s"
ls -l "$IMG.xz" "$DEB"
echo "imagen: ${SIZE_MB} MB sin comprimir (se agranda sola al arrancar) · sistema: ${ROOT_MB} MB"

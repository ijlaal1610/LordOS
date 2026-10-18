# ISO live de LordOS

Una imagen para arrancar LordOS desde un pendrive o un CD en cualquier PC de 64 bits, sin
instalar nada. Es Debian 13 mínimo con LordOS adentro y entra directo al escritorio. Sirve para
probar el hardware real: la GPU (AMD o Nvidia), el sonido, el Wi-Fi y las pantallas. Es el
primer paso de M8 (la imagen instalable).

| | |
|---|---|
| ![bios](iso-bios-arranque.png) | ![uefi](iso-uefi-arranque.png) |
| VM con SeaBIOS: arranca sola hasta LordOS | VM con OVMF (UEFI) |
| ![grub](iso-grub.png) | ![terminal](iso-terminal.png) |
| Menú de arranque (captura anterior al arreglo de la fuente de grub: los bordes salían como «?») | Red por DHCP propio de LordOS, DNS, `ping` sin root, sonido (PipeWire) y arranque en 11,8 s |

## Grabarlo en un USB

El ISO es **híbrido**: se graba tal cual, sin herramientas especiales. Ojo: se borra todo el
pendrive.

- **Linux o macOS:**
  ```sh
  sha256sum -c lordos-live-0.7.0-amd64.iso.sha256      # macOS: shasum -a 256 -c …
  sudo dd if=lordos-live-0.7.0-amd64.iso of=/dev/sdX bs=4M status=progress conv=fsync
  ```
  `/dev/sdX` es el pendrive entero (no una partición). En macOS es `/dev/rdiskN`: buscalo con
  `diskutil list` y desmontalo antes con `diskutil unmountDisk /dev/diskN`.
- **Windows:** Rufus en modo **DD** (cuando pregunta "ISO o DD", elegir DD) o balenaEtcher.
- **Ventoy** también funciona: se copia el `.iso` al pendrive.

Después se arranca la PC desde el USB: suele ser F12, F11, F8 o Esc al prender, según la
placa. Anda con **UEFI** y con **BIOS/Legacy**. **Secure Boot** tiene que estar apagado (ver
"Pendiente").

## Menú de arranque

| Entrada | Para qué |
|---|---|
| LordOS (en vivo) | normal, en español |
| LordOS (modo seguro: sin GPU) | pasa `lordos.gpu=0` → `LORD_GPU=0`: lord-server no usa el camino de GPU y compone por CPU |
| LordOS (English) | la interfaz en inglés la primera vez |
| LordOS (copiar a la RAM) | `toram`: copia el sistema a la memoria (hacen falta ~1 GB libres) y se puede sacar el pendrive |

En la sesión live:
- **Usuario:** `lordos`, sin contraseña, con `sudo` sin contraseña.
- **root:** contraseña `lordos`, solo para la consola de emergencia.
- **Cambios:** todo lo que se cambia queda en la RAM y se pierde al apagar. La Tienda y la
  instalación de paquetes andan igual, pero en memoria.

## Instalar al disco

En el ISO live se abre solo el asistente **Instalar LordOS** (`apps/installer`). Solo existe
en el live: el manifest tiene `"live": true` y el SDK lo oculta si no está
`/run/live/medium/live/filesystem.squashfs`.

| | |
|---|---|
| ![bienvenida](iso-instalador-bienvenida.png) | ![disco](iso-instalador-disco.png) |
| Bienvenida | Disco: modelo, tamaño, contenido y confirmación fuerte |
| ![usuario](iso-instalador-usuario.png) | ![progreso](iso-instalador-progreso.png) |
| Usuario, contraseña y nombre del equipo, con validaciones | Instalando: barra por fases |
| ![instalado](iso-instalador-instalado.png) | ![terminal](iso-instalador-terminal.png) |
| Arrancado desde el disco (UEFI), con el usuario creado | Grupos, fstab con UUID, es_AR, zona horaria, sin live-boot, red; sudo pide contraseña |
| ![bios](iso-instalador-bios.png) | ![gpu](iso-live-gpu.png) |
| El mismo disco arrancando por SeaBIOS (BIOS + GPT) | El ISO trae el camino por GPU (`GPU=1`); en la VM (virtio sin 3D) cae a CPU solo |

Pasos del asistente:

1. **Bienvenida.**
2. **Idioma, región y zona horaria.** El idioma cambia la interfaz en vivo.
3. **Teclado.** La distribución se aplica en vivo y hay un campo para probarla.
4. **Disco.** Lista los discos (modelo, tamaño, conexión y lo que tienen adentro), sin el disco
   del que arrancó el live. Para seguir hay que marcar «Entiendo que se borra todo el disco» y
   escribir el nombre del disco (`sda`). Opcional: swap como archivo, de 2, 4 u 8 GB.
5. **Usuario.**
   - El nombre de usuario se sugiere a partir del nombre completo.
   - La contraseña se escribe dos veces.
   - El nombre del equipo se sugiere como `<usuario>-pc`.
   - Las mismas reglas se validan después, de nuevo, del lado root.
6. **Resumen** y «Instalar».
7. **Progreso**, en cuatro fases:
   - particionar;
   - copiar el sistema, con el porcentaje que informa `unsquashfs`;
   - configurar;
   - instalar el arranque.

   Al final aparece «Reiniciar».

Cómo queda el disco:

| Partición | Tamaño | Para qué |
|---|---|---|
| 1 `biosboot` (EF02) | 1 MiB | grub para BIOS en un disco GPT |
| 2 `EFI` (EF00) | 512 MiB, FAT32 | grub para UEFI, en la ruta removible (`EFI/BOOT/BOOTX64.EFI`: arranca en cualquier firmware sin entrada en la NVRAM) |
| 3 `LordOS` (8304) | el resto, ext4 | el sistema; la swap es `/swapfile` |

El sistema instalado es una copia del live con estos cambios:

- **Fuera lo del live:**
  - se quitan `live-boot` y el usuario `lordos`, con su sudo sin contraseña;
  - root queda bloqueado (`*`).
- **El usuario nuevo** está en los grupos `sudo`, `audio`, `video`, `input` y `render`, y
  tiene linger, así su PipeWire arranca con el sistema.
- **Configuración** que se escribe:
  - `fstab` con UUID;
  - hostname y `/etc/hosts`;
  - `/etc/default/locale`, `/etc/localtime` y `/etc/default/keyboard`;
  - los ajustes de LordOS del usuario (idioma y teclado);
  - un `machine-id` nuevo;
  - el initramfs regenerado y `grub.cfg`.
- **Inicio de sesión:**
  - `lordos.service` arranca `lord-sessiond`, que muestra la pantalla de ingreso propia
    (`SESIONES.md`): el instalador escribe `AUTOLOGIN=` vacío en `/etc/lordos/session.conf`;
  - el inicio automático se prende en Ajustes → Usuarios;
  - la contraseña sirve para `sudo` y para las acciones de administración.

### Seguridad del instalador

Agrega tres acciones al ayudante privilegiado (`server/src/install/install.c`, macro
`INSTALL_ADMIN_ACTIONS` en `admin.c`):

| Acción | Quién | Qué hace |
|---|---|---|
| `install-disks` | administrador | lista los discos instalables (JSON por `LOG`) |
| `install-run <json>` | administrador, en un proceso aparte | instala |
| `install-reboot` | administrador | `systemctl reboot` |

Las tres responden **ERR fuera del modo live**: tiene que existir
`/run/live/medium/live/filesystem.squashfs`. En un sistema instalado, ese archivo no existe.

Qué valida `install-run` del lado root, sin confiar en la interfaz:

- **El disco:**
  - tiene que estar en la misma lista de `install-disks` (de `/sys/block`, sin loop, ram,
    zram, sr, dm, md ni nbd, sin los de solo lectura y sin el disco que contiene
    `/run/live/medium`);
  - tiene que tener al menos 8 GB;
  - no puede tener nada montado ni swap activa.
- **Usuario:**
  - `^[a-z_][a-z0-9_-]{0,31}$`;
  - no puede ser un nombre reservado ni un usuario que ya exista en el sistema.
- **Nombre completo:** UTF-8, sin `:`, `,` ni caracteres de control.
- **Nombre del equipo:** `^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$`.
- **Región:** tiene que figurar en `/usr/share/i18n/SUPPORTED` con UTF-8.
- **Zona horaria:** un archivo regular de `/usr/share/zoneinfo`, sin `..`.
- **Teclado:** la distribución tiene que existir en `/usr/share/X11/xkb/symbols`; la variante,
  `[A-Za-z0-9_-]`.
- **Swap:** de 0 a 16384 MiB.
- **La contraseña:**
  - nunca va a un argv: llega por el socketpair del ayudante;
  - se hashea con `crypt(3)` yescrypt (`$y$`, como Debian, con `crypt_gensalt_ra`);
  - el hash se escribe directo en `/etc/shadow` del sistema nuevo;
  - después se borra de la memoria (`explicit_bzero`), y la interfaz también la olvida al
    empezar.
- **Programas de ruta fija:** `wipefs`, `sgdisk`, `udevadm`, `mkfs.vfat`, `mkfs.ext4`,
  `unsquashfs`, `blkid`, `fallocate`, `mkswap`, `chroot` (con `dpkg`, `userdel`, `useradd`,
  `locale-gen`, `systemd-machine-id-setup`, `update-initramfs`, `grub-install` y `grub-mkconfig`
  adentro) y `systemctl`. Los argumentos los arma `install.c` con los valores ya validados.

## Qué trae

- **LordOS** como paquete propio, `lordos_<versión>_amd64.deb`, que arma `tools/iso/mkdeb.sh`:
  - `/usr/lib/lordos/bin/lord-sessiond` (root: sesiones, pantalla de ingreso, escritorio remoto
    apagado, puerto 5673) y `/usr/lib/lordos/bin/lord-server` (cada sesión);
  - los temas ya construidos, en `/usr/lib/lordos/themes`;
  - `assets/`: fuentes, textos, íconos y la integración del shell;
  - `lordos.service`.
- **Arranque:** `lordos.service` arranca `lord-sessiond` en tty1 como root.
  - `/etc/lordos/session.conf` dice `AUTOLOGIN=lordos`: entra solo, como antes. El usuario no
    tiene contraseña; después de bloquear la pantalla se vuelve con el campo vacío (solo en el
    monitor).
  - Antes, `lordos-live-prep` (`ExecStartPre`, solo si existe `/run/live`) aplica
    `lordos.lang` (a `/etc/default/locale` y a los ajustes del usuario), crea sus carpetas y
    deja `/etc/network/interfaces` solo con `lo`.
  - `lordos.gpu=0` (modo seguro) sigue funcionando: lord-sessiond lo lee de `/proc/cmdline` y les
    pasa `LORD_GPU=0` a todas las sesiones. Sin esa opción, la sesión que está en el monitor
    compone por GPU como antes (Mesa: EGL, GBM, GLES2, linux-dmabuf y scanout directo).
  - El usuario tiene linger, así su PipeWire arranca con el sistema.
  - El usuario de sistema `lordos-greeter`, el de la pantalla de ingreso, se crea al armar el
    ISO (lord-sessiond también lo crea si falta).
- **Network:** LordOS's own DHCP, in `lord-netd` from boot (`RED.md`). No ifupdown,
  systemd-networkd or NetworkManager.
- **Sonido:** PipeWire, pipewire-pulse, WirePlumber, alsa-ucm-conf y el firmware SOF (audio de
  las notebooks Intel).
- **GPU:** Mesa (radeonsi, iris, nouveau, llvmpipe y RADV/ANV/NVK para Vulkan), `libgbm1`,
  `libegl1`, `libgles2`, `firmware-amd-graphics` y `firmware-misc-nonfree`. Para probarla:
  `glmark2-es2-wayland`, `eglinfo`/`glxinfo` (mesa-utils) y `vulkaninfo`.
- **Wi-Fi:** wpa_supplicant y firmware Intel (iwlwifi), Realtek y Atheros.
- **Fuentes:** Noto (core, CJK, emoji) y DejaVu; el resto de lo de M5b: libthai-data, skkdic y
  rime-data-pinyin-simp.
- **Apps Linux de ejemplo:** foot y FeatherPad.

## Armarlo

En un Debian 13, desde la raíz del repositorio ya compilado. Desde M8 se arma en la **VM 116**
(`lordos-gpu`, 172.17.0.52, clon de la 115), en `~/LordOS-iso`: la 115 es la VM de desarrollo
del usuario y no se usa para esto.

```sh
# en el Mac: los temas (también la pantalla de ingreso, themes/greeter)
npm run build
COPYFILE_DISABLE=1 tar czf - --no-xattrs --exclude build --exclude node_modules \
  package.json README.md server third_party assets themes tools | \
  ssh lord@172.17.0.52 'rm -rf ~/LordOS-iso && mkdir ~/LordOS-iso && cd ~/LordOS-iso && tar xzf -'
# en la VM 116
make -C server -j4                            # lord-server (GPU=1) y lord-sessiond (necesita libssl-dev)
sudo tools/iso/build.sh /var/tmp/lordos-iso   # → /var/tmp/lordos-iso/out/
```

El ISO se copia a Proxmox (`local:iso/lordos-live-<versión>-amd64.iso`). Antes de pisarlo, el
anterior se guarda con su fecha: `lordos-live-0.7.0-amd64-20261016-1529.iso` es el último con
`lordos-session` (sha256 `d4a8b25f…4b4dc5`).

Pasos de `build.sh`:
1. `mkdeb.sh`: el paquete de LordOS.
2. `debootstrap --variant=minbase` de trixie, con main, contrib, non-free y non-free-firmware.
3. `apt-get install --no-install-recommends` de la lista de arriba y del paquete de LordOS.
4. Configuración:
   - regiones `es_ES`, `es_AR`, `es_MX`, `en_US` y `pt_BR`;
   - el usuario `lordos`, con linger;
   - `lordos.service` habilitado y `getty@tty1` enmascarado;
   - `/etc/resolv.conf` como archivo común, para el DHCP de LordOS;
   - `/etc/network/interfaces` solo con `lo`;
   - `net.ipv4.ping_group_range`, para que `ping` funcione sin root.
5. Limpieza: listas de apt, cachés y `machine-id`.
6. `mksquashfs` (xz, BCJ x86).
7. `grub-mkrescue`: un solo ISO con El Torito para BIOS y una imagen EFI (`efi.img`) para UEFI,
   con el MBR híbrido para USB.

lord-server se compila con `make -C server GPU=1` (hacen falta `libgbm-dev libegl-dev libgles-dev`) para que el ISO traiga la composición por GPU (Mesa; ver `GPU.md`).

Medido en la VM 116 (4 núcleos Xeon Silver 4108, 4 GB): **~8 minutos**, casi todo en bajar e
instalar paquetes y comprimir.

| | |
|---|---|
| ISO | 651 MB (650 809 344 bytes; con lord-sessiond, la pantalla de ingreso, el instalador y el camino por GPU) |
| squashfs | 545 MB |
| sistema descomprimido | 1,5 GB |
| `lordos_0.7.0_amd64.deb` | 2,9 MB |

### Paquetes de build

```sh
sudo apt-get install -y --no-install-recommends debootstrap squashfs-tools xorriso grub-pc-bin \
  grub-efi-amd64-bin grub-common mtools dosfstools lintian libgbm-dev libegl-dev libgles-dev libssl-dev \
  libpoppler-glib-dev libarchive-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  libswresample-dev libva-dev clang
```

| Paquete | Para qué |
|---|---|
| `libssl-dev` | compilar `lord-sessiond` (TLS del escritorio remoto); sin él, `make` arma solo lord-server y `mkdeb.sh` se niega |
| `libpoppler-glib-dev`, `libarchive-dev` | compilar `lord-doc` (PDF y comprimidos); sin ellos `make` lo omite y el paquete sale sin visor de PDF |
| `libavcodec-dev`, `libavformat-dev`, `libavutil-dev`, `libswscale-dev`, `libswresample-dev` | compilar `lord-media` (video, fotos, cámara, grabadora); sin ellos `make` lo omite |
| `libva-dev` | opcional: decodificación por GPU (VA-API) en `lord-media` |
| `clang` | compilar los programas BPF de `lord-netperm` (permisos de red por app); sin él se arma igual, pero no aplica reglas |
| `debootstrap` | el sistema base de Debian |
| `squashfs-tools` | `mksquashfs` |
| `xorriso`, `grub-pc-bin`, `grub-efi-amd64-bin`, `grub-common`, `mtools`, `dosfstools` | `grub-mkrescue`: el ISO híbrido BIOS + UEFI |
| `lintian` | revisar el `.deb` |

### lintian

`lintian lordos_0.7.0_amd64.deb` no da errores. Quedan dos avisos, a propósito:

- **`duplicate-font-file`:** JetBrains Mono viene en `assets/fonts` porque la Terminal la
  necesita aunque no esté `fonts-jetbrains-mono`.
- **`uses-dpkg-database-directly`:** el motor de paquetes propio lee `/var/lib/dpkg/status`
  (ver `PAQUETES.md`); no escribe la base de dpkg.

## Prueba con lord-sessiond (M8, 16 de octubre)

El ISO arranca `lord-sessiond` en lugar de `lordos-session`. ISO probado:
`lordos-live-0.7.0-amd64.iso`, 650 817 536 bytes, sha256
`7cf1c1b1d97ee040e74dcf8d93a6996f1ae369b457f51aba23cdd2bdd64e898c`.

| | |
|---|---|
| ![grub](m8-iso-grub.png) | ![escritorio](m8-iso-escritorio.png) |
| UEFI (OVMF): menú de grub | Entra solo al escritorio, sin pasar por la pantalla de ingreso |
| ![terminal](m8-iso-terminal.png) | ![bloqueada](m8-iso-bloqueada.png) |
| `lordos`, `AUTOLOGIN=lordos`, `/sys/firmware/efi`, red por DHCP, el puerto 5673 cerrado | ❖L: pantalla bloqueada, con la IP abajo |
| ![desbloqueada](m8-iso-desbloqueada.png) | ![compartir](m8-iso-compartir.png) |
| Enter con el campo vacío: vuelve a la misma sesión | Ajustes → Compartir: el escritorio remoto apagado (no hay `/etc/lordos/remote/config`) |
| ![grub seguro](m8-iso-grub-modo-seguro.png) | ![modo seguro](m8-iso-modo-seguro.png) |
| Entrada «modo seguro» | `lordos.gpu=0` → la sesión recibe `LORD_GPU=0` y compone por CPU |
| ![english](m8-iso-english.png) | ![disco](m8-iso-instalador-disco.png) |
| `lordos.lang=en`: la interfaz en inglés | Instalador, en una VM temporal con un disco vacío de 24 GB (OVMF) |
| ![usuario](m8-iso-instalador-usuario.png) | ![resumen](m8-iso-instalador-resumen.png) |
| Usuario `ana` / «Ana Prueba», equipo `ana-pc` | Resumen, con el texto nuevo del inicio de sesión |
| ![progreso](m8-iso-instalador-progreso.png) | ![listo](m8-iso-instalador-listo.png) |
| Instalando (~1 minuto) | Listo; «Reiniciar» con un clic |
| ![greeter](m8-iso-instalado-greeter.png) | ![sesión](m8-iso-instalado-sesion.png) |
| El sistema instalado arranca en la pantalla de ingreso | Se entra con `ana` y su contraseña |
| ![terminal instalado](m8-iso-instalado-terminal.png) | |
| `AUTOLOGIN=` vacío; sin `live-boot` ni el usuario `lordos`; grupos sudo, audio, video, render e input; red; UEFI | |

**Arreglos encontrados en esta prueba:**

- **La red no se levantaba.** Con lord-sessiond, todas las sesiones son `--headless`, también
  la del monitor. `main.c` solo mandaba `net-restore` sin `--headless`, así que `ens18` quedaba
  DOWN.
  - Ahora toda sesión de usuario manda `net-restore`.
  - `net_act_restore` (`server/src/net/actions.c`) anota en `/run/lordos/net-owner` el pid del
    ayudante que levantó la red. Mientras ese ayudante viva, las otras sesiones no levantan otro
    DHCP en la misma placa.
- **El resumen del instalador decía** que el sistema entraba solo y que la pantalla de ingreso
  llegaba "en una versión próxima". Se cambió `installer.summary.login` en es, en y pt.

Las capturas del instalador hasta «Listo» y las del sistema instalado son del ISO anterior a
la corrección de ese texto (sha256 `daba6313…c8ec65`). Ese ISO tiene los mismos binarios; solo
cambian esos textos. Con el ISO final se repitieron el arranque por UEFI, la red, ❖L/Enter y el
resumen del instalador. En esa repetición no se instaló.

## Pruebas (VM 117 `lordos-live`, Proxmox)

La VM 117 tiene 4 GB de RAM, 4 núcleos, q35, `vga: virtio`, placa de sonido HDA y el ISO como
CD. También tiene un disco `scsi0` de 24 GB, que quedó de una prueba anterior del instalador y
tiene una instalación vieja. Arranca primero del CD.

- **SeaBIOS:** arranca sola hasta LordOS; `systemd-analyze` da 11,8 s (4,3 s de kernel y 7,5 s de
  userspace). Funcionan:
  - la Terminal;
  - PipeWire, que ve la placa HDA: `wpctl status` lista la salida y la entrada, y a LordOS
    como cliente;
  - Ajustes, Archivos y la Tienda;
  - la red, con el DHCP propio de LordOS: IP, ruta, DNS y salida a internet.
- **OVMF (UEFI)**, con el efidisk sin claves precargadas (Secure Boot apagado): arranca sola
  hasta LordOS (`/sys/firmware/efi` presente) en 12,5 s, con la red y el resto igual que con
  BIOS.
- **Instalación al disco** (24 GB, OVMF): el asistente completo en ~3 minutos; después arranca desde el disco por **UEFI y por SeaBIOS**, entra solo con el usuario creado, con red (DHCP), sonido (HDA), swap de 2 GB, es_AR, America/Argentina/Buenos_Aires y teclado latinoamericano; `sudo` pide la contraseña elegida; `live-boot` y el usuario `lordos` ya no están.
- **«Reiniciar» al terminar** (bug encontrado en la VM 120): después de instalar, el primer clic no hacía nada y el segundo dejaba la pantalla negra sin reiniciar. La causa era el `bind` recursivo de `/dev`, `/proc`, `/sys` y `/run` para el chroot: con systemd esos montajes son *shared*, así que al desmontar el destino también se desmontaba `/run/live/medium` del sistema en vivo. Desde ahí el live perdía archivos y la acción respondía «solo desde el sistema en vivo». Ahora los `bind` quedan como esclavos (`MS_SLAVE`). Además:
  - `install-reboot` lanza `systemctl --no-block reboot` desde un proceso aparte y no espera. Si a los 20 s nada lo terminó, hace `sync` + `reboot(2)`;
  - `lordos.service` tiene `TimeoutStopSec=5`;
  - el botón muestra «Reiniciando…».
  Medido en la VM 117 después del arreglo: un solo clic, el apagado termina y a los ~5 s ya está el firmware.
- **Errores encontrados y arreglados en el camino:**
  - **Red apagada:** `live-boot` escribe `/etc/network/interfaces` con la placa por DHCP.
    LordOS lo lee como "manejada por ifupdown" (aunque ifupdown no está) y no la tocaba, así
    que la red quedaba apagada. Ahora `lordos-live-prep` deja ese archivo solo con `lo` cuando no
    hay ifupdown. Probé `ip=frommedia`, pero el initramfs de Debian lo toma como el nombre de una
    placa y pierde unos segundos buscándola.
  - **Sin `ping` para el usuario:** la capacidad `cap_net_raw` del ejecutable no sobrevive en el
    live. Se agregó `ping_group_range`.
  - **Bordes del menú como «?»:** faltaba `loadfont unicode` en `grub.cfg`.
  - **PipeWire sin tiempo real:** faltaba `rtkit`.
  - **El servicio no se reiniciaba:** `systemctl restart lordos` esperaba a las terminales,
    que están pensadas para sobrevivir al servidor. Ahora usa `KillMode=process`.
  - **Faltaban símbolos:** algunos íconos de Ajustes (◐ ▣ ◧) salían en blanco. Ahora el ISO
    trae `fonts-dejavu-core`.

## Pendiente

- **Secure Boot:** firmar con shim y un MOK propio, o con el shim de Debian más un grub firmado.
  Hoy hay que apagar Secure Boot.
- **Cambiar de consola** (Ctrl+Alt+F2) mientras corre lord-server: es parte de la sesión propia
  de M8.
- **Instalador al disco** (M8) y persistencia opcional en el pendrive (`persistence` de live-boot).
- **Teclado:** el ISO arranca con la distribución que elige LordOS para el idioma (en español,
  latinoamericana) y se cambia en Ajustes → Teclado. Falta un selector en el primer arranque.
- **GPU real (AMD/Nvidia):** se prueba con este ISO en hardware real. El camino de GPU lo hace
  otro fork; el ISO ya trae Mesa y el firmware, y en la entrada «modo seguro» pasa `LORD_GPU=0`.
- **Robustez de la red:** `net/state.c` toma `/etc/network/interfaces` como señal de ifupdown
  aunque ifupdown no esté instalado. Convendría que también mire que exista `/sbin/ifup`. El ISO
  hoy lo esquiva en `lordos-live-prep`.
- Las herramientas de diagnóstico de audio (`pactl`) no vienen; `wpctl status` sí.
- ~~**Network before the first login**~~ and ~~**folders of the installed user**~~: fixed in M9.
  The network belongs to the system (`lord-netd`, started by `lord-sessiond` at boot; see `RED.md`)
  and the installer creates Escritorio, Documentos, Descargas, Música, Imágenes and Videos in the
  chosen language, with `~/.config/user-dirs.dirs` (`server/src/users/homedirs.c`, the same list as
  Settings → Users and `lordos-live-prep`). Still to repeat with a new ISO installed to disk.
- **Primer movimiento del mouse después de desbloquear:** en la VM 117, el primer evento
  absoluto por QMP después de ❖L/Enter dejó el cursor arriba (y≈8). Los siguientes funcionaron
  bien. No se investigó.

## Para VM-SETUP.md

Paso de M8 (ISO live), en la VM de desarrollo:

```sh
sudo apt-get install -y --no-install-recommends debootstrap squashfs-tools xorriso grub-pc-bin \
  grub-efi-amd64-bin grub-common mtools dosfstools lintian libssl-dev
```

En Proxmox, la VM 117 `lordos-live` sirve para probar el ISO. Tiene 4 GB, 4 núcleos,
q35, `vga: virtio`, `audio0: ich9-intel-hda`, OVMF con efidisk y el ISO en
`local:iso/lordos-live-<versión>-amd64.iso`. Para BIOS: `qm set 117 --bios seabios`.

# LordOS en Raspberry Pi (ARM64)

Desde M8, ARM64 es una plataforma oficial de LordOS, con la **Raspberry Pi 5** como placa de
referencia. Este documento junta lo que hace falta para armar la imagen, grabarla, desarrollar
en la placa y probarla.

> **Estado (2026-10-16):** preparado en el código y en las herramientas, **sin probar en una
> placa todavía**. La Pi 5 de 16 GB del usuario no está en la red. Nada de lo que sigue está
> verificado en hardware: no decir que anda hasta pasar la lista de pruebas de abajo con
> capturas.

## Modelos

| Placa | Estado | Notas |
|---|---|---|
| **Raspberry Pi 5** (4, 8 o 16 GB) | **objetivo principal** | BCM2712, 4× Cortex-A76 a 2,4 GHz, V3D 7.1 (OpenGL ES 3.1, Vulkan 1.3), kernel con páginas de 16K |
| Raspberry Pi 500 / CM5 | debería andar igual | el mismo SoC; sin probar |
| Raspberry Pi 4 / 400 / CM4 | mejor esfuerzo | BCM2711, 4× Cortex-A72, V3D 4.2 (GLES 3.1), kernel `rpi-v8` con páginas de 4K; con 2 GB queda justo |
| Pi 3 y anteriores, Zero 2 | no | poca RAM y GPU vc4 vieja (GLES 2 sin dmabuf moderno) |

## Equipo recomendado

- **Raspberry Pi 5 de 8 o 16 GB.** Con 4 GB anda, pero las apps GTK/Qt y Firefox ocupan bastante.
- **NVMe con el M.2 HAT+** (o cualquier base PCIe para M.2 2230/2242). Es varias veces más rápido
  que una SD, y se nota al arrancar y al abrir apps. La imagen trae `dtparam=pciex1_gen=3`; si el
  disco da errores, se comenta esa línea en `config.txt` y queda en gen 2.
- **Active Cooler** oficial, o un disipador con ventilador. Sin ventilador, la Pi 5 baja la
  frecuencia en pocos minutos de carga (video, compilar). El kernel maneja el ventilador solo.
- **Fuente oficial de 27 W (5 V, 5 A).** Con una de 3 A, la placa limita el USB a 600 mA y avisa.
- Monitor HDMI (micro-HDMI en la placa) y teclado y mouse USB.

## La imagen

`tools/rpi/build-img.sh` arma `lordos-<versión>-rpi5-arm64.img.xz`. Se graba igual en una SD o en un
NVMe, y la misma imagen arranca en la Pi 5 y en la Pi 4.

### Qué trae

- **Debian 13 (trixie) arm64**, minbase, con los mismos paquetes de LordOS que el ISO: PipeWire,
  wpa_supplicant, OpenVPN, Noto, foot, FeatherPad.
- **Kernel y firmware del archivo de Raspberry Pi** (`archive.raspberrypi.com/debian`, suite
  trixie), solo estos paquetes (`/etc/apt/preferences.d/raspberrypi`):
  - `linux-image-rpi-2712`: el de la Pi 5, con páginas de 16K;
  - `linux-image-rpi-v8`: el de la Pi 4/400/CM4, con páginas de 4K. La Pi 5 también lo puede
    usar, con `kernel=kernel8.img` en `config.txt`;
  - `raspi-firmware`: el arranque, los device trees y los overlays en `/boot/firmware`;
  - `firmware-brcm80211`: Wi-Fi y Bluetooth (CYW43455);
  - `rpi-eeprom` y `raspi-utils` (`vcgencmd`, `pinctrl`), si están.

  Todo lo demás viene de Debian, también Mesa.
- **Mesa de Debian 13:** v3d (GLES 3.1), v3dv (Vulkan) y kmsro (vc4 para la pantalla, v3d para
  dibujar). lord-server se compila con `GPU=1`.
- **El mismo paquete de LordOS que el ISO.** Lo arma `tools/iso/mkdeb.sh` con
  `DEB_ARCH=arm64`.
- **Particiones:** MBR, con el formato que esperan Raspberry Pi Imager y el firmware:

  | Partición | Tamaño | Qué tiene |
  |---|---|---|
  | 1 `bootfs` | 512 MiB, FAT32 | firmware, kernels (`kernel_2712.img`, `kernel8.img`), initramfs, device trees, `config.txt`, `cmdline.txt` |
  | 2 `rootfs` | el resto, ext4 | el sistema; se agranda solo en el primer arranque |

  `fstab` y `cmdline.txt` usan PARTUUID, así la misma imagen sirve para SD (`mmcblk0`) y NVMe
  (`nvme0n1`).

### Kernel: por qué el de Raspberry Pi y no el de Debian

Investigado a octubre de 2026 (confirmarlo en la placa):

- **El kernel de Debian 13** (`linux-image-arm64`, 6.12) arranca en la Pi 4 con el
  `raspi-firmware` de Debian. En la Pi 5 le falta el soporte completo de la placa: el chip RP1
  (USB, Ethernet, GPIO) y la parte de pantalla de la BCM2712 llegaron a Linux después de la 6.12,
  así que sin teclado, red ni HDMI no sirve.

  Además, el `raspi-firmware` de Debian regenera `config.txt` y `cmdline.txt` con cada kernel, y
  pisaría la configuración de LordOS.
- **El kernel de Raspberry Pi** (`linux-image-rpi-2712`, 6.12 con sus parches) es el de Raspberry Pi
  OS Trixie. Tiene todo: RP1, vc4 con HDMI 4K60, v3d, el decodificador HEVC, PCIe gen 3 y el
  ventilador.

  Sus hooks copian el kernel a `/boot/firmware` con el nombre que busca el firmware
  (`kernel_2712.img`). Si no lo hacen, `build-img.sh` lo copia a mano. **Elegido.**

Con el pinning, `apt upgrade` del sistema instalado sigue trayendo el kernel y el firmware de
Raspberry Pi, y todo lo demás de Debian.

### Paquetes para armarla

En un Debian 13 (x86-64 o arm64):

```sh
sudo apt-get install -y --no-install-recommends debootstrap fdisk dosfstools e2fsprogs xz-utils curl gpg \
  util-linux dpkg-dev
# solo en un host x86-64: emulación de aarch64 para debootstrap, apt y la compilación
sudo apt-get install -y --no-install-recommends qemu-user-static binfmt-support
```

### Armarla

```sh
# en el Mac: los temas (son JS, iguales en todas las arquitecturas)
npm run build
# en el Debian donde se arma (la VM 116 o la propia Pi), desde la raíz del repositorio
sudo tools/rpi/build-img.sh /var/tmp/lordos-rpi     # → /var/tmp/lordos-rpi/out/
```

Pasos:

1. **lord-server y lord-sessiond para aarch64.**
   - Si `server/build/` ya es aarch64 (`make -C server` en la Pi), se usa.
   - Si no, se arma un chroot arm64 aparte (`buildroot/`, se reutiliza) y se compila adentro con
     `GPU=1`.
   - Después corre `build/canvas-test`, que compara NEON con C. Si da distinto, el build se para.
2. **El paquete:** `DEB_ARCH=arm64 tools/iso/mkdeb.sh`.
3. **debootstrap arm64:** nativo en un host arm64. En x86-64 son dos etapas, con qemu.
4. **El archivo de Raspberry Pi.** Se verifica la huella de su clave (`RPI_KEY_FPR`). Después se
   instalan los paquetes y el `.deb`.
5. **Configuración:** la misma que el ISO (regiones, `ping` sin root, lordos.service en tty1,
   PipeWire con linger, el usuario `lordos-greeter`), más:
   - el primer arranque (`lordos-rpi-firstboot.service`);
   - la espera de vc4 (`lordos.service.d/rpi.conf`).

   root queda bloqueado.
6. **La imagen:** `sfdisk` (MBR), FAT32 y ext4. Después se copia el sistema y se escriben `fstab`
   y `cmdline.txt` con los PARTUUID.
7. **`xz -T0 -6`** y el `.sha256`.

Variables:

| Variable | Para qué |
|---|---|
| `RPI_SSH=1` | agrega `openssh-server` (para desarrollar) |
| `IMG_EXTRA_MB` | espacio libre en la imagen, 512 por defecto |
| `MIRROR`, `RPI_MIRROR` | espejos de Debian y de Raspberry Pi |
| `RPI_KEY_FPR`, `RPI_KEY_NOCHECK=1` | la clave de Raspberry Pi |

**Antes del primer build:** comprobar la huella de la clave del archivo de Raspberry Pi
(`gpg --show-keys raspberrypi.gpg.key`) contra una fuente oficial. La del script
(`CF8A1AF5…7FA3303E`) se anotó de memoria y no se verificó.

`build-img.sh` todavía no se corrió. Estimado: en x86-64 con qemu, 30–60 minutos la primera vez;
nativo en la Pi 5 con NVMe, unos 10.

### Grabarla

- **Raspberry Pi Imager:** «Elegir SO» → «Usar personalizado» → el `.img.xz`, y después la tarjeta
  o el NVMe.
  - La personalización del SO de Imager (usuario, Wi-Fi, SSH) está pensada para Raspberry Pi OS.
    Si deja un `userconf.txt` en la partición de arranque, LordOS lo usa (ver abajo).
  - El resto (Wi-Fi, SSH, teclado) no se aplica.
- **dd** (Linux o macOS):
  ```sh
  shasum -a 256 -c lordos-<versión>-rpi5-arm64.img.xz.sha256
  xz -dc lordos-<versión>-rpi5-arm64.img.xz | sudo dd of=/dev/rdiskN bs=4m   # Linux: of=/dev/sdX bs=4M conv=fsync
  ```
- **balenaEtcher:** abre el `.img.xz` directo.
- **NVMe:**
  - se puede grabar con un adaptador USB-NVMe desde otra máquina;
  - o arrancar la Pi desde una SD con LordOS (o Raspberry Pi OS) y grabarlo con `dd` en
    `/dev/nvme0n1`.

  Para que la Pi 5 arranque primero del NVMe:
  ```sh
  sudo rpi-eeprom-config --edit
  # BOOT_ORDER=0xf416   (NVMe, después SD, después USB)
  ```

### Primer arranque

`lordos-rpi-firstboot.service` corre antes de lord-sessiond (`/usr/lib/lordos/rpi/firstboot`) y
hace dos cosas.

1. **Agranda la partición 2** hasta el final de la tarjeta o del disco (`sfdisk -N 2`,
   `partx -u` y `resize2fs` en caliente).
2. **El usuario**, como el paso «Usuario» del instalador (`docs/ISO.md`).
   - **Con `lordos-user.txt`** en la partición `bootfs`: se copia de `lordos-user.txt.example`, que
     viene en la imagen. Tiene usuario, nombre, `password_hash` (o `password`), equipo, idioma,
     zona horaria y teclado.
     - Se valida con las reglas de `install.c`: el nombre, los reservados, el equipo, la zona y el
       teclado.
     - Se crea el usuario: grupos `sudo`, `audio`, `video`, `input` y `render`, linger y las
       carpetas del idioma.
     - El archivo se pisa con ceros y se borra.
     - Arranca en la **pantalla de ingreso**, como un sistema instalado (`AUTOLOGIN=` vacío).
     - También sirve el `userconf.txt` de Raspberry Pi OS / Imager (`usuario:hash`).
   - **Sin archivo: modo de configuración.**
     - Entra solo con el usuario `lordos`, sin contraseña y con sudo sin contraseña, como el ISO
       live.
     - En **Ajustes → Usuarios** se crea la cuenta propia, como administrador. En el próximo
       arranque `firstboot` ve que hay un administrador, borra `lordos` y su sudo y pasa a la
       pantalla de ingreso.
     - Mientras tanto, **no prender el escritorio remoto** y no dejar la placa expuesta en la red.

Pendiente: un paso de «crear usuario» propio en la pantalla de ingreso, así no hace falta el modo
de configuración (ver ROADMAP, M8).

Opciones en `cmdline.txt` (una sola línea), como en el ISO:

| Opción | Qué hace |
|---|---|
| `lordos.gpu=0` | modo seguro: todo por CPU |
| `lordos.lang=en` / `pt` | idioma del primer arranque |
| `video=HDMI-A-1:1920x1080@60` | fija el modo, por ejemplo en un monitor 4K (con CPU, 4K pesa: ver abajo) |

## Pantalla y GPU en la Pi 5

### Dos dispositivos DRM

| Nodo | Driver | Qué es |
|---|---|---|
| `/dev/dri/cardN` (suele ser `card1`) | `vc4` | la pantalla: HVS, HDMI ×2, planos, cursor. No tiene render node |
| `/dev/dri/cardM` (suele ser `card0`) | `v3d` | la GPU 3D, sin conectores |
| `/dev/dri/renderD128` | `v3d` | donde dibuja Mesa |

El orden de `card0` y `card1` depende de qué driver carga primero.

- **`server/src/drmcard.h`**, nuevo, lo usan lord-server (`main.c`) y lord-sessiond
  (`remote/monitor.c`). Elige la primera tarjeta que tiene conectores y CRTCs, y prefiere la que
  tiene un monitor enchufado (`GETRESOURCES` + `GETCONNECTOR`).
  - Ya no se asume `card0`. Esto también arregla las PC donde simpledrm deja la placa real en
    `card1`.
  - `LORD_CARD=/dev/dri/cardN` la fija, y `--card` de lord-server sigue funcionando.
- **`lordos.service.d/rpi.conf`** espera hasta 10 s a que aparezca un conector en `/sys/class/drm`
  (`wait-kms`). Si lord-sessiond arranca antes de que se cargue vc4, sin esto solo vería v3d.
- **GBM y EGL** se crean sobre la tarjeta de la pantalla (vc4), como siempre. Mesa la reconoce
  (kmsro) y dibuja con v3d. Los búferes de pantalla se crean en vc4 y v3d los importa. lord-server
  no necesita saber nada de esto.
- **El feedback de linux-dmabuf** anuncia el render node donde dibuja Mesa: `gpu_device()` en
  `gpu/gpu.c`. Así las apps (GTK, Qt, juegos) piden sus búferes a v3d y no a vc4, que no tiene
  render node. Lo busca así:
  1. EGL (`EGL_EXT_device_query` + `EGL_EXT_device_drm_render_node`);
  2. el render node de la misma placa;
  3. si la placa de la pantalla no tiene, el primer render node que haya (v3d).

  El log dice `gpu: composición por GPU (V3D 7.1…, render /dev/dri/renderD128, pantalla vc4)`.

### Modificadores

- **El plano primario de vc4** muestra RGB solo **lineal**. También acepta `BROADCOM_VC4_T_TILED`,
  que v3d no dibuja, y `BROADCOM_SAND64/128/256` solo para YUV (NV12/P030, video). v3d dibuja en
  **UIF** (`DRM_FORMAT_MOD_BROADCOM_UIF`), que vc4 no puede mostrar.
- **kms.c** ahora lee la propiedad `IN_FORMATS` del plano primario del CRTC (con
  `DRM_CLIENT_CAP_UNIVERSAL_PLANES`) y anota los pares formato/modificador de RGB de 32 bits. El
  log dice:

  ```
  kms: placa vc4, XRGB8888 en el plano primario con modificadores: 0x0 …
  ```
- **El scanout directo** de una app a pantalla completa (`gpu_can_scanout`) ahora exige que el
  plano acepte el modificador del búfer (`kms_scanout_ok`). Un búfer UIF de v3d no se manda a la
  pantalla (antes se probaba ADDFB2 y, si el driver lo aceptaba igual, se veía basura).
  - El modificador implícito (`INVALID`) cuenta como lineal.
  - Si el driver no informa `IN_FORMATS`, todo queda como antes.
- **La superficie de la UI** (`gbm_surface_create` con `SCANOUT | RENDERING`, sin modificadores):
  con kmsro, Mesa la crea en vc4, lineal. Así se muestra siempre.
- **Los dmabuf de las apps** se importan por EGL con su modificador. UIF para texturas está bien: se
  componen, no van a la pantalla.

### Qué verificar en la placa

1. `grep -E 'kms:|gpu:' ~/.cache/lordos/session.log` (o el log de la sesión). Tiene que mostrar:
   - la tarjeta elegida, con la placa `vc4`;
   - los modificadores del plano (que esté `0x0`, lineal);
   - `composición por GPU (V3D 7.1…)`;
   - `render /dev/dri/renderD128`;
   - `cursor por hardware`.
2. `journalctl -u lordos` muestra `lord-sessiond: pantalla: /dev/dri/card1` (o la que sea).
3. `LORD_GPU_DEBUG=1`:
   - la lista de formatos dmabuf que anuncia EGL (tiene que incluir UIF `0x0700000000000006` y
     lineal);
   - si un juego a pantalla completa pasa por scanout directo o se compone.
4. `glmark2-es2-wayland` y `vkcube` en ventana y a pantalla completa, con capturas.
5. GTK4 (gtk4-demo) por dmabuf:
   - `WAYLAND_DEBUG=1` muestra que pide el render node de v3d;
   - la ventana se ve bien (sin franjas de un tiling mal interpretado).
6. El cursor por hardware de vc4 (`CURSOR2`, 64×64).
7. Dos monitores: hoy LordOS usa el primer conector conectado (HDMI-A-1); el segundo queda apagado.
8. Monitor 4K. Por CPU, el lienzo de 3840×2160 son 33 MB por búfer y copiarlo cuesta. Con GPU
   debería andar bien. Si no, `video=HDMI-A-1:1920x1080@60`.

## Video (para el ayudante `lord-media`)

Esta parte es para el agente de medios: es lo que la Pi 5 necesita además de VA-API, que es el
camino de las PC. No se tocaron sus archivos.

- **La Pi 5 no tiene decodificador de H.264 por hardware** (la Pi 4 sí, y ya no se usa). Tiene solo
  **HEVC/H.265 por hardware** (`rpi-hevc-dec`), hasta 4Kp60 de 8 y 10 bits.
  - Es un decodificador **V4L2 stateless** (API «request»), en `/dev/video19` y un
    `/dev/mediaN`.
  - No hay VA-API ni VDPAU en la Pi.
- **FFmpeg:**
  - El de Debian 13 (7.1) **no trae** el hwaccel V4L2-request; el de upstream todavía no lo tiene.
  - El de Raspberry Pi (`archive.raspberrypi.com`, versiones `+rpt`) sí: `-hwaccel drm`, con el
    decodificador `hevc` y salida `drm_prime`. Algunas versiones lo exponen como
    `hevc_v4l2request`.

  Opciones:
  1. pinear `ffmpeg` y las `libav*` de Raspberry Pi a 990, como el kernel, solo en la imagen de la
     Pi;
  2. que `lord-media` cargue una `libavcodec` alternativa;
  3. hablar V4L2-request directamente desde C (mucho trabajo).

  Recomendado: la 1. Agregar
  `ffmpeg libavcodec61 libavformat61 libavutil59 libswscale8 libswresample5 libavfilter10` a
  `/etc/apt/preferences.d/raspberrypi` (verificar los nombres con `apt-cache policy` en la placa).
- **Los cuadros decodificados** son DRM PRIME:
  - **NV12 o P030 con modificador `BROADCOM_SAND128`**: el formato «en columnas» de la Pi;
  - `av_hwframe_map` / `AVDRMFrameDescriptor` dan el fd, el offset, el pitch y el modificador.

  Para mostrarlos:
  - **Por GPU (lo mejor):** importarlos por EGL con el modificador SAND128 y
    `GL_OES_EGL_image_external`. Mesa v3d tiene que aceptar SAND128 (se cree que lo convierte con
    la TFU): **verificar** con `LORD_GPU_DEBUG=1`. lord-server ya importa dmabuf multiplano con
    modificador (`gpu_tex_dmabuf`) si el formato está en el feedback. Hoy `gpu_formats()` solo
    anuncia RGB de 32 bits (`known[]` en `gpu.c`): para el video hay que agregar NV12/P030.
  - **Por un plano overlay de vc4:** el HVS muestra SAND128 directo (es lo que hace Kodi). LordOS
    todavía no usa planos overlay (GPU.md, Pendiente).
  - **Por CPU:** `av_hwframe_transfer_data` a NV12 lineal y convertir. Sirve para 1080p; para 4K es
    caro.
- **H.264, VP9 y AV1 por software,** en los 4 Cortex-A76 (FFmpeg y dav1d tienen NEON):
  - H.264 1080p30/60 debería andar con 2–3 hilos;
  - 4K H.264 no es realista;
  - AV1 1080p con dav1d, al límite.

  Usar `thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE` y `threads = 4`. Con el Active Cooler, para
  que no baje la frecuencia.
- **En la Pi 4:** H.264 por hardware (`h264_v4l2m2m`, V4L2 stateful, `/dev/video10`, en el FFmpeg de
  Debian) y HEVC por `rpi-hevc-dec` como en la Pi 5.
- **Lo que tiene que medir el agente de medios en la placa:**
  - CPU y cuadros perdidos con 1080p H.264, 1080p HEVC y 4K HEVC;
  - la temperatura (`vcgencmd measure_temp`);
  - el throttling (`vcgencmd get_throttled`, tiene que dar `0x0`).

## Desarrollar en la placa

En la placa hay dos sistemas posibles:

- **Raspberry Pi OS Lite 64-bit (Trixie):** el kernel y el firmware ya están, y Mesa viene del
  archivo de Raspberry Pi. Lo más simple para empezar.
- **La imagen de LordOS**, armada con `RPI_SSH=1`.

### Pasos

1. **SSH:**
   - con Raspberry Pi OS, se habilita en Imager;
   - con LordOS, se arma con `RPI_SSH=1` y se usa la cuenta creada en el primer arranque;
   - `ssh usuario@<ip>`; la IP aparece en la pantalla de ingreso o con `ip -4 addr`.
2. **Paquetes para compilar** (los mismos de `VM-SETUP.md` más Mesa):
   ```sh
   sudo apt-get install -y --no-install-recommends build-essential pkg-config libxkbcommon-dev libwayland-dev \
     wayland-protocols libgbm-dev libegl-dev libgles-dev libssl-dev libcrypt-dev xkb-data \
     libgl1-mesa-dri libegl1 libgles2 libgbm1 mesa-utils glmark2-es2-wayland
   ```
3. **El árbol,** desde el Mac (como para la VM; los temas se construyen en el Mac):
   ```sh
   npm run build
   COPYFILE_DISABLE=1 tar czf - --no-xattrs --exclude build --exclude node_modules \
     package.json README.md server third_party assets themes tools | \
     ssh usuario@<ip-de-la-pi> 'rm -rf ~/LordOS && mkdir ~/LordOS && cd ~/LordOS && tar xzf -'
   ```
4. **Compilar en la placa,** nativo y sin cruzar:
   ```sh
   cd ~/LordOS
   make -C server -j4           # ARCH=aarch64 sale solo: NEON=1 y GPU=1 si están los -dev
   make -C server test          # todas las pruebas sin pantalla, incluida build/canvas-test
   server/build/canvas-test     # NEON = C, y cuánto tarda cada uno
   ```
5. **Probar lord-server solo,** desde una consola (Ctrl+Alt+F2, con lordos.service parado):
   ```sh
   cd ~/LordOS/server && sudo build/lord-server --user "$USER" 2> /tmp/lord.log
   ```
6. **Instalarlo en la imagen de LordOS:** `sh tools/iso/mkdeb.sh /tmp/lordos.deb` (en la placa
   toma `arm64` de `dpkg --print-architecture`) y `sudo apt install /tmp/lordos.deb`.
7. **Capturas:**
   - el escritorio remoto de LordOS (Ajustes → Compartir, puerto 5673; `REMOTO.md`) desde el
     navegador del Mac, solo en la LAN y apagándolo después;
   - o `grim` dentro de una sesión Wayland.

   No hay `screendump` de QEMU: es una placa.

## Lista de pruebas en la placa

Cada punto, con una captura o la salida pegada, en este documento.

| # | Prueba | Cómo | Esperado |
|---|---|---|---|
| 1 | Arranque | `systemd-analyze`, `systemd-analyze blame \| head` | SD: <25 s; NVMe: <12 s hasta la pantalla de ingreso |
| 2 | Primer arranque | imagen recién grabada, con y sin `lordos-user.txt` | partición agrandada (`df -h /`); pantalla de ingreso o modo de configuración; el archivo borrado |
| 3 | RAM | `free -m`, RSS de lord-server (`ps -o rss`) | escritorio en reposo <400 MB en total; lord-server ~35–60 MB (con 16K de página el RSS sube un poco) |
| 4 | Tarjeta y GPU | el log (ver «Qué verificar») | `vc4`, `V3D 7.1`, `renderD128`, cursor por hardware |
| 5 | FPS de la composición por GPU | `glmark2-es2-wayland` en ventana y a pantalla completa; arrastrar ventanas; Launchpad | 60 FPS sincronizados al vblank |
| 6 | Camino por CPU | `lordos.gpu=0` | usable a 1080p; anotar ms por cuadro con `--profile` |
| 7 | Modificadores | `LORD_GPU_DEBUG=1`, `kms:` en el log | lineal en el plano; UIF solo para texturas; sin basura en pantalla |
| 8 | dmabuf de las apps | gtk4-demo, foot, FeatherPad (Qt6), `vkcube` | se ven bien; `WAYLAND_DEBUG` muestra renderD128 |
| 9 | Video 1080p | H.264 y HEVC (cuando esté `lord-media`) | sin cuadros perdidos; CPU anotada |
| 10 | Video 4K | HEVC 4Kp60 por hardware | fluido; temperatura anotada |
| 11 | Temperatura | `vcgencmd measure_temp`, `vcgencmd get_throttled`, Monitor de LordOS (`cpu_thermal` y el ventilador) | <75 °C con el Active Cooler en carga; `throttled=0x0` |
| 12 | NEON | `server/build/canvas-test` | «NEON igual a C»; NEON más rápido que C |
| 13 | Pruebas sin pantalla | `make -C server test` y cada `build/*-test` | todas pasan, como en x86 (WASM: la suite wg-2.0 completa) |
| 14 | Aislamiento de apps | abrir un `.lpk`; `sandbox[...]:` en el log; `cat /sys/kernel/security/lsm` | seccomp=1 y Landlock si el kernel lo trae (el de Raspberry Pi podría no tenerlo) |
| 15 | Escritorio remoto | Ajustes → Compartir, `https://<ip>:5673` desde el Mac | se ve y se maneja; capturas para este documento |
| 16 | Red y sonido | Wi-Fi (brcmfmac), Ethernet, HDMI audio por PipeWire | IP por el DHCP propio; `wpctl status` muestra HDMI |
| 17 | Pi 4 | la misma imagen | arranca con `kernel8.img`; anotar qué falla |

## Auditoría de portabilidad del servidor (aarch64)

Revisión de `server/` en el Mac, sin compilar para Linux ARM. Los archivos cambiados se revisaron
con `clang -arch arm64 -fsyntax-only -Wall -Wextra` contra los headers DRM de Linux 6.12 y los de
EGL/GLES/GBM. Lo que sigue es el resultado.

**No había nada específico de x86:**
- ni intrinsics SSE/AVX ni asm en `server/src` (el blending, el blur y el escalado son C portable,
  con SWAR de 32 bits);
- ni `-march=native` en el Makefile.

**Arreglado:**

| Dónde | Qué |
|---|---|
| `server/Makefile` | `ARCH` (de `$(CC) -dumpmachine`). En aarch64, `NEON=1` por defecto (`-DLORD_NEON`; `NEON=0` lo apaga). QuickJS con `-ffp-contract=off`: GCC en modo GNU fusiona `a*b+c` en FMA en ARM y cambiaría resultados numéricos de JS; nuestro código usa `-std=c11`, que ya lo apaga. `$(LD)` en vez de `ld` para `pages.o` (compilación cruzada). Nueva prueba `build/canvas-test` |
| `server/src/canvas.c` | Las filas de `canvas_blit` (sin esquinas) y de `canvas_alpha_mask` (texto, sombras) son funciones aparte, con versión NEON (4 píxeles por vuelta) bajo `LORD_NEON && __ARM_NEON`. Bit a bit iguales a C con orígenes premultiplicados válidos: comprobado con `tests/canvas_test.c` bajo emulación (SIMDe) en el Mac, 4000 filas al azar. Falta en la placa |
| `server/src/ui/stb.c` | `STBI_NEON` en aarch64: el JPEG de stb usa NEON (en x86 ya usaba SSE2) |
| `server/src/term/vt.c:890` | `src[i] >= 0 && …` con `char`: en ARM `char` no tiene signo y `-Wextra` avisa (`-Wtype-limits`). Ahora `(unsigned char)src[i] < 0x20`, igual en las dos arquitecturas |
| `server/src/kms.c` `kms_dispatch` | el búfer de eventos DRM alineado a 8 (se lee con cast a `drm_event_vblank`, que tiene campos de 64 bits) |
| `server/src/drmcard.h` (nuevo), `main.c`, `remote/monitor.c` | elegir la tarjeta con conectores en vez de `card0` (ver arriba) |
| `server/src/kms.c`/`kms.h`, `gpu/gpu.c` | `IN_FORMATS` del plano primario, `kms_scanout_ok`, el render node de EGL para el feedback de dmabuf (ver arriba) |
| `server/src/ui/js_sys.c` | `lord.system.info().cpu`: en ARM64 `/proc/cpuinfo` no tiene «model name». Ahora sale de «CPU part» y `/proc/device-tree/model`: «Cortex-A76 · Raspberry Pi 5 Model B Rev 1.0» |
| `server/src/sec/sandbox.c` (lo arregló el agente de sandbox, a pedido) | `SYS_epoll_wait` sin `#ifdef`: en aarch64 no existe y no compilaba. Se quitó el `SYS_read` repetido |

**Revisado, sin cambios:**
- **Tamaño de página (16K en la Pi 5):** no hay 4096 fijo en `mmap`, shm ni búferes.
  - Los dumb buffers usan `size` y `pitch` de `CREATE_DUMB`.
  - La memoria de WASM reserva y protege en múltiplos de 64 KiB (múltiplo de 16K).
  - El Monitor usa `sysconf(_SC_PAGESIZE)` (`monitor/mon.c:443`).
  - El feedback de dmabuf hace `memfd` + `mmap` de un tamaño cualquiera, que es válido.
- **Lecturas desalineadas y endianness:**
  - los lectores de OpenType, MaxMind DB, xz, PNG/JPEG (stb), thbrk e IME leen byte por byte en
    big-endian o con `memcpy`;
  - los atributos de netlink (`net/state.c`, `nl.c`, `wg.c`) están alineados a 4 por el formato.
  - Todo es little-endian, como x86.
- **`char` sin signo:** los decodificadores UTF-8 y los escapes JSON ya usan `unsigned char`
  (`mon.c`, `nl.c`, `snd.c`, `textlayout.c`, `json.c`, `wl.c`, `conn.c`); los demás `char` solo
  se comparan con ASCII.
- **Estructuras e ioctls:** `input_event` (24 bytes), `drm_mode_*` y `EVIOCG*` son iguales en LP64
  (x86-64 y aarch64). No hay código de fbdev.
- **`SYS_close_range` y `SYS_capset`** existen en aarch64.
- **WASM:** las conversiones float→int verifican el rango antes del cast, los NaN se tratan a mano
  (`QNAN32/64`) y las cargas usan `memcpy`. Correr `build/wasm-test` en la placa.
- **QuickJS 2025-04-26** soporta aarch64.
- **`pkg/status.c`** ya mapea `aarch64` → `arm64` para dpkg y la Tienda.
- **Hilos:** `PTHREAD_STACK_MIN` es 128 KiB en aarch64; los hilos del Monitor piden 512 KiB, alcanza.

**Pendiente para la etapa en la placa** (riesgos, con archivo:línea):

| Dónde | Riesgo |
|---|---|
| `server/src/main.c:488` (`mirror_frame(out.px…)` en el camino por CPU) | Con el escritorio remoto abierto, se lee desde el dumb buffer mapeado. En ARM esa memoria es write-combined/uncached y leerla es muy lento; en x86 también, pero menos. Mejor espejar desde `s->scene` + `wl_paint_fullscreen` a un búfer en RAM. Medir antes de tocar |
| `server/src/main.c:484–498` (`canvas_copy`, línea 486) | El camino por CPU copia el lienzo entero al dumb buffer (`canvas_copy`) en cada cuadro. A 4K son 33 MB por cuadro, demasiado para la memoria de la Pi. Copiar solo el daño |
| `server/src/canvas.c` `box_pass`/`canvas_backdrop_blur` | La pasada vertical recorre por columnas (`step = sw`), que es lento con la caché de la A76. Candidato a NEON (4 columnas a la vez) o a transponer; medir con `--profile` |
| `server/src/gpu/gpu.c` `gpu_formats` (`known[]`) | Solo anuncia RGB de 32 bits: el video SAND128/NV12 de la Pi no se anuncia (ver Video) |
| `server/src/gpu/gpu.c` `gpu_device` | Que EGL devuelva el render node con kmsro está sin verificar. Si devuelve nada, cae al primer render node, que en la Pi es v3d |
| `server/src/kms.c` `read_scanout_formats` | `IN_FORMATS` con `UNIVERSAL_PLANES` sin probar en vc4. Si falla, queda como antes |
| `server/src/drmcard.h` | `GETCONNECTOR` fuerza una lectura del EDID por tarjeta al arrancar (~50 ms). Hotplug: si el HDMI se enchufa después del arranque, lord-sessiond ya eligió la tarjeta (vc4 igual, porque tiene conectores) |
| `server/src/sec/sandbox.c` | seccomp: revisar en la placa que ninguna app `.lpk` muera por un syscall que glibc de arm64 usa y x86 no. Landlock: el kernel de Raspberry Pi podría no traerlo; `cmdline.txt` pide `lsm=landlock,…`. El perfil compartido de ayudantes (`sandbox_apply_helper`) usa la misma lista y está bien en arm64, pero no tiene `ioctl`: el video por hardware (V4L2 en la Pi, VA-API en la PC) dentro de la caja de arena lo necesita (se avisó al agente de sandbox) |
| `server/src/monitor/mon.c:703` | El «model name» del Monitor queda vacío en ARM. Se avisó al agente del Monitor (sirve el `arm_cpu_name` de `js_sys.c`) |
| `server/src/install/install.c:677` | El instalador usa `grub-install --target=x86_64-efi`. En la Pi no aplica (no es live, el instalador no aparece), pero es lo que habría que cambiar para un instalador ARM64/UEFI |
| tamaño de página | Algún programa de terceros (no LordOS) asume 4K: Widevine, box64, jemalloc viejo. Si hace falta, `kernel=kernel8.img` en `config.txt` (4K, un poco más lento) |

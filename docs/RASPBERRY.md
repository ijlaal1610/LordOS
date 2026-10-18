# LordOS on Raspberry Pi (ARM64)

Since M8, ARM64 is an official LordOS platform, with the **Raspberry Pi 5** as the reference board.
This document gathers what's needed to build the image, write it, develop on the board and test it.

> **Status (2026-10-16):** prepared in the code and tools, **not yet tested on a board**. The user's
> 16 GB Pi 5 isn't on the network. Nothing below is verified on hardware: don't claim it works until
> the test checklist below has been passed with screenshots.

## Models

| Board | Status | Notes |
|---|---|---|
| **Raspberry Pi 5** (4, 8 or 16 GB) | **main target** | BCM2712, 4× Cortex-A76 at 2.4 GHz, V3D 7.1 (OpenGL ES 3.1, Vulkan 1.3), kernel with 16K pages |
| Raspberry Pi 500 / CM5 | should work the same | same SoC; untested |
| Raspberry Pi 4 / 400 / CM4 | best effort | BCM2711, 4× Cortex-A72, V3D 4.2 (GLES 3.1), `rpi-v8` kernel with 4K pages; with 2 GB it's tight |
| Pi 3 and earlier, Zero 2 | no | too little RAM and an old vc4 GPU (GLES 2 without modern dmabuf) |

## Recommended equipment

- **Raspberry Pi 5 with 8 or 16 GB.** It works with 4 GB, but GTK/Qt apps and Firefox take quite a
  lot.
- **NVMe with the M.2 HAT+** (or any PCIe base for M.2 2230/2242). It's several times faster than an
  SD card, and it shows when booting and opening apps. The image ships `dtparam=pciex1_gen=3`; if the
  disk gives errors, comment that line out in `config.txt` and it stays at gen 2.
- The official **Active Cooler**, or a heatsink with a fan. Without a fan, the Pi 5 throttles within
  a few minutes of load (video, compiling). The kernel drives the fan on its own.
- **Official 27 W power supply (5 V, 5 A).** With a 3 A one, the board limits USB to 600 mA and warns
  about it.
- HDMI monitor (micro-HDMI on the board) and a USB keyboard and mouse.

## The image

`tools/rpi/build-img.sh` builds `lordos-<version>-rpi5-arm64.img.xz`. It is written the same way to an
SD card or an NVMe, and the same image boots on the Pi 5 and the Pi 4.

### What it ships

- **Debian 13 (trixie) arm64**, minbase, with the same LordOS packages as the ISO: PipeWire,
  wpa_supplicant, OpenVPN, Noto, foot, FeatherPad.
- **Kernel and firmware from the Raspberry Pi archive** (`archive.raspberrypi.com/debian`, suite
  trixie), only these packages (`/etc/apt/preferences.d/raspberrypi`):
  - `linux-image-rpi-2712`: the Pi 5 one, with 16K pages;
  - `linux-image-rpi-v8`: the Pi 4/400/CM4 one, with 4K pages. The Pi 5 can also use it, with
    `kernel=kernel8.img` in `config.txt`;
  - `raspi-firmware`: the boot files, device trees and overlays in `/boot/firmware`;
  - `firmware-brcm80211`: Wi-Fi and Bluetooth (CYW43455);
  - `rpi-eeprom` and `raspi-utils` (`vcgencmd`, `pinctrl`), if available.

  Everything else comes from Debian, Mesa included.
- **Debian 13's Mesa:** v3d (GLES 3.1), v3dv (Vulkan) and kmsro (vc4 for the display, v3d for
  rendering). lord-server is compiled with `GPU=1`.
- **The same LordOS package as the ISO.** Built by `tools/iso/mkdeb.sh` with `DEB_ARCH=arm64`.
- **Partitions:** MBR, in the layout Raspberry Pi Imager and the firmware expect:

  | Partition | Size | What it holds |
  |---|---|---|
  | 1 `bootfs` | 512 MiB, FAT32 | firmware, kernels (`kernel_2712.img`, `kernel8.img`), initramfs, device trees, `config.txt`, `cmdline.txt` |
  | 2 `rootfs` | the rest, ext4 | the system; it grows on its own at first boot |

  `fstab` and `cmdline.txt` use PARTUUIDs, so the same image works for SD (`mmcblk0`) and NVMe
  (`nvme0n1`).

### Kernel: why Raspberry Pi's and not Debian's

Researched as of October 2026 (confirm on the board):

- **Debian 13's kernel** (`linux-image-arm64`, 6.12) boots on the Pi 4 with Debian's
  `raspi-firmware`. On the Pi 5 it lacks full board support: the RP1 chip (USB, Ethernet, GPIO) and
  the BCM2712's display part reached Linux after 6.12, so without keyboard, network or HDMI it's no
  use.

  Also, Debian's `raspi-firmware` regenerates `config.txt` and `cmdline.txt` with every kernel, and
  would overwrite LordOS's configuration.
- **The Raspberry Pi kernel** (`linux-image-rpi-2712`, 6.12 with its patches) is the one from
  Raspberry Pi OS Trixie. It has everything: RP1, vc4 with HDMI 4K60, v3d, the HEVC decoder, PCIe
  gen 3 and the fan.

  Its hooks copy the kernel to `/boot/firmware` under the name the firmware looks for
  (`kernel_2712.img`). If they don't, `build-img.sh` copies it by hand. **Chosen.**

With the pinning, `apt upgrade` on the installed system keeps bringing the kernel and firmware from
Raspberry Pi, and everything else from Debian.

### Packages to build it

On a Debian 13 (x86-64 or arm64):

```sh
sudo apt-get install -y --no-install-recommends debootstrap fdisk dosfstools e2fsprogs xz-utils curl gpg \
  util-linux dpkg-dev
# only on an x86-64 host: aarch64 emulation for debootstrap, apt and compiling
sudo apt-get install -y --no-install-recommends qemu-user-static binfmt-support
```

### Building it

```sh
# on the Mac: the themes (they are JS, the same on every architecture)
npm run build
# on the Debian where it's built (VM 116 or the Pi itself), from the repository root
sudo tools/rpi/build-img.sh /var/tmp/lordos-rpi     # → /var/tmp/lordos-rpi/out/
```

Steps:

1. **lord-server and lord-sessiond for aarch64.**
   - If `server/build/` is already aarch64 (`make -C server` on the Pi), it is used.
   - If not, a separate arm64 chroot is built (`buildroot/`, reused) and it's compiled inside with
     `GPU=1`.
   - Then `build/canvas-test` runs, which compares NEON with C. If they differ, the build stops.
2. **The package:** `DEB_ARCH=arm64 tools/iso/mkdeb.sh`.
3. **debootstrap arm64:** native on an arm64 host. On x86-64 it's two stages, with qemu.
4. **The Raspberry Pi archive.** Its key's fingerprint is verified (`RPI_KEY_FPR`). Then the packages
   and the `.deb` are installed.
5. **Configuration:** the same as the ISO (locales, `ping` without root, lordos.service on tty1,
   PipeWire with linger, the `lordos-greeter` user), plus:
   - the first boot (`lordos-rpi-firstboot.service`);
   - waiting for vc4 (`lordos.service.d/rpi.conf`).

   root is locked.
6. **The image:** `sfdisk` (MBR), FAT32 and ext4. Then the system is copied and `fstab` and
   `cmdline.txt` are written with the PARTUUIDs.
7. **`xz -T0 -6`** and the `.sha256`.

Variables:

| Variable | What for |
|---|---|
| `RPI_SSH=1` | adds `openssh-server` (for development) |
| `IMG_EXTRA_MB` | free space in the image, 512 by default |
| `MIRROR`, `RPI_MIRROR` | Debian and Raspberry Pi mirrors |
| `RPI_KEY_FPR`, `RPI_KEY_NOCHECK=1` | the Raspberry Pi key |

**Before the first build:** check the fingerprint of the Raspberry Pi archive key
(`gpg --show-keys raspberrypi.gpg.key`) against an official source. The one in the script
(`CF8A1AF5…7FA3303E`) was written down from memory and hasn't been verified.

`build-img.sh` hasn't been run yet. Estimate: on x86-64 with qemu, 30–60 minutes the first time;
natively on the Pi 5 with NVMe, about 10.

### Writing it

- **Raspberry Pi Imager:** "Choose OS" → "Use custom" → the `.img.xz`, and then the card or the
  NVMe.
  - Imager's OS customization (user, Wi-Fi, SSH) is designed for Raspberry Pi OS. If it leaves a
    `userconf.txt` on the boot partition, LordOS uses it (see below).
  - The rest (Wi-Fi, SSH, keyboard) isn't applied.
- **dd** (Linux or macOS):
  ```sh
  shasum -a 256 -c lordos-<version>-rpi5-arm64.img.xz.sha256
  xz -dc lordos-<version>-rpi5-arm64.img.xz | sudo dd of=/dev/rdiskN bs=4m   # Linux: of=/dev/sdX bs=4M conv=fsync
  ```
- **balenaEtcher:** opens the `.img.xz` directly.
- **NVMe:**
  - it can be written with a USB-NVMe adapter from another machine;
  - or boot the Pi from an SD card with LordOS (or Raspberry Pi OS) and write it with `dd` to
    `/dev/nvme0n1`.

  To make the Pi 5 boot from NVMe first:
  ```sh
  sudo rpi-eeprom-config --edit
  # BOOT_ORDER=0xf416   (NVMe, then SD, then USB)
  ```

### First boot

`lordos-rpi-firstboot.service` runs before lord-sessiond (`/usr/lib/lordos/rpi/firstboot`) and does
two things.

1. **Grows partition 2** to the end of the card or disk (`sfdisk -N 2`, `partx -u` and an online
   `resize2fs`).
2. **The user**, like the installer's "User" step (`docs/ISO.md`).
   - **With `lordos-user.txt`** on the `bootfs` partition: it's copied from
     `lordos-user.txt.example`, which ships in the image. It has user, name, `password_hash` (or
     `password`), computer name, language, time zone and keyboard.
     - It's validated with `install.c`'s rules: the name, reserved names, the computer name, the
       zone and the keyboard.
     - The user is created: groups `sudo`, `audio`, `video`, `input` and `render`, linger and the
       language's folders.
     - The file is overwritten with zeros and deleted.
     - It boots to the **login screen**, like an installed system (empty `AUTOLOGIN=`).
     - The `userconf.txt` from Raspberry Pi OS / Imager (`user:hash`) also works.
   - **Without the file: setup mode.**
     - It logs in on its own with the `lordos` user, without a password and with passwordless sudo,
       like the live ISO.
     - In **Settings → Users** you create your own account, as an administrator. On the next boot
       `firstboot` sees there's an administrator, deletes `lordos` and its sudo and switches to the
       login screen.
     - In the meantime, **don't turn on remote desktop** and don't leave the board exposed on the
       network.

Pending: a "create user" step of our own on the login screen, so setup mode isn't needed (see
ROADMAP, M8).

Options in `cmdline.txt` (a single line), as on the ISO:

| Option | What it does |
|---|---|
| `lordos.gpu=0` | safe mode: everything on the CPU |
| `lordos.lang=en` / `pt` | first-boot language |
| `video=HDMI-A-1:1920x1080@60` | pins the mode, for example on a 4K monitor (on the CPU, 4K is heavy: see below) |

## Display and GPU on the Pi 5

### Two DRM devices

| Node | Driver | What it is |
|---|---|---|
| `/dev/dri/cardN` (usually `card1`) | `vc4` | the display: HVS, HDMI ×2, planes, cursor. It has no render node |
| `/dev/dri/cardM` (usually `card0`) | `v3d` | the 3D GPU, without connectors |
| `/dev/dri/renderD128` | `v3d` | where Mesa renders |

The order of `card0` and `card1` depends on which driver loads first.

- **`server/src/drmcard.h`**, new, is used by lord-server (`main.c`) and lord-sessiond
  (`remote/monitor.c`). It picks the first card that has connectors and CRTCs, and prefers the one
  with a monitor plugged in (`GETRESOURCES` + `GETCONNECTOR`).
  - `card0` is no longer assumed. This also fixes PCs where simpledrm leaves the real card at
    `card1`.
  - `LORD_CARD=/dev/dri/cardN` pins it, and lord-server's `--card` still works.
- **`lordos.service.d/rpi.conf`** waits up to 10 s for a connector to appear in `/sys/class/drm`
  (`wait-kms`). If lord-sessiond starts before vc4 is loaded, without this it would only see v3d.
- **GBM and EGL** are created on the display card (vc4), as always. Mesa recognizes it (kmsro) and
  renders with v3d. Scanout buffers are created on vc4 and v3d imports them. lord-server doesn't need
  to know anything about this.
- **The linux-dmabuf feedback** announces the render node where Mesa renders: `gpu_device()` in
  `gpu/gpu.c`. That way apps (GTK, Qt, games) ask v3d for their buffers and not vc4, which has no
  render node. It looks it up like this:
  1. EGL (`EGL_EXT_device_query` + `EGL_EXT_device_drm_render_node`);
  2. the same card's render node;
  3. if the display card has none, the first render node there is (v3d).

  The log says `gpu: composición por GPU (V3D 7.1…, render /dev/dri/renderD128, pantalla vc4)`.

### Modifiers

- **vc4's primary plane** shows RGB only as **linear**. It also accepts `BROADCOM_VC4_T_TILED`, which
  v3d doesn't render, and `BROADCOM_SAND64/128/256` only for YUV (NV12/P030, video). v3d renders in
  **UIF** (`DRM_FORMAT_MOD_BROADCOM_UIF`), which vc4 can't display.
- **kms.c** now reads the `IN_FORMATS` property of the CRTC's primary plane (with
  `DRM_CLIENT_CAP_UNIVERSAL_PLANES`) and records the 32-bit RGB format/modifier pairs. The log says:

  ```
  kms: placa vc4, XRGB8888 en el plano primario con modificadores: 0x0 …
  ```
- **Direct scanout** of a fullscreen app (`gpu_can_scanout`) now requires the plane to accept the
  buffer's modifier (`kms_scanout_ok`). A v3d UIF buffer isn't sent to the display (before, ADDFB2 was
  tried and, if the driver accepted it anyway, garbage showed up).
  - The implicit modifier (`INVALID`) counts as linear.
  - If the driver doesn't report `IN_FORMATS`, everything stays as before.
- **The UI surface** (`gbm_surface_create` with `SCANOUT | RENDERING`, without modifiers): with
  kmsro, Mesa creates it on vc4, linear. So it always displays.
- **Apps' dmabufs** are imported via EGL with their modifier. UIF is fine for textures: they're
  composited, they don't go to the display.

### What to verify on the board

1. `grep -E 'kms:|gpu:' ~/.cache/lordos/session.log` (or the session log). It has to show:
   - the chosen card, with the `vc4` driver;
   - the plane's modifiers (with `0x0`, linear, present);
   - `composición por GPU (V3D 7.1…)`;
   - `render /dev/dri/renderD128`;
   - `cursor por hardware`.
2. `journalctl -u lordos` shows `lord-sessiond: display: /dev/dri/card1` (or whichever).
3. `LORD_GPU_DEBUG=1`:
   - the list of dmabuf formats EGL announces (it has to include UIF `0x0700000000000006` and
     linear);
   - whether a fullscreen game goes through direct scanout or is composited.
4. `glmark2-es2-wayland` and `vkcube` windowed and fullscreen, with screenshots.
5. GTK4 (gtk4-demo) over dmabuf:
   - `WAYLAND_DEBUG=1` shows it requests v3d's render node;
   - the window looks right (no stripes from misinterpreted tiling).
6. vc4's hardware cursor (`CURSOR2`, 64×64).
7. Two monitors: today LordOS uses the first connected connector (HDMI-A-1); the second stays off.
8. 4K monitor. On the CPU, the 3840×2160 canvas is 33 MB per buffer and copying it is costly. With
   the GPU it should work well. If not, `video=HDMI-A-1:1920x1080@60`.

## Video (for the `lord-media` helper)

This part is for the media agent: it's what the Pi 5 needs besides VA-API, which is the PC path. Its
files weren't touched.

- **The Pi 5 has no hardware H.264 decoder** (the Pi 4 does, and it's no longer used). It only has
  **hardware HEVC/H.265** (`rpi-hevc-dec`), up to 4Kp60 at 8 and 10 bits.
  - It's a **V4L2 stateless** decoder ("request" API), at `/dev/video19` and a `/dev/mediaN`.
  - There's no VA-API or VDPAU on the Pi.
- **FFmpeg:**
  - Debian 13's (7.1) **doesn't include** the V4L2-request hwaccel; upstream doesn't have it yet.
  - Raspberry Pi's (`archive.raspberrypi.com`, `+rpt` versions) does: `-hwaccel drm`, with the
    `hevc` decoder and `drm_prime` output. Some versions expose it as `hevc_v4l2request`.

  Options:
  1. pin `ffmpeg` and the Raspberry Pi `libav*` at 990, like the kernel, only in the Pi image;
  2. have `lord-media` load an alternative `libavcodec`;
  3. speak V4L2-request directly from C (a lot of work).

  Recommended: option 1. Add
  `ffmpeg libavcodec61 libavformat61 libavutil59 libswscale8 libswresample5 libavfilter10` to
  `/etc/apt/preferences.d/raspberrypi` (verify the names with `apt-cache policy` on the board).
- **Decoded frames** are DRM PRIME:
  - **NV12 or P030 with the `BROADCOM_SAND128` modifier**: the Pi's "column" format;
  - `av_hwframe_map` / `AVDRMFrameDescriptor` give the fd, offset, pitch and modifier.

  To display them:
  - **On the GPU (best):** import them via EGL with the SAND128 modifier and
    `GL_OES_EGL_image_external`. Mesa v3d has to accept SAND128 (it is believed to convert it with
    the TFU): **verify** with `LORD_GPU_DEBUG=1`. lord-server already imports multiplanar dmabufs
    with modifiers (`gpu_tex_dmabuf`) if the format is in the feedback. Today `gpu_formats()` only
    announces 32-bit RGB (`known[]` in `gpu.c`): for video NV12/P030 have to be added.
  - **Through a vc4 overlay plane:** the HVS displays SAND128 directly (that's what Kodi does).
    LordOS doesn't use overlay planes yet (GPU.md, Pending).
  - **On the CPU:** `av_hwframe_transfer_data` to linear NV12 and convert. Good for 1080p; for 4K
    it's expensive.
- **H.264, VP9 and AV1 in software,** on the 4 Cortex-A76 cores (FFmpeg and dav1d have NEON):
  - H.264 1080p30/60 should work with 2–3 threads;
  - 4K H.264 isn't realistic;
  - AV1 1080p with dav1d, at the limit.

  Use `thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE` and `threads = 4`. With the Active Cooler,
  so it doesn't throttle.
- **On the Pi 4:** hardware H.264 (`h264_v4l2m2m`, V4L2 stateful, `/dev/video10`, in Debian's FFmpeg)
  and HEVC via `rpi-hevc-dec` as on the Pi 5.
- **What the media agent has to measure on the board:**
  - CPU and dropped frames with 1080p H.264, 1080p HEVC and 4K HEVC;
  - temperature (`vcgencmd measure_temp`);
  - throttling (`vcgencmd get_throttled`, it has to give `0x0`).

## Developing on the board

There are two possible systems on the board:

- **Raspberry Pi OS Lite 64-bit (Trixie):** the kernel and firmware are already there, and Mesa
  comes from the Raspberry Pi archive. The simplest way to start.
- **The LordOS image**, built with `RPI_SSH=1`.

### Steps

1. **SSH:**
   - with Raspberry Pi OS, enable it in Imager;
   - with LordOS, build with `RPI_SSH=1` and use the account created at first boot;
   - `ssh user@<ip>`; the IP shows up on the login screen or with `ip -4 addr`.
2. **Packages for compiling** (the same as in `VM-SETUP.md` plus Mesa):
   ```sh
   sudo apt-get install -y --no-install-recommends build-essential pkg-config libxkbcommon-dev libwayland-dev \
     wayland-protocols libgbm-dev libegl-dev libgles-dev libssl-dev libcrypt-dev xkb-data \
     libgl1-mesa-dri libegl1 libgles2 libgbm1 mesa-utils glmark2-es2-wayland
   ```
3. **The tree,** from the Mac (as for the VM; the themes are built on the Mac):
   ```sh
   npm run build
   COPYFILE_DISABLE=1 tar czf - --no-xattrs --exclude build --exclude node_modules \
     package.json README.md server third_party assets themes tools | \
     ssh user@<pi-ip> 'rm -rf ~/LordOS && mkdir ~/LordOS && cd ~/LordOS && tar xzf -'
   ```
4. **Compile on the board,** natively, no cross-compiling:
   ```sh
   cd ~/LordOS
   make -C server -j4           # ARCH=aarch64 is detected: NEON=1 and GPU=1 if the -dev packages are there
   make -C server test          # all headless tests, including build/canvas-test
   server/build/canvas-test     # NEON = C, and how long each one takes
   ```
5. **Test lord-server alone,** from a console (Ctrl+Alt+F2, with lordos.service stopped):
   ```sh
   cd ~/LordOS/server && sudo build/lord-server --user "$USER" 2> /tmp/lord.log
   ```
6. **Install it on the LordOS image:** `sh tools/iso/mkdeb.sh /tmp/lordos.deb` (on the board it takes
   `arm64` from `dpkg --print-architecture`) and `sudo apt install /tmp/lordos.deb`.
7. **Screenshots:**
   - LordOS's remote desktop (Settings → Sharing, port 5673; `REMOTE.md`) from the Mac's browser,
     only on the LAN and turning it off afterward;
   - or `grim` inside a Wayland session.

   There's no QEMU `screendump`: it's a board.

## Test checklist on the board

Each item, with a screenshot or the pasted output, in this document.

| # | Test | How | Expected |
|---|---|---|---|
| 1 | Boot | `systemd-analyze`, `systemd-analyze blame \| head` | SD: <25 s; NVMe: <12 s to the login screen |
| 2 | First boot | freshly written image, with and without `lordos-user.txt` | partition grown (`df -h /`); login screen or setup mode; the file deleted |
| 3 | RAM | `free -m`, lord-server RSS (`ps -o rss`) | idle desktop <400 MB total; lord-server ~35–60 MB (with 16K pages RSS goes up a bit) |
| 4 | Card and GPU | the log (see "What to verify") | `vc4`, `V3D 7.1`, `renderD128`, hardware cursor |
| 5 | GPU compositing FPS | `glmark2-es2-wayland` windowed and fullscreen; dragging windows; Launchpad | 60 FPS synced to vblank |
| 6 | CPU path | `lordos.gpu=0` | usable at 1080p; note ms per frame with `--profile` |
| 7 | Modifiers | `LORD_GPU_DEBUG=1`, `kms:` in the log | linear on the plane; UIF only for textures; no garbage on screen |
| 8 | Apps' dmabuf | gtk4-demo, foot, FeatherPad (Qt6), `vkcube` | they look right; `WAYLAND_DEBUG` shows renderD128 |
| 9 | 1080p video | H.264 and HEVC (once `lord-media` is there) | no dropped frames; CPU noted |
| 10 | 4K video | hardware HEVC 4Kp60 | smooth; temperature noted |
| 11 | Temperature | `vcgencmd measure_temp`, `vcgencmd get_throttled`, LordOS Monitor (`cpu_thermal` and the fan) | <75 °C with the Active Cooler under load; `throttled=0x0` |
| 12 | NEON | `server/build/canvas-test` | "NEON matches C"; NEON faster than C |
| 13 | Headless tests | `make -C server test` and each `build/*-test` | all pass, as on x86 (WASM: the full wg-2.0 suite) |
| 14 | App isolation | open a `.lpk`; `sandbox[...]:` in the log; `cat /sys/kernel/security/lsm` | seccomp=1 and Landlock if the kernel has it (the Raspberry Pi one might not) |
| 15 | Remote desktop | Settings → Sharing, `https://<ip>:5673` from the Mac | visible and controllable; screenshots for this document |
| 16 | Network and sound | Wi-Fi (brcmfmac), Ethernet, HDMI audio via PipeWire | IP from our own DHCP; `wpctl status` shows HDMI |
| 17 | Pi 4 | the same image | boots with `kernel8.img`; note what fails |

## Server portability audit (aarch64)

A review of `server/` on the Mac, without compiling for Linux ARM. The changed files were checked
with `clang -arch arm64 -fsyntax-only -Wall -Wextra` against the Linux 6.12 DRM headers and the
EGL/GLES/GBM ones. What follows is the result.

**There was nothing x86-specific:**
- no SSE/AVX intrinsics or asm in `server/src` (blending, blur and scaling are portable C, with
  32-bit SWAR);
- no `-march=native` in the Makefile.

**Fixed:**

| Where | What |
|---|---|
| `server/Makefile` | `ARCH` (from `$(CC) -dumpmachine`). On aarch64, `NEON=1` by default (`-DLORD_NEON`; `NEON=0` turns it off). QuickJS with `-ffp-contract=off`: GCC in GNU mode fuses `a*b+c` into FMA on ARM and would change JS numeric results; our code uses `-std=c11`, which already turns it off. `$(LD)` instead of `ld` for `pages.o` (cross-compiling). New test `build/canvas-test` |
| `server/src/canvas.c` | The rows of `canvas_blit` (without corners) and of `canvas_alpha_mask` (text, shadows) are separate functions, with a NEON version (4 pixels per iteration) under `LORD_NEON && __ARM_NEON`. Bit-for-bit identical to C with valid premultiplied sources: checked with `tests/canvas_test.c` under emulation (SIMDe) on the Mac, 4000 random rows. Still to do on the board |
| `server/src/ui/stb.c` | `STBI_NEON` on aarch64: stb's JPEG uses NEON (on x86 it already used SSE2) |
| `server/src/term/vt.c:890` | `src[i] >= 0 && …` with `char`: on ARM `char` is unsigned and `-Wextra` warns (`-Wtype-limits`). Now `(unsigned char)src[i] < 0x20`, the same on both architectures |
| `server/src/kms.c` `kms_dispatch` | the DRM event buffer aligned to 8 (it's read with a cast to `drm_event_vblank`, which has 64-bit fields) |
| `server/src/drmcard.h` (new), `main.c`, `remote/monitor.c` | pick the card with connectors instead of `card0` (see above) |
| `server/src/kms.c`/`kms.h`, `gpu/gpu.c` | the primary plane's `IN_FORMATS`, `kms_scanout_ok`, EGL's render node for the dmabuf feedback (see above) |
| `server/src/ui/js_sys.c` | `lord.system.info().cpu`: on ARM64 `/proc/cpuinfo` has no "model name". Now it comes from "CPU part" and `/proc/device-tree/model`: «Cortex-A76 · Raspberry Pi 5 Model B Rev 1.0» |
| `server/src/sec/sandbox.c` (fixed by the sandbox agent, on request) | `SYS_epoll_wait` without `#ifdef`: on aarch64 it doesn't exist and didn't compile. The repeated `SYS_read` was removed |

**Reviewed, no changes:**
- **Page size (16K on the Pi 5):** there's no hardcoded 4096 in `mmap`, shm or buffers.
  - Dumb buffers use `CREATE_DUMB`'s `size` and `pitch`.
  - WASM memory reserves and protects in multiples of 64 KiB (a multiple of 16K).
  - The Monitor uses `sysconf(_SC_PAGESIZE)` (`monitor/mon.c:443`).
  - The dmabuf feedback does `memfd` + `mmap` of an arbitrary size, which is valid.
- **Unaligned reads and endianness:**
  - the OpenType, MaxMind DB, xz, PNG/JPEG (stb), thbrk and IME readers read byte by byte in
    big-endian or with `memcpy`;
  - netlink attributes (`net/state.c`, `nl.c`, `wg.c`) are 4-aligned by the format.
  - Everything is little-endian, like x86.
- **Unsigned `char`:** the UTF-8 decoders and JSON escapes already use `unsigned char` (`mon.c`,
  `nl.c`, `snd.c`, `textlayout.c`, `json.c`, `wl.c`, `conn.c`); the other `char`s are only compared
  with ASCII.
- **Structures and ioctls:** `input_event` (24 bytes), `drm_mode_*` and `EVIOCG*` are the same on LP64
  (x86-64 and aarch64). There's no fbdev code.
- **`SYS_close_range` and `SYS_capset`** exist on aarch64.
- **WASM:** float→int conversions check the range before the cast, NaNs are handled by hand
  (`QNAN32/64`) and loads use `memcpy`. Run `build/wasm-test` on the board.
- **QuickJS 2025-04-26** supports aarch64.
- **`pkg/status.c`** already maps `aarch64` → `arm64` for dpkg and the Store.
- **Threads:** `PTHREAD_STACK_MIN` is 128 KiB on aarch64; the Monitor threads ask for 512 KiB, which
  is enough.

**Pending for the on-board stage** (risks, with file:line):

| Where | Risk |
|---|---|
| `server/src/main.c:488` (`mirror_frame(out.px…)` on the CPU path) | With remote desktop open, it reads from the mapped dumb buffer. On ARM that memory is write-combined/uncached and reading it is very slow; on x86 too, but less so. Better to mirror from `s->scene` + `wl_paint_fullscreen` to a buffer in RAM. Measure before touching |
| `server/src/main.c:484–498` (`canvas_copy`, line 486) | The CPU path copies the whole canvas to the dumb buffer (`canvas_copy`) every frame. At 4K that's 33 MB per frame, too much for the Pi's memory. Copy only the damage |
| `server/src/canvas.c` `box_pass`/`canvas_backdrop_blur` | The vertical pass walks by columns (`step = sw`), which is slow with the A76's cache. Candidate for NEON (4 columns at a time) or transposing; measure with `--profile` |
| `server/src/gpu/gpu.c` `gpu_formats` (`known[]`) | Only announces 32-bit RGB: the Pi's SAND128/NV12 video isn't announced (see Video) |
| `server/src/gpu/gpu.c` `gpu_device` | That EGL returns the render node with kmsro is unverified. If it returns nothing, it falls back to the first render node, which on the Pi is v3d |
| `server/src/kms.c` `read_scanout_formats` | `IN_FORMATS` with `UNIVERSAL_PLANES` untested on vc4. If it fails, it stays as before |
| `server/src/drmcard.h` | `GETCONNECTOR` forces an EDID read per card at startup (~50 ms). Hotplug: if HDMI is plugged in after boot, lord-sessiond has already chosen the card (vc4 anyway, because it has connectors) |
| `server/src/sec/sandbox.c` | seccomp: check on the board that no `.lpk` app dies from a syscall that arm64 glibc uses and x86 doesn't. Landlock: the Raspberry Pi kernel might not have it; `cmdline.txt` asks for `lsm=landlock,…`. The shared helper profile (`sandbox_apply_helper`) uses the same list and is fine on arm64, but has no `ioctl`: hardware video (V4L2 on the Pi, VA-API on the PC) inside the sandbox needs it (the sandbox agent was told) |
| `server/src/monitor/mon.c:703` | The Monitor's "model name" stays empty on ARM. The Monitor agent was told (`js_sys.c`'s `arm_cpu_name` would do) |
| `server/src/install/install.c:677` | The installer uses `grub-install --target=x86_64-efi`. On the Pi it doesn't apply (it's not live, the installer doesn't appear), but it's what would have to change for an ARM64/UEFI installer |
| page size | Some third-party program (not LordOS) assumes 4K: Widevine, box64, old jemalloc. If needed, `kernel=kernel8.img` in `config.txt` (4K, a bit slower) |

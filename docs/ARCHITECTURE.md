# LordOS architecture

LordOS is a **GNU/Linux distribution based on Debian 13** that ships a **desktop environment of
its own** (on the level of KDE or GNOME) with a **real display server and Wayland compositor
written from scratch in C**. It is not a simulator or a web page: it takes the
graphics card and input directly from the Linux kernel. The interface (desktop, bar, dock,
native apps) is written in **JavaScript with React**, which draws on an engine of our own, just
like React Native but on top of our display server.

## Layers

```
 LordOS themes and apps ── React + JSX + CSS-like styles
        │                 <View style={{flexDirection:'row', borderRadius:12}}>
        ▼
 lord-ui (C, ours) ───── QuickJS (JS engine, vendored)
        │                + our own React reconciler + our own flexbox layout
        │                + our own 2D rasterizer + text (stb_truetype)
        ▼
 lord-server (C, ours)   display server: DRM/KMS + evdev, windows, focus,
        │                composition, theme decorations
        │                + Wayland protocol → GTK (GNOME), Qt (KDE) apps,
        │                  Firefox, Electron; optional Xwayland for X11
        ▼
 Linux kernel (minimal Debian 13, generic kernel with DRM)
```

| Component    | Status      | Description                                                   |
|---------------|-------------|---------------------------------------------------------------|
| `server/src/` (kms, input, canvas) | **M1 done** | Display (KMS), input (evdev), 2D rasterizer |
| `server/src/ui/` | **M2 done** | Nodes, styles, flexbox, text, images, painting, damage, QuickJS |
| `runtime/`    | **M2 done** | React renderer (`@lordos/runtime`): reconciler, CSS → native, events |
| `themes/default/` | **M2 done** | Theme with a menu bar, a dock and an app launcher, with our own assets |
| `server/src/ui/js_sys.c` | **M3 done** | System APIs: fs (in `$HOME`), /proc, settings, themes, session |
| `sdk/` | **M3 done** | `@lordos/sdk`: windows, settings, tokens, controls, system hooks |
| `apps/` | **M3 done** | Apps as packages (`manifest.json` + `index.jsx`), independent of the theme |
| `themes/classic/` | **M3 done** | Second theme and template for creating themes |
| `server/src/term/` | **M4 done** | Our own terminal: xterm emulator, PTY, drawing, session holders (see `TERMINAL.md`) |
| `server/src/ui/otf.c`, `fonts.c` | **M4 done** | Our own OpenType (GSUB substitutions, CBDT emoji) and catalog of installed fonts: no HarfBuzz or fontconfig |
| `apps/terminal/` | **M4 done** | Native Terminal app (tabs, panes, profiles…) |
| `sdk/i18n.js`, `assets/locales/` | **M5 done** | Text, plurals and formats per language and region (see `LANGUAGES.md`) |
| `server/src/keyboard.c` | **M5 done** | Keyboard with libxkbcommon: every XKB layout, compose |
| `server/src/admin.c` | **M5 done** | Privileged helper with closed actions (region, time zone, language packs) |
| `server/src/ui/bidi.c`, `unicode.c`, `shape.c`, `textlayout.c` | **M5b done** | Full Unicode bidi, Arabic shaping and marks (GSUB/GPOS), per-script font fallback, CJK line breaking, mirrored interface (`direction`) |
| `server/src/ime.c`, `sdk/ime.js` | **M5b done** | Input methods: Korean, Japanese (SKK), Chinese (pinyin) |
| `server/src/app/`, `ui/js_broker.c`, `runtime/app-host.js` | **M8 done** | App mode: each `.lpk` as an isolated process (`lord-server --app <id>`), a Wayland client of the compositor, with a permission broker (see `PRIVACY.md`) |
| `server/src/sec/`, `ui/js_sec.c` | **M8 done** | Isolation of .lpk apps (Landlock+seccomp+cgroup), per-app permissions and Ed25519 signing (see `PRIVACY.md`, `SIGNING.md`) |
| `server/src/wl/`, `sdk/wayland.js` | **M6 done** | Our own Wayland compositor on libwayland-server: GTK and Qt apps as theme windows (see `WAYLAND.md`) |
| `server/src/tp/`, `wl/wl_input.c`, `js_input.c`, `sdk/trackpad.js` | **done (VM, uinput)** | Our own touchpad and touchscreen driver on evdev (no libinput): pointer acceleration, tap, finger clicks, palm rejection, kinetic scrolling, pinch, swipe; gestures and touches for Wayland apps (see `TRACKPAD.md`) |
| `server/src/doc/`, `js_doc.c`, `apps/pdf`, `apps/fonts`, `apps/characters` | **M9** | Documents: the sandboxed helper `lord-doc` (Poppler, libarchive, FreeType) and its apps: PDF Viewer, archives and Quick Preview in Files, Fonts, Emoji & Symbols (see `DOCUMENTS.md`) |
| `server/src/print/`, `js_print.c`, `sdk/printdialog.jsx`, `apps/scanner` | **M9** | Printing: our own IPP client against CUPS, our own mDNS, eSCL scanners, the system print dialog (see `PRINTING.md`) |
| `server/src/media/`, `js_media.c`, `sdk/media.js`, `apps/{photos,player,screenshots,camera,recorder}` | **M9** | Media: the sandboxed helper `lord-media` (FFmpeg, libva) and the Photos, Player, Screenshots, Camera and Recorder apps; `media` node, audio over shared memory with the PipeWire stream in lord-server, protected windows in screenshots, recording and remote desktop (see `MEDIA.md`) |
| `server/src/netperm/`, `server/src/fw/`, `js_fw.c` | **M8** | Per-app firewall: the root service `lord-netperm` enforces the rules with cgroup BPF in the sessions; Rule column in the Monitor and Trusted / Restricted apps in Settings (see `FIREWALL.md`) |
| `server/src/fan/`, `js_fan.c`, `sdk/cooling.jsx` | **M8** | Fans: the root service `lord-fand` drives them by temperature; Settings → Cooling and Monitor → Cooling (see `FANS.md`) |
| `server/src/monitor/`, `js_monitor.c`, `apps/monitor/` | **M8** | Monitor: CPU, memory, network, disks, processes and per-app connections with a map; its own thread only while the app is open, sock_diag and country by IP from a local database (see `MONITOR.md`) |
| `server/src/vault/`, `js_vault.c`, `sdk/vault.js`, `apps/passwords` | **M9** | Passwords and the system keyring: the sandboxed helper `lord-vault` (Monocypher: XChaCha20-Poly1305, Argon2id), own `.lvault` format, FIDO2/CTAP2 client, session keyring opened at login, Secret Service on D-Bus with own code (see `PASSWORDS.md`) |
| `server/src/net/netd.c`, `remote/netsup.c` | **M9** | `lord-netd`: the network belongs to the system. A root service started and supervised by `lord-sessiond` at boot, serving the `net-*` actions for every session (see `NETWORK.md`) |
| `server/src/disk/`, `js_disk.c`, `sdk/disks.js`, `apps/disks` | **M9** | Disks: listing without root (sysfs, udev database, mountinfo), our own SMART reader (ATA PASS-THROUGH over SG_IO, NVMe health log), closed `disk-*` actions, bootable USB verified with SHA-256 (see `SYSTEM.md`) |
| `server/src/mail/`, `js_mail.c`, `sdk/{mail,calendar,contacts}.js`, `apps/{mail,calendar,contacts,reminders}` | **M9** | Mail, Calendar, Contacts and Reminders: the sandboxed helper `lord-maild` (IMAP with IDLE/CONDSTORE, SMTP, CalDAV/CardDAV, OpenSSL) with no sockets of its own — `lord-server` connects only to configured servers and hands it the fd; own MIME, HTML sanitizer, iCalendar, vCard and XML parsers (see `MAIL.md`) |
| `server/src/backup/`, `js_backup.c`, `sdk/backup.js`, `apps/backup` | **M9** | Backups: hard-link snapshots in a lord-server thread (nice 19, idle I/O), hourly/daily/weekly retention, optional encryption with the vendored Monocypher; no root and no `admin.c` actions (see `SYSTEM.md`) |
| `server/src/logs/`, `js_logs.c`, `sdk/logs.js`, `apps/logs` | **M9** | Logs: our own systemd journal reader (no libsystemd), LordOS logs, system logs for admins through a closed action, live follow with inotify (see `SYSTEM.md`) |
| `server/src/ui/tzif.c`, `js_help.c`, `sdk/{notify,alarms,statusitems,help}.js`, `apps/{clock,help}` | **M9** | Clock (our own TZif reader, session alarms with the app closed), session notifications, menu-bar status items, Help (markdown articles in es/en/pt, welcome tour, Search source) (see `SYSTEM.md`) |
| `server/src/ui/grab.c`, `cursor.c`, `sdk/windowshell.jsx` | **M8** | Windows: moving and resizing in C (one event per frame, no JS), a layer for the window being moved, edges on all 4 sides and corners, our own cursors and `cursor-shape-v1` (see `WINDOWS.md`) |
| `server/src/outputs.c`, `kms.c`, `hw/display.c`, `ui/paint.c` (`paint_view`) | **M9** | Several displays (extend and mirror, arrangement by EDID identity, hotplug) and sharp HiDPI at 100–200 % per display: one logical space, each display painted at its own resolution; one `wl_output` per display, xdg-output, fractional-scale-v1 (see `DISPLAYS.md`) |
| `server/src/hw/`, `ui/js_hw.c`, `sdk/hardware.jsx`, `sdk/defaults.js` | **M9** | Settings' hardware pages: displays (EDID, live modes with a 15 s revert, per-display persistence, software rotation and 200 % scale, Night Light on the gamma LUT or by software), Bluetooth (BlueZ over our own D-Bus client, pairing agent), battery and power profiles, idle (DPMS, lock, sleep), date and time, notifications from Linux apps (`org.freedesktop.Notifications`) and default apps (`mimeapps.list`); kernel uevents and D-Bus signals, no polling (see `SETTINGS.md`) |

### Flow of a frame

1. An event arrives (evdev) → `hit_test` finds the node under the cursor → `__lordEvent` in JS.
2. React updates state → the reconciler calls `lord.setStyle/appendChild/...` → nodes are marked.
3. `lord-server` recomputes the layout, computes the damaged region and repaints only that region.
4. It copies the scene to the back buffer, draws the cursor, and page-flips on the next vblank.

## Dependency policy

Besides the kernel and libc, only these are allowed:

| Dependency          | Why                                                       | How       |
|----------------------|---------------------------------------------------------------|------------|
| kernel headers   | DRM / evdev ioctls                                            | system    |
| `libwayland-server`  | serializes the Wayland protocol that GTK/Qt speak (M6, in use) | system    |
| `libwayland-client`  | "app mode" (`lord-server --app <id>`): each isolated `.lpk` app is a Wayland client of the compositor (M8); same family and source package as `libwayland-server` | system    |
| `libxkbcommon`       | XKB keyboard layouts (M5) and what Wayland apps require (M6) | system    |
| Mesa: `libgbm`, `libEGL`, `libGLESv2` | GPU composition and dmabuf buffers from apps (games); optional: without a 3D GPU or without the `-dev` packages it builds and runs without Mesa (approved by the user on 2026-10-16, see `GPU.md`) | system |
| QuickJS              | ES2023 JavaScript engine, ~1 MB, written in C                  | vendored|
| `stb_truetype`, `stb_image` | fonts and images; public domain, a single header    | vendored|
| `stb_image_write`    | PNG and JPEG for the remote desktop (`lord-sessiond` only); public domain | vendored|
| OpenSSL (`libssl`, `libcrypto`) | TLS. **Only two binaries link it**: `lord-sessiond` (remote desktop on the LAN, `REMOTE.md`) and `lord-maild` (IMAP/SMTP/CalDAV/CardDAV, certificates checked against the system CA store, `MAIL.md`). Never `lord-server` | system |
| `libcrypt`           | verifying passwords against `/etc/shadow` (`lord-sessiond`, `admin.c`) | system |
| FFmpeg (`libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`) and `libva` (+ `libva-drm`) | Decoding and encoding video, audio and images; VA-API for hardware decoding (M9, approved by the user). **Only `lord-media` links them**, the sandboxed helper; the player, synchronization and interface are our own. Never `lord-server` (`MEDIA.md`) | system |
| Poppler (`libpoppler-glib`) and libarchive | PDF and archives (M9, approved by the user). **Only `lord-doc` links them**, the sandboxed helper; Poppler brings cairo, FreeType and fontconfig, which `lord-doc` also uses for font samples and for building PDFs. Never `lord-server` (`DOCUMENTS.md`) | system |
| `clang` (build time only) | the BPF programs of `lord-netperm` (`np.bpf.c`, `clang -target bpf`); they are embedded in the binary. Without clang (`BPF=0`) the service runs without BPF. Neither libbpf nor bcc is used | build machine |
| Monocypher 4.0.2 | XChaCha20-Poly1305, Argon2id/Argon2d, BLAKE2b, ChaCha20 for the Passwords vault and the KeePass importer (approved by the user; CC0/BSD-2, one C file). **Only `lord-vault` links it**; its official test vectors run in `make test` (`PASSWORDS.md`) | vendored |
| EFF word list, 10k common passwords | passphrase generator and the offline "breached" check (CC BY 3.0 US / MIT), embedded in `lord-vault` | `third_party/wordlists` |
| Inter, JetBrains Mono | interface and terminal fonts (OFL)            | `assets/fonts` |

React (`react`, `react-reconciler`) is a dependency **of the theme**, not of the system: it is bundled
with esbuild inside the theme's `.js`. Since 0.7.0 it is React 19.3.1 with `react-reconciler` 0.34
(the default theme weighs 367 KB with all the apps; React 18 was ~40 KB less). Node.js is only
used on the development machine.

Since M8 there are two programs, and since M9 two helpers:

- **`lord-netd`** (M9, root): the system network, started and supervised by `lord-sessiond` at boot;
  only the `net-*` actions of `admin.c`. libc only. See `NETWORK.md`.

- **`lord-sessiond`** (root, `lordos.service`): the login screen, the session list, the
  monitor and the remote desktop. See `SESSIONS.md` and `REMOTE.md`.
- **`lord-server`**: one session per user, always without a monitor of its own (`--headless`); the
  monitor and the browsers look at it through its socket.
- **`lord-netperm`** (root, `lord-netperm.service`): per-app network permissions. It attaches its
  cgroup BPF programs to `lordos.service` and reads each user's rules. No network of its own and no
  execution. See `FIREWALL.md`.
- **`lord-fand`** (root, `lord-fand.service`): fans by temperature from the first boot (the
  Raspberry Pi 5 fan header and motherboard PWM chips). No capabilities, no network, executes
  nothing; sessions pick a profile over `/run/lordos/fand.sock`. See `FANS.md`.

- **`lord-doc`** (M9): the documents helper. `lord-server` launches it when needed, with a
  fixed path (next to its binary) and no arguments. It runs as the user, without network and sandboxed
  (Landlock + seccomp), and works only with the fds `lord-server` passes it. It opens PDFs,
  archives, fonts and images that come from outside. See `DOCUMENTS.md`.
- **`lord-media`** (M9): the media helper, just like `lord-doc`: fixed path, no arguments,
  as the user, no network or files (only the fds `lord-server` passes it, including the
  camera and the render node), in `sandbox_apply_helper`. It decodes and encodes with FFmpeg; the
  frames come back through sealed shared memory and the audio goes to a ring whose PipeWire
  stream belongs to `lord-server`. See `MEDIA.md`.

- **`lord-vault`** (M9): the Passwords and keyring helper. Same rules as `lord-doc`/`lord-media`
  (fixed path, no arguments, minimal environment, `sandbox_apply_helper`: no network, no new
  sockets, no exec), but it lives for the whole session: it holds the decrypted vaults in an
  `mlock`/`MADV_DONTDUMP` arena, receives the vault folder, picked files, the FIDO2 hidraw device
  and the already-connected session D-Bus socket as fds, and serves the Secret Service. See
  `PASSWORDS.md`.

- **`lord-maild`** (M9): the Mail, Calendar, Contacts and Reminders helper. Same rules as
  `lord-doc` (fixed path, no arguments, minimal environment, `sandbox_apply_helper`), lives while
  the session does (it sends mail later, keeps IMAP IDLE and computes alarms with the apps closed).
  It speaks IMAP, SMTP, HTTP/WebDAV and TLS (OpenSSL) but **cannot create sockets**: it asks
  `lord-server` (`mail/mailc.c`) for a connection, and `lord-server` connects only to the servers
  of the accounts file (or, after *Load Images*, to public addresses for remote images) and passes
  the connected fd. Its only writable folder is its cache. See `MAIL.md`.

`lord-server` can still run on its own, with its own DRM and evdev, for development.

Data shipped with LordOS for the Monitor (`MONITOR.md`): `assets/monitor/geoip-country.mmdb.xz`
(DB-IP Lite Country, CC BY 4.0, read by our own MaxMind DB reader) and the Natural
Earth map (public domain). libmaxminddb is not used.

System data that is read (not linked): `xkb-data` (layouts), `libx11-data`
(only the compose tables in `/usr/share/X11/locale`), `locales` and `tzdata`.

System fonts (for example `fonts-noto-color-emoji` for emoji, or Fira Code) are
optional: if present, the Terminal offers them; HarfBuzz, FreeType and fontconfig are not linked.

Since M7, what LordOS does with its own code and what it delegates to a Debian program:

- **Our own code, no libraries:**
  - OpenPGP (RSA, Ed25519), SHA-256/512, xz, HTTP and the dependency resolver (`server/src/pkg`);
  - the sandbox, permissions and signing of `.lpk` apps (`server/src/sec`): Landlock,
    seccomp, cgroup v2, Ed25519 (signing and verifying) and a JSON reader, with no external libraries;
  - the WebAssembly interpreter (`server/src/wasm`);
  - the PulseAudio protocol client (`server/src/sound`);
  - netlink, DHCP and WireGuard (`server/src/net`);
  - the CUPS IPP client, mDNS/DNS-SD and eSCL (`server/src/print`, M9): no libcups, avahi-client
    or SANE.
- **Debian services spoken to over their protocol:** CUPS (IPP over
  `/run/cups/cups.sock`; it manages the queue and the drivers, see `PRINTING.md`).
- **D-Bus services (Settings, `SETTINGS.md`):** BlueZ (`bluetoothd`), power-profiles-daemon and
  systemd-timedated on the system bus, with our own D-Bus client (`vault/dbus.c`); lord-server also
  serves `org.freedesktop.Notifications` on the session bus. No libdbus, sd-bus or libbluetooth.
- **Debian programs** (with a fixed path, never linked, launched by the `admin.c` helper):
  - `dpkg`: unpacking and configuring;
  - `mkfs.vfat`, `mkfs.exfat`, `mkfs.ext4`, `mkfs.ntfs` (optional) and their label tools: formatting
    and renaming in Disks (M9, `disk-*` actions);
  - `wpa_supplicant`: Wi-Fi;
  - `openvpn`;
  - `resolvectl` and `networkctl`.
  - `powerprofilesctl`, `systemctl suspend`/`reload systemd-logind` and `timedatectl` (Settings'
    closed `power-*` and `time-*` actions, `SETTINGS.md`).
- **Sound server:** PipeWire, over the PulseAudio protocol.

apt, libapt, gpgv, curl, zlib, liblzma, NetworkManager, wireguard-tools, libpulse,
libpipewire, libcups, avahi-client and SANE are not used.

Forbidden: X11/Xorg, wlroots, libdrm, libinput, Mesa as a requirement (only as the optional GPU path), WebKit/Chromium,
GTK/Qt inside LordOS; bubblewrap, flatpak, firejail or libseccomp for isolation (it is
our own code using Landlock and seccomp-bpf directly). Vendored = copied as source code into `third_party/`.

## Platforms

| Architecture | Machines | Status | Image |
|---|---|---|---|
| x86-64 (amd64) | PC with UEFI or BIOS; AMD, Intel or Nvidia GPU, or no 3D GPU (CPU) | primary, tested on VM | live ISO with installer (`ISO.md`) |
| ARM64 (aarch64) | **Raspberry Pi 5** (official), Pi 4 (best effort); other ARM64 boards with KMS on Linux | ready, not tested on the board | SD/NVMe image, `tools/rpi/build-img.sh` (`RASPBERRY.md`) |

The code is the same. The differences:

- **Build:** the Makefile detects `ARCH` with `$(CC) -dumpmachine` and never uses
  `-march=native`.
  - On aarch64 the NEON loops in `canvas.c` (`NEON=0` turns them off) and `STBI_NEON` are enabled.
  - On x86-64 everything is portable C (stb uses SSE2 by itself).
- **Display and GPU:**
  - The card is chosen because it has connectors (`server/src/drmcard.h`), not because it is `card0`.
  - On ARM boards the display (vc4) and the GPU (v3d) are different devices. Mesa joins them
    (kmsro), lord-server advertises the GPU's render node to apps and only sends the display
    the modifiers the plane accepts (`IN_FORMATS`).
- **16K memory pages** (the Pi 5 kernel): nothing in LordOS assumes 4096.
- **Seccomp:** the allow list uses `#ifdef` for the syscalls arm64 doesn't have (`open`, `stat`,
  `epoll_wait`, `pipe`…).
- **Kernel:** on x86-64, Debian's; on the Raspberry Pi, the one from the Raspberry Pi archive (Debian
  13's doesn't handle the Pi 5). Mesa is Debian's on both.

## Why not a browser

WebKit or Chromium cost between 100 and 300 MB of RAM per process and are not our own.
LordOS's goal is to run on very low-end machines. With React on our own
engine, the theme writes JSX and styles, but it is rendered with a subset of CSS
(flexbox, colors, rounded borders, shadows, blur, transitions) that we do control.

## Theme model

A theme is a JS package that exports React components with a fixed contract:
`Desktop`, `MenuBar`, `Dock`, `Launcher`, `ControlCenter`, `WindowFrame` and its tokens
(colors, radii, shadows, fonts, wallpaper). The theme talks to the server only through
`@lordos/sdk` (`useWindows()`, `launch()`, `focus()`, `close()`…), never directly.
The default theme follows a familiar menu-bar-and-dock desktop layout, drawn with
our own icons and wallpapers; no third-party desktop assets are used or redistributed.

## JavaScript layers

```
themes/<theme>/  full desktop (bars, dock, window frames)               ─┐
apps/<id>/       content of each window, independent of the theme       ─┤ a single bundle
sdk/             @lordos/sdk: windows, settings, tokens, controls       ─┤ per theme
runtime/         @lordos/runtime: React renderer, CSS → native, events  ─┘ (esbuild)
```

The theme and the apps communicate through the token contract (`useTheme`) and the SDK's window
manager (`wm`). Switching themes discards the whole QuickJS context and loads the
other bundle; settings persist on disk.

## Security

- `lord-server` starts with sudo only to open DRM, evdev and the console; right away it switches to the
  session user (`SUDO_USER` or `--user`), keeping only `CAP_SYS_TTY_CONFIG`.
- `lord.fs` is read-only and rejects paths that (after `realpath`) leave `$HOME`.
- `lord.settings` only writes `~/.config/lordos/settings.json` (max. 64 KB, atomic write).
- `lord.themes.use` only accepts plain folder names in `themes/`; a theme that fails
  to load is banned to avoid loops.
- There is no API to run arbitrary commands. The only way to start a process is
  `lord.term.create()`, which launches the user's own login shell (the one in `/etc/passwd`)
  with no capabilities and no `SUDO_*` variables, just like any terminal emulator.
- The clipboard belongs to the session (in memory). Terminal programs can write it
  (OSC 52) but not read it.
- Manifest `permissions` are declarative until there is per-app isolation.
- External Wayland apps (M5) will only see their own surfaces.
- Documents that come from outside (PDFs, archives, fonts, scanner images) are opened
  only in `lord-doc`, sandboxed. A malicious file can bring that process down, but not
  `lord-server`. Extracting never writes outside the destination folder (`DOCUMENTS.md`, «Extracting
  without leaving the folder»).
- Media (videos, music, photos, the camera) are decoded only in `lord-media`, sandboxed and
  without the PipeWire socket. Screen, microphone and camera ask for permission (`sec/perms.c`, checked
  in C), and protected windows (`wm.setProtected`) come out black in screenshots, recordings and the
  remote desktop (`MEDIA.md`).
- Printers: managing queues (add, remove, default) is authorized by CUPS. Only
  users in the `lpadmin` group (the administrators) can do it, identified with PeerCred over
  the unix socket. There is no new root action in `admin.c` (`PRINTING.md`).

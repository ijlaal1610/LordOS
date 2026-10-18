# LordOS roadmap

Every milestone is verified on the `lordos-dev` VM (see `VM-SETUP.md`) with real
screenshots (QEMU `screendump`) and documented.

## M1 — Minimal display server ✅ (2026-10-15)

- [x] Our own modesetting with DRM ioctls (no libdrm), double buffering + page flip.
- [x] Our own input from evdev (no libinput): keyboard, relative mouse, absolute tablet, `EVIOCGRAB`.
- [x] Our own 2D rasterizer: alpha, antialiased rounded rectangles, shadows.
- [x] Test scene: wallpaper, menu bar, dock with magnification, draggable window, cursor.
- [x] Measured: 1280×800, full CPU render ≈ 13 ms/frame on the VM.

## M2 — UI engine with React ✅ (2026-10-15)

- [x] QuickJS 2025-04-26 vendored in `third_party/quickjs`, embedded in `lord-server`.
- [x] Native node tree (`View`, `Text`, `Image`) + styles (a CSS subset, see `themes/README.md`).
- [x] Our own flexbox layout (`server/src/ui/layout.c`).
- [x] Text with `stb_truetype` and the Inter font (OFL); PNG/JPEG images with `stb_image`.
- [x] React 18 renderer (`runtime/`, `react-reconciler` 0.29) that creates and updates native nodes.
- [x] Mouse events with bubbling (enter/leave/click), keyboard, timers, `requestAnimationFrame`.
- [x] "Lord Default" theme: menu bar with blur, dock with magnification and tooltip, draggable
      windows with traffic lights, Files/Settings/About apps, light/dark mode and live accents.
- [x] Cached shadows, reduced-resolution blur, damage tracking.

Measured on the VM (1280×800, CPU, no GPU):

| Situation | Render |
|---|---|
| First frame (full screen) | ~65 ms |
| Menu hover | 0.3–0.6 ms |
| Dock hover with magnification | 2–4 ms |
| Window drag | ~5 ms |
| Total server RAM (with React theme) | ~24 MB |
| Theme load (126 KB, React included) | ~28 ms |

Left for later: per-corner radii, `transform: scale`, text
input (keyboard → characters per layout), copying only the damaged region to the back buffer.

## M3 — SDK, apps as packages and swappable themes ✅ (2026-10-15)

- [x] Security: `lord-server` drops root after opening the display/input; it runs as the user with
      only `CAP_SYS_TTY_CONFIG` (verified in `/proc/<pid>/status`).
- [x] Native APIs: `lord.fs` (read-only, confined to `$HOME`), `lord.system` (/proc),
      `lord.settings` (`~/.config/lordos/settings.json`), `lord.themes`, `lord.quit`.
- [x] Input: right click, wheel, double click; keyboard with `latam` and `us` layouts,
      accents with dead keys, keypad.
- [x] `@lordos/sdk` (`sdk/`): window manager, persistent settings, token contract
      (`ThemeProvider`/`useTheme`), `TextInput`, `ScrollView`, controls, system hooks.
- [x] Apps as packages (`apps/<id>/manifest.json`): Files (real FS), Settings, About
      (real data), Calculator, Terminal (M4 notice).
- [x] "Lord Default" theme: app launcher with search, Control Center (live CPU/RAM),
      drop-down menus, context menu, 4 wallpapers of our own.
- [x] "Classic" theme as a template (taskbar + start menu, a single file).
- [x] Live theme switching from Settings, persistent, with protection against broken themes.

Measured on the VM: server RAM 24–26 MB with Lord Default, 11 MB with Classic; theme
switch ~30 ms + first frame ~60 ms.

Left over from M3 and moved to later: volume and brightness (no audio or backlight on the VM),
real per-app isolation by permissions, a development environment in the development machine's browser.

## M4 — Native terminal ✅ (2026-10-15)

Moved ahead of Wayland at the project's request: the LordOS terminal is our own, not a Linux
app. Full details, shortcuts and the parity table with popular terminal emulators in `TERMINAL.md`.

- [x] C engine (`server/src/term/`): xterm-256color emulator (VT500 parser, direct color,
      alternate screen, mouse, bracketed paste, synchronized output), scrollback with
      reflow, unprivileged PTY, drawing with per-row damage, hand-drawn boxes/blocks/powerline.
- [x] OSC 1337 extensions: inline images (`imgcat`), badges, marks, bash
      integration (OSC 133/7), Sixel, OSC 8 hyperlinks, OSC 52, OSC 9 notifications.
- [x] React app: tabs, split panes, profiles with 16 color schemes, transparency and
      blur, regex search, toolbelt, status bar, hotkey window (F12),
      copy mode, Instant Replay, triggers, automatic profile switching, saved
      arrangements, undo close, composer, autocomplete, command and paste history.
- [x] Platform: several windows per app, dynamic title, app menus in the theme's
      bar, confirmation on close, arguments when opening apps, window resizing,
      `<Terminal>` component, session clipboard, `Stepper` and `Slider`.

- [x] Parity with popular terminal emulators completed: font choice and "non-ASCII" font,
      ligatures (our own GSUB engine, `server/src/ui/otf.c`), color emoji (CBDT), combining
      characters and graphemes (mode 2027), zsh and fish integration, configurable shortcuts and
      keys, profile switching by user@host, highlight/mark/title triggers,
      sessions that survive a server restart (holders, `term/hold.c`),
      `tmux -CC` with native tabs and panes, and **profile import from other terminal emulators**
      (JSON, `.itermcolors`, XML and binary plist).

Measured on the VM: `seq 1 1000000` in 2.1 s with transparency; server RAM ~39 MB with
3 terminals, ~43 MB with 6 sessions + 3 tmux panes + emoji; ~2.2 MB per holder.
What is left out (background image, COLR/SVG emoji, GPOS) is in `TERMINAL.md`.

## M5 — Language and region (system-wide) ✅ (2026-10-15)

Details in `LANGUAGES.md`. It comes before Wayland so GTK/Qt apps receive the same language.

- [x] Per-user system region (`~/.config/locale.conf`) inherited by the session and the
      shells; Debian messages (gettext) in the chosen language (verified with `ls`, `date`, `printf`).
- [x] Interface: catalogs per namespace (`assets/locales/<language>/*.json`), `t()`/`useLocale()`,
      plurals (our own CLDR rules), dates and numbers per region; live switching without losing
      windows or terminals; English fallback. 391 keys.
- [x] Spanish, English and Portuguese complete; `tools/i18n-check.mjs` and `tools/i18n-pack.mjs`;
      test French pack installed from Settings.
- [x] Language packs: LordOS catalogs (user, no root) + glibc region + the language's Debian
      packages (manpages, hunspell, aspell, hyphen, mythes, fonts for the script).
- [x] Keyboard with libxkbcommon + xkb-data: every layout and variant, up to 4 at
      once, dead keys and compose.
- [x] System time zone and region from Settings.
- [x] Minimal privileged helper (`server/src/admin.c`) with closed actions, approved.

## M5b — Complex scripts ✅ (2026-10-15)

Details in `LANGUAGES.md` → "Complex scripts". No HarfBuzz, fribidi, ICU, ibus or fcitx.

- [x] Full Unicode bidi (UAX #9): 91,707/91,707 cases of `BidiCharacterTest.txt`.
- [x] Arabic contextual forms, ligatures, marks, cursive (nastaliq), Indic shaper
      (9 scripts) and Thai/Lao: 217/217 sentences identical to HarfBuzz.
- [x] Per-script font fallback in the interface and the terminal (Noto; CJK by language).
- [x] Line breaking for Chinese and Japanese.
- [x] Mirrored interface for RTL languages (`direction` as in CSS, `textAlign` start/end),
      tested in Arabic with both themes.
- [x] Input methods: Korean (hangul), Japanese (romaji → kana + SKK, phrase
      conversion with segments), Chinese (pinyin with abbreviations); in the terminal and in text fields.
- [x] Terminal: Arabic and Hebrew in visual order and shaped complex scripts (per-profile option).
- [x] Khmer and Myanmar shapers and the Universal Shaping Engine (Tibetan, Sinhala, Balinese,
      Javanese, Mongolian and ~80 more scripts): with the above, 409/409 sentences identical to HarfBuzz
      (2026-10-16).
- [x] Thai word-based line breaking with the libthai-data dictionary and our own
      reader (identical to libthai on 705/705 test paragraphs); Khmer and Myanmar by syllable (2026-10-16).
- [ ] Pending: Lao by words, vowel constraints with ◌, Japanese with frequencies.

## M6 — Linux app compatibility (Wayland) ✅ (2026-10-16)

Details in `WAYLAND.md`.

- [x] `libwayland-server` + `wl_compositor`, `wl_shm`, `wl_subcompositor`, `xdg_shell`
      (windows and popups), `wl_seat` (pointer and XKB keyboard), `wl_output`.
- [x] GTK4 (own frame) and Qt6 (theme frame) app windows inside both themes:
      title, focus, move, resize, maximize, minimize, close, Dock.
- [x] Clipboard shared with LordOS in both directions, `xdg-decoration`.
- [ ] Launching `.desktop` apps from the app launcher (needs an approved exception to "the server
      doesn't execute commands"), `linux-dmabuf`, app cursors, drag and drop,
      `text-input`, Xwayland.

## M7 — Apps, packages and system services ✅ (2026-10-16)

Defined with the user on 2026-10-16: our own store (Debian apps and native LordOS apps),
our own installer, and network, VPN, sound and clipboard with our own interface.

- [x] Launcher for installed Linux apps (`.desktop`) in the app launcher, the start menu and the
      Dock, with the approved `lord.apps.launch(id)` exception (`WAYLAND.md`, "The launcher").
- [x] Installable native `.lpk` apps (no root), with React and the SDK shared with the theme,
      `<Image pixels>` and `loadWasm` (`NATIVE-APPS.md`); example `examples/apps/fractal`.
- [x] Our own WebAssembly interpreter and the `WebAssembly` global (`WASM.md`): 26,290/26,290
      assertions of the official wg-2.0 suite; 3–5× faster than the same code in JS.
- [x] Our own Debian package engine (`PACKAGES.md`): our own OpenPGP (RSA/Ed25519), xz, HTTP and
      resolver, `dpkg` at the end; plans identical to apt in 12 cases; orphans.
- [x] Store: Debian apps by category (AppStream), search, plan before installing or
      removing, updates, local `.deb` and `.lpk`.
- [x] App launcher and start menu with categories (freedesktop), AppStream icons for apps
      that only ship SVG.
- [x] Settings grouped as in TangoOS (search, user, General, Network, Sound,
      Updates…).
- [x] Clipboard: images between Linux apps that survive the source app, history (❖V).
- [x] Sound on PipeWire with our own client (`SOUND.md`): Control Center and Settings.
- [x] Our own networking (netlink, DHCP, Wi-Fi with wpa_supplicant) and VPN (WireGuard over our own netlink,
      OpenVPN) in Settings → Network (`NETWORK.md`).
- [x] React 19.3.1 (`react-reconciler` 0.34): adapted renderer (`runtime/index.js`), tested with
      both themes, live theme switching, installed apps (.lpk) and Linux apps. lord-server: 34 MB RSS.
- [x] Files (`FILES.md`): tabs, four views, freedesktop
      Trash, Quick Preview, Get Info, background copy/move (`fsops.c`),
      drag and drop, tags, search, mounted disks and «Open With» (`launchWith`).
- [ ] Left over from M7: OpenConnect and IPsec (strongSwan), WPA3/Enterprise, real per-app
      permissions, a signed LordOS app repository.
- [x] `flexWrap` in our own layout engine (`layout.c`).
- [x] Notes: sticky notes on the desktop (widgets), markdown with reading view and index, images,
      attachments and emoji, saved automatically in `~/Notes` (`NOTES.md`).

## M8 — Session and system

- [x] Our own session: `lord-sessiond` with a login screen, lock (❖L), fast user
  switching, parallel sessions and Settings → Users (`SESSIONS.md`).
- [x] Remote desktop on the LAN from the browser (port 5673, off by default;
  `REMOTE.md`). Pending: audio (stage 2, designed).
- [x] Privacy and signing of `.lpk` apps (`PRIVACY.md`, `SIGNING.md`):
  - [x] Our own sandbox (`server/src/sec/sandbox.c`): Landlock (ABI≤6), seccomp-bpf (allow
    list), no network without permission, `no_new_privs`, dropped capabilities and per-app cgroup v2;
    tested on the VM (`build/sandbox-test`, 7/7). No bubblewrap/flatpak/firejail.
  - [x] per-app permissions: prompts drawn by the system, decisions stored where
    the app can't write (`permisos.json`), Settings → Privacy and the green/orange
    camera/microphone dots in both themes.
  - [x] Our own Ed25519 signing (SHA-256/Ed25519, verify and sign), `contenido.json` with hashes,
    trust levels, signed catalog with revocation, and «Modified by you» / «Restore
    original» / «Share my version». Tests `build/sign-test` (55/55, RFC 8032 + NIST) and
    `build/pkgstate-test` (11/11).
  - [x] `tools/lpk.mjs`: `keygen`, signing when packaging, `sign`, `verify` and `catalog`.
  - [x] Each `.lpk` as an isolated process (`lord-server --app <id>`): a Wayland client with the
    theme's frame, in the Dock and the remote desktop; a broker that checks every `lord.*`; system
    prompts requested by the app. The malicious app (`tests/lpk-evil`) fails at everything at
    run time; the legitimate one (`tests/lpk-legit`) works after consent.
  - [x] Per-session delegated cgroup (`Delegate=yes`, `lord-sessiond` in the `supervisor` leaf):
    `app-lordos-lpk-<id>.scope` and `app-lordos-<id>.scope` (Linux apps) under
    `lordos-session-<uid>`; tested with fast user switching and remote desktop.
  - [x] `sandbox_apply_helper` hardened for lord-media and lord-doc (no sockets or exec, threads
    only, TSYNC, folders by fd, ioctl by class, our own kcmp): `sandbox-test helper` 17/17.
  - [ ] Pending: graphical powerbox (a dialog that hands over the fd), the camera fd and PipeWire
    stream to the app, live tokens/language/RTL and menus for the isolated app.
- [x] **Per-app firewall**, steps 1 to 4 (`FIREWALL.md`), tested on VM 122:
  - [x] rule store, precedence, `dnsmap` and JSON (`server/src/fw`, step 1);
  - [x] `lord-netperm`, the root service that enforces the rules with cgroup BPF (connect, sendmsg,
    bind, sock_create) in the sessions' subtree, with our own loader (no libbpf), rules
    compiled to LPM/hash maps (`build/npcompile-test`: 480,000 connections against `fw_decide`,
    0 differences) and names only from the system resolver (systemd-resolved monitor);
  - [x] kernel test `tests/netperm_integration.sh` 18/18: deny/allow, "only
    example.com", LAN only, no incoming, no network, off;
  - [x] Monitor: per-app Rule switch and allowed/denied counters; Settings →
    Privacy: mode, Trusted apps and Restricted apps;
  - [x] root-owned scopes: `lord-sessiond` places every app process and a Linux app can't move
    itself to another cgroup (VM 122). Still open: `systemd-run --user --scope` (PID 1 moves it
    into `user@<uid>.service`).
  - [ ] "ask" mode (designed), per-destination switches.
- [x] **Monitor** app, phase 1 (`MONITOR.md`), tested on VM 123:
  - [x] Overview, CPU, Memory, Network, Disks and Processes tabs with the look of TangoOS's
    Monitor;
  - [x] metrics read by `lord-server` in its own thread, at 4 Hz and only while the app is open
    (closed, it costs nothing);
  - [x] per-app connections with sock_diag, from process to app through the `.desktop` files;
  - [x] our own vector map with zoom and arcs, in the style of a per-app network monitor;
  - [x] country by IP with our own MaxMind DB reader and DB-IP Lite (CC BY 4.0), no network;
  - [x] item in Settings → Privacy.
  - [x] Phase 2, firewall: see "Per-app firewall" above.
  - [ ] Phase 2, the rest: connection alerts ("ask"), camera and microphone in use (`monitor.deviceUsers()`), host names without
    reverse DNS, history and usage alerts.
- [ ] GPU sessions (today they are drawn on the CPU), VT switching and multiple monitors.
- [ ] More native LordOS apps in React (text editor, image viewer, browser).
- [ ] Installable image based on minimal Debian 13, with LordOS as `.deb` packages.
- [ ] **ARM64 / Raspberry Pi 5 as an official platform** (`RASPBERRY.md`):
  - [x] Portability audit of `server/` for aarch64: unsigned char, 16K pages,
    arm64 seccomp, alignment and `ARCH` in the Makefile (no `-march=native`).
  - [x] Optional per-architecture NEON in `canvas.c` (blit and masks), bit-identical to C
    (`build/canvas-test`); `STBI_NEON`.
  - [x] Separate display and GPU (vc4 + v3d):
    - the card with connectors (`drmcard.h`), in lord-server and lord-sessiond;
    - `IN_FORMATS` of the primary plane and direct scanout only with accepted modifiers;
    - the EGL render node in the dmabuf feedback.
  - [x] `tools/rpi/build-img.sh`: `lordos-<version>-rpi5-arm64.img.xz` for Raspberry Pi Imager:
    - Debian 13 arm64 with the Raspberry Pi kernel and firmware;
    - the same `.deb` as the ISO;
    - the partition grows by itself;
    - first boot with `lordos-user.txt`/`userconf.txt` (login screen) or setup
      mode.
  - [x] LordOS build of the Raspberry Pi kernel with Landlock (`tools/rpi/build-kernel.sh`,
    `6.18.57-lordos-rpi-2712`), pinned over the archive kernel.
  - [x] Switch files on the boot partition: `ssh`, `remote-desktop`, Wi-Fi in `lordos-user.txt`;
    `tools/rpi/verify-board.sh` after flashing.
  - [ ] Run `build-img.sh` and test on the 16 GB Pi 5 (test list in `RASPBERRY.md`,
    with screenshots through the remote desktop).
  - [ ] Hardware video on the Pi 5 (HEVC via V4L2 stateless; H.264 in software) in
    `lord-media`.
  - [ ] A «create user» step on the login screen, instead of setup mode.
  - [ ] Pi 4 (best effort).

## M9 — Documents and printing ✅ (2026-10-17)

Details in `DOCUMENTS.md` and `PRINTING.md`. Tested on VM 116 with screenshots (`archives-*`, `pdf-*`, `print-*`, `fonts-*` and related `.png` files).

- [x] **`lord-doc`, the sandboxed helper:**
  - it is the only binary with Poppler (glib interface) and libarchive;
  - it is launched on demand with a fixed path and no arguments, and works only with the fds
    lord-server passes it;
  - it locks itself in with `sandbox_apply_helper`: no network or sockets, Landlock with writes only
    under the destination, seccomp, cgroup and a memory cap;
  - pages come back in sealed memfds;
  - the compositor never waits: there are deadlines, and if the helper crashes nothing else happens.
- [x] **PDF Viewer:**
  - continuous and single-page scrolling, thumbnails, outline;
  - search with highlighting, zoom and fit, rotate, select and copy, go to page, links;
  - annotations (highlight, note, freehand) saved as a copy, forms;
  - inverted dark page;
  - print.
- [x] **Files:**
  - looking inside archives as if they were a folder;
  - Extract Here (zip, tar.gz/xz/zst/bz2, 7z, bare gz) without leaving the folder, with
    `arcpath-test` and malicious archives: 31/31;
  - Compress to `.zip`;
  - Quick Preview for PDFs, fonts, archives and what is inside an archive;
  - Quick Preview for audio and video with `media.poster` when available.
- [x] **Fonts:** samples, character table by block, information, installing and removing
  user fonts.
- [x] **Emoji & Symbols:**
  - global picker (❖.) with search in es/en/pt (CLDR), categories, recents and skin tone;
  - it types through the input method path;
  - an app with Unicode symbols by block.
- [x] **Printing:**
  - our own IPP client for CUPS (no libcups), our own mDNS;
  - Settings → Printers & Scanners: add driverless printers (IPP Everywhere), manual URI,
    default, queue and cancel, administration through `lpadmin` + PeerCred;
  - the system print dialog with preview, used by the PDF Viewer, the Editor and
    Notes (Photos has the API ready).
- [x] **Scanner:** our own eSCL client, no SANE: preview, area, resolution, color, and
  saving as PDF/PNG/JPEG. Tested with a test scanner (`tests/escl_server.py`).
- [ ] **Pending:**
  - PDF thumbnails in icon view;
  - encrypted archives;
  - PDF signatures;
  - scanners and `ipps` over TLS from lord-server;
  - complex-script shaping in printed text;
  - ZWJ emoji in the text engine;
  - testing with real printers and scanners.

## M9 — Native media apps ✅ (2026-10-17)

Details in `MEDIA.md`. Tested on VM 119 with screenshots (`photos-*`, `music-*`, `video-*`, `recorder-*`, `screenshots-*` and related `.png` files).

- [x] **`lord-media`, the sandboxed helper:**
  - it is the only binary with FFmpeg (libavcodec, libavformat, libswscale, libswresample) and libva;
  - it is launched on demand with a fixed path and no arguments, and works only with the fds
    lord-server passes it (files, camera, render node);
  - `sandbox_apply_helper`: no sockets, exec or network, ioctl by class (DRM, V4L2) only when
    needed, TSYNC; `media-test sandbox` verifies it;
  - frames come back through three sealed shared-memory slots; if it crashes, only the
    window finds out (tested with a real segfault).
- [x] **Audio over shared memory:** the PipeWire stream belongs to lord-server; the PCM goes through a
  ring and the video clock through a seqlock on the same page (no round trips). Mean A/V offset
  0.5–1.9 ms. The microphone, the other way around.
- [x] **Player:**
  - video with synchronized audio, time bar, volume, `.srt` and in-track subtitles;
  - fullscreen and picture in picture (`wm.setFullscreen`/`wm.setPip` in the SDK);
  - music library by artist and album with cover art, queue, shuffle and repeat;
  - media keys and the Control Center tile.
- [x] **Photos:** photo library from `~/Imágenes` with thumbnails in `~/.cache/lordos/thumbs`, viewer with zoom,
  panning, rotation, fullscreen, slideshow, animated GIF, HEIC/AVIF/WebP/BMP, basic
  editing saved as a copy, and printing.
- [x] **Screenshots:**
  - screen, window or area, and a timer;
  - to the clipboard or to `~/Imágenes/Capturas`;
  - floating thumbnail and annotation editor;
  - screen recording to MP4 (H.264 + AAC from the microphone);
  - shortcuts ❖⇧3/❖⇧4/❖⇧5 and Print Screen.
- [x] **Protected windows** (`wm.setProtected`): black in screenshots, recordings, the remote
  desktop and thumbnails; also Wayland apps and their menus.
- [x] **Camera:** our own V4L2, JPEG photo, H.264 + AAC video, mirror, camera choice (tested with
  v4l2loopback).
- [x] **Recorder:** microphone over PipeWire, waveform, rename, trim and export to Opus,
  MP3 or WAV.
- [x] **Files:** «Open With» Photos and Player, video thumbnails, cover art and WebP/HEIC/AVIF;
  `media.poster` for Quick Preview.
- [x] **Performance:** painting straight from the video when only an opaque frame changed: lord-server with
  a 1080p video went from 42 % to 13 % of a core windowed and from 92 % to 15 % fullscreen.
- [ ] **Pending:**
  - measure VA-API on the AMD PC (the VM has no driver);
  - zero-copy dmabuf → EGL on the GPU path;
  - HEVC via `v4l2request` on the Raspberry Pi 5;
  - image-based subtitles;
  - recording an area and the system audio;
  - saving as WebP.

## M9 — System apps and system network ✅ (2026-10-18)

Details in `SYSTEM.md` (apps), `NETWORK.md` (network) and `SESSIONS.md` (user folders). Tested on VM 119
with screenshots (`backup-*`, `disks-*`, `logs-*`, `clock-*`, `help-*` `.png` files).

- [x] **The network belongs to the system (`lord-netd`):** a small root service started and
  supervised by `lord-sessiond` at boot. DHCP, saved Wi-Fi, saved configs and DNS come up before
  anyone logs in (IP on the greeter, remote desktop reachable); sessions forward their `net-*`
  actions over `/run/lordos/netd.sock`. It survives two users logging in and out, its own crash
  (the DHCP lease is resumed without dropping the address) and a `lord-sessiond` restart (adopted
  through a pidfd).
- [x] **User folders:** the installer and Settings → Users create Escritorio, Documentos,
  Descargas, Música, Imágenes and Videos in the user's language plus `user-dirs.dirs`
  (`users/homedirs.c`, with `build/homedirs-test`).
- [x] **Disks** (`apps/disks`): disks and partitions with usage, our own SMART reader (ATA over
  `SG_IO`, NVMe health log; `build/smart-test`), mount/unmount/eject, format FAT32/exFAT/ext4/NTFS,
  rename, erase, bootable USB from an ISO/IMG verified with SHA-256; closed `disk-*` actions that
  never touch the system disk.
- [x] **Backups** (`apps/backup`): scheduled hard-link snapshots of the home, hourly/daily/weekly
  retention, browse by date and restore, exclusions, optional encryption with Monocypher; a
  lord-server thread at nice 19 and idle I/O, paused on battery (`build/backup-test`).
- [x] **Logs** (`apps/logs`): our own systemd journal reader, the user's own journal and LordOS
  logs, system logs for admins through a closed action, filters, search, live follow and export
  (`build/journal-test`).
- [x] **Clock** (`apps/clock`): world clock with our own TZif reader (`build/tzif-test`), alarms that
  fire with the app closed, timer and stopwatch, a menu-bar status item.
- [x] **Help** (`apps/help`): searchable markdown articles in es/en/pt, a skippable welcome tour on
  first login, links to Settings pages, indexed by Search.
- [x] **Session notifications** (`sdk/notify.js`) and **menu-bar status items**
  (`sdk/statusitems.js`) for any theme.
- [ ] **Pending:**
  - install a new ISO to disk and repeat the network-before-login and user-folder checks there;
  - test `lord-netd` with real NICs and Wi-Fi; a per-VPN "connect at boot" option;
  - Disks: GPT for disks over 2 TiB, manual partitioning, LUKS/LVM, real NVMe;
  - Backups: test pausing on a real laptop battery, store the passphrase in the Passwords keyring;
  - Logs: zstd-compressed journal fields;
  - RTL screenshots of the five apps.

## M9 — Passwords and the system keyring ✅ (2026-10-18)

Details in `PASSWORDS.md`. Tested on VM 116 with a real `lord-sessiond` session
(`passwords-*.png`).

- [x] **`lord-vault`**, sandboxed helper (no network, no sockets, no exec, fd-only files); the only
  binary with Monocypher; decrypted data in an mlock/MADV_DONTDUMP arena, wiped on lock.
- [x] **Own `.lvault` format:** XChaCha20-Poly1305, vault key wrapped per unlock method (Argon2id
  tuned per machine, BLAKE2b), attachments sealed separately, atomic save with `.bak`; every flipped
  byte and every truncation detected (`vault-test`, 74/74).
- [x] **Unlock:** master password, FIDO2 `hmac-secret` key (own CTAP2 client), key file on a USB
  stick, password + key file, printable recovery key. Auto-lock on idle, ❖L and user switch.
- [x] **App:** sidebar (All, Favorites, Archived, 2FA codes, Wi-Fi, Secure
  notes, Cards, Security), KeePassX-style category tree with 16 built-in templates and user
  categories with their own fields, tags, history, trash, attachments, rotation reminders, Wi-Fi QR,
  TOTP with countdown (also from a QR image), generator, security report (offline breach check),
  import (own KDBX 4/3.1 reader, Bitwarden, Chrome, Firefox, CSV from other password managers) and export. Protected window.
- [x] **Clipboard:** secrets marked sensitive (out of the history, `x-kde-passwordManagerHint` for
  Wayland clients), cleared after 30 s.
- [x] **Session keyring** (`login.lvault`) opened with the login password via `lord-sessiond`;
  `keyring` API per app with sharing; Settings → Network stores Wi-Fi/VPN secrets there;
  `<AutofillButton>`.
- [x] **Secret Service** with our own D-Bus code (plain + DH sessions): `secret-tool` and Chromium
  work without gnome-keyring.
- [x] **The Passwords UI runs as its own sandboxed process** (`lord-server --app passwords`, a
  system package built into `assets/sysapps/`): its own channel to `lord-vault`
  (`vc_attach_app`), files through the powerbox, FIDO2 and secret clipboard through the broker
  (`BOP_VAULT_FIDO`, `BOP_CLIP_SECRET`), protected Wayland window. Revealed values never enter
  `lord-server`'s JavaScript heap. In-process fallback kept (see `PASSWORDS.md`).
- [ ] Keyring for isolated `.lpk` apps: a broker op that sets the owner from the verified app id.
- [ ] Test with real FIDO2 keys (only the uhid software authenticator was available).

## M9 — Settings: hardware pages ✅ (2026-10-18)

Details in `SETTINGS.md`. Tested on VM 118 (`settings-*.png`), Bluetooth with a virtual controller
(`btvirt`).

- [x] **Displays:** outputs, resolutions and refresh rates from KMS with EDID names; live modeset
  with the system's 15-second "Keep these display settings?"; saved per display (EDID identity) and
  used from the first frame; main display choice and move on unplug (DRM uevents); software rotation
  and a doubled 200 % scale (CPU path); brightness through a closed action; Night Light on the gamma
  LUT or by software, scheduled sunset-to-sunrise from the time zone's coordinates or custom hours.
- [x] **Bluetooth:** BlueZ over our own D-Bus client: power, discovery, pair (system-drawn PIN,
  passkey and confirmation prompts), connect, disconnect, forget; headsets in Sound through
  PipeWire.
- [x] **Energy:** battery state, time and health from sysfs, low-battery notices, percentage in the
  menu bar, power profiles (power-profiles-daemon or the cpufreq governor), display off / lock /
  sleep after N minutes, power button and lid (logind drop-in).
- [x] **Notifications:** per-app allow, style, sound; Do Not Disturb with a schedule; grouping;
  `org.freedesktop.Notifications` for Linux apps; packages use the sandbox permission.
- [x] **Date & Time:** NTP (timesyncd) or manual time, manual time zone (never from the network),
  12/24 h, menu bar clock format.
- [x] **Default Apps:** `mimeapps.list` and `xdg-terminals.list`; Files' "Open With" and
  `lord.apps.launchWith('', path)` honor them.
- [ ] **Pending:** extend/mirror across displays; sharp HiDPI and 125/150 %; Night Light on the GPU
  path without a LUT; real laptops (battery, backlight, lid) and real Bluetooth controllers;
  notifications on the lock screen and a notification center; Wayland idle inhibit.

## M9 — Mail, Calendar, Contacts and Reminders ✅ (2026-10-18)

Details in `MAIL.md`. Tested on VM 123 against local Dovecot, Postfix and Radicale with a test CA
(`mail-*.png`, `calendar-*.png`, `contacts*.png`, `reminders*.png`).

- [x] **`lord-maild`**, sandboxed helper with no sockets of its own: `lord-server` connects only to
  the configured servers (or public image hosts after *Load Images*) and passes the fd. OpenSSL,
  certificates verified against the system CA store, explicit per-account trust otherwise.
- [x] **IMAP** (IDLE, CONDSTORE, MOVE, UIDPLUS, SPECIAL-USE, modified UTF-7), **SMTP** 465/587,
  local cache and offline prefix search; a 10,000-message folder syncs in ~2 s and opens from the
  cache in ~20 ms; virtualized list.
- [x] **Mail app:** three panes, toolbar, unified inbox, conversations, flags, six categories (IMAP
  `$label1…6`), attachments through the system's default apps, signatures, rich text / markdown /
  plain compose, server drafts, send later, new-mail notifications.
- [x] **HTML** through our own sanitizer into native views (no web engine), remote images blocked
  until asked, tracking pixels dropped, deceptive links flagged; plain-text fallback.
- [x] **Accounts:** any IMAP/SMTP; popular providers (Gmail, Yahoo and others) from our own table; app
  passwords in the keyring; OAuth prepared and disabled ("needs your client ID").
- [x] **Calendar** (day / week / month, colored calendars, local and CalDAV, recurrence),
  **invitations** from Mail (Accept / Maybe / Decline with iTIP REPLY), **Reminders** (lists, due
  dates, priorities, flags, smart lists, VTODO over CalDAV), **Contacts** (local and CardDAV,
  groups, photo; Mail autocompletes from it). Alarms ring with the apps closed.
- [x] **Tests:** `mail-test` 30,953 checks (IMAP, MIME, charsets, HTML sanitizer, iCal, vCard, XML,
  mutation fuzzing), `mailnet-test` 138, `mailc-test` 69; ASan/UBSan target.
- [ ] Real providers (Gmail, Yahoo and the others in the table) and OAuth2 with a registered client ID.
- [ ] Edit a single occurrence of a repeating event; create calendars/address books on the server.
- [ ] CJK multibyte charsets; S/MIME and OpenPGP; QRESYNC.

## Pending before the final ISO

- [x] **Mail, Calendar, Contacts and Reminders** (see "M9 — Mail, Calendar, Contacts and
  Reminders" above and `MAIL.md`).
- [x] **Missing Settings sections:** Displays, Bluetooth, Energy, Notifications, Date &
  Time, Default Apps (see "M9 — Settings: hardware pages" above and `SETTINGS.md`).
- [x] **Chromium and Firefox** preinstalled (Linux apps, not native; verified in the final ISO, see
  `docs/ISO.md`, "Final ISO test").
- [x] **Camera in isolated apps** (`.lpk`) and **several windows per isolated app** (VM 122,
  `PRIVACY.md`): read-only frame ring from `lord-media` with validated ACKs, photo/video through
  the powerbox, instant revoke; one Wayland toplevel per window, grouped in the Dock, «New
  window» from the app and the Dock.
- [x] **Visual polish, app by app** (before the ISO; done 2026-10-18, see `docs/polish/`): review every native app
  (Settings, Files, Terminal, Store, Editor, Notes, Calculator, Monitor, PDF, Fonts,
  Emoji, Scanner, Photos, Player, Screenshots, Camera, Recorder, Passwords, Disks, Backups,
  Logs, Clock, Help, Mail…) following our design guide, modeled on widely used desktop human-interface conventions (`DESIGN.md`): 8 px grid,
  uniform margins and spacing, alignment, typographic hierarchy, sidebars,
  toolbars and sheets as in the design guide, controls with the same size and height, nothing overlapping or
  clipped; in light, dark, RTL and in small windows; with before and after screenshots.
- [ ] **Final ISO** with everything (check that `lord-doc` and `lord-media` are inside) and a **Raspberry
  Pi 5 image** (`tools/rpi/build-img.sh`), tested on the real Pi.

## After the final ISO

- [ ] **Virtual machines**: a **free desktop virtualization app**, with the polish and
  way of working of the best commercial ones (not Proxmox's, which only contributes the pieces underneath: KVM, QEMU and drivers).
  A generic name and our own logo, icon and colors:
  - **Control Center** with the list of VMs and their live thumbnail, state, start, pause,
    suspend and shut down;
  - **Installation Assistant**: cards for downloading popular guest systems,
    Ubuntu, Debian, Fedora, Android, «Install from an ISO file», «Migrate from another PC»;
  - **VM window** with its bar (Actions, Devices: USB, CD, folders, network, sound) and
    the **Window, Full Screen, Seamless and Picture in Picture** modes;
  - **Configuration** in tabs: General, Options (startup and shutdown,
    optimization, sharing, applications, Seamless), Hardware (CPU and memory, graphics,
    mouse and keyboard, network, sound, USB, disks, printers, TPM, boot), Security, Backup;
  - **profiles** when creating the VM (Productivity, Software Development, Testing, Design, Games)
    that tune CPU, memory and options automatically;
  - the guest's apps in the app launcher and the Dock, opening LordOS files with a guest
    app, clipboard and dragging files between the two;
  - the **KVM** hypervisor (the kernel's); **QEMU** as the device engine, with **swtpm** (TPM)
    and **OVMF** (UEFI and secure boot) for guests that require them;
  - the same drivers Proxmox uses: **virtio-win** (disk, network, memory, 2D display
    `viogpudo`, virtiofs for non-Linux guests) signed by Red Hat, the **QEMU guest agent**
    (`qemu-ga`) and the **SPICE agent** (clipboard, resolution, file dragging); they are
    downloaded from Fedora's official source the first time and their signature is verified; Linux
    guests already ship them in the kernel;
  - our own assistant (automatic download and installation, mounts virtio-win during the guest
    install and installs the tools at the end), snapshots, cloning, host USB passthrough to the
    VM, shared folders (virtiofs), resolution that follows the window;
  - the VM's display inside our compositor via dmabuf (no QEMU windows);
  - **Seamless** mode: apps of a Linux guest as standalone LordOS windows (forwarding
    Wayland); other guests with our own agent, later;
  - accelerated 3D in Linux guests (virgl/venus); on other guests only 2D for now (there is no good free
    3D driver);
  - another architecture (arm64 on a PC, amd64 on the Pi) through TCG emulation: it works, but 5–20×
    slower;
  - later, a lightweight engine of our own only for Linux guests (like crosvm/Firecracker).
- [ ] **Android:** our own container with LineageOS and apps from our Store.
- [ ] **Firewall, ask mode** (an alert on the first connection attempt, like a per-app network monitor).
- [ ] **Accessibility:** screen reader, zoom, high contrast.
- [ ] Full **demo video** (installation + tour, English voice-over with Spanish
  subtitles).

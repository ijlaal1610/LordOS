# Changelog

## 0.7.0 Beta — first beta (2026-10-18)

The first public beta of LordOS, a GNU/Linux distribution based on Debian 13 with its own desktop
environment: a display server and Wayland compositor written in C, and a React interface on its own
engine. Milestone details are in `docs/ROADMAP.md`; the
architecture and dependency policy are in `docs/ARCHITECTURE.md`.

### M1 — Minimal display server

- Our own DRM/KMS modesetting (no libdrm) with double buffering and page flips.
- Our own evdev input (no libinput) and a 2D rasterizer with antialiased rounded rectangles and shadows.

### M2 — UI engine with React

- QuickJS embedded in `lord-server`, a native node tree (`View`, `Text`, `Image`), a CSS subset and
  our own flexbox layout.
- A React renderer (now React 19.3.1), damage tracking, cached shadows and blur.
- The "Lord Default" theme with menu bar, magnifying Dock and light/dark mode.

### M3 — SDK, apps as packages and swappable themes

- `@lordos/sdk`: window manager, settings, theme tokens, controls and system hooks.
- Apps as packages independent of the theme; a second "Classic" theme; live theme switching.
- `lord-server` drops root after opening the display and input.

### M4 — Native terminal

- Our own xterm-256color emulator with the features of popular terminal emulators: tabs, split panes, profiles, inline images,
  Sixel, shell integration, triggers, Instant Replay, `tmux -CC`, ligatures and color emoji.
- Sessions survive a server restart.

### M5 and M5b — Language, region and complex scripts

- System-wide language and region, English, Spanish and Portuguese catalogs, language packs, every
  XKB layout.
- Full Unicode bidi, Arabic and Indic shaping, the Universal Shaping Engine (409/409 sentences
  identical to HarfBuzz), mirrored RTL interface, and Korean, Japanese and Chinese input methods.

### M6 — Linux apps (Wayland)

- Our own Wayland compositor on libwayland-server: GTK and Qt apps as theme windows, shared
  clipboard, `xdg-decoration`.

### M7 — Apps, packages and system services

- Our own Debian package engine (OpenPGP, xz, HTTP, resolver, then `dpkg`) and the Store.
- Installable native `.lpk` apps and our own WebAssembly interpreter.
- Network, Wi-Fi and VPN (WireGuard, OpenVPN), sound on PipeWire, clipboard history.
- Files, Notes, the app launcher with categories and Linux app launching.

### M8 — Session and system

- `lord-sessiond`: login screen, lock, fast user switching and remote desktop from the browser.
- Per-app sandbox (Landlock, seccomp, cgroup v2), per-app permissions with system-drawn prompts, Ed25519 app signing, and each
  `.lpk` app as its own isolated process behind a permission broker.
- Per-app firewall with cgroup BPF (`lord-netperm`) and the Monitor app.
- Our own touchpad and touchscreen driver, fluid window moving and resizing in C, and the live ISO
  with installer.

### M9 — Apps and system

- Documents (`lord-doc`): PDF Viewer, archives, Quick Preview, Fonts, Emoji & Symbols.
- Printing and scanning: our own IPP client for CUPS, mDNS and eSCL.
- Media (`lord-media`): Photos, Player, Screenshots, Camera and Recorder, with protected windows.
- Passwords and the system keyring (`lord-vault`): own `.lvault` format, FIDO2 unlock, KDBX import,
  Secret Service on D-Bus.
- Mail, Calendar, Contacts and Reminders (`lord-maild`) with IMAP, SMTP, CalDAV and CardDAV.
- System apps: Disks, Backups, Logs, Clock and Help; the network belongs to the system (`lord-netd`).
- Settings hardware pages: displays, Bluetooth, energy, notifications, date and time, default apps.
- Several displays (extend and mirror) and sharp HiDPI at 100–200 % per display.

### Tonight's work (2026-10-18)

- UI engine: text ellipsis, per-side borders and logical edges, scale and rotate transforms, and
  faster painting (full-screen paint 12.5 → 4.8 ms, two-display drag 11.3 → 2.5 ms per frame).
- QA fixes: Dock hit box, audio for new users, upgrade-safe helpers, login screen preselects the last
  user, and `make test` runs every self-contained suite.
- Translatable error codes and English as the default language.
- Polish passes for Settings, Files, Terminal, Store, mail and system apps.
- The version and release channel (`package.json`: `0.7.0`, `beta`) are built into the system and
  shown in About.
- English only: code, comments and documentation are now in English (UI text stays translated in
  `assets/locales/`).

### Known gaps

- Not yet tested on the Raspberry Pi 5 or with real FIDO2 keys, printers and mail providers.
- Chromium and Firefox are not preinstalled; per-app firewall "ask" mode and accessibility are still
  to come. See the pending items in `docs/ROADMAP.md`.

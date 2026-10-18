<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/brand/logo-horizontal-dark.svg">
    <img src="assets/brand/logo-horizontal-light.svg" alt="LordOS" height="96">
  </picture>
</p>

# LordOS

**A free, fast and secure GNU/Linux desktop, built on solid Debian and Wayland foundations.**

LordOS is a free GNU/Linux distribution based on Debian 13. It ships its own desktop
environment instead of GNOME or KDE, built from its fundamental components on top of Debian and
Wayland, and it doesn't use X11: a display server and Wayland compositor written in C, its own React-based UI engine, and native apps designed to feel calm,
clear and private. An idle session uses about 120 MB of RAM. Its look and workflow take inspiration from the desktop of a well-known
fruit-logo company: a Dock, an app launcher, a menu bar, instant search and polished native
apps, all reimagined as free software. It runs on Intel/AMD PCs and on the Raspberry Pi 5, and
it still runs regular Linux apps such as Chromium, Firefox, GTK and Qt programs.

## Four pillars

1. **Our own, end to end.** Display server, Wayland compositor and UI engine; login, sessions
   and remote desktop; app store and package installer; firewall; password manager; touchpad
   driver. All written for LordOS.
2. **Light and smooth.** Built for modest hardware: about 120 MB at rest and 0 % CPU when idle,
   windows that move at the display's refresh rate, GPU composition when available and a CPU
   path when not.
3. **Secure by design.**
   - Third-party apps run sandboxed in their own process and ask before using the camera,
     microphone, files or network.
   - Every app gets kernel-enforced network rules (allow, block, LAN-only, per domain).
   - A built-in Monitor shows network and hardware activity per app, with a world map.
   - Signed packages, capture-protected windows and an encrypted password vault.
4. **Complete and polished.** More than 35 native apps (Files, Mail, Calendar, Contacts,
   Photos, Player, PDF viewer, Passwords, Disks, Backups and more), multiple monitors with
   sharp fractional scaling, touchpad gestures, multi-user sessions, remote desktop from any
   browser, and English, Spanish and Portuguese, including right-to-left scripts.

Status: **0.7.0 Beta** (see `CHANGELOG.md` and `docs/ROADMAP.md`). License: MIT.

## Repository map

- `server/`: `lord-server` (the display server: DRM/KMS and evdev straight from the kernel,
  without X11, libdrm or libinput) plus `lord-sessiond`, `lord-netd`, `lord-netperm` and the
  sandboxed helpers (`lord-media`, `lord-doc`, `lord-vault`, `lord-maild`).
- `docs/ARCHITECTURE.md`: layers, dependency policy and theme model.
- `docs/ROADMAP.md`: milestones and status.
- `docs/VM-SETUP.md`: development VM on Proxmox, step by step.
- `docs/TERMINAL.md`: the native terminal, its shortcuts and features.
- `docs/WAYLAND.md`: Linux apps (GTK, Qt) as LordOS windows with our own Wayland compositor.
- `docs/LORDOS-BRAND.md` and `docs/brand/`: the brand identity, the logo, its files (SVG, PNG, favicons, social card), colours, typeface and usage rules.
- `docs/LANGUAGES.md`: language and region across the whole system (es, en, pt and language
  packs), complex scripts (Arabic, Hebrew, CJK, mirrored UI) and input methods.

- `runtime/`: `@lordos/runtime`, the React renderer for lord-server.
- `sdk/`: `@lordos/sdk`, the API for themes and apps (windows, settings, files, system).
- `apps/`: native apps as packages. See `apps/README.md`.
- `themes/`: themes (complete desktops) in React, swappable live. See `themes/README.md`.
- `third_party/`: QuickJS, stb, Monocypher and word lists, vendored (see `THIRD_PARTY.md`). `assets/fonts/`: Inter, JetBrains Mono and Plus Jakarta Sans (the wordmark) (OFL).
- `assets/locales/`: UI texts per language. `tools/i18n-check.mjs` and `tools/i18n-pack.mjs`.
- `server/src/term/`: our own terminal (xterm emulator, PTY, drawing); the app is in `apps/terminal/`.

## Trying it

On the development machine (needs Node.js):

```sh
npm install && npm run build        # bundles every theme with the SDK and the apps
```

On Debian 13 with a generic kernel (see `docs/VM-SETUP.md` for copying the code):

```sh
sudo apt-get install -y --no-install-recommends build-essential
make -C server -j4
cd server && sudo ./build/lord-server     # drops root on startup; quit: Ctrl+Alt+Backspace
```

It must run from a text console, with no other graphical environment active.

## Building the ISO

See `docs/ISO.md` (live USB for PCs) and `docs/RASPBERRY.md` (SD/NVMe image for the
Raspberry Pi 5).

## License

MIT for LordOS's code. Each third-party component keeps its own license (see `THIRD_PARTY.md`).

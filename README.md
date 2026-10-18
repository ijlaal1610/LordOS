# LordOS

A desktop environment of its own for Linux (Debian 13), with a real graphics server written
**from scratch in C** and a user interface built with **React**.
Lightweight, designed for low-end machines.

- `server/`: `lord-server`, the graphics server. It uses DRM/KMS and evdev straight from the
  kernel, without X11, Wayland, libdrm or libinput.
- `docs/ARCHITECTURE.md`: layers, dependency policy and theme model.
- `docs/ROADMAP.md`: milestones and status.
- `docs/VM-SETUP.md`: development VM on Proxmox, step by step.
- `docs/TERMINAL.md`: the native terminal (iTerm2-style), shortcuts and parity with iTerm2.
- `docs/WAYLAND.md`: Linux apps (GTK, Qt) as LordOS windows with our own Wayland compositor.
- `docs/LANGUAGES.md`: language and region across the whole system (es, en, pt and language
  packs), complex scripts (Arabic, Hebrew, CJK, mirrored UI) and input methods.

- `runtime/`: `@lordos/runtime`, the React renderer for lord-server.
- `sdk/`: `@lordos/sdk`, the API for themes and apps (windows, settings, files, system).
- `apps/`: native apps as packages. See `apps/README.md`.
- `themes/`: themes (complete desktops) in React, swappable live. See `themes/README.md`.
- `third_party/`: QuickJS and stb, vendored. `assets/fonts/`: Inter and JetBrains Mono (OFL).
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

## License

MIT for LordOS's code. Each vendored dependency keeps its own license.

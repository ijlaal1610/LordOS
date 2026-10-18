# LordOS

Entorno de escritorio propio para Linux (Debian 13), con un servidor gráfico real
escrito **desde cero en C** y una interfaz que se construye con **React**.
Liviano, pensado para máquinas de poco rendimiento.

- `server/`: `lord-server`, el servidor gráfico. Usa DRM/KMS y evdev directo del kernel,
  sin X11, Wayland, libdrm ni libinput.
- `docs/ARCHITECTURE.md`: capas, política de dependencias y modelo de temas.
- `docs/ROADMAP.md`: hitos y estado.
- `docs/VM-SETUP.md`: VM de desarrollo en Proxmox, paso a paso.
- `docs/TERMINAL.md`: la terminal nativa (estilo iTerm2), atajos y paridad con iTerm2.
- `docs/WAYLAND.md`: apps Linux (GTK, Qt) como ventanas de LordOS con el compositor Wayland propio.
- `docs/IDIOMAS.md`: idioma y región en todo el sistema (es, en, pt y paquetes de idioma),
  escrituras complejas (árabe, hebreo, CJK, interfaz espejada) y métodos de entrada.

- `runtime/`: `@lordos/runtime`, el renderer de React para lord-server.
- `sdk/`: `@lordos/sdk`, la API para temas y apps (ventanas, ajustes, archivos, sistema).
- `apps/`: apps nativas como paquetes. Ver `apps/README.md`.
- `themes/`: temas (escritorios completos) en React, intercambiables en vivo. Ver `themes/README.md`.
- `third_party/`: QuickJS y stb, vendorizados. `assets/fonts/`: Inter y JetBrains Mono (OFL).
- `assets/locales/`: textos de la interfaz por idioma. `tools/i18n-check.mjs` y `tools/i18n-pack.mjs`.
- `server/src/term/`: terminal propia (emulador xterm, PTY, dibujo); la app está en `apps/terminal/`.

## Probar

En la máquina de desarrollo (necesita Node.js):

```sh
npm install && npm run build        # empaqueta todos los temas con el SDK y las apps
```

En Debian 13 con kernel genérico (ver `docs/VM-SETUP.md` para copiar el código):

```sh
sudo apt-get install -y --no-install-recommends build-essential
make -C server -j4
cd server && sudo ./build/lord-server     # suelta root al arrancar; salir: Ctrl+Alt+Backspace
```

Debe correr desde una consola de texto, sin otro entorno gráfico activo.

## Licencia

MIT para el código de LordOS. Cada dependencia vendorizada mantiene su licencia.

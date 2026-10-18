# Arquitectura LordOS

LordOS es un **entorno de escritorio propio** (al nivel de KDE o GNOME) con un **servidor
gráfico real escrito desde cero en C**. No es un simulador ni una página web: toma la placa
de video y el input directamente del kernel Linux. La interfaz (escritorio, barra, dock,
apps nativas) se escribe en **JavaScript con React**, que dibuja un motor propio, igual
que React Native pero sobre nuestro servidor gráfico.

## Capas

```
 Temas y apps LordOS ── React + JSX + estilos tipo CSS
        │                 <View style={{flexDirection:'row', borderRadius:12}}>
        ▼
 lord-ui (C, propio) ─── QuickJS (motor JS, vendorizado)
        │                + reconciler React propio + layout flexbox propio
        │                + rasterizador 2D propio + texto (stb_truetype)
        ▼
 lord-server (C, propio) servidor gráfico: DRM/KMS + evdev, ventanas, foco,
        │                composición, decoraciones del tema
        │                + protocolo Wayland → apps GTK (GNOME), Qt (KDE),
        │                  Firefox, Electron; Xwayland opcional para X11
        ▼
 Kernel Linux (Debian 13 mínimo, kernel genérico con DRM)
```

| Componente    | Estado      | Descripción                                                   |
|---------------|-------------|---------------------------------------------------------------|
| `server/src/` (kms, input, canvas) | **M1 hecho** | Pantalla (KMS), input (evdev), rasterizador 2D |
| `server/src/ui/` | **M2 hecho** | Nodos, estilos, flexbox, texto, imágenes, pintado, damage, QuickJS |
| `runtime/`    | **M2 hecho** | Renderer React (`@lordos/runtime`): reconciler, CSS → nativo, eventos |
| `themes/default/` | **M2 hecho** | Tema estilo Mac-OS-Desktop con assets propios |
| `server/src/ui/js_sys.c` | **M3 hecho** | APIs de sistema: fs (en `$HOME`), /proc, ajustes, temas, sesión |
| `sdk/` | **M3 hecho** | `@lordos/sdk`: ventanas, ajustes, tokens, controles, hooks de sistema |
| `apps/` | **M3 hecho** | Apps como paquetes (`manifest.json` + `index.jsx`), independientes del tema |
| `themes/classic/` | **M3 hecho** | Segundo tema y plantilla para crear temas |
| `server/src/term/` | **M4 hecho** | Terminal propia: emulador xterm, PTY, dibujo, guardianes de sesión (ver `TERMINAL.md`) |
| `server/src/ui/otf.c`, `fonts.c` | **M4 hecho** | OpenType propio (sustituciones GSUB, emoji CBDT) y catálogo de fuentes instaladas: sin HarfBuzz ni fontconfig |
| `apps/terminal/` | **M4 hecho** | App Terminal estilo iTerm2 (pestañas, paneles, perfiles…) |
| `sdk/i18n.js`, `assets/locales/` | **M5 hecho** | Textos, plurales y formatos por idioma y región (ver `IDIOMAS.md`) |
| `server/src/keyboard.c` | **M5 hecho** | Teclado con libxkbcommon: todas las distribuciones de XKB, compose |
| `server/src/admin.c` | **M5 hecho** | Ayudante privilegiado con acciones cerradas (región, zona horaria, paquetes de idioma) |
| `server/src/ui/bidi.c`, `unicode.c`, `shape.c`, `textlayout.c` | **M5b hecho** | Bidi de Unicode completo, formas del árabe y marcas (GSUB/GPOS), respaldo de fuentes por escritura, corte CJK, interfaz espejada (`direction`) |
| `server/src/ime.c`, `sdk/ime.js` | **M5b hecho** | Métodos de entrada: coreano, japonés (SKK), chino (pinyin) |
| `server/src/app/`, `ui/js_broker.c`, `runtime/app-host.js` | **M8 hecho** | Modo app: cada `.lpk` como proceso aislado (`lord-server --app <id>`), cliente Wayland del compositor, con broker de permisos (ver `PRIVACIDAD.md`) |
| `server/src/sec/`, `ui/js_sec.c` | **M8 hecho** | Aislamiento de apps .lpk (Landlock+seccomp+cgroup), permisos TCC y firma Ed25519 (ver `PRIVACIDAD.md`, `FIRMA.md`) |
| `server/src/wl/`, `sdk/wayland.js` | **M6 hecho** | Compositor Wayland propio sobre libwayland-server: apps GTK y Qt como ventanas del tema (ver `WAYLAND.md`) |
| `server/src/tp/`, `wl/wl_input.c`, `js_input.c`, `sdk/trackpad.js` | **hecho (VM, uinput)** | Driver propio de touchpads y pantallas táctiles sobre evdev (sin libinput): puntero con aceleración, tocar, clic con dedos, palmas, scroll con inercia, pellizco, deslizar; gestos y toques para las apps Wayland (ver `TRACKPAD.md`) |
| `server/src/doc/`, `js_doc.c`, `apps/pdf`, `apps/fonts`, `apps/characters` | **M9** | Documentos: el ayudante encerrado `lord-doc` (Poppler, libarchive, FreeType) y sus apps: Visor de PDF, comprimidos y Vista rápida en Archivos, Tipografías, Emojis y símbolos (ver `DOCUMENTOS.md`) |
| `server/src/print/`, `js_print.c`, `sdk/printdialog.jsx`, `apps/scanner` | **M9** | Impresión: cliente IPP propio contra CUPS, mDNS propio, escáneres eSCL, diálogo de impresión del sistema (ver `IMPRESION.md`) |
| `server/src/media/`, `js_media.c`, `sdk/media.js`, `apps/{photos,player,screenshots,camera,recorder}` | **M9** | Medios: el ayudante encerrado `lord-media` (FFmpeg, libva) y las apps Fotos, Reproductor, Capturas, Cámara y Grabadora; nodo `media`, audio por memoria compartida con el stream de PipeWire en lord-server, ventanas protegidas en capturas, grabación y escritorio remoto (ver `MEDIOS.md`) |
| `server/src/netperm/`, `server/src/fw/`, `js_fw.c` | **M8** | Firewall por app: el servicio root `lord-netperm` aplica las reglas con BPF de cgroup en las sesiones; columna Regla del Monitor y Apps confiables / restringidas en Ajustes (ver `FIREWALL.md`) |
| `server/src/monitor/`, `js_monitor.c`, `apps/monitor/` | **M8** | Monitor: CPU, memoria, red, discos, procesos y conexiones por app con mapa; hilo propio solo con la app abierta, sock_diag y país por IP con una base local (ver `MONITOR.md`) |
| `server/src/ui/grab.c`, `cursor.c`, `sdk/windowshell.jsx` | **M8** | Ventanas: mover y agrandar en C (un evento por cuadro, sin JS), capa de la ventana que se mueve, bordes en los 4 lados y esquinas, cursores propios y `cursor-shape-v1` (ver `VENTANAS.md`) |

### Flujo de un frame

1. Llega un evento (evdev) → `hit_test` busca el nodo bajo el cursor → `__lordEvent` en JS.
2. React actualiza estado → el reconciler llama `lord.setStyle/appendChild/...` → nodos marcados.
3. `lord-server` recalcula el layout, calcula la región dañada y repinta solo esa región.
4. Copia la escena al back buffer, dibuja el cursor, page flip en el próximo vblank.

## Política de dependencias

Solo se permite, además del kernel y la libc:

| Dependencia          | Por qué                                                       | Cómo       |
|----------------------|---------------------------------------------------------------|------------|
| headers del kernel   | ioctls DRM / evdev                                            | sistema    |
| `libwayland-server`  | serializa el protocolo Wayland que GTK/Qt hablan (M6, en uso) | sistema    |
| `libwayland-client`  | el «modo app» (`lord-server --app <id>`): cada app `.lpk` aislada es un cliente Wayland del compositor (M8); misma familia y paquete fuente que `libwayland-server` | sistema    |
| `libxkbcommon`       | distribuciones de teclado de XKB (M5) y lo que las apps Wayland exigen (M6) | sistema    |
| Mesa: `libgbm`, `libEGL`, `libGLESv2` | composición por GPU y búferes dmabuf de las apps (juegos); opcional: sin GPU con 3D o sin los `-dev` se compila y corre sin Mesa (aprobado por el usuario el 2026-10-16, ver `GPU.md`) | sistema |
| QuickJS              | motor JavaScript ES2023, ~1 MB, escrito en C                  | vendorizado|
| `stb_truetype`, `stb_image` | fuentes e imágenes; dominio público, un solo header    | vendorizado|
| `stb_image_write`    | PNG y JPEG del escritorio remoto (solo `lord-sessiond`); dominio público | vendorizado|
| OpenSSL (`libssl`, `libcrypto`) | TLS del escritorio remoto en la LAN. **Solo lo enlaza `lord-sessiond`**, nunca `lord-server` (`REMOTO.md`) | sistema |
| `libcrypt`           | verificar contraseñas contra `/etc/shadow` (`lord-sessiond`, `admin.c`) | sistema |
| FFmpeg (`libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`) y `libva` (+ `libva-drm`) | Decodificar y codificar video, audio e imágenes; VA-API para decodificar por hardware (M9, aprobados por el usuario). **Solo los enlaza `lord-media`**, el ayudante encerrado; el reproductor, la sincronización y la interfaz son propios. Nunca `lord-server` (`MEDIOS.md`) | sistema |
| Poppler (`libpoppler-glib`) y libarchive | PDF y comprimidos (M9, aprobados por el usuario). **Solo los enlaza `lord-doc`**, el ayudante encerrado; con Poppler vienen cairo, FreeType y fontconfig, que `lord-doc` también usa para las muestras de fuentes y para armar PDF. Nunca `lord-server` (`DOCUMENTOS.md`) | sistema |
| `clang` (solo al compilar) | los programas BPF de `lord-netperm` (`np.bpf.c`, `clang -target bpf`); van adentro del binario. Sin clang (`BPF=0`) el servicio corre sin BPF. No se usa libbpf ni bcc | máquina que compila |
| Inter, JetBrains Mono | fuentes de la interfaz y de la terminal (OFL)            | `assets/fonts` |

React (`react`, `react-reconciler`) es dependencia **del tema**, no del sistema: se empaqueta
con esbuild dentro del `.js` del tema. Desde 0.7.0 es React 19.3.1 con `react-reconciler` 0.34
(el tema por defecto pesa 367 KB con todas las apps; React 18 eran ~40 KB menos). Node.js solo se
usa en la máquina de desarrollo.

Desde M8 hay dos programas, y desde M9 dos ayudantes:

- **`lord-sessiond`** (root, `lordos.service`): la pantalla de ingreso, la lista de sesiones, el
  monitor y el escritorio remoto. Ver `SESIONES.md` y `REMOTO.md`.
- **`lord-server`**: una sesión por usuario, siempre sin monitor propio (`--headless`); el
  monitor y los navegadores la miran por su socket.
- **`lord-netperm`** (root, `lord-netperm.service`): los permisos de red por app. Engancha sus
  programas BPF de cgroup en `lordos.service` y lee las reglas de cada usuario. Sin red propia ni
  ejecución. Ver `FIREWALL.md`.

- **`lord-doc`** (M9): el ayudante de documentos. Lo lanza `lord-server` cuando hace falta, con
  ruta fija (al lado de su binario) y sin argumentos. Corre como el usuario, sin red y encerrado
  (Landlock + seccomp), y trabaja solo con los fds que le pasa `lord-server`. Abre PDF,
  comprimidos, fuentes e imágenes que llegan de afuera. Ver `DOCUMENTOS.md`.
- **`lord-media`** (M9): el ayudante de medios, igual que `lord-doc`: ruta fija, sin argumentos,
  como el usuario, sin red ni archivos (solo los fds que le pasa `lord-server`, incluidos la
  cámara y el render node), en `sandbox_apply_helper`. Decodifica y codifica con FFmpeg; los
  cuadros vuelven por memoria compartida sellada y el audio va a un anillo cuyo stream de
  PipeWire es de `lord-server`. Ver `MEDIOS.md`.

`lord-server` sigue pudiendo correr solo, con DRM y evdev propios, para el desarrollo.

Datos que vienen con LordOS para el Monitor (`MONITOR.md`): `assets/monitor/geoip-country.mmdb.xz`
(DB-IP Lite Country, CC BY 4.0, leída por un lector propio de MaxMind DB) y el mapa de Natural
Earth (dominio público). No se usa libmaxminddb.

Datos del sistema que se leen (no se enlazan): `xkb-data` (distribuciones), `libx11-data`
(solo las tablas de compose de `/usr/share/X11/locale`), `locales` y `tzdata`.

Las fuentes del sistema (por ejemplo `fonts-noto-color-emoji` para emoji o Fira Code) son
opcionales: si están, la Terminal las ofrece; no se enlaza HarfBuzz, FreeType ni fontconfig.

Desde M7, lo que LordOS hace con código propio y lo que delega en un programa de Debian:

- **Código propio, sin librerías:**
  - OpenPGP (RSA, Ed25519), SHA-256/512, xz, HTTP y el resolvedor de dependencias (`server/src/pkg`);
  - la caja de arena, los permisos y la firma de las apps `.lpk` (`server/src/sec`): Landlock,
    seccomp, cgroup v2, Ed25519 (firmar y verificar) y un lector de JSON, sin librerías externas;
  - el intérprete WebAssembly (`server/src/wasm`);
  - el cliente del protocolo de PulseAudio (`server/src/sound`);
  - netlink, DHCP y WireGuard (`server/src/net`);
  - el cliente IPP de CUPS, mDNS/DNS-SD y eSCL (`server/src/print`, M9): sin libcups, avahi-client
    ni SANE.
- **Servicios de Debian a los que se les habla por su protocolo:** CUPS (IPP por
  `/run/cups/cups.sock`; administra la cola y los controladores, ver `IMPRESION.md`).
- **Programas de Debian** (con ruta fija, nunca enlazados, lanzados por el ayudante `admin.c`):
  - `dpkg`: desempaquetar y configurar;
  - `wpa_supplicant`: Wi-Fi;
  - `openvpn`;
  - `resolvectl` y `networkctl`.
- **Servidor de sonido:** PipeWire, por el protocolo de PulseAudio.

No se usan apt, libapt, gpgv, curl, zlib, liblzma, NetworkManager, wireguard-tools, libpulse,
libpipewire, libcups, avahi-client ni SANE.

Prohibido: X11/Xorg, wlroots, libdrm, libinput, Mesa como requisito (solo como camino GPU opcional), WebKit/Chromium,
GTK/Qt dentro de LordOS; bubblewrap, flatpak, firejail ni libseccomp para el aislamiento (es
código propio con Landlock y seccomp-bpf directos). Vendorizado = copiado como código fuente en `third_party/`.

## Plataformas

| Arquitectura | Equipos | Estado | Imagen |
|---|---|---|---|
| x86-64 (amd64) | PC con UEFI o BIOS; GPU AMD, Intel o Nvidia, o sin GPU 3D (CPU) | principal, probada en VM | ISO live con instalador (`ISO.md`) |
| ARM64 (aarch64) | **Raspberry Pi 5** (oficial), Pi 4 (mejor esfuerzo); otras placas ARM64 con KMS en Linux | preparada, sin probar en la placa | imagen para SD/NVMe, `tools/rpi/build-img.sh` (`RASPBERRY.md`) |

El código es el mismo. Las diferencias:

- **Compilación:** el Makefile detecta `ARCH` con `$(CC) -dumpmachine` y nunca usa
  `-march=native`.
  - En aarch64 se activan los bucles NEON de `canvas.c` (`NEON=0` los apaga) y `STBI_NEON`.
  - En x86-64 todo es C portable (stb usa SSE2 solo).
- **Pantalla y GPU:**
  - La tarjeta se elige por tener conectores (`server/src/drmcard.h`), no por ser `card0`.
  - En las placas ARM la pantalla (vc4) y la GPU (v3d) son dispositivos distintos. Mesa los une
    (kmsro), lord-server anuncia a las apps el render node de la GPU y solo manda a la pantalla
    los modificadores que acepta el plano (`IN_FORMATS`).
- **Páginas de memoria de 16K** (kernel de la Pi 5): nada en LordOS asume 4096.
- **Seccomp:** la lista blanca usa `#ifdef` para los syscalls que arm64 no tiene (`open`, `stat`,
  `epoll_wait`, `pipe`…).
- **Kernel:** en x86-64, el de Debian; en la Raspberry Pi, el del archivo de Raspberry Pi (el de
  Debian 13 no maneja la Pi 5). Mesa es la de Debian en las dos.

## Por qué no un navegador

WebKit o Chromium cuestan entre 100 y 300 MB de RAM por proceso y no son propios.
El objetivo de LordOS es correr en máquinas de muy poco rendimiento. Con React sobre un
motor propio, el tema escribe JSX y estilos, pero se renderiza con un subconjunto de CSS
(flexbox, colores, bordes redondeados, sombras, blur, transiciones) que sí controlamos.

## Modelo de temas

Un tema es un paquete JS que exporta componentes React con un contrato fijo:
`Desktop`, `MenuBar`, `Dock`, `Launcher`, `ControlCenter`, `WindowFrame` y sus tokens
(colores, radios, sombras, fuentes, fondo). El tema habla con el servidor solo a través
de `@lordos/sdk` (`useWindows()`, `launch()`, `focus()`, `close()`…), nunca directo.
El tema por defecto replica el estilo de https://github.com/mhmdmhd6/Mac-OS-Desktop con
íconos y fondos propios. Los assets de Apple no se pueden redistribuir.

## Capas de JavaScript

```
themes/<tema>/   escritorio completo (barras, dock, marcos de ventana)   ─┐
apps/<id>/       contenido de cada ventana, independiente del tema        ─┤ un solo bundle
sdk/             @lordos/sdk: ventanas, ajustes, tokens, controles        ─┤ por tema
runtime/         @lordos/runtime: renderer React, CSS → nativo, eventos  ─┘ (esbuild)
```

El tema y las apps se comunican por el contrato de tokens (`useTheme`) y el gestor de
ventanas del SDK (`wm`). Cambiar de tema descarta el contexto QuickJS completo y carga el
otro bundle; los ajustes persisten en disco.

## Seguridad

- `lord-server` arranca con sudo solo para abrir DRM, evdev y la consola; enseguida pasa al
  usuario de la sesión (`SUDO_USER` o `--user`) conservando únicamente `CAP_SYS_TTY_CONFIG`.
- `lord.fs` es de solo lectura y rechaza rutas que (tras `realpath`) salgan de `$HOME`.
- `lord.settings` solo escribe `~/.config/lordos/settings.json` (máx. 64 KB, escritura atómica).
- `lord.themes.use` solo acepta nombres simples de carpetas en `themes/`; un tema que falla
  al cargar queda vetado para evitar bucles.
- No existe API para ejecutar comandos arbitrarios. La única forma de iniciar un proceso es
  `lord.term.create()`, que lanza el shell de login del propio usuario (el de `/etc/passwd`)
  sin capacidades ni variables `SUDO_*`, igual que cualquier emulador de terminal.
- El portapapeles es de la sesión (en memoria). Los programas de la terminal pueden escribirlo
  (OSC 52) pero no leerlo.
- Los `permissions` de los manifests son declarativos hasta que haya aislamiento por app.
- Las apps Wayland externas (M5) solo verán sus propias superficies.
- Los documentos que llegan de afuera (PDF, comprimidos, fuentes, imágenes del escáner) se abren
  solo en `lord-doc`, encerrado. Un archivo malicioso puede tirar abajo ese proceso, pero no
  `lord-server`. Extraer nunca escribe fuera de la carpeta de destino (`DOCUMENTOS.md`, «Extraer
  sin salir de la carpeta»).
- Los medios (videos, música, fotos, la cámara) se decodifican solo en `lord-media`, encerrado y
  sin el socket de PipeWire. Pantalla, micrófono y cámara piden permiso (`sec/perms.c`, verificado
  en C), y las ventanas protegidas (`wm.setProtected`) salen negras en capturas, grabaciones y el
  escritorio remoto (`MEDIOS.md`).
- Impresoras: administrar las colas (agregar, quitar, predeterminada) lo autoriza CUPS. Solo
  pueden los usuarios del grupo `lpadmin` (los administradores), identificados con PeerCred por
  el socket unix. No hay ninguna acción de root nueva en `admin.c` (`IMPRESION.md`).

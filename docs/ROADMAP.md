# Roadmap LordOS

Cada hito se verifica en la VM `lordos-dev` (ver `VM-SETUP.md`) con capturas reales de
pantalla (`screendump` de QEMU) y se documenta.

## M1 — Servidor gráfico mínimo ✅ (2026-10-15)

- [x] Modesetting propio con ioctls DRM (sin libdrm), doble buffer + page flip.
- [x] Input propio desde evdev (sin libinput): teclado, mouse relativo, tablet absoluto, `EVIOCGRAB`.
- [x] Rasterizador 2D propio: alpha, rectángulos redondeados con antialiasing, sombras.
- [x] Escena de prueba: fondo, barra de menú, dock con magnificación, ventana arrastrable, cursor.
- [x] Medido: 1280×800, render completo por CPU ≈ 13 ms/frame en la VM.

## M2 — Motor de UI con React ✅ (2026-10-15)

- [x] QuickJS 2025-04-26 vendorizado en `third_party/quickjs`, embebido en `lord-server`.
- [x] Árbol de nodos nativo (`View`, `Text`, `Image`) + estilos (subconjunto CSS, ver `themes/README.md`).
- [x] Layout flexbox propio (`server/src/ui/layout.c`).
- [x] Texto con `stb_truetype` y fuente Inter (OFL); imágenes PNG/JPEG con `stb_image`.
- [x] Renderer de React 18 (`runtime/`, `react-reconciler` 0.29) que crea y actualiza nodos nativos.
- [x] Eventos de mouse con burbujeo (enter/leave/click), teclado, timers, `requestAnimationFrame`.
- [x] Tema "Lord Default": barra de menú con blur, dock con magnificación y tooltip, ventanas
      arrastrables con semáforo, apps Archivos/Ajustes/Acerca de, modo claro/oscuro y acentos en vivo.
- [x] Sombras cacheadas, blur a resolución reducida, damage tracking.

Medido en la VM (1280×800, CPU, sin GPU):

| Situación | Render |
|---|---|
| Primer frame (pantalla completa) | ~65 ms |
| Hover en menú | 0,3–0,6 ms |
| Hover en dock con magnificación | 2–4 ms |
| Arrastrar ventana | ~5 ms |
| RAM total del servidor (con tema React) | ~24 MB |
| Carga del tema (126 KB, React incluido) | ~28 ms |

Pendiente para más adelante: radios por esquina, `transform: scale`, entrada de
texto (teclado → caracteres según layout), copiar al back buffer solo la región dañada.

## M3 — SDK, apps como paquetes y temas intercambiables ✅ (2026-10-15)

- [x] Seguridad: `lord-server` suelta root tras abrir pantalla/input; corre como el usuario con
      solo `CAP_SYS_TTY_CONFIG` (verificado en `/proc/<pid>/status`).
- [x] APIs nativas: `lord.fs` (solo lectura, encerrado en `$HOME`), `lord.system` (/proc),
      `lord.settings` (`~/.config/lordos/settings.json`), `lord.themes`, `lord.quit`.
- [x] Input: clic derecho, rueda, doble clic; teclado con distribuciones `latam` y `us`,
      tildes con tecla muerta, keypad.
- [x] `@lordos/sdk` (`sdk/`): gestor de ventanas, ajustes persistentes, contrato de tokens
      (`ThemeProvider`/`useTheme`), `TextInput`, `ScrollView`, controles, hooks de sistema.
- [x] Apps como paquetes (`apps/<id>/manifest.json`): Archivos (FS real), Ajustes, Acerca de
      (datos reales), Calculadora, Terminal (aviso M4).
- [x] Tema "Lord Default": Launchpad con búsqueda, Centro de control (CPU/RAM en vivo),
      menús desplegables, menú contextual, 4 fondos propios.
- [x] Tema "Clásico" como plantilla (barra de tareas + menú de inicio, un solo archivo).
- [x] Cambio de tema en vivo desde Ajustes, persistente, con protección ante temas rotos.

Medido en la VM: RAM del servidor 24–26 MB con Lord Default, 11 MB con Clásico; cambio de
tema ~30 ms + primer frame ~60 ms.

Pendiente de M3 que pasa a más adelante: volumen y brillo (no hay audio ni backlight en la VM),
aislamiento real de apps por permisos, entorno de desarrollo en el navegador del Mac.

## M4 — Terminal nativa estilo iTerm2 ✅ (2026-10-15)

Se adelantó a Wayland por pedido del proyecto: la terminal de LordOS es propia, no una app
Linux. Detalle completo, atajos y tabla de paridad con iTerm2 en `TERMINAL.md`.

- [x] Motor en C (`server/src/term/`): emulador xterm-256color (parser VT500, color directo,
      pantalla alternativa, mouse, pegado entre corchetes, salida sincronizada), historia con
      reflujo, PTY sin privilegios, dibujo con daño por filas, cajas/bloques/powerline a mano.
- [x] Extensiones de iTerm2: imágenes en línea (`imgcat`), insignias, marcas, integración con
      bash (OSC 133/7), Sixel, hipervínculos OSC 8, OSC 52, notificaciones OSC 9.
- [x] App React: pestañas, paneles divididos, perfiles con 16 esquemas, transparencia y
      desenfoque, buscador con regex, toolbelt, barra de estado, ventana desplegable (F12),
      modo copia, Instant Replay, disparadores, cambio automático de perfil, disposiciones
      guardadas, deshacer cierre, compositor, autocompletar, historial de comandos y pegado.
- [x] Plataforma: varias ventanas por app, título dinámico, menús de la app en la barra del
      tema, confirmación al cerrar, argumentos al abrir apps, redimensionar ventanas,
      componente `<Terminal>`, portapapeles de la sesión, `Stepper` y `Slider`.

- [x] Completada la paridad con iTerm2: elección de fuente y fuente "no ASCII",
      ligaduras (motor GSUB propio, `server/src/ui/otf.c`), emoji a color (CBDT), caracteres
      combinantes y grafemas (modo 2027), integración con zsh y fish, atajos y teclas
      configurables, cambio de perfil por usuario@host, disparadores de resaltar/marca/título,
      sesiones que sobreviven a un reinicio del servidor (guardianes, `term/hold.c`),
      `tmux -CC` con pestañas y paneles nativos, e **importación de perfiles de iTerm2**
      (JSON, `.itermcolors`, plist XML y binario).

Medido en la VM: `seq 1 1000000` en 2,1 s con transparencia; RAM del servidor ~39 MB con
3 terminales, ~43 MB con 6 sesiones + 3 paneles de tmux + emoji; ~2,2 MB por guardián.
Lo que queda fuera (imagen de fondo, emoji COLR/SVG, GPOS) está en `TERMINAL.md`.

## M5 — Idioma y región (todo el sistema) ✅ (2026-10-15)

Detalle en `IDIOMAS.md`. Va antes de Wayland para que las apps GTK/Qt reciban el mismo idioma.

- [x] Región del sistema por usuario (`~/.config/locale.conf`) heredada por la sesión y los
      shells; mensajes de Debian (gettext) en el idioma elegido (verificado con `ls`, `date`, `printf`).
- [x] Interfaz: catálogos por espacio (`assets/locales/<idioma>/*.json`), `t()`/`useLocale()`,
      plurales (reglas CLDR propias), fechas y números por región; cambio en vivo sin perder
      ventanas ni terminales; respaldo en inglés. 391 claves.
- [x] Español, inglés y portugués completos; `tools/i18n-check.mjs` y `tools/i18n-pack.mjs`;
      paquete de prueba en francés instalado desde Ajustes.
- [x] Paquetes de idioma: catálogos de LordOS (usuario, sin root) + región de glibc + paquetes
      de Debian del idioma (manpages, hunspell, aspell, hyphen, mythes, fuentes de la escritura).
- [x] Teclado con libxkbcommon + xkb-data: todas las distribuciones y variantes, hasta 4 a la
      vez, teclas muertas y compose.
- [x] Zona horaria y región del sistema desde Ajustes.
- [x] Ayudante privilegiado mínimo (`server/src/admin.c`) con acciones cerradas, aprobado.

## M5b — Escrituras complejas ✅ (2026-10-15)

Detalle en `IDIOMAS.md` → "Escrituras complejas". Sin HarfBuzz, fribidi, ICU, ibus ni fcitx.

- [x] Bidi de Unicode completo (UAX #9): 91.707/91.707 casos de `BidiCharacterTest.txt`.
- [x] Formas contextuales del árabe, ligaduras, marcas, cursivo (nastaliq), shaper índico
      (9 escrituras) y tailandés/laosiano: 217/217 frases idénticas a HarfBuzz.
- [x] Respaldo de fuentes por escritura en la interfaz y la terminal (Noto; CJK según el idioma).
- [x] Corte de línea para chino y japonés.
- [x] Interfaz espejada para idiomas RTL (`direction` como en CSS, `textAlign` start/end),
      probada en árabe con los dos temas.
- [x] Métodos de entrada: coreano (hangul), japonés (romaji → kana + SKK, conversión por
      frase con segmentos), chino (pinyin con abreviaturas); en la terminal y en campos de texto.
- [x] Terminal: árabe y hebreo en orden visual y escrituras complejas armadas (opción por perfil).
- [x] Shapers de jemer y birmano y el Universal Shaping Engine (tibetano, cingalés, balinés,
      javanés, mongol y ~80 escrituras más): con lo anterior, 409/409 frases idénticas a HarfBuzz
      (2026-10-16).
- [x] Corte de línea del tailandés por palabras con el diccionario de libthai-data y lector
      propio (igual a libthai en 705/705 párrafos de prueba); jemer y birmano por sílaba (2026-10-16).
- [ ] Pendiente: laosiano por palabras, restricciones de vocales con ◌, japonés con frecuencias.

## M6 — Compatibilidad con apps Linux (Wayland) ✅ (2026-10-16)

Detalle en `WAYLAND.md`.

- [x] `libwayland-server` + `wl_compositor`, `wl_shm`, `wl_subcompositor`, `xdg_shell`
      (ventanas y menús emergentes), `wl_seat` (puntero y teclado XKB), `wl_output`.
- [x] Ventanas de apps GTK4 (marco propio) y Qt6 (marco del tema) dentro de los dos temas:
      título, foco, mover, cambiar tamaño, maximizar, minimizar, cerrar, Dock.
- [x] Portapapeles compartido con LordOS en los dos sentidos, `xdg-decoration`.
- [ ] Lanzar apps `.desktop` desde el Launchpad (necesita aprobar una excepción a "el servidor
      no ejecuta comandos"), `linux-dmabuf`, cursores de las apps, arrastrar y soltar,
      `text-input`, Xwayland.

## M7 — Apps, paquetes y servicios del sistema ✅ (2026-10-16)

Definido con el usuario el 2026-10-16: tienda propia (apps de Debian y apps nativas LordOS),
instalador propio, y red, VPN, sonido y portapapeles con interfaz propia.

- [x] Lanzador de apps Linux instaladas (`.desktop`) en el Launchpad, el menú de inicio y el
      Dock, con la excepción `lord.apps.launch(id)` aprobada (`WAYLAND.md`, "El lanzador").
- [x] Apps nativas instalables `.lpk` (sin root), con React y el SDK compartidos con el tema,
      `<Image pixels>` y `loadWasm` (`APPS-NATIVAS.md`); ejemplo `examples/apps/fractal`.
- [x] Intérprete WebAssembly propio y el global `WebAssembly` (`WASM.md`): 26.290/26.290
      aserciones de la suite oficial wg-2.0; 3–5× más rápido que el mismo código en JS.
- [x] Motor de paquetes de Debian propio (`PAQUETES.md`): OpenPGP (RSA/Ed25519), xz, HTTP y
      resolvedor propios, `dpkg` al final; planes idénticos a apt en 12 casos; huérfanos.
- [x] Tienda: apps de Debian por categoría (AppStream), búsqueda, plan antes de instalar o
      quitar, actualizaciones, `.deb` y `.lpk` locales.
- [x] Launchpad y menú de inicio con categorías (freedesktop), íconos de AppStream para las apps
      que solo traen SVG.
- [x] Ajustes agrupado como en macOS/TangoOS (buscador, usuario, General, Red, Sonido,
      Actualizaciones…).
- [x] Portapapeles: imágenes entre apps Linux que sobreviven a la app de origen, historial (❖V).
- [x] Sonido sobre PipeWire con cliente propio (`SONIDO.md`): Centro de control y Ajustes.
- [x] Red propia (netlink, DHCP, Wi-Fi con wpa_supplicant) y VPN (WireGuard por netlink propio,
      OpenVPN) en Ajustes → Red (`RED.md`).
- [x] React 19.3.1 (`react-reconciler` 0.34): renderer adaptado (`runtime/index.js`), probado con
      los dos temas, cambio de tema en vivo, apps instaladas (.lpk) y apps Linux. lord-server: 34 MB de RSS.
- [x] Archivos al estilo Finder (`ARCHIVOS.md`): pestañas, cuatro vistas, Papelera de
      freedesktop, Vista rápida, Ver información, copiar/mover en segundo plano (`fsops.c`),
      arrastrar y soltar, etiquetas, búsqueda, discos montados y «Abrir con» (`launchWith`).
- [ ] Pendiente de M7: OpenConnect e IPsec (strongSwan), WPA3/Enterprise, permisos reales por
      app, repositorio de apps LordOS firmado.
- [x] `flexWrap` en el motor de layout propio (`layout.c`).
- [x] Notas: post-its en el escritorio (widgets), markdown con lectura e índice, imágenes,
      adjuntos y emojis, guardadas solas en `~/Notas` (`NOTAS.md`).

## M8 — Sesión y sistema

- [x] Sesión propia: `lord-sessiond` con pantalla de ingreso, bloqueo (❖L), cambio rápido de
  usuario, sesiones en paralelo y Ajustes → Usuarios (`SESIONES.md`).
- [x] Escritorio remoto en la LAN desde el navegador (puerto 5673, apagado por defecto;
  `REMOTO.md`). Pendiente: audio (etapa 2, diseñada).
- [x] Privacidad y firma de apps `.lpk` (`PRIVACIDAD.md`, `FIRMA.md`):
  - [x] Caja de arena propia (`server/src/sec/sandbox.c`): Landlock (ABI≤6), seccomp-bpf (lista
    blanca), sin red sin permiso, `no_new_privs`, capacidades soltadas y cgroup v2 por app;
    probada en la VM (`build/sandbox-test`, 7/7). Sin bubblewrap/flatpak/firejail.
  - [x] Permisos estilo TCC de macOS: avisos dibujados por el sistema, decisiones guardadas donde
    la app no escribe (`permisos.json`), Ajustes → Privacidad y los puntos verde/naranja de
    cámara/micrófono en los dos temas.
  - [x] Firma Ed25519 propia (SHA-256/Ed25519, verificar y firmar), `contenido.json` con hashes,
    niveles de confianza, catálogo firmado con revocación, y «Modificada por vos» / «Restaurar
    original» / «Compartir mi versión». Pruebas `build/sign-test` (55/55, RFC 8032 + NIST) y
    `build/pkgstate-test` (11/11).
  - [x] `tools/lpk.mjs`: `keygen`, firmar al empaquetar, `sign`, `verify` y `catalog`.
  - [x] Cada `.lpk` como proceso aislado (`lord-server --app <id>`): cliente Wayland con el marco
    del tema, en el Dock y el escritorio remoto; broker que verifica cada `lord.*`; avisos del
    sistema pedidos por la app. La app maliciosa (`tests/lpk-evil`) falla en todo en tiempo de
    ejecución; la legítima (`tests/lpk-legit`) funciona tras el consentimiento.
  - [x] cgroup delegado por sesión (`Delegate=yes`, `lord-sessiond` en la hoja `supervisor`):
    `app-lordos-lpk-<id>.scope` y `app-lordos-<id>.scope` (apps Linux) bajo
    `lordos-session-<uid>`; probado con cambio rápido de usuario y escritorio remoto.
  - [x] `sandbox_apply_helper` endurecido para lord-media y lord-doc (sin sockets ni exec, solo
    hilos, TSYNC, carpetas por fd, ioctl por clase, kcmp propio): `sandbox-test helper` 17/17.
  - [ ] Pendiente: powerbox gráfico (diálogo que entrega el fd), fd de la cámara y flujo de
    PipeWire hacia la app, tokens/idioma/RTL en vivo y menús de la app aislada.
- [x] **Firewall por app**, pasos 1 a 4 (`FIREWALL.md`), probado en la VM 122:
  - [x] tienda de reglas, precedencia, `dnsmap` y JSON (`server/src/fw`, paso 1);
  - [x] `lord-netperm`, el servicio root que aplica las reglas con BPF de cgroup (connect, sendmsg,
    bind, sock_create) en el subárbol de las sesiones, con cargador propio (sin libbpf), reglas
    compiladas a mapas LPM/hash (`build/npcompile-test`: 480 000 conexiones contra `fw_decide`,
    0 diferencias) y nombres solo del resolvedor del sistema (monitor de systemd-resolved);
  - [x] prueba con el kernel `tests/netperm_integration.sh` 18/18: denegar/permitir, "solo
    example.com", solo LAN, sin entrantes, sin red, apagado;
  - [x] Monitor: interruptor Regla por app y contadores de permitidas/denegadas; Ajustes →
    Privacidad: modo, Apps confiables y Apps restringidas;
  - [x] root-owned scopes: `lord-sessiond` places every app process and a Linux app can't move
    itself to another cgroup (VM 122). Still open: `systemd-run --user --scope` (PID 1 moves it
    into `user@<uid>.service`).
  - [ ] modo "preguntar" (diseñado), interruptores por destino.
- [x] App **Monitor**, fase 1 (`MONITOR.md`), probada en la VM 123:
  - [x] pestañas Resumen, CPU, Memoria, Red, Discos y Procesos con el aspecto del Monitor de
    TangoOS;
  - [x] métricas leídas por `lord-server` en un hilo propio, a 4 Hz y solo con la app abierta
    (cerrada no cuesta nada);
  - [x] conexiones por app con sock_diag, del proceso a la app por los `.desktop`;
  - [x] mapa vectorial propio con zoom y arcos, al estilo de Little Snitch;
  - [x] país por IP con un lector propio de MaxMind DB y DB-IP Lite (CC BY 4.0), sin red;
  - [x] ítem en Ajustes → Privacidad.
  - [x] Fase 2, firewall: ver "Firewall por app" arriba.
  - [ ] Fase 2, resto: alertas de conexión ("preguntar"), cámara y micrófono en uso (`monitor.deviceUsers()`), nombres de host sin
    DNS inverso, historial y alertas de uso.
- [ ] Sesiones con GPU (hoy se dibujan por CPU), cambio de VT y varios monitores.
- [ ] Más apps nativas LordOS en React (editor de texto, visor de imágenes, navegador).
- [ ] Imagen instalable basada en Debian 13 mínimo, con LordOS como paquetes `.deb`.
- [ ] **ARM64 / Raspberry Pi 5 como plataforma oficial** (`RASPBERRY.md`):
  - [x] Auditoría de portabilidad de `server/` para aarch64: char sin signo, páginas de 16K,
    seccomp de arm64, alineación y `ARCH` en el Makefile (sin `-march=native`).
  - [x] NEON opcional por arquitectura en `canvas.c` (blit y máscaras), igual bit a bit a C
    (`build/canvas-test`); `STBI_NEON`.
  - [x] Pantalla y GPU separadas (vc4 + v3d):
    - la tarjeta con conectores (`drmcard.h`), en lord-server y lord-sessiond;
    - `IN_FORMATS` del plano primario y scanout directo solo con modificadores aceptados;
    - el render node de EGL en el feedback de dmabuf.
  - [x] `tools/rpi/build-img.sh`: `lordos-<versión>-rpi5-arm64.img.xz` para Raspberry Pi Imager:
    - Debian 13 arm64 con el kernel y el firmware de Raspberry Pi;
    - el mismo `.deb` del ISO;
    - la partición se agranda sola;
    - primer arranque con `lordos-user.txt`/`userconf.txt` (pantalla de ingreso) o modo de
      configuración.
  - [ ] Correr `build-img.sh` y probar en la Pi 5 de 16 GB (lista de pruebas de `RASPBERRY.md`,
    con capturas por el escritorio remoto).
  - [ ] Video por hardware en la Pi 5 (HEVC por V4L2 stateless; H.264 por software) en
    `lord-media`.
  - [ ] Paso de «crear usuario» en la pantalla de ingreso, en lugar del modo de configuración.
  - [ ] Pi 4 (mejor esfuerzo).

## M9 — Documentos e impresión ✅ (2026-10-17)

Detalle en `DOCUMENTOS.md` y `IMPRESION.md`. Probado en la VM 116 con capturas (`m9-docs-*.png`).

- [x] **`lord-doc`, el ayudante encerrado:**
  - es el único binario con Poppler (interfaz de glib) y libarchive;
  - se lanza a pedido con ruta fija y sin argumentos, y trabaja solo con los fds que le pasa
    lord-server;
  - se encierra con `sandbox_apply_helper`: sin red ni sockets, Landlock con escritura solo
    debajo del destino, seccomp, cgroup y tope de memoria;
  - las páginas vuelven en memfd sellados;
  - el compositor no espera nunca: hay plazos, y si el ayudante se cae no pasa nada más.
- [x] **Visor de PDF:**
  - desplazamiento continuo y de a una página, miniaturas, índice;
  - búsqueda con resaltado, zoom y ajuste, rotar, seleccionar y copiar, ir a página, enlaces;
  - anotaciones (resaltar, nota, a mano alzada) guardadas como copia, formularios;
  - página oscura invertida;
  - imprimir.
- [x] **Archivos:**
  - mirar adentro de los comprimidos como una carpeta;
  - Extraer aquí (zip, tar.gz/xz/zst/bz2, 7z, gz sueltos) sin salir de la carpeta, con
    `arcpath-test` y comprimidos maliciosos: 31/31;
  - Comprimir a `.zip`;
  - Vista rápida de PDF, fuentes, comprimidos y lo de adentro de un comprimido;
  - Vista rápida de audio y video con `media.poster` si está.
- [x] **Tipografías:** muestras, tabla de caracteres por bloque, información, instalar y quitar
  fuentes del usuario.
- [x] **Emojis y símbolos:**
  - selector global (❖.) con búsqueda en es/en/pt (CLDR), categorías, recientes y tono de piel;
  - escribe por el camino del método de entrada;
  - app con símbolos de Unicode por bloque.
- [x] **Impresión:**
  - cliente IPP propio para CUPS (sin libcups), mDNS propio;
  - Ajustes → Impresoras y escáneres: agregar IPP Everywhere/AirPrint, URI manual,
    predeterminada, cola y cancelar, administración por `lpadmin` + PeerCred;
  - diálogo de impresión del sistema con vista previa, usado por el Visor de PDF, el Editor y
    Notas (Fotos tiene la API lista).
- [x] **Escáner:** cliente eSCL propio, sin SANE: vista previa, área, resolución, color, y
  guardar como PDF/PNG/JPEG. Probado con un escáner de prueba (`tests/escl_server.py`).
- [ ] **Pendiente:**
  - miniaturas de PDF en la vista de íconos;
  - comprimidos cifrados;
  - firmas en PDF;
  - escáneres y `ipps` por TLS desde lord-server;
  - armado de escrituras complejas en el texto impreso;
  - emojis con ZWJ en el motor de texto;
  - probar con impresoras y escáneres reales.

## M9 — Apps nativas de medios ✅ (2026-10-17)

Detalle en `MEDIOS.md`. Probado en la VM 119 con capturas (`m9-media-*.png`).

- [x] **`lord-media`, el ayudante encerrado:**
  - es el único binario con FFmpeg (libavcodec, libavformat, libswscale, libswresample) y libva;
  - se lanza a pedido con ruta fija y sin argumentos, y trabaja solo con los fds que le pasa
    lord-server (archivos, cámara, render node);
  - `sandbox_apply_helper`: sin sockets, exec ni red, ioctl por clase (DRM, V4L2) solo si hace
    falta, TSYNC; `media-test sandbox` lo verifica;
  - los cuadros vuelven por tres ranuras de memoria compartida sellada; si se cae, solo la
    ventana se entera (probado con un segfault real).
- [x] **Audio por memoria compartida:** el stream de PipeWire es de lord-server; el PCM va por un
  anillo y el reloj del video por un seqlock en la misma página (sin idas y vueltas). Desfase A/V
  medio 0,5–1,9 ms. El micrófono, al revés.
- [x] **Reproductor:**
  - video con audio sincronizado, barra de tiempo, volumen, subtítulos `.srt` y de la pista;
  - pantalla completa e imagen en imagen (`wm.setFullscreen`/`wm.setPip` en el SDK);
  - biblioteca de música por artista y álbum con carátulas, cola, aleatorio y repetir;
  - teclas multimedia y el tile del Centro de control.
- [x] **Fotos:** fototeca de `~/Imágenes` con miniaturas en `~/.cache/lordos/thumbs`, visor con zoom,
  desplazamiento, giro, pantalla completa, presentación, GIF animado, HEIC/AVIF/WebP/BMP, edición
  básica guardada como copia e imprimir.
- [x] **Capturas:**
  - pantalla, ventana o área y temporizador;
  - al portapapeles o a `~/Imágenes/Capturas`;
  - miniatura flotante y editor de anotaciones;
  - grabación de pantalla a MP4 (H.264 + AAC del micrófono);
  - atajos ❖⇧3/❖⇧4/❖⇧5 e Impr Pant.
- [x] **Ventanas protegidas** (`wm.setProtected`): negras en capturas, grabaciones, el escritorio
  remoto y las miniaturas; también las apps Wayland y sus menús.
- [x] **Cámara:** V4L2 propio, foto JPEG, video H.264 + AAC, espejo, elegir cámara (probado con
  v4l2loopback).
- [x] **Grabadora:** micrófono por PipeWire, forma de onda, renombrar, recortar y exportar a Opus,
  MP3 o WAV.
- [x] **Archivos:** «Abrir con» Fotos y Reproductor, miniaturas de video, carátulas y WebP/HEIC/AVIF;
  `media.poster` para la Vista rápida.
- [x] **Rendimiento:** pintar desde el video cuando solo cambió un cuadro opaco: lord-server con
  un 1080p pasó de 42 % a 13 % de un núcleo en ventana y de 92 % a 15 % en pantalla completa.
- [ ] **Pendiente:**
  - medir VA-API en la PC con AMD (la VM no tiene driver);
  - cero-copia dmabuf → EGL en el camino GPU;
  - HEVC por `v4l2request` en la Raspberry Pi 5;
  - subtítulos de imagen;
  - grabar un área y el audio del sistema;
  - guardar en WebP.

## M9 — System apps and system network ✅ (2026-10-18)

Details in `SYSTEM.md` (apps), `RED.md` (network) and `SESIONES.md` (user folders). Tested on VM 119
with screenshots (`m9-sistema-*.png`).

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
  first login, links to Settings pages, indexed by Spotlight.
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
(`m9-contrasenas-*.png`).

- [x] **`lord-vault`**, sandboxed helper (no network, no sockets, no exec, fd-only files); the only
  binary with Monocypher; decrypted data in an mlock/MADV_DONTDUMP arena, wiped on lock.
- [x] **Own `.lvault` format:** XChaCha20-Poly1305, vault key wrapped per unlock method (Argon2id
  tuned per machine, BLAKE2b), attachments sealed separately, atomic save with `.bak`; every flipped
  byte and every truncation detected (`vault-test`, 74/74).
- [x] **Unlock:** master password, FIDO2 `hmac-secret` key (own CTAP2 client), key file on a USB
  stick, password + key file, printable recovery key. Auto-lock on idle, ❖L and user switch.
- [x] **App:** Apple-Passwords-style sidebar (All, Favorites, Archived, 2FA codes, Wi-Fi, Secure
  notes, Cards, Security), KeePassX-style category tree with 16 built-in templates and user
  categories with their own fields, tags, history, trash, attachments, rotation reminders, Wi-Fi QR,
  TOTP with countdown (also from a QR image), generator, security report (offline breach check),
  import (own KDBX 4/3.1 reader, Bitwarden, Chrome, Firefox, Apple) and export. Protected window.
- [x] **Clipboard:** secrets marked sensitive (out of the history, `x-kde-passwordManagerHint` for
  Wayland clients), cleared after 30 s.
- [x] **Session keyring** (`login.lvault`) opened with the login password via `lord-sessiond`;
  `keyring` API per app with sharing; Settings → Network stores Wi-Fi/VPN secrets there;
  `<AutofillButton>`.
- [x] **Secret Service** with our own D-Bus code (plain + DH sessions): `secret-tool` and Chromium
  work without gnome-keyring.
- [ ] Run the Passwords UI as an isolated process (app mode); today only `lord-vault` is sandboxed.
- [ ] Test with real FIDO2 keys (only the uhid software authenticator was available).

## Pendiente antes del ISO final

- [ ] **Mail, Calendario, Contactos y Recordatorios** (estilo Outlook; cuentas IMAP/SMTP, CalDAV y
  CardDAV; usa el llavero de Contraseñas).
- [ ] **Secciones de Ajustes que faltan:** Pantallas, Bluetooth, Energía, Notificaciones, Fecha y
  hora, Apps por defecto.
- [ ] **Chromium y Firefox** preinstalados (apps Linux, no nativas).
- [x] **Cámara en apps aisladas** (`.lpk`) y **varias ventanas por app aislada** (VM 122,
  `PRIVACIDAD.md`): read-only frame ring from `lord-media` with validated ACKs, photo/video through
  the powerbox, instant revoke; one Wayland toplevel per window, grouped in the Dock, «New
  window» from the app and the Dock.
- [ ] **Pulido visual estilo Apple, app por app** (antes del ISO): revisar todas las apps nativas
  (Ajustes, Archivos, Terminal, Store, Editor, Notas, Calculadora, Monitor, PDF, Tipografías,
  Emojis, Escáner, Fotos, Reproductor, Capturas, Cámara, Grabadora, Contraseñas, Discos, Copias,
  Registros, Reloj, Ayuda, Mail…) siguiendo las guías de interfaz de macOS (HIG): grilla de 8 px,
  márgenes y separaciones uniformes, alineación, jerarquía tipográfica, barras laterales, barras de
  herramientas y hojas como en macOS, controles con el mismo tamaño y altura, nada encimado ni
  cortado; en claro, oscuro, RTL y en ventanas chicas; con capturas antes y después.
- [ ] **ISO final** con todo (verificar que `lord-doc` y `lord-media` estén adentro) e **imagen
  para Raspberry Pi 5** (`tools/rpi/build-img.sh`), probada en la Pi real.

## Después del ISO final

- [ ] **Máquinas virtuales**: un **clon libre de Parallels Desktop**, con su misma interfaz y
  forma de trabajar (no la de Proxmox, que solo aporta las piezas de abajo: KVM, QEMU y drivers).
  Nombre genérico y logo, ícono y colores propios:
  - **Centro de control** con la lista de VMs y su miniatura en vivo, estado, iniciar, pausar,
    suspender y apagar;
  - **Asistente de instalación** como el de Parallels: tarjetas para «Descargar Windows 11»,
    Ubuntu, Debian, Fedora, Android, «Instalar desde un archivo ISO», «Migrar desde otra PC»;
  - **Ventana de la VM** con su barra (Acciones, Dispositivos: USB, CD, carpetas, red, sonido) y
    los modos **Ventana, Pantalla completa, Coherence e Imagen en imagen**;
  - **Configuración** en pestañas como Parallels: General, Opciones (inicio y apagado,
    optimización, compartir, aplicaciones, Coherence), Hardware (CPU y memoria, gráficos,
    mouse y teclado, red, sonido, USB, discos, impresoras, TPM, arranque), Seguridad, Copias;
  - **perfiles** al crear la VM (Productividad, Desarrollo de software, Pruebas, Diseño, Juegos)
    que ajustan CPU, memoria y opciones solos;
  - las apps del invitado en el Launchpad y el Dock, abrir archivos de LordOS con una app del
    invitado, portapapeles y arrastrar archivos entre los dos;
  - hipervisor **KVM** (el del kernel); **QEMU** como motor de dispositivos, con **swtpm** (TPM)
    y **OVMF** (UEFI y arranque seguro) para Windows 11;
  - los mismos drivers que usa Proxmox: **virtio-win** (disco, red, memoria, pantalla 2D
    `viogpudo`, virtiofs para Windows) firmados por Red Hat, el **agente invitado de QEMU**
    (`qemu-ga`) y el **agente de SPICE** (portapapeles, resolución, arrastrar archivos); se
    descargan de la fuente oficial de Fedora la primera vez y se verifica la firma; los invitados
    Linux ya los traen en el kernel;
  - asistente propio (descarga e instalación automática, monta virtio-win durante la instalación
    de Windows e instala las herramientas al final), instantáneas, clonado, USB del equipo a la
    VM, carpetas compartidas (virtiofs), resolución que se ajusta a la ventana;
  - la pantalla de la VM dentro de nuestro compositor por dmabuf (sin ventanas de QEMU);
  - modo **Coherence**: apps de un Linux invitado como ventanas sueltas de LordOS (reenviando
    Wayland); Windows con agente propio, más adelante;
  - 3D acelerado en invitados Linux (virgl/venus); en Windows solo 2D por ahora (no hay driver 3D
    libre de calidad);
  - otra arquitectura (arm64 en una PC, amd64 en la Pi) por emulación TCG: funciona, pero 5–20×
    más lento;
  - más adelante, un motor propio liviano solo para invitados Linux (como crosvm/Firecracker).
- [ ] **Android:** contenedor propio con LineageOS y apps desde nuestro Store.
- [ ] **Firewall, modo pregunta** (alerta al primer intento de conexión, estilo Little Snitch).
- [ ] **Accesibilidad:** lector de pantalla, lupa, alto contraste.
- [ ] **Video de demostración** completo (instalación + recorrido, voz en inglés con subtítulos
  en español).

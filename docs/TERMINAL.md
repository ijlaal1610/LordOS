# LordOS Terminal

Terminal nativa de LordOS, inspirada en iTerm2. No es una app Linux dentro de LordOS:
la emulación, el PTY y el dibujo están escritos desde cero en C dentro de `lord-server`,
y la interfaz (pestañas, paneles, perfiles, menús) es una app React como las demás.

```
apps/terminal/          React: pestañas, paneles, perfiles, buscador, toolbelt, atajos
      │  lord.term.* (server/src/ui/js_term.c)
      ▼
server/src/term/
  term.c        sesiones: PTY ↔ emulador, teclado, mouse, eventos, Instant Replay, registro,
                tmux -CC (paneles virtuales), guardar/recuperar pantalla
  vt.c          emulador xterm-256color: parser VT500, grilla + historia, reflujo, selección,
                búsqueda, marcas, grafemas, OSC 7/8/52/133/1337, imágenes (iTerm2 y Sixel)
  pty.c         posix_openpt + fork/exec del shell del usuario, sin privilegios
  hold.c        guardianes: mantienen vivos los shells si lord-server se reinicia
  term_paint.c  dibujo de la grilla: fuentes, ligaduras, emoji, cajas, cursor, selección, imágenes
server/src/ui/otf.c     OpenType propio: sustituciones GSUB (ligaduras, emoji) y glifos CBDT
server/src/ui/fonts.c   catálogo de fuentes instaladas (TTF/OTF/TTC)
apps/terminal/iterm.js  importador de perfiles de iTerm2 (JSON, .itermcolors, plist XML/binario)
apps/terminal/tmux.js   protocolo de tmux en modo control → pestañas y paneles nativos
assets/shell/           integración con bash (lord-bash.sh), zsh (zsh/.zshenv + lord-zsh.zsh)
                        y fish (lord-fish.fish)
assets/fonts/JetBrainsMono-*.ttf  fuente monoespaciada incluida (OFL)
```

Por qué en C: `cat` de un archivo grande, `htop` o una compilación generan megabytes por
segundo. Pasarlos por JavaScript sería lento en máquinas modestas; acá el texto va del PTY
a la grilla y de la grilla a la pantalla sin tocar QuickJS. Solo el *daño* (las filas que
cambiaron) se repinta.

## Capturas (VM, lord-server real)

| | |
|---|---|
| ![paneles](m4-terminal-paneles.png) | ![esquemas](m4-terminal-esquemas.png) |
| Paneles divididos, `top` en pantalla alternativa | Preferencias → Colores |
| ![transparencia](m4-terminal-transparencia.png) | ![desplegable](m4-terminal-desplegable.png) |
| Dracula con transparencia y desenfoque | Ventana desplegable (F12), toolbelt, marcas de tiempo |
| ![imgcat](m4-terminal-imgcat.png) | ![sixel](m4-terminal-sixel.png) |
| `imgcat` e insignia | Sixel |
| ![regex](m4-terminal-regex.png) | ![replay](m4-terminal-replay.png) |
| Búsqueda con expresión regular | Instant Replay |
| ![colores](m4-terminal-colores.png) | ![tema](m4-terminal-cambio-tema.png) |
| SGR: negrita, itálica, subrayado, tachado, color directo | El mismo shell (PID 4126) sigue vivo tras pasar al tema Clásico |
| ![ligaduras](m4-terminal-ligaduras.png) | ![emoji](m4-terminal-emoji.png) |
| Perfil importado de iTerm2: Fira Code con ligaduras, Dracula, disparador que resalta «ERROR» | Ligaduras, combinantes y emoji a color; abajo con el modo 2027 (👍🏽 🇦🇷 👨‍👩‍👧 en un glifo) |
| ![importar](m4-terminal-importar-iterm2.png) | ![teclas](m4-terminal-teclas.png) |
| Importar el plist binario de preferencias de iTerm2 | Teclas por perfil y globales, atajos editables |
| ![recuperada](m4-terminal-sesion-recuperada.png) | ![tmux](m4-terminal-tmux.png) |
| Tras reiniciar lord-server: mismas pestañas y paneles, el contador siguió corriendo | `tmux -CC`: ventana de tmux con 3 paneles nativos (recuperada tras un reinicio) |
| ![fish](m4-terminal-fish.png) | ![host](m4-terminal-perfil-host.png) |
| Integración con fish (marcas rojas/azules, directorio) | Cambio automático de perfil por `root@*` |

## Rendimiento (VM de desarrollo, 1280×800, CPU)

| Prueba | Resultado |
|---|---|
| `seq 1 1000000` (6,9 MB) con transparencia + desenfoque | 2,1 s |
| RAM del servidor con 3 terminales abiertas | ~39 MB |
| Carga del tema con la app Terminal incluida | ~40 ms |
| RAM del servidor con 6 sesiones + 3 paneles de tmux + fuente de emoji | ~43 MB |
| Cada guardián de sesión (`lord-term-holder`) | ~2,2 MB |

## Paridad con iTerm2

Estado: ✅ hecho y probado en la VM · 🟡 parcial · ⏳ pendiente · — no aplica en LordOS.

### Emulación

| Función | Estado | Notas |
|---|---|---|
| xterm-256color, color directo (24 bits) | ✅ | SGR completo, `38;2`, `38;5`, subparámetros con `:` |
| Negrita, tenue, itálica, subrayado (simple, doble, ondulado), tachado, sobrerrayado, video inverso, parpadeo, oculto | ✅ | |
| Pantalla alternativa, regiones de scroll, inserción/borrado, tabulaciones, juego gráfico DEC | ✅ | probado con `top`, `less`, `vi` |
| Unicode con caracteres anchos (CJK) | ✅ | `wcwidth` de glibc; hace falta una fuente CJK instalada (en la VM no hay: se ven cajas) |
| Caracteres combinantes (é, ñ con acento aparte) | ✅ | grafemas en una tabla global; el texto copiado los conserva |
| Grafemas de emoji (👍🏽, banderas, 👨‍👩‍👧) | ✅ | con el modo 2027 (como contour/wezterm/foot); sin él se respeta `wcwidth` para no desalinear el shell |
| Reflujo del texto al cambiar el tamaño | ✅ | también con caracteres anchos |
| Modos de mouse (1000/1002/1003/1006), foco (1004), pegado entre corchetes (2004), salida sincronizada (2026) | ✅ | |
| Respuestas DA, DSR/CPR, DECRQM, XTVERSION, tamaño de ventana | ✅ | |
| Hipervínculos OSC 8, título OSC 0/2, directorio OSC 7, portapapeles OSC 52 (solo escritura) | ✅ | leer el portapapeles desde el shell no se permite |
| Notificaciones OSC 9 | ✅ | aviso dentro de la ventana |
| Colores dinámicos OSC 4/10/11/12/104 | ✅ | |
| Imágenes en línea de iTerm2 (OSC 1337 File, `imgcat`) | ✅ | PNG/JPEG/GIF (primer cuadro), con `-w`/`-h` |
| Sixel | ✅ | probado con una imagen sixel generada con `printf` (ver `docs/m4-terminal-sixel.png`) |
| Insignia (OSC 1337 SetBadgeFormat), marcas, CurrentDir, SetProfile, CursorShape, ReportCellSize, Copy, ClearScrollback, RequestAttention | ✅ | |
| Integración con el shell (OSC 133): marcas de prompt, código de salida, comando ejecutado | ✅ | bash, zsh y fish |
| Cajas, bloques y flechas de powerline dibujados a mano | ✅ | empalman sin huecos |
| Elección de fuente y fuente "no ASCII" | ✅ | fuentes monoespaciadas instaladas; negrita simulada si la familia no tiene |
| Ligaduras | ✅ | GSUB propio (`calt`/`liga`); mismos glifos que HarfBuzz en las pruebas (JetBrains Mono, Fira Code) |
| Árabe, hebreo, índicas, tailandés (bidi) | ✅ | Orden visual (bidi de Unicode) y palabras armadas con su ancho natural; no en pantalla alternativa; opción por perfil (iTerm2 lo tiene como experimental). Ver `IDIOMAS.md` |
| Emoji a color | ✅ | CBDT (Noto Color Emoji); COLR/SVG no |
| Integración con tmux (`tmux -CC`) | ✅ | ventanas → pestañas, paneles → paneles nativos; ver abajo |

### Ventanas, pestañas y paneles

| Función | Estado | Notas |
|---|---|---|
| Pestañas (nueva, cerrar, mover, ❖1…❖9, actividad, campana, color, título editable) | ✅ | |
| Paneles divididos (derecha/abajo), redimensionar arrastrando, navegar, maximizar panel, igualar, mover a pestaña | ✅ | |
| Atenuar paneles inactivos | ✅ | |
| Varias ventanas | ✅ | gestor de ventanas del SDK |
| Ventana desplegable (hotkey window) | ✅ | F12 o ❖\` |
| Transparencia y desenfoque | ✅ | |
| Barra de estado | ✅ | directorio, proceso, código de salida, tamaño, hora |
| Toolbelt | ✅ | trabajos, comandos recientes, salida capturada, portapapeles, directorios, perfiles, notas |
| Guardar/restaurar disposición de ventanas | ✅ | en `settings.json` |
| Deshacer cierre de panel | ✅ | 5 s, ❖Z |
| Abrir rápido (buscar sesiones) | ✅ | ❖⇧O |
| Restaurar sesiones al cambiar de tema | ✅ | se reabren las mismas ventanas, pestañas y paneles |
| Sesiones que sobreviven a un reinicio del servidor | ✅ | guardianes (ver abajo); se recupera pantalla, historia y disposición |
| Pantalla completa | ✅ | maximiza la ventana |

### Texto, selección y búsqueda

| Función | Estado | Notas |
|---|---|---|
| Selección por arrastre, doble clic (palabra), triple (línea), cuádruple (inteligente: URL/ruta), rectangular (Alt) | ✅ | |
| Copiar al seleccionar, clic medio pega, Shift extiende | ✅ | |
| Modo copia con teclado (estilo vi) | ✅ | ❖⇧C |
| Buscar con resaltado, mayúsculas/minúsculas y expresiones regulares | ✅ | |
| Autocompletar con palabras de la pantalla | ✅ | ❖; |
| Historial de pegado, historial de comandos | ✅ | ❖⇧H, ❖⇧; |
| Pegado especial (en una línea, entre comillas) | ✅ | |
| Compositor (escribir el comando antes de mandarlo) | ✅ | ❖⇧. |
| Marcas: saltar entre prompts, agregar, seleccionar salida del último comando | ✅ | |
| Marcas de tiempo por línea | ✅ | ❖⇧E |
| Ctrl/❖+clic en rutas y enlaces | 🟡 | rutas de `$HOME` abren Archivos; las URLs se copian (no hay navegador) |
| Instant Replay | ✅ | ❖⌥B, hasta 1200 cuadros / 6 MB por sesión |
| Registrar la sesión en un archivo, guardar el contenido | ✅ | `~/.local/share/lordos/terminal/`, `~/Documentos` |
| Transmitir la entrada a todos los paneles | ✅ | ❖⌥I |
| Avisar cuando termine el comando | ✅ | ❖⌥A |

### Perfiles

| Función | Estado | Notas |
|---|---|---|
| Varios perfiles, duplicar, perfil por defecto, perfil por pestaña | ✅ | |
| 16 esquemas incluidos y colores editables (16 ANSI + texto, fondo, negrita, enlaces, cursor, selección) | ✅ | |
| Tamaño de letra, interlineado, margen, cursor (bloque/subrayado/barra, parpadeo, guía) | ✅ | |
| Negrita brillante, fuente negrita/itálica, texto parpadeante, contraste mínimo | ✅ | |
| Directorio inicial (personal, el anterior, otro), insignia, color de pestaña | ✅ | |
| Historia configurable, reporte de mouse, rueda en pantalla alternativa, campana, caracteres de palabra, Alt como Esc+ | ✅ | |
| Disparadores (aviso, campana, atención, enviar texto, capturar, resaltar, marca, título, detener) | ✅ | se pueden desactivar uno por uno |
| Cambio automático de perfil por directorio y por usuario@host | ✅ | usuario@host lo informa la integración del shell (OSC 1337 RemoteHost) |
| Atajos de teclado configurables | ✅ | Preferencias → Teclas |
| Teclas por perfil y globales (enviar texto, Esc+, hex, ignorar, acción) | ✅ | las del perfil ganan a las globales y a los atajos |
| Enviar texto al abrir, tamaño de ventanas nuevas en columnas × filas | ✅ | |
| Importar perfiles de iTerm2 | ✅ | ver abajo |

### No aplica o pendiente por diseño

| Función de iTerm2 | Estado |
|---|---|
| Gestor de contraseñas, API de Python, asistente de IA | — no se exponen secretos ni scripting al tema |
| Disparadores que corren scripts o coprocesos, comandos propios por perfil | — LordOS solo abre el shell del usuario; un "comando" importado se escribe en el shell al abrir |
| Imagen de fondo, espaciado horizontal | ⏳ |
| Emoji COLR/SVG, ligaduras con posicionamiento GPOS (marcas sobre letras con más precisión) | ⏳ |
| Actualizaciones automáticas, Sparkle | — se actualiza con LordOS |

## Atajos

❖ = tecla Super (cumple el papel de Cmd). Casi todos tienen variante con Ctrl+Shift.
Super sola abre el Launchpad solo si se suelta sin haber hecho un atajo.

| Acción | Atajo |
|---|---|
| Nueva pestaña / ventana | ❖T · ❖N |
| Cerrar panel / pestaña / deshacer | ❖W · ❖⇧W · ❖Z |
| Dividir a la derecha / abajo | ❖D · ❖⇧D |
| Panel siguiente / anterior / en dirección | ❖] · ❖[ · ❖⌥flechas |
| Pestaña siguiente / anterior / número | ❖⇧] · ❖⇧[ · ❖1…❖9 |
| Buscar / siguiente / anterior | ❖F · ❖G · ❖⇧G |
| Copiar / pegar / historial de pegado | ❖C · ❖V · ❖⇧H |
| Modo copia / Instant Replay | ❖⇧C · ❖⌥B |
| Agrandar / achicar / tamaño real | ❖= · ❖- · ❖0 |
| Marca anterior / siguiente / agregar | ❖⇧↑ · ❖⇧↓ · ❖⇧M |
| Limpiar búfer / seleccionar todo | ❖K · ❖A |
| Preferencias / toolbelt / marcas de tiempo | ❖, · ❖⇧B · ❖⇧E |
| Ventana desplegable | F12 |

La lista completa está en `apps/terminal/keys.js` y en Preferencias → Teclas, donde
también se cambian (clic en el atajo y apretar la combinación nueva). Los cambios se
guardan en `settings.json` (`terminal.shortcuts`, `terminal.keyMap`, `perfil.keyMap`).

## Fuentes, ligaduras y emoji

- Preferencias → Texto lista las fuentes **monoespaciadas** instaladas (`assets/fonts`,
  `/usr/share/fonts`, `/usr/local/share/fonts`, `~/.local/share/fonts`, `~/.fonts`). El
  catálogo lo arma `server/src/ui/fonts.c` leyendo solo las tablas `name`, `OS/2`, `post` y
  `hmtx` con un parser propio que valida límites; stb_truetype abre únicamente la elegida.
- Las ligaduras aplican las features `calt`/`liga`/`clig` con `server/src/ui/otf.c`
  (lookups 1, 2, 4, 5, 6 y 7). `make -C server test` compila `build/shape-test` para
  comparar con HarfBuzz; con JetBrains Mono y Fira Code los resultados coinciden.
- Emoji: la primera fuente CBDT instalada (en Debian, `fonts-noto-color-emoji`). Los PNG se
  decodifican con stb_image y se escalan a dos celdas. Las secuencias (👍🏽, 🇦🇷, 👨‍👩‍👧) se
  componen con su `ccmp`.
- Respaldo para caracteres que faltan: fuente del perfil → fuente "no ASCII" → JetBrains
  Mono → DejaVu Sans Mono → Noto Sans Mono → DejaVu Sans → Noto Sans Symbols → Inter →
  emoji → caja vacía.

## Importar perfiles de iTerm2

Preferencias → **Importar de iTerm2…** acepta:

| Archivo (exportado en el Mac) | Qué trae |
|---|---|
| `Profiles.json` (Other Actions → Copy All Profiles as JSON / Save Profile as JSON) o un Dynamic Profile | perfiles |
| `*.itermcolors` | esquema de colores (aparece en Colores → Importados de iTerm2) |
| `~/Library/Preferences/com.googlecode.iterm2.plist` (binario o XML) | todos los perfiles, el perfil por defecto, esquemas propios y atajos globales |

Se convierten: colores (sRGB y Display P3, con variantes claro/oscuro), fuente y tamaño,
fuente no ASCII, ligaduras, interlineado, cursor, negrita/itálica, contraste mínimo,
transparencia y desenfoque, columnas × filas, historia, reporte de mouse, Option como
Esc+, campana, confirmar al cerrar, directorio inicial, insignia, color de pestaña,
"Bound Hosts" (cambio automático), disparadores, teclas del perfil y globales, texto
inicial. Lo que LordOS no tiene se lista como aviso antes de importar. Volver a importar
un perfil con el mismo GUID lo actualiza en lugar de duplicarlo. Verificado en la VM con
`.itermcolors` reales (Dracula, Catppuccin) y con un plist binario de preferencias.

## Sesiones que sobreviven a un reinicio

Cada sesión tiene un **guardián** (`lord-term-holder`, el mismo binario re-ejecutado con
`--term-holder`): un proceso de ~2 MB sin privilegios que solo conserva abierto el maestro
del PTY y escucha en un socket unix privado (`/run/user/<uid>/lordos-term/`, 0700). Si
lord-server termina por una señal (reinicio, actualización) el shell sigue corriendo; al
arrancar, el servidor se conecta a cada guardián (solo del mismo usuario, `SO_PEERCRED`) y
recibe el PTY por `SCM_RIGHTS`. Antes de salir guarda pantalla e historia (hasta 5000
líneas, con colores) en un `.state` al lado del socket; la app guarda la disposición de
ventanas → pestañas → paneles en `terminal.live`. Cerrar sesión desde el tema o
Ctrl+Alt+Backspace sí cierra los shells. Instant Replay, imágenes y la historia más vieja
que 5000 líneas no se conservan.

## tmux -CC

Correr `tmux -CC` (o `tmux -CC attach -t sesión`) en una pestaña la convierte en la
"puerta" del modo control de tmux: se abre otra ventana donde cada ventana de tmux es una
pestaña y cada panel un panel nativo (con su historia, colores y cursor). En esa ventana:

| Acción en LordOS | Comando de tmux |
|---|---|
| ❖T / ❖D / ❖⇧D | `new-window` / `split-window -h` / `split-window -v` |
| ❖W / ❖⇧W | `kill-pane` / `kill-window` |
| Clic en un panel, igualar, maximizar panel | `select-pane`, `select-layout tiled`, `resize-pane -Z` |
| Cambiar el tamaño de la ventana | `refresh-client -C ancho×alto` |
| Esc en la puerta o cerrar la ventana | `detach-client` (la sesión de tmux sigue viva) |

La salida de cada panel (`%output`) va del parser en C directo a su sesión virtual; lo
que se escribe vuelve como `send-keys -H`. Si lord-server se reinicia con tmux conectado,
la puerta vuelve al modo control y las ventanas de tmux se reabren solas. Probado con tmux
3.5a en la VM: adjuntar, escribir en un panel, `split-window -v`, `kill-pane`, `new-window`,
`refresh-client`, salida en vivo, recuperación tras reiniciar y `detach-client`. Igualar y
maximizar panel están implementados pero sin prueba en pantalla.

## Integración con el shell

Se usa el shell de login de `/etc/passwd`:

| Shell | Cómo se carga | Probado |
|---|---|---|
| bash | `bash --rcfile assets/shell/lord-bash.sh -i` (carga `/etc/profile` y `~/.bash_profile`/`~/.profile` antes) | ✅ |
| zsh | `ZDOTDIR=assets/shell/zsh`: su `.zshenv` restaura el `ZDOTDIR` del usuario, carga su `.zshenv` y registra `lord-zsh.zsh`; después zsh lee `.zprofile`/`.zshrc` normalmente | ✅ zsh 5.9 |
| fish | `fish -l --init-command "source …/lord-fish.fish"` (después de la configuración del usuario) | ✅ fish 4.0 |
| otros | el shell de login, sin integración | |

Las tres emiten OSC 133 (prompt, comando, salida, código de salida), OSC 7 (directorio) y
OSC 1337 RemoteHost (usuario@host, para el cambio automático de perfil) y definen
`imgcat [-w ancho] [-h alto] archivo…` y `lord_badge texto`.

## Seguridad

- `lord.term.create()` es la única API de LordOS que inicia un proceso. Solo lanza el shell
  de login del usuario que ya corre la sesión, sin argumentos elegidos por JS (directorio
  inicial aparte).
- El hijo no hereda capacidades (se limpian con `capset`), señales bloqueadas, descriptores
  del servidor ni las variables `SUDO_*`.
- OSC 52 solo puede **escribir** el portapapeles; leerlo desde un programa no está permitido.
- El registro de sesión y "Guardar contenido" escriben solo dentro de `$HOME`.
- Los guardianes no leen ni escriben el PTY ni ejecutan nada; su socket y los `.state`
  (que contienen el texto de la pantalla) están en una carpeta 0700 del usuario.
- `lord.term.tmuxCommand()` solo funciona mientras esa sesión está en modo control y manda
  una línea sin saltos: es lo mismo que el usuario podría escribir en ese tmux.
- `lord.fs.read()` (para el importador) lee archivos regulares de `$HOME`, máximo 8 MB.
- Las fuentes se buscan solo en carpetas de fuentes; las de `~/.fonts` son del propio usuario.

## Pruebas

```sh
make -C server test            # compila build/vt-dump y build/shape-test
printf 'hola\033[31m rojo\r\n' | server/build/vt-dump 20 5 12 6 rojo
#                                       columnas filas  → redimensiona a 12×6 y busca "rojo"
printf 'e\xcc\x81\033[?2027h\xf0\x9f\x87\xa6\xf0\x9f\x87\xb7' | server/build/vt-dump 10 2   # [é][🇦🇷]
server/build/shape-test /usr/share/fonts/truetype/firacode/FiraCode-Regular.ttf liga 'a -> b'
```

En la VM, `tools/lord-type.py` escribe texto con el teclado virtual de QEMU (ver
`docs/VM-SETUP.md`, paso 8).

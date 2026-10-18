# Documentos (M9)

PDF, comprimidos, fuentes y emojis en LordOS. Este documento cubre:

- el **Visor de PDF**;
- en **Archivos**, los comprimidos y la **Vista rápida**;
- **Tipografías**;
- **Emojis y símbolos**;
- el ayudante encerrado **lord-doc**, que hace el trabajo pesado de todo lo anterior.

La impresión y el escáner están en `IMPRESION.md`.

## Capturas (VM 116, lord-server real)

| | |
|---|---|
| ![índice](m9-docs-pdf-indice.png) | ![búsqueda](m9-docs-pdf-busqueda.png) |
| Visor de PDF: índice del documento (marcadores) a la izquierda, 224 páginas | Búsqueda con resaltado: el resultado actual en naranja, «2 de 8 coincidencias» |
| ![anotaciones](m9-docs-pdf-anotaciones.png) | ![nota](m9-docs-pdf-nota.png) |
| Resaltado y trazo a mano alzada, dibujados por Poppler en la página y en la miniatura | Nota: el ícono queda en la página y se guarda en la copia |
| ![formulario](m9-docs-pdf-formulario.png) | ![oscuro](m9-docs-pdf-oscuro.png) |
| Formulario (campos de texto de un PDF de CUPS) completado | Página oscura: la luminosidad invertida, conservando los colores |
| ![rotar](m9-docs-pdf-rotar.png) | ![vista rápida](m9-docs-vista-rapida-pdf.png) |
| Rotado un cuarto de vuelta; los campos siguen en su lugar | Vista rápida de un PDF: la primera página y la cantidad de páginas |
| ![fuente](m9-docs-vista-rapida-fuente.png) | ![zip](m9-docs-vista-rapida-zip.png) |
| Vista rápida de una fuente `.ttf` | Vista rápida de un `.zip`: cantidad, tamaño y lo que tiene adentro |
| ![menú](m9-docs-comprimidos-menu.png) | ![adentro](m9-docs-comprimidos-adentro.png) |
| «Extraer aquí» y «Comprimir» en el menú contextual | Un `.zip` abierto como una carpeta |
| ![extraer](m9-docs-comprimidos-extraer.png) | ![comprimir](m9-docs-comprimidos-comprimir.png) |
| Un zip con `../` en los nombres: lo sano se extrae, 2 entradas se saltean por seguridad | «Comprimir» una carpeta: «Fotos-del-viaje 2.zip», 3 archivos |
| ![tipografías](m9-docs-tipografias-muestra.png) | ![caracteres](m9-docs-tipografias-caracteres.png) |
| Tipografías: muestra con texto y tamaños a elección | Tabla de caracteres por bloque de Unicode |
| ![archivo](m9-docs-tipografias-archivo.png) | ![instalada](m9-docs-tipografias-instalada.png) |
| Un `.ttf` abierto desde Archivos, sin instalar | Instalada para el usuario (`~/.local/share/fonts`), con «Quitar» |
| ![selector](m9-docs-emoji-selector.png) | ![editor](m9-docs-emoji-en-editor.png) |
| ❖. en el Editor: búsqueda «corazon» (34 resultados), tonos de piel | 😍 y 📕 escritos en el Editor por el camino del método de entrada |
| ![emojis](m9-docs-emoji-app.png) | ![símbolos](m9-docs-simbolos-app.png) |
| Emojis y símbolos: categoría con tono de piel | Símbolos de Unicode por bloque (flechas) |
| ![rtl](m9-docs-pdf-rtl.png) | |
| En árabe: la interfaz espejada (barra lateral a la derecha) y la página en su sentido; el campo se completa igual | |

## Visor de PDF (`apps/pdf`)

Al estilo de Vista Previa de macOS:

- **Ver:**
  - desplazamiento continuo o de a una página;
  - barra lateral con miniaturas o con el índice del PDF (plegable);
  - zoom (−/+, ajustar al ancho, página entera, tamaño real), con ❖ y la rueda;
  - rotar ↻;
  - página oscura: invierte la luminosidad y conserva el matiz (◐).
- **Buscar:** el campo de la barra (❖F). Resalta todo, marca en naranja el resultado actual y salta
  a él (Enter y ⇧Enter, ❖G). Los resultados llegan página por página: los primeros aparecen
  enseguida, aunque el documento tenga cientos de páginas.
- **Texto:** arrastrar selecciona (letra por letra, palabra con doble clic) y ❖C copia. ❖A
  selecciona la página.
- **Ir a una página:** con un clic en «Página N de M» o con ❖⌥G.
- **Enlaces:** los internos llevan a la página; los externos se copian al portapapeles (LordOS no
  tiene navegador propio todavía).
- **Anotaciones:**
  - resaltar ▬ (cinco colores);
  - nota 🗨 (un texto en un punto);
  - a mano alzada ✎.

  ❖Z deshace la última anotación. Todo se guarda **como copia**, «nombre (anotado).pdf» al
  lado del original, con ❖S o Archivo → Guardar una copia. El original no se toca nunca.
- **Formularios:** los campos de texto, las casillas, los botones de opción y las listas se
  completan en el lugar, y se guardan con la copia.
- **Imprimir** (❖P o ⎙): abre el diálogo del sistema con el documento tal como está en pantalla,
  incluidos los formularios y las anotaciones.
- **PDF con contraseña:** se pide antes de abrir.
- **Si el ayudante se cae** con un PDF dañado, el visor lo dice y ofrece volver a abrirlo. El
  escritorio sigue igual.

Cómo dibuja:

- Las páginas se piden a lord-doc solo para lo que se ve, más las vecinas con menos prioridad.
- Un pedido que deja de hacer falta (la página salió de la vista) se cancela antes de empezar.
- Mientras llega una página con el zoom nuevo, se muestra la versión que haya de esa página,
  escalada: nunca queda un hueco.
- **Caché** (`apps/pdf/cache.js`): 48 MB de páginas y 8 MB de miniaturas por documento. Encima
  hay un tope de 160 MB para todas las imágenes que guarda lord-server (`doc.c`).

## Comprimidos en Archivos

- **Mirar por dentro:** doble clic en un `.zip`, `.tar`, `.tar.gz/.xz/.zst/.bz2`, `.tgz`, `.7z`,
  `.gz`… lo abre como una carpeta (`{ type: 'archive', path, inner }` en `apps/files/archive.js`).
  - Las carpetas de adentro se recorren igual que las de verdad.
  - Abrir o ver con Espacio un archivo de adentro extrae **solo ese** a
    `~/.cache/lordos/archives/<id>/` y lo abre con su app.
  - Adentro del comprimido no se puede borrar, renombrar ni pegar.
  - «Extraer N elementos» saca lo elegido a una carpeta nueva al lado del comprimido.
- **Extraer aquí:**
  - crea una carpeta nueva con el nombre del comprimido sin la extensión; si ya existe, «nombre 2»;
  - muestra el progreso en la barra de estado;
  - al final dice cuántos archivos salieron y cuántos se saltearon por seguridad.
- **Comprimir:** la selección va a `.zip` al lado: «nombre.zip» si es un solo elemento, o
  «Archivo.zip». Lleva carpetas recursivas, enlaces simbólicos como enlaces y nombres en UTF-8.
- **Formatos que lee** libarchive: zip, tar (gz, xz, zst, bz2, lzma, lz), 7z, cpio, iso, ar,
  rar (solo listar y extraer, según libarchive) y `.gz/.xz/.zst/.bz2` sueltos. Escribe solo zip.
- **Archivos cifrados:** se listan con su candado; extraerlos no está soportado todavía (se
  saltean con el motivo «encrypted»).

### Extraer sin salir de la carpeta

Un comprimido malicioso no puede escribir fuera de la carpeta de destino. Hay tres capas:

1. **Los nombres** (`server/src/doc/arcpath.c`, probado aparte):
   - se rechazan las rutas absolutas, cualquier tramo `..`, los caracteres de control, los tramos
     de más de 255 bytes y más de 64 niveles;
   - un **enlace simbólico** solo se crea si su destino es relativo, tiene los `..` solo al
     comienzo y no sube por encima de la raíz. Así, aunque un tramo sea otro enlace del mismo
     comprimido, tampoco sale;
   - los **enlaces duros** solo pueden apuntar a otra entrada de adentro;
   - no se crean dispositivos, FIFOs ni sockets;
   - los permisos quedan sin setuid, setgid ni sticky.
2. **El disco** (`h_arc.c`):
   - cada carpeta se abre tramo por tramo desde el fd de destino, con `O_NOFOLLOW`: un enlace no
     sirve de puente;
   - los archivos se crean con `O_EXCL` y nunca pisan uno que ya esté;
   - hay topes: 500.000 entradas, 64 GB escritos y 128 MB libres en el disco, contra las
     «bombas».
3. **El encierro:** lord-doc corre bajo Landlock con escritura **solo** debajo del fd de destino
   (`rw_dir_fds`), sin red y con seccomp.

Probado en `tests/doc_evil.sh` con los comprimidos de `tests/make_evil_archives.py`:

- zip slip (`../`, `a/../../`);
- ruta absoluta (`/tmp/…`);
- enlace a `..` y a `/etc` seguido de un archivo «a través» del enlace;
- cadena de enlaces que juntos salen;
- enlace duro a `/etc/passwd`;
- dispositivo, FIFO y setuid;
- archivo repetido (no pisa);
- nombres en varias escrituras;
- basura con extensión `.zip`;
- `.gz` suelto.

Resultado: nada fuera del destino, en 31 de 31 comprobaciones (con el encierro y los formatos).

## Vista rápida (Espacio)

`apps/files/quicklook.jsx`. Lo que muestra según el tipo de archivo:

| Archivo | Qué muestra | Quién lo hace |
|---|---|---|
| imagen PNG/JPEG | la imagen | stb_image en lord-server, como antes |
| PDF | la primera página y la cantidad de páginas | la sesión de utilidades de lord-doc, ya caliente |
| texto y código | las primeras líneas | — |
| fuente (`.ttf/.otf/.ttc`) | una muestra en varios tamaños | FreeType en lord-doc |
| comprimido | cantidad de archivos, tamaño y las primeras entradas | — |
| audio y video | un cuadro o la tapa, si el SDK trae `media.poster` (el agente de medios); si no, el ícono | agente de medios |
| archivo de adentro de un comprimido | lo mismo, después de extraer solo ese a la caché | lord-doc |

Las flechas pasan al siguiente; Espacio o Esc cierran.

## Tipografías (`apps/fonts`)

- **Colecciones:** todas, del usuario, monoespaciadas y a color, con búsqueda por familia.
- **Muestra** con el texto que se escriba y un tamaño de 8 a 120 px, y el alfabeto en cuatro
  tamaños. Cada estilo de la familia se elige arriba.
- **Caracteres:** la tabla por bloque de Unicode, solo de los bloques que la fuente tiene. Con
  un clic se ven el carácter grande y su U+XXXX, y se copia.
- **Información:** familia, estilo, nombre PostScript, glifos, caracteres, tipo, archivo y tamaño.
- **Instalar:** al abrir un `.ttf/.otf/.ttc` desde Archivos, «Instalar» lo copia a
  `~/.local/share/fonts`. Si esa familia y estilo ya están instalados, lo dice en vez de copiar.
- **Quitar:** solo las del usuario; van a la Papelera.

Las muestras y la tabla las dibuja lord-doc con FreeType y cairo. El catálogo es el de siempre,
`lord.fonts.list`, que ahora trae también `path` e `index`. Las fuentes nuevas no se leen en
lord-server hasta que se instalan.

## Emojis y símbolos

- **El selector** (`sdk/emoji.jsx`, lo monta `startShell` en todos los temas):
  - se abre con **❖.** o Ctrl+❖Espacio, debajo del cursor del campo con foco, o al medio de la
    pantalla;
  - **busca** por nombre y palabras clave en español, inglés y portugués a la vez (CLDR, sin
    importar tildes): «corazon», «heart» y «coração» encuentran lo mismo;
  - tiene **categorías** (Tab pasa de una a otra), **recientes** y **tono de piel** (los dos se
    guardan en los ajustes);
  - las flechas eligen y Enter escribe.
- **Cómo escribe:** por el **mismo camino que el método de entrada**. El texto llega como una
  tecla que escribe ese texto, igual que `ime_commit` en `js.c`, así que sirve en los `TextInput`,
  el Editor, Notas y la Terminal:
  - en una **app Linux** con foco, lo pega con el portapapeles (`lord.wayland.paste`), como el
    panel del portapapeles;
  - sin ningún campo con foco, lo copia y avisa.
- **La app «Emojis y símbolos»** (`apps/characters`) es lo mismo en una ventana, más los
  **símbolos de Unicode por bloque**: flechas, matemática, monedas, cajas, braille, griego…
  Un clic elige y «Copiar» lo deja en el portapapeles.
- **Los datos:**
  - `sdk/emojidata.js`: 1.898 emojis por categoría, sin textos, y los 323 que admiten tono;
  - `assets/locales/{es,en,pt}/emojikw.json`: los nombres y las palabras clave.

  Los genera `tools/gen-emoji.mjs` a partir de Unicode emoji-test 15.1 y CLDR 45 (Unicode
  License v3).

## lord-doc: el ayudante encerrado

```
lord-server (sin Poppler ni libarchive)
  └── doc.c: un hilo propio con poll() sobre los ayudantes; cola por prioridad; plazos
        ├── lord-doc  sesión "pdf"   un PDF abierto (Visor, vista previa de impresión)
        ├── lord-doc  sesión "arc"   un listado, una extracción o una compresión
        └── lord-doc  sesión "util"  larga: fuentes, PDF para imprimir, miniaturas, imágenes
```

- **Binario:** `server/build/lord-doc` (`src/doc/helper.c`, `h_pdf.c`, `h_arc.c`, `h_font.c`,
  `h_make.c`).
  - Enlaza Poppler (interfaz de glib) y libarchive, y con Poppler vienen cairo, FreeType y
    fontconfig.
  - lord-server **no** enlaza ninguna de esas bibliotecas.
  - Si faltan los `-dev`, `make` arma todo lo demás y los documentos dicen que el ayudante no
    está.
- **Cómo se lanza:**
  - lo lanza `doc.c` con **ruta fija**: la carpeta del propio `lord-server` + `/lord-doc`;
  - tiene que ser de root o del mismo dueño que lord-server, y no escribible por otros;
  - sin argumentos y con un entorno mínimo: HOME, `LANG=C.UTF-8` y la carpeta de fuentes de
    LordOS;
  - el canal es un `socketpair` SOCK_SEQPACKET en el fd 3.
- **Archivos:** el ayudante **no abre nada por ruta**. lord-server valida la ruta (carpeta
  personal o montajes del usuario; para fuentes, también `/usr/share/fonts`) y le pasa el fd con
  SCM_RIGHTS:
  - para leer, el documento;
  - para extraer, el fd de la carpeta de destino (creada por lord-server, nueva salvo en la caché);
  - para guardar una copia o un `.zip`, un archivo nuevo abierto con `O_EXCL`.
- **El encierro** (`sandbox_apply_helper`, el módulo común `server/src/sec/sandbox.c`), aplicado
  apenas recibe esos fds y antes de leer un byte del documento:
  - **Landlock:**
    - solo lectura de `/usr`, `/etc/fonts`, la caché de fontconfig y las fuentes del usuario;
    - escritura solo debajo de la carpeta de destino (`rw_dir_fds`), y lectura de las carpetas
      a comprimir (`ro_dir_fds`);
    - ninguna conexión TCP.
  - **seccomp:**
    - sin `socket`, `connect`, `bind`, `execve` ni `ptrace`;
    - `clone` solo para hilos;
    - las syscalls están en una lista por arquitectura (x86-64 y ARM64).
  - **Además:** sin capacidades, `no_new_privs`, cgroup propio `lordos-doc.scope` (768 MB/1 GB),
    `RLIMIT_AS` de 1,5 GB, `PR_SET_PDEATHSIG` y sin volcados de memoria.
  - **Probado:** `build/doc-test … selftest` y `selftest-arc` intentan lo prohibido. No pueden
    abrir `~/.ssh` ni `~/.bashrc`, escribir en `/tmp`, abrir sockets (ni de red ni unix, o sea ni
    D-Bus, ni CUPS, ni Wayland) ni hacer fork. Las fuentes sí se leen.
- **Lo que vuelve:**
  - **JSON:** lord-server lo valida con el lector estricto de `sec/json.c` antes de pasarlo a JS.
  - **Imágenes:** ARGB32 premultiplicado, el formato del motor, en un memfd. lord-server exige el
    sello `F_SEAL_SHRINK` antes de mapearlo, así un ayudante tramposo no puede provocar SIGBUS
    achicándolo.
  - **PDF armados:** en un memfd sellado (un «blob»).
- **Si algo falla:**
  - el compositor **nunca espera**: los resultados se leen con `lord.doc.poll()` solo mientras
    hay algo pendiente;
  - cada pedido tiene plazo (30 s una página, 60 s abrir; buscar, extraer y comprimir se cortan
    a los 60 s sin progreso). Vencido, el ayudante se mata;
  - si se cae, sus pedidos terminan con `crash`;
  - un PDF o un comprimido malicioso solo puede tirar abajo a su ayudante.

### API

`lord.doc` (`server/src/ui/js_doc.c`) y en el SDK:

| Módulo | Qué hay |
|---|---|
| `sdk/doc.js` | `docAvailable()`, `<DocImage image>`, `releaseImage`, `fontInfo`, `fontSample`, `fontGrid`, `decodeImage`, `convertImage`, `makeTextPdf`, `makeImagePdf`, `pdfThumb`, `saveBlob`, `releaseBlob`, `warmDocs()` |
| `sdk/pdf.js` | `pdf.open(ruta \| { blob })` → `render`, `select`, `find`, `outline`, `pageInfo`, `setField`, `annot`, `undo`, `save`, `blob`, `sizes`, `close`; `pdf.thumb` |
| `sdk/archive.js` | `archive.list`, `extract(ruta, carpeta, { only, existing, onProgress })`, `create([rutas], destino.zip)`, `tree`, `isArchive`, `stem` |
| `sdk/emoji.jsx` | `emoji.groups/search/name/withTone/insert/open`, `EmojiGrid`, `EmojiToneBar` |

## Rendimiento (medido en la VM 116: Xeon Silver 4108 a 1,8 GHz, 4 núcleos, sin GPU)

| Qué | Tiempo |
|---|---|
| Lanzar un lord-doc y encerrarlo | 32 ms |
| Abrir un PDF de 224 páginas (1,4 MB) con los tamaños de todas las páginas | 210 ms (el visor pide primero solo la primera y los demás después, sin frenar el primer dibujo) |
| Primera página en el visor, desde el doble clic (135 %, 1070 px de ancho) | 437–479 ms |
| Página dibujada por Poppler, ya abierto el documento | mediana 20 ms, entre 0,5 y 155 ms según el contenido |
| Miniatura de PDF en la sesión de utilidades caliente (400×480) | 102–150 ms |
| **Vista rápida de un PDF**, desde que se abre hasta tener la página | **174 ms** la primera vez; el ayudante solo, 125–150 ms |
| Vista rápida de texto, código e imágenes | menos de 1 ms (no pasa por el ayudante) |
| Vista rápida de una fuente | 24 ms |
| Vista rápida de un `.zip` (lanza un ayudante y lista) | 146–310 ms; navegando con las flechas suma el redibujo de Archivos (~400 ms) |
| Extraer un `.tar.gz` de 100 MB (232 MB, 8.958 archivos) | 1,59 s (GNU tar: 2,32 s); contenido idéntico, salvo un enlace absoluto que se salteó a propósito |
| Guardar una copia anotada de 1,6 MB | 34 ms |
| RAM de lord-server | 53,6 MB sin documentos, 82 MB con el PDF de 224 páginas abierto a 135 % |
| RAM de los ayudantes | sesión pdf: 87 MB con ese PDF; sesión de utilidades: 25 MB |

**La meta de 150 ms para la Vista rápida:**
- **Se cumple** con texto, imágenes y fuentes; un `.zip` chico, a veces.
- **Un PDF queda en ~175 ms** en esta VM: casi todo es el dibujo de Poppler. La sesión de
  utilidades ya está preparada (Archivos la «calienta» al abrirse, con un PDF mínimo), así que no
  se paga el arranque del ayudante. En una CPU de escritorio actual debería quedar por debajo.

Las mediciones se ven en el registro de la sesión:
- `js: pdf: … primera página en N ms`;
- `js: vista-rapida: …`;
- con el archivo `~/.cache/lordos/doc-trace` (o `LORD_DOC_TRACE=1`), `lord-doc: pedido N: cola X
  ms, ayudante Y ms` por cada pedido.

## Pruebas

```sh
make -C server test        # incluye build/arcpath-test, build/ipp-test y build/doc-test
server/build/arcpath-test  # 37/37: nombres y destinos de enlaces
sh tests/doc_evil.sh       # 31/31 (en server/): comprimidos maliciosos, formatos, encierro y copia anotada
server/build/doc-test server/build/lord-doc pdf archivo.pdf 900   # abrir, dibujar y medir
```

## Pendiente

- **Archivos:**
  - miniaturas de PDF en la vista de íconos (hoy solo en la Vista rápida);
  - extraer comprimidos cifrados (pedir la contraseña);
  - escribir `.tar.gz` o `.7z`.
- **Visor de PDF:**
  - firmas digitales y el panel de anotaciones existentes con su texto;
  - abrir los enlaces externos en un navegador;
  - con la rotación del usuario, las páginas que traen su propia rotación (`/Rotate`) pueden
    tener la búsqueda y los enlaces corridos (no se probó).
- **Escrituras:** el texto que se imprime (Editor, Notas) se arma con fallback por carácter, sin
  armado de escrituras complejas: el árabe y las índicas salen sin unir.
- **Emojis:**
  - las secuencias con ZWJ (❤️‍🔥, 👩‍💻) se ven como sus partes, porque al motor de texto le
    falta unirlas;
  - en apps Linux se escribe pegando, porque no hay `text-input` de Wayland.
- **Sin probar:**
  - la Vista rápida de audio y video con `media.poster`: lord-media no está compilado en la VM 116;
  - la impresión desde Fotos (es del agente de medios; la API está lista: `print.open({ image })`).

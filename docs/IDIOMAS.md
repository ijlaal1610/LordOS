# Idioma y región en LordOS

LordOS maneja el idioma en todo el sistema, no solo en la interfaz. Viene con **español,
inglés y portugués**, y se le agregan idiomas con paquetes, sin tocar código.

| Capa | Qué hace | Dónde |
|---|---|---|
| Interfaz de LordOS | Textos de temas, apps y SDK con `t('clave')`; plurales; fechas y números | `sdk/i18n.js`, `assets/locales/` |
| Región del usuario | `LANG`/`LC_*` en `~/.config/locale.conf`; la heredan los shells (y las apps Linux en M6) | `server/src/ui/js_sys.c` |
| Regiones de glibc | genera `es_AR.UTF-8`, `pt_BR.UTF-8`… (`locale-gen`), región por defecto del sistema | ayudante `server/src/admin.c` |
| Mensajes de los programas | las traducciones de Debian (gettext): `ls`, `date`, `man`… salen en el idioma elegido | Debian |
| Teclado | todas las distribuciones de XKB con libxkbcommon: variantes, AltGr, teclas muertas, compose | `server/src/keyboard.c`, `sdk/keyboard.js` |
| Zona horaria | `timedatectl set-timezone` (o `/etc/localtime`) | ayudante `server/src/admin.c` |
| Paquetes de Debian del idioma | manpages, hunspell, aspell, hyphen, mythes, fuentes de la escritura y diccionario del método de entrada | ayudante `server/src/admin.c` |
| Escrituras complejas | derecha a izquierda (bidi), formas del árabe, marcas, CJK, respaldo de fuentes por escritura | `server/src/ui/bidi.c`, `shape.c`, `otf.c`, `textlayout.c` |
| Interfaz espejada | con árabe, hebreo, persa… toda la interfaz va de derecha a izquierda | `direction` en `layout.c`, `sdk/i18n.js` |
| Métodos de entrada | coreano (hangul), japonés (kana + SKK), chino (pinyin) | `server/src/ime.c`, `sdk/ime.js` |

Todo se maneja desde **Ajustes → Idioma y región** y **Ajustes → Teclado**.

## Capturas (VM, lord-server real)

| | |
|---|---|
| ![pt](m5-terminal-pt.png) | ![shell](m5-region-pt-shell.png) |
| Terminal en portugués (los menús se ajustan al largo del texto) | Con `pt_BR.UTF-8`: `date`, `ls` y `printf` responden como en Brasil |
| ![en](m5-idioma-region-en.png) | ![terminal-en](m5-terminal-en.png) |
| Interfaz en inglés con región pt_BR: nombres en inglés, números y fecha de Brasil | Preferencias de la Terminal en inglés |
| ![paquetes](m5-paquete-idioma.png) | ![xkb](m5-teclado-xkb.png) |
| Instalando los paquetes de Debian de pt_BR (salida de apt en vivo) | Distribución brasileña (XKB): ´+a = á, ç, ~+a = ã |
| ![fr](m5-paquete-frances.png) | |
| Un paquete de idioma nuevo (francés) armado con `i18n-pack` y agregado desde Ajustes | |

## Textos de la interfaz

```js
import { t, useLocale, formatDate, formatTime, formatNumber } from '@lordos/sdk';

t('files.items', { count: 3 })     // "3 elementos" / "3 items" / "3 itens"
t('about.version', { v: '0.5.0', m: 'M5' })
formatDate(new Date(), 'long')     // "jueves, 15 de octubre de 2026"
formatNumber(1234.5, 2)            // "1.234,50" (es_AR) / "1,234.50" (en_US)
```

- La clave es `espacio.resto`; el espacio es el archivo: `t('terminal.action.newTab')` está en
  `assets/locales/<idioma>/terminal.json` bajo `"action.newTab"`.
- Un valor puede ser texto o un objeto de plurales (`{ "one": "…", "other": "…" }`, y
  `few`/`many`/`two`/`zero` según el idioma). Variables: `{nombre}`.
- Si falta una clave en el idioma elegido se usa el inglés; si tampoco está, la clave.
- Los nombres de las apps salen de `apps.json` (`appName(app)`); si no hay traducción, del
  `manifest.json`.
- Al cambiar el idioma se vuelve a montar toda la interfaz. Las ventanas, pestañas y sesiones
  viven en stores y se conservan. Por eso las listas con textos no pueden ser constantes de
  módulo: se arman en una función o al dibujar.
- No hay `Intl` en QuickJS: las reglas de plural (`sdk/i18n.js`) y los formatos
  (`format.json`) son propios.

### Formatos

`format.json` de cada idioma tiene nombres de meses y días, patrones de fecha (`short`,
`long`, `numeric`), separadores decimal y de miles, y reloj de 12 o 24 horas, con variantes
por región (`regions.en_GB`, `regions.pt_PT`…). Como en macOS, los **nombres** salen del
idioma de la interfaz y el **orden, los separadores y el reloj** de la región: inglés con
región pt_BR da «Thursday, October 15, 2026 · 21:56» y «15/10/2026 · 1.234.567,89».

## Paquetes de idioma

Un idioma es una carpeta `<id>/` con `meta.json`, `format.json` y un JSON por espacio:

```
assets/locales/pt/meta.json   { "id": "pt", "name": "Português", "english": "Portuguese",
                                "plural": "zero-one-other", "region": "pt_BR", "direction": "ltr" }
```

Los que trae LordOS están en `assets/locales/`; los del usuario en
`~/.local/share/lordos/locales/` (no necesitan root y ganan si existen los dos).

Para crear uno:

```sh
node tools/i18n-pack.mjs new fr "Français" French zero-one-other fr_FR   # lordos-lang-fr.json, en inglés
# traducir los textos del archivo…
node tools/i18n-check.mjs lordos-lang-fr.json                           # faltantes y variables
```

Después se copia `lordos-lang-fr.json` a `~/Descargas` y se usa **Ajustes → Idioma y región →
Agregar**. Las reglas de plural disponibles: `one-other`, `zero-one-other`, `none`,
`east-slavic`, `polish`, `czech-slovak`, `arabic`. `node tools/i18n-pack.mjs export pt` saca un
idioma existente en el mismo formato.

`node tools/i18n-check.mjs` sin argumentos revisa todos los idiomas incluidos y además las
claves que usa el código (`t('…')` en apps, temas y SDK) y no existen en inglés. Hoy: 391
claves, completas en es, en y pt.

## Ayudante privilegiado (`lord-admin`)

Lo que necesita root lo hace un proceso chico que `lord-server` crea antes de soltar
privilegios (`server/src/admin.c`). No tiene pantalla, input ni otros descriptores. Solo
entiende una lista cerrada de acciones, cada una validada por su lado:

| Acción | Validación | Hace |
|---|---|---|
| `locale-gen es_AR.UTF-8` | la región figura en `/usr/share/i18n/SUPPORTED` como UTF-8 | habilita la línea en `/etc/locale.gen` y corre `locale-gen` |
| `system-locale es_AR.UTF-8` | soportada y ya generada (`locale -a`) | escribe `/etc/default/locale` |
| `timezone America/Sao_Paulo` | nombre seguro y archivo existente en `/usr/share/zoneinfo` | `timedatectl set-timezone` |
| `langpack pt_BR` | `ll` o `ll_CC` | `apt-get install` de `manpages/hunspell/aspell/hyphen/mythes-<ll[-cc]>` que existan + fuentes de la escritura (lista compilada) |

- Solo atiende si el usuario de la sesión está en el grupo `sudo`, `admin` o `wheel`.
- Los programas se ejecutan con ruta absoluta fija y entorno limpio; nunca se arma un comando con texto del usuario.
- La lista de paquetes está compilada dentro del binario, no en un archivo editable.
- Muere con `lord-server` (`PR_SET_PDEATHSIG`).
- Desde JS: `admin.run('locale-gen', 'pt_BR.UTF-8', onLog)` devuelve una promesa. El progreso
  (las líneas de apt, por ejemplo) llega por `onLog` y por el store `admin.busy`.

Diferencia con polkit: no pide contraseña. Ser administrador basta, y las acciones son pocas
y acotadas.

## Teclado

`server/src/keyboard.c` usa **libxkbcommon** con los datos de **xkb-data**, como GNOME y KDE.
Hay cualquier distribución y variante (`latam`, `br`, `pt`, `us(intl)`, `fr(oss)`…), hasta 4
a la vez; el botón de la barra de menú pasa a la siguiente. Las teclas muertas y las
secuencias compose usan la tabla de compose de la región (paquete `libx11-data`, que son solo
datos). Si no se puede cargar XKB, la UI vuelve a sus tablas propias (latam y us).

## Escrituras complejas (M5b)

Sin HarfBuzz, fribidi, ICU, ibus ni fcitx: todo en C propio y verificado contra las
referencias (ver "Cómo se verificó").

### Derecha a izquierda (bidi)

`server/src/ui/bidi.c` implementa el **algoritmo bidireccional de Unicode completo** (UAX #9,
Unicode 15.1): incrustaciones y anulaciones explícitas, aislamientos (LRI/RLI/FSI/PDI),
secuencias de ejecución aisladas, reglas W, pares de paréntesis (N0), N1–N2, I1–I2 y, por línea,
L1–L2. Pasa los **91.707 casos de `BidiCharacterTest.txt`** de Unicode sin fallas.

La dirección de cada párrafo es la de su primer carácter fuerte (como "natural" en macOS): un
nombre de archivo en árabe se ve bien aunque la interfaz esté en español, y "Informe (versión 2)
تقرير.pdf" mezcla las dos direcciones como corresponde. Los paréntesis se espejan en los tramos
RTL. En contexto árabe, "4.5%" se muestra "%4.5": es lo que dice el algoritmo (los dígitos que
siguen a una letra árabe son números árabes) y lo mismo que hace Chrome.

### Formas del árabe, marcas y ligaduras

`server/src/ui/shape.c` + `otf.c` arman cada tramo como HarfBuzz: formas contextuales del árabe (isol/init/medi/fina según el tipo de unión de cada
letra, con las etapas de HarfBuzz), ligaduras obligatorias (لا), `ccmp`, `calt`, `liga`, banderas
de lookup con GDEF (ignorar marcas), sustitución encadenada inversa, kerning por GPOS, cursivo
(nastaliq del urdu) y marcas ancladas a la base, a ligaduras y a otras marcas (harakat, niqqud,
acentos combinantes).

### Escrituras índicas y tailandés

`server/src/ui/shape_indic.c` reimplementa el shaper índico de HarfBuzz para devanagari (hindi,
marathi, nepalí), bengalí, gurmukhi, gujarati, oriya, tamil, telugu, kannada y malayalam:
normalización (matras partidas, nukta), categorías de Unicode, posición de cada consonante
según la fuente, sílabas (con ◌ si la sílaba está rota), reordenamiento inicial (base, reph,
matras que van antes de la consonante), las features de cada etapa (nukt, akhn, rphf, rkrf, pref,
blwf, abvf, half, pstf, vatu, cjct) sin cruzar de sílaba, reordenamiento final (matra después de
las medias formas, reph según la escritura, pref, init) y el resto (pres, abvs, blws, psts, haln).
En tailandés y laosiano, SARA AM (ำ) se separa y NIKHAHIT pasa antes de los tonos, como HarfBuzz.
Las sílabas se cortan enteras al partir líneas.
Antes de armar, las marcas se ordenan por clase combinante (con los ajustes de HarfBuzz para
hebreo y árabe) y los invisibles (ZWJ, marcas de dirección) se ocultan.

### Jemer, birmano y el Universal Shaping Engine

| | |
|---|---|
| ![archivos](m5b-use-archivos.png) | ![terminal](m5b-use-terminal.png) |
| En la interfaz (nombres de archivo en Archivos): cingalés, tibetano, birmano, jemer, mongol, tai tham, sundanés, balinés y javanés | En la Terminal, con el tailandés al final (`docs/packs/escrituras-m5b.txt`) |

Para el resto de las escrituras del sur y sudeste de Asia, LordOS sigue los otros tres shapers
de HarfBuzz:

| Shaper | Escrituras | Qué hace |
|---|---|---|
| jemer (`shape_khmer.c`) | jemer | vocales partidas (ើ = េ + ើ), coeng + ro antes de la base (pref, y cfar para lo que sigue), la vocal izquierda al comienzo, ◌ en sílabas rotas; locl…cfar juntas por sílaba y clig en vez de liga |
| birmano (`shape_myanmar.c`) | birmano, mon, shan, karen… (fuentes con la etiqueta `mym2`; las viejas `mymr` van por el armado común, como en HarfBuzz) | kinzi (င်္) después de la base, la medial ra (ြ) y ေ antes, el orden de las de abajo; rphf, pref, blwf y pstf de a una; marcas sin ancho antes de GPOS |
| Universal Shaping Engine (`shape_use.c`) | tibetano, cingalés, balinés, javanés, sundanés, buginés, batak, cham, lepcha, limbu, tai tham, tai le, chakma, brahmi y las ~80 escrituras que HarfBuzz manda al USE, entre ellas mongol, n'ko y adlam (con las formas de unión del árabe) | categorías USE de Microsoft, sílabas con su gramática (sin ver CGJ ni el ZWNJ antes de una marca), rphf (un reph sustituido pasa a R), pref, las básicas por sílaba, ◌ en sílabas rotas, el reph antes del primer glifo post-base y las vocales pre-base al comienzo, isol/init/medi/fina por sílaba |

Las gramáticas de sílabas son las de Ragel de HarfBuzz, escritas como texto en cada shaper y
compiladas al primer uso por `shape_syl.c` (un autómata de Thompson recorrido con conjuntos de
estados; gana la alternativa más larga y, a igual largo, la primera, como el escáner de Ragel).
La normalización también es la de HarfBuzz: descompone solo lo que la fuente puede mostrar
separado, ordena las marcas por clase combinante (con los ajustes de HarfBuzz para télugu,
tailandés y tibetano) y vuelve a componer una marca con su base si la fuente tiene el compuesto.
GPOS saltea los invisibles (ZWJ, ZWNJ) al buscar pares de kerning, como HarfBuzz.

Las tablas de Unicode (clase bidi, espejos, paréntesis, tipo de unión, escritura, clase
combinante, invisibles, corte de línea, marcas, categorías índicas, de jemer y birmano y del
USE) se generan con `node tools/gen-unicode.mjs <UCD>` en `server/src/ui/unidata.h` y se
compilan solo en `unicode.c`. Las categorías del USE siguen `gen-use-table.py` de HarfBuzz:
además de la UCD necesitan `ArabicShaping.txt` y los dos archivos de Microsoft que trae
HarfBuzz (`src/ms-use/IndicSyllabicCategory-Additional.txt` e
`IndicPositionalCategory-Additional.txt`).

### Fuentes de respaldo y corte de línea

El texto latino, griego y cirílico que Inter cubre sigue por el camino rápido de siempre
(`text.c`). El resto pasa por `textlayout.c`, que elige fuente por carácter: Inter, la Noto de
su escritura (`Noto Sans Arabic`, `Noto Sans Hebrew`, `Noto Sans <escritura>` para cualquier
otra instalada), `Noto Sans`/`DejaVu Sans`, símbolos y la fuente de emoji a color. El mismo
ideograma se dibuja distinto en chino, japonés y coreano: la fuente CJK (`Noto Sans CJK
JP/KR/SC/TC`) sale del idioma de la interfaz. La terminal usa la misma cadena, con la variante
monoespaciada (`Noto Sans Mono CJK`).

Para chino y japonés, que no separan palabras, se puede cortar entre caracteres respetando las
reglas de UAX #14 que importan (no cortar antes de 、。」！ ni después de 「). Las marcas, los
unidores y los selectores de variante nunca quedan solos al principio de una línea.

El **tailandés se corta por palabras**, como en GNOME: `server/src/ui/thbrk.c` lee el
diccionario de libthai (`/usr/share/libthai/thbrk.tri`, paquete `libthai-data`, un trie doble de
libdatrie) con un lector propio y busca la segmentación con el mismo "maximal matching" de
libthai (el camino de palabras que llega más lejos con menos caracteres fuera del diccionario y
menos cortes, y la recuperación después de una palabra desconocida), con sus pistas de comienzo
de sílaba, las siglas con punto (พ.ศ., กอ.รมน.) y las reglas de ๆ, dígitos y ฿. No enlaza libthai
ni libdatrie. Sin el diccionario, se corta donde puede empezar una sílaba. El jemer y el birmano
(sin diccionario en Debian) se cortan entre sílabas, sin separar la consonante final (င် con
asat, ញ់ con bantoc) de la sílaba que cierra; el laosiano, solo en los espacios. Para probar
cortes sin pantalla: `LORD_WRAP=230 build/text-test salida.ppm "texto"…` (`tests/text_test.c`). Si una
palabra no entra en la línea, se corta igual, pero nunca dentro de un grupo de glifos (una
sílaba armada, una letra con sus marcas).

Cada texto se arma una vez y se guarda en una caché chica (48 entradas): el layout lo mide y
cada repintado lo dibuja sin volver a armarlo.

### Interfaz espejada

Si el idioma es de derecha a izquierda (`meta.json` → `"direction": "rtl"`; `i18n-pack` lo pone
solo para ar, he, fa, ur…), **toda la interfaz se da vuelta** como en macOS: barra de menús,
menús (se abren hacia la izquierda), ventanas (botones a la derecha), Dock, pestañas, Ajustes,
Archivos y el tema Clásico (inicio y barra de tareas a la derecha). El contenido de la terminal
sigue de izquierda a derecha (ver abajo, "En la terminal").

El motor sigue a CSS: `direction: 'rtl' | 'ltr'` se hereda, da vuelta el eje de las filas y el
inicio de las columnas, y `textAlign` tiene `'start'`/`'end'` (por defecto `'start'`). `left`,
`right`, márgenes y paddings siguen siendo físicos, así las posiciones absolutas (ventanas,
menús contextuales) no cambian. Lo que calcula posiciones con la x del mouse (la lupa del Dock,
el divisor de paneles de la Terminal, el Slider) se maqueta con `direction: 'ltr'`.

APIs: `direction()` (dirección de la interfaz), `textDirection(texto)` (la de un texto suelto;
`TextInput` la usa para poner el cursor del lado correcto), `useLocale().dir`.

### Métodos de entrada

`server/src/ime.c` tiene tres métodos sobre el teclado latino (us). Se agregan como fuentes de
entrada en **Ajustes → Teclado → Métodos de entrada**; el botón de la barra (한, あ, 拼) y
**❖⌥Espacio** (o Ctrl+⌥Espacio) pasan de una fuente a otra, como ⌃⌥Espacio en macOS. ❖Espacio y Ctrl+Espacio abren Spotlight (`SPOTLIGHT.md`).

| Método | Cómo se escribe | Diccionario (Debian) |
|---|---|---|
| Coreano (hangul) | teclado de 2 juegos (dubeolsik): `gksrmf` → 한글, vocales y finales compuestas, la final pasa a la sílaba siguiente; Retroceso deshace de a una jamo | ninguno |
| Japonés (kana) | romaji → hiragana (Hepburn y kunrei, っ por doble consonante, nn/n' → ん, ー 、。「」); Espacio convierte **la frase**: se parte en palabras (diccionario, okurigana como 書く/行きます, partículas y terminaciones como です/ています que quedan en hiragana) y el segmento con foco se resalta; ←/→ cambian de segmento, Shift+←/→ lo achican o agrandan, Espacio/↓ recorren sus candidatos (siempre con hiragana y katakana), 1–9 eligen, Enter confirma todo, Esc vuelve a kana | `skkdic` (SKK-JISYO.L, EUC-JP, búsqueda binaria sobre el archivo mapeado, entradas con y sin okurigana) |
| Chino simplificado (pinyin) | letras → candidatos por frecuencia: la frase entera, **abreviaturas por iniciales** (`zg` → 这个/中国, `bjdx` → 北京大学, también `zhg`), frases que empiezan con lo escrito (`zhongg` → 中国, 中国人) y las que cubren el comienzo (el resto sigue en composición); 1–9 o Espacio eligen; − = pasan página; Enter deja las letras | `rime-data-pinyin-simp` (65.000 caracteres y frases) |

El paquete de idioma de Debian de japonés o chino instala también su diccionario (y las fuentes
CJK). Un diccionario propio se puede poner en `~/.local/share/lordos/ime/`.

Solo hay composición cuando un campo de texto o una terminal tiene el foco: si no, las teclas
pasan tal cual y los atajos de las apps siguen andando. Con Ctrl/Alt/❖ se confirma lo que haya
y la tecla pasa. El texto confirmado llega como una tecla que escribe ese texto, así cualquier
app lo recibe sin cambios. `TextInput` muestra la composición subrayada en su lugar; la terminal,
sobre el cursor. La ventana de candidatos (`<ImeOverlay />`) la monta `startShell`: todos los
temas la tienen.

Para un campo de texto propio: `ime.attach(token, () => ({ x, y, h }), { inline })` al tomar el
foco y `ime.detach(token)` al perderlo; `useIme()` da `{ preedit, candidates, selected }`.

La partición japonesa usa el diccionario y reglas, no un modelo estadístico como Mozc: en el
banco de frases de `server/tests/ime-ja.txt` acierta 16 de 18 (los dos errores necesitan saber
qué palabra es más frecuente, 昨日 contra 機能, y SKK no trae frecuencias); se corrige con
Espacio o Shift+flechas. El pinyin no pasa la puntuación a ancho completo y no hay modo latino
temporal con Shift (se cambia de fuente con ❖⌥Espacio).

### En la terminal

Una fila con árabe o hebreo se muestra en **orden visual** (el mismo bidi, con párrafo de
izquierda a derecha, como Konsole y mlterm) y las palabras árabes, hebreas, índicas, tailandesas,
tibetanas, birmanas, jemeres, cingalesas y de las demás escrituras del USE se arman juntas (letras unidas, لا, niqqud, conjuntos, matras) y se dibujan con su ancho natural
dentro de las celdas que ocupan. El contenido no cambia: copiar, buscar y el programa ven el
orden lógico; el cursor, la selección y el mouse siguen el orden visual. En pantalla alternativa
(vim, less, mc) no se aplica, porque esos programas acomodan su propia pantalla. Se apaga por
perfil en Preferencias → Texto → "Árabe y hebreo". Para estas escrituras la terminal usa primero
la fuente Noto de la escritura, para que una palabra no mezcle fuentes.

### Cómo se verificó

| Prueba | Resultado |
|---|---|
| `build/bidi-test BidiCharacterTest.txt` (Unicode 15.1) | 91.707 / 91.707 |
| `tests/hb_compare.py` contra HarfBuzz (uharfbuzz 0.56, HarfBuzz 14.6): árabe Sans, Naskh y Nastaliq con harakat, hebreo con niqqud, latín, cirílico, griego, CJK, Inter, las 9 escrituras índicas (palabras y frases con conjuntos, reph, pref, chillus, ZWJ/ZWNJ), tailandés y laosiano; jemer (Sans y Serif), birmano (Sans y Serif), tibetano, cingalés (con ZWJ), balinés, javanés, mongol (con selectores de variante), buginés, tai tham, chakma, brahmi, sundanés, cham, batak, lepcha, limbu, n'ko, adlam, tai viet, tai lue y tai le, con sílabas rotas, CGJ y ZWJ/ZWNJ | 409 / 409 frases idénticas (glifos y posiciones) |
| `tests/thbrk_compare.sh` contra libthai 0.1.29: los cortes entre caracteres tailandeses de `tests/th-break.txt` y de 705 párrafos de la Wikipedia en tailandés (191.000 caracteres) | 30 / 30 y 705 / 705 |
| Ligaduras de la terminal (JetBrains Mono, Fira Code) contra HarfBuzz | sin diferencias (igual que en M4) |
| `build/ime-test` (hangul, kana, pinyin) | 한글 안녕하세요, 日本/漢字/東京/新聞/きって/こんにちは, 你好/中国/我爱, zg/bjdx/zhongg |
| Frases japonesas (`tests/ime-ja.txt`) | 16 / 18 (私は日本語を勉強しています, 東京に行きます, 学校へ行きました…) |
| VM: interfaz en árabe con ambos temas, nombres de archivo en 7 escrituras, los tres métodos en la Terminal y en un campo de texto | ver capturas |

Memoria: con las fuentes CJK y los dos diccionarios cargados, lord-server pasa de ~43 a 55 MB
de RSS (36 MB anónimos; 19 MB son las fuentes y diccionarios mapeados, que el kernel comparte
y puede descartar).

### Capturas M5b

| | |
|---|---|
| ![escrituras](m5b-archivos-escrituras.png) | ![arabe](m5b-interfaz-arabe.png) |
| Nombres en árabe, hebreo, chino, japonés, coreano y mezclados (bidi), con emoji | Interfaz en árabe: barra, ventana, barra lateral, columnas y Dock espejados |
| ![menu](m5b-menu-arabe.png) | ![clasico](m5b-clasico-arabe.png) |
| Menú en árabe: se abre hacia la izquierda, atajo del otro lado | Tema Clásico en árabe; la terminal sigue de izquierda a derecha |
| ![japones](m5b-ime-japones.png) | ![pinyin](m5b-ime-pinyin.png) |
| Japonés en la Terminal: にほんご → 日本語 / にほんご / ニホンゴ | Pinyin: zhongguo → 中国, 中古, 中… (página 1 de 6) |
| ![hangul](m5b-ime-hangul.png) | ![resultado](m5b-ime-terminal.png) |
| Coreano: 한 confirmado, 글 en composición | `echo 日本語 中国 한글` con los tres métodos |
| ![campo](m5b-ime-campo-texto.png) | ![ajustes](m5b-ajustes-metodos.png) |
| En un campo de texto: 東京 en composición, candidatos sobre el cursor | Ajustes → Teclado con las fuentes de entrada |
| ![indicas](m5b-archivos-indicas.png) | ![terminal](m5b-terminal-escrituras.png) |
| Hindi, bengalí, tamil, malayalam, tailandés y urdu en Archivos | La terminal con índicas, tailandés, urdu, árabe y hebreo armados y en orden visual |
| ![frase](m5b-ime-japones-frase.png) | |
| Conversión por frase: 私は日本語を勉強しています, con el segmento 私は en foco | |

El paquete de árabe de las capturas es de prueba (`docs/packs/lordos-lang-ar.json`): traduce el
escritorio, Ajustes, Archivos, Acerca de y la Calculadora; la Terminal queda en inglés.

## Pendiente

- Las restricciones de vocales de HarfBuzz (◌ ante secuencias de vocales inválidas en
  devanagari, cingalés y otras): una secuencia mal escrita se arma sin el círculo.
- Corte de línea del laosiano por palabras (no hay diccionario empaquetado en Debian; ICU trae
  uno): hoy solo en los espacios. El jemer y el birmano se cortan por sílaba, no por palabra.
- Japonés con frecuencias: no hay en Debian un diccionario liviano con frecuencias (los datos de
  Mozc, `mozc-data`, vienen en su formato binario propio); zhuyin, cangjie y hanja.
- Traducir los mensajes de registro del servidor (los que van a stderr, para desarrolladores).
- `lord.locale.available()` marca como "generada" solo lo que el proceso del servidor ya
  veía al arrancar; Ajustes lo corrige en pantalla después de generar.

## Paquetes en la VM (M5b)

| Paquete | Para qué |
|---|---|
| `libthai-data` | diccionario del corte de palabras del tailandés (ya venía instalado como dependencia; lo instala también el paquete de idioma `th`) |
| `libthai-dev` | solo para la prueba `tests/thbrk_compare.sh` (compara con libthai); LordOS no la usa |
| `fonts-noto-core` | ya instalado: trae Noto Sans Khmer, Myanmar, Sinhala, Balinese, Javanese, Mongolian…, Noto Serif Tibetan (los paquetes de idioma km, my, bo, si, mn, jv… lo piden) |

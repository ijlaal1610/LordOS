# Apps de LordOS

Cada app es una carpeta `apps/<id>/` con dos archivos. Las apps no dependen del tema:
cualquier tema las muestra con sus propios colores (vía `useTheme()`).

```
apps/notas/
├── manifest.json
└── index.jsx
```

**manifest.json**

```json
{
  "id": "notas",
  "name": "Notas",
  "description": "Notas rápidas",
  "size": [500, 400],
  "dock": true,
  "permissions": []
}
```

Campos opcionales: `"transparent": true` (el tema no pinta fondo bajo el contenido; la app
dibuja el suyo, ej. la Terminal con transparencia) y `"minSize": [ancho, alto]`.

**index.jsx**

```jsx
import { View, Text, useTheme, TextInput } from '@lordos/sdk';
import { useState } from 'react';

export function Icon({ size }) {          // ícono para dock, Launchpad, barra de tareas
  return <View style={{ width: size, height: size, borderRadius: size * 0.22, backgroundColor: '#ffd60a' }} />;
}

export default function Notas() {         // contenido de la ventana
  const t = useTheme();
  const [texto, setTexto] = useState('');
  return (
    <View style={{ flex: 1, padding: 16, gap: 10 }}>
      <Text style={{ fontSize: 18, fontWeight: 700, color: t.text }}>Notas</Text>
      <TextInput value={texto} onChange={setTexto} placeholder="Escribí…" />
    </View>
  );
}
```

`npm run build` vuelve a empaquetar todos los temas con todas las apps. Para distribuir una app
sin tocar los temas, se empaqueta como `.lpk` con `node tools/lpk.mjs <carpeta>` y se instala
desde la Tienda (ver `docs/APPS-NATIVAS.md`).

## API disponible (`@lordos/sdk`)

| Área | API |
|---|---|
| UI | `View`, `Text` (con `fontFamily: 'monospace'` para JetBrains Mono), `Image`, `ScrollView`, `TextInput` (con `onKeyDown`), `Button`, `Toggle`, `Segmented`, `Stepper`, `Slider`, `SidebarItem`, `SectionTitle`, `Terminal` |
| Tema | `useTheme()` → `text`, `textDim`, `accent`, `windowBg`, `sidebarBg`, `separator`, `hover`, `control`, `inputBg`, `mode` |
| Ventanas | `wm.open(app, { new, instance, frame, args })`, `wm.close(id)`, `wm.focus(id)`, `wm.setTitle(id, t)`, `wm.setMenus(id, menús)`, `wm.setCloseGuard(id, fn)`, `useAppWindow()` → `{ id, focused, args }` |
| Terminal | `term.create/close/configure/key/paste/search/...` (ver `docs/TERMINAL.md`), `clipboard.get/set`, `clipboardHistory` |
| Teclado | `useKeyboard(fn)` (solo con la ventana al frente), `TextInput`; `keyboard.list/configured/active/next/set` (distribuciones de XKB) |
| Idioma | `t('espacio.clave', vars)`, `useLocale()`, `appName(app)`, `formatDate`, `formatTime`, `formatNumber`, `languages()`, `setLanguage(id)`, `direction()`, `textDirection(texto)` — ver `docs/IDIOMAS.md` |
| Métodos de entrada | `TextInput` ya los usa; para un campo propio, `ime.attach(token, caret)` / `ime.detach(token)` y `useIme()` |
| Administración | `admin.run('locale-gen' \| 'system-locale' \| 'timezone' \| 'langpack', arg, onLog)` → promesa; `admin.busy` (progreso) |
| Archivos | `useDirectory(ruta)`, `fs.list`, `fs.read(ruta, { binary })` (máx. 8 MB), `fs.write(ruta, texto \| ArrayBuffer)` (atómico, máx. 32 MB), `fs.mkdir`, `fs.rename`, `fs.copy`/`fs.move` (en segundo plano: `fs.jobs()`), `fs.trash`/`trashList`/`restore`/`removeFromTrash`/`emptyTrash` (Papelera freedesktop), `fs.stat`, `fs.du`, `fs.free`, `fs.search`, `fs.recent`, `fs.volumes`, `fs.join`, `fs.parent`, `fs.basename` — solo en la carpeta personal y en los discos montados del usuario (`/media/$USER`, `/run/media/$USER`); no se puede escribir en `~/.local/share/applications`, `~/.config/autostart`, `~/.local/share/lordos/apps` ni en los ajustes, y borrar solo se puede adentro de la Papelera (ver `docs/ARCHIVOS.md`) |
| Fuentes | `lord.fonts.list({ rescan })` → `{ family, style, ps, weight, italic, mono, color }` de las fuentes instaladas |
| Touchpad | `onWheel` con `e.precise`, `e.deltaX/deltaY` en píxeles y `e.phase` (`'momentum'` = inercia); `onPinch({ scale, rotation, x, y, phase })`; `onSwipe({ dx, dy, fingers, phase, system })`; `useTrackpads()`, `trackpad.config()/set(cambios)`, `registerGestureActions({ up, down, left, right })` (temas) — ver `docs/TRACKPAD.md` |
| Sistema | `useSystemInfo()`, `useCpuUsage()`, `useServerStats()`, `formatBytes`, `formatDuration` |
| Ajustes | `useSettings()` → `[ajustes, actualizar]`, guardados en `~/.config/lordos/settings.json` |
| Sesión | `session.quit()`, `themes.list()`, `themes.use(id)` |
| Apps | `useLauncherApps()` (LordOS, paquetes y apps Linux), `launchApp(id)`, `packages.*`, `windowIcon(w)` — ver `docs/APPS-NATIVAS.md` |
| Portapapeles | `clipboard.get/set(texto, { secure })/use/pin/remove/clear`, `clipboardHistory` y `clipboardKind` (historial guardado; panel ❖V y ❖⇧V) — ver `docs/SPOTLIGHT.md` |
| Spotlight | `spotlight.open/close/toggle` (❖Espacio); `spotlightCalc(texto)` y `spotlightConvert(texto)` |
| Sonido | `useSound()`, `sound.setVolume/setMute/setDefault/setPort/move/meter/play`, `defaultSink(s)`, `microphones(s)` — ver `docs/SONIDO.md` |
| WebAssembly | el global `WebAssembly` estándar (`docs/WASM.md`); en apps instaladas, `loadWasm(id, ruta)` |
| Lienzo | `<Image pixels={{ width, height, data }} />`: píxeles RGBA (por ejemplo, calculados en WASM) |
| Medios | `media.play/image/thumb/probe/poster/edit/transcode/screenshot/annotate/saveImage/copyImage/screenRecord/audioRecord/camera/photo/cameraRecord`, `usePlayer(id)`, `<MediaView media={id} />`, `nowPlaying`, `NowPlayingTile`, `useThumb(ruta)`, `userDirs()` — decodifica `lord-media`, aislado (ver `docs/MEDIOS.md`) |
| Ventanas (medios) | `wm.setProtected(id, on)` (negra en capturas, grabaciones y escritorio remoto), `wm.setFullscreen(id, on)`, `wm.setPip(id, frame)`, `useWindowState(clave, inicial)`, `useAppWindow().mode` (`'window'`, `'fullscreen'` o `'pip'`) |
| Markdown | `<Markdown source base width onLink onToggleTask />`, `markdownHeadings(texto)`, `toggleTask` — ver `docs/NOTAS.md` |
| Escritorio | `registerWidgetType(tipo, Componente)`, `setWidgets(tipo, [{ id, x, y, w, h, pinned }])`: widgets entre el fondo y las ventanas |
| Notifications (M9) | `notify({ app, title, body, actions: [{ id, label, primary, run }], sound, sticky, timeout, onClose })` → id, `dismissNotification(id)`, `useNotifications()`, `notifyHistory`; `registerStatusItem(id, Component)` (status icons in the theme's bar) — see `docs/SYSTEM.md` |
| Clock and Help (M9) | `clockAlarms`, `clockTimer`/`timerStart…`, `stopwatch`/`swStart…` (keep running with the app closed); `lord.time.zoneOffset(zone, t)`; `help.list/read/search/open/startTour`; `registerSpotlightSource(id, fn)`; `weekdayName(d)` |
| Documentos (M9) | `pdf.open(ruta \| { blob })` (página, búsqueda, índice, enlaces, formularios, anotaciones, guardar copia), `<DocImage image>`, `archive.list/extract/create`, `fontInfo/fontSample/fontGrid`, `pdfThumb`, `makeTextPdf`/`makeImagePdf`: todo en el ayudante encerrado `lord-doc` — ver `docs/DOCUMENTOS.md` |
| Imprimir (M9) | `print.open({ title, path \| doc \| text \| image })` abre el diálogo del sistema; `print.printers/jobs/cancel/add/remove/setDefault/discover/scanCaps/scan` — ver `docs/IMPRESION.md` |
| Emojis (M9) | `emoji.open()` (❖.), `emoji.search/name/withTone/insert`, `EmojiGrid`, `EmojiToneBar` |
| Foco de texto propio | `takeTextFocus(token, onKey, onChange)` / `releaseTextFocus(token)`: un campo propio recibe las teclas como un TextInput |

## Textos e idiomas

Los textos visibles no se escriben en el código: van con `t('miapp.clave')` y sus traducciones
en `assets/locales/{es,en,pt}/miapp.json` (`{ "clave": "Texto" }`). El nombre de la app se
traduce en `apps.json` (`"miapp": "Mi app"`). Las listas con textos se arman al dibujar,
no como constantes de módulo, porque al cambiar el idioma la interfaz se vuelve a montar.
`node tools/i18n-check.mjs` avisa si falta alguna clave.

## Permisos

`permissions` en el manifest es **declarativo** por ahora: documenta qué usa la app, pero
todas las apps corren en el mismo contexto JS que el tema. El aislamiento real (un contexto
o proceso por app, con permisos verificados por el servidor) es trabajo pendiente. Lo que sí
es real hoy: el servidor corre sin root, `fs` no sale de la carpeta personal y no existe
ninguna API para ejecutar comandos (la terminal solo puede abrir el shell del usuario).

`fs` solo escribe dentro de la carpeta personal y nunca en las carpetas que el sistema usa para
lanzar o instalar cosas (`.desktop`, autostart, apps instaladas), así que una app no puede
convertir una escritura en la ejecución de un comando.

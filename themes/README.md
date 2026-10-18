# Temas de LordOS

Un tema es una app React que dibuja **todo el escritorio**: fondo, barras, dock o barra de
tareas, menús y marcos de ventana. Las apps (`apps/`) van adentro de esas ventanas y se
adaptan solas a los colores del tema. `lord-server` ejecuta el tema con QuickJS y lo pinta
con su motor en C.

| Tema | Estilo |
|---|---|
| `default` | Lord Default: barra de menú, dock con magnificación, Launchpad, Centro de control |
| `classic` | Clásico (**plantilla**): barra de tareas, menú de inicio, todo en un solo archivo |

```sh
npm install           # una vez (React + esbuild)
npm run build         # empaqueta todos los temas, con el SDK y todas las apps
```

El usuario cambia de tema en vivo desde **Ajustes → Temas** (sin reiniciar). La elección se
guarda y se respeta al volver a iniciar.

## Crear un tema nuevo

1. Copiar `themes/classic` a `themes/mitema`.
2. Editar `theme.json` (nombre y descripción que muestra Ajustes).
3. Modificar `index.jsx`. Un tema debe:
   - llamar a `configureWindows({ apps, workArea })` con el área libre para ventanas,
   - envolver todo en `<ThemeProvider tokens={...}>`,
   - dibujar cada ventana visible y adentro `<AppHost id={w.id} focused={...} />`,
   - terminar con `startShell(<App />)`.

   Recomendado para que las apps se vean completas:
   - título con `windowTitle(w)` (las apps pueden cambiarlo, ej. la Terminal),
   - sin fondo bajo el contenido si `getApp(w.id).transparent`,
   - menús de la app con `useAppMenus(ventanaEnfocada)` (ej.: Shell, Editar, Ver de la Terminal),
   - el nodo de la ventana con `<WindowShell win={w} style={…marco…}>` del SDK: pone los bordes invisibles
     para cambiar el tamaño (4 lados y 4 esquinas, con su cursor) y la barra de título llama a
     `wm.beginMove(w.id)` en `onMouseDown`. Mover y agrandar lo hace lord-server en C, sin un evento por
     cuadro (ver `docs/VENTANAS.md`),
   - el punto de "app abierta" del dock comparando con `w.app` (una app puede tener varias ventanas).
   - textos con `t('shell.…')` (ver `docs/IDIOMAS.md`), nombres de apps con `appName(app)` y
     fecha y hora con `formatDate`/`formatTime` del SDK, para respetar idioma y región,
   - la distribución de teclado activa con `keyboard.label(keyboard.active())` y `keyboard.next()`
     (incluye los métodos de entrada: 한, あ, 拼),
   - que funcione de derecha a izquierda: con un idioma RTL el motor da vuelta filas y alineación
     solo; lo que posiciona con la x del mouse (una lupa tipo Dock) va con `direction: 'ltr'`, y
     los menús desplegables se abren hacia la izquierda si `direction() === 'rtl'`
     (ver el tema default y `docs/IDIOMAS.md`). La ventana de candidatos del método de entrada
     la pone `startShell`: el tema no tiene que hacer nada.
   - las ventanas de apps Linux (Wayland) llegan como ventanas de la app interna `linux`: si
     `win.args.csd` es verdadero la app dibuja su propio marco y el tema muestra solo
     `<AppHost>`; y `configureWindows({ chrome: { w, h } })` dice cuánto suma el marco del tema
     (bordes + barra de título) para abrirlas con el tamaño que pidieron (ver `docs/WAYLAND.md`).
4. `npm run build` y elegirlo en Ajustes → Temas.

Las imágenes (`<Image src="assets/...">`) se resuelven relativas a la carpeta del tema.

## Tokens que el tema debe proveer (contrato con las apps)

`mode`, `accent`, `text`, `textDim`, `windowBg`, `sidebarBg`, `separator`, `hover`, `control`,
`inputBg`. Opcionales: `gtkButtons` (orden de los botones de las apps GTK, ej. `':minimize,maximize,close'`; por defecto los semáforos a la izquierda, ver `docs/WAYLAND.md`), `accents` (mapa nombre → color, para Ajustes), `wallpapers`
(`[{ id, name, src }]`). El tema puede agregar los tokens propios que quiera.

## Componentes nativos

| Componente | Qué es |
|---|---|
| `<View>`  | caja con layout flexbox, fondo, borde, sombra, blur |
| `<Text>`  | texto (fuente Inter 400/500/600/700), con ajuste de línea |
| `<Terminal session={id}>` | grilla de una sesión de terminal (dibujada en C) |
| `<Image src>` | PNG o JPEG, con `objectFit` |

Eventos: `onMouseDown`, `onMouseUp`, `onClick`, `onDoubleClick`, `onContextMenu` (clic derecho),
`onMouseMove`, `onMouseEnter`, `onMouseLeave`, `onWheel` (`e.deltaY`).
Touchpad y pantalla táctil (`docs/TRACKPAD.md`):
- `onWheel` también llega con `e.precise: true`, `e.deltaX`/`e.deltaY` en píxeles exactos y
  `e.phase` (`'update'`, `'end'`, `'momentum'` mientras sigue por inercia, `'momentum-end'`);
  `e.notches` cuenta pasos enteros de 48 px (para lo que avanza de a renglón).
- `onPinch({ scale, rotation, x, y, dx, dy, phase })`: `scale` relativa al comienzo, `rotation`
  acumulada en grados, `phase` `'begin'`/`'update'`/`'end'`.
- `onSwipe({ dx, dy, fingers, phase, system })`.

Durante un gesto el destino queda fijo: el nodo que estaba bajo el puntero al empezar.
Globales: `addListener('pointermove' | 'pointerup' | 'pointerdown' | 'keydown' | 'keyup' | 'swipe' | 'pinch', fn)`.

Deslizar con tres dedos (o cuatro, según Ajustes → Trackpad) es del escritorio. El tema elige qué
hace cada dirección con `registerGestureActions({ up, down, left, right })` del SDK, que devuelve
la función para sacarlas (usarlo en un `useEffect`). Ejemplos:
- `default`: arriba abre el Launchpad y abajo lo cierra;
- `classic`: arriba abre el menú de inicio y abajo lo cierra.

Sin acción del tema, izquierda y derecha cambian de ventana y arriba/abajo no hacen nada.
Teclado: `e.key`, `e.char` (según distribución), `e.code` (evdev), `e.shift`, `e.ctrl`, `e.alt`.

## Widgets del escritorio

Un tema dibuja `<DesktopWidgets layer="below" />` entre el fondo y las ventanas y
`<DesktopWidgets layer="above" />` encima de ellas (las notas de la app Notas, por ejemplo; ver
`docs/NOTAS.md`). Los dos temas incluidos ya lo hacen.

## Estilos soportados (subconjunto de CSS)

- **Layout:** `display` (`flex`/`none`), `position` (`relative`/`absolute`), `flexDirection`
  (por defecto `column`, como React Native), `flexWrap` (`nowrap`/`wrap`: los hijos que no
  entran pasan a otra línea; `gap` separa también las líneas, que van al comienzo: sin
  `wrap-reverse` ni `alignContent`), `justifyContent`, `alignItems`, `alignSelf`, `flex`,
  `flexGrow`, `flexShrink`, `flexBasis`, `gap`, `width`/`height` (px, `%`, `auto`),
  `min*`/`max*`, `left`/`top`/`right`/`bottom`, `inset`, `margin*`, `padding*`, `overflow`, `zIndex`.
- **Visual:** `backgroundColor`, `background: linear-gradient(...)`, `borderRadius`, `border`,
  `borderWidth`, `borderColor`, `opacity`, `boxShadow`, `backdropFilter: blur(px)`,
  `transform: translate(...)` / `translateX` / `translateY`, `pointerEvents`, `cursor` (`default`, `pointer`,
  `text`, `move`, `grab`, `grabbing`, `not-allowed`, `wait`, `progress`, `crosshair`, `n-resize`… `ew-resize`,
  `ns-resize`, `nesw-resize`, `nwse-resize`; vale el del primer nodo hacia arriba que lo tenga).
- **Texto:** `color`, `fontSize`, `fontWeight`, `lineHeight`, `textAlign`, `whiteSpace: nowrap`.
- **Imagen:** `objectFit` (`cover`, `contain`, `fill`).

Colores: `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()`, `rgba()` y algunos nombres.
Todavía no hay: `flexWrap`, bordes por lado, radios por esquina, `transform: scale/rotate`,
animaciones CSS (se animan desde JS con estado y `requestAnimationFrame`).

El SDK completo está en `apps/README.md`.

El Dock o la barra de tareas siguen `dockPrefs(settings)` del SDK (posición, tamaño, lupa, esconder solo): ver `docs/DOCK.md`.

# Ventanas: mover, cambiar el tamaño y cursores

Mover una ventana tiene que verse fluido incluso en una máquina chica y por CPU, que es el camino por
defecto en las VMs y en el ISO live. Además, cada ventana se agranda desde los 4 lados y las 4
esquinas, y el cursor muestra la flecha de cada dirección.

| | |
|---|---|
| ![bordes](fix-ventanas-bordes.png) | ![redimensionar](fix-ventanas-redimensionar.png) |
| Los 8 bordes invisibles de una ventana (Chromium con el marco del tema): ↖ ↕ ↗ ↔ ↔ ↙ ↕ ↘ | Agrandando desde la esquina: Chromium se vuelve a dibujar al tamaño nuevo mientras se arrastra |
| ![mover](fix-ventanas-mover.png) | ![gtk](fix-ventanas-gtk-mover.png) |
| Moviendo una ventana: cursor de mover, el Dock sigue encima | GTK Demo (marco propio, CSD) movida desde su barra: `xdg_toplevel.move` lo sigue lord-server en C |
| ![cursores](fix-ventanas-cursores.png) | ![app](fix-ventanas-cursor-app.png) |
| Los cursores de LordOS (`server/src/cursor.c`) a 2x sobre fondo claro y oscuro; el punto rojo es el punto activo | El I-beam en la barra de direcciones de Chromium: lo pide por `cursor-shape-v1` y lo dibuja LordOS |

Las capturas son de la VM 118 por CPU (en el camino GPU el cursor va por el plano de cursor y
`screendump` no lo muestra).

## Por qué no era fluido (antes)

Cada evento del mouse llamaba a JS y JS movía la ventana con React:

1. `pointermove` → `wm.move` → el store de ventanas cambia → **se volvía a renderizar todo el
   shell**: el escritorio, todas las ventanas, el Dock y la barra de menú (unos 38 ms por evento en
   QuickJS);
2. después, el layout y el repintado de todo lo que hay debajo y encima de la ventana en la región
   vieja y la nueva: fondo, otras ventanas, el contenido de la ventana, la terminal, sombras y el
   desenfoque del Dock (unos 17 ms);
3. con un mouse de 125 Hz o más llegaba un evento cada 8 ms: los eventos se acumulaban en evdev
   (se perdían, a veces el «soltar» también) y la pantalla se actualizaba a 15–18 cuadros por segundo.

## Qué se hizo

### Un movimiento por cuadro (`main.c`)

El movimiento del puntero ya no se manda en cada `SYN` de evdev: se junta y se manda **una vez por
cuadro**, cuando termina el page flip (`motion_pending`). Los botones y la rueda se mandan enseguida
(con el movimiento que haya hasta ese momento). Así un mouse de 1000 Hz no llama a JS ni a las apps
Linux mil veces por segundo. El movimiento crudo para juegos (`relative-pointer`) y el plano de
cursor siguen siendo por evento.

### El arrastre lo hace C (`ui/grab.c`, `lord.wm`)

El tema empieza el arrastre y lord-server lo sigue solo:

```js
wm.beginMove(id)              // en el onMouseDown de la barra de título
wm.beginResize(id, edges)     // edges como xdg_toplevel.resize: 1 arriba, 2 abajo, 4 izquierda, 8 derecha
```

- Por cuadro, C corre el nodo de la ventana (`left`/`top`, y `width`/`height` al cambiar el tamaño)
  y vuelve a hacer el layout. **No se llama a JS ni se vuelve a renderizar React.**
- Mientras dura, las apps Linux no reciben el puntero (`wl_pointer_grab`: `leave`), como en Weston.
- Al soltar, JS recibe `__lordWm('end', { node, dx, dy, dw, dh })` y el SDK guarda la geometría en el
  gestor de ventanas. React pone en el nodo el mismo valor que ya tenía, y `lord.setStyle` ya no
  invalida nada si el estilo no cambió (`js.c`).
- Límites: el tamaño mínimo es el `minSize` del manifest (las apps Linux, 160×100). El marco no sube
  por encima del área de trabajo (la barra de menú) y siempre quedan 48 px a la vista.
- `xdg_toplevel.move` y `xdg_toplevel.resize` de las apps que dibujan su marco (GTK, Chromium con CSD)
  usan lo mismo (`sdk/wayland.js`).
- Si el tema no marcó el nodo de la ventana (`wm.frameRef`), o la ventana está maximizada,
  `beginMove` devuelve `false` y el tema puede seguir con su arrastre en JS de antes.

### La ventana que se mueve es una capa (`ui/paint.c`)

- **La capa.** Al empezar a mover, el árbol de la ventana (con su sombra) se pinta una vez en un
  búfer aparte. En cada cuadro solo se copia en la posición nueva: no se vuelve a pintar texto,
  terminal, imágenes ni sombra.
  - Se pinta en sus coordenadas de pantalla (un canvas con el origen corrido). Así la terminal y las
    apps Linux siguen anotando dónde están.
- **Si algo de adentro cambia,** como una línea de la terminal o el cursor que parpadea, se vuelve a
  pintar solo esa parte de la capa. Si cambia un estilo de adentro, se vuelve a pintar toda.
- **Lo de abajo** (todo lo que se pinta antes que la ventana: fondo, escritorio, las otras ventanas)
  también se guarda una vez. Por cuadro:
  1. se copia la región que cambió;
  2. encima va la capa;
  3. lo que está por encima (barra de menú, Dock) se pinta normal, porque su desenfoque depende de
     lo que tiene debajo.
- **Si algo de abajo cambia** (un reloj, un video), lo de abajo se vuelve a armar. Si cambia muy
  seguido, se deja de usar.
- **Al cambiar el tamaño** la ventana se repinta en cada cuadro (su contenido se acomoda), pero lo de
  abajo igual se guarda.
- **El daño** es la región vieja más la nueva de la ventana.
- **En el camino GPU:**
  - lo de abajo anota otra vez sus ventanas de apps en cada cuadro (`wl_paint_keep`/`wl_paint_kept`);
  - la capa recuerda dónde tiene los huecos de las apps: alfa 0 exacto, distinto de un fondo vacío
    gracias a un valor centinela. Sobre la mezcla se vuelven a abrir y la app se anota en su lugar
    nuevo (`wl_paint_holes(0)`).
- **No hay capa** si adentro hay desenfoque de fondo (`backdropFilter`), o un video en el camino GPU.
- **Un detalle de `canvas_blit`:** los píxeles opacos se copian directo (mismo resultado que la
  mezcla, sin cuentas).

### Bordes para cambiar el tamaño (`sdk/windowshell.jsx`)

`<WindowShell>` es el nodo de la ventana: el marco va adentro y alrededor hay 8 zonas invisibles. Son
6 px afuera del marco y 3 px adentro, como en macOS. Los lados llevan `ns-resize`/`ew-resize` y las
esquinas (16 px) `nwse-resize`/`nesw-resize`. Los dos temas lo usan, también para las apps Linux con
marco propio (CSD): su borde para agrandar está en la sombra, que LordOS recorta.

```jsx
<WindowShell win={w} onMouseDown={() => wm.focus(w.id)} style={{ borderRadius: 12, overflow: 'hidden', boxShadow }}>
  <View onMouseDown={() => wm.beginMove(w.id)}>…barra de título…</View>
  <AppHost id={w.id} focused={focused} />
</WindowShell>
```

### Cursores (`server/src/cursor.c`)

- **Juego de cursores propio, dibujado por LordOS.** Son formas vectoriales (polígonos, cápsulas,
  anillos con distancia con signo), rasterizadas con 4×4 muestras por píxel a 1x (32 px) y 2x (64 px,
  en pantallas desde 2560×1440). Relleno negro, contorno blanco y una sombra suave.
- **No se lee ningún tema del sistema.** Debian no instala Adwaita ni DMZ en el ISO, así que no hay
  una dependencia nueva.
- **Formas:** `default`, `pointer` (mano), `text`, `move`, `grab`, `grabbing`, `not-allowed`, `wait`,
  `progress`, `crosshair` y las 8 direcciones de cambiar el tamaño, más `ew`, `ns`, `nesw` y `nwse`.
- **Estilo `cursor` (CSS) en cualquier nodo:** vale el del primer nodo hacia arriba que lo tenga.
  Ejemplo: `style={{ cursor: 'pointer' }}`.
- **Qué cursor se ve:**
  1. durante un arrastre, el del arrastre: `move` o la flecha del borde;
  2. sobre una app Linux, el que pida ella: `wl_pointer.set_cursor` (imagen u oculto) o
     **`wp_cursor_shape_manager_v1`** (forma por nombre, la usan Chromium, GTK 4 y Qt 6), dibujada con
     estas mismas formas;
  3. si no, el del nodo.
- **Dónde se dibuja:** por CPU sobre el cuadro; en el camino GPU por el plano de cursor de KMS (o
  como textura si el driver no tiene plano). El escritorio remoto y las grabaciones reciben la imagen
  (`mirror_cursor`, `media_composed`).

### Presentar por CPU: solo lo que cambió

Cada búfer de KMS recibe solo lo que cambió desde la última vez que se dibujó en él. Es la región
repintada en ese cuadro y en el anterior (doble búfer), más donde estaba el cursor; antes era toda la
escena (4 MB a 1280×800). Copian todo:

- los dos cuadros después de cambiar de tamaño o de volver de la GPU;
- una app a pantalla completa.

Con la Terminal arrastrándose, «presentar» bajó de 0,66 a 0,35 ms por cuadro (y el arrastre llegó a
95–103 cuadros por segundo: virtio-gpu termina el flip antes del refresco).

## Mediciones (VM 118, 1280×800 a 75 Hz, 4 vCPU)

Arrastre continuo de 6 s en círculos con una tableta virtual (uinput, 125 eventos/s). lord-server
escribe una línea por arrastre en `~/.cache/lordos/session.log`:

```
arrastre: 464 cuadros en 6011 ms = 77.2 FPS (464 movimientos); por cuadro: trabajo 12.80 ms (máx 36.83;
JS 0.20, layout 0.56, pintado 11.43, presentar 0.61), intervalo máx 36.9 ms; capa: 1023 pintadas, 467 copiadas (CPU)
```

«trabajo» es lo que gasta lord-server por cuadro: el JS del puntero y de los timers, el layout, el
pintado y la presentación.

| Camino | Ventana | Antes | Después |
|---|---|---|---|
| CPU | Terminal | 17,9 FPS, 55,6 ms por cuadro (JS 38,2) | **77,2 FPS** (el tope es el refresco de 75 Hz), 12,8 ms (JS 0,2) |
| CPU | Chromium (SSD, 900×600) | 17,1 FPS, 58,2 ms (JS 35,9) | **50,7–67 FPS**, 15–20 ms (JS 0,3) |
| GPU (`LORD_GPU=1`, llvmpipe) | Terminal | 15,3 FPS, 64,5 ms (JS 38,3) | **40 FPS**, 24 ms (JS 0,4; presentar con llvmpipe 10 ms) |
| GPU (`LORD_GPU=1`, llvmpipe) | Chromium | 11,5 FPS, 85,9 ms (JS 47,0) | **30 FPS**, 32 ms (JS 0,4; presentar con llvmpipe 12,7 ms) |
| CPU | Chromium, cambiar el tamaño | — | 19,5 FPS: lo marca Chromium, que vuelve a dibujar cada tamaño |

- **«Antes»** es el mismo árbol, pero con el movimiento sin juntar y los temas originales (el
  arrastre en JS). El «antes» perdía eventos: de 720 movimientos llegaban unos 110.
- **Lo que queda en «pintado»** es sobre todo lo que está encima de la ventana: el desenfoque y los
  íconos del Dock (unos 4–7 ms cuando la ventana pasa por debajo) y la copia de la capa.
- **Con GPU real** (AMD), «presentar» es casi 0. En la VM es llvmpipe por software.
- **Picos de unos 35 ms:**
  - el primer cuadro (arma lo de abajo, que es pintar toda la pantalla una vez);
  - alguna reconstrucción si algo de abajo cambió.

**Cómo se midió** (scripts del scratchpad, uno por VM):
- `uinput-ventanas-118.py`: corre en la VM como root y crea la tableta «LordOS prueba tableta». Se
  la maneja por `/run/uinput-ventanas`.
- El ratón de QMP (`input-send-event`) no sirve para medir: la tableta USB de QEMU tiene una cola de
  16 eventos y a 120 Hz pierde movimientos y hasta el «soltar».

## Pendiente

- Capa por GPU de verdad (la ventana como textura propia que solo se corre), para no pintar nada por
  CPU al mover con una placa real.
- El desenfoque del Dock se recalcula cuando una ventana pasa por debajo.
- El primer cuadro de un arrastre arma lo de abajo (unos 35 ms).
- `xdg_toplevel.resize` pedido por una app GTK desde su borde: el borde está en la sombra que LordOS
  recorta, así que en la práctica se agranda con los bordes del tema (probado con Chromium; GTK Demo
  se probó moviéndola).
- Temas de cursor del sistema (Xcursor) y el tamaño del cursor en Ajustes.

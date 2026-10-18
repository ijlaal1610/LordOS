# Dock y barra de tareas

En **Ajustes → Escritorio y Dock** se elige, como en macOS:

- dónde va el Dock (abajo, a la izquierda o a la derecha);
- su tamaño, la lupa y cuánto amplía;
- si se esconde solo;
- si muestra las apps recientes.

En el tema Clásico, la misma sección elige la posición de la barra de tareas: abajo, arriba, a la
izquierda o a la derecha. Todo se aplica en vivo, sin reiniciar, y se guarda en
`~/.config/lordos/settings.json`.

| | |
|---|---|
| ![ajustes](dock-ajustes.png) | ![abajo](dock-abajo.png) |
| Ajustes → Escritorio y Dock | Abajo (como siempre), con la lupa |
| ![izquierda](dock-izquierda.png) | ![derecha](dock-derecha.png) |
| A la izquierda: lupa sobre el eje vertical, nombre al costado | A la derecha: los puntos de app abierta van del lado de afuera |
| ![menú](dock-derecha-menu.png) | ![revelado](dock-oculto-revelado.png) |
| Clic derecho con el Dock a la derecha: el menú se abre hacia adentro | Esconder solo: aparece al llevar el puntero al borde (sin el Dock: [`dock-oculto.png`](dock-oculto.png)) |
| ![clásico](dock-clasico-izquierda-barra.png) | ![clásico, inicio](dock-clasico-izquierda.png) |
| Tema Clásico con la barra a la izquierda (solo íconos y la hora abajo) | Su menú de inicio se abre al lado de la barra |

## Ajustes (`sdk/dock.js`: `DOCK_DEFAULTS`, `dockPrefs(settings)`)

| Clave | Valores | Por defecto |
|---|---|---|
| `dockPosition` | `bottom`, `left`, `right` | `bottom` |
| `dockSize` | 36–80 (px de cada ícono) | 54 |
| `dockMagnify` | `true`/`false` | `true` |
| `dockMagnifySize` | del tamaño a 128 (px del ícono más grande) | 86 |
| `dockAutohide` | `true`/`false` | `false` |
| `dockRecents` | `true`/`false`: las últimas 3 apps usadas que no están fijadas, después de una línea | `false` |
| `taskbarPosition` | `bottom`, `top`, `left`, `right` (tema Clásico) | `bottom` |

Las apps recientes las anota el SDK en `dockRecentApps`, cada vez que una app abre una ventana
(`useDockEntries(fijadas, { recents: 3 })`).

## Área de trabajo

Cada tema vuelve a llamar a `configureWindows({ apps, workArea })` cuando cambia la posición, el
tamaño, «esconder solo» o la pantalla. El área deja afuera el lado del Dock o de la barra. Así:

- las ventanas maximizadas y las nuevas no quedan debajo;
- los avisos (`NotificationBanner`) se acomodan solos.

Con «esconder solo» el área es toda la pantalla, menos la barra de menú, como en macOS. Las ventanas
que ya estaban abiertas no se mueven.

Si hay muchas apps, los íconos se achican para que el Dock entre en la pantalla, contando lo que crece
con la lupa.

## Esconder solo, sin costo

El Dock no se dibuja mientras está escondido. Un oyente del puntero (`useAutohide` en
`themes/default/components/Dock.jsx`) solo mira si el puntero tocó el borde del lado elegido:

1. al tocarlo, espera 120 ms y muestra el Dock con una animación de 180 ms;
2. si el puntero sale del Dock (con 8 px de margen), espera 500 ms y lo esconde;
3. con el menú del clic derecho abierto o el puntero sobre un ícono, se queda.

Solo cambia el estado al cruzar esos límites. Mover el mouse por el resto de la pantalla no vuelve a
dibujar nada, y la animación vuelve a dibujar solo el Dock, no el escritorio. La lupa ya era así: su
estado es del componente Dock.

## Derecha a izquierda (árabe, hebreo)

**Izquierda y derecha son lados físicos de la pantalla**, como en macOS: con un idioma RTL el Dock a
la izquierda sigue a la izquierda.

- El Dock se maqueta con `direction: 'ltr'`.
- Abajo, el orden de los íconos se invierte en RTL, como antes.
- De costado, el orden es siempre de arriba abajo.
- Los menús del Dock se abren hacia adentro de la pantalla con `side` (en `Menu.jsx`), sin la vuelta
  que hacen en RTL los otros menús.
- En el tema Clásico la barra también es física; el menú de inicio y la lista de saltos se pegan al
  lado de la barra.

Ajustes lo dice debajo de «Posición en la pantalla».

## Pendiente

- Efecto al minimizar (genio o escala): el motor todavía no tiene `transform: scale`.
- Que la barra del tema Clásico se esconda sola.
- Arrastrar el Dock a otro borde con el mouse, como macOS con ⇧ en el separador.

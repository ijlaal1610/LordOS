## En pocas palabras

LordOS tiene un escritorio propio, pero abajo sigue siendo Debian. Tenés a mano las decenas de miles de programas del archivo de Debian, y las apps gráficas hechas con GTK o Qt, más navegadores como Chromium y Firefox, se abren como ventanas de LordOS, con el mismo marco, el mismo Dock y el mismo modo claro u oscuro que las apps nativas.

[[shot:0]]

El truco es un idioma en común. Las apps de Linux hablan **Wayland**, un protocolo para decir "acá está mi ventana, acá están mis píxeles, mandame el mouse y el teclado". El servidor gráfico de LordOS también lo habla. Así, una app que no sabe nada de LordOS igual puede vivir en su escritorio, como un viajero que habla el idioma del lugar y no necesita guía.

[[diagram:linux]]

## Cómo se instalan

- **La Tienda** muestra las apps de Debian por categoría, con búsqueda, y te muestra un plan (qué se va a instalar o quitar) antes de hacer nada. También instala las apps `.lpk` propias de LordOS y archivos `.deb` locales.
- **El motor de paquetes propio de LordOS** lee tus fuentes de apt, verifica los índices firmados con su propio código OpenPGP, resuelve las dependencias y controla cada archivo descargado contra el SHA-256 del índice firmado. Recién entonces `dpkg`, la herramienta propia de Debian, desempaqueta y configura el paquete. La base de datos de dpkg la sigue escribiendo solo dpkg, así que `apt` y `dpkg` siguen funcionando normalmente en la Terminal.
- **La Terminal** funciona como en cualquier sistema Debian. Cada pestaña ya tiene las variables de Wayland configuradas, así que arrancar un programa GTK o Qt desde ahí simplemente funciona.

[[shot:1]]

## Cómo se abren

Las apps con un archivo `.desktop` aparecen en el lanzador de apps, en el menú de inicio del tema Clásico y en Buscar, con sus íconos. Tocar una pasa por una puerta en `lord-server` que es angosta a propósito:

1. La interfaz solo puede pasar el **id** de un archivo `.desktop` que ya esté instalado en `/usr/share/applications` o `~/.local/share/applications`, nunca un comando.
2. `lord-server` vuelve a leer ese archivo, verifica que sea una aplicación real, visible y que no sea de terminal, y arma el comando a partir de su línea `Exec` según la especificación, sin shell y sin argumentos de quien llama. "Abrir con" en Archivos puede agregar exactamente un archivo, solo si la app declara los tipos de archivo que maneja y el archivo está en tu carpeta personal o en un disco que montaste.
3. La app arranca en su propio cgroup, `app-lordos-<id>.scope`, creado por root, así el Monitor, el firewall por app y sus restricciones saben a qué app pertenece cada proceso. Si le restringiste a la app tus archivos, acá es también donde recibe su propia vista: sus propios espacios de nombres, una carpeta personal vacía con solo sus propias carpetas, y Landlock.

El JavaScript de LordOS no puede escribir archivos `.desktop`, así que la interfaz no puede inventar un comando: solo puede elegir una app que ya está instalada.

## Cómo se ven y se comportan

- **El marco.** Las apps que le piden al compositor que dibuje su marco (las apps Qt, foot, Chromium con la barra de título del sistema) reciben el marco del tema. Las apps GTK 3 como Firefox lo reciben por un segundo protocolo de decoración que LordOS también habla. Las apps GTK 4 que dibujan su propia barra la conservan, y LordOS les recorta la sombra.
- **Claro y oscuro, en vivo.** LordOS sirve por su cuenta el portal de escritorio "Settings" en el bus de la sesión, así que las apps GTK 4 y libadwaita siguen el modo, el color de acento y el orden de los botones. Las apps GTK 3 lo siguen a través del archivo de claves de GSettings, y LordOS trae un tema GTK que combina. Las dos cambian en vivo cuando cambiás de modo.
- **Un solo portapapeles.** El texto que copiás en una app de Linux se pega en LordOS y al revés; las imágenes siguen disponibles después de cerrar la app de origen y aparecen en el historial del portapapeles (❖V).
- **Nítidas en cualquier escala.** Cada pantalla es un `wl_output` aparte, y la escala fraccionaria permite que GTK 4, Qt 6 y Chromium se dibujen nítidos al 125–175 %.
- **Gestos del touchpad.** El driver de touchpad propio de LordOS les pasa a las apps de Linux los gestos de pellizcar, deslizar y mantener, así que el zoom con pellizco funciona en los navegadores.
- **Juegos y apps de GPU.** Con una GPU, las apps le pueden pasar a LordOS buffers de GPU directamente (`linux-dmabuf`), usar pantalla completa de verdad, bloquear el puntero y recibir el movimiento crudo del mouse.
- **Las notificaciones** de las apps de Linux llegan por el servicio estándar de notificaciones, que provee `lord-server`, y se pueden configurar por app en Ajustes.
- **Contraseñas:** el Secret Service estándar lo sirve el llavero de LordOS, así que los navegadores pueden guardar ahí sus secretos sin otro demonio de llavero.

[[shot:2]]

[[shot:3]]

## Qué protocolos Wayland hay

`wl_compositor`, `wl_subcompositor`, `wl_shm`, `wl_output` (uno por pantalla), `xdg_wm_base` con menús emergentes, `wl_seat` (puntero, teclado, táctil), `wl_data_device_manager` (portapapeles), `zxdg_decoration_manager_v1`, el protocolo de decoración del servidor de KDE, `zxdg_output_manager_v1`, `wp_fractional_scale_v1`, `wp_viewporter`, `zwp_linux_dmabuf_v1`, `wp_presentation`, `zwp_relative_pointer_manager_v1`, `zwp_pointer_constraints_v1`, `zwp_pointer_gestures_v1` y `wp_cursor_shape_manager_v1`.

Ausente a propósito: cualquier protocolo que le permita a una app capturar la pantalla o las ventanas de otras apps.

[[shot:4]]

## Las preguntas del lector escéptico

**¿Las apps de Linux están en una caja de arena?** No de forma predeterminada. Esto es importante: corren como tu usuario, como en cualquier sistema Debian, y pueden leer tu carpeta personal. Lo que LordOS suma es un cgroup por app, la atribución en el Monitor, el firewall por app que aplica el kernel (permitir, denegar, solo LAN, sin conexiones entrantes, reglas por dominio o dirección) y restricciones que podés fijar por app: sin cámara, sin USB y sin sockets Bluetooth (en el kernel), sin micrófono (la guardia de PipeWire de LordOS más los nodos de captura de la placa de sonido), sin notificaciones y "sin acceso a tus archivos", que se aplica desde el próximo inicio. No cubren el portapapeles, el Bluetooth a través de BlueZ en el bus del sistema ni los programas iniciados fuera del lanzador de apps. Solo las apps `.lpk` propias de LordOS tienen la caja de arena completa.

**¿Una app de Linux puede ver otras ventanas o grabar la pantalla?** No a través de LordOS. El compositor solo le da a una app sus propias superficies y no ofrece ningún protocolo de captura de pantalla, no hay servidor X ni portal de captura de pantalla, y a una app restringida también se le niega el dispositivo de framebuffer, así que las ventanas protegidas como Contraseñas también quedan a salvo de ellas.

**¿Y las apps que solo usan X11?** Todavía no funcionan: Xwayland no es parte de LordOS 0.8.1 Beta. Está planeado antes de la 1.0, con las apps X11 con la misma identidad por app, el mismo firewall y las mismas restricciones. Las apps GTK y Qt actuales hablan Wayland, y también el juego SDL que se probó.

**¿Y Flatpak?** LordOS no lo usa ni lo integra, y no se probó.

**¿Por qué los paquetes llegan por HTTP?** El motor de paquetes de LordOS no tiene TLS propio, así que las fuentes `https://` se descargan por `http://`. La seguridad viene de las firmas y los hashes: cada índice tiene que llevar una firma válida de una clave de confianza, no puede estar vencido ni ser más viejo que el que ya está guardado, y cada archivo se controla contra su SHA-256 antes de usarse. Alguien en la red puede ver qué paquetes descargás, pero no puede modificarlos.

## Límites honestos

- **Todavía sin Xwayland** (planeado antes de la 1.0), así que no hay apps que usen solo X11.
- **Todavía no:** arrastrar y soltar entre apps, la selección primaria con clic del medio, los métodos de entrada de LordOS (coreano, japonés, chino) dentro de las apps de Linux, y los pedidos de activación de apps.
- **Íconos solo en SVG:** LordOS todavía no dibuja SVG. Usa los íconos PNG que publica Debian para la mayoría de las apps, o la inicial.
- **Las apps de terminal** con archivo `.desktop` no aparecen en el lanzador de apps; arrancalas desde la Terminal.
- **Las apps GTK 3 mantienen su propio acento azul;** la lectura del esquema de colores desde el portal en Qt 6 no se probó.

## Leé el código

- [server/src/wl/wl.c](https://github.com/lordbasex/LordOS/blob/main/server/src/wl/wl.c): el compositor Wayland y sus protocolos
- [server/src/apps.c](https://github.com/lordbasex/LordOS/blob/main/server/src/apps.c): el lanzador de `.desktop`
- [server/src/ui/portal.c](https://github.com/lordbasex/LordOS/blob/main/server/src/ui/portal.c): el portal de configuración para GTK y libadwaita
- [sdk/wayland.js](https://github.com/lordbasex/LordOS/blob/main/sdk/wayland.js): las ventanas de Linux en el gestor de ventanas
- [server/src/pkg](https://github.com/lordbasex/LordOS/tree/main/server/src/pkg): el motor de paquetes Debian
- [apps/store](https://github.com/lordbasex/LordOS/tree/main/apps/store): la Tienda
- [docs/WAYLAND.md](https://github.com/lordbasex/LordOS/blob/main/docs/WAYLAND.md), [docs/PACKAGES.md](https://github.com/lordbasex/LordOS/blob/main/docs/PACKAGES.md), [docs/GPU.md](https://github.com/lordbasex/LordOS/blob/main/docs/GPU.md)

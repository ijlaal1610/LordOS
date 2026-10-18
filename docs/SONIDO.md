# Sonido en LordOS

El servidor de sonido es **PipeWire**, el mismo que usan las apps GTK, Qt y Firefox. LordOS no
trae otro servidor ni usa libpipewire o libpulse: lord-server le habla a PipeWire con un
cliente propio en C (`server/src/sound/`). Con ese cliente, la interfaz muestra y cambia:

- las salidas y entradas;
- el volumen de cada canal;
- el silencio;
- el dispositivo predeterminado;
- el volumen y la salida de cada app;
- y un medidor de nivel del micrófono.

| | |
|---|---|
| ![centro](m7-centro-control-sonido.png) | ![ajustes](m7-ajustes-sonido.png) |
| Volumen en el Centro de control (lo cambia en PipeWire: `pactl` da el mismo valor) | Ajustes → Sonido: salidas, entradas, prueba y volumen por app (`paplay` sonando) |
| ![placa](m7-ajustes-sonido-placa.png) | |
| Con la placa HDA de la VM (`audio0: ich9-intel-hda`): salida, conector (Line Out ↔ Speakers, probado con `pactl`) y micrófono con medidor | |

## Por qué el protocolo de PulseAudio

PipeWire acepta clientes por dos protocolos:

1. **El suyo** (`pipewire-0`). Viaja serializado en *POD* de SPA, que son estructuras anidadas
   con alineación, tipos de 64 bits y objetos con propiedades por id. Cambiar un volumen ahí
   implica:
   - hacer un *bind* del nodo;
   - armar un `Props` dentro de un `SPA_PARAM_Props`;
   - y conocer cómo WirePlumber guarda los volúmenes: si se cambia el nodo directo,
     WirePlumber no lo recuerda.

   El protocolo no está documentado aparte de libpipewire.
2. **El de PulseAudio** (`pulse/native`), que sirve `pipewire-pulse`. Es un protocolo estable
   desde hace 15 años y está documentado en el código de PulseAudio. Es simple: una lista de
   valores con tipo (`tagstruct`). Tiene órdenes de alto nivel ("volumen de este sink", "app a
   esta salida", "salida predeterminada") que PipeWire traduce y WirePlumber recuerda. Es lo que
   usan pavucontrol, los applets de GNOME y KDE, y `pactl`.

Elegimos el 2: con unas 1300 líneas (`snd.c` y `pulse.c`) cubre todo lo que necesita la interfaz. Igual es hablarle a
PipeWire: `pipewire-pulse` es un módulo de PipeWire, no un servidor aparte. El formato se tomó
de `pulsecore/protocol-native.c` y `tagstruct.h` de PulseAudio 17. Ese código solo se leyó para
seguir el formato: no se enlaza ni se copia.

## Cómo funciona

- **Conexión.** `snd_start()` arranca después de soltar privilegios. Se conecta a
  `$PULSE_SERVER` (`unix:/ruta`) o a `$XDG_RUNTIME_DIR/pulse/native`. Si no está
  `XDG_RUNTIME_DIR`, usa `/run/user/<uid>`. Solo se conecta si el socket pertenece al usuario.
- **Saludo.** Manda `AUTH` (protocolo 32, sin memoria compartida, así que los datos van por el
  socket) y `SET_CLIENT_NAME` ("LordOS", `org.lordos.Server`).
- **Estado.** Se suscribe a los cambios de sinks, sources, apps, servidor y tarjetas, y pide las
  cuatro listas. Con cada aviso pide solo lo que cambió.
- **Avisos a JS.** Cuando cambió algo y ya llegaron todas las respuestas, se avisa a JS con un
  `state` que lleva el estado completo.
- **Reconexión.** Si PipeWire se cae o se reinicia, `snd_fd()` pasa a ser un temporizador y se
  reintenta cada 2 s. Mientras tanto, el estado dice `connected: false`.
- **Medidor.** Es un stream de grabación de la entrada elegida: 1 canal en float, a 25 Hz, con
  `PEAK_DETECT` (lo mismo que hace pavucontrol). El servidor manda un pico por paquete.
- **Sonidos del sistema.** `pop`, `notify` y `error` se sintetizan en el momento: senos con
  envolvente, sin archivos ni decodificador de OGG. Se suben una vez al caché de muestras del
  servidor y después se tocan con `PLAY_SAMPLE` en la salida predeterminada, con
  `media.role=event`.
- **Streams propios.** Los streams de LordOS (el medidor y los sonidos) no aparecen en las
  listas de apps. Si no, el medidor del micrófono haría parecer que alguien está grabando.

## API: `lord.sound`

| Función | Qué hace |
|---|---|
| `state()` | el estado completo (forma abajo) |
| `setVolume(kind, index, valor)` | `valor` es un número (lleva al canal más alto a ese nivel y mantiene el balance) o una lista por canal; 1 = 100 %, hasta 1.5 |
| `setMute(kind, index, bool)` | silencio |
| `setDefault('sink' \| 'source', name)` | salida o entrada predeterminada |
| `setPort('sink' \| 'source', index, port)` | parlantes ↔ auriculares, etc. (un puerto de `ports`) |
| `move('input', app, sink)` / `move('output', app, source)` | pasa una app a otra salida o entrada |
| `meter(sourceIndex)` / `meter(-1)` | prende o apaga el medidor; de una salida, se usa su `monitor` |
| `play('pop' \| 'notify' \| 'error')` | sonido del sistema |

- `kind` es `'sink'` (salida), `'source'` (entrada), `'input'` (app que suena) u `'output'`
  (app que graba).
- Todas devuelven `true` si la orden salió. El resultado se ve en el próximo `state`.
- Validan el tipo, el índice (entero de 32 bits) y los nombres (sin caracteres de control,
  menos de 256 bytes). Además, el índice o nombre tiene que existir en el estado actual.

Eventos: `globalThis.__lordSound = (type, data) => …`

- `type === 'state'`: `data` es lo mismo que `state()`.
- `type === 'peak'`: `data` es `{ source, level }` (0..1), 25 veces por segundo mientras el
  medidor está prendido.

Forma del estado:

```js
{
  connected: true, server: 'PulseAudio (on PipeWire 1.4.2)', version: '15.0.0',
  defaultSink: 'alsa_output.pci-…', defaultSource: '…', meter: -1,
  sinks: [{ index, name, description, icon, formFactor, bus,
            volume: [1, 1], level: 1,          // por canal y el máximo; 1 = 100 %
            mute, state,                       // 0 corriendo, 1 inactivo, 2 suspendido
            monitor,                           // índice de su fuente "monitor" (para el medidor)
            card, default: true,
            ports: [{ name, description, priority, available }],   // available: true/false/null (no se sabe)
            activePort }],
  sources: [{ …lo mismo, con monitorOf (índice del sink, o -1 si es una entrada real) }],
  inputs:  [{ index, name, app, appId, icon, binary, role, pid, sink,
              volume, level, mute, corked, hasVolume, volumeWritable }],   // apps que suenan
  outputs: [{ …lo mismo, con source en vez de sink }],                      // apps que graban
}
```

Las entradas con `monitorOf >= 0` son el "monitor" de una salida, lo que suena por ella. La
interfaz normalmente las oculta de la lista de micrófonos.

## Pruebas

- `make -C server test` incluye `build/sound-test`: ida y vuelta de cada tipo del tagstruct,
  listas de propiedades, descriptores, paquetes truncados (ninguno lee fuera del búfer) y
  tipos equivocados.
- `build/sound-cli` (`make -C server build/sound-cli`) es un cliente de línea de comandos sobre
  el mismo `snd.c`, contra el PipeWire real: `list`, `volume`, `mute`, `default`, `port`,
  `move`, `meter`, `play`, `watch`. `LORD_SND_DEBUG=1` muestra cada paquete.

Probado en la VM (PipeWire 1.4.2) con dos salidas virtuales (`module-null-sink`) y una entrada
virtual. Los resultados se compararon con `pactl` y `wpctl status`:

| Prueba | Resultado |
|---|---|
| Listar salidas, entradas, monitores y apps (`paplay` con `--client-name`) | igual que `pactl list` (nombre, descripción, canales, volumen, pid, binario) |
| Volumen 42 %; balance 30/70; nivel 90 % manteniendo el balance | `pactl get-sink-volume`: 42 %; 30 % y 70 %; 39 % y 90 % |
| Silencio de salida y de app | `pactl`: `Mute: yes` / `no` |
| Salida y entrada predeterminadas | `pactl get-default-sink` y `wpctl status` (el `*`) |
| Volumen de una app (25 %) y pasarla a otra salida | `pactl list sink-inputs`: 25 %, `Sink: 59` |
| Medidor | 50 picos en 2 s (25 Hz) con sonido; nivel ~0 en silencio |
| Sonidos del sistema | `pactl list samples`: `lordos-pop`, `lordos-notify` y `lordos-error`; al tocarse, `pactl subscribe` ve aparecer y terminar el stream |
| Cambios hechos por otro programa (`pactl set-sink-volume`, `set-default-sink`) | llega un `state` nuevo con el valor |
| `systemctl --user restart pipewire pipewire-pulse wireplumber` | "desconectado… reintentando", `connected: false`, y a los 2 s reconecta solo |
| `lord.sound` desde JS (QuickJS con el binding, sin la pantalla) | cambia volumen y silencio, prende el medidor y toca sonidos; rechaza tipo, índice o nombre inválidos |

lord-server compila con el cliente adentro (`main.c` hace poll de `snd_fd()`). La interfaz de
sonido del Centro de control y Ajustes se arma aparte, sobre esta API.

## Sesión: dónde está PipeWire

PipeWire corre como servicio de usuario de systemd (`pipewire`, `pipewire-pulse` y
`wireplumber`, con activación por socket). Esos servicios viven en el `systemd --user` del
usuario, que existe mientras el usuario tiene una sesión o "linger".

- **Hoy en la VM:** lord-server se arranca con `sudo` desde SSH y después suelta privilegios a
  `lord`. Para que PipeWire esté aunque no haya un SSH abierto, se activó
  `loginctl enable-linger lord`: el `systemd --user` arranca con la máquina y deja los sockets
  en `/run/user/1000`. lord-server encuentra el socket ahí aunque `sudo` haya borrado
  `XDG_RUNTIME_DIR`. El compositor Wayland ya completa esa variable con `/run/user/<uid>`.
- **Con la sesión propia (M8):** el inicio de sesión pasa por PAM con `pam_systemd`. Eso crea la
  sesión de logind con asiento (`seat0`), `XDG_RUNTIME_DIR` y el `user@<uid>.service`. Así se
  levanta PipeWire por socket sin linger. Además, logind les da al usuario del asiento acceso a
  `/dev/snd/*` (ACL `uaccess`), que hoy, por SSH y sin asiento, no tiene: `lord` no está en el
  grupo `audio`.

## Paquetes en la VM

| Paquete | Para qué |
|---|---|
| `pipewire`, `pipewire-pulse`, `wireplumber` | el servidor de sonido (Debian 13 lo trae por defecto en los escritorios) |
| `pipewire-alsa` | que las apps que usan ALSA directo pasen por PipeWire |
| `pulseaudio-utils` | solo para probar: `pactl` y `paplay` |
| `sound-theme-freedesktop` | solo para probar: sonidos `.oga` para `paplay` |

Configuración: `sudo loginctl enable-linger lord` y
`systemctl --user enable --now pipewire pipewire-pulse wireplumber`. Para leer el formato se
bajó el código de PulseAudio con `apt-get source --download-only pulseaudio` en `~/snd-ref`:
solo se usó como referencia y no se compila.

## Tiempo real: PipeWire no tiene que esperar a nadie

El audio de Chromium (YouTube) se cortaba en el ISO live.

**Causa.** PipeWire corría con prioridad normal (`SCHED_OTHER`, nice 0), igual que lord-server, que
compone por CPU, y que el navegador. Pide tiempo real de dos formas, y en LordOS no andaba ninguna:

- **Por los límites del usuario (`RLIMIT_RTPRIO`).** No los tenía.
- **Por RealtimeKit (`rtkit`).** El ISO lo trae, pero rtkit le pregunta a polkit. La regla de rtkit
  es `allow_any=no`: solo da tiempo real a procesos de una sesión activa en un asiento de logind. Las
  sesiones de `lord-sessiond` corren en `user@<uid>.service` sin asiento, así que polkit dice que no.
  En el registro de PipeWire sale «can't set rt prio to 83 (try increasing rlimits)».

En una PC el quantum es chico (los navegadores piden ~5–10 ms). Con PipeWire compitiendo por la CPU,
cualquier demora más larga que eso es un corte. En una VM no se nota, porque PipeWire sube el mínimo
a 1024 (21 ms) cuando detecta una VM (`vm.overrides`).

**Arreglo.** Los usuarios de LordOS están en el grupo **`pipewire`**:
- El paquete `pipewire` ya trae `/etc/security/limits.d/25-pw-rlimits.conf` (`@pipewire`: rtprio 95,
  nice -19, memlock 4 MB).
- `pam_limits` está en la sesión de `systemd --user` (`/usr/lib/pam.d/systemd-user`).

Así PipeWire toma tiempo real por sí mismo, sin polkit: sus hilos `data-loop` quedan en
`SCHED_FIFO` 88/83 y los principales en nice -11. Se agrega el grupo:
- al usuario del ISO live (`tools/iso/build.sh`);
- al primer usuario del instalador (`server/src/install/install.c`);
- a los usuarios nuevos de Ajustes → Usuarios (`server/src/users/users_admin.c`).

Un usuario que ya existía necesita volver a entrar, o `systemctl restart user@<uid>`.

**Prueba (VM 118)** con `pw-top`. «Cortes» es la diferencia del contador ERR, de la salida y del
stream:

| Prueba | Sin tiempo real | Con el grupo `pipewire` |
|---|---|---|
| `paplay` a 5 ms (`PULSE_LATENCY_MSEC=5`, como un navegador en una PC), 30 s, arrastrando ventanas y 6 procesos gastando CPU | **16 cortes en la salida, 303 en el stream** | **0 y 0** |
| Lo mismo sin carga extra, arrastrando | 0 y 2 | 0 y 0 |
| YouTube en Chromium, 3 minutos, arrastrando ventanas y 2 procesos gastando CPU | — | 0 y 0 |

Para el «sin tiempo real» se sacó el ajuste de VM de PipeWire con dos archivos de prueba en
`~/.config/pipewire/*.conf.d/` (borrados después).

lord-server no necesita tiempo real y no frena a PipeWire:
- su cliente de PulseAudio es no bloqueante (`O_NONBLOCK`; si el socket está lleno, guarda y sigue);
- gasta 2 % de CPU con o sin audio sonando.

No hizo falta cambiar el quantum de `pipewire-pulse`.

## Pendiente

- **Parlantes de verdad.** La VM tiene ahora una placa HDA con códec (`audio0: ich9-intel-hda`;
  el usuario `lord` está en el grupo `audio`): WirePlumber ve la salida y el micrófono
  analógicos y `setPort` (Line Out ↔ Speakers) se probó con `pactl`. Que se escuche de verdad
  no se puede comprobar en una VM sin audio en el host (`driver=none`); falta probarlo en una
  máquina física.
- **Perfiles de tarjeta** (estéreo, HDMI, dúplex): falta `GET_CARD_INFO_LIST` y
  `SET_CARD_PROFILE`.
- **Volumen de entradas virtuales.** El volumen de una entrada virtual
  (`media.class=Audio/Source/Virtual`) no cambia, ni con `pactl`: es así en PipeWire 1.4.
- **Salidas virtuales suspendidas.** Una salida virtual suspendida (sin nada sonando) recién
  creada después de reiniciar PipeWire ignora el volumen, también con `pactl`. Con algo sonando
  funciona. No pasa con salidas reales.
- **Sonidos del sistema con archivos.** Los temas freedesktop vienen en OGG Vorbis y se
  necesitaría un decodificador. Hoy hay 3 sonidos sintetizados.
- **Eventos repetidos.** Los cambios de los streams propios (medidor, sonidos) generan algunos
  `state` de más, aunque no se muestran. Unos pocos por segundo al prenderlos, no continuos.

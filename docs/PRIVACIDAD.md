# Privacidad: aislamiento de apps y permisos (M8)

LordOS aísla las apps de terceros (`.lpk`) y controla qué puede usar cada una (cámara,
micrófono, archivos, red…) con permisos al estilo **TCC de macOS**: el sistema pregunta la
primera vez y el usuario decide. Ver también [FIRMA.md](FIRMA.md) (paquetes firmados) y
[APPS-NATIVAS.md](APPS-NATIVAS.md).

## Estado

| Pieza | Estado |
|---|---|
| Cada app `.lpk` corre como **su propio proceso** (`lord-server --app <id>`), cliente Wayland del compositor | **hecho**, probado en la VM 122 |
| **Broker**: un solo socket entre la app y `lord-server`, que verifica el permiso de cada llamada | **hecho** (fs, portapapeles, permisos, indicadores) |
| Caja de arena propia (`server/src/sec/sandbox.c`): Landlock + seccomp (TSYNC) + cgroup + sin capacidades | **hecho** (`build/sandbox-test`: 13/13 app, 13/13 con red) |
| Variante endurecida para ayudantes de decodificación (`sandbox_apply_helper`, lord-media y lord-doc) | **hecho** (`build/sandbox-test helper`: 17/17) |
| Caja de arena (`build/sandbox-test`) | **13/13→14/14 app**, 14/14 con red, 17/17 ayudante |
| Permisos por app, estilo TCC, guardados donde la app no escribe (`server/src/sec/perms.c`) | **hecho** |
| Avisos dibujados por el sistema (también los que pide una app aislada por el broker), Ajustes → Privacidad, puntos verde/naranja | **hecho** |
| cgroup por app (`app-lordos-lpk-<id>.scope`, `app-lordos-<id>.scope`) con límites, en el subárbol delegado de la sesión | **hecho**, probado con `lord-sessiond` y `Delegate=yes` |
| **Root-owned app scopes**: an app can't move itself to another cgroup (`lord-sessiond` places each process, see [Root-owned app scopes](#root-owned-app-scopes)) | **done**, tested on VM 122 (the `systemd-run --user` escape is now closed by the `user@` union, see there and `docs/FIREWALL.md`) |
| Firma de paquetes y niveles de confianza | **hecho**, ver [FIRMA.md](FIRMA.md) |
| Tokens del tema, idioma y dirección (RTL) **en vivo** dentro del proceso de la app | **hecho** (claro/oscuro, acento, idioma; probado con árabe) |
| Revocación en vivo: al denegar un permiso, la app suelta la cámara/el micrófono al instante | **hecho** |
| Powerbox: diálogo de abrir/guardar **dibujado por lord-server** que entrega solo el fd del archivo elegido | **hecho** (`fs.openFile`/`fs.saveFile`) |
| Menús de la app en la barra de menú del sistema | **hecho** (por el broker) |
| **Micrófono** como datos (audio) por un anillo de solo lectura, tras consentimiento | **hecho** (lord-server abre el flujo de PipeWire; la app lee el nivel por su cursor; revocar lo cierra) |
| Cámara como frames decodificados por un anillo (vía lord-media), tras consentimiento | **done**, tested on VM 122 with v4l2loopback (see [Camera for isolated apps](#camera-for-isolated-apps)) |
| Varias ventanas por app aislada | **done**, tested on VM 122 (see [Several windows per isolated app](#several-windows-per-isolated-app)) |

## El proceso por app

Cuando se abre una app `.lpk` (Launchpad, Dock, Tienda), `lord-server` la lanza como su propio
proceso: **el mismo binario en «modo app»**, `lord-server --app <id>` (`server/src/app/`).

```
lord-server (sesión, compositor)                     lord-server --app evil (aislado)
├── compositor Wayland (wl.c)   ◀── cliente Wayland ── ventana (wl_shm, xdg_toplevel)
├── broker (sec/brokersrv.c)    ◀── socket (SEQPACKET) ── lord.fs / clipboard / permisos…
└── avisos del sistema (sdk/privacy.js)                 QuickJS + React + SDK + app.js
```

- **Lanzamiento** (`app/appspawn.c`): solo con un id **validado** de un paquete instalado
  (`lpk_valid_id` y la carpeta existe); nunca argumentos del llamador. Es la tercera excepción
  acotada a «el servidor no ejecuta comandos» (ver `AGENTS.md`). Se crea un `socketpair` para el
  broker y se le pasa al hijo (`LORD_BROKER_FD`). Desde JS: `lord.apps.launchPackage(id)`; el SDK
  lo usa en `launchApp()` y, si algo abre la ventana de un paquete en proceso (`wm.open`, «Nueva
  ventana» del Dock), esa ventana lanza el proceso y se cierra sin evaluar `app.js`
  (`sdk/packages.js`).
- **El proceso de la app** (`app/appclient.c`):
  1. conecta al compositor como cliente Wayland (`libwayland-client`), pide **decoración del
     servidor** (como las apps Qt), así el tema le dibuja **el mismo marco** que a las apps
     nativas, y pone su `app_id` = el id del paquete;
  2. aplica la caja de arena (abajo) **antes** de cargar código de la app;
  3. corre su propio QuickJS, React y motor (`ui/`), y dibuja en un búfer `wl_shm` que entrega al
     compositor.
- **En el escritorio** llega como cualquier ventana Wayland: el SDK la muestra con el marco del
  tema, el **nombre** y el **ícono del paquete** (`sdk/wayland.js`, `windowIcon`), en el Dock,
  Mission Control y el escritorio remoto.
- **El SDK de la app no cambia** para quien la escribe: el «app host» (`runtime/app-host.js`,
  `assets/app-host.bundle.js`) trae React, el runtime y `@lordos/sdk`; `lord.*` de dibujo es local
  y lo demás va por el broker (`ui/js_broker.c`).

![La app maliciosa en su propio proceso: todos los intentos bloqueados](m8-privacidad-evil-proceso.png)

| | |
|---|---|
| ![modo claro, acento violeta, en vivo](m8-privacidad-app-claro.png) | ![la misma app en árabe (RTL), en vivo](m8-privacidad-app-arabe.png) |
| El tema cambió a claro y al acento del usuario: la app aislada lo sigue al instante | Pasar la sesión a árabe da vuelta la app y la retraduce, sin reiniciarla |
| ![powerbox: diálogo dibujado por el sistema](m8-privacidad-powerbox.png) | ![menús de la app en la barra del sistema](m8-privacidad-app-menu.png) |
| Abrir un archivo: el diálogo lo dibuja lord-server; la app recibe solo el fd | Los menús de la app aislada en la barra de menú del sistema |
| ![revocar cierra el micrófono](m8-privacidad-revocar.png) | ![app detenida: el compositor no cambia](m8-privacidad-app-detenida.png) |
| Denegar el micrófono lo apaga al instante (dejó de grabar, el punto se fue) | Con la app colgada (SIGSTOP) el escritorio responde igual |

### El broker (`sec/broker.h`, `sec/brokersrv.c`, `ui/js_broker.c`)

Mensajes `SOCK_SEQPACKET`: encabezado (`reqid`, `op`, `status`, `len`) + datos. Cada pedido se
verifica en `lord-server`:

| Pedido | Qué hace `lord-server` |
|---|---|
| `fs.home/read/write/list` | solo dentro de `~/.local/share/lordos/appdata/<id>`; el resto, **denegado** |
| `clipboard.get` | solo con el permiso `clipboard` concedido; `clipboard.set` siempre |
| `broker.request(perm)` | permitido/denegado según lo guardado; si «preguntar», encola un **aviso del sistema** y responde cuando el usuario decide (la app espera) |
| `broker.device(kind, on)` | enciende/apaga el indicador de cámara/micrófono, solo si ese permiso está concedido |
| `broker.pick(save, nombre)` | **powerbox**: lord-server dibuja el diálogo de abrir/guardar y le pasa a la app **solo el fd** del archivo elegido (SCM_RIGHTS); la app no ve la carpeta |
| `broker.menus(json)` | publica los menús de la app para la barra de menú del sistema; los clics vuelven como un evento |

Además, lord-server le manda a la app, sin que los pida, el **estado en vivo** (`BOP_EVENT`): los
tokens del tema (claro/oscuro, acento, fondo), el idioma y la dirección (RTL), la distribución de
teclado, la **revocación** de un permiso (para que suelte el dispositivo) y el ítem de menú que el
usuario eligió. El servidor nunca espera a una app: lee y escribe el socket sin bloquear
(`MSG_DONTWAIT`) en el mismo `poll` del bucle principal; si una app se cuelga, solo se cuelga ella
(los cuadros del compositor no cambian, ver «Rendimiento»). Al cerrarse el socket, se apagan sus
indicadores.

### Tema, idioma y RTL en vivo

El tema publica sus tokens con `lord.broker.publish('theme', …)` y el idioma con
`publish('locale', …)` (`sdk/theme.js`, `sdk/privacy.js`); lord-server los reenvía a todas las
apps aisladas y los guarda para las que arranquen después (`lord.broker.state()` al iniciar). El
«app host» (`runtime/app-host.js`) aplica los tokens y, al cambiar de idioma, vuelve a montar la
app (como el tema). Probado: cambiar a modo claro y el acento se ven al instante en la app, y pasar
la sesión a árabe la da vuelta (RTL) y la retraduce.

### Powerbox

La app no puede abrir la carpeta personal (`sandbox-test`: abrir `~/Documentos` directo falla).
Para abrir o guardar, llama a `fs.openFile()` / `fs.saveFile(nombre, datos)`; lord-server dibuja el
diálogo (`sdk/powerbox.jsx`, en el shell), abre el archivo elegido con los permisos del usuario y le
pasa a la app **solo ese fd** por SCM_RIGHTS. La app lee/escribe el fd (`broker.fdRead/fdWrite`) y
nunca ve el resto del disco.

### Menús y revocación

- **Menús:** `wm.setMenus(id, menús)` en una app aislada serializa los menús (con un id por ítem) y
  los manda por el broker; lord-server los pone en la ventana de esa app y el tema los muestra en su
  barra de menú. Al elegir un ítem, lord-server le avisa a la app, que ejecuta su `onClick`.
- **Revocación en vivo:** al poner un permiso en «Preguntar» o «Denegar» en Ajustes → Privacidad,
  lord-server le avisa a la app (que suelta la cámara/el micrófono con `onPermissionRevoked`) y apaga
  el indicador en el acto, sin cerrar la app. Para el micrófono, además cierra el flujo de PipeWire
  (`maudio_close`) en el servidor: el `source-output` desaparece al instante.

### Micrófono (anillo de difusión de solo lectura)

El micrófono lo abre **lord-server**, no la app: con el permiso en ALLOW, el broker llama a
`maudio_open(1, …)` (`server/src/media/maudio.c`), que crea un flujo de captura de PipeWire y un
anillo en un `memfd` sellado. El broker le pasa a la app un **duplicado de solo lectura** del fd
(`/proc/self/fd/N` abierto `O_RDONLY`, por SCM_RIGHTS): la app solo lo puede mapear `PROT_READ`.

Es un anillo de **difusión**: lord-server escribe (`wpos`) y, como el único consumidor (la app) lee
con su propio cursor y **nunca toca `rpos`**, el servidor libera el espacio él mismo
(`maudio_mic_drain`, avanza `rpos` hasta `wpos`) para que el anillo no se trabe. La app sigue `wpos`,
detecta los desbordes (`wpos - cursor > tamaño`) y lee solo lo más reciente. La app nunca abre
PipeWire ni ve el fd del dispositivo. `lord.broker.micLevel()` devuelve el RMS (0..1).

![nivel del micrófono en vivo en la app aislada](m8-privacidad-mic-nivel.png)

Probado en la VM 122: con audio real entrando por la captura, la barra de nivel de la grabadora se
mueve; al apretar «Detener» o al **Denegar** el permiso en Ajustes, el `source-output` de
lord-server desaparece y la barra y el punto naranja se apagan.

![al denegar el micrófono se cierra la captura](m8-privacidad-mic-revocado.png)

## La caja de arena (`server/src/sec/sandbox.c`)

Código propio, sin bubblewrap, flatpak ni firejail. Usa directamente lo del kernel 6.12:

- **Landlock** (ABI hasta 6): solo lectura de la carpeta del paquete, del «app host», de las
  fuentes (LordOS y `/usr/share/fonts`) y de las bibliotecas; lectura y escritura solo de
  `~/.local/share/lordos/appdata/<id>`; nada más de `$HOME`. Reglas de TCP y aislamiento de
  sockets unix abstractos y señales (`LANDLOCK_SCOPE_*`).
- **seccomp-bpf**: una **lista blanca** de syscalls, aplicada a todos los hilos (`TSYNC`). Lo
  peligroso queda afuera por definición: `ptrace`, `mount`, `kexec`, `bpf`, `perf_event_open`,
  `keyctl`/`add_key`, `init_module`/`finit_module`, `execve`…
- **Sockets**: Wayland y el broker ya están conectados antes de la caja de arena, así que la app
  **no puede abrir sockets unix nuevos** ni `socketpair` (Landlock ≤ 6 no filtra el `connect` a
  sockets unix con nombre: así no llega al bus de sesión, a `cups.sock` ni a `wayland-0`).
- **La red**: `lord-server` corre sin root, así que **no puede** crear un network namespace
  (`unshare(CLONE_NEWNET)` pide `CAP_SYS_ADMIN`). Decisión: la app queda en el netns del equipo,
  y **sin el permiso `network`** seccomp rechaza `socket(AF_INET/AF_INET6)` y Landlock (ABI ≥ 4)
  le niega `bind`/`connect` de TCP. Con el permiso (declarado **y** concedido, se decide al
  lanzar), se permiten. El agente de monitoreo agrega reglas de nftables por cgroup (defensa en
  capas) y atribuye el tráfico por el nombre de la scope.
- **`no_new_privs`**, todas las capacidades soltadas (`capset` vacío + `PR_CAPBSET_DROP`).
- **cgroup v2** propio, `app-lordos-lpk-<id>.scope`: `memory.high` 192 MB, `memory.max` 512 MB,
  `cpu.max` un núcleo.

Si falta algo en el kernel (Landlock viejo, controlador ausente, sin delegación), se avisa por el
registro y se sigue sin esa parte; la app arranca igual.

### Prueba de la app maliciosa (`tests/lpk-evil`, en su propio proceso)

La app declara **cero** permisos e intenta todo al abrirse. Registro de la sesión (VM 122, con
`lord-sessiond`):

```
lord-server: app 'evil' lanzada como proceso aislado
sandbox[evil]: cgroup=1 landlock-fs=1 landlock-net=1 seccomp=1 nopriv=1 nocaps=1 red=no
```

| Intento | Resultado |
|---|---|
| leer `~/.ssh/id_rsa` | bloqueado (el broker solo sirve la carpeta de la app; Landlock la tapa igual) |
| leer `~/.config/lordos/settings.json` | bloqueado |
| escribir fuera de su carpeta | bloqueado |
| leer el portapapeles en silencio | bloqueado (sin permiso `clipboard`) |
| abrir `/etc/shadow` | bloqueado |
| abrir un socket de red, `exec /bin/sh`, `ptrace`, sockets unix nuevos | bloqueados por seccomp (ver `sandbox-test`) |
| abrir un dispositivo (`/dev/dri/card0`, `/dev/video0`) | bloqueado por Landlock (la app no ve `/dev`; la cámara llegará como fd tras el consentimiento). En la VM no hay `/dev/video0`: se probó con la placa |
| el micrófono sin consentimiento | sin sockets unix nuevos no llega a PipeWire; por el broker, solo con el permiso `microphone` |

Como contraste, la misma app abierta **en proceso** (el modo de M7) leía `settings.json` y
escribía `~/.config/lordos/robado.txt`. Por eso ahora un paquete nunca se evalúa en el contexto
del tema cuando existe el modo app.

### `build/sandbox-test` (VM)

```
sandbox-test app: 14/14      appdata se escribe; ~/.ssh, settings.json y $HOME no; sin red;
                             socket unix nuevo y socketpair fallan; el fd abierto antes sirve;
                             ptrace (también a otro proceso), exec de /bin/sh y abrir
                             /dev/dri/card0 fallan (aunque el usuario esté en el grupo video)
sandbox-test net: 14/14      igual, pero socket AF_INET se abre (permiso network)
sandbox-test helper: 17/17   ver «Ayudantes» abajo
```

## Ayudantes de decodificación: `sandbox_apply_helper`

Una sola caja de arena para todo LordOS, sin copias: los ayudantes que decodifican datos ajenos
(`lord-media`: video, fotos, cámara; `lord-doc`: Poppler y libarchive) usan
`sandbox_apply_helper(const SandboxHelperCfg *cfg)` de `server/src/sec/sandbox.h`:

```c
SandboxHelperCfg cfg = {
    .name = "lord-doc",                     /* scope de cgroup */
    .ro_paths = (const char *[]){ "/usr/share/fonts", NULL },
    .ro_dir_fds = NULL,                     /* carpetas ya abiertas, solo lectura (-1 al final) */
    .rw_dir_fds = (int[]){ workfd, -1 },    /* carpetas ya abiertas, lectura y escritura */
    .extra_syscalls = (int[]){ SYS_kcmp, -1 },
    .allow_ioctl_classes = SANDBOX_IOCTL_DRM | SANDBOX_IOCTL_DMABUF,
    .mem_max_mb = 512,
};
sandbox_apply_helper(&cfg);                 /* SANDBOX_HELPER_DIR_FDS indica que existen los fds */
```

- **Sin red y sin sockets nuevos**: `socket`, `socketpair`, `connect`, `bind`, `listen` y
  `accept` caen en EPERM; el canal con `lord-server` llega heredado (`sendmsg`/`recvmsg` siguen).
- **Sin `execve`/`execveat`**. **Solo hilos**: `clone` solo con `CLONE_THREAD`; `clone3` → ENOSYS
  (glibc cae a `clone`, que sí se puede filtrar por flags).
- **TSYNC**: el filtro alcanza también a los hilos que ya existían (VA-API, drivers). Si seccomp
  no se puede aplicar, el ayudante termina (`_exit(70)`): **falla cerrado**. Si falta Landlock
  (algunos kernels de la Raspberry Pi), sigue con seccomp y avisa.
- **Archivos**: nada salvo `ro_paths` y las carpetas que llegan como fds (`rw_dir_fds`: escribir,
  crear archivos, carpetas y enlaces, y borrar archivos, **sin truncar**).
- **ioctl por clase** (`allow_ioctl_classes`): solo los tipos `'d'` (DRM/VA-API), `'V'` (V4L2,
  Pi 5) y `'b'` (sincronizar DMA-BUF), filtrando el byte `_IOC_TYPE` del número de ioctl; nunca
  un ioctl genérico ni `TIOCSTI`. El ayudante no puede abrir `/dev`: recibe los fds de los
  dispositivos que abre `lord-server`.
- **`kcmp`**: solo si se pide en `extra_syscalls`, y solo para comparar fds **del propio proceso**
  (`pid1 == pid2 == getpid()`, el pid se conoce al armar el filtro). Mesa lo necesita
  (`os_same_file_description` deduplica fds DRM); con otro proceso da EPERM.
- La lista base incluye además `mkdirat`, `symlinkat`, `linkat`, `unlinkat`, `fchmod`,
  `utimensat`, `fstatfs`, `eventfd2`, `pipe2`, `getcwd`, `umask`, `fadvise64`, `memfd_create` y
  `ftruncate`; las que solo existen en x86-64 van con `#ifdef`. Lo que un ayudante necesite de
  más (`getcpu`, `membarrier`, `sched_getparam`, `futex_waitv`…) va en `extra_syscalls`, que
  nunca puede volver a habilitar `clone`, `socket`, `connect`, `bind`, `execve` ni `ioctl`.

`sandbox-test helper` (17/17): socket nuevo, `connect` a `/run/user/1000/wayland-0`, `bind` y
`socketpair` fallan; `execve` y `fork` fallan; `clone3` = ENOSYS; un hilo nuevo funciona; un hilo
creado **antes** del filtro tampoco puede `exec` ni `fork` (TSYNC); escribir dentro de
`rw_dir_fds` funciona, truncar ahí y escribir afuera fallan; `/etc` no se lee; las fuentes sí;
`kcmp` consigo mismo funciona y con el padre da EPERM; `ptrace` falla.

## cgroup por app y delegación (`server/src/sec/cgroup.c`)

`lord-server` corre sin root; para crear las scopes de las apps necesita un **subárbol de cgroup
delegado**. Lo prepara `lord-sessiond` (ver [SESIONES.md](SESIONES.md)):

1. `lordos.service` lleva `Delegate=yes`.
2. Al arrancar, `lord-sessiond` se mueve a la hoja `supervisor` (`cgroup_service_init`): la raíz
   del servicio queda sin procesos y puede habilitar `cpu memory pids` para sus hijos (regla «sin
   procesos internos» de cgroup v2).
3. Por cada sesión, `cgroup_delegate_session(uid, gid)` crea `lordos-session-<uid>/server` y mete
   ahí al `lord-server` del usuario. **Update:** the subtree is no longer chowned to the user; it
   stays root's.
4. **Update:** the app scopes (`app-lordos-lpk-<id>.scope`, `app-lordos-<id>.scope`,
   `app-lordos-media.scope`…) are created by `lord-sessiond` on request and are root-owned, so an
   app can't leave its scope. See [Root-owned app scopes](#root-owned-app-scopes).

Medido en la VM 122 (dos usuarios, `lord` en el monitor y `bob` desde el navegador):

```
Unit lordos.service (/system.slice/lordos.service):
├─supervisor
│ ├─16249 lord-sessiond
│ └─16570 lord-server --headless 1280x800 --greeter …
├─lordos-session-1000
│ ├─app-lordos-lpk-evil.scope
│ │ └─16366 lord-server --app evil
│ ├─server
│ │ ├─16254 lord-server --headless 1280x800 --user lord …
│ │ └─16256 lord-server --headless 1280x800 --user lord …
│ └─app-lordos-featherpad.scope
│   └─16401 featherpad
└─lordos-session-1001
  └─server
    ├─16520 lord-server --headless 1280x800 --user bob …
    └─16524 lord-server --headless 1280x800 --user bob …
```

`app-lordos-lpk-evil.scope`: `memory.high` 201326592, `memory.max` 536870912, `cpu.max`
`100000 100000`. Sin delegación (por ejemplo `lord-server` suelto con sudo, en el desarrollo) las
apps corren sin scope, con un aviso.

## Permisos (estilo TCC de macOS)

- **Avisos la primera vez**, dibujados por el sistema, nunca por la app:
  «**«App» quiere usar el micrófono — Permitir / No permitir**». Para una app aislada, el pedido
  llega por el broker y el aviso lo dibuja la sesión (`sdk/privacy.js`, `__lordBroker` →
  `PermissionPrompt` → `lord.broker.resolve`).
- **Dónde se guardan:** por usuario, en `~/.config/lordos/permisos.json`, un archivo que la app
  **no puede escribir**: está en la lista de rechazo de `lord.fs` (`js_sys.c` `resolve_target` y
  `fsops.c` `fsops_protected`), igual que `settings.json`, y fuera de lo que Landlock le deja ver
  al proceso de la app. El estado lo maneja `server/src/sec/perms.c`; la búsqueda es O(1).
- **Manifest:** una app declara `permissions`. El instalador y la Tienda los muestran, y una app
  no puede obtener un permiso que no declaró (por ejemplo, `network` se concede solo si está
  declarado **y** permitido).
- **Revocar** en Ajustes → Privacidad vale para el próximo pedido; un permiso ya usado (por
  ejemplo, un micrófono abierto) sigue hasta que la app lo suelta o se cierra.

Permisos: `files`, `clipboard`, `notifications`, `network`, `camera`, `microphone`, `screen`,
`listen`, `usb`, `bluetooth`, `apps`.

| | |
|---|---|
| ![aviso pedido por una app aislada](m8-privacidad-prompt-broker.png) | ![micrófono en uso](m8-privacidad-indicador-broker.png) |
| La app de prueba legítima (`tests/lpk-legit`), en su proceso, pide el micrófono: el aviso lo dibuja el sistema | Después de «Permitir»: graba y aparece el punto naranja; la decisión queda en `permisos.json` |

### Ajustes → Privacidad

Una página en los grupos estilo macOS/TangoOS, como «Privacidad y seguridad»: lista cada permiso
con sus apps y un control Permitir / Preguntar / Denegar (`apps/settings/Privacy.jsx`). El
**Monitor** agrega su sección (ver las conexiones por app, ubicarlas en el mapa) por
`registerPrivacySection({ id, order, Component })` de `@lordos/sdk`, sin tocar la página.

![Ajustes → Privacidad](m8-privacidad-ajustes.png)

### Indicadores de cámara y micrófono

Un punto **verde** en la barra de menú mientras se usa la cámara y uno **naranja** para el
micrófono (temas `default` y `classic`); al tocarlo muestra qué app lo usa. Fuentes: el broker
(apps aisladas) y, para las apps Linux, `sdk/monitor.js` del Monitor (por PipeWire, fase 2) por
`globalThis.__lordos.monitorDevices`.

| | |
|---|---|
| ![aviso](m8-privacidad-prompt.png) | ![indicador](m8-privacidad-indicador-mic.png) |
| Aviso del sistema (archivos) | Punto naranja con la app grabando |

## Rendimiento (VM 122, 4 vCPU, CPU sin GPU)

| | En proceso (M7) | Proceso aislado (`--app`) |
|---|---|---|
| RSS | lo que pese su JS: comparte React, SDK y motor con la sesión (~54 MB la sesión completa) | **13,7 MB** por app (5 mediciones: 13,7–13,9 MB) |
| Arranque hasta cargar la app | evaluar `app.js` (milisegundos; no medido aparte) | **76–194 ms**, mediana 106 ms (de eso, ~60 ms son React + SDK + app) |
| `sandbox_apply()` | — | 0,89 ms |
| Cuadros del compositor con la app colgada (`kill -STOP`) | — | sin cambios: layout 0,56 → 0,54 ms, pintado 52,7 → 49,3 ms, mismos cuadros |

- El compositor nunca espera a una app ni al broker: el socket del broker y la conexión Wayland
  se atienden en el `poll` principal solo cuando hay datos. Con la app detenida, la ventana
  conserva su último cuadro y el resto del escritorio responde normal
  (![app detenida](m8-privacidad-app-detenida.png)).
- Las comprobaciones de permiso son O(1) (`perm_get`, tabla en memoria).
- El pintado alto (≈50 ms) es del camino por CPU de esta VM al mover el mouse con QMP; es igual
  con o sin apps aisladas.

## Archivos

| Ruta | Qué es |
|---|---|
| `server/src/app/appclient.c` | `lord-server --app <id>`: cliente Wayland, caja de arena, motor |
| `server/src/app/appspawn.c` | lanzar la app aislada con su broker (id validado) |
| `server/src/sec/brokersrv.c`, `broker.h` | broker, lado servidor |
| `server/src/ui/js_broker.c` | broker, lado app (`lord.fs/clipboard/broker` del proceso de la app) |
| `runtime/app-host.js` | el «app host» (React + SDK + cargador de `app.js`); aplica tema/idioma en vivo |
| `sdk/powerbox.jsx` | el diálogo de abrir/guardar (powerbox) y `fs.openFile`/`fs.saveFile` |
| `server/src/sec/sandbox.c` | Landlock + seccomp + red + no_new_privs + capacidades; `sandbox_apply_helper` |
| `server/src/sec/cgroup.c` | scopes por app y delegación del subárbol de la sesión |
| `server/src/sec/perms.c` | permisos por app (TCC), `permisos.json` |
| `server/src/ui/js_sec.c` | `lord.perms`, `lord.sign` y `lord.broker.resolve` |
| `sdk/privacy.js` | avisos, indicadores, `registerPrivacySection`, `requestPermission` |
| `server/src/remote/cgsup.c` | the root placement service on `/run/lordos/cgroup.sock` (child of `lord-sessiond`) |
| `server/src/sec/brokercam.c`, `broker_int.h` | the camera of isolated apps, server side (ring, ACK validation, powerbox saves, revoke) |
| `server/src/ui/js_brokercam.c` | `lord.media` (camera) inside the app process |
| `tests/lpk-legit` | also: camera, photo/video through the powerbox, «New window» |
| `apps/settings/Privacy.jsx` | Ajustes → Privacidad |
| `tests/lpk-evil`, `tests/lpk-legit` | apps de prueba (maliciosa y legítima) |
| `server/tests/sandbox_test.c` | pruebas de la caja de arena (app, red, ayudante) |

## Root-owned app scopes

The session's cgroup subtree used to be delegated to the user (`chown`), so a Linux app without a
sandbox, running as the user, could write its pid into another cgroup's `cgroup.procs` (another
app's scope, `server`, or a new cgroup it created) and leave its per-app firewall rules and its
accounting behind. `.lpk` apps never could (Landlock hides `/sys`).

Now the subtree stays **root-owned** and the scopes are created by root:

- `cgroup_delegate_session()` (`server/src/sec/cgroup.c`) creates `lordos-session-<uid>/server`
  and moves the session's `lord-server` there, but no longer chowns anything to the user
  (`cgroup_harden_session`). At startup, `cgroup_service_init()` hardens the subtrees left by an
  older `lord-sessiond` (sessions survive a restart).
- `lord-sessiond` forks a small root child (`server/src/remote/cgsup.c`) that listens on
  `/run/lordos/cgroup.sock` (`SOCK_SEQPACKET`, mode 0666; `SO_PEERCRED` decides). It serves one
  request: `enter <scope> <memory.high MB> <memory.max MB> <cpu %>`. It dies with its parent
  (`PR_SET_PDEATHSIG`) and is restarted if it ends.
- `cgroup_enter()` (used by the Linux app launcher, `sandbox_apply` and `sandbox_apply_helper`)
  asks that socket to place **the calling process itself**, right before `execve` (or before the
  sandbox). Without `lord-sessiond` (development) it falls back to the old self-migration.
- `cgroup_place_serve()` (root) only places a peer that:
  - is in its own session's `server` leaf (`/proc/<pid>/cgroup`): `lord-server` or a child it
    forked right before exec. A process already in an app scope can never ask;
  - runs a binary from `lord-server`'s directory (root-owned, not writable by others;
    `insecure_dev` relaxes it on development VMs). A binary replaced while running
    (`(deleted)`) is still accepted;
  - asks for a valid name: `app-lordos-<id>.scope` or `lordos-<helper>.scope`, never
    `lordos-session-…`.

  It creates the scope (root-owned, its control files too), writes the limits, and writes the
  peer's pid into `cgroup.procs`. The peer is blocked waiting for the reply, so its pid can't be
  reused meanwhile.

Result: in the session subtree every `cgroup.procs` (the session's, `server`'s and each scope's)
and every directory is root's. Moving a process between two cgroups needs write access to the
`cgroup.procs` of their common ancestor, so a user process can't leave its scope, enter another
one or create a cgroup.

Tested on VM 122, from inside a Linux app (`foot` in `app-lordos-aisla.scope`, with the firewall
restriction «no internet»):

```
$ echo $$ > server/cgroup.procs                 Permission denied
$ echo $$ > ./cgroup.procs        (session)     Permission denied
$ mkdir lordos-session-1000/escape              Permission denied
$ mkdir <own scope>/sub                         Permission denied
== my cgroup now: lordos-session-1000/app-lordos-aisla.scope
== net after: curl: (7) Failed to connect to example.com port 443   (the rule still applies)
```

![The scope escape attempts fail](m8-aislamiento-scope.png)

Also tested: the `.lpk` app, `lord-media` (`app-lordos-media.scope`) and Linux apps are placed
by `lord-sessiond`; `systemctl restart lordos` twice with the session alive (the new
`lord-sessiond` hardens the old subtree, starts a new placement service, and an app launched
afterwards still lands in its scope); `DelegateSubgroup=supervisor` in the unit.

**`systemd-run --user --scope`: closed (M8, the `user@` union).** A process can still ask the
user's systemd manager for a new scope or service. The manager has PID 1 do the move
(`AttachProcessesToUnit`), which runs as root and only checks that the process and the target unit
belong to the same UID; it does not look at the source cgroup. The process then runs in
`user@<uid>.service/app.slice/…`, outside `lordos.service`. This is closed by `lord-netperm`:
it attaches its cgroup programs to each LordOS session user's `user@<uid>.service` too, and holds a
process that escapes there to the strictest **union** of that user's running restricted apps'
restrictions (restriction-only, so never less restrictive than the app's own policy). Tested on
VM 122 with `systemd-run --user --scope`, `systemd-run --user` (service) and `setsid` — all denied,
normal processes unaffected, and the union never leaks between users. See `docs/FIREWALL.md`,
«Escape via `systemd-run --user`», and `docs/m8-firewall-escape-blocked.png`. `.lpk` apps couldn't
do it anyway (no D-Bus, no new sockets).

Not tested this round: a second user over fast user switching and the remote desktop (no test
password at hand). Their code path is the same `cgroup_delegate_session()` per uid.

## Camera for isolated apps

Consent, the green dot and revocation already existed. The frames now reach the app without the
app ever touching the device:

```
/dev/video10 ──open (after consent)──▶ lord-server ──fd──▶ lord-media (MK_CAMERA, sandbox_apply_helper)
                                                              │ decodes into MM_RING (3 BGRA slots,
                                                              │ sealed memfd), MM_FRAME per frame
lord-server (sec/brokercam.c) ◀──────────────────────────────┘
   │ read-only reopen of the ring (/proc/self/fd/N, O_RDONLY) + "frame <sid> <id>" events
   ▼
lord-server --app (ui/js_brokercam.c) — maps it PROT_READ, draws it with <MediaView> (media/mview.c),
   answers "ack <sid> <id>"; lord-server validates it and only then sends MM_ACK to lord-media
```

- **Broker op `BOP_CAMERA`** (`sec/broker.h`): `list`, `open dev w h`, `ring sid` (→ the read-only
  fd), `ack sid id` and `size sid w h` (no reply), `photo sid mirror name`,
  `record sid mirror mic name`, `recstop sid`, `close sid`. Each one checks the `camera`
  permission again in `lord-server`.
- **The device:** `lord-server` validates `/dev/videoN`, opens it and passes it only to
  `lord-media`. The app process never sees it (Landlock hides `/dev` anyway).
- **The ring:** `lord-server` checks the memfd from `lord-media` (sealed against shrinking, big
  enough) and keeps it. The app gets a new **read-only** open file description, so it can only map
  it `PROT_READ` (verified: the mapping is `r--s` in `/proc/<pid>/maps`) and can't write a frame
  that another reader would see.
- **ACK validation** (`brokercam.c`): an ACK is forwarded to `lord-media` only if its generation
  is the current ring's, its slot exists and holds a frame `lord-server` forwarded and nobody
  acknowledged yet, and the app has tokens left (120 ACKs/s, burst 30). Anything else is dropped
  and counted. `lord-media` frees slots only on a valid ACK, so an app that never acknowledges, or
  acknowledges garbage, only stops its own preview. If the app's socket is full, `lord-server`
  acknowledges the frame itself so the ring never jams. After a resize the app may acknowledge a
  frame of the previous generation once: that's logged as `invalid camera ACK ignored (1)`.
- **Photo and video:** the app asks `photo`/`record` with a suggested name. `lord-server` opens
  the **system's save dialog** (the powerbox, reqids with the high bit set), opens the chosen
  file itself (`O_NOFOLLOW`, regular file only) and gives that fd to `lord-media` (`MM_PHOTO`,
  `MM_RECORD`). The app only gets the file's name back (event `photo`/`video`). Recording with
  sound also needs the `microphone` permission; the PipeWire stream is `lord-server`'s.
- **Revoke:** Settings → Privacy → Camera → Deny calls `brokersrv_revoke`, which sends `revoke`
  to the app, kills its `lord-media` (`SIGKILL`) and closes the source: the stream stops at once
  and the green dot goes off. The app gets `closed` (`revoked: true`) and blanks its view.
- **Same SDK:** an isolated app uses `media.cameras()`, `media.camera()`, `<MediaView>`,
  `media.photo()` and `media.cameraRecord()` as in the session; in the app process `lord.media`
  is the camera subset above (`js_brokercam.c`).

Tested on VM 122 with `v4l2loopback` (`/dev/video10`) fed by `ffmpeg` (`testsrc2` 1280×720 YUYV,
service `lordos-test-camfeed`) and `tests/lpk-legit`: live preview, `foto.jpg` (1280×720 JPEG)
and `video.mp4` (H.264, 4.5 s) saved through the powerbox, and revocation (the `lord-media`
process is gone within 0.5 s). While previewing, the app process uses ~1–2 % CPU and
`lord-media` ~9 %.

| | |
|---|---|
| ![camera preview inside the isolated app](m8-aislamiento-camara.png) | ![camera revoked](m8-aislamiento-camara-revocada.png) |
| The preview in `tests/lpk-legit` (its own process), with the green dot | After «Deny» in Settings: the stream stopped, the dot is gone |

Not tested: a hostile app sending forged or flooded ACKs (the JS API only acknowledges frames it
received; it would take native code inside the sandbox). The validation is in
`brokercam_handle` (`"ack"`).

## Several windows per isolated app

`server/src/app/appclient.c` keeps one `AppWin` per toplevel (surface, `xdg_surface`,
`xdg_toplevel`, decoration, its two `wl_shm` buffers, size, frame callback and the root node it
draws). The app host (`runtime/app-host.js`) registers the app in the SDK's window store of its
own process, so the app opens and closes windows with the usual SDK:

- `wm.open(appId, { new: true })` opens another window; `wm.close(id)`, `wm.setTitle(id, t)` and
  `useAppWindow()` work per window. The app host renders every window as a child of the React root
  and binds it to its surface with `lord.appWindows` (`open`, `attach`, `close`, `setTitle`).
- Each window is laid out and painted on its own surface from its root node (`layout_root`,
  `paint_root`), and the pointer is hit-tested in the window it is over (`wl_pointer.enter`).
  Keyboard focus follows `wl_keyboard.enter`; the app host raises that window (`wm.focus`).
- The frame's close button sends `xdg_toplevel.close` for that surface; the app host runs
  `wm.close(id)` (close guards still apply). Closing the last window ends the process.
- In the session, every surface is its own `linux#<surface>` window with the theme frame
  (`sdk/wayland.js`); the Dock groups all of them under the app's entry (`launcherIdOf`), the app's
  menus go to every window of the app, and «New window» in the Dock reaches the running process
  through the broker (`window` event) instead of launching a second process.

![two windows of one isolated app, the Dock menu lists both](m8-aislamiento-ventanas.png)

Tested on VM 122 with `tests/lpk-legit`: «New window» from the app and from the Dock menu, the Dock
menu listing every window, closing a window, focus and input in each one.

Also fixed while testing: the app's event loop now drains broker events that arrived during a
synchronous RPC (`js_broker_pending`), events are NUL-terminated before `JS_ParseJSON` (a
`revoke` could be dropped), and an isolated app no longer inherits `lord-server`'s blocked
`SIGTERM`/`SIGINT`.

## En pocas palabras

La mayoría de los firewalls piensan en puertos y direcciones. El firewall de LordOS piensa en **apps**: vos decidís cuáles de tus apps pueden usar la red, y cuánto. Y como está integrado al Monitor, podés ver qué está haciendo cada app antes de decidir.

[[shot:0]]

Imaginate un edificio con un guardia en la puerta de cada departamento en lugar de uno solo en la calle. Cuando una app intenta abrir una conexión, el guardia de su puerta revisa su lista en una fracción de microsegundo y la deja pasar o le contesta "no permitido". El guardia está dentro del kernel Linux, así que una app insistente no puede simplemente rodearlo.

[[diagram:path]]

## Qué ves y qué decidís

- **Monitor → Red** lista las apps con conexiones, cuánto mandó y recibió cada una, y un mapa del mundo con un arco hacia cada país con el que hablan. Cada app tiene un interruptor de **Regla**: si lo apagás, a la app se le niega la red al instante, con contadores de conexiones permitidas y denegadas que salen directo del kernel.
- **Ajustes → Privacidad → Red de las apps** tiene un modo y dos listas:
  - **Silencioso** (el predeterminado): lo que no tiene regla se permite y se registra, para que lo revises en el Monitor. **Apagado**: solo valen tus denegaciones explícitas y las restricciones.
  - **Apps confiables**: se permiten sin registrar. Una denegación explícita que pongas igual gana.
  - **Apps restringidas**: marcás lo que una app no puede hacer. Las restricciones de red son **sin internet** (solo LAN), **sin LAN**, **sin red** y **sin conexiones entrantes**.

[[shot:1]]

[[shot:2]]

## Cómo funciona

**1. Cada app tiene un nombre que el kernel puede ver.** Cuando abrís una app de Linux desde el lanzador de apps, `lord-server` le pide a root que la ubique en su propio cgroup, `app-lordos-<id>.scope`. Las apps `.lpk` instaladas reciben `app-lordos-lpk-<id>.scope`. Estos scopes los crea y los tiene root, así que una app no se puede mudar a otro.

**2. Un servicio root chico aplica las reglas.** `lord-netperm` carga siete programas eBPF en el cgroup del servicio de LordOS, que contiene todas las sesiones:

| Gancho del kernel | Qué revisa |
|---|---|
| `connect4`, `connect6` | TCP y UDP conectado |
| `sendmsg4`, `sendmsg6` | UDP enviado con una dirección |
| `bind4`, `bind6` | entrantes: "sin conexiones entrantes" y reglas de entrada |
| `sock_create` | "sin red": la app ni siquiera puede crear un socket de internet |

Cada programa averigua a qué app pertenece el proceso que llama (por el cgroup ancestro al nivel del scope, así que los procesos hijos cuentan como la misma app), decide y devuelve permitir o denegar. Una app denegada recibe `EPERM`, "Operation not permitted".

[[shot:4]]

**3. Tus reglas se convierten en mapas del kernel.** Cambiás las reglas en la sesión; `lord-server` las guarda en `~/.config/lordos/firewall.json` y le manda a `lord-netperm` un único mensaje sin datos: `RELOAD`. Después el servicio lee el archivo por su cuenta, con validación estricta, y compila las reglas en mapas hash y de coincidencia por prefijo más largo. Las reglas nunca viajan por el socket.

**4. Nada corre por paquete.** Los controles corren cuando se abre una conexión o se reserva un puerto, no en cada paquete. Medido en una VM de prueba: unos **0,5 µs** por connect UDP, TCP dentro del ruido, y sin cambios en el rendimiento (`docs/FIREWALL.md`).

## ¿Funciona por dominio?

El motor sí, con cuidado sobre de dónde salen los nombres. Una conexión solo lleva una dirección IP, así que para aplicar una regla como "permitir solo `example.com`" el firewall tiene que saber a qué nombres corresponde una dirección. Eso lo aprende **solo del resolvedor del sistema**: se suscribe al monitor de systemd-resolved (o, sin él, lee las respuestas del puerto 53 de los servidores de `/etc/resolv.conf`). Nunca confía en respuestas DNS que una app recibió en sus propios sockets, que una app maliciosa podría falsificar.

[[diagram:dns]]

En la VM de prueba, "denegar todo menos example.com" dejó pasar `https://example.com` y denegó `https://www.debian.org`, 20 de 20 veces incluso con la caché del resolvedor vacía. Una app que usa su propio DNS sobre HTTPS obtiene direcciones que el firewall nunca aprendió, así que bajo una regla así quedan denegadas.

Lo que **todavía no** está: una pantalla para escribir reglas por dominio. El motor de reglas y la API `lord.fw.addRule` soportan dominios, comodines, redes, puertos y protocolos, pero la interfaz hoy ofrece el interruptor por app y las listas de restricciones. Los interruptores por destino en el Monitor están planeados.

## Las preguntas del lector escéptico

**¿Se aplica también a las apps de Linux?** Sí. Es uno de sus puntos principales: Chromium, Firefox o cualquier app abierta desde el lanzador de apps tiene su propio scope y sus propias reglas. Las apps incluidas de LordOS y los programas iniciados desde la Terminal cuentan juntos como "LordOS".

**¿Puede una app escaparse de su scope?** Se probó. Una app de Linux no puede anotarse en otro cgroup, porque cada archivo `cgroup.procs` del árbol de la sesión es de root. Todavía puede pedirle al gestor systemd del usuario que arranque un proceso nuevo fuera del árbol (`systemd-run --user`). LordOS también cierra eso: `lord-netperm` engancha además sus programas al `user@<uid>.service` de cada usuario, y cualquier proceso ahí queda sujeto a la unión más estricta de las restricciones de las apps restringidas que ese usuario tenga corriendo. Probado con `systemd-run --user --scope`, un servicio transitorio y `setsid`: todos denegados.

[[shot:3]]

**¿Puedo permitir una conexión una sola vez, o que me pregunte?** El motor de reglas conoce tres duraciones (para siempre, hasta que la app se cierre, una sola vez) y un modo "preguntar", pero el aviso de preguntar todavía no está hecho: hoy "preguntar" se comporta como silencioso. Está diseñado como un aviso de reintento: la primera conexión se deniega, te aparece *Permitir una vez / Siempre / Denegar*, y el reintento de la app pasa.

**¿Qué pasa si el servicio se detiene?** Se cierran sus enganches y todo queda permitido. El escritorio no depende de él. En la ISO final el servicio estaba corriendo con sus siete programas enganchados.

**¿El servicio en sí es peligroso?** Se lo mantiene chico a propósito: no ejecuta programas, no recibe argumentos, no tiene sockets de internet propios (su unidad solo permite sockets Unix y de paquetes) y acepta un solo tipo de mensaje. Usa su propio cargador sobre la llamada al sistema `bpf()`, con los programas compilados dentro del binario; sin libbpf ni bcc.

## Límites honestos

- **ICMP** (ping) solo se bloquea con "sin red". **Las conexiones entrantes a un puerto que ya está abierto**, y `listen` sin `bind`, no se revisan: eso necesitaría un gancho por paquete.
- **La unión de `user@` es gruesa:** mientras corre una app restringida, cada proceso del gestor systemd de ese usuario queda sujeto a la unión de restricciones.
- **Un scope recién creado** se registra en microsegundos, pero una conexión hecha antes pasaría con las reglas predeterminadas.
- **Los nombres de host todavía no se muestran en el Monitor**, solo IPs y países. El Monitor nunca hace DNS inverso, así que ninguna dirección sale de la máquina solo porque la miraste.
- **Las restricciones que no son de red** (cámara, micrófono, pantalla, USB, Bluetooth, archivos, notificaciones) se pueden marcar en Ajustes pero **todavía no se aplican**: solo se aplican las cuatro restricciones de red. Para la cámara y el micrófono, usá los permisos por app.
- **Las apps de Linux sin caja de arena pueden editar el archivo de reglas**, porque corren como vos. Las apps `.lpk` instaladas no llegan a él.

## Leé el código

- [server/src/netperm/np.bpf.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/np.bpf.c): los siete programas eBPF de cgroup
- [server/src/netperm/netperm.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/netperm.c): el servicio root
- [server/src/netperm/npcompile.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/npcompile.c): las reglas convertidas en mapas del kernel
- [server/src/netperm/npdns.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/npdns.c): los nombres desde el resolvedor del sistema
- [server/src/fw/rules.c](https://github.com/lordbasex/LordOS/blob/main/server/src/fw/rules.c): reglas, precedencia y el lector estricto del archivo
- [server/src/ui/js_fw.c](https://github.com/lordbasex/LordOS/blob/main/server/src/ui/js_fw.c): `lord.fw`, la API de la sesión
- [apps/monitor/Firewall.jsx](https://github.com/lordbasex/LordOS/blob/main/apps/monitor/Firewall.jsx): la sección de Ajustes
- [server/tests/netperm_integration.sh](https://github.com/lordbasex/LordOS/blob/main/server/tests/netperm_integration.sh): la prueba con el kernel, 18 de 18
- [docs/FIREWALL.md](https://github.com/lordbasex/LordOS/blob/main/docs/FIREWALL.md) y [docs/MONITOR.md](https://github.com/lordbasex/LordOS/blob/main/docs/MONITOR.md)

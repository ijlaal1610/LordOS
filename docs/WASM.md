# WebAssembly en LordOS

LordOS trae su propio intérprete de WebAssembly, escrito en C y sin dependencias. No usa
wasm3, WAMR, V8 ni Wasmtime. Desde JavaScript se usa con el global estándar `WebAssembly`,
igual que en un navegador, así que una app nativa de LordOS (React) puede llevar módulos
compilados desde C, C++, Rust o Zig y llamarlos.

| Pieza | Archivo |
|---|---|
| Decodificador, validador y compilador a código interno | `server/src/wasm/wasm.c` |
| Intérprete, instancias, memorias, tablas y globales | `server/src/wasm/wasm_run.c` |
| API en C | `server/src/wasm/wasm.h` (internas: `wasm_int.h`) |
| Global `WebAssembly` en QuickJS | `server/src/ui/js_wasm.c` |
| Pruebas | `server/tests/wasm_test.c`, `server/tests/wasm_js_test.c`, `server/tests/wasm/` |

## Qué soporta

Cubre WebAssembly 2.0 sin SIMD:
- **El MVP completo:** enteros y flotantes con la semántica exacta de la especificación (NaN,
  −0, redondeos, trampas de división y conversión), memoria, tablas, `call_indirect`, globales,
  `start`, imports y exports.
- **Agregados que generan clang y rustc hoy:**
  - sign-extension;
  - non-trapping float-to-int (`trunc_sat`);
  - multi-value (funciones y bloques con varios resultados o con parámetros);
  - bulk memory (`memory.copy`, `memory.fill`, `memory.init`, `data.drop`, `table.init`,
    `elem.drop`, `table.copy`);
  - reference types (`funcref`/`externref` en tablas, globales y parámetros, `ref.null`,
    `ref.func`, `ref.is_null`, `table.get/set/size/grow/fill`, varias tablas, `select` con tipo);
  - import y export de globales mutables.

**Límites:**
- **Memoria:** una sola, de 32 bits (hasta 4 GiB). Se reserva con `mmap` sin ocupar RAM, así
  que solo se gastan las páginas que el módulo toca. Al crecer no cambia de dirección.
- **Pila:** 20.000 llamadas wasm anidadas (`wasm_max_call_depth`) y 8 MiB de valores. Cuando
  se pasa, da la trampa "call stack exhausted" y no rompe el servidor.
- **Errores:** todo error es una trampa o un error de validación, nunca un crash. Después de
  una trampa, la instancia se puede seguir usando.

**Lo que no soporta (queda pendiente):**
- SIMD (`v128`);
- hilos y memoria compartida;
- memory64 y varias memorias;
- excepciones (`try`/`throw`) y llamadas de cola;
- GC, `extended-const` y relaxed SIMD;
- WASI: un módulo que importa `wasi_snapshot_preview1` no se puede instanciar, salvo que la
  app le dé esas funciones desde JS.

## Cómo funciona

1. **Una pasada por función:** `wasm.c` valida los tipos con el algoritmo del apéndice de la
   especificación y, en la misma pasada, genera código interno. Cada instrucción es la
   dirección de su manejador (computed goto de GCC/clang) seguida de sus inmediatos, ya
   decodificados: en ejecución no se lee LEB128.
2. **Saltos resueltos de antemano:** el validador sabe la altura exacta de la pila en cada
   punto, así que cada `br`, `br_if` y `br_table` sale con su destino relativo y con cuántos
   valores mover. Si no hay nada que mover, el salto es una sola suma al puntero de
   instrucción. `i32.wrap_i64` y los `reinterpret` no generan código.
3. **El código muerto no se genera:** lo que sigue a `br`, `return` o `unreachable` se valida
   (la pila "polimórfica" de la especificación), pero no se compila.
4. **Pilas:**
   - una pila de valores de 64 bits y una de marcos, compartidas por todas las llamadas;
   - las llamadas wasm → wasm no usan la pila de C;
   - una función del host (un import de JS) puede volver a llamar a wasm.
5. **Referencias:**
   - módulos, instancias, memorias, tablas y globales llevan cuenta de referencias;
   - una tabla o global guarda las funciones de su propia instancia sin sumar referencias, lo
     que evita el ciclo instancia ↔ su tabla;
   - si alguien de afuera (JS, otra instancia) retiene la tabla, la tabla retiene a su
     instancia.

## Desde JavaScript (`WebAssembly`)

El global es el mismo que el de los navegadores:
- **Funciones:** `validate`, `compile` e `instantiate`. `instantiate` acepta bytes (y devuelve
  `{module, instance}`) o un `Module` (y devuelve la `Instance`), siempre con promesas.
- **Clases:** `Module` (con `exports`, `imports` y `customSections`), `Instance` (con
  `exports` congelado y sin prototipo), `Memory` (`buffer`, `grow`), `Table` (`length`, `get`,
  `set`, `grow`) y `Global` (`value`, `valueOf`).
- **Errores:** `CompileError`, `LinkError` y `RuntimeError` son subclases de `Error`. Cuando
  se agota la pila, el error es `RangeError`.

**Funciones exportadas:**
- son funciones de JS de verdad: `typeof` da `"function"` y funcionan `call`, `apply` y
  `bind`;
- la misma función wasm devuelve siempre el mismo objeto: `exports.f === tabla.get(0)`.

**Conversiones de valores:**
- i64 ↔ `BigInt`: pasarle un `Number` a un parámetro i64 da `TypeError`;
- f32: se redondea con `Math.fround`;
- `externref` acepta cualquier valor de JS;
- `funcref` acepta una función exportada de wasm o `null`;
- si una función tiene varios resultados, devuelve un arreglo.

**Imports y excepciones:**
- un import puede ser cualquier función de JS;
- si esa función tira una excepción, la misma excepción atraviesa wasm y le llega a quien
  llamó desde JS.

**Memoria y `Memory.buffer`:**
- `Memory.buffer` es un `ArrayBuffer` sobre la memoria real, sin copiar: lo que wasm escribe,
  JS lo lee enseguida;
- al crecer (desde wasm o con `grow`), el `ArrayBuffer` anterior queda desvinculado
  (`byteLength` 0) y `buffer` da uno nuevo, como pide la especificación;
- la memoria nunca se mueve, así que no puede quedar ningún puntero colgado.

### Ejemplo en una app de LordOS

`calc.c`:
```c
__attribute__((export_name("suma"))) int suma(int a, int b) { return a + b; }
```
```sh
clang --target=wasm32 -O2 -nostdlib -Wl,--no-entry -o calc.wasm calc.c
```

En la app (React):
```jsx
import { useEffect, useState } from 'react';
import { View, Text } from '@lordos/sdk';

export default function Calc() {
  const [r, setR] = useState(null);
  useEffect(() => {
    // bytes: lord.apps.packageFile('mi.app', 'calc.wasm') si el .wasm viaja en el paquete
    // de la app (ver APPS-NATIVAS.md), o fs.read(ruta, { binary: true }) desde la carpeta personal.
    const bytes = lord.apps.packageFile('mi.app', 'calc.wasm');
    WebAssembly.instantiate(bytes, {}).then(({ instance }) => setR(instance.exports.suma(2, 3)));
  }, []);
  return <View><Text>{r === null ? '…' : `2 + 3 = ${r}`}</Text></View>;
}
```

Con imports, por ejemplo una función que imprime desde C usando la memoria del módulo:
```js
let inst;
const imports = { env: { log: (ptr, len) => console.log(new TextDecoder().decode(new Uint8Array(inst.exports.memory.buffer, ptr, len))) } };
({ instance: inst } = await WebAssembly.instantiate(bytes, imports));
```

**Seguridad:**
- un módulo solo ve su propia memoria y los imports que le da la app;
- no tiene acceso al sistema de archivos, a la red ni a `lord.*` salvo que la app se los
  pase como funciones;
- toda dirección se comprueba contra el tamaño de la memoria antes de leer o escribir.

**Ciclos de memoria:** el recolector de QuickJS no ve las referencias que el intérprete
guarda hacia funciones de JS (imports) ni hacia valores `externref`. Un ciclo que pase por
ahí, como un import que usa la instancia que lo importa, queda vivo mientras viva el contexto
de JS. Al cambiar de tema o cerrar el servidor, `js_wasm_shutdown` corta esas referencias y se
libera todo; `build/wasm-js-test` lo comprueba con el `assert` de QuickJS, que aborta si queda
algún objeto vivo.

## API en C

```c
WasmError err;
WasmModule *m = wasm_module_new(bytes, len, &err);            // decodifica, valida y compila
WasmFunc *log = wasm_func_new_host(wasm_functype(ii, 2, NULL, 0), mi_log, datos, NULL);
WasmExtern imports[] = { { .kind = WASM_EXT_FUNC, .func = log } };
WasmInstance *inst = wasm_instantiate(m, imports, 1, &err);   // corre start
WasmExtern f = wasm_instance_export_find(inst, "suma");
WasmVal args[2] = { { .i32 = 2 }, { .i32 = 3 } }, res[1];
if (wasm_call(f.func, args, res, &err)) printf("trampa: %s\n", err.msg);
```

Los nombres de imports y exports pueden tener bytes 0: `wasm_export_name(m, i, &len)` y
`wasm_instance_export_find_len`.

## Pruebas

```sh
sh server/tests/wasm/build.sh        # demo.c → demo.wasm (clang) y api.wat → api.wasm (wabt)
make -C server build/wasm-test build/wasm-js-test
cd server && ./build/wasm-test && ./build/wasm-js-test
```

- **`wasm-test`:** corre el módulo compilado desde C (`tests/wasm/demo.c`, sin libc). Prueba
  recursión, i64, una criba con 2 MB de memoria, quicksort con `call_indirect`, CRC32,
  flotantes, un import del host que lee la memoria, `memory.grow`, bulk memory, la trampa
  de 7/0 y la recursión infinita (pila agotada). Al final comprueba que la instancia sigue
  andando: 16 de 16.
- **`wasm-js-test`:** corre `tests/wasm/api_test.js` en QuickJS con el global `WebAssembly`:
  38 de 38 comprobaciones. Al terminar libera el contexto y verifica que no quede nada vivo.

### Suite oficial de la especificación

Son los 90 archivos `.wast` de `test/core` de WebAssembly/spec en la etiqueta `wg-2.0` (sin la
carpeta `simd`). wabt los pasa a JSON y binarios, y `wasm-test spec` los corre:

```sh
git clone https://github.com/WebAssembly/spec && cd spec && git checkout wg-2.0
mkdir json && for f in test/core/*.wast; do wast2json $f -o json/$(basename $f .wast).json; done
.../server/build/wasm-test spec json/*.json
```

Resultado en la VM: **26.290 de 26.290 aserciones correctas, 0 omitidas**. Cubre:
- `assert_return` con NaN canónicos y aritméticos;
- `assert_trap`, `assert_exhaustion`, `assert_invalid` y `assert_malformed` (los binarios);
- `assert_unlinkable`, `assert_uninstantiable`, `register` y `linking`.

Hay 1 que difiere solo en el tipo de error y está contada entre esas 26.290:
`memory_init.wast:190`. `data.drop` sin sección `datacount` da "malformed", que es lo que pide
el formato binario. El intérprete de referencia la espera "invalid" porque lee el texto sin
pasar por el binario.

Las aserciones de texto (`module_type: text`) prueban el parser de wabt, no el nuestro, y no
se cuentan.

## Rendimiento (VM: Xeon Silver 4108 a 1,8 GHz, 1 núcleo de 4 vCPU)

| Prueba | wasm (LordOS) | Mismo código en JS (QuickJS) |
|---|---|---|
| `fib(30)` recursivo (1,6 millones de llamadas) | 66–90 ms | 322 ms |
| Criba de primos hasta 2.000.000 | 173–237 ms | 540 ms |
| 100.000 llamadas JS → wasm | 18 ms | — |

El rango depende de la carga de la VM, que tenía otras compilaciones corriendo.

Comparación con los motores de los navegadores: un intérprete es entre 3 y 10 veces más lento
que su compilador JIT, que LordOS no tiene. Aun así, wasm le gana entre 2 y 3,5 veces al mismo
código escrito en JS en QuickJS. Instanciar `demo.wasm` (2,5 KB, con 2 MB de memoria) no
ocupa RAM hasta que el módulo toca esa memoria.

Queda para más adelante juntar las secuencias frecuentes en una sola instrucción
(`local.get` + `i32.add`, comparación + `br_if`): da un 20–40% más en otros intérpretes.

## Paquetes en la VM

Solo para compilar las pruebas y correr la suite; LordOS no los necesita para funcionar.

| Paquete | Para qué |
|---|---|
| `clang`, `lld` | compilar `tests/wasm/demo.c` a wasm32 (`wasm-ld` está en `lld`) |
| `wabt` (1.0.36) | `wat2wasm` para `api.wat` y `wast2json` para la suite de la especificación |
| `git`, `ca-certificates` | clonar WebAssembly/spec (en `~/wasm-spec` de la VM) |

```sh
sudo apt-get install -y --no-install-recommends clang lld wabt git ca-certificates
```

// api_test.js — el global WebAssembly de LordOS (src/ui/js_wasm.c). Lo corre build/wasm-js-test.
let fails = 0, oks = 0;
const ok = (cond, what) => { if (cond) oks++; else { fails++; print('FALLA ' + what); } };
const throwsAs = (fn, cls, what) => { try { fn(); ok(false, what + ' (no tiró)'); } catch (e) { ok(e instanceof cls, what + ' (tiró ' + e + ')'); } };

const api = readFile('tests/wasm/api.wasm'), demo = readFile('tests/wasm/demo.wasm');
ok(WebAssembly.validate(api), 'validate api');
ok(!WebAssembly.validate(new Uint8Array([0, 97, 115, 109, 2, 0, 0, 0])), 'validate malo');
throwsAs(() => new WebAssembly.Module(new Uint8Array([1, 2, 3])), WebAssembly.CompileError, 'CompileError');
ok(new WebAssembly.CompileError('x') instanceof Error, 'CompileError extiende Error');

const m = new WebAssembly.Module(api);
ok(WebAssembly.Module.exports(m).length === 14, 'Module.exports');
ok(JSON.stringify(WebAssembly.Module.imports(m)[2]) === '{"module":"js","name":"g","kind":"global"}', 'Module.imports');
throwsAs(() => new WebAssembly.Instance(m, {}), TypeError, 'imports faltantes');
throwsAs(() => new WebAssembly.Instance(m, { js: { throws() {}, add3: 1, g: 0, mem: 0 } }), WebAssembly.LinkError, 'LinkError');

const mem = new WebAssembly.Memory({ initial: 1, maximum: 10 });
const g = new WebAssembly.Global({ value: 'i32', mutable: true }, 7);
const inst = new WebAssembly.Instance(m, { js: { throws() { throw new Error('desde JS'); }, add3: (x) => x + 3, g, mem } });
const e = inst.exports;
ok(Object.isFrozen(e) && Object.getPrototypeOf(e) === null, 'exports congelado y sin prototipo');
ok(typeof e.big === 'function' && e.big.length === 1, 'función exportada');
ok(e.big(5n) === 15n, 'i64 ↔ BigInt');
throwsAs(() => e.big(5), TypeError, 'i64 con Number');
ok(JSON.stringify(e.pair(1, 2)) === '[2,1]', 'multi-value');
const obj = { hola: 1 };
ok(e.ext(obj) === obj && e.ext(null) === null && e.ext(undefined) === undefined, 'externref');
try { e.callThrows(); ok(false, 'excepción de JS'); } catch (x) { ok(x.message === 'desde JS', 'la excepción de JS atraviesa wasm'); }
ok(e.useAdd(4) === 7, 'import de JS');
e.incG(); e.incG();
ok(g.value === 9, 'global mutable importada');
throwsAs(() => e.trap(), WebAssembly.RuntimeError, 'trampa → RuntimeError');
ok(e.answer instanceof WebAssembly.Global && e.answer.value === 42, 'global exportada');
throwsAs(() => { e.answer.value = 1; }, TypeError, 'global inmutable');
e.g64.value = 1n << 40n;
ok(e.g64.value === 1n << 40n, 'global i64');
ok(e.tbl.length === 2 && e.tbl.get(0) === e.f1 && e.tbl.get(1) === null, 'tabla e identidad de funciones');
e.tbl.set(1, e.f1);
ok(e.callTbl(1) === 11, 'call_indirect por una entrada puesta desde JS');
throwsAs(() => e.callTbl(5), WebAssembly.RuntimeError, 'call_indirect fuera de la tabla');
throwsAs(() => e.tbl.set(0, () => 1), TypeError, 'tabla funcref con función de JS');
ok(e.tbl.grow(3) === 2 && e.tbl.length === 5, 'Table.grow');

// memoria: el buffer es la memoria real; al crecer se desvincula
const b1 = mem.buffer;
ok(b1 === mem.buffer && b1.byteLength === 65536, 'Memory.buffer');
e.store(100, 77);
ok(new Uint8Array(b1)[100] === 77, 'wasm escribe y JS lee sin copiar');
ok(e.grow(2) === 1, 'memory.grow desde wasm');
ok(b1.byteLength === 0, 'el buffer viejo quedó desvinculado');
ok(mem.buffer.byteLength === 3 * 65536 && new Uint8Array(mem.buffer)[100] === 77, 'buffer nuevo con los datos');
ok(mem.grow(1) === 3 && mem.buffer.byteLength === 4 * 65536, 'Memory.grow desde JS');
throwsAs(() => mem.grow(100), RangeError, 'Memory.grow más allá del máximo');

// el módulo de C (demo.c), con promesas
let log = '';
let demoInst;
WebAssembly.instantiate(demo, { env: { log: (p, n) => { log = String.fromCharCode(...new Uint8Array(demoInst.exports.memory.buffer, p, n)); } } })
  .then(({ module, instance }) => {
    demoInst = instance;
    ok(module instanceof WebAssembly.Module && instance instanceof WebAssembly.Instance, 'instantiate(bytes)');
    ok(instance.exports.fib(20) === 6765, 'fib desde JS');
    ok(instance.exports.greet(0) === 13 && log === 'hola desde C!', 'import que lee la memoria');
    const t0 = Date.now();
    const p = instance.exports.primes(2000000);
    print('primes(2000000) = ' + p + ' en ' + (Date.now() - t0) + ' ms (desde JS)');
    return WebAssembly.instantiate(module, { env: { log() {} } });
  })
  .then((i2) => {
    ok(i2 instanceof WebAssembly.Instance, 'instantiate(Module)');
    return WebAssembly.compile(new Uint8Array([0, 1, 2]));
  })
  .then(() => ok(false, 'compile malo resolvió'), (err) => ok(err instanceof WebAssembly.CompileError, 'compile rechaza con CompileError'))
  .then(() => print((fails ? fails + ' FALLAS, ' : 'todo bien, ') + oks + ' comprobaciones'));

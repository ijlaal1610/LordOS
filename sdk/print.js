// print.js — impresión e impresoras (CUPS por IPP desde un cliente propio en C, lord.print) y
// escáneres eSCL de la red. Ver docs/IMPRESION.md.
//
// Para las apps:
//   print.open({ title, path })            imprimir un PDF (abre el diálogo del sistema)
//   print.open({ title, doc })             un PDF ya abierto con pdf.open (con sus anotaciones)
//   print.open({ title, text, mono })      texto (Editor, Notas): se arma un PDF en lord-doc
//   print.open({ title, image })           una imagen (Fotos, el escáner)
//     → promesa: { job, printer } | { saved: ruta } | null (cancelado)
//
// Lo de abajo lo usan Ajustes → Impresoras y el Escáner:
//   printers() → { printers: [{ name, info, location, model, state, message, accepting, color, media, sides, colorModes }], default }
//   jobs(), cancel(id), add(nombre, uri, info, lugar), remove(nombre), setDefault(nombre)
//   discover() → { printers: [{ name, uri, addr, port, model, uuid, color, duplex }], scanners: [{ name, addr, port, rs, model }] }
//   scanCaps(scanner), scan(scanner, ajustes) → { file, format, bytes }
//   submit(impresora, blob | rutaPdf, opciones) → { job }
import { createStore } from './store.js';

const has = () => typeof lord !== 'undefined' && typeof lord.print !== 'undefined';
const waiting = new Map();
let timer = null;

function pump() {
  let evs = null;
  try { evs = lord.print.poll(); } catch (e) { evs = null; }
  for (const e of evs || []) {
    const w = waiting.get(e.id);
    if (!w) continue;
    waiting.delete(e.id);
    if (e.ok) w.resolve(e.data);
    else { const err = new Error(e.data?.error || 'error'); err.code = e.data?.error; err.detail = e.data?.detail; w.reject(err); }
  }
  if (!waiting.size && timer) { clearInterval(timer); timer = null; }
}

function wait(id) {
  if (!(id > 0)) return Promise.reject(Object.assign(new Error('busy'), { code: 'busy' }));
  return new Promise((resolve, reject) => {
    waiting.set(id, { resolve, reject });
    if (!timer) timer = setInterval(pump, 50);
  });
}

const call = (fn) => { try { return wait(fn()); } catch (e) { return Promise.reject(e); } };

// Diálogo del sistema (lo dibuja <PrintDialog />, que monta startShell)
export const printRequest = createStore(null);    // { source, resolve }

export const print = {
  available: () => has(),
  hasCups: () => has() && lord.print.hasCups(),
  printers: () => call(() => lord.print.printers()),
  jobs: () => call(() => lord.print.jobs()),
  cancel: (id) => call(() => lord.print.cancel(id)),
  add: (name, uri, info = '', location = '') => call(() => lord.print.add(name, uri, info, location)),
  remove: (name) => call(() => lord.print.remove(name)),
  setDefault: (name) => call(() => lord.print.setDefault(name)),
  discover: () => call(() => lord.print.discover()),
  scanCaps: (s) => call(() => lord.print.scanCaps(s.addr, s.port, s.rs || 'eSCL')),
  scan: (s, settings) => call(() => lord.print.scan(s.addr, s.port, s.rs || 'eSCL', JSON.stringify(settings))),
  submit: (printer, source, options = {}) => call(() => lord.print.submit(printer, source, JSON.stringify(options))),
  // Abre el diálogo. Si ya hay uno abierto, el nuevo espera su turno (devuelve null).
  open(source) {
    if (printRequest.get()) return Promise.resolve(null);
    return new Promise((resolve) => printRequest.set({ source, resolve }));
  },
};

// Nombre de CUPS a partir de un nombre libre: "Oficina (HP)" → "Oficina_HP"
export const queueName = (s) => String(s).normalize('NFD').replace(/[̀-ͯ]/g, '').replace(/[^A-Za-z0-9_-]+/g, '_').replace(/^_+|_+$/g, '').slice(0, 60) || 'Impresora';

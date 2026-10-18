// doc.js — base de los documentos: habla con lord.doc (el ayudante encerrado lord-doc, ver
// docs/DOCUMENTOS.md). Todo es asincrónico: los pedidos devuelven promesas y un temporizador lee
// los resultados (lord.doc.poll) solo mientras hay algo pendiente, así el escritorio nunca espera.
//
//   docAvailable()                         ¿está el ayudante?
//   <DocImage image={id} style />          muestra una imagen devuelta (página, muestra de fuente)
//   releaseImage(id)
//   fonts: fontInfo(ruta), fontSample(ruta, { lines, width, fg, bg }), fontGrid(ruta, { cps, cell, cols, fg })
//   imágenes: decodeImage(ruta, { maxw, maxh }), convertImage(ruta, salida, { format })
//   PDF para imprimir: makeTextPdf({ text, title, mono, paper }), makeImagePdf(ruta, { paper }) → { blob, size }
//   blobs: saveBlob(blob, ruta), releaseBlob(blob)
import { createElement as h, useEffect, useRef } from 'react';
import { Image } from '@lordos/runtime';
import { settings } from './settings.js';

const has = () => typeof lord !== 'undefined' && typeof lord.doc !== 'undefined';
export const docAvailable = () => has() && lord.doc.available();

const waiting = new Map();        // `${sid}:${req}` → { resolve, reject, onPart, onProgress }
let timer = null;

function dispatch(e) {
  if (e.type === 'ready') return;
  if (e.type === 'crash') {
    // los pedidos de esa sesión ya llegan como "err" desde el servidor; avisar a quien escuche
    for (const fn of crashListeners.get(e.sid) || []) fn(e.data);
    return;
  }
  const k = `${e.sid}:${e.req}`;
  const w = waiting.get(k);
  if (!w) {
    if (e.type === 'img') lord.doc.release(e.image);        // nadie la espera: se libera
    else if (e.type === 'blob') lord.doc.blobRelease(e.blob);
    return;
  }
  if (e.type === 'part') { w.onPart?.(e.data); return; }
  if (e.type === 'prog') { w.onProgress?.(e.data); return; }
  waiting.delete(k);
  if (e.type === 'err') { const err = new Error(e.data?.error || 'error'); err.code = e.data?.error; err.detail = e.data?.detail; err.data = e.data; w.reject(err); return; }
  if (e.type === 'img') w.resolve({ image: e.image, w: e.w, h: e.h, data: e.data || {} });
  else if (e.type === 'blob') w.resolve({ blob: e.blob, size: e.size });
  else w.resolve(e.data || {});
}

function pump() {
  let evs = null;
  try { evs = lord.doc.poll(); } catch (e) { evs = null; }
  if (evs) for (const e of evs) { try { dispatch(e); } catch (err) { console.log(`doc: ${err.message}`); } }
  if (!waiting.size && !lord.doc.pending() && timer) { clearInterval(timer); timer = null; }
}

const crashListeners = new Map();
export function onSessionCrash(sid, fn) {
  if (!crashListeners.has(sid)) crashListeners.set(sid, new Set());
  crashListeners.get(sid).add(fn);
  return () => crashListeners.get(sid)?.delete(fn);
}

// Espera el resultado de un pedido ya hecho ({ sid, req } o los dos números).
export function waitFor(sid, req, { onPart, onProgress } = {}) {
  if (req == null || req < 0) return Promise.reject(Object.assign(new Error('unavailable'), { code: 'unavailable' }));
  return new Promise((resolve, reject) => {
    waiting.set(`${sid}:${req}`, { resolve, reject, onPart, onProgress });
    if (!timer) timer = setInterval(pump, 8);
  });
}

export function cancelRequest(sid, req) {
  if (lord.doc.cancel(sid, req)) {
    const w = waiting.get(`${sid}:${req}`);
    waiting.delete(`${sid}:${req}`);
    w?.reject(Object.assign(new Error('cancelled'), { code: 'cancelled' }));
  }
}

// Pedido a una sesión PDF/comprimido (op: open render select find outline pageinfo setfield annot undo blob sizes list).
export function request(sid, op, args, { prio = 0, timeout, onPart, onProgress } = {}) {
  let req;
  try { req = lord.doc.request(sid, op, args ? JSON.stringify(args) : '', prio, timeout ?? -1); } catch (e) { return Promise.reject(e); }
  const p = waitFor(sid, req, { onPart, onProgress });
  p.req = req;
  return p;
}

// Pedido a la sesión de utilidades (fuentes, imágenes, PDF para imprimir, miniaturas).
function util(op, args, path, out) {
  let r;
  const t0 = Date.now();
  try { r = lord.doc.util(op, args ? JSON.stringify(args) : '{}', path || undefined, out || undefined); } catch (e) { return Promise.reject(e); }
  const p = waitFor(r.sid, r.req);
  if (settings.get().docTrace) p.then(() => console.log(`doc: ${op} ${Date.now() - t0} ms`), () => {});
  return p;
}

export const releaseImage = (id) => { if (id && has()) lord.doc.release(id); };
export const releaseBlob = (id) => { if (id && has()) lord.doc.blobRelease(id); };
export const saveBlob = (blob, path) => lord.doc.blobSave(blob, path);

export const fontInfo = (path, index = 0) => util('fontinfo', { index }, path);
export const fontSample = (path, { lines, width = 600, fg = '#000000', bg = '#00000000', index = 0 }) => util('fontrender', { lines, width: Math.round(width), fg, bg, index }, path);
export const fontGrid = (path, { cps, cell = 40, cols = 16, fg = '#000000', index = 0 }) => util('fontgrid', { cps, cell, cols, fg, index }, path);
export const decodeImage = (path, { maxw = 1600, maxh = 1600 } = {}) => util('decode', { maxw, maxh }, path);
export const convertImage = (path, out, { format = 'png', quality = 90, paper, native, dpi } = {}) => util('convert', { format, quality, paper, native, dpi }, path, out);
export const makeTextPdf = ({ text, title = '', mono = false, paper = 'a4', size }) => util('maketext', { text, title, mono, paper, size });
export const makeImagePdf = (path, { paper = 'a4', fill = false } = {}) => util('makeimage', { paper, fill }, path);
// Prepara la sesión de utilidades (Archivos la llama al abrirse: la primera Vista rápida no espera)
let warmed = false;
export const warmDocs = () => { if (warmed || !docAvailable()) return; warmed = true; util('warm', {}).catch(() => { warmed = false; }); };
export const pdfThumb = (path, { width = 400, height = 600, invert = false } = {}) => util('pdfthumb', { width, height, invert }, path);

// <DocImage image={id} />: la imagen se copia al nodo (el id se puede liberar después).
export function DocImage({ image, style }) {
  const ref = useRef(null);
  useEffect(() => {
    if (!ref.current || !has()) return;
    lord.doc.bind(ref.current.id, image || 0);
  }, [image]);
  return h(Image, { ref, style });
}

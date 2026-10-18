// pdf.js — PDF por el ayudante encerrado (Poppler adentro de lord-doc; ver docs/DOCUMENTOS.md).
//
//   const doc = await pdf.open(ruta | { blob }, { quick })
//   doc.info           { pages, sizes: [[w, h]…] (puntos), title, author, outline, forms }
//   doc.render(página, ancho, { rot (0..3), invert, prio }) → { image, w, h }   (liberar con releaseImage)
//   doc.select(página, x1, y1, x2, y2, estilo)        → { text, rects }   (puntos, arriba-izquierda)
//   doc.find(texto, { case, whole }, (página, rects) => …) → { total }
//   doc.outline() → { items: [{ title, page, kids }] }
//   doc.pageInfo(página) → { links, fields, notes }
//   doc.setField(id, valor), doc.annot({ page, type, … }), doc.undo(página)
//   doc.save(ruta) (copia nueva), doc.blob() → { blob }, doc.sizes(), doc.close()
//   pdf.thumb(ruta, { width, height }) → miniatura de la página 1 sin abrir una sesión
import { request, waitFor, onSessionCrash, pdfThumb, cancelRequest } from './doc.js';

class PdfDoc {
  constructor(sid, info) { this.sid = sid; this.info = info; this.closed = false; }
  render(page, width, { rot = 0, invert = false, prio = 1 } = {}) {
    // rot en cuartos de vuelta (0..3); el ayudante recibe grados
    return request(this.sid, 'render', { page, width: Math.round(width), rot: ((rot % 4) + 4) % 4 * 90, invert }, { prio });
  }
  select(page, x1, y1, x2, y2, style = 0) { return request(this.sid, 'select', { page, x1, y1, x2, y2, style }, { prio: 5 }); }
  find(text, opts = {}, onPage) {
    return request(this.sid, 'find', { text, case: !!opts.case, whole: !!opts.whole }, { prio: 3, onPart: (d) => onPage?.(d.page, d.rects) });
  }
  outline() { return request(this.sid, 'outline', null, { prio: 4 }); }
  pageInfo(page) { return request(this.sid, 'pageinfo', { page }, { prio: 2 }); }
  setField(id, value) { return request(this.sid, 'setfield', { id, value }, { prio: 6 }); }
  annot(a) { return request(this.sid, 'annot', a, { prio: 6 }); }
  undo(page) { return request(this.sid, 'undo', { page }, { prio: 6 }); }
  sizes() { return request(this.sid, 'sizes', null, { prio: 0 }); }
  blob() { return request(this.sid, 'blob', null, { prio: 6 }); }
  save(path) {
    let req;
    try { req = lord.doc.savePdf(this.sid, path); } catch (e) { return Promise.reject(e); }
    return waitFor(this.sid, req);
  }
  cancel(p) { if (p?.req) cancelRequest(this.sid, p.req); }
  onCrash(fn) { return onSessionCrash(this.sid, fn); }
  close() { if (!this.closed) { this.closed = true; lord.doc.close(this.sid); } }
}

async function open(src, { quick = false, password = '' } = {}) {
  const sid = typeof src === 'object' && src && src.blob ? lord.doc.openBlob(src.blob) : lord.doc.openPdf(src);
  try {
    const info = await request(sid, 'open', { slot: 0, quick, password }, { prio: 9 });
    return new PdfDoc(sid, info);
  } catch (e) {
    lord.doc.close(sid);
    throw e;
  }
}

export const pdf = { open, thumb: pdfThumb };

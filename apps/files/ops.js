// ops.js — lo que comparten las ventanas de Archivos: portapapeles de archivos, tareas de
// copia en curso, etiquetas de colores y abrir archivos con la app que corresponde.
import { createStore, fs, wm, launchApp, archive } from '@lordos/sdk';
import { kindOf, mimeOf, appOpens, isTextual, isImage } from './kinds.js';

// Cambia cada vez que algo cambió en disco (copia terminada, papelera…): las vistas se recargan.
export const fsVersion = createStore(0);
export const bump = () => fsVersion.set((v) => v + 1);

// ---- portapapeles de archivos (aparte del de texto) ----
export const fileClipboard = createStore(null);          // { mode: 'copy' | 'cut', paths: [] }
export const copyFiles = (paths) => fileClipboard.set({ mode: 'copy', paths });
export const cutFiles = (paths) => fileClipboard.set({ mode: 'cut', paths });

// Nombre libre en una carpeta: "foto.png" → "foto 2.png", "foto 3.png"…
export function freeName(dir, name, existing) {
  const taken = existing || new Set(lord.fs.list(dir).map((e) => e.name));
  if (!taken.has(name)) return name;
  const m = /^(.*?)(\.[^.]+)?$/.exec(name);
  const stem = m[1].replace(/ \d+$/, ''), ext = m[2] || '';
  for (let i = 2; i < 10000; i++) {
    const n = `${stem} ${i}${ext}`;
    if (!taken.has(n)) return n;
  }
  return `${stem} ${Date.now()}${ext}`;
}

// ---- tareas de copia / movimiento (hilos del servidor) ----
export const jobState = createStore([]);                  // [{ id, src, dst, move, done, bytes, total, … }]
let polling = null;
function poll() {
  const list = fs.jobs();
  const done = list.filter((j) => j.done);
  for (const j of done) {
    fs.forgetJob(j.id);
    if (j.failed) jobErrors.set((e) => [...e, `${fs.basename(j.src)}: ${j.error}`].slice(-5));
  }
  if (done.length) bump();
  const running = list.filter((j) => !j.done);
  jobState.set(running);
  if (!running.length && polling) { clearInterval(polling); polling = null; }
}
export const jobErrors = createStore([]);
function watchJobs() { if (!polling) polling = setInterval(poll, 250); poll(); }

// Copiar o mover varias rutas a una carpeta. Copiar en el mismo lugar = duplicar con nombre libre.
export function transfer(paths, destDir, move) {
  const existing = new Set(lord.fs.list(destDir).map((e) => e.name));
  const errors = [];
  for (const p of paths) {
    const name = fs.basename(p);
    if (move && fs.parent(p) === destDir) continue;                       // ya está ahí
    if (move && (destDir === p || destDir.startsWith(`${p}/`))) { errors.push(name); continue; }
    const target = freeName(destDir, name, existing);
    existing.add(target);
    try {
      if (move) fs.move(p, `${destDir}/${target}`);
      else fs.copy(p, `${destDir}/${target}`);
    } catch (e) { errors.push(`${name}: ${e.message}`); }
  }
  watchJobs();
  if (errors.length) jobErrors.set((e) => [...e, ...errors].slice(-5));
  return errors;
}

export function paste(destDir) {
  const c = fileClipboard.get();
  if (!c || !c.paths.length) return;
  transfer(c.paths, destDir, c.mode === 'cut');
  if (c.mode === 'cut') fileClipboard.set(null);
}

// ---- etiquetas (~/.local/share/lordos/tags.json: ruta → [colores]) ----
const TAGS_FILE = '.local/share/lordos/tags.json';
export const tagStore = createStore((() => {
  try { return JSON.parse(fs.read(TAGS_FILE)); } catch (e) { return {}; }
})());
export const tagsOf = (path) => tagStore.get()[path] || [];
export function toggleTag(paths, color) {
  const all = { ...tagStore.get() };
  const on = paths.every((p) => (all[p] || []).includes(color));
  for (const p of paths) {
    const cur = new Set(all[p] || []);
    if (on) cur.delete(color); else cur.add(color);
    if (cur.size) all[p] = [...cur]; else delete all[p];
  }
  tagStore.set(all);
  try { fs.mkdir('.local/share/lordos'); fs.write(TAGS_FILE, JSON.stringify(all, null, 1)); } catch (e) { /* sin disco */ }
}
// Mover o renombrar conserva las etiquetas.
export function moveTags(from, to) {
  const all = tagStore.get();
  if (!all[from]) return;
  const next = { ...all, [to]: all[from] };
  delete next[from];
  tagStore.set(next);
  try { fs.write(TAGS_FILE, JSON.stringify(next, null, 1)); } catch (e) { /* sin disco */ }
}

// ---- abrir ----

// Apps que abren una entrada: [{ id, label, run }] (la primera es la de siempre).
export function openersFor(e, linuxApps, t) {
  const out = [];
  const k = kindOf(e);
  if (k === 'lpk' || k === 'deb') out.push({ id: 'store', label: t('apps.store'), run: () => wm.open('store', { args: { install: e.path } }) });
  // documentos (M9): PDF en el Visor de PDF y fuentes en Tipografías
  if (k === 'pdf') out.push({ id: 'pdf', label: t('apps.pdf'), run: () => wm.open('pdf', { new: true, args: { path: e.path } }) });
  if (/\.(ttf|otf|ttc|otc)$/i.test(e.name)) out.push({ id: 'fonts', label: t('apps.fonts'), run: () => wm.open('fonts', { args: { path: e.path } }) });
  // medios (M9): imágenes en Fotos; audio y video en el Reproductor (son los de siempre)
  if (k === 'image') out.push({ id: 'photos', label: t('apps.photos'), run: () => wm.open('photos', { new: true, args: { path: e.path } }) });
  if (k === 'audio' || k === 'video') out.push({ id: 'player', label: t('apps.player'), run: () => wm.open('player', { args: { path: e.path } }) });
  if (isTextual(e) || k === 'other') out.push({ id: 'editor', label: t('apps.editor'), run: () => wm.open('editor', { args: { path: e.path } }) });
  const mime = mimeOf(e);
  for (const a of linuxApps) {
    if (a.kind === 'linux' && appOpens(a.mimeTypes, mime)) out.push({ id: a.id, label: a.name, run: () => lord.apps.launchWith(a.desktopId, e.path) });
  }
  return out;
}

export function openEntry(e, ctx) {
  if (e.app) { launchApp(e.id); return; }
  // comprimidos (M9): se miran por dentro como una carpeta; lo de adentro se extrae a la caché al abrirlo
  if (e.arc) { if (e.dir) ctx.navigate({ type: 'archive', path: e.arc.file, inner: e.arc.inner }); else ctx.openArc(e); return; }
  if (e.dir) { ctx.navigate({ type: 'dir', path: e.path }); return; }
  if (!e.trash && archive.isArchive(e.name)) { ctx.navigate({ type: 'archive', path: e.path, inner: '' }); return; }
  const list = openersFor(e, ctx.linuxApps, ctx.t);
  if (isImage(e) && !list.some((o) => o.id !== 'editor')) { ctx.quickLook(e); return; }    // sin visor: Vista rápida
  if (list.length) list[0].run();
  else ctx.quickLook(e);
}

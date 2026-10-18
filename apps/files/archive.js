// archive.js — los comprimidos en Archivos: mirarlos por dentro como una carpeta, abrir o ver un
// archivo de adentro (se extrae solo ese a la caché), «Extraer aquí» y «Comprimir». El trabajo lo
// hace el ayudante encerrado lord-doc (libarchive), que nunca escribe fuera de la carpeta de
// destino (ver docs/DOCUMENTOS.md).
//   lugar: { type: 'archive', path: '/…/fotos.zip', inner: 'carpeta/sub' }
import { archive, createStore, fs } from '@lordos/sdk';
import { bump, freeName } from './ops.js';

const lists = new Map();            // ruta → { mtime, state: 'loading' | 'ok' | 'error', dirs, info, error }
export const archiveVersion = createStore(0);
const ERRS = ['invalid', 'disk-full', 'too-big', 'too-many', 'corrupt', 'write'];

export const isArchiveFile = (e) => !e.dir && !e.app && !e.trash && archive.isArchive(e.name);

function load(path) {
  let st;
  try { st = fs.stat(path); } catch (e) { return { state: 'error', error: e.message }; }
  const cur = lists.get(path);
  if (cur && cur.mtime === st.mtime) return cur;
  const entry = { mtime: st.mtime, state: 'loading' };
  lists.set(path, entry);
  archive.list(path).then((r) => {
    entry.state = 'ok';
    entry.info = r;
    entry.dirs = archive.tree(r.entries);
    archiveVersion.set((v) => v + 1);
  }).catch((e) => {
    entry.state = 'error';
    entry.error = e.code || e.message;
    archiveVersion.set((v) => v + 1);
  });
  return entry;
}

// Entradas del lugar (sincrónico: mientras carga devuelve [] y después avisa con archiveVersion).
export function archiveEntries(loc) {
  const a = load(loc.path);
  if (a.state === 'error') throw new Error(a.error === 'invalid' ? 'archive-invalid' : a.error);
  if (a.state !== 'ok') return [];
  const list = a.dirs.get(loc.inner || '') || [];
  return list.map((x) => ({ name: x.name, path: `archive:${loc.path}::${x.path}`, dir: x.dir, size: x.size, mtime: x.mtime, arc: { file: loc.path, inner: x.path }, enc: x.enc }));
}
export const archiveState = (path) => lists.get(path) || null;

// carpeta de la caché para lo extraído de este comprimido (una por ruta y fecha)
function cacheDir(file) {
  let h = 2166136261;
  const key = `${file}|${lists.get(file)?.mtime || 0}`;
  for (let i = 0; i < key.length; i++) { h ^= key.charCodeAt(i); h = Math.imul(h, 16777619) >>> 0; }
  for (const p of ['.cache', '.cache/lordos', '.cache/lordos/archives']) { try { fs.mkdir(p); } catch (e) { /* ya está */ } }
  const dir = `${fs.home()}/.cache/lordos/archives/${h.toString(16)}`;
  try { fs.mkdir(dir); } catch (e) { /* ya está */ }
  return dir;
}

// Extrae una entrada a la caché y devuelve su ruta real (para abrirla o verla).
const extracted = new Map();
export async function extractEntry(e) {
  const dir = cacheDir(e.arc.file);
  const real = `${dir}/${e.arc.inner}`;
  if (extracted.get(real)) return real;
  try { fs.stat(real); extracted.set(real, true); return real; } catch (err) { /* hay que extraerla */ }
  await archive.extract(e.arc.file, dir, { only: [e.arc.inner], existing: true });
  extracted.set(real, true);
  return real;
}

// «Extraer aquí»: carpeta nueva con el nombre del comprimido, al lado.
export function extractHere(e, report, t) {
  const dir = fs.parent(e.path);
  const name = freeName(dir, archive.stem(e.name));
  report(t('docs.extracting', { name: e.name }));
  return archive.extract(e.path, `${dir}/${name}`, {
    onProgress: (p) => report(t('docs.extractProgress', { name: e.name, files: p.files, pct: p.size ? Math.round(100 * p.pos / p.size) : 0 })),
  }).then((r) => {
    bump();
    report(r.skipped ? t('docs.extractedSkipped', { name, count: r.files, skipped: r.skipped }) : t('docs.extracted', { name, count: r.files }));
    return { dir: `${dir}/${name}`, ...r };
  }).catch((err) => {
    bump();
    report(t('docs.extractError', { name: e.name, error: ERRS.includes(err.code) ? t(`docs.err.${err.code}`) : err.code || err.message }));
  });
}

// «Comprimir»: un .zip al lado (el nombre del elemento, o «Archivo.zip» si son varios).
export function compress(paths, dir, report, t) {
  if (!paths.length) return Promise.resolve(null);
  const base = paths.length === 1 ? fs.basename(paths[0]) : t('docs.archiveName');
  const name = freeName(dir, `${base}.zip`);
  report(t('docs.compressing', { name }));
  return archive.create(paths, `${dir}/${name}`, {
    onProgress: (p) => report(t('docs.compressProgress', { name, files: p.files })),
  }).then((r) => { bump(); report(t('docs.compressed', { name, count: r.files })); return name; })
    .catch((err) => { bump(); report(t('docs.compressError', { name, error: err.code || err.message })); });
}

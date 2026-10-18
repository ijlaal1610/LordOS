// fs.js — archivos de la carpeta personal y de los discos montados del usuario (/media/$USER,
// /run/media/$USER); el servidor rechaza rutas de afuera. Escribir es atómico y no puede tocar
// ~/.local/share/applications, ~/.config/autostart, ~/.local/share/lordos/apps ni
// ~/.config/lordos/settings.json (ver server/src/ui/js_sys.c y server/src/fsops.c).
import { useEffect, useState } from 'react';

export const home = () => lord.fs.home();

export function list(path, { hidden = false } = {}) {
  const entries = lord.fs.list(path).filter((e) => hidden || !e.name.startsWith('.'));
  return entries.sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
}

export const join = (...parts) => parts.filter(Boolean).join('/').replace(/\/+/g, '/');
export const parent = (path) => path.split('/').slice(0, -1).join('/');
export const basename = (path) => String(path).split('/').filter(Boolean).pop() || '/';

// Rutas relativas a la carpeta personal: '' = inicio.
export function useDirectory(path, options) {
  const [state, setState] = useState({ entries: [], error: null });
  const [version, setVersion] = useState(0);
  useEffect(() => {
    try { setState({ entries: list(path || '.', options), error: null }); }
    catch (e) { setState({ entries: [], error: e.message }); }
  }, [path, version, options?.hidden]);
  return { ...state, reload: () => setVersion((v) => v + 1) };
}

// Contenido de un archivo (máx. 8 MB): texto UTF-8 o, con { binary: true }, un ArrayBuffer.
export const read = (path, { binary = false } = {}) => lord.fs.read(path, binary ? 'binary' : 'text');

// Crea o reemplaza un archivo (texto o ArrayBuffer, hasta 32 MB). Conserva los permisos.
export const write = (path, data) => lord.fs.write(path, data);
export const mkdir = (path) => lord.fs.mkdir(path);
// Mover o renombrar en el mismo disco (no pisa). Entre discos, move() con una tarea.
export const rename = (from, to) => lord.fs.rename(from, to);

// Copiar y mover en segundo plano (un hilo del servidor): devuelven el id de la tarea; el
// progreso se ve con jobs() → [{ id, src, dst, move, done, failed, error, bytes, total, files }].
// Recursivo, conserva permisos y fechas, no pisa y copia los enlaces como enlaces.
export const copy = (from, to) => lord.fs.copy(from, to, false);
export const move = (from, to) => lord.fs.copy(from, to, true);
export const jobs = () => lord.fs.jobs();
export const cancelJob = (id) => lord.fs.jobCancel(id);
export const forgetJob = (id) => lord.fs.jobForget(id);

// Papelera (freedesktop: ~/.local/share/Trash, y $montaje/.Trash-$uid en los discos).
// Borrar de verdad solo se puede adentro de la Papelera.
export const trash = (path) => lord.fs.trash(path);
export const trashList = () => lord.fs.trashList();          // [{ id, name, original, deleted, dir, size }]
export const restore = (id) => lord.fs.restore(id);
export const removeFromTrash = (id) => lord.fs.removeFromTrash(id);
export const emptyTrash = () => lord.fs.emptyTrash();

// Información: { name, path, dir, link, target, size, mode, user, group, mtime, atime, ctime, btime, writable }
export const stat = (path) => lord.fs.stat(path);
export const du = (path, ms) => lord.fs.du(path, ms);          // { size, files, dirs, complete } (acotado)
export const free = (path) => lord.fs.free(path);              // { total, free } del disco
export const search = (dir, text, opts) => lord.fs.search(dir, text, opts);   // por nombre, sin ocultos
export const recent = (limit = 60, days = 30) => lord.fs.recent(limit, days);
export const volumes = () => lord.fs.volumes();                // [{ path, name, fs, total, free }]

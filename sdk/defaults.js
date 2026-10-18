// defaults.js — default applications (Settings → Default Apps, docs/SETTINGS.md). They are kept
// in ~/.config/mimeapps.list (freedesktop spec, so GNOME/KDE apps and xdg-open agree) through
// lord.mime; the terminal goes in ~/.config/xdg-terminals.list. LordOS's own apps are written as
// "lordos-<app>.desktop".
//
//   DEFAULT_CATEGORIES                  [{ id, mimes, natives }]
//   defaultFor(categoryId)              'lordos-photos.desktop' | 'org.gnome.Loupe.desktop' | null
//   setDefaultFor(categoryId, id)       writes every MIME type of the category
//   candidatesFor(categoryId, linux)    [{ id, name, Icon, native }] that can be chosen
//   defaultOpenerId(mime)               the default for one type (Files: "Open With", double click)
import { getApp } from './windows.js';
import { appName } from './i18n.js';

const NATIVE = (id) => `lordos-${id}.desktop`;
export const nativeOf = (desktopId) => (desktopId && /^lordos-[a-z0-9_-]+\.desktop$/.test(desktopId) ? desktopId.slice(7, -8) : null);

// First MIME type is the one read back to show the current choice.
export const DEFAULT_CATEGORIES = [
  { id: 'browser', mimes: ['x-scheme-handler/https', 'x-scheme-handler/http', 'text/html', 'application/xhtml+xml'], natives: [] },
  { id: 'mail', mimes: ['x-scheme-handler/mailto', 'message/rfc822'], natives: ['mail'] },
  { id: 'music', mimes: ['audio/mpeg', 'audio/flac', 'audio/ogg', 'audio/x-vorbis+ogg', 'audio/x-opus+ogg', 'audio/mp4', 'audio/aac', 'audio/x-wav', 'audio/x-m4a'], natives: ['player'] },
  { id: 'video', mimes: ['video/mp4', 'video/x-matroska', 'video/webm', 'video/quicktime', 'video/x-msvideo', 'video/mpeg', 'video/ogg'], natives: ['player'] },
  { id: 'images', mimes: ['image/png', 'image/jpeg', 'image/gif', 'image/webp', 'image/bmp', 'image/tiff', 'image/svg+xml', 'image/heic'], natives: ['photos'] },
  { id: 'pdf', mimes: ['application/pdf'], natives: ['pdf'] },
  { id: 'text', mimes: ['text/plain', 'text/markdown', 'text/x-log'], natives: ['editor'] },
  { id: 'terminal', mimes: [], natives: ['terminal'], terminal: true },
];

const cat = (id) => DEFAULT_CATEGORIES.find((c) => c.id === id);
const api = () => typeof lord !== 'undefined' && lord.mime;

export function defaultFor(id) {
  const c = cat(id);
  if (!c || !api()) return null;
  if (c.terminal) return lord.mime.terminal() || NATIVE('terminal');
  for (const m of c.mimes) { const d = lord.mime.get(m); if (d) return d; }
  return c.natives.find((n) => getApp(n)) ? NATIVE(c.natives.find((n) => getApp(n))) : null;
}

export function setDefaultFor(id, desktopId) {
  const c = cat(id);
  if (!c || !api() || !desktopId) return false;
  if (c.terminal) return lord.mime.setTerminal(desktopId);
  let ok = true;
  for (const m of c.mimes) ok = lord.mime.set(m, desktopId) && ok;
  return ok;
}

// Does a Linux app's MimeType list cover the category? (its main type, or the whole family)
function covers(mimeTypes, c) {
  if (!mimeTypes) return false;
  const list = mimeTypes.split(';').filter(Boolean);
  const main = c.mimes[0], top = main.split('/')[0];
  return list.includes(main) || list.includes(`${top}/*`) || (c.id === 'browser' && list.includes('text/html') && list.includes('x-scheme-handler/http'));
}

export function candidatesFor(id, linuxApps = []) {
  const c = cat(id);
  if (!c) return [];
  const out = c.natives.map((n) => getApp(n)).filter(Boolean).map((a) => ({ id: NATIVE(a.id), name: appName(a), Icon: a.Icon, native: true }));
  for (const a of linuxApps) {
    if (a.kind !== 'linux') continue;
    const ok = c.terminal ? /(^|;)TerminalEmulator(;|$)/.test(a.categories || '') : covers(a.mimeTypes, c);
    if (ok) out.push({ id: `${a.desktopId}.desktop`, name: a.name, Icon: a.Icon, native: false });
  }
  return out;
}

export function defaultOpenerId(mime) {
  if (!api() || !mime) return null;
  const d = lord.mime.get(mime);
  if (d) return d;
  const c = DEFAULT_CATEGORIES.find((x) => x.mimes.includes(mime) || (x.mimes[0] && mime.startsWith(`${x.mimes[0].split('/')[0]}/`) && x.id !== 'browser' && x.id !== 'mail'));
  return c ? defaultFor(c.id) : null;
}

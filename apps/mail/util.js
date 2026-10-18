// util.js — small helpers for Mail: dates in the list, folder names, color categories, opening
// an attachment the way Files would.
import { t, formatDate, formatTime, wm, getApp, fs, defaultOpenerId, nativeOf, calendarEvents, contacts, addressBooks } from '@lordos/sdk';

export const CATEGORY_COLORS = ['#ff453a', '#ff9f0a', '#ffd60a', '#30d158', '#0a84ff', '#bf5af2'];
// labels are built at render time (they follow the language)
export const categoryName = (i, cfg) => cfg?.categories?.[i - 1]?.name || t(`mail.cat.${i}`);

export function listDate(ms) {
  const d = new Date(ms), now = new Date();
  if (d.toDateString() === now.toDateString()) return formatTime(d);
  const y = new Date(now); y.setDate(now.getDate() - 1);
  if (d.toDateString() === y.toDateString()) return t('mail.yesterday');
  if (now - d < 6 * 86400000) return formatDate(d, { weekday: 'short' });
  return formatDate(d, { day: 'numeric', month: 'short', ...(d.getFullYear() !== now.getFullYear() ? { year: 'numeric' } : {}) });
}

export const fullDate = (ms) => `${formatDate(new Date(ms), { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' })} ${formatTime(new Date(ms))}`;

const SPECIAL_ICONS = { inbox: '📥', drafts: '📝', sent: '📤', archive: '🗄️', junk: '⚠️', trash: '🗑️', all: '📚', flagged: '🚩' };
export const folderIcon = (f) => SPECIAL_ICONS[f.special] || '📁';
export const folderName = (f) => (f.special && f.special !== 'all' && f.special !== 'flagged' ? t(`mail.folder.${f.special}`) : f.display);

export const addrText = (a) => (a?.name ? `${a.name} <${a.email}>` : a?.email || '');
export const addrShort = (a) => a?.name || a?.email || '';

export function formatSize(n) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(0)} KB`;
  return `${(n / 1048576).toFixed(1)} MB`;
}

// The same rules as Files (apps/files/ops.js): our apps for documents, images and media, the
// system's default app for the rest, and otherwise reveal it in Files.
export function openPath(path, name) {
  const n = String(name || path).toLowerCase();
  if (/\.pdf$/.test(n) && getApp('pdf')) return wm.open('pdf', { new: true, args: { path } });
  if (/\.(png|jpe?g|gif|webp|bmp|heic|avif)$/.test(n) && getApp('photos')) return wm.open('photos', { new: true, args: { path } });
  if (/\.(mp3|ogg|flac|wav|m4a|opus|mp4|mkv|webm|mov|avi)$/.test(n) && getApp('player')) return wm.open('player', { args: { path } });
  if (/\.(txt|md|csv|log|json|xml|ya?ml|c|h|js|py|sh)$/.test(n) && getApp('editor')) return wm.open('editor', { args: { path } });
  if (/\.ics$/.test(n) && getApp('calendar')) { calendarEvents.importFile(path).then(() => wm.open('calendar')).catch(() => revealPath(path)); return true; }
  if (/\.vcf$/.test(n) && getApp('contacts')) {
    addressBooks.list().then((b) => (b[0] ? contacts.importFile(path, b[0].id) : null)).then(() => wm.open('contacts')).catch(() => revealPath(path));
    return true;
  }
  try {
    const def = defaultOpenerId(typeof lord.mime !== 'undefined' ? lord.mime.guess(n) : '');
    const native = nativeOf(def);
    if (native && getApp(native)) return wm.open(native, { new: true, args: { path } });
    if (def && lord.apps.launchWith(def.replace(/\.desktop$/, ''), path)) return true;
    if (lord.apps.launchWith('', path)) return true;
  } catch (e) { /* no default app */ }
  return revealPath(path);
}

export const revealPath = (path) => wm.open('files', { new: true, args: { path: fs.parent(path), select: fs.basename(path), nonce: Date.now() } });

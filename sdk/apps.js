// apps.js — todas las apps que se pueden abrir, para el Launchpad, el menú de inicio y la tienda:
//   - las apps LordOS incluidas en el tema (apps/),
//   - las apps LordOS instaladas como paquete (.lpk, ver docs/APPS-NATIVAS.md),
//   - las apps Linux instaladas (archivos .desktop; se abren como ventanas Wayland).
//
//   const list = useLauncherApps();      // [{ id, name, Icon, kind, comment, categories }]
//   launchApp(list[0].id);
//
// Las apps Linux se lanzan con lord.apps.launch(id): el servidor solo acepta el id de un
// .desktop instalado y ejecuta su línea Exec sin argumentos ni shell (ver server/src/apps.c).
import { createElement as h } from 'react';
import { View, Text, Image } from '@lordos/runtime';
import { createStore, useStore } from './store.js';
import { locale, appName } from './i18n.js';
import { listApps, wm, windows, appRegistry, getApp as registryApp } from './windows.js';

const DESKTOP = 'desktop:';
export const isLinuxApp = (id) => String(id).startsWith(DESKTOP);

// Ícono de reemplazo: la inicial sobre un color que sale del nombre (apps sin PNG).
export function LetterIcon({ size: s = 32, name = '?' }) {
  let hsh = 0;
  for (const ch of String(name)) hsh = (hsh * 31 + ch.codePointAt(0)) >>> 0;
  const hue = hsh % 360;
  const hex = (l) => {             // hsl(hue, 55%, l) → #rrggbb (el motor no entiende hsl())
    const a = 0.55 * Math.min(l, 1 - l), f = (n) => { const k = (n + hue / 30) % 12; return l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1)); };
    return '#' + [0, 8, 4].map((n) => Math.round(f(n) * 255).toString(16).padStart(2, '0')).join('');
  };
  const letter = (String(name).replace(/^.*\./, '')[0] || '?').toUpperCase();
  return h(View, { style: { width: s, height: s, borderRadius: s * 0.225, alignItems: 'center', justifyContent: 'center',
    background: `linear-gradient(180deg, ${hex(0.52)}, ${hex(0.36)})` } },
  h(Text, { style: { fontSize: s * 0.5, fontWeight: 700, color: '#fff' } }, letter));
}

// Ícono desde un PNG (o la inicial si no hay).
export function iconFromFile(path, name) {
  if (!path) return ({ size }) => h(LetterIcon, { size, name });
  return ({ size = 32 }) => h(Image, { src: path, style: { width: size, height: size, objectFit: 'contain' } });
}

const linux = createStore([]);

function readLinux() {
  if (typeof lord.apps === 'undefined' || typeof lord.apps.linux !== 'function') return [];   // no en modo app
  return lord.apps.linux(locale.get().lang).map((a) => ({
    ...a, id: DESKTOP + a.id, desktopId: a.id, kind: 'linux', Icon: iconFromFile(a.icon, a.name),
  }));
}

// Vuelve a leer los .desktop (después de instalar o quitar paquetes; es barato: el servidor
// solo relee si cambiaron las carpetas).
export function refreshApps() { linux.set(readLinux()); }
locale.subscribe(refreshApps);

export function useLauncherApps() {
  const lin = useStore(linux);
  useStore(locale);
  useStore(appRegistry);
  const own = listApps().map((a) => ({ ...a, kind: a.package ? 'package' : 'lordos', name: appName(a) }));
  return [...own, ...lin].sort((a, b) => a.name.localeCompare(b.name));
}

// Categorías principales del estándar de freedesktop (las mismas de AppStream en la Tienda).
// Una app Linux la toma del campo Categories de su .desktop; una de LordOS, de "category" en su
// manifest.
export const APP_CATEGORIES = ['AudioVideo', 'Development', 'Education', 'Game', 'Graphics', 'Network', 'Office', 'Science', 'Settings', 'System', 'Utility'];
const ALIASES = { Audio: 'AudioVideo', Video: 'AudioVideo', TerminalEmulator: 'System', FileManager: 'System', WebBrowser: 'Network', TextEditor: 'Utility' };
export function appCategory(a) {
  if (a.category && APP_CATEGORIES.includes(a.category)) return a.category;
  const list = String(a.categories || '').split(';').filter(Boolean);
  for (const c of list) if (APP_CATEGORIES.includes(c)) return c;
  for (const c of list) if (ALIASES[c]) return ALIASES[c];
  return 'Utility';
}

export function launchApp(id) {
  if (isLinuxApp(id)) return lord.apps.launch(String(id).slice(DESKTOP.length));
  // Una app .lpk instalada corre como su propio proceso aislado (lord-server --app <id>), si el
  // servidor lo soporta; llega al escritorio como ventana Wayland con app_id = su id. Si no, cae
  // en el modo en proceso (compatibilidad). Ver docs/PRIVACIDAD.md.
  const app = registryApp(id);
  if (app && app.package && typeof lord.apps !== 'undefined' && typeof lord.apps.launchPackage === 'function')
    return lord.apps.launchPackage(id);
  // a built-in app that runs as its own sandboxed process (manifest "process": "isolated")
  if (app && app.isolated && typeof lord.apps !== 'undefined' && typeof lord.apps.launchPackage === 'function') {
    const running = windows.get().filter((w) => w.args?.pkg && w.args?.appId === id);
    if (running.length) { wm.focus(running[running.length - 1].id); return true; }
    if (lord.apps.launchPackage(id)) return true;
  }
  return wm.open(id) != null;
}

// App Linux de una ventana Wayland (por su app_id: el nombre del .desktop o StartupWMClass).
export function linuxAppFor(appId) {
  if (!appId) return null;
  const want = String(appId).toLowerCase();
  if (!linux.get().length) refreshApps();
  return linux.get().find((a) => a.desktopId.toLowerCase() === want || a.wmClass.toLowerCase() === want ||
    a.desktopId.toLowerCase().endsWith(`.${want}`)) || null;
}

// Ícono de una ventana: el de su app; para una ventana Linux, el de su .desktop si se encuentra.
export function windowIcon(w, getApp) {
  const appId = w?.args?.appId;
  if (String(w?.id).startsWith('linux#')) {
    // una app .lpk aislada (lord-server --app) llega como ventana Wayland con app_id = su id
    const pkg = appId ? registryApp(appId) : null;
    if (pkg && (pkg.package || pkg.isolated) && pkg.Icon) return pkg.Icon;
    const a = linuxAppFor(appId);
    return a ? a.Icon : ({ size }) => h(LetterIcon, { size, name: appId || w?.title || 'Linux' });
  }
  return getApp(w?.id)?.Icon || (({ size }) => h(LetterIcon, { size, name: w?.id }));
}

refreshApps();

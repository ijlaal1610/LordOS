// dock.js — Dock y barra de tareas: apps fijadas (favoritas), ventanas agrupadas por app y el
// menú del clic derecho (como en macOS). Lo usan todos los temas; cada uno lo dibuja a su modo.
//
//   const entries = useDockEntries(['files', 'terminal'])   // fijadas por defecto del tema
//   // entry = { id, name, Icon, windows: [ventana…], pinned, running, focused }
//   dockActivate(entry)        // clic: abre la app o trae su ventana
//   dockMenu(entry)            // ítems para el menú contextual: [{ label, onClick, checked, submenu, header } | '-']
//
// Una app puede sumar sus propios ítems al menú (la Terminal: Nueva ventana, Nueva pestaña…):
//   registerDockMenu('terminal', (entry) => [{ label: '…', onClick }])
import { createElement as h } from 'react';
import { settings } from './settings.js';
import { useStore } from './store.js';
import { useWindows, wm, windowTitle, appIdOf, getApp, windows } from './windows.js';
import { useLauncherApps, launchApp, linuxAppFor, isLinuxApp, LetterIcon, windowIcon } from './apps.js';
import { t } from './i18n.js';

const menuProviders = new Map();
export function registerDockMenu(appId, fn) { menuProviders.set(appId, fn); return () => menuProviders.delete(appId); }

// ---- fijadas (en ~/.config/lordos/settings.json → dockPinned) ----

let themeDefaults = [];
const pinnedList = () => settings.get().dockPinned || themeDefaults;
export const dock = {
  pinned: pinnedList,
  isPinned: (id) => pinnedList().includes(id),
  pin(id, index) {
    const list = pinnedList().filter((x) => x !== id);
    list.splice(index == null ? list.length : index, 0, id);
    settings.set({ dockPinned: list });
  },
  unpin: (id) => settings.set({ dockPinned: pinnedList().filter((x) => x !== id) }),
  move(id, index) { dock.pin(id, index); },
};

// ---- preferencias del Dock y de la barra de tareas (Ajustes → Escritorio y Dock) ----
// Se guardan en settings.json. Izquierda y derecha son lados físicos de la pantalla, también con un
// idioma de derecha a izquierda (como macOS).
export const DOCK_DEFAULTS = {
  dockPosition: 'bottom',     // 'bottom' | 'left' | 'right' (tema Lord Default)
  dockSize: 54,               // px de cada ícono (36–80)
  dockMagnify: true,          // lupa
  dockMagnifySize: 86,        // px del ícono más grande con la lupa (más que dockSize)
  dockAutohide: false,        // se esconde y vuelve al tocar el borde
  dockRecents: false,         // muestra las últimas apps usadas que no están fijadas
  taskbarPosition: 'bottom',  // 'bottom' | 'top' | 'left' | 'right' (tema Clásico)
};
export function dockPrefs(s = settings.get()) {
  const p = { ...DOCK_DEFAULTS };
  for (const k of Object.keys(DOCK_DEFAULTS)) if (s[k] !== undefined && s[k] !== null) p[k] = s[k];
  if (!['bottom', 'left', 'right'].includes(p.dockPosition)) p.dockPosition = 'bottom';
  if (!['bottom', 'top', 'left', 'right'].includes(p.taskbarPosition)) p.taskbarPosition = 'bottom';
  p.dockSize = Math.max(36, Math.min(80, Math.round(+p.dockSize) || 54));
  p.dockMagnifySize = Math.max(p.dockSize, Math.min(128, Math.round(+p.dockMagnifySize) || 86));
  return p;
}

// Apps usadas hace poco (para «Mostrar apps recientes»): cada app que abre una ventana pasa adelante.
let openIds = new Set();
windows.subscribe(() => {
  const now = new Set();
  for (const w of windows.get()) if (!(getApp(w.id)?.builtin && !String(w.id).startsWith('linux#'))) now.add(launcherIdOf(w));
  const fresh = [...now].filter((id) => !openIds.has(id) && !id.startsWith('linux:'));
  openIds = now;
  if (!fresh.length) return;
  const cur = settings.get().dockRecentApps || [];
  const next = [...fresh, ...cur.filter((x) => !fresh.includes(x))].slice(0, 8);
  if (next.join('\n') !== cur.join('\n')) settings.set({ dockRecentApps: next });
});

// A qué ícono del Dock pertenece una ventana: su app LordOS, o la app Linux de su .desktop.
export function launcherIdOf(w) {
  if (!String(w.id).startsWith('linux#')) return appIdOf(w.id);
  const appId = w.args?.appId || '';
  if (w.args?.pkg && appId && (getApp(appId)?.package || getApp(appId)?.isolated)) return appId;   // isolated app (.lpk or built-in): grouped under its own entry
  const a = linuxAppFor(appId);
  return a ? a.id : `linux:${appId || w.id}`;
}

// opts.recents: cuántas apps recientes (no fijadas ni abiertas) se agregan al final, con recent: true.
export function useDockEntries(defaults = [], opts = {}) {
  themeDefaults = defaults;
  const s = useStore(settings);
  const { windows, focused } = useWindows();
  const apps = useLauncherApps();
  const pinned = s.dockPinned || defaults;
  const byId = new Map(apps.map((a) => [a.id, a]));
  const groups = new Map();
  for (const w of windows) {
    if (getApp(w.id)?.builtin && !String(w.id).startsWith('linux#')) continue;
    const k = launcherIdOf(w);
    if (!groups.has(k)) groups.set(k, []);
    groups.get(k).push(w);
  }
  const entry = (id, a, wins) => ({
    id, name: a?.name || windowTitle(wins[0]) || id, Icon: a?.Icon || (wins[0] ? windowIcon(wins[0]) : ({ size }) => h(LetterIcon, { size, name: id })),
    windows: wins, pinned: pinned.includes(id), running: wins.length > 0, focused: wins.some((w) => w.id === focused), app: a,
  });
  const out = [];
  for (const id of pinned) {
    const a = byId.get(id);
    if (!a && !groups.has(id)) continue;          // fijada pero desinstalada
    out.push(entry(id, a, groups.get(id) || []));
  }
  for (const [id, wins] of groups) if (!pinned.includes(id)) out.push(entry(id, byId.get(id), wins));
  if (opts.recents) {
    const have = new Set(out.map((e) => e.id));
    for (const id of (s.dockRecentApps || []).filter((x) => !have.has(x) && byId.has(x)).slice(0, opts.recents)) {
      out.push({ ...entry(id, byId.get(id), []), recent: true });
    }
  }
  return out;
}

// Clic en el ícono: sin ventanas abre la app; con ventanas trae la más reciente (si ya estaba
// al frente y hay varias, pasa a la siguiente, como ❖` en macOS).
export function dockActivate(e) {
  if (!e.windows.length) { launchApp(e.id); return; }
  const top = wm.focused();
  const idx = e.windows.findIndex((w) => w.id === top);
  const next = idx >= 0 && e.windows.length > 1 ? e.windows[(idx + e.windows.length - 1) % e.windows.length] : e.windows[e.windows.length - 1];
  wm.focus(next.id);
}

const closeWindow = (w) => wm.close(w.id);

export function dockMenu(e) {
  const top = wm.focused();
  const items = [];
  if (e.windows.length) {
    for (const w of [...e.windows].reverse()) {
      items.push({ label: windowTitle(w) || e.name, checked: w.id === top, hint: w.minimized ? '–' : '', onClick: () => wm.focus(w.id) });
    }
    items.push('-');
  }
  const appKey = isLinuxApp(e.id) || e.id.startsWith('linux:') ? null : e.id;
  const custom = appKey && menuProviders.get(appKey) ? menuProviders.get(appKey)(e) : null;
  if (custom && custom.length) items.push(...custom, '-');
  else if (e.windows.length && !e.id.startsWith('linux:')) items.push({ label: t('shell.dock.newWindow'), onClick: () => (appKey ? wm.open(appKey, { new: true }) : launchApp(e.id)) }, '-');
  if (!e.id.startsWith('linux:')) {
    items.push({ label: t('shell.dock.options'), submenu: [
      { label: t('shell.dock.keep'), checked: dock.isPinned(e.id), onClick: () => (dock.isPinned(e.id) ? dock.unpin(e.id) : dock.pin(e.id)) },
    ] }, '-');
  }
  if (e.windows.length) {
    items.push(
      { label: t('shell.dock.showAll'), onClick: () => { for (const w of e.windows) wm.focus(w.id); } },
      { label: t('shell.dock.hide'), onClick: () => { for (const w of e.windows) wm.minimize(w.id); } },
      { label: t('shell.dock.quit'), onClick: () => { for (const w of [...e.windows]) closeWindow(w); } },
    );
  } else items.push({ label: t('shell.dock.open'), onClick: () => launchApp(e.id) });
  while (items[items.length - 1] === '-') items.pop();
  return items;
}

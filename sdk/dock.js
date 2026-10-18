// dock.js — Dock and taskbar: pinned (favorite) apps, windows grouped by app and the
// right-click menu (as in macOS). Every theme uses it; each one draws it its own way.
//
//   const entries = useDockEntries(['files', 'terminal'])   // the theme's default pinned apps
//   // entry = { id, name, Icon, windows: [window…], pinned, running, focused }
//   dockActivate(entry)        // click: opens the app or brings up its window
//   dockMenu(entry)            // items for the context menu: [{ label, onClick, checked, submenu, header } | '-']
//
// An app can add its own items to the menu (the Terminal: New Window, New Tab…):
//   registerDockMenu('terminal', (entry) => [{ label: '…', onClick }])
import { createElement as h } from 'react';
import { settings } from './settings.js';
import { useStore } from './store.js';
import { useWindows, wm, windowTitle, appIdOf, getApp, windows } from './windows.js';
import { useLauncherApps, launchApp, linuxAppFor, isLinuxApp, LetterIcon, windowIcon } from './apps.js';
import { t } from './i18n.js';

const menuProviders = new Map();
export function registerDockMenu(appId, fn) { menuProviders.set(appId, fn); return () => menuProviders.delete(appId); }

// ---- pinned (in ~/.config/lordos/settings.json → dockPinned) ----

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

// ---- Dock and taskbar preferences (Settings → Desktop & Dock) ----
// Stored in settings.json. Left and right are physical sides of the screen, even with a
// right-to-left language (as in macOS).
export const DOCK_DEFAULTS = {
  dockPosition: 'bottom',     // 'bottom' | 'left' | 'right' (Lord Default theme)
  dockSize: 54,               // px of each icon (36–80)
  dockMagnify: true,          // magnification
  dockMagnifySize: 86,        // px of the largest icon when magnified (more than dockSize)
  dockAutohide: false,        // hides and comes back when the edge is touched
  dockRecents: false,         // shows the most recently used apps that aren't pinned
  taskbarPosition: 'bottom',  // 'bottom' | 'top' | 'left' | 'right' (Classic theme)
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

// Recently used apps (for «Show recent apps»): each app that opens a window moves to the front.
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

// Which Dock icon a window belongs to: its LordOS app, or the Linux app from its .desktop.
export function launcherIdOf(w) {
  if (!String(w.id).startsWith('linux#')) return appIdOf(w.id);
  const appId = w.args?.appId || '';
  if (w.args?.pkg && appId && (getApp(appId)?.package || getApp(appId)?.isolated)) return appId;   // isolated app (.lpk or built-in): grouped under its own entry
  const a = linuxAppFor(appId);
  return a ? a.id : `linux:${appId || w.id}`;
}

// opts.recents: how many recent apps (neither pinned nor open) are appended at the end, with recent: true.
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
    if (!a && !groups.has(id)) continue;          // pinned but uninstalled
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

// Click on the icon: with no windows it opens the app; with windows it brings up the most recent one (if it
// was already in front and there are several, it moves to the next, like ❖` in macOS).
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

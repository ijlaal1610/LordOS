// windows.js — window manager for LordOS apps (independent of the theme).
// The theme decides how they look; the SDK decides which windows exist, their order and geometry.
//
// Each window has its own `id` and the `app` that shows it. An app can have several
// windows (wm.open('terminal', { new: true }) → id 'terminal#2'), a dynamic title
// (wm.setTitle), confirmation on close (wm.setCloseGuard) and its own menus (wm.setMenus)
// that the theme shows in its menu bar.
import { screen, onScreenChange, addListener, mainDisplay, displayAt, displayFor } from '@lordos/runtime';
import { createStore, useStore } from './store.js';
import { appName } from './i18n.js';

let registry = new Map();
let builtins = [];
let themeApps = [];
let packageApps = [];
// Changes when an app is installed or removed (to redraw the app launcher).
export const appRegistry = createStore(0);
const rebuild = () => {
  registry = new Map([...themeApps, ...packageApps, ...builtins].map((a) => [a.id, a]));
  appRegistry.set((v) => v + 1);
};
// Work area of each display (docs/DISPLAYS.md): the theme gives a function display → rect (menu bar
// and Dock margins), or the main display's rect as before; other displays then use all of theirs.
let areaFn = null;
let area = { x: 0, y: 0, w: screen.width, h: screen.height };
let chrome = { w: 0, h: 0 };
// The display under the pointer: new windows open there.
const pointer = { x: -1, y: -1 };
addListener('pointermove', (e) => { pointer.x = e.x; pointer.y = e.y; });
addListener('pointerdown', (e) => { pointer.x = e.x; pointer.y = e.y; });
export function workAreaOf(d) {
  d = d || mainDisplay();
  if (areaFn) return areaFn(d);
  return d.main ? area : { x: d.x, y: d.y, w: d.w, h: d.h };
}
const pointerDisplay = () => (pointer.x >= 0 ? displayAt(pointer.x, pointer.y) : mainDisplay());
// Work area of the display under the pointer (where a new window opens).
export const pointerWorkArea = () => workAreaOf(pointerDisplay());
const areaOfWindow = (w) => workAreaOf(displayFor(w));
let seq = 2;
const guards = new Map();

// State: list ordered from bottom to top (the last one is in front).
export const windows = createStore([]);

// The screen size changed: the work area keeps the theme's margins (menu
// bar, dock) and the windows are rearranged to stay in view.
onScreenChange((now, old) => {
  if (!areaFn && (old.displays || []).length <= 1 && screen.displays.length <= 1) {
    area = { ...area, w: Math.max(200, area.w + now.width - old.width), h: Math.max(150, area.h + now.height - old.height) };
  }
  // a window left on a display that is gone (or outside every display) moves to the main one; the
  // others stay on their display, inside its work area
  const onSome = (w) => screen.displays.some((d) => w.x < d.x + d.w - 48 && w.x + w.w > d.x + 48 && w.y < d.y + d.h - 24 && w.y + 24 > d.y);
  windows.set((ws) => ws.map((w) => {
    const a = onSome(w) ? areaOfWindow(w) : workAreaOf(mainDisplay());
    const ww = Math.min(w.w, a.w), hh = Math.min(w.h, a.h);
    const keep = onSome(w) && w.x + ww > a.x && w.x < a.x + a.w;
    return {
      ...w, w: ww, h: hh,
      x: Math.round(keep ? Math.min(Math.max(w.x, a.x - ww + 96), a.x + a.w - 96) : a.x + (a.w - ww) / 2),
      y: Math.round(keep ? Math.min(Math.max(w.y, a.y), a.y + a.h - Math.min(hh, 48)) : a.y + Math.max(0, (a.h - hh) / 2)),
    };
  }));
});
// Menus of each window: id → [{ label, items: [{ label, hint, onClick, disabled, checked } | '-'] }]
export const appMenus = createStore({});

// chrome: what the theme's frame adds around the content (borders and title bar);
// used to open Linux app windows at the size they requested.
export function configureWindows({ apps, workArea, chrome: c }) {
  themeApps = apps;
  rebuild();
  if (typeof workArea === 'function') areaFn = workArea;
  else if (workArea) { area = workArea; areaFn = null; }
  if (c) chrome = c;
}

// SDK internal apps (they don't appear in the app launcher): 'linux' = Wayland windows.
export function registerBuiltinApp(app) {
  builtins = [...builtins.filter((a) => a.id !== app.id), app];
  registry.set(app.id, app);
}
// Apps installed as a package (packages.js). They don't override the theme's.
export function setPackageApps(list) {
  packageApps = list.filter((a) => !themeApps.some((t) => t.id === a.id));
  rebuild();
}
export const windowChrome = () => chrome;

export const appIdOf = (id) => String(id ?? '').split('#')[0];
export const getApp = (id) => registry.get(appIdOf(id));
// Live system (ISO): apps with "live": true in their manifest (the installer) only exist there.
let live = null;
export const isLiveSystem = () => {
  if (live === null) { try { live = !!lord.system.info().live; } catch (e) { live = false; } }
  return live;
};
export const listApps = () => [...registry.values()].filter((a) => !a.builtin && (!a.live || isLiveSystem()));
export const workArea = (d) => workAreaOf(d);

function place(appId, count, frame) {
  if (frame) return { ...frame, minimized: false, maximized: false };
  const [sw, sh] = registry.get(appId)?.size || [640, 420];
  const offset = (count % 6) * 30;
  const area = workAreaOf(pointerDisplay());       // where the pointer is (docs/DISPLAYS.md)
  // the whole window stays inside the work area: a cascaded window must not slide under the
  // Dock, which would cover (and take the clicks of) its bottom edge
  const w = Math.min(sw, area.w), h = Math.min(sh, area.h);
  const x = Math.round(area.x + (area.w - w) / 2 - 60 + offset);
  const y = Math.round(Math.max(area.y + 16, area.y + (area.h - h) / 2 - 40 + offset));
  return {
    w, h, minimized: false, maximized: false,
    x: Math.max(area.x, Math.min(x, area.x + area.w - w)),
    y: Math.max(area.y, Math.min(y, area.y + area.h - h)),
  };
}

const raise = (ws, id) => {
  const w = ws.find((x) => x.id === id);
  return w ? [...ws.filter((x) => x.id !== id), { ...w, minimized: false }] : ws;
};
const patch = (id, p) => windows.set((ws) => ws.map((w) => (w.id === id ? { ...w, ...p } : w)));

// ---- dragging in C (lord.wm, docs/WINDOWS.md) ----
// The theme marks each window's node with ref={wm.frameRef(id, inset)}; inset = how much the
// node extends past the frame on each side (the invisible borders for resizing). Moving or resizing is tracked
// by lord-server with no JS per frame; on release __lordWm('end') arrives and the new geometry is saved.
const frameNodes = new Map();         // window id → { node, inset }
const frameRefs = new Map();
let grab = null;
function frameRef(id, inset = 0) {
  const key = `${id}\u0000${inset}`;
  let fn = frameRefs.get(key);
  if (!fn) {
    fn = (inst) => {
      if (inst && inst.id) frameNodes.set(id, { node: inst.id, inset });
      else if (frameNodes.get(id)?.inset === inset) frameNodes.delete(id);
    };
    frameRefs.set(key, fn);
  }
  return fn;
}
function beginGrab(id, edges) {
  const w = windows.get().find((x) => x.id === id);
  const f = frameNodes.get(id);
  if (!w || !f || w.maximized || w.fullscreen || typeof lord === 'undefined' || !lord.wm) return false;
  const [minW, minH] = registry.get(w.app)?.minSize || [160, 100];
  const areas = screen.displays.map((d) => workAreaOf(d)).map((a, i) => ({ x: screen.displays[i].x, w: screen.displays[i].w, y: a.y }));
  if (!lord.wm.grab(f.node, edges, { inset: f.inset, minW, minH, minTop: areaOfWindow(w).y, areas })) return false;
  grab = { id, node: f.node, f: { x: w.x, y: w.y, w: w.w, h: w.h } };
  return true;
}
globalThis.__lordWm = (type, d) => {
  if (type !== 'end' || !grab || grab.node !== d.node) return;
  const { id, f } = grab;
  grab = null;
  if (!d.dx && !d.dy && !d.dw && !d.dh) return;      // it was a click on the bar
  patch(id, { x: Math.round(f.x + d.dx), y: Math.round(f.y + d.dy), w: Math.round(f.w + d.dw), h: Math.round(f.h + d.dh), maximized: false });
};

// when each window was opened (lord.now()), for the developer timing log (settings "debugTiming")
export const openedAt = new Map();

export const wm = {
  // opts: { new: true } another window of the same app; { instance: 'x' } named window;
  //       { frame: { x, y, w, h } } initial geometry; { args: {...} } data for the app
  //       (e.g. wm.open('files', { args: { path: '/home/lord/Documentos' } })).
  // Returns the window id.
  open(appId, opts = {}) {
    if (!registry.has(appId)) {
      // inside an isolated system app (lord-server --app): another system app is opened by the
      // session, through the broker (Mail → Calendar, Contacts → a new message; sec/brokermail.c)
      if (typeof lord !== 'undefined' && typeof lord.appId === 'string' && lord.broker && typeof lord.broker.openApp === 'function') {
        lord.broker.openApp(String(appId), JSON.stringify(opts.args || {}), !!opts.new);
        return null;
      }
      console.warn('unknown app:', appId);
      return null;
    }
    const id = opts.instance ? `${appId}#${opts.instance}` : opts.new ? `${appId}#${seq++}` : appId;
    const args = opts.args ? { ...opts.args, nonce: seq++ } : null;
    if (globalThis.__lordTiming && typeof lord !== 'undefined' && lord.now) openedAt.set(id, lord.now());
    windows.set((ws) => (ws.some((w) => w.id === id)
      ? raise(ws, id).map((w) => (w.id === id && args ? { ...w, args } : w))
      : [...ws, { id, app: appId, title: null, args, ...place(appId, ws.length, opts.frame) }]));
    return id;
  },
  // Closes the window; if the app set a guard and it returns false, it doesn't close.
  close(id, force = false) {
    const guard = guards.get(id);
    if (!force && guard && guard() === false) return false;
    guards.delete(id);
    appMenus.set((m) => { if (!(id in m)) return m; const n = { ...m }; delete n[id]; return n; });
    windows.set((ws) => ws.filter((w) => w.id !== id));
    return true;
  },
  focus: (id) => windows.set((ws) => (ws[ws.length - 1]?.id === id && !ws[ws.length - 1].minimized ? ws : raise(ws, id))),
  minimize: (id) => patch(id, { minimized: true }),
  toggleMaximize: (id) => windows.set((ws) => ws.map((w) => (w.id === id ? { ...w, maximized: !w.maximized } : w))),
  move: (id, x, y) => patch(id, { x: Math.round(x), y: Math.round(Math.max(workAreaOf(displayAt(x, y)).y, y)), maximized: false }),
  resize: (id, w, h) => patch(id, { w: Math.round(w), h: Math.round(h) }),
  setFrame: (id, f) => patch(id, { ...f, maximized: false }),
  setTitle: (id, title) => {
    const w = windows.get().find((x) => x.id === id);
    if (w && w.title !== title) patch(id, { title });
  },
  setCloseGuard(id, fn) {
    guards.set(id, fn);
    return () => { if (guards.get(id) === fn) guards.delete(id); };
  },
  setMenus: (id, menus) => {
    // In an isolated app (lord-server --app) the menus go to the system menu bar through the
    // broker: they are serialized (no onClick, with an id per item) and clicks come back as an event.
    if (typeof lord !== 'undefined' && typeof lord.appId === 'string') { sendAppMenus(menus); return; }   // only in the app process
    appMenus.set((m) => ({ ...m, [id]: menus }));
  },
  get: (id) => windows.get().find((w) => w.id === id) || null,
  focused() {
    const vis = windows.get().filter((w) => !w.minimized);
    return vis.length ? vis[vis.length - 1].id : null;
  },
  // Protected window (capture protection): it looks normal on screen, but
  // comes out black in screenshots, recordings and remote desktop (docs/MEDIA.md). Used, for
  // example, by a password app.
  setProtected: (id, on = true) => patch(id, { protected: !!on }),
  // Fullscreen for a LordOS app (Player, Photos): the SDK draws it above everything,
  // without the theme's frame. Esc or wm.setFullscreen(id, false) goes back.
  setFullscreen: (id, on = true) => windows.set((ws) => ws.map((w) => (w.id === id ? { ...w, fullscreen: !!on, minimized: false } : w))),
  // Picture-in-picture: a small frameless window, always on top (frame = { x, y, w, h } or null).
  setPip: (id, frame) => patch(id, { pip: frame ? { ...frame } : null, fullscreen: false }),
  // Dragging in C (see frameRef above). beginMove/beginResize return false if it wasn't possible (no
  // marked node, maximized, or no button held): the theme can continue with its own JS drag.
  // edges as in xdg_toplevel.resize: 1 top, 2 bottom, 4 left, 8 right (5 = top-left…).
  frameRef,
  beginMove: (id) => beginGrab(id, 0),
  beginResize: (id, edges) => beginGrab(id, edges & 15),
  // Effective geometry (a maximized window takes the work area).
  frame: (w) => { if (!w.maximized) return w; const a = areaOfWindow(w); return { x: a.x, y: a.y, w: a.w, h: a.h }; },
};

export function useWindows() {
  const list = useStore(windows);
  // fullscreen and picture-in-picture windows are drawn by the SDK (FloatingWindows)
  const visible = list.filter((w) => !w.minimized && !w.fullscreen && !w.pip);
  return { windows: list, visible, focused: visible.length ? visible[visible.length - 1].id : null };
}

// ---- menus of an isolated app: serialize to the broker and receive the clicks ----
const appMenuHandlers = new Map();
let appMenuSeq = 1;
function sendAppMenus(menus) {
  appMenuHandlers.clear();
  const ser = (menus || []).map((g) => ({
    label: g.label,
    items: (g.items || []).map((it) => {
      if (it === '-' || it == null) return '-';
      const mid = `m${appMenuSeq++}`;
      if (typeof it.onClick === 'function') appMenuHandlers.set(mid, it.onClick);
      const sub = it.submenu ? it.submenu.map((s) => (s === '-' ? '-' : (() => { const sid = `m${appMenuSeq++}`; if (s.onClick) appMenuHandlers.set(sid, s.onClick); return { label: s.label, hint: s.hint, disabled: !!s.disabled, checked: !!s.checked, id: sid }; })())) : undefined;
      return { label: it.label, hint: it.hint, disabled: !!it.disabled, checked: !!it.checked, id: mid, submenu: sub };
    }),
  }));
  try { lord.broker.menus(JSON.stringify(ser)); } catch (e) { /* server without broker */ }
}
// Called by the app host when the user picks an item in the system menu bar.
export function appMenuClick(itemId) { const fn = appMenuHandlers.get(itemId); if (fn) try { fn(); } catch (e) { console.error('menu:', e); } }

// Menus of window `id` (for the theme's menu bar).
export const useAppMenus = (id) => useStore(appMenus, (m) => (id ? m[id] : null)) || null;

// Display name of a window: its title or the app's name.
export const windowTitle = (w) => w?.title || appName(getApp(w?.id)) || '';

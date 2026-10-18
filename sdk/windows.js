// windows.js — gestor de ventanas de las apps LordOS (independiente del tema).
// El tema decide cómo se ven; el SDK decide qué ventanas hay, su orden y geometría.
//
// Cada ventana tiene un `id` propio y el `app` que la muestra. Una app puede tener varias
// ventanas (wm.open('terminal', { new: true }) → id 'terminal#2'), título dinámico
// (wm.setTitle), confirmación al cerrar (wm.setCloseGuard) y menús propios (wm.setMenus)
// que el tema muestra en su barra de menú.
import { screen, onScreenChange } from '@lordos/runtime';
import { createStore, useStore } from './store.js';
import { appName } from './i18n.js';

let registry = new Map();
let builtins = [];
let themeApps = [];
let packageApps = [];
// Cambia cuando se instala o quita una app (para volver a dibujar el Launchpad).
export const appRegistry = createStore(0);
const rebuild = () => {
  registry = new Map([...themeApps, ...packageApps, ...builtins].map((a) => [a.id, a]));
  appRegistry.set((v) => v + 1);
};
let area = { x: 0, y: 0, w: screen.width, h: screen.height };
let chrome = { w: 0, h: 0 };
let seq = 2;
const guards = new Map();

// Estado: lista ordenada de abajo hacia arriba (la última está al frente).
export const windows = createStore([]);

// Cambió el tamaño de la pantalla: el área de trabajo conserva los márgenes del tema (barra de
// menú, dock) y las ventanas se acomodan para seguir a la vista.
onScreenChange((now, old) => {
  area = { ...area, w: Math.max(200, area.w + now.width - old.width), h: Math.max(150, area.h + now.height - old.height) };
  windows.set((ws) => ws.map((w) => {
    const ww = Math.min(w.w, area.w), hh = Math.min(w.h, area.h);
    return {
      ...w, w: ww, h: hh,
      x: Math.round(Math.min(Math.max(w.x, area.x), area.x + area.w - ww)),
      y: Math.round(Math.min(Math.max(w.y, area.y), area.y + area.h - hh)),
    };
  }));
});
// Menús de cada ventana: id → [{ label, items: [{ label, hint, onClick, disabled, checked } | '-'] }]
export const appMenus = createStore({});

// chrome: lo que suma el marco del tema alrededor del contenido (bordes y barra de título);
// sirve para abrir las ventanas de apps Linux con el tamaño que pidieron.
export function configureWindows({ apps, workArea, chrome: c }) {
  themeApps = apps;
  rebuild();
  if (workArea) area = workArea;
  if (c) chrome = c;
}

// Apps internas del SDK (no aparecen en el Launchpad): 'linux' = ventanas Wayland.
export function registerBuiltinApp(app) {
  builtins = [...builtins.filter((a) => a.id !== app.id), app];
  registry.set(app.id, app);
}
// Apps instaladas como paquete (packages.js). No pisan a las del tema.
export function setPackageApps(list) {
  packageApps = list.filter((a) => !themeApps.some((t) => t.id === a.id));
  rebuild();
}
export const windowChrome = () => chrome;

export const appIdOf = (id) => String(id ?? '').split('#')[0];
export const getApp = (id) => registry.get(appIdOf(id));
// Sistema en vivo (ISO): las apps con "live": true en su manifest (el instalador) solo existen ahí.
let live = null;
export const isLiveSystem = () => {
  if (live === null) { try { live = !!lord.system.info().live; } catch (e) { live = false; } }
  return live;
};
export const listApps = () => [...registry.values()].filter((a) => !a.builtin && (!a.live || isLiveSystem()));
export const workArea = () => area;

function place(appId, count, frame) {
  if (frame) return { ...frame, minimized: false, maximized: false };
  const [w, h] = registry.get(appId)?.size || [640, 420];
  const offset = (count % 6) * 30;
  return {
    w, h, minimized: false, maximized: false,
    x: Math.round(area.x + (area.w - w) / 2 - 60 + offset),
    y: Math.round(Math.max(area.y + 16, area.y + (area.h - h) / 2 - 40 + offset)),
  };
}

const raise = (ws, id) => {
  const w = ws.find((x) => x.id === id);
  return w ? [...ws.filter((x) => x.id !== id), { ...w, minimized: false }] : ws;
};
const patch = (id, p) => windows.set((ws) => ws.map((w) => (w.id === id ? { ...w, ...p } : w)));

// ---- arrastre en C (lord.wm, docs/VENTANAS.md) ----
// El tema marca el nodo de cada ventana con ref={wm.frameRef(id, inset)}; inset = cuánto agranda el
// nodo al marco por lado (los bordes invisibles para cambiar el tamaño). Mover o agrandar lo sigue
// lord-server sin JS por cuadro; al soltar llega __lordWm('end') y se guarda la geometría nueva.
const frameNodes = new Map();         // id de ventana → { node, inset }
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
  if (!lord.wm.grab(f.node, edges, { inset: f.inset, minW, minH, minTop: area.y })) return false;
  grab = { id, node: f.node, f: { x: w.x, y: w.y, w: w.w, h: w.h } };
  return true;
}
globalThis.__lordWm = (type, d) => {
  if (type !== 'end' || !grab || grab.node !== d.node) return;
  const { id, f } = grab;
  grab = null;
  if (!d.dx && !d.dy && !d.dw && !d.dh) return;      // fue un clic en la barra
  patch(id, { x: Math.round(f.x + d.dx), y: Math.round(f.y + d.dy), w: Math.round(f.w + d.dw), h: Math.round(f.h + d.dh), maximized: false });
};

export const wm = {
  // opts: { new: true } otra ventana de la misma app; { instance: 'x' } ventana con nombre;
  //       { frame: { x, y, w, h } } geometría inicial; { args: {...} } datos para la app
  //       (ej.: wm.open('files', { args: { path: '/home/lord/Documentos' } })).
  // Devuelve el id de la ventana.
  open(appId, opts = {}) {
    if (!registry.has(appId)) { console.warn('app desconocida:', appId); return null; }
    const id = opts.instance ? `${appId}#${opts.instance}` : opts.new ? `${appId}#${seq++}` : appId;
    const args = opts.args ? { ...opts.args, nonce: seq++ } : null;
    windows.set((ws) => (ws.some((w) => w.id === id)
      ? raise(ws, id).map((w) => (w.id === id && args ? { ...w, args } : w))
      : [...ws, { id, app: appId, title: null, args, ...place(appId, ws.length, opts.frame) }]));
    return id;
  },
  // Cierra la ventana; si la app puso una guarda y devuelve false, no se cierra.
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
  move: (id, x, y) => patch(id, { x: Math.round(x), y: Math.round(Math.max(area.y, y)), maximized: false }),
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
    // En una app aislada (lord-server --app) los menús van a la barra de menú del sistema por el
    // broker: se serializan (sin onClick, con un id por ítem) y los clics vuelven por un evento.
    if (typeof lord !== 'undefined' && typeof lord.appId === 'string') { sendAppMenus(menus); return; }   // solo en el proceso de la app
    appMenus.set((m) => ({ ...m, [id]: menus }));
  },
  get: (id) => windows.get().find((w) => w.id === id) || null,
  focused() {
    const vis = windows.get().filter((w) => !w.minimized);
    return vis.length ? vis[vis.length - 1].id : null;
  },
  // Ventana protegida (como la protección de captura de macOS): se ve normal en la pantalla, pero
  // sale negra en capturas, grabaciones y el escritorio remoto (docs/MEDIOS.md). La usa, por
  // ejemplo, una app de contraseñas.
  setProtected: (id, on = true) => patch(id, { protected: !!on }),
  // Pantalla completa de una app LordOS (Reproductor, Fotos): la dibuja el SDK encima de todo,
  // sin el marco del tema. Esc o wm.setFullscreen(id, false) vuelve.
  setFullscreen: (id, on = true) => windows.set((ws) => ws.map((w) => (w.id === id ? { ...w, fullscreen: !!on, minimized: false } : w))),
  // Imagen en imagen: una ventanita sin marco, siempre encima (frame = { x, y, w, h } o null).
  setPip: (id, frame) => patch(id, { pip: frame ? { ...frame } : null, fullscreen: false }),
  // Arrastre en C (ver frameRef arriba). beginMove/beginResize devuelven false si no se pudo (sin
  // nodo marcado, maximizada, o sin un botón apretado): el tema puede seguir con su arrastre en JS.
  // edges como xdg_toplevel.resize: 1 arriba, 2 abajo, 4 izquierda, 8 derecha (5 = arriba-izquierda…).
  frameRef,
  beginMove: (id) => beginGrab(id, 0),
  beginResize: (id, edges) => beginGrab(id, edges & 15),
  // Geometría efectiva (maximizada ocupa el área de trabajo).
  frame: (w) => (w.maximized ? { x: area.x, y: area.y, w: area.w, h: area.h } : w),
};

export function useWindows() {
  const list = useStore(windows);
  // las de pantalla completa y las de imagen en imagen las dibuja el SDK (FloatingWindows)
  const visible = list.filter((w) => !w.minimized && !w.fullscreen && !w.pip);
  return { windows: list, visible, focused: visible.length ? visible[visible.length - 1].id : null };
}

// ---- menús de una app aislada: serializar hacia el broker y recibir los clics ----
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
  try { lord.broker.menus(JSON.stringify(ser)); } catch (e) { /* servidor sin broker */ }
}
// Llamado por el app host cuando el usuario elige un ítem en la barra de menú del sistema.
export function appMenuClick(itemId) { const fn = appMenuHandlers.get(itemId); if (fn) try { fn(); } catch (e) { console.error('menu:', e); } }

// Menús de la ventana `id` (para la barra de menú del tema).
export const useAppMenus = (id) => useStore(appMenus, (m) => (id ? m[id] : null)) || null;

// Nombre a mostrar de una ventana: su título o el nombre de la app.
export const windowTitle = (w) => w?.title || appName(getApp(w?.id)) || '';

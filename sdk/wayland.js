// wayland.js — ventanas de apps Linux (GTK, Qt, foot…) que se conectan al compositor Wayland
// de lord-server (server/src/wl). Para el tema son ventanas como las de las apps LordOS: entran
// en el gestor con la app interna 'linux' y su id es 'linux#<superficie>'.
//
// Las que piden decoración del servidor (Qt, foot) llevan el marco del tema; las que dibujan
// el suyo (GTK) van "sin marco": args.csd = true, y el tema dibuja solo el contenido. Mover y
// cambiar el tamaño desde la barra de una app GTK llega como pedido del compositor.
import { createElement as h } from 'react';
import { View, Text, Surface, addListener } from '@lordos/runtime';
import { windows, wm, registerBuiltinApp, windowChrome, workArea, getApp } from './windows.js';
import { useAppWindow } from './components.jsx';
import { locale, appName } from './i18n.js';

const idOf = (sid) => `linux#${sid}`;
export const surfaceOf = (wid) => (String(wid).startsWith('linux#') ? Number(String(wid).slice(6)) || 0 : 0);

function LinuxWindow() {
  const { args } = useAppWindow();
  return h(Surface, { surface: args?.surface || 0, style: { flex: 1 } });
}

// Ícono genérico (los temas que dibujan app.Icon lo necesitan): un pingüino sobre gris.
function LinuxIcon({ size: s = 32 }) {
  return h(View, { style: { width: s, height: s, borderRadius: s * 0.225, alignItems: 'center', justifyContent: 'center',
    background: 'linear-gradient(180deg, #5b6573, #353c47)' } },
  h(Text, { style: { fontSize: s * 0.58, lineHeight: s } }, '🐧'));
}

registerBuiltinApp({ id: 'linux', name: 'Linux', builtin: true, size: [800, 600], minSize: [160, 100], Component: LinuxWindow, Icon: LinuxIcon });

/* arrastre pedido por la app (xdg_toplevel.move/resize): sigue al puntero hasta soltar */
let drag = null;
addListener('pointermove', (e) => {
  if (!drag) return;
  const w = wm.get(drag.id);
  if (!w) { drag = null; return; }
  if (!drag.start || drag.start.x == null) { drag.start = { x: e.x, y: e.y, f: wm.frame(w) }; return; }
  const dx = e.x - drag.start.x, dy = e.y - drag.start.y, f = drag.start.f;
  if (drag.edges == null) { wm.move(drag.id, f.x + dx, f.y + dy); return; }
  const E = drag.edges;      // 1 arriba, 2 abajo, 4 izquierda, 8 derecha
  let { x, y, w: ww, h: hh } = f;
  if (E & 8) ww = Math.max(160, f.w + dx);
  if (E & 2) hh = Math.max(100, f.h + dy);
  if (E & 4) { ww = Math.max(160, f.w - dx); x = f.x + f.w - ww; }
  if (E & 1) { hh = Math.max(100, f.h - dy); y = f.y + f.h - hh; }
  wm.setFrame(drag.id, { x, y, w: ww, h: hh });
});
addListener('pointerup', () => { drag = null; });

globalThis.__lordWayland = (sid, type, d = {}) => {
  const id = idOf(sid);
  const win = wm.get(id);
  switch (type) {
    case 'map': {
      const c = d.csd ? { w: 0, h: 0 } : windowChrome();
      const area = workArea();
      const w = Math.min(area.w, d.width + c.w), hh = Math.min(area.h, d.height + c.h);
      const n = windows.get().length;
      wm.open('linux', {
        instance: String(sid),
        frame: { w, h: hh, x: Math.round(area.x + (area.w - w) / 2 + (n % 5) * 24), y: Math.round(area.y + Math.max(8, (area.h - hh) / 2 - 30) + (n % 5) * 24) },
        args: { surface: sid, csd: !!d.csd, appId: d.appId || '', pkg: !!(d.appId && (getApp(d.appId)?.package || getApp(d.appId)?.isolated)) },
      });
      // app .lpk aislada: su nombre (del manifest) en vez del id
      { const pkg = d.appId ? getApp(d.appId) : null; wm.setTitle(id, pkg && (pkg.package || pkg.isolated) && (!d.title || d.title === d.appId) ? (pkg.isolated ? appName(pkg) : pkg.name) : (d.title || d.appId || null)); }
      // a built-in isolated app that asks for it (manifest "protected": Passwords) is black in captures
      if (d.appId && getApp(d.appId)?.isolated && getApp(d.appId)?.protected) wm.setProtected(id, true);
      wm.setCloseGuard(id, () => { lord.wayland.close(sid); return false; });   // la app decide cuándo cierra
      if (d.appId && (getApp(d.appId)?.package || getApp(d.appId)?.isolated)) setTimeout(() => applyAppMenus(d.appId), 0);   // menús de la app aislada (ya mapeada)
      break;
    }
    case 'unmap': if (win) wm.close(id, true); break;
    case 'title': if (win) wm.setTitle(id, d.title || null); break;
    case 'geometry': {                     // la app cambió su tamaño por su cuenta
      if (!win || win.maximized) break;
      const c = win.args?.csd ? { w: 0, h: 0 } : windowChrome();
      if (Math.abs(win.w - (d.width + c.w)) > 1 || Math.abs(win.h - (d.height + c.h)) > 1) wm.resize(id, d.width + c.w, d.height + c.h);
      break;
    }
    // desde donde se apretó el botón (la app pide mover recién después de un umbral)
    // lo sigue lord-server en C (wm.beginMove/beginResize, docs/VENTANAS.md); si no se puede (el tema no
    // marcó el nodo de la ventana), con los eventos del puntero como antes
    case 'move': if (win) { wm.focus(id); if (!wm.beginMove(id)) drag = { id, edges: null, start: { x: d.x, y: d.y, f: wm.frame(wm.get(id)) } }; } break;
    case 'resize': if (win) { wm.focus(id); if (!wm.beginResize(id, d.edges || 10)) drag = { id, edges: d.edges || 10, start: { x: d.x, y: d.y, f: wm.frame(wm.get(id)) } }; } break;
    case 'maximize': if (win && !win.maximized) wm.toggleMaximize(id); break;
    case 'unmaximize': if (win && win.maximized) wm.toggleMaximize(id); break;
    case 'minimize': if (win) wm.minimize(id); break;
    default: break;
  }
};

/* el teclado va a la app Linux del frente; maximizada o no, se le avisa */
let lastFocus = -1;
const lastState = new Map();
function sync() {
  if (typeof lord.wayland === 'undefined') return;
  const list = windows.get();
  const vis = list.filter((w) => !w.minimized);
  const top = vis.length ? vis[vis.length - 1] : null;
  const f = top ? surfaceOf(top.id) : 0;
  if (f !== lastFocus) { lastFocus = f; lord.wayland.focus(f); }
  for (const w of list) {
    const s = surfaceOf(w.id);
    if (!s) continue;
    const st = w.maximized ? 1 : 0;
    if (lastState.get(s) !== st) { lastState.set(s, st); lord.wayland.state(s, !!w.maximized, false); }
  }
}
windows.subscribe(sync);

export const wayland = {
  info: () => (typeof lord.wayland !== 'undefined' ? lord.wayland.info() : { socket: '', windows: 0 }),
  isLinuxWindow: (id) => surfaceOf(id) > 0,
};

// Apps .lpk aisladas: su título es el nombre del paquete en el idioma actual; al cambiar de idioma
// se actualiza (refreshPackages recalcula los nombres en el mismo cambio).
locale.subscribe(() => setTimeout(() => {
  for (const w of windows.get()) {
    if (!w.args?.pkg) continue;
    const pkg = getApp(w.args.appId);
    if (pkg && pkg.name) wm.setTitle(w.id, pkg.isolated ? appName(pkg) : pkg.name);
  }
}, 0));

// Compositor: una app .lpk aislada publicó sus menús por el broker. Se ponen en la ventana de esa
// app (linux#<superficie> con app_id = su id) para que el tema los muestre en su barra de menú;
// los clics vuelven a la app por lord.broker.menuClick. Ver docs/PRIVACIDAD.md.
function applyAppMenus(app) {
  if (typeof lord === 'undefined' || !lord.broker || typeof lord.broker.menus !== 'function') return;
  const wins = windows.get().filter((w) => surfaceOf(w.id) > 0 && w.args?.appId === app);   // every window of the app
  if (!wins.length) return;
  let raw;
  try { raw = lord.broker.menus(app); } catch (e) { return; }
  const groups = (Array.isArray(raw) ? raw : []).map((g) => ({
    label: g.label,
    items: (g.items || []).map((it) => {
      if (it === '-' || it == null) return '-';
      const row = { label: it.label, hint: it.hint, disabled: !!it.disabled, checked: !!it.checked };
      if (it.id) row.onClick = () => { try { lord.broker.menuClick(app, it.id); } catch (e) { /* */ } };
      if (it.submenu) row.submenu = it.submenu.map((s) => (s === '-' ? '-' : { label: s.label, hint: s.hint, disabled: !!s.disabled, checked: !!s.checked, onClick: s.id ? () => lord.broker.menuClick(app, s.id) : undefined }));
      return row;
    }),
  }));
  for (const win of wins) wm.setMenus(win.id, groups);
}
if (typeof globalThis !== 'undefined') globalThis.__lordApplyAppMenus = applyAppMenus;

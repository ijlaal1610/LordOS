// trackpad.js — touchpad y pantallas táctiles: ajustes (settings.trackpad → lord.input.configure,
// en vivo) y acciones del escritorio al deslizar con 3 dedos (o 4). Ver docs/TRACKPAD.md.
//
// El tema elige qué hace cada dirección con registerGestureActions({ up, down, left, right }):
// el tema por defecto abre el Launchpad hacia arriba. Sin acción del tema, izquierda/derecha
// cambian de ventana (la de atrás pasa al frente) y arriba/abajo no hacen nada.
import { useEffect } from 'react';
import { addListener } from '@lordos/runtime';
import { createStore, useStore } from './store.js';
import { settings } from './settings.js';
import { windows } from './windows.js';

export const TRACKPAD_DEFAULTS = {
  speed: 0.5,               // velocidad del puntero (0..1)
  tap: true,                // tocar para hacer clic
  tapDrag: true,            // tocar y arrastrar
  natural: true,            // desplazamiento natural (como macOS)
  secondary: 'two-fingers', // clic secundario: 'two-fingers' | 'bottom-right' | 'bottom-left'
  threeDrag: false,         // arrastrar con tres dedos (deslizar pasa a 4 dedos)
  dwt: true,                // ignorar mientras se escribe
  scroll: true,             // scroll con dos dedos
  kinetic: true,            // inercia
  pinch: true,              // pellizcar para hacer zoom
  swipe3: true,             // deslizar con 3 dedos: acciones del escritorio
  swipe4: false,            // deslizar con 4 dedos: acciones del escritorio (si no, van a las apps)
};

const native = () => typeof lord !== 'undefined' && lord.input;
export const trackpadConfig = () => ({ ...TRACKPAD_DEFAULTS, ...(settings.get().trackpad || {}) });

function apply() { if (native()) lord.input.configure(trackpadConfig()); }
apply();
let applied = settings.get().trackpad;
settings.subscribe(() => {
  const now = settings.get().trackpad;
  if (now !== applied) { applied = now; apply(); }
});

// Dispositivos (los presta lord-sessiond al mostrar la sesión en el monitor: se vuelven a mirar).
export const trackpadDevices = createStore(native() ? lord.input.devices() : []);
export function refreshTrackpads() {
  if (!native()) return;
  const list = lord.input.devices();
  if (JSON.stringify(list) !== JSON.stringify(trackpadDevices.get())) trackpadDevices.set(list);
}
export function useTrackpads() {
  useEffect(() => { refreshTrackpads(); const id = setInterval(refreshTrackpads, 3000); return () => clearInterval(id); }, []);
  return useStore(trackpadDevices);
}
export const hasTouchpad = () => trackpadDevices.get().some((d) => d.kind === 'touchpad');

export const setTrackpad = (patch) => settings.set((s) => ({ ...s, trackpad: { ...(s.trackpad || {}), ...patch } }));

// ---- deslizar del escritorio ----

const actions = { up: null, down: null, left: null, right: null };
export function registerGestureActions(map) {
  Object.assign(actions, map);
  return () => { for (const k of Object.keys(map)) if (actions[k] === map[k]) actions[k] = null; };
}

// Cambiar de ventana: izquierda trae la de más atrás; derecha manda la del frente atrás de todo.
function cycle(dir) {
  windows.set((ws) => {
    const vis = ws.filter((w) => !w.minimized && !w.fullscreen && !w.pip);
    if (vis.length < 2) return ws;
    if (dir === 'left') { const back = vis[0]; return [...ws.filter((w) => w !== back), back]; }
    const front = vis[vis.length - 1];
    return [front, ...ws.filter((w) => w !== front)];
  });
}
const DEFAULTS = { left: () => cycle('left'), right: () => cycle('right') };

// Último deslizar reconocido (para la interfaz o las pruebas).
export const lastSwipe = createStore(null);

let acc = null;
addListener('swipe', (e) => {
  if (!e.system) return;
  if (e.phase === 'begin') acc = { x: 0, y: 0, fingers: e.fingers };
  else if (e.phase === 'update' && acc) { acc.x += e.dx; acc.y += e.dy; }
  else if ((e.phase === 'end' || e.phase === 'cancel') && acc) {
    const { x, y, fingers } = acc;
    acc = null;
    if (e.phase === 'cancel' || Math.max(Math.abs(x), Math.abs(y)) < 60) return;
    const dir = Math.abs(x) > Math.abs(y) ? (x < 0 ? 'left' : 'right') : (y < 0 ? 'up' : 'down');
    lastSwipe.set({ dir, fingers, at: lord.now() });
    const fn = actions[dir] || DEFAULTS[dir];
    if (fn) fn({ dir, fingers });
  }
});

export const trackpad = {
  devices: () => trackpadDevices.get(),
  config: trackpadConfig,
  set: setTrackpad,
  refresh: refreshTrackpads,
};

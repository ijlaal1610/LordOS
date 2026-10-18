// events.js — eventos de mouse y teclado que manda lord-server, con burbujeo estilo DOM.
// Props: onMouseDown, onMouseUp, onClick, onDoubleClick, onContextMenu, onMouseMove,
// onMouseEnter, onMouseLeave, onWheel, onPinch, onSwipe.
import { DiscreteEventPriority, ContinuousEventPriority, DefaultEventPriority } from 'react-reconciler/constants';
import { translate, nativeKey, modifiers } from './keymap.js';

const instances = new Map();          // id nativo → instancia del reconciler
const listeners = { pointermove: new Set(), pointerdown: new Set(), pointerup: new Set(), keydown: new Set(), keyup: new Set(), swipe: new Set(), pinch: new Set() };

export const register = (inst) => instances.set(inst.id, inst);
export const unregister = (inst) => instances.delete(inst.id);

// Prioridad de React para el evento en curso (la consulta resolveUpdatePriority): clics y
// teclas son discretos, el movimiento y la rueda continuos; lo demás, la de siempre.
let priority = DefaultEventPriority;
export const currentPriority = () => priority;

export function addListener(type, fn) {
  listeners[type].add(fn);
  return () => listeners[type].delete(fn);
}

function chainOf(id) {
  const out = [];
  for (let n = instances.get(id); n; n = n.parent) out.push(n);
  return out;
}

function dispatch(chain, name, ev) {
  for (const n of chain) {
    const h = n.props[name];
    if (typeof h === 'function') {
      ev.currentTarget = n;
      h(ev);
      if (ev.stopped) break;
    }
  }
}

function makeEvent(type, target, x, y, button, extra) {
  const m = modifiers();
  return {
    type, x, y, button, target, ...extra,
    mods: m, shift: !!(m & 1), alt: !!(m & 2), ctrl: !!(m & 4), meta: !!(m & 8),
    stopped: false,
    stopPropagation() { this.stopped = true; },
    // Coordenadas relativas a la caja de una instancia (ej. ev.local(ev.currentTarget)).
    local(inst) { const [lx, ly] = lord.getLayout(inst.id); return { x: x - lx, y: y - ly }; },
  };
}

let hover = [];
let down = null;
let lastClick = { target: null, time: 0 };

function run(p, fn) {
  priority = p;
  try { fn(); } catch (e) { console.error('evento:', e); } finally { priority = DefaultEventPriority; }
}

globalThis.__lordEvent = (type, id, x, y, button) => {
  const chain = chainOf(id);
  const target = chain[0] || null;
  const fire = (set, ev) => { for (const fn of listeners[set]) fn(ev); };
  if (type === 'move') run(ContinuousEventPriority, () => {
    const now = new Set(chain), before = new Set(hover);
    for (const n of hover) if (!now.has(n)) n.props.onMouseLeave?.(makeEvent('mouseleave', n, x, y, 0));
    for (let i = chain.length - 1; i >= 0; i--) {
      const n = chain[i];
      if (!before.has(n)) n.props.onMouseEnter?.(makeEvent('mouseenter', n, x, y, 0));
    }
    hover = chain;
    dispatch(chain, 'onMouseMove', makeEvent('mousemove', target, x, y, 0));
    fire('pointermove', makeEvent('pointermove', target, x, y, 0));
  });
  else if (type === 'down') run(DiscreteEventPriority, () => {
    down = chain;
    dispatch(chain, 'onMouseDown', makeEvent('mousedown', target, x, y, button));
    fire('pointerdown', makeEvent('pointerdown', target, x, y, button));
  });
  else if (type === 'up') run(DiscreteEventPriority, () => {
    dispatch(chain, 'onMouseUp', makeEvent('mouseup', target, x, y, button));
    fire('pointerup', makeEvent('pointerup', target, x, y, button));
    if (!down) return;
    const pressed = new Set(down);
    const common = chain.filter((n) => pressed.has(n));
    down = null;
    if (button === 2) { dispatch(common, 'onContextMenu', makeEvent('contextmenu', target, x, y, button)); return; }
    if (button !== 0) return;
    dispatch(common, 'onClick', makeEvent('click', target, x, y, button));
    const now = lord.now();
    if (lastClick.target === target && now - lastClick.time < 450) {
      dispatch(common, 'onDoubleClick', makeEvent('dblclick', target, x, y, button));
      lastClick = { target: null, time: 0 };
    } else lastClick = { target, time: now };
  });
  else if (type === 'wheel') run(ContinuousEventPriority, () => {
    dispatch(chain, 'onWheel', makeEvent('wheel', target, x, y, 0, { deltaY: -button * 48, notches: button }));
  });
};

// Con libxkbcommon (server/src/keyboard.c) el servidor ya trae la tecla traducida; si no
// hay distribución XKB cargada llegan solo código y valor y se usa keymap.js de respaldo.
globalThis.__lordKey = (code, value, key, chr, base, flags) => {
  const ev = key === undefined ? translate(code, value) : nativeKey(code, value, key, chr, base, flags);
  run(DiscreteEventPriority, () => { for (const fn of listeners[ev.down ? 'keydown' : 'keyup']) fn(ev); });
};

// Touchpad, pantalla táctil y trackpad del escritorio remoto (server/src/tp, docs/TRACKPAD.md):
//   scroll: onWheel con deltaX/deltaY en píxeles exactos, precise: true y phase ('begin', 'update',
//           'end', 'momentum' mientras sigue por inercia, 'momentum-end'). notches cuenta pasos
//           enteros de 48 px (como una rueda) para las vistas que avanzan de a renglón.
//   pinch:  onPinch({ scale, rotation, x, y, dx, dy, phase }), scale relativa al comienzo.
//   swipe:  onSwipe({ dx, dy, fingers, phase, system }); system = deslizar del escritorio (lo atiende
//           el SDK con las acciones del tema si nadie llama a stopPropagation()).
// Durante un gesto el destino queda fijo (el nodo bajo el puntero al empezar), como en macOS.
const latched = { scroll: null, pinch: null, swipe: null };
let wheelAcc = 0;
globalThis.__lordGesture = (kind, phase, id, x, y, a, b, c, d) => run(ContinuousEventPriority, () => {
  // una rueda (fuente 1) no tiene fases: cada evento va al nodo de ahora
  if (phase === 'begin' || !latched[kind] || (kind === 'scroll' && c === 1)) latched[kind] = chainOf(id);
  const chain = latched[kind];
  const target = chain[0] || null;
  if (kind === 'scroll') {
    if (phase === 'begin') { wheelAcc = 0; return; }
    wheelAcc += b;
    const steps = Math.trunc(wheelAcc / 48);
    wheelAcc -= steps * 48;
    dispatch(chain, 'onWheel', makeEvent('wheel', target, x, y, 0, {
      deltaX: a, deltaY: b, notches: -steps, precise: true, phase, momentum: phase === 'momentum' || phase === 'momentum-end', source: c,
    }));
    if (phase === 'momentum-end' || phase === 'cancel') latched.scroll = null;
  } else if (kind === 'pinch') {
    const ev = makeEvent('pinch', target, x, y, 0, { scale: a, rotation: b, dx: c, dy: d, phase });
    dispatch(chain, 'onPinch', ev);
    if (!ev.stopped) for (const fn of listeners.pinch) fn(ev);
    if (phase === 'end' || phase === 'cancel') latched.pinch = null;
  } else if (kind === 'swipe') {
    const ev = makeEvent('swipe', target, x, y, 0, { dx: a, dy: b, fingers: c, system: !!d, phase });
    dispatch(chain, 'onSwipe', ev);
    if (!ev.stopped) for (const fn of listeners.swipe) fn(ev);
    if (phase === 'end' || phase === 'cancel') latched.swipe = null;
  }
});

// displaysui.jsx — "Identify" for Settings → Displays (docs/DISPLAYS.md, "Numbers, identify and snapping"):
//
//   identifyDisplays(ids?)   shows each display's number (1, 2, 3…; sdk/displaylayout.js displayNumbers),
//                            big, centered on that real display for 1.5 s, then fades it out. `ids` are
//                            lord.display.list() output ids (one or an array); none = every active display.
//                            Returns false when there is nothing to show (no display API, mirror mode).
//   <DisplayIdentify />      the badges; startShell mounts it in the `notification` layer (sdk/index.js,
//                            docs/LAYERS.md), so every theme has it. It never takes clicks.
//
// The desktop is one logical space across displays: each badge is placed at its display's global
// rectangle (lord.screen.displays, matched by connector; the list's own rectangle otherwise).
import { createElement as h, useEffect, useRef } from 'react';
import { View, Text, displays as screenDisplays } from '@lordos/runtime';
import { createStore, useStore } from './store.js';
import { settings } from './settings.js';
import { displays } from './hardware.jsx';
import { displayNumbers } from './displaylayout.js';

export const IDENTIFY_MS = 1500;
const FADE_IN_MS = 120, FADE_OUT_MS = 450;

export const identifying = createStore(null);    // { seq, items: [{ id, n, x, y, w, h }] }
let seq = 0, timer = 0;

export function identifyDisplays(ids) {
  const d = displays.get();
  if (!d || d.mirror) return false;
  const outs = (d.outputs || []).filter((o) => o.active && o.w);
  const nums = displayNumbers(outs);
  let screen = [];
  try { screen = screenDisplays() || []; } catch (e) { screen = []; }
  const want = ids == null ? null : new Set([].concat(ids));
  const items = outs.filter((o) => !want || want.has(o.id)).map((o) => {
    const s = screen.find((x) => x.connector && x.connector === o.connector);
    const r = s && s.w ? s : o;
    return { id: o.id, n: nums[o.id], x: r.x, y: r.y, w: r.w, h: r.h };
  }).filter((it) => it.n && it.w > 0 && it.h > 0);
  if (!items.length) return false;
  clearTimeout(timer);
  identifying.set({ seq: ++seq, items });
  timer = setTimeout(() => identifying.set(null), IDENTIFY_MS);
  return true;
}

// One big rounded badge with the number, centered on its display.
function Badge({ it }) {
  const size = Math.round(Math.max(120, Math.min(240, Math.min(it.w, it.h) * 0.3)));
  return h(View, { style: { position: 'absolute', left: it.x, top: it.y, width: it.w, height: it.h, alignItems: 'center', justifyContent: 'center', pointerEvents: 'none' } },
    h(View, { style: { width: size, height: size, borderRadius: Math.round(size * 0.24), alignItems: 'center', justifyContent: 'center',
      backgroundColor: 'rgba(28,28,32,0.78)', border: '1px solid rgba(255,255,255,0.18)', boxShadow: '0 18px 50px rgba(0,0,0,0.45)', pointerEvents: 'none' } },
    h(Text, { style: { fontSize: Math.round(size * 0.6), fontWeight: 700, color: '#ffffff', textAlign: 'center', pointerEvents: 'none' } }, String(it.n))));
}

export function DisplayIdentify() {
  const c = useStore(identifying);
  const reduce = useStore(settings, (s) => !!s.reduceMotion);
  const root = useRef(null);
  useEffect(() => {
    if (!c || reduce || typeof lord === 'undefined' || !lord.setStyle) return undefined;
    // opacity through lord.setStyle (no React render per frame): in quickly, hold, out at the end
    const t0 = Date.now();
    const set = (o) => { try { if (root.current) lord.setStyle(root.current.id, 'opacity', o); } catch (e) { /* unmounted */ } };
    set(0);
    const id = setInterval(() => {
      const t = Date.now() - t0;
      const o = t < FADE_IN_MS ? t / FADE_IN_MS : t > IDENTIFY_MS - FADE_OUT_MS ? Math.max(0, (IDENTIFY_MS - t) / FADE_OUT_MS) : 1;
      set(Math.round(o * 100) / 100);
      if (t >= IDENTIFY_MS) clearInterval(id);
    }, 33);
    return () => clearInterval(id);
  }, [c && c.seq, reduce]);
  if (!c) return null;
  return h(View, { ref: root, key: c.seq, style: { position: 'absolute', left: 0, top: 0, right: 0, bottom: 0, pointerEvents: 'none' } },
    ...c.items.map((it) => h(Badge, { key: it.id, it })));
}

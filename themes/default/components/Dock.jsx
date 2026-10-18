// Dock with magnification by mouse position, tooltip and open-app dot. It can go at the bottom,
// on the left or on the right (Settings → Desktop & Dock), auto-hide and show recent
// apps. Left and right are physical sides of the screen, also in Arabic or Hebrew (as
// on popular desktop systems): the Dock is laid out with direction: 'ltr' and only the bottom row reverses its order in RTL.
import { View, Text, screen, appName, useLocale, addListener, t as tr } from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';

const GAP = 8, PAD = 9, MARGIN = 6;
const SEP = GAP + 1;          // separator before the recent apps
const SLIDE_MS = 180;

// What the Dock takes up from its screen edge (for the work area).
export const dockThickness = (size) => size + PAD * 2 + MARGIN + 4;

export function LaunchpadIcon({ size: s }) {
  const colors = ['#ff5f57', '#febc2e', '#28c840', '#0a84ff', '#bf5af2', '#ff9f0a', '#30d158', '#ff375f', '#64d2ff'];
  const d = s * 0.14, g = s * 0.08, o = (s - 3 * d - 2 * g) / 2;
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(180deg, #5b5b66, #2b2b33)',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)` }}>
      {colors.map((c, i) => (
        <View key={i} style={{ position: 'absolute', left: o + (i % 3) * (d + g), top: o + Math.floor(i / 3) * (d + g),
          width: d, height: d, borderRadius: d * 0.3, backgroundColor: c }} />
      ))}
    </View>
  );
}

// Auto-hide: shows when the pointer touches the edge (after a short delay) and hides
// when it leaves the Dock. State only changes when crossing those boundaries: moving the mouse around the rest of the
// screen doesn't redraw anything. The animation belongs to this component, not the desktop.
function useAutohide(enabled, position, rectRef, keepOpen, areaRef) {
  const [p, setP] = useState(enabled ? 0 : 1);     // 0 hidden … 1 in view
  const want = useRef(!enabled);
  const keep = useRef(keepOpen);
  keep.current = keepOpen;
  useEffect(() => {
    if (!enabled) { want.current = true; setP(1); return undefined; }
    want.current = false;
    setP(0);
    let timer = 0, raf = 0, from = 0, t0 = 0;
    const animate = (to) => {
      cancelAnimationFrame(raf);
      t0 = performance.now();
      const step = (now) => {
        const k = Math.min(1, (now - t0) / SLIDE_MS), e = 1 - (1 - k) * (1 - k);
        setP(from + (to - from) * e);
        if (k < 1) raf = requestAnimationFrame(step);
        else from = to;
      };
      raf = requestAnimationFrame(step);
    };
    const set = (on) => {
      if (want.current === on) return;
      want.current = on;
      from = on ? 0 : 1;
      animate(on ? 1 : 0);
    };
    const off = addListener('pointermove', (e) => {
      // the edge of the display the Dock is on (the main one: docs/DISPLAYS.md)
      const A = areaRef.current, inX = e.x >= A.x && e.x < A.x + A.w, inY = e.y >= A.y && e.y < A.y + A.h;
      const edge = position === 'bottom' ? inX && e.y >= A.y + A.h - 2 : position === 'left' ? inY && e.x <= A.x + 1 : inY && e.x >= A.x + A.w - 2;
      const r = rectRef.current;
      const over = r && e.x >= r.x0 - 8 && e.x < r.x1 + 8 && e.y >= r.y0 - 8 && e.y < r.y1 + 8;
      if (!want.current) {
        if (edge && !timer) timer = setTimeout(() => { timer = 0; set(true); }, 120);
        else if (!edge && timer) { clearTimeout(timer); timer = 0; }
      } else if (!over && !keep.current) {
        if (!timer) timer = setTimeout(() => { timer = 0; if (!keep.current) set(false); }, 500);
      } else if (timer) { clearTimeout(timer); timer = 0; }
    });
    return () => { off(); clearTimeout(timer); cancelAnimationFrame(raf); };
  }, [enabled, position]);
  return p;
}

export function Dock({ t, items: list, running, onOpen, onContext, position = 'bottom', size = 54, magnify = true, maxSize = 86,
  autohide = false, top = 0, keepOpen = false, area = { x: 0, y: 0, w: screen.width, h: screen.height } }) {
  // area: the display it is on (global coordinates); everything below is placed inside it
  const vert = position !== 'bottom';
  const [m, setM] = useState(null);             // mouse position along the Dock's axis
  const { dir } = useLocale();
  // The magnifier computes positions with the mouse coordinate: in Arabic/Hebrew the bottom row reverses
  // the list (the visual result users expect); on the side the order is top to bottom in all of them.
  const items = !vert && dir === 'rtl' ? [...list].reverse() : list;
  const n = items.length;
  const sepAt = items.findIndex((it) => it.recent);
  const sep = sepAt > 0 ? SEP : 0;
  const origin = vert ? area.y + top : area.x, avail = vert ? area.h - top : area.w;
  const AX = area.x, AY = area.y, AR = area.x + area.w, AB = area.y + area.h;
  // with many apps the icons shrink so the Dock fits on screen
  // (it accounts for magnifier growth: about 2.6 extra icons in total)
  const grow = magnify ? 2.6 * Math.max(0, maxSize / size - 1) : 0;
  const fit = Math.floor((avail - 2 * PAD - 16 - (n - 1) * GAP - sep) / (Math.max(1, n) + grow));
  const BASE = Math.max(20, Math.min(size, fit));
  const MAX = magnify ? Math.max(BASE, Math.round(maxSize * BASE / size)) : BASE, RANGE = BASE * 3;
  const baseLen = n * BASE + (n - 1) * GAP + sep;
  const baseStart = origin + (avail - baseLen) / 2;
  const centers = items.map((_, i) => baseStart + i * (BASE + GAP) + (sepAt > 0 && i >= sepAt ? sep : 0) + BASE / 2);
  const sizes = centers.map((c) => {
    if (m == null || MAX === BASE) return BASE;
    const d = Math.abs(m - c) / RANGE;
    return d < 1 ? Math.round(BASE + (MAX - BASE) * Math.cos((d * Math.PI) / 2)) : BASE;
  });
  const hovered = m == null ? -1 : centers.findIndex((c) => Math.abs(m - c) <= (BASE + GAP) / 2);
  const len = sizes.reduce((a, b) => a + b, 0) + GAP * (n - 1) + sep + PAD * 2;
  const start = Math.max(origin, origin + (avail - len) / 2);
  let x = PAD;
  const iconC = sizes.map((s, i) => { if (i === sepAt && sep) x += sep; const c = x + s / 2; x += s + GAP; return c; });
  const bar = BASE + PAD * 2;

  // where the bar is on screen (for auto-hide) and how far it moves when hiding
  const rect = useRef(null);
  rect.current = position === 'bottom' ? { x0: start, x1: start + len, y0: AB - MARGIN - bar, y1: AB }
    : position === 'left' ? { x0: AX, x1: AX + MARGIN + bar, y0: start, y1: start + len }
      : { x0: AR - MARGIN - bar, x1: AR, y0: start, y1: start + len };
  const areaRef = useRef(area);
  areaRef.current = area;
  const shown = useAutohide(autohide, position, rect, keepOpen || hovered >= 0, areaRef);
  const hide = Math.round((1 - shown) * (bar + MARGIN + 2));
  if (autohide && shown <= 0) return null;

  // The hit box covers only the bar until the pointer is on it; then it grows to fit the magnified
  // icons and the tooltip. A full-height box at rest would swallow clicks on windows just above the Dock.
  const BH = m != null ? MAX + PAD * 2 + 34 : bar, BW = m != null ? MAX + PAD * 2 : bar;
  const box = position === 'bottom' ? { left: start, top: AB - (MARGIN - hide) - BH, width: len, height: BH }
    : position === 'left' ? { left: AX + MARGIN - hide, top: start, width: BW, height: len }
      : { left: AR - (MARGIN - hide) - BW, top: start, width: BW, height: len };
  const back = position === 'bottom' ? { left: 0, right: 0, bottom: 0, height: bar }
    : position === 'left' ? { left: 0, top: 0, bottom: 0, width: bar } : { right: 0, top: 0, bottom: 0, width: bar };
  const strip = position === 'bottom' ? { left: PAD, bottom: PAD, flexDirection: 'row', alignItems: 'flex-end' }
    : position === 'left' ? { left: PAD, top: PAD, flexDirection: 'column', alignItems: 'flex-start' }
      : { right: PAD, top: PAD, flexDirection: 'column', alignItems: 'flex-end' };
  const dot = (s) => (position === 'bottom' ? { left: s / 2 - 2, bottom: -7 } : position === 'left' ? { top: s / 2 - 2, left: -7 } : { top: s / 2 - 2, right: -7 });
  const dotColor = t.mode === 'dark' ? 'rgba(255,255,255,0.85)' : 'rgba(0,0,0,0.7)';
  // where to open the right-click menu and the tooltip: beside the icon, on the inner side
  const anchor = (i) => (position === 'bottom' ? { x: start + iconC[i], y: AB - MARGIN - bar - 6 }
    : position === 'left' ? { x: AX + MARGIN + bar + 8, y: start + iconC[i] } : { x: AR - MARGIN - bar - 8, y: start + iconC[i] });
  const label = hovered >= 0 ? (items[hovered].id === 'launchpad' ? tr('shell.menu.apps') : appName(items[hovered])) : '';
  const tip = (
    <View style={{ paddingHorizontal: 10, paddingVertical: 4, borderRadius: 7, backgroundColor: t.panelBg,
      backdropFilter: `blur(${t.blur}px)`, border: `1px solid ${t.windowBorder}` }}>
      <Text style={{ fontSize: 12, fontWeight: 500, color: t.text, whiteSpace: 'nowrap' }}>{label}</Text>
    </View>
  );

  return (
    <>
      <View onMouseMove={(e) => setM(vert ? e.y : e.x)} onMouseLeave={() => setM(null)}
        style={{ position: 'absolute', ...box, direction: 'ltr' }}>
        <View style={{ position: 'absolute', ...back, borderRadius: 20,
          backgroundColor: t.dockBg, backdropFilter: `blur(${t.blur}px)`, border: `1px solid ${t.dockBorder}` }} />
        <View style={{ position: 'absolute', ...strip, gap: GAP }}>
          {items.flatMap((app, i) => [
            i === sepAt && sep ? (
              <View key="sep" style={vert ? { height: 1, width: BASE * 0.7, alignSelf: 'center', backgroundColor: t.dockBorder }
                : { width: 1, height: BASE * 0.7, alignSelf: 'center', backgroundColor: t.dockBorder }} />
            ) : null,
            <View key={app.id} onClick={() => onOpen(app.id)} onContextMenu={() => onContext?.(app.id, anchor(i))}
              style={{ width: sizes[i], height: sizes[i] }}>
              <app.Icon size={sizes[i]} />
              {running.includes(app.id) && (
                <View style={{ position: 'absolute', ...dot(sizes[i]), width: 4, height: 4, borderRadius: 2, backgroundColor: dotColor }} />
              )}
            </View>,
          ]).filter(Boolean)}
        </View>
        {hovered >= 0 && position === 'bottom' && (
          <View style={{ position: 'absolute', left: iconC[hovered] - 90, width: 180, bottom: sizes[hovered] + PAD + 12,
            alignItems: 'center', pointerEvents: 'none' }}>{tip}</View>
        )}
      </View>
      {hovered >= 0 && vert && (
        <View style={{ position: 'absolute', top: start + iconC[hovered] - 13, pointerEvents: 'none', direction: 'ltr',
          ...(position === 'left' ? { left: AX + MARGIN - hide + PAD + sizes[hovered] + 14 } : { right: screen.width - AR + MARGIN - hide + PAD + sizes[hovered] + 14 }) }}>{tip}</View>
      )}
    </>
  );
}

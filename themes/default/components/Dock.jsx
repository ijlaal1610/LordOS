// Dock con magnificación según la posición del mouse, tooltip y punto de app abierta. Puede ir abajo,
// a la izquierda o a la derecha (Ajustes → Escritorio y Dock), esconderse solo y mostrar las apps
// recientes. Izquierda y derecha son lados físicos de la pantalla, también en árabe o hebreo (como
// macOS): el Dock se maqueta con direction: 'ltr' y solo la fila de abajo invierte el orden en RTL.
import { View, Text, screen, appName, useLocale, addListener } from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';

const GAP = 8, PAD = 9, MARGIN = 6;
const SEP = GAP + 1;          // separador antes de las apps recientes
const SLIDE_MS = 180;

// Lo que ocupa el Dock desde su borde de la pantalla (para el área de trabajo).
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

// Esconder solo: se muestra cuando el puntero toca el borde (con una pequeña espera) y se esconde
// cuando sale del Dock. Solo cambia el estado al cruzar esos límites: mover el mouse por el resto de la
// pantalla no vuelve a dibujar nada. La animación es de este componente, no del escritorio.
function useAutohide(enabled, position, rectRef, keepOpen) {
  const [p, setP] = useState(enabled ? 0 : 1);     // 0 escondido … 1 a la vista
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
      const edge = position === 'bottom' ? e.y >= screen.height - 2 : position === 'left' ? e.x <= 1 : e.x >= screen.width - 2;
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
  autohide = false, top = 0, keepOpen = false }) {
  const vert = position !== 'bottom';
  const [m, setM] = useState(null);             // posición del mouse sobre el eje del Dock
  const { dir } = useLocale();
  // La lupa calcula posiciones con la coordenada del mouse: en árabe/hebreo la fila de abajo invierte
  // la lista (mismo resultado visual que macOS); de costado el orden es de arriba abajo en todos.
  const items = !vert && dir === 'rtl' ? [...list].reverse() : list;
  const n = items.length;
  const sepAt = items.findIndex((it) => it.recent);
  const sep = sepAt > 0 ? SEP : 0;
  const origin = vert ? top : 0, avail = vert ? screen.height - top : screen.width;
  // con muchas apps los íconos se achican para que el Dock entre en la pantalla (como macOS)
  // (cuenta lo que crece con la lupa: unos 2,6 íconos de más en total)
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

  // dónde está la barra en pantalla (para esconderse solo) y cuánto se corre al esconderse
  const rect = useRef(null);
  rect.current = position === 'bottom' ? { x0: start, x1: start + len, y0: screen.height - MARGIN - bar, y1: screen.height }
    : position === 'left' ? { x0: 0, x1: MARGIN + bar, y0: start, y1: start + len }
      : { x0: screen.width - MARGIN - bar, x1: screen.width, y0: start, y1: start + len };
  const shown = useAutohide(autohide, position, rect, keepOpen || hovered >= 0);
  const hide = Math.round((1 - shown) * (bar + MARGIN + 2));
  if (autohide && shown <= 0) return null;

  const box = position === 'bottom' ? { left: start, bottom: MARGIN - hide, width: len, height: MAX + PAD * 2 + 34 }
    : position === 'left' ? { left: MARGIN - hide, top: start, width: MAX + PAD * 2, height: len }
      : { right: MARGIN - hide, top: start, width: MAX + PAD * 2, height: len };
  const back = position === 'bottom' ? { left: 0, right: 0, bottom: 0, height: bar }
    : position === 'left' ? { left: 0, top: 0, bottom: 0, width: bar } : { right: 0, top: 0, bottom: 0, width: bar };
  const strip = position === 'bottom' ? { left: PAD, bottom: PAD, flexDirection: 'row', alignItems: 'flex-end' }
    : position === 'left' ? { left: PAD, top: PAD, flexDirection: 'column', alignItems: 'flex-start' }
      : { right: PAD, top: PAD, flexDirection: 'column', alignItems: 'flex-end' };
  const dot = (s) => (position === 'bottom' ? { left: s / 2 - 2, bottom: -7 } : position === 'left' ? { top: s / 2 - 2, left: -7 } : { top: s / 2 - 2, right: -7 });
  const dotColor = t.mode === 'dark' ? 'rgba(255,255,255,0.85)' : 'rgba(0,0,0,0.7)';
  // dónde abrir el menú del clic derecho y el tooltip: al costado del ícono, del lado de adentro
  const anchor = (i) => (position === 'bottom' ? { x: start + iconC[i], y: screen.height - MARGIN - bar - 6 }
    : position === 'left' ? { x: MARGIN + bar + 8, y: start + iconC[i] } : { x: screen.width - MARGIN - bar - 8, y: start + iconC[i] });
  const label = hovered >= 0 ? (items[hovered].id === 'launchpad' ? 'Launchpad' : appName(items[hovered])) : '';
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
          ...(position === 'left' ? { left: MARGIN - hide + PAD + sizes[hovered] + 14 } : { right: MARGIN - hide + PAD + sizes[hovered] + 14 }) }}>{tip}</View>
      )}
    </>
  );
}

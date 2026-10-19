// Task view: every window of the main display laid out in a grid, at a smaller scale, over a dimmed
// desktop. The windows themselves are drawn smaller (a transform on their frame, which lord-server
// paints from a cached raster: no app redraws), so what you see is live. Click one to bring it to
// the front; Escape, a click on the desktop or the taskbar button closes it. ←/→ and Enter work too.
import { View, Text, addListener, t as tr, windowTitle, windowIcon, wm } from '@lordos/sdk';
import { useEffect } from 'react';
import { CloseGlyph } from '../glyphs.jsx';

const PAD = 48, GAP = 28, LABEL = 30;

// id → { x, y, scale, w, h } (screen coordinates of the scaled frame)
export function taskViewLayout(list, a) {
  const out = new Map();
  const n = list.length;
  if (!n) return out;
  const cols = Math.ceil(Math.sqrt(n * (a.w / Math.max(1, a.h)) / 1.6));
  const c = Math.max(1, Math.min(n, cols)), r = Math.ceil(n / c);
  const cw = (a.w - 2 * PAD - GAP * (c - 1)) / c, ch = (a.h - 2 * PAD - GAP * (r - 1)) / r;
  list.forEach((w, i) => {
    const col = i % c, row = Math.floor(i / c);
    const inRow = row === r - 1 ? n - row * c : c;            // the last row is centered
    const rowLeft = a.x + PAD + ((c - inRow) * (cw + GAP)) / 2;
    const scale = Math.min(0.85, cw / w.w, (ch - LABEL) / w.h);
    const sw = w.w * scale, sh = w.h * scale;
    out.set(w.id, {
      x: Math.round(rowLeft + col * (cw + GAP) + (cw - sw) / 2),
      y: Math.round(a.y + PAD + row * (ch + GAP) + LABEL + (ch - LABEL - sh) / 2),
      scale, w: sw, h: sh,
    });
  });
  return out;
}

export function TaskViewBackdrop({ area, onClose, empty }) {
  return (
    <View onClick={onClose} style={{ position: 'absolute', left: area.x, top: area.y, width: area.w, height: area.h,
      backgroundColor: 'rgba(8,10,16,0.45)', alignItems: 'center', justifyContent: 'center' }}>
      {empty ? <Text style={{ fontSize: 15, color: '#fff' }}>{tr('shell.classic.noWindows')}</Text> : null}
    </View>
  );
}

// The title above each scaled window, with a close button.
export function TaskViewLabels({ t, list, layout, hover, setHover }) {
  return list.map((w) => {
    const p = layout.get(w.id);
    if (!p) return null;
    const Icon = windowIcon(w);
    return (
      <View key={w.id} onMouseEnter={() => setHover(w.id)}
        style={{ position: 'absolute', left: p.x, top: p.y - LABEL, width: p.w, height: LABEL - 4, flexDirection: 'row', alignItems: 'center', gap: 8,
          paddingHorizontal: 6, borderRadius: 6, backgroundColor: hover === w.id ? t.panelBg : 'transparent' }}>
        {Icon ? <Icon size={16} /> : null}
        <Text style={{ flex: 1, fontSize: 12, color: hover === w.id ? t.text : '#ffffff', whiteSpace: 'nowrap', textOverflow: 'ellipsis', textAlign: 'start' }}>{windowTitle(w)}</Text>
        {hover === w.id ? (
          <View onClick={() => wm.close(w.id)} style={{ width: 22, height: 22, borderRadius: 4, alignItems: 'center', justifyContent: 'center', backgroundColor: t.closeHover }}>
            <CloseGlyph color="#ffffff" size={9} />
          </View>
        ) : null}
      </View>
    );
  });
}

// Keyboard while the task view is open.
export function useTaskViewKeys(open, list, hover, setHover, pick, close) {
  useEffect(() => {
    if (!open) return undefined;
    return addListener('keydown', (e) => {
      if (e.key === 'Escape') { close(); return; }
      if (!list.length) return;
      const i = Math.max(0, list.findIndex((w) => w.id === hover));
      if (e.key === 'ArrowRight' || e.key === 'Tab') setHover(list[(i + 1) % list.length].id);
      else if (e.key === 'ArrowLeft') setHover(list[(i - 1 + list.length) % list.length].id);
      else if (e.key === 'Enter' && hover) pick(hover);
    });
  }, [open, list.map((w) => w.id).join(','), hover]);
}

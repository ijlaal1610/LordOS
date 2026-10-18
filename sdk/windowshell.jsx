// windowshell.jsx — <WindowShell>: a window's node with invisible borders for resizing
// on all 4 sides and 4 corners, 6 px outside the frame and 3 px inside, with the
// cursor for each direction. Moving and resizing is done by lord-server in C (wm.beginMove/beginResize,
// docs/WINDOWS.md): the theme doesn't receive an event per frame.
//
//   <WindowShell win={w} style={{ borderRadius: 12, backgroundColor, overflow: 'hidden', boxShadow }}
//                onMouseDown={() => wm.focus(w.id)}>
//     <View onMouseDown={(e) => wm.beginMove(w.id)}>…title bar…</View>
//     <AppHost id={w.id} focused={focused} />
//   </WindowShell>
import { View } from '@lordos/runtime';
import { wm } from './windows.js';

export const WINDOW_EDGE = 6;     // px of border outside the frame
const INSIDE = 3;                 // and inside
const CORNER = 16;

// [xdg edges, cursor, style]
const GRIPS = [
  [1, 'ns-resize', (t) => ({ left: CORNER, right: CORNER, top: 0, height: t })],
  [2, 'ns-resize', (t) => ({ left: CORNER, right: CORNER, bottom: 0, height: t })],
  [4, 'ew-resize', (t) => ({ top: CORNER, bottom: CORNER, left: 0, width: t })],
  [8, 'ew-resize', (t) => ({ top: CORNER, bottom: CORNER, right: 0, width: t })],
  [5, 'nwse-resize', () => ({ left: 0, top: 0, width: CORNER, height: CORNER })],
  [10, 'nwse-resize', () => ({ right: 0, bottom: 0, width: CORNER, height: CORNER })],
  [9, 'nesw-resize', () => ({ right: 0, top: 0, width: CORNER, height: CORNER })],
  [6, 'nesw-resize', () => ({ left: 0, bottom: 0, width: CORNER, height: CORNER })],
];

export function WindowShell({ win, style, children, onMouseDown, resizable = true }) {
  const geo = wm.frame(win);
  const m = resizable && !win.maximized ? WINDOW_EDGE : 0;
  return (
    <View ref={wm.frameRef(win.id, m)} onMouseDown={onMouseDown}
      style={{ position: 'absolute', left: geo.x - m, top: geo.y - m, width: geo.w + 2 * m, height: geo.h + 2 * m }}>
      <View style={{ position: 'absolute', left: m, top: m, right: m, bottom: m, ...style }}>{children}</View>
      {m > 0 && GRIPS.map(([edges, cursor, at]) => (
        <View key={edges} style={{ position: 'absolute', cursor, ...at(m + INSIDE) }}
          onMouseDown={(e) => { e.stopPropagation(); wm.focus(win.id); wm.beginResize(win.id, edges); }} />
      ))}
    </View>
  );
}

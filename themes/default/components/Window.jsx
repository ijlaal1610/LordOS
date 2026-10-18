// Window frame: draggable title bar, traffic lights, shadow. The content is the app
// (the SDK's AppHost); the theme only decides how the frame looks. Moving and resizing (invisible
// borders on all 4 sides and 4 corners, <WindowShell>) is done by lord-server in C
// (docs/WINDOWS.md); the JS drag below is only kept for a maximized window.
import { View, Text, addListener, wm, AppHost, windowTitle, WindowShell } from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';

const TITLE_H = 40;
const LIGHTS = [
  { key: 'close', color: '#ff5f57', glyph: '×', act: (id) => wm.close(id) },
  { key: 'min', color: '#febc2e', glyph: '−', act: (id) => wm.minimize(id) },
  { key: 'max', color: '#28c840', glyph: '+', act: (id) => wm.toggleMaximize(id) },
];

export function Window({ win, app, t, focused }) {
  const drag = useRef(null);
  const [hover, setHover] = useState(false);
  const geo = wm.frame(win);

  useEffect(() => {
    const offMove = addListener('pointermove', (e) => {
      if (drag.current) wm.move(win.id, e.x - drag.current.dx, e.y - drag.current.dy);
    });
    const offUp = addListener('pointerup', () => { drag.current = null; });
    return () => { offMove(); offUp(); };
  }, [win.id]);

  // Linux app that draws its own frame (GTK, Chromium): only the content, with the theme's shadow.
  if (win.args?.csd) {
    return (
      <WindowShell win={win} onMouseDown={() => wm.focus(win.id)}
        style={{ boxShadow: focused ? '0 24px 56px rgba(0,0,0,0.5)' : '0 10px 28px rgba(0,0,0,0.3)' }}>
        <AppHost id={win.id} focused={focused} />
      </WindowShell>
    );
  }

  return (
    <WindowShell win={win} onMouseDown={() => wm.focus(win.id)} style={{
      borderRadius: win.maximized ? 0 : 12, backgroundColor: app.transparent ? 'transparent' : t.windowBg, overflow: 'hidden',
      border: `1px solid ${t.windowBorder}`,
      boxShadow: focused ? '0 24px 56px rgba(0,0,0,0.55)' : '0 10px 28px rgba(0,0,0,0.32)',
    }}>
      <View onMouseDown={(e) => { if (!wm.beginMove(win.id)) drag.current = { dx: e.x - geo.x, dy: e.y - geo.y }; }}
        onDoubleClick={() => wm.toggleMaximize(win.id)}
        style={{ height: TITLE_H, flexDirection: 'row', alignItems: 'center', paddingLeft: 14, backgroundColor: t.titleBg }}>
        <Text style={{ position: 'absolute', left: 0, right: 0, top: 0, lineHeight: TITLE_H, textAlign: 'center',
          fontSize: 13, fontWeight: 600, color: focused ? t.text : t.textDim, pointerEvents: 'none', whiteSpace: 'nowrap',
          paddingHorizontal: 80 }}>{windowTitle(win)}</Text>
        <View onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)}
          style={{ flexDirection: 'row', gap: 8, paddingVertical: 6 }}>
          {LIGHTS.map((l) => (
            <View key={l.key} onMouseDown={(e) => e.stopPropagation()} onClick={() => l.act(win.id)}
              style={{ width: 13, height: 13, borderRadius: 7, alignItems: 'center', justifyContent: 'center',
                backgroundColor: focused || hover ? l.color : t.lightOff }}>
              {hover && <Text style={{ fontSize: 11, fontWeight: 700, lineHeight: 13, color: 'rgba(0,0,0,0.6)' }}>{l.glyph}</Text>}
            </View>
          ))}
        </View>
      </View>
      <View style={{ height: 1, backgroundColor: t.separator }} />
      <View style={{ flex: 1 }}><AppHost id={win.id} focused={focused} /></View>
    </WindowShell>
  );
}

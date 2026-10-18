// Launchpad: all apps in full screen, with keyboard search.
// An app is pinned to the Dock by dragging it to the bottom strip or with a right click.
import { View, Text, TextInput, ScrollView, screen, t as tr, appName, APP_CATEGORIES, appCategory, addListener, dock, useStore, settings } from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';
import { Menu } from './Menu.jsx';

const DROP_H = 110;              // bottom strip where you drop to pin to the Dock

const COLS = 6, CELL = 130, ICON = 72;

// area: the display it covers (the main one; global coordinates)
export function Launchpad({ t, apps, onOpen, onClose, area = { x: 0, y: 0, w: screen.width, h: screen.height } }) {
  const A = useRef(area);
  A.current = area;
  const [query, setQuery] = useState('');
  const [cat, setCat] = useState(null);                      // null: all
  const q = query.trim().toLowerCase();
  const match = (a) => [appName(a), a.name, a.id, a.genericName, a.keywords, a.comment].some((v) => v && String(v).toLowerCase().includes(q));
  const present = APP_CATEGORIES.filter((c) => apps.some((a) => appCategory(a) === c));
  const list = apps.filter((a) => (!q || match(a)) && (q || !cat || appCategory(a) === cat));
  const rows = [];
  for (let i = 0; i < list.length; i += COLS) rows.push(list.slice(i, i + COLS));
  const open = (id) => { onClose(); onOpen(id); };
  useStore(settings);                                         // «Keep in Dock» changes immediately
  const [ctx, setCtx] = useState(null);                       // { app, x, y }
  const [drag, setDrag] = useState(null);                     // { app, x, y }
  const press = useRef(null);
  const dragged = useRef(false);
  useEffect(() => {
    const offMove = addListener('pointermove', (e) => {
      const p = press.current;
      if (!p) return;
      if (!dragged.current && Math.hypot(e.x - p.x, e.y - p.y) < 8) return;
      dragged.current = true;
      setDrag({ app: p.app, x: e.x, y: e.y });
    });
    const offUp = addListener('pointerup', (e) => {
      const p = press.current;
      press.current = null;
      if (p && dragged.current && e.y > A.current.y + A.current.h - DROP_H) dock.pin(p.app.id);
      setDrag(null);
      setTimeout(() => { dragged.current = false; }, 0);
    });
    return () => { offMove(); offUp(); };
  }, []);

  return (
    <View onClick={onClose} style={{
      position: 'absolute', left: area.x, top: area.y, width: area.w, height: area.h, alignItems: 'center',
      backgroundColor: t.effects ? 'rgba(10,10,16,0.35)' : 'rgba(18,18,24,0.94)', backdropFilter: `blur(${t.effects ? 32 : 0}px)`,
    }}>
      <View onClick={(e) => e.stopPropagation()} style={{ marginTop: 48, width: 260 }}>
        <TextInput value={query} onChange={setQuery} placeholder={tr('shell.launchpad.search')} autoFocus
          onSubmit={() => list[0] && open(list[0].id)} onCancel={onClose}
          style={{ backgroundColor: 'rgba(255,255,255,0.14)', border: '1px solid rgba(255,255,255,0.22)' }} />
      </View>
      {!q && (
        <View onClick={(e) => e.stopPropagation()} style={{ marginTop: 22, flexDirection: 'row', gap: 6 }}>
          {[null, ...present].map((c) => (
            <View key={c || 'all'} onClick={() => setCat(c)} style={{ paddingHorizontal: 12, paddingVertical: 5, borderRadius: 14,
              backgroundColor: c === cat ? 'rgba(255,255,255,0.32)' : 'rgba(255,255,255,0.1)' }}>
              <Text style={{ fontSize: 12, fontWeight: 600, color: '#fff' }}>{c ? tr(`shell.category.${c}`) : tr('shell.category.all')}</Text>
            </View>
          ))}
        </View>
      )}
      <ScrollView onClick={(e) => e.stopPropagation()} style={{ marginTop: q ? 40 : 14, height: area.h - 190, width: Math.min(COLS * CELL, area.w - 20) }}>
      <View style={{ gap: 26, paddingVertical: 16 }}>
        {rows.map((row, r) => (
          <View key={r} style={{ flexDirection: 'row' }}>
            {row.map((a) => (
              <View key={a.id} onClick={(e) => { e.stopPropagation(); if (!dragged.current) open(a.id); }}
                onMouseDown={(e) => { press.current = { app: a, x: e.x, y: e.y }; dragged.current = false; }}
                onContextMenu={(e) => { e.stopPropagation(); setCtx({ app: a, x: e.x, y: e.y }); }}
                style={{ width: CELL, alignItems: 'center', gap: 10 }}>
                <a.Icon size={ICON} />
                <Text style={{ fontSize: 13, fontWeight: 500, color: '#fff', whiteSpace: 'nowrap', maxWidth: CELL - 10, overflow: 'hidden' }}>{appName(a)}</Text>
              </View>
            ))}
          </View>
        ))}
        {list.length === 0 && <Text style={{ fontSize: 14, color: 'rgba(255,255,255,0.7)', textAlign: 'center' }}>{tr('shell.launchpad.empty')}</Text>}
      </View>
      </ScrollView>
      {drag && (
        <View style={{ position: 'absolute', left: 0, right: 0, bottom: 0, height: DROP_H, alignItems: 'center', justifyContent: 'center',
          backgroundColor: drag.y > area.y + area.h - DROP_H ? 'rgba(255,255,255,0.22)' : 'rgba(255,255,255,0.08)', pointerEvents: 'none' }}>
          <Text style={{ fontSize: 13, fontWeight: 600, color: '#fff' }}>{tr('shell.dock.dropHere')}</Text>
        </View>
      )}
      {drag && (
        <View style={{ position: 'absolute', left: drag.x - area.x - ICON / 2, top: drag.y - area.y - ICON / 2, opacity: 0.85, pointerEvents: 'none' }}>
          <drag.app.Icon size={ICON} />
        </View>
      )}
      {ctx && (
        <Menu x={ctx.x - area.x} y={ctx.y - area.y} onClose={() => setCtx(null)} items={[
          { label: tr('shell.dock.open'), onClick: () => open(ctx.app.id) },
          { label: tr('shell.dock.keep'), checked: dock.isPinned(ctx.app.id),
            onClick: () => (dock.isPinned(ctx.app.id) ? dock.unpin(ctx.app.id) : dock.pin(ctx.app.id)) },
        ]} />
      )}
    </View>
  );
}

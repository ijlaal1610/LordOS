// "Classic" theme — TEMPLATE for creating new themes. The whole desktop in one file:
//   1. tokens (colors that apps also use via useTheme)
//   2. window frame
//   3. taskbar + start menu
//   4. App: wallpaper + windows + bar
// Copy this folder, change theme.json and start modifying.
import {
  startShell, settings, View, Text, screen, addListener, ThemeProvider, useSettings, useWindows, useClock,
  configureWindows, getApp, wm, AppHost, session, windowTitle, t as tr, formatTime,
  ScrollView, useLauncherApps, launchApp, windowIcon, APP_CATEGORIES, appCategory, useDockEntries, dockActivate, dockMenu,
  DesktopWidgets, PrivacyIndicators, registerGestureActions, WindowShell, StatusItems, dockPrefs, mainDisplay,
  useStore, dock, dockIdOf, dockDrag, dockPoof, beginDockDrag, registerDockTarget, dockDragJustEnded, useTrash, openTrash, emptyTrash, trashMenu,
  refreshTrash, ConfirmDialog, takeTextFocus, releaseTextFocus, Layer, PowerDialog,
} from '@lordos/sdk';
import { apps } from '@lordos/apps';
import { useEffect, useRef, useState } from 'react';

// ---- 1. tokens ----
const ACCENTS = { azul: '#2f6fed', violeta: '#8b5cf6', rosa: '#e11d74', naranja: '#ea7a0c', verde: '#16a34a', grafito: '#6b7280' };

function makeTheme({ mode, accent }) {
  const dark = mode === 'dark';
  return {
    mode, accents: ACCENTS, accent: ACCENTS[accent] || ACCENTS.azul, wallpapers: [],
    text: dark ? '#e8eaf0' : '#14161c', textDim: dark ? '#8b90a0' : '#646a78',
    windowBg: dark ? '#1b1e25' : '#f7f8fa', sidebarBg: dark ? '#20242c' : '#eceef2',
    separator: dark ? '#2d323d' : '#d7dae1', hover: dark ? '#262b35' : '#e3e6ec',
    control: dark ? '#2d323d' : '#d7dae1', inputBg: dark ? '#14161c' : '#ffffff',
    desktop: dark ? '#0f2537' : '#3a7ca5', bar: dark ? '#14171d' : '#e9ecf1', titleBg: dark ? '#232833' : '#dfe3ea',
    gtkButtons: ':minimize,maximize,close',     // GTK apps: buttons on the right, like this frame
    // overlay scroll bars (sdk/components.jsx ScrollView): this theme's squarer, solid thumb with a hairline
    scrollThumb: dark ? '#4a5160' : '#a9afbb', scrollThumbHover: dark ? '#5d6577' : '#8d94a2',
    scrollThumbBorder: dark ? '#20242c' : '#eceef2', scrollRadius: 2,
  };
}

const BAR_H = 44;      // bar at the top or bottom
const BAR_W = 64;      // bar on the left or right (icons only)
// Bar position (Settings → Desktop & Dock: taskbarPosition). Left and right are physical
// sides of the screen, also in Arabic or Hebrew. side() gives where each panel attaches.
const barSize = (pos) => (pos === 'left' || pos === 'right' ? BAR_W : BAR_H);
// Several displays (docs/DISPLAYS.md): the bar is on the main one; the others are all work area.
function workAreaFor(pos, d = mainDisplay()) {
  const a = { x: d.x, y: d.y, w: d.w, h: d.h };
  if (!d.main) return a;
  const b = barSize(pos);
  if (pos === 'top') { a.y += b; a.h -= b; } else if (pos === 'left') { a.x += b; a.w -= b; } else if (pos === 'right') a.w -= b; else a.h -= b;
  return a;
}
const areaFn = (d) => workAreaFor(dockPrefs(settings.get()).taskbarPosition, d);
// Style to attach a panel next to the bar (start menu, jump list).
function nextToBar(pos, along) {
  const b = barSize(pos) + 6;
  if (pos === 'top') return { left: along, top: b };
  if (pos === 'left') return { left: b, top: along };
  if (pos === 'right') return { right: b, top: along };
  return { left: along, bottom: b };
}
// The bar's box in screen coordinates (main display), so a menu drawn in another layer can still
// be placed against the bar with nextToBar.
function barBox(pos, m = mainDisplay()) {
  const b = barSize(pos);
  if (pos === 'left' || pos === 'right') return { left: m.x + (pos === 'right' ? m.w - b : 0), top: m.y, width: b, height: m.h };
  return { left: m.x, top: m.y + (pos === 'top' ? 0 : m.h - b), width: m.w, height: b };
}
// A menu next to the bar: in the popover layer (sdk/layers.js), above the bar and the windows.
function NextToBar({ pos, children }) {
  return (
    <Layer name="popover">
      <View style={{ position: 'absolute', ...barBox(pos), pointerEvents: 'box-none' }}>{children}</View>
    </Layer>
  );
}
// Is the point over the bar? (a click there doesn't close the menu: the start button toggles it)
function onBar(pos, x, y) {
  const b = barSize(pos), m = mainDisplay();
  x -= m.x; y -= m.y;
  return pos === 'top' ? y < b : pos === 'left' ? x < b : pos === 'right' ? x >= m.w - b : y >= m.h - b;
}

// ---- 2. window frame ----
// <WindowShell> (SDK) adds invisible borders for resizing on all 4 sides and 4 corners;
// moving and resizing is done by lord-server in C (wm.beginMove/beginResize, docs/WINDOWS.md). The
// JS drag below is only kept for a maximized window.
function Window({ win, t, focused }) {
  const drag = useRef(null);
  const geo = wm.frame(win);
  const app = getApp(win.id);
  useEffect(() => {
    const offMove = addListener('pointermove', (e) => {
      if (drag.current) wm.move(win.id, e.x - drag.current.dx, e.y - drag.current.dy);
    });
    const offUp = addListener('pointerup', () => { drag.current = null; });
    return () => { offMove(); offUp(); };
  }, [win.id]);
  const Btn = ({ label, onClick, danger }) => (
    <View onMouseDown={(e) => e.stopPropagation()} onClick={onClick} style={{ width: 34, height: 28, alignItems: 'center',
      justifyContent: 'center', backgroundColor: danger ? '#c4314b' : 'transparent' }}>
      <Text style={{ fontSize: 14, color: danger ? '#fff' : t.text }}>{label}</Text>
    </View>
  );
  if (win.args?.csd) {     // Linux app with its own frame (GTK, Chromium)
    return (
      <WindowShell win={win} onMouseDown={() => wm.focus(win.id)} style={{ boxShadow: '0 8px 24px rgba(0,0,0,0.35)' }}>
        <AppHost id={win.id} focused={focused} />
      </WindowShell>
    );
  }
  return (
    <WindowShell win={win} onMouseDown={() => wm.focus(win.id)} style={{
      backgroundColor: app.transparent ? 'transparent' : t.windowBg, border: `1px solid ${focused ? t.accent : t.separator}`,
      boxShadow: '0 8px 24px rgba(0,0,0,0.35)' }}>
      <View onMouseDown={(e) => { if (!wm.beginMove(win.id)) drag.current = { dx: e.x - geo.x, dy: e.y - geo.y }; }}
        onDoubleClick={() => wm.toggleMaximize(win.id)}
        style={{ height: 30, flexDirection: 'row', alignItems: 'center', paddingLeft: 10, backgroundColor: focused ? t.titleBg : t.windowBg }}>
        <Text style={{ flex: 1, fontSize: 12, fontWeight: 600, color: t.text, whiteSpace: 'nowrap' }}>{windowTitle(win)}</Text>
        <Btn label="−" onClick={() => wm.minimize(win.id)} />
        <Btn label="□" onClick={() => wm.toggleMaximize(win.id)} />
        <Btn label="×" danger={focused} onClick={() => wm.close(win.id)} />
      </View>
      <View style={{ flex: 1 }}><AppHost id={win.id} focused={focused} /></View>
    </WindowShell>
  );
}

// ---- 3. taskbar + start menu ----

// The power symbol, drawn (it doesn't depend on the font having ⏻)
function PowerGlyph({ color, bg }) {
  return (
    <View style={{ width: 14, height: 14 }}>
      <View style={{ position: 'absolute', left: 1, top: 2, width: 12, height: 12, borderRadius: 6, border: `1.6px solid ${color}` }} />
      <View style={{ position: 'absolute', left: 4, top: 0, width: 6, height: 5, backgroundColor: bg }} />
      <View style={{ position: 'absolute', left: 6.2, top: 0, width: 1.6, height: 7, borderRadius: 1, backgroundColor: color }} />
    </View>
  );
}

// A choice under the power button (Restart…, Shut Down…)
function PowerRow({ t, label, onClick }) {
  const [hover, setHover] = useState(false);
  return (
    <View onClick={onClick} onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)}
      style={{ padding: 8, paddingLeft: 30, backgroundColor: hover ? t.hover : 'transparent' }}>
      <Text style={{ fontSize: 13, color: t.text }}>{label}</Text>
    </View>
  );
}

// An app is pinned by dragging it onto the bar (the menu closes when the drag starts) or with a
// right click → Pin to taskbar.
function StartMenu({ t, onClose, pos }) {
  const ref = useRef(null);
  const list = useLauncherApps();
  const [ctx, setCtx] = useState(null);       // { app, x }
  const [power, setPower] = useState(false);   // the power button's menu (Restart…, Shut Down…)
  const groups = APP_CATEGORIES.map((c) => [c, list.filter((a) => appCategory(a) === c)]).filter(([, items]) => items.length);
  useEffect(() => addListener('pointerdown', (e) => {
    for (let n = e.target; n; n = n.parent) if (n === ref.current) return;
    if (!onBar(pos, e.x, e.y)) onClose();
  }), [pos]);
  return (
    <View ref={ref} style={{ position: 'absolute', ...nextToBar(pos, 6), width: 280, padding: 8, direction: 'ltr',
      backgroundColor: t.bar, border: `1px solid ${t.separator}`, boxShadow: '0 10px 30px rgba(0,0,0,0.4)' }}>
      <Text style={{ fontSize: 11, fontWeight: 600, color: t.textDim, margin: 6 }}>{tr('shell.start.apps')}</Text>
      {/* the list leaves room for the footer (lock, switch user, log out, power and its two choices) */}
      <ScrollView style={{ height: Math.min(list.length * 40 + groups.length * 26, screen.height - BAR_H - 200 - (power ? 72 : 0)) }}>
        {groups.map(([c, items]) => (
          <View key={c}>
            <Text style={{ fontSize: 10, fontWeight: 700, color: t.textDim, marginLeft: 6, marginTop: 8, marginBottom: 2 }}>{tr(`shell.category.${c}`)}</Text>
            {items.map((a) => (
              <View key={a.id} onClick={() => { if (dockDragJustEnded()) return; onClose(); launchApp(a.id); }}
                onMouseDown={(e) => { if (!e.button && dockIdOf(a)) beginDockDrag({ id: dockIdOf(a), Icon: a.Icon, x: e.x, y: e.y, source: 'launcher', onStart: onClose }); }}
                onContextMenu={(e) => dockIdOf(a) && setCtx({ app: a, x: (pos === 'left' || pos === 'right' ? e.y : e.x) - 20 })}
                style={{ flexDirection: 'row', alignItems: 'center', gap: 10, padding: 6 }}>
                <a.Icon size={28} />
                <Text style={{ fontSize: 13, color: t.text, whiteSpace: 'nowrap' }}>{a.name}</Text>
              </View>
            ))}
          </View>
        ))}
      </ScrollView>
      <View style={{ height: 1, backgroundColor: t.separator, marginVertical: 6 }} />
      {session.managed() ? (
        <View onClick={() => { onClose(); session.lock(); }} style={{ padding: 8 }}><Text style={{ fontSize: 13, color: t.text }}>{tr('shell.menu.lock')}</Text></View>
      ) : null}
      {session.managed() ? (
        <View onClick={() => { onClose(); session.switchUser(); }} style={{ padding: 8 }}><Text style={{ fontSize: 13, color: t.text }}>{tr('shell.menu.switchUser')}</Text></View>
      ) : null}
      <View style={{ flexDirection: 'row', alignItems: 'center' }}>
        <View onClick={() => session.quit()} style={{ flex: 1, padding: 8 }}><Text style={{ fontSize: 13, color: t.text }}>{tr('shell.menu.logout')}</Text></View>
        {session.canPower() ? (
          <View onClick={() => setPower((v) => !v)} style={{ flexDirection: 'row', alignItems: 'center', gap: 6, paddingVertical: 6,
            paddingHorizontal: 10, borderRadius: 4, backgroundColor: power ? t.hover : 'transparent' }}>
            <PowerGlyph color={t.text} bg={power ? t.hover : t.bar} />
            <Text style={{ fontSize: 13, color: t.text }}>{tr('shell.menu.power')}</Text>
          </View>
        ) : null}
      </View>
      {power ? [['restart', () => session.restart()], ['shutdown', () => session.shutdown()]].map(([k, go]) => (
        <PowerRow key={k} t={t} label={tr(`shell.menu.${k}`)} onClick={() => { onClose(); go(); }} />
      )) : null}
      {ctx && (
        <PopupMenu t={t} pos={pos} x={ctx.x} onClose={() => setCtx(null)} items={[
          { label: tr('shell.dock.open'), onClick: () => { onClose(); launchApp(ctx.app.id); } },
          dock.isPinned(ctx.app)
            ? { label: tr('shell.dock.unpin'), disabled: !dock.canRemove(ctx.app), onClick: () => dock.unpin(ctx.app) }
            : { label: tr('shell.dock.pin'), onClick: () => dock.pin(ctx.app) },
        ]} />
      )}
    </View>
  );
}

// A small menu next to the bar (the jump list, the Trash's and the start menu's right click).
// Keyboard: ↑/↓, Enter, Escape.
function PopupMenu({ t, items, x, onClose, pos }) {
  const ref = useRef(null);
  const [active, setActive] = useState(-1);
  const st = useRef({});
  st.current = { items, active };
  const ok = (it) => it && it !== '-' && !it.disabled;
  useEffect(() => {
    const offPtr = addListener('pointerdown', (e) => {
      for (let n = e.target; n; n = n.parent) if (n === ref.current) return;
      onClose();
    });
    const tok = {};
    takeTextFocus(tok, (e) => {
      const { items: list, active: a } = st.current;
      const step = (d) => { for (let k = 1; k <= list.length; k++) { const i = ((a < 0 && d < 0 ? 0 : a) + d * k + list.length * 2) % list.length; if (ok(list[i])) { setActive(i); return; } } };
      if (e.key === 'Escape') onClose();
      else if (e.key === 'ArrowDown') step(1);
      else if (e.key === 'ArrowUp') step(-1);
      else if ((e.key === 'Enter' || e.key === ' ') && ok(list[a])) { onClose(); list[a].onClick?.(); }
    });
    return () => { offPtr(); releaseTextFocus(tok); };
  }, []);
  const W = 250;
  const H = items.reduce((h, it) => h + (it === '-' ? 9 : 28), 8);
  const maxH = screen.height - BAR_H - 20;
  const rows = items.map((it, i) => (it === '-'
    ? <View key={i} style={{ height: 1, backgroundColor: t.separator, marginVertical: 4 }} />
    : (
      <View key={i} onMouseEnter={() => setActive(ok(it) ? i : -1)} onClick={() => { if (it.disabled) return; onClose(); it.onClick?.(); }}
        style={{ flexDirection: 'row', alignItems: 'center', height: 28, paddingHorizontal: 8, backgroundColor: i === active ? t.hover : 'transparent' }}>
        <Text style={{ width: 18, fontSize: 12, color: t.accent }}>{it.checked ? '✓' : ''}</Text>
        <Text style={{ flex: 1, fontSize: 13, color: it.disabled ? t.textDim : t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{it.label}</Text>
      </View>
    )));
  return (
    <View ref={ref} style={{ position: 'absolute', ...nextToBar(pos, pos === 'left' || pos === 'right' ? Math.max(4, Math.min(x, screen.height - Math.min(H, maxH) - 8))
      : Math.max(4, Math.min(x, screen.width - W - 4))), width: W, padding: 4, direction: 'ltr',
      backgroundColor: t.bar, border: `1px solid ${t.separator}`, boxShadow: '0 10px 30px rgba(0,0,0,0.4)' }}>
      {H > maxH ? <ScrollView style={{ height: maxH }}>{rows}</ScrollView> : rows}
    </View>
  );
}

// The bar's right-click menu (a jump list): the SDK's items (dockMenu), with submenus flattened
// and taskbar wording.
function JumpList({ t, entry, x, onClose, pos }) {
  const items = [];
  for (const it of dockMenu(entry, { keepLabel: tr('shell.dock.pin'), removeLabel: tr('shell.dock.unpin') })) {
    if (it !== '-' && it.submenu) items.push(...it.submenu);
    else if (it !== '-' && it.label === tr('shell.dock.quit')) items.push({ ...it, label: tr('shell.dock.closeAll') });
    else if (it !== '-' && it.label === tr('shell.dock.hide')) continue;
    else items.push(it);
  }
  return <PopupMenu t={t} items={items} x={x} pos={pos} onClose={onClose} />;
}

// Taskbar: Files first, the pinned and open apps (one per app: windows of the same app are
// grouped), then the Trash, always last. The same rules and SDK calls as the default theme's Dock
// (sdk/dock.js): drag a button to reorder it (a line shows where it lands), drag it off the bar and
// hold to unpin it, drop apps from the start menu; Files and Settings can't be unpinned.
// Click: opens the app or brings its window (if it was already in front, minimizes it); right click: JumpList.
function Taskbar({ t, onStart, pos, onEmptyTrash }) {
  const clock = useClock((d) => formatTime(d, { seconds: !!settings.get().clock?.seconds }), 1000);   // Settings → Date & Time
  const entries = useDockEntries(['files', 'terminal', 'store', 'settings']);
  const { focused } = useWindows();
  const [jump, setJump] = useState(null);
  const [trashMenuAt, setTrashMenuAt] = useState(null);
  const trash = useTrash();
  const drag = useStore(dockDrag);
  const vert = pos === 'left' || pos === 'right';
  const refs = useRef(new Map());
  const barRef = useRef(null);
  const trashRef = useRef(null);
  const click = (e) => {
    if (dockDragJustEnded()) return;
    const top = e.windows.find((w) => w.id === focused);
    if (top && e.windows.length === 1) wm.minimize(top.id); else dockActivate(e);
  };
  // where the buttons are (global coordinates), for the drop position
  const centerOf = (id) => {
    const n = refs.current.get(id);
    if (!n) return null;
    const [x, y, w, h] = lord.getLayout(n.id);
    return vert ? y + h / 2 : x + w / 2;
  };
  const geo = useRef({});
  geo.current = { entries, vert, drag };
  useEffect(() => registerDockTarget({
    hit(x, y) {
      const g = geo.current;
      if (!barRef.current) return null;
      const [bx, by, bw, bh] = lord.getLayout(barRef.current.id);
      if (!(x >= bx - 16 && x < bx + bw + 16 && y >= by - 16 && y < by + bh + 16)) return null;
      if (trashRef.current) {
        const [tx, ty, tw, th] = lord.getLayout(trashRef.current.id);
        if (x >= tx && x < tx + tw && y >= ty && y < ty + th) return { trash: true };
      }
      const a = g.vert ? y : x;
      const own = g.drag?.source === 'dock' ? g.drag.id : null;
      const cs = g.entries.filter((e) => e.pinned && !e.recent && e.id !== own).map((e) => centerOf(e.id)).filter((c) => c != null);
      return { index: cs.filter((c) => c < a).length };
    },
    near(x, y) {
      if (!barRef.current) return false;
      const [bx, by, bw, bh] = lord.getLayout(barRef.current.id);
      const dx = x < bx ? bx - x : x > bx + bw ? x - bx - bw : 0, dy = y < by ? by - y : y > by + bh ? y - by - bh : 0;
      return Math.hypot(dx, dy) <= 60;
    },
  }), []);
  // the drop line: before the pinned button at that index (after the last one at the end)
  let dropLine = null;
  if (drag && drag.over && drag.over.index != null && barRef.current) {
    const own = drag.source === 'dock' ? drag.id : null;
    const pinned = entries.filter((e) => e.pinned && !e.recent && e.id !== own);
    const k = Math.max(1, Math.min(pinned.length, drag.over.index));
    const ref = refs.current.get((pinned[k] || pinned[pinned.length - 1])?.id);
    if (ref) {
      const [bx, by] = lord.getLayout(barRef.current.id);
      const [x, y, w, h] = lord.getLayout(ref.id);
      const at = pinned[k] ? (vert ? y - by - 3 : x - bx - 3) : (vert ? y - by + h + 1 : x - bx + w + 1);
      dropLine = vert ? { top: at, left: 6, right: 6, height: 2 } : { left: at, top: 6, bottom: 6, width: 2 };
    }
  }
  const btn = (e) => {
    const active = e.focused;
    const title = e.windows.length ? windowTitle(e.windows.find((w) => w.id === focused) || e.windows[e.windows.length - 1]) : '';
    const dragged = drag && drag.source === 'dock' && drag.id === e.id;
    return (
      <View key={e.id} ref={(n) => { if (n) refs.current.set(e.id, n); else refs.current.delete(e.id); }}
        onClick={() => click(e)} onContextMenu={(ev) => setJump({ entry: e, x: (vert ? ev.y : ev.x) - 20 })}
        onMouseDown={(ev) => { if (!ev.button) beginDockDrag({ id: e.id, Icon: e.Icon, x: ev.x, y: ev.y, source: 'dock' }); }}
        style={{ height: vert ? 44 : 32, width: vert ? 52 : undefined, paddingHorizontal: vert ? 0 : e.running ? 10 : 6, flexDirection: 'row',
          alignItems: 'center', justifyContent: vert ? 'center' : 'flex-start', gap: 8, opacity: dragged ? 0.35 : 1,
          backgroundColor: active ? t.hover : 'transparent', border: `1px solid ${active ? t.accent : 'transparent'}` }}>
        {e.running && <View style={{ position: 'absolute', backgroundColor: t.accent, ...(pos === 'top' ? { left: 0, right: 0, top: 0, height: 2 }
          : pos === 'left' ? { top: 0, bottom: 0, left: 0, width: 2 } : pos === 'right' ? { top: 0, bottom: 0, right: 0, width: 2 }
            : { left: 0, right: 0, bottom: 0, height: 2 }) }} />}
        <e.Icon size={vert ? 26 : 20} />
        {e.running && !vert && <Text style={{ fontSize: 12, color: t.text, whiteSpace: 'nowrap' }}>{title.slice(0, 24)}</Text>}
        {e.windows.length > 1 && <Text style={{ fontSize: 10, fontWeight: 700, color: t.textDim }}>{e.windows.length}</Text>}
      </View>
    );
  };
  return (
    <View ref={barRef} style={{ position: 'absolute', ...(vert ? { top: 0, bottom: 0, width: BAR_W, [pos]: 0, flexDirection: 'column', paddingVertical: 6 }
      : { left: 0, right: 0, height: BAR_H, [pos]: 0, flexDirection: 'row', paddingHorizontal: 6 }), alignItems: 'center',
      gap: 4, backgroundColor: t.bar, border: `1px solid ${t.separator}`, direction: 'ltr' }}>
      <View onClick={onStart} style={{ height: 32, paddingHorizontal: vert ? 0 : 14, width: vert ? 52 : undefined, alignItems: 'center',
        justifyContent: 'center', backgroundColor: t.accent }}>
        <Text style={{ fontSize: vert ? 11 : 13, fontWeight: 700, color: '#fff' }}>LordOS</Text>
      </View>
      {entries.map(btn)}
      <View style={vert ? { width: 30, height: 1, backgroundColor: t.separator } : { width: 1, height: 22, backgroundColor: t.separator }} />
      <View ref={trashRef} dropTrash onClick={() => { if (!dockDragJustEnded()) openTrash(); }}
        onContextMenu={(ev) => setTrashMenuAt((vert ? ev.y : ev.x) - 20)} onMouseUp={() => setTimeout(refreshTrash, 60)}
        style={{ height: vert ? 44 : 32, width: vert ? 52 : 34, alignItems: 'center', justifyContent: 'center',
          backgroundColor: drag?.over?.trash ? t.hover : 'transparent' }}>
        <TrashGlyph size={vert ? 26 : 22} full={trash.full} t={t} />
      </View>
      <View style={{ flex: 1 }} />
      {vert ? null : <StatusItems color={t.text} />}
      <PrivacyIndicators />
      <Text style={{ fontSize: vert ? 12 : 13, fontWeight: 600, color: t.text, marginRight: vert ? 0 : 10, marginBottom: vert ? 6 : 0 }}>{clock}</Text>
      {dropLine && <View style={{ position: 'absolute', ...dropLine, backgroundColor: t.accent, pointerEvents: 'none' }} />}
      {jump && <NextToBar pos={pos}><JumpList t={t} pos={pos} entry={entries.find((x) => x.id === jump.entry.id) || jump.entry} x={jump.x} onClose={() => setJump(null)} /></NextToBar>}
      {trashMenuAt != null && <NextToBar pos={pos}><PopupMenu t={t} pos={pos} x={trashMenuAt} onClose={() => setTrashMenuAt(null)}
        items={trashMenu({ onEmpty: onEmptyTrash }).filter((it) => it !== '-')} /></NextToBar>}
    </View>
  );
}

// The Trash on the taskbar: a small bin, with papers when it isn't empty.
function TrashGlyph({ size: s, full, t }) {
  const line = t.mode === 'dark' ? 'rgba(255,255,255,0.7)' : 'rgba(30,34,44,0.65)';
  return (
    <View style={{ width: s, height: s }}>
      {full && <View style={{ position: 'absolute', left: s * 0.3, top: s * 0.02, width: s * 0.4, height: s * 0.24, borderRadius: 2,
        backgroundColor: t.mode === 'dark' ? '#d9dde6' : '#ffffff', border: `1px solid ${line}`, transform: 'rotate(-10deg)' }} />}
      <View style={{ position: 'absolute', left: s * 0.18, top: s * 0.18, width: s * 0.64, height: s * 0.07, backgroundColor: line, borderRadius: 1 }} />
      <View style={{ position: 'absolute', left: s * 0.24, top: s * 0.28, width: s * 0.52, height: s * 0.62, border: `1.5px solid ${line}`,
        borderBottomLeftRadius: s * 0.08, borderBottomRightRadius: s * 0.08 }}>
        {[0.3, 0.6].map((k) => <View key={k} style={{ position: 'absolute', top: s * 0.08, bottom: s * 0.08, left: s * 0.52 * k - 1, width: 1.5, backgroundColor: line }} />)}
      </View>
    </View>
  );
}

// What is being dragged (from the bar or the start menu), and the remove cue.
function DragGhost({ t }) {
  const drag = useStore(dockDrag);
  if (!drag) return null;
  return (
    <View style={{ position: 'absolute', left: drag.x - 14, top: drag.y - 14, pointerEvents: 'none', opacity: drag.removing ? 0.5 : 0.9, alignItems: 'center' }}>
      {drag.Icon ? <drag.Icon size={28} /> : null}
      {drag.removing && (
        <View style={{ marginTop: 4, paddingHorizontal: 6, paddingVertical: 2, backgroundColor: t.bar, border: `1px solid ${t.separator}` }}>
          <Text style={{ fontSize: 11, color: t.text, whiteSpace: 'nowrap' }}>{tr('shell.dock.unpinCue')}</Text>
        </View>
      )}
    </View>
  );
}

// After an unpin: the button fades out where it was dropped (nothing with reduce motion).
function PoofFade() {
  const poof = useStore(dockPoof);
  const reduce = !!useStore(settings).reduceMotion;
  const [, tick] = useState(0);
  const p = poof ? Math.min(1, (Date.now() - poof.t0) / 300) : 1;
  useEffect(() => {
    if (!poof || p >= 1 || reduce) return undefined;
    const raf = requestAnimationFrame(() => tick((n) => n + 1));
    return () => cancelAnimationFrame(raf);
  });
  if (!poof || p >= 1 || reduce) return null;
  const k = Math.round(28 * (1 - 0.5 * p));
  return (
    <View style={{ position: 'absolute', left: poof.x - k / 2, top: poof.y - k / 2, pointerEvents: 'none', opacity: 1 - p }}>
      {poof.Icon ? <poof.Icon size={k} /> : null}
    </View>
  );
}


// ---- 4. App ----
configureWindows({ apps, workArea: areaFn, chrome: { w: 2, h: 32 } });
wm.open('about');

function App() {
  const [s] = useSettings();
  const t = makeTheme(s);
  const { windows, visible, focused } = useWindows();
  const [start, setStart] = useState(false);
  const [askEmpty, setAskEmpty] = useState(false);
  const pos = dockPrefs(s).taskbarPosition;
  // the work area follows the bar live
  useEffect(() => { configureWindows({ apps, workArea: areaFn }); }, [pos, screen.width, screen.height]);
  const m = mainDisplay();
  // trackpad: a 3-finger swipe up opens the start menu, down closes it (docs/TRACKPAD.md)
  useEffect(() => registerGestureActions({ up: () => setStart(true), down: () => setStart(false) }), []);
  return (
    <ThemeProvider tokens={t}>
      <View style={{ width: '100%', height: '100%', backgroundColor: t.desktop }}>
        <Text style={{ position: 'absolute', left: m.x + m.w - (pos === 'right' ? BAR_W : 0) - 24 - 170, top: m.y + m.h - (pos === 'bottom' ? BAR_H : 0) - 16 - 50, fontSize: 40, fontWeight: 700, color: 'rgba(255,255,255,0.12)' }}>LordOS</Text>
        <DesktopWidgets layer="below" />
        {visible.map((w) => <Window key={w.id} win={w} t={t} focused={w.id === focused} />)}
        <DesktopWidgets layer="above" />
        {/* the bar on the main display, and above it the start menu: each in its layer
            (sdk/layers.js, docs/LAYERS.md), so the order and the clicks are the layers' */}
        <Layer name="dock">
          <View style={{ position: 'absolute', left: m.x, top: m.y, width: m.w, height: m.h, pointerEvents: 'box-none' }}>
            <Taskbar t={t} pos={pos} onStart={() => setStart((v) => !v)} onEmptyTrash={() => setAskEmpty(true)} />
          </View>
        </Layer>
        {start && (
          <Layer name="popover">
            <View style={{ position: 'absolute', left: m.x, top: m.y, width: m.w, height: m.h, pointerEvents: 'box-none' }}>
              <StartMenu t={t} pos={pos} onClose={() => setStart(false)} />
            </View>
          </Layer>
        )}
        <Layer name="drag">
          <DragGhost t={t} />
          <PoofFade />
        </Layer>
        <Layer name="modal"><PowerDialog /></Layer>   {/* Restart…, Shut Down… (sdk/power.jsx) */}
        {askEmpty && (
          <Layer name="modal">
            <ConfirmDialog title={tr('shell.trash.confirmTitle')} detail={tr('shell.trash.confirmDetail')} okLabel={tr('shell.trash.empty')}
              cancelLabel={tr('shell.trash.cancel')} destructive area={m}
              onCancel={() => setAskEmpty(false)} onOk={() => { setAskEmpty(false); emptyTrash(); }} />
          </Layer>
        )}
      </View>
    </ThemeProvider>
  );
}

startShell(<App />);

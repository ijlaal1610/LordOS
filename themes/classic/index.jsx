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

function StartMenu({ t, onClose, pos }) {
  const ref = useRef(null);
  const list = useLauncherApps();
  const groups = APP_CATEGORIES.map((c) => [c, list.filter((a) => appCategory(a) === c)]).filter(([, items]) => items.length);
  useEffect(() => addListener('pointerdown', (e) => {
    for (let n = e.target; n; n = n.parent) if (n === ref.current) return;
    if (!onBar(pos, e.x, e.y)) onClose();
  }), [pos]);
  return (
    <View ref={ref} style={{ position: 'absolute', ...nextToBar(pos, 6), width: 280, padding: 8, direction: 'ltr',
      backgroundColor: t.bar, border: `1px solid ${t.separator}`, boxShadow: '0 10px 30px rgba(0,0,0,0.4)' }}>
      <Text style={{ fontSize: 11, fontWeight: 600, color: t.textDim, margin: 6 }}>{tr('shell.start.apps')}</Text>
      <ScrollView style={{ height: Math.min(list.length * 40 + groups.length * 26, screen.height - BAR_H - 120) }}>
        {groups.map(([c, items]) => (
          <View key={c}>
            <Text style={{ fontSize: 10, fontWeight: 700, color: t.textDim, marginLeft: 6, marginTop: 8, marginBottom: 2 }}>{tr(`shell.category.${c}`)}</Text>
            {items.map((a) => (
              <View key={a.id} onClick={() => { onClose(); launchApp(a.id); }} style={{ flexDirection: 'row', alignItems: 'center', gap: 10, padding: 6 }}>
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
      <View onClick={() => session.quit()} style={{ padding: 8 }}><Text style={{ fontSize: 13, color: t.text }}>{tr('shell.menu.logout')}</Text></View>
    </View>
  );
}

// The bar's right-click menu (a jump list). Uses the SDK's
// items (dockMenu), with submenus flattened and taskbar wording.
function JumpList({ t, entry, x, onClose, pos }) {
  const ref = useRef(null);
  useEffect(() => addListener('pointerdown', (e) => {
    for (let n = e.target; n; n = n.parent) if (n === ref.current) return;
    onClose();
  }), []);
  const items = [];
  for (const it of dockMenu(entry)) {
    if (it !== '-' && it.submenu) items.push(...it.submenu.map((s) => (s.checked != null && s.label === tr('shell.dock.keep') ? { ...s, label: tr('shell.dock.pin') } : s)));
    else if (it !== '-' && it.label === tr('shell.dock.quit')) items.push({ ...it, label: tr('shell.dock.closeAll') });
    else if (it !== '-' && it.label === tr('shell.dock.hide')) continue;
    else items.push(it);
  }
  const W = 250;
  return (
    <View ref={ref} style={{ position: 'absolute', ...nextToBar(pos, pos === 'left' || pos === 'right' ? Math.max(4, Math.min(x, screen.height - 300))
      : Math.max(4, Math.min(x, screen.width - W - 4))), width: W, padding: 4,
      backgroundColor: t.bar, border: `1px solid ${t.separator}`, boxShadow: '0 10px 30px rgba(0,0,0,0.4)' }}>
      {items.map((it, i) => (it === '-'
        ? <View key={i} style={{ height: 1, backgroundColor: t.separator, marginVertical: 4 }} />
        : (
          <View key={i} onClick={() => { onClose(); it.onClick?.(); }} style={{ flexDirection: 'row', alignItems: 'center', height: 28, paddingHorizontal: 8 }}>
            <Text style={{ width: 18, fontSize: 12, color: t.accent }}>{it.checked ? '✓' : ''}</Text>
            <Text style={{ flex: 1, fontSize: 13, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{it.label}</Text>
          </View>
        )))}
    </View>
  );
}

// Taskbar: pinned and open apps, one per app (windows of the same app are grouped).
// Click: opens the app or brings its window (if it was already in front, minimizes it); right click: JumpList.
function Taskbar({ t, onStart, pos }) {
  const clock = useClock((d) => formatTime(d, { seconds: !!settings.get().clock?.seconds }), 1000);   // Settings → Date & Time
  const entries = useDockEntries(['files', 'terminal', 'store']);
  const { focused } = useWindows();
  const [jump, setJump] = useState(null);
  const vert = pos === 'left' || pos === 'right';
  const click = (e) => {
    const top = e.windows.find((w) => w.id === focused);
    if (top && e.windows.length === 1) wm.minimize(top.id); else dockActivate(e);
  };
  return (
    <View style={{ position: 'absolute', ...(vert ? { top: 0, bottom: 0, width: BAR_W, [pos]: 0, flexDirection: 'column', paddingVertical: 6 }
      : { left: 0, right: 0, height: BAR_H, [pos]: 0, flexDirection: 'row', paddingHorizontal: 6 }), alignItems: 'center',
      gap: 4, backgroundColor: t.bar, border: `1px solid ${t.separator}`, direction: 'ltr' }}>
      <View onClick={onStart} style={{ height: 32, paddingHorizontal: vert ? 0 : 14, width: vert ? 52 : undefined, alignItems: 'center',
        justifyContent: 'center', backgroundColor: t.accent }}>
        <Text style={{ fontSize: vert ? 11 : 13, fontWeight: 700, color: '#fff' }}>LordOS</Text>
      </View>
      {entries.map((e) => {
        const active = e.focused;
        const title = e.windows.length ? windowTitle(e.windows.find((w) => w.id === focused) || e.windows[e.windows.length - 1]) : '';
        return (
          <View key={e.id} onClick={() => click(e)} onContextMenu={(ev) => setJump({ entry: e, x: (vert ? ev.y : ev.x) - 20 })}
            style={{ height: vert ? 44 : 32, width: vert ? 52 : undefined, paddingHorizontal: vert ? 0 : e.running ? 10 : 6, flexDirection: 'row',
              alignItems: 'center', justifyContent: vert ? 'center' : 'flex-start', gap: 8,
              backgroundColor: active ? t.hover : 'transparent', border: `1px solid ${active ? t.accent : 'transparent'}` }}>
            {e.running && <View style={{ position: 'absolute', backgroundColor: t.accent, ...(pos === 'top' ? { left: 0, right: 0, top: 0, height: 2 }
              : pos === 'left' ? { top: 0, bottom: 0, left: 0, width: 2 } : pos === 'right' ? { top: 0, bottom: 0, right: 0, width: 2 }
                : { left: 0, right: 0, bottom: 0, height: 2 }) }} />}
            <e.Icon size={vert ? 26 : 20} />
            {e.running && !vert && <Text style={{ fontSize: 12, color: t.text, whiteSpace: 'nowrap' }}>{title.slice(0, 24)}</Text>}
            {e.windows.length > 1 && <Text style={{ fontSize: 10, fontWeight: 700, color: t.textDim }}>{e.windows.length}</Text>}
          </View>
        );
      })}
      <View style={{ flex: 1 }} />
      {vert ? null : <StatusItems color={t.text} />}
      <PrivacyIndicators />
      <Text style={{ fontSize: vert ? 12 : 13, fontWeight: 600, color: t.text, marginRight: vert ? 0 : 10, marginBottom: vert ? 6 : 0 }}>{clock}</Text>
      {jump && <JumpList t={t} pos={pos} entry={entries.find((x) => x.id === jump.entry.id) || jump.entry} x={jump.x} onClose={() => setJump(null)} />}
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
        {/* the bar and the start menu on the main display */}
        <View style={{ position: 'absolute', left: m.x, top: m.y, width: m.w, height: m.h }}>
          <Taskbar t={t} pos={pos} onStart={() => setStart((v) => !v)} />
          {start && <StartMenu t={t} pos={pos} onClose={() => setStart(false)} />}
        </View>
      </View>
    </ThemeProvider>
  );
}

startShell(<App />);

// "Lord Default" theme — style inspired by Mac-OS-Desktop, with its own assets.
// The theme builds the desktop (wallpaper, bar, dock, Launchpad, Control Center) and the
// window frames. Apps come from apps/ through the SDK.
import {
  startShell, View, Text, Image, screen, ThemeProvider, useSettings, useWindows, configureWindows, getApp, wm, addListener, t as tr,
  useLauncherApps, launchApp, useDockEntries, dockActivate, dockMenu, dock, dockPrefs,
  DesktopWidgets, registerGestureActions, screenDisplays, mainDisplay, settings,
} from '@lordos/sdk';
import { apps } from '@lordos/apps';
import { useEffect, useState } from 'react';
import { makeTheme } from './tokens.js';
import { MenuBar } from './components/MenuBar.jsx';
import { Dock, LaunchpadIcon, dockThickness } from './components/Dock.jsx';
import { Window } from './components/Window.jsx';
import { Launchpad } from './components/Launchpad.jsx';
import { ControlCenter } from './components/ControlCenter.jsx';
import { Menu } from './components/Menu.jsx';

const MENU_H = 28;
const DOCK_ORDER = ['files', 'terminal', 'calculator', 'settings', 'about'];

// Area for windows: below the menu bar and without the Dock's side (unless it auto-hides),
// so maximized and new windows don't end up under the Dock.
// Several displays (docs/DISPLAYS.md): the menu bar and the Dock go on the main one; the menu bar
// also on the others with Settings → Displays → "Show menu bar on all displays".
function workAreaFor(p, d, menuAll) {
  const bar = d.main || menuAll ? MENU_H : 0;
  const a = { x: d.x, y: d.y + bar, w: d.w, h: d.h - bar };
  if (!d.main || p.dockAutohide) return a;
  const k = dockThickness(p.dockSize);
  if (p.dockPosition === 'left') { a.x += k; a.w -= k; }
  else if (p.dockPosition === 'right') a.w -= k;
  else a.h -= k;
  return a;
}
const areaFn = (d) => { const s = settings.get(); return workAreaFor(dockPrefs(s), d, !!s.menuBarAllDisplays); };

// chrome: borders (1 + 1) and title bar (40 + 1 separator) of Window.jsx's frame
configureWindows({ apps, workArea: areaFn, chrome: { w: 2, h: 43 } });
wm.open('files');
wm.open('about');

// Pinned by default (the user changes the Dock with «Keep in Dock»: settings.dockPinned).
const DOCK_DEFAULT = [...DOCK_ORDER, ...apps.filter((a) => a.dock && !DOCK_ORDER.includes(a.id)).map((a) => a.id)];

function App() {
  const [s] = useSettings();
  const t = makeTheme(s);
  const { windows, visible, focused } = useWindows();
  const [launchpad, setLaunchpad] = useState(false);
  const [control, setControl] = useState(null);         // the display whose menu bar opened it
  const [context, setContext] = useState(null);
  const [dockCtx, setDockCtx] = useState(null);
  const launcherApps = useLauncherApps();
  const dp = dockPrefs(s);
  // the work area follows the Dock live (position, size, auto-hide, another screen)
  useEffect(() => { configureWindows({ apps, workArea: areaFn }); },
    [dp.dockPosition, dp.dockSize, dp.dockAutohide, s.menuBarAllDisplays, screen.width, screen.height]);
  const main = mainDisplay();
  const all = screenDisplays();
  const bars = s.menuBarAllDisplays && !screen.mirror ? all : [main];

  // F4 opens the Launchpad; Super too, but only if released without being used in a shortcut
  // (Super+T, Super+D... belong to the apps, like Cmd on macOS).
  useEffect(() => {
    let superAlone = false;
    const offDown = addListener('keydown', (e) => {
      if (e.code === 62 && !e.repeat) setLaunchpad((v) => !v);
      if (e.code === 125 || e.code === 126) { if (!e.repeat) superAlone = true; } else superAlone = false;
      if (e.key === 'Escape') { setControl(null); setContext(null); }
    });
    const offUp = addListener('keyup', (e) => {
      if ((e.code === 125 || e.code === 126) && superAlone) setLaunchpad((v) => !v);
      superAlone = false;
    });
    return () => { offDown(); offUp(); };
  }, []);

  // Trackpad: a 3-finger swipe up opens the Launchpad and down closes it (like
  // macOS); left/right switch windows (the SDK does that). See docs/TRACKPAD.md.
  useEffect(() => registerGestureActions({ up: () => setLaunchpad(true), down: () => setLaunchpad(false) }), []);

  const wallpaper = t.wallpapers.find((w) => w.id === s.wallpaper) || t.wallpapers[0];
  const launchpadEntry = { id: 'launchpad', name: 'Launchpad', Icon: LaunchpadIcon };
  // Pinned and open apps; windows of the same app (Linux too) share a single icon.
  const entries = useDockEntries(DOCK_DEFAULT, { recents: dp.dockRecents ? 3 : 0 });
  const byId = new Map(entries.map((e) => [e.id, e]));

  return (
    <ThemeProvider tokens={t}>
      <View style={{ width: '100%', height: '100%' }}>
        {all.map((d) => (
          <Image key={d.id + ':' + d.x} src={wallpaper.src} onContextMenu={(e) => setContext({ x: e.x, y: e.y })}
            style={{ position: 'absolute', left: d.x, top: d.y, width: d.w, height: d.h, objectFit: 'cover' }} />
        ))}
        <DesktopWidgets layer="below" />
        {visible.map((w) => <Window key={w.id} win={w} app={getApp(w.id)} t={t} focused={w.id === focused} />)}
        <DesktopWidgets layer="above" />
        {bars.map((d) => (
          <MenuBar key={d.id + ':' + d.x} t={t} height={MENU_H} focused={focused} controlCenter={!!control} area={d}
            onLaunchpad={() => setLaunchpad(true)} onControlCenter={() => setControl((v) => (v ? null : d))} />
        ))}
        <Dock t={t} items={[launchpadEntry, ...entries]} running={entries.filter((e) => e.running).map((e) => e.id)}
          onOpen={(id) => (id === 'launchpad' ? setLaunchpad(true) : byId.has(id) && dockActivate(byId.get(id)))}
          onContext={(id, at) => byId.has(id) && setDockCtx({ entry: byId.get(id), at })}
          onDropApp={(id, index) => dock.pin(id, Math.max(0, index - 1))}
          position={dp.dockPosition} size={dp.dockSize} magnify={dp.dockMagnify} maxSize={dp.dockMagnifySize}
          autohide={dp.dockAutohide} top={MENU_H} keepOpen={!!dockCtx} area={main} />
        {dockCtx && (dp.dockPosition === 'bottom'
          ? <Menu above side="right" x={dockCtx.at.x - 110} y={dockCtx.at.y} onClose={() => setDockCtx(null)} items={dockMenu(byId.get(dockCtx.entry.id) || dockCtx.entry)} />
          : <Menu centered side={dp.dockPosition === 'left' ? 'right' : 'left'} x={dockCtx.at.x} y={dockCtx.at.y} onClose={() => setDockCtx(null)}
            items={dockMenu(byId.get(dockCtx.entry.id) || dockCtx.entry)} />)}
        {control && <ControlCenter t={t} top={MENU_H} area={control} onClose={() => setControl(null)} />}
        {context && (
          <Menu x={context.x} y={context.y} onClose={() => setContext(null)} items={[
            { label: tr('shell.desktop.wallpaper'), onClick: () => setControl(main) },
            { label: tr('shell.desktop.openLaunchpad'), onClick: () => setLaunchpad(true) },
            '-',
            { label: tr('shell.menu.settings'), onClick: () => wm.open('settings') },
            { label: tr('shell.menu.about'), onClick: () => wm.open('about') },
          ]} />
        )}
        {launchpad && <Launchpad t={t} apps={launcherApps} onOpen={launchApp} onClose={() => setLaunchpad(false)} area={main} />}
      </View>
    </ThemeProvider>
  );
}

startShell(<App />);

// Tema "Lord Default" — estilo inspirado en Mac-OS-Desktop, con assets propios.
// El tema arma el escritorio (fondo, barra, dock, Launchpad, Centro de control) y los
// marcos de ventana. Las apps vienen de apps/ a través del SDK.
import {
  startShell, View, Text, Image, screen, ThemeProvider, useSettings, useWindows, configureWindows, getApp, wm, addListener, t as tr,
  useLauncherApps, launchApp, useDockEntries, dockActivate, dockMenu, dock, dockPrefs,
  DesktopWidgets, registerGestureActions,
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

// Área para las ventanas: debajo de la barra de menú y sin el lado del Dock (si no se esconde solo),
// así las ventanas maximizadas y las nuevas no quedan debajo del Dock.
function workAreaFor(p) {
  const a = { x: 0, y: MENU_H, w: screen.width, h: screen.height - MENU_H };
  if (p.dockAutohide) return a;
  const d = dockThickness(p.dockSize);
  if (p.dockPosition === 'left') { a.x += d; a.w -= d; }
  else if (p.dockPosition === 'right') a.w -= d;
  else a.h -= d;
  return a;
}

// chrome: bordes (1 + 1) y barra de título (40 + 1 de separador) del marco de Window.jsx
configureWindows({ apps, workArea: workAreaFor(dockPrefs()), chrome: { w: 2, h: 43 } });
wm.open('files');
wm.open('about');

// Fijadas por defecto (el usuario cambia el Dock con «Mantener en el Dock»: settings.dockPinned).
const DOCK_DEFAULT = [...DOCK_ORDER, ...apps.filter((a) => a.dock && !DOCK_ORDER.includes(a.id)).map((a) => a.id)];

function App() {
  const [s] = useSettings();
  const t = makeTheme(s);
  const { windows, visible, focused } = useWindows();
  const [launchpad, setLaunchpad] = useState(false);
  const [control, setControl] = useState(false);
  const [context, setContext] = useState(null);
  const [dockCtx, setDockCtx] = useState(null);
  const launcherApps = useLauncherApps();
  const dp = dockPrefs(s);
  // el área de trabajo sigue al Dock en vivo (posición, tamaño, esconderse solo, otra pantalla)
  useEffect(() => { configureWindows({ apps, workArea: workAreaFor(dp) }); },
    [dp.dockPosition, dp.dockSize, dp.dockAutohide, screen.width, screen.height]);

  // F4 abre el Launchpad; Super también, pero solo si se suelta sin usarla en un atajo
  // (Super+T, Super+D... son de las apps, como Cmd en macOS).
  useEffect(() => {
    let superAlone = false;
    const offDown = addListener('keydown', (e) => {
      if (e.code === 62 && !e.repeat) setLaunchpad((v) => !v);
      if (e.code === 125 || e.code === 126) { if (!e.repeat) superAlone = true; } else superAlone = false;
      if (e.key === 'Escape') { setControl(false); setContext(null); }
    });
    const offUp = addListener('keyup', (e) => {
      if ((e.code === 125 || e.code === 126) && superAlone) setLaunchpad((v) => !v);
      superAlone = false;
    });
    return () => { offDown(); offUp(); };
  }, []);

  // Trackpad: deslizar con 3 dedos hacia arriba abre el Launchpad y hacia abajo lo cierra (como
  // macOS); izquierda/derecha cambian de ventana (lo hace el SDK). Ver docs/TRACKPAD.md.
  useEffect(() => registerGestureActions({ up: () => setLaunchpad(true), down: () => setLaunchpad(false) }), []);

  const wallpaper = t.wallpapers.find((w) => w.id === s.wallpaper) || t.wallpapers[0];
  const launchpadEntry = { id: 'launchpad', name: 'Launchpad', Icon: LaunchpadIcon };
  // Apps fijadas y abiertas; las ventanas de una misma app (también Linux) van en un solo ícono.
  const entries = useDockEntries(DOCK_DEFAULT, { recents: dp.dockRecents ? 3 : 0 });
  const byId = new Map(entries.map((e) => [e.id, e]));

  return (
    <ThemeProvider tokens={t}>
      <View style={{ width: '100%', height: '100%' }}>
        <Image src={wallpaper.src} onContextMenu={(e) => setContext({ x: e.x, y: e.y })}
          style={{ position: 'absolute', left: 0, top: 0, width: '100%', height: '100%', objectFit: 'cover' }} />
        <DesktopWidgets layer="below" />
        {visible.map((w) => <Window key={w.id} win={w} app={getApp(w.id)} t={t} focused={w.id === focused} />)}
        <DesktopWidgets layer="above" />
        <MenuBar t={t} height={MENU_H} focused={focused} controlCenter={control}
          onLaunchpad={() => setLaunchpad(true)} onControlCenter={() => setControl((v) => !v)} />
        <Dock t={t} items={[launchpadEntry, ...entries]} running={entries.filter((e) => e.running).map((e) => e.id)}
          onOpen={(id) => (id === 'launchpad' ? setLaunchpad(true) : byId.has(id) && dockActivate(byId.get(id)))}
          onContext={(id, at) => byId.has(id) && setDockCtx({ entry: byId.get(id), at })}
          onDropApp={(id, index) => dock.pin(id, Math.max(0, index - 1))}
          position={dp.dockPosition} size={dp.dockSize} magnify={dp.dockMagnify} maxSize={dp.dockMagnifySize}
          autohide={dp.dockAutohide} top={MENU_H} keepOpen={!!dockCtx} />
        {dockCtx && (dp.dockPosition === 'bottom'
          ? <Menu above side="right" x={dockCtx.at.x - 110} y={dockCtx.at.y} onClose={() => setDockCtx(null)} items={dockMenu(byId.get(dockCtx.entry.id) || dockCtx.entry)} />
          : <Menu centered side={dp.dockPosition === 'left' ? 'right' : 'left'} x={dockCtx.at.x} y={dockCtx.at.y} onClose={() => setDockCtx(null)}
            items={dockMenu(byId.get(dockCtx.entry.id) || dockCtx.entry)} />)}
        {control && <ControlCenter t={t} top={MENU_H} onClose={() => setControl(false)} />}
        {context && (
          <Menu x={context.x} y={context.y} onClose={() => setContext(null)} items={[
            { label: tr('shell.desktop.wallpaper'), onClick: () => setControl(true) },
            { label: tr('shell.desktop.openLaunchpad'), onClick: () => setLaunchpad(true) },
            '-',
            { label: tr('shell.menu.settings'), onClick: () => wm.open('settings') },
            { label: tr('shell.menu.about'), onClick: () => wm.open('about') },
          ]} />
        )}
        {launchpad && <Launchpad t={t} apps={launcherApps} onOpen={launchApp} onClose={() => setLaunchpad(false)} />}
      </View>
    </ThemeProvider>
  );
}

startShell(<App />);

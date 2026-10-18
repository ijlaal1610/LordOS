// Barra de menú superior: menú L, app activa, menú Ventana, distribución de teclado,
// Centro de control y reloj.
import { View, Text, useClock, wm, getApp, session, useAppMenus, t as tr, appName, formatDate, formatTime, keyboard, useSettings, direction, screen, windowTitle, LordOSLogo, spotlight, PrivacyIndicators, StatusItems, help } from '@lordos/sdk';
import { useRef, useState } from 'react';
import { Menu } from './Menu.jsx';

// Fecha y hora con el formato de la región (sdk/i18n.js).
const format = (d) => `${formatDate(d, 'short')}   ${formatTime(d)}`;

function BarItem({ t, children, onClick, active, anchorRef }) {
  const [hover, setHover] = useState(false);
  return (
    <View ref={anchorRef} onClick={onClick} onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)}
      style={{ height: 22, paddingHorizontal: 9, borderRadius: 5, justifyContent: 'center', flexDirection: 'row', alignItems: 'center',
        backgroundColor: active || hover ? t.menuHover : 'transparent' }}>
      {children}
    </View>
  );
}

const Label = ({ t, children, bold }) => (
  <Text style={{ fontSize: 13, fontWeight: bold ? 700 : 500, color: t.menuText, whiteSpace: 'nowrap' }}>{children}</Text>
);

function ControlGlyph({ t }) {
  const bar = (top, knob) => (
    <View style={{ position: 'absolute', left: 0, top, width: 16, height: 6, borderRadius: 3, border: `1.5px solid ${t.menuText}` }}>
      <View style={{ position: 'absolute', left: knob, top: 0.5, width: 2, height: 2, borderRadius: 1, backgroundColor: t.menuText }} />
    </View>
  );
  return <View style={{ width: 16, height: 14 }}>{bar(0, 9)}{bar(8, 2)}</View>;
}

export function MenuBar({ t, height, focused, onLaunchpad, onControlCenter, controlCenter }) {
  const clock = useClock(format, 1000);
  useSettings();         // la distribución de teclado activa vive en los ajustes
  const [open, setOpen] = useState(null);
  const refs = { logo: useRef(null), window: useRef(null), help: useRef(null) };
  const appRefs = useRef({});
  const app = focused ? getApp(focused) : null;
  const extra = useAppMenus(focused) || [];
  // Posición del menú: debajo del ítem que lo abrió (desde su borde derecho en árabe/hebreo).
  const anchorX = (ref) => {
    if (!ref?.current) return direction() === 'rtl' ? screen.width - 6 : 6;
    const [x, , w] = lord.getLayout(ref.current.id);
    return direction() === 'rtl' ? x + w : x;
  };
  const toggle = (name, ref) => setOpen((o) => (o?.name === name ? null : { name, x: anchorX(ref) }));
  const close = () => setOpen(null);

  const menus = {
    logo: { items: [
      { label: tr('shell.menu.about'), onClick: () => wm.open('about') },
      '-',
      { label: tr('shell.menu.settings'), onClick: () => wm.open('settings') },
      { label: 'Launchpad', hint: 'F4', onClick: onLaunchpad },
      '-',
      ...(session.managed() ? [
        { label: tr('shell.menu.lock'), hint: '❖L', onClick: () => session.lock() },
        { label: tr('shell.menu.switchUser'), onClick: () => session.switchUser() },
      ] : []),
      { label: tr('shell.menu.logout'), onClick: () => session.quit() },
    ] },
    window: { items: [
      { label: tr('shell.window.minimize'), disabled: !app, onClick: () => wm.minimize(focused) },
      { label: tr('shell.window.maximize'), disabled: !app, onClick: () => wm.toggleMaximize(focused) },
      '-',
      { label: tr('shell.window.close'), disabled: !app, onClick: () => wm.close(focused) },
    ] },
    help: { items: [
      ...(getApp('help') ? [{ label: tr('shell.help.open'), onClick: () => wm.open('help') }, { label: tr('shell.help.tour'), onClick: () => help.startTour() }, '-'] : []),
      { label: tr('shell.menu.about'), onClick: () => wm.open('about') },
      { label: tr('shell.help.theme'), disabled: true },
    ] },
  };
  extra.forEach((m, i) => { menus[`app${i}`] = { items: m.items }; });

  const cur = open && menus[open.name];
  const anchor = open && (refs[open.name] || { current: appRefs.current[open.name] });
  return (
    <View style={{ position: 'absolute', left: 0, top: 0, right: 0, height }}>
      <View style={{
        position: 'absolute', left: 0, top: 0, right: 0, height, flexDirection: 'row', alignItems: 'center',
        paddingHorizontal: 8, backgroundColor: t.menuBg, backdropFilter: `blur(${t.blur}px)`,
      }}>
        <BarItem t={t} active={open?.name === 'logo'} anchorRef={refs.logo} onClick={() => toggle('logo', refs.logo)}>
          <LordOSLogo size={18} />
        </BarItem>
        <BarItem t={t}><Label t={t} bold>{app ? (app.builtin ? windowTitle(wm.get(focused)) : appName(app)) : 'LordOS'}</Label></BarItem>
        {extra.map((m, i) => {
          const key = `app${i}`;
          const ref = { get current() { return appRefs.current[key]; } };
          return (
            <BarItem key={key} t={t} active={open?.name === key} anchorRef={(inst) => { appRefs.current[key] = inst; }}
              onClick={() => toggle(key, ref)}><Label t={t}>{m.label}</Label></BarItem>
          );
        })}
        <BarItem t={t} active={open?.name === 'window'} anchorRef={refs.window} onClick={() => toggle('window', refs.window)}><Label t={t}>{tr('shell.menu.window')}</Label></BarItem>
        <BarItem t={t} active={open?.name === 'help'} anchorRef={refs.help} onClick={() => toggle('help', refs.help)}><Label t={t}>{tr('shell.menu.help')}</Label></BarItem>
        <View style={{ flex: 1 }} />
        <BarItem t={t} onClick={() => keyboard.next()}>
          <Label t={t}>{keyboard.label(keyboard.active())}</Label>
        </BarItem>
        <StatusItems color={t.menuText} />
        <PrivacyIndicators />
        <BarItem t={t} onClick={() => spotlight.toggle()}><Label t={t}>⌕</Label></BarItem>
        <BarItem t={t} active={controlCenter} onClick={onControlCenter}><ControlGlyph t={t} /></BarItem>
        <BarItem t={t}><Label t={t}>{clock}</Label></BarItem>
      </View>
      {cur && <Menu x={open.x} y={height + 2} items={cur.items} onClose={close} anchor={anchor} width={260} />}
    </View>
  );
}

// Ajustes → Escritorio y Dock: posición del Dock (abajo, izquierda, derecha), tamaño, lupa, esconderse
// solo y apps recientes; en el tema Clásico, la posición de la barra de tareas. Se guarda en
// settings.json (sdk/dock.js: DOCK_DEFAULTS) y el tema lo aplica en vivo, también el área de trabajo.
import { View, Text, useTheme, useLocale, useSettings, Segmented, Toggle, Slider, dockPrefs, themes } from '@lordos/sdk';

function Row({ label, hint, children }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 12, gap: 16 }}>
      <View style={{ flex: 1 }}>
        <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{label}</Text>
        {hint ? <Text style={{ fontSize: 11, color: t.textDim, marginTop: 3, lineHeight: 1.35 }}>{hint}</Text> : null}
      </View>
      {children}
    </View>
  );
}

const Divider = () => <View style={{ height: 1, backgroundColor: useTheme().separator }} />;

// Un dibujito de la pantalla con el Dock del lado elegido.
function Preview({ side, t }) {
  const W = 120, H = 76, k = 7;
  const bar = side === 'bottom' ? { left: 30, right: 30, bottom: 4, height: k }
    : side === 'top' ? { left: 0, right: 0, top: 0, height: k }
      : side === 'left' ? { left: 4, top: 18, bottom: 14, width: k } : { right: 4, top: 18, bottom: 14, width: k };
  return (
    <View style={{ width: W, height: H, borderRadius: 8, overflow: 'hidden', background: 'linear-gradient(135deg, #4b3aa8, #c0457a)',
      border: `1px solid ${t.separator}`, direction: 'ltr' }}>
      <View style={{ position: 'absolute', left: 0, right: 0, top: 0, height: 5, backgroundColor: 'rgba(0,0,0,0.35)' }} />
      <View style={{ position: 'absolute', ...bar, borderRadius: 3, backgroundColor: 'rgba(255,255,255,0.75)' }} />
    </View>
  );
}

export function DockSettings() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [s, set] = useSettings();
  const p = dockPrefs(s);
  const classic = themes.current() === 'classic';
  if (classic) {
    return (
      <View>
        <Row label={tr('settings.dock.taskbar')} hint={`${tr('settings.dock.taskbarHint')} ${tr('settings.dock.sidesHint')}`}>
          <Preview side={p.taskbarPosition} t={t} />
        </Row>
        <View style={{ flexDirection: 'row', justifyContent: 'flex-end', paddingBottom: 12 }}>
          <Segmented value={p.taskbarPosition} onChange={(v) => set({ taskbarPosition: v })}
            options={['bottom', 'top', 'left', 'right'].map((v) => [v, tr(`settings.dock.${v}`)])} />
        </View>
      </View>
    );
  }
  return (
    <View>
      <Row label={tr('settings.dock.position')} hint={tr('settings.dock.sidesHint')}>
        <Preview side={p.dockPosition} t={t} />
      </Row>
      <View style={{ flexDirection: 'row', justifyContent: 'flex-end', paddingBottom: 12 }}>
        <Segmented value={p.dockPosition} onChange={(v) => set({ dockPosition: v })}
          options={['left', 'bottom', 'right'].map((v) => [v, tr(`settings.dock.${v}`)])} />
      </View>
      <Divider />
      <Row label={tr('settings.dock.size')}>
        <Text style={{ fontSize: 11, color: t.textDim }}>{tr('settings.dock.small')}</Text>
        <Slider value={p.dockSize} min={36} max={80} onChange={(v) => set({ dockSize: Math.round(v) })} />
        <Text style={{ fontSize: 11, color: t.textDim }}>{tr('settings.dock.large')}</Text>
      </Row>
      <Divider />
      <Row label={tr('settings.dock.magnify')} hint={tr('settings.dock.magnifyHint')}>
        <Toggle on={p.dockMagnify} onChange={(dockMagnify) => set({ dockMagnify })} />
      </Row>
      {p.dockMagnify && (
        <Row label={tr('settings.dock.magnifySize')}>
          <Text style={{ fontSize: 11, color: t.textDim }}>{tr('settings.dock.small')}</Text>
          <Slider value={p.dockMagnifySize} min={p.dockSize} max={128} onChange={(v) => set({ dockMagnifySize: Math.round(v) })} />
          <Text style={{ fontSize: 11, color: t.textDim }}>{tr('settings.dock.large')}</Text>
        </Row>
      )}
      <Divider />
      <Row label={tr('settings.dock.autohide')} hint={tr('settings.dock.autohideHint')}>
        <Toggle on={p.dockAutohide} onChange={(dockAutohide) => set({ dockAutohide })} />
      </Row>
      <Divider />
      <Row label={tr('settings.dock.recents')} hint={tr('settings.dock.recentsHint')}>
        <Toggle on={p.dockRecents} onChange={(dockRecents) => set({ dockRecents })} />
      </Row>
    </View>
  );
}

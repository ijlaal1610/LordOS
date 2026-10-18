// Settings → Desktop & Dock: Dock position (bottom, left, right), size, magnification, auto-hide
// and recent apps; in the Classic theme, the taskbar position. Saved in
// settings.json (sdk/dock.js: DOCK_DEFAULTS) and the theme applies it live, including the work area.
import { View, Text, useTheme, useLocale, useSettings, Toggle, Slider, dockPrefs, themes, spacing, typeScale } from '@lordos/sdk';
import { Stack, Section, SettingRow } from './ui.jsx';

// A small picture of the screen with the Dock (or the taskbar) on the chosen side.
function Preview({ side, t }) {
  const W = 96, H = 60, k = 6;
  const bar = side === 'bottom' ? { left: 24, right: 24, bottom: 3, height: k }
    : side === 'top' ? { left: 0, right: 0, top: 0, height: k }
      : side === 'left' ? { left: 3, top: 14, bottom: 10, width: k } : { right: 3, top: 14, bottom: 10, width: k };
  return (
    <View style={{ width: W, height: H, borderRadius: 7, overflow: 'hidden', background: 'linear-gradient(135deg, #4b3aa8, #c0457a)',
      border: `1px solid ${t.separator}`, direction: 'ltr' }}>
      <View style={{ position: 'absolute', left: 0, right: 0, top: 0, height: 4, backgroundColor: 'rgba(0,0,0,0.35)' }} />
      <View style={{ position: 'absolute', ...bar, borderRadius: 3, backgroundColor: 'rgba(255,255,255,0.8)' }} />
    </View>
  );
}

// The sides as picture tiles (like Appearance), the chosen one ringed in the accent color.
function SidePicker({ value, sides, onChange }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  return (
    <View style={{ flexDirection: 'row', flexWrap: 'wrap', justifyContent: 'center', gap: spacing.l, paddingVertical: spacing.m }}>
      {sides.map((v) => (
        <View key={v} onClick={() => onChange(v)} style={{ alignItems: 'center', gap: 6 }}>
          <View style={{ padding: 2, borderRadius: 9, border: `2px solid ${value === v ? t.accent : 'transparent'}` }}>
            <Preview side={v} t={t} />
          </View>
          <Text style={{ fontSize: typeScale.callout, fontWeight: value === v ? 600 : 400, color: t.text }}>{tr(`settings.dock.${v}`)}</Text>
        </View>
      ))}
    </View>
  );
}

function SizeSlider({ value, min, max, onChange }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const cap = { fontSize: typeScale.caption, color: t.textDim };
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: spacing.s }}>
      <Text style={cap}>{tr('settings.dock.small')}</Text>
      <Slider value={value} min={min} max={max} width={150} onChange={(v) => onChange(Math.round(v))} />
      <Text style={cap}>{tr('settings.dock.large')}</Text>
    </View>
  );
}

export function DockSettings() {
  const { t: tr } = useLocale();
  const [s, set] = useSettings();
  const p = dockPrefs(s);
  const classic = themes.current() === 'classic';
  if (classic) {
    return (
      <Stack>
        <Section title={tr('settings.dock.taskbar')} footer={`${tr('settings.dock.taskbarHint')} ${tr('settings.dock.sidesHint')}`}>
          <SidePicker value={p.taskbarPosition} sides={['bottom', 'top', 'left', 'right']} onChange={(v) => set({ taskbarPosition: v })} />
        </Section>
      </Stack>
    );
  }
  return (
    <Stack>
      <Section title={tr('settings.dock.position')} footer={tr('settings.dock.sidesHint')}>
        <SidePicker value={p.dockPosition} sides={['left', 'bottom', 'right']} onChange={(v) => set({ dockPosition: v })} />
      </Section>
      <Section>
        <SettingRow label={tr('settings.dock.size')}>
          <SizeSlider value={p.dockSize} min={36} max={80} onChange={(dockSize) => set({ dockSize })} />
        </SettingRow>
        <SettingRow label={tr('settings.dock.magnify')} hint={tr('settings.dock.magnifyHint')}>
          <Toggle small on={p.dockMagnify} onChange={(dockMagnify) => set({ dockMagnify })} />
        </SettingRow>
        {p.dockMagnify && (
          <SettingRow label={tr('settings.dock.magnifySize')}>
            <SizeSlider value={p.dockMagnifySize} min={p.dockSize} max={128} onChange={(dockMagnifySize) => set({ dockMagnifySize })} />
          </SettingRow>
        )}
      </Section>
      <Section>
        <SettingRow label={tr('settings.dock.autohide')} hint={tr('settings.dock.autohideHint')}>
          <Toggle small on={p.dockAutohide} onChange={(dockAutohide) => set({ dockAutohide })} />
        </SettingRow>
        <SettingRow label={tr('settings.dock.recents')} hint={tr('settings.dock.recentsHint')}>
          <Toggle small on={p.dockRecents} onChange={(dockRecents) => set({ dockRecents })} />
        </SettingRow>
      </Section>
    </Stack>
  );
}

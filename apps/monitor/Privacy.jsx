// Privacy.jsx — the "Monitor" item of Settings → Privacy (another module builds the page and the
// Monitor registers itself with registerPrivacySection, if it exists). Two switches, saved in
// settings: see each app's connections and place them on the map with the local country database.
// Drawn like the Settings sections: a 13/600 header, an inset GroupBox and a footer.
import { View, Text, Toggle, Button, GroupBox, useTheme, useLocale, useSettings, wm, spacing, typeScale } from '@lordos/sdk';

function Row({ label, hint, children }) {
  const t = useTheme();
  return (
    <View style={{ minHeight: 36, flexDirection: 'row', alignItems: 'center', paddingVertical: 7, gap: spacing.m }}>
      <View style={{ flex: 1, minWidth: 0, gap: 2 }}>
        <Text style={{ fontSize: typeScale.body, color: t.text, textAlign: 'start' }}>{label}</Text>
        {hint ? <Text style={{ fontSize: typeScale.caption, color: t.textDim, lineHeight: 1.35, textAlign: 'start' }}>{hint}</Text> : null}
      </View>
      {children}
    </View>
  );
}

export function MonitorPrivacy() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [s, set] = useSettings();
  const m = s.monitor || {};
  const put = (patch) => set({ monitor: { ...m, ...patch } });
  return (
    <View style={{ gap: 6 }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', minHeight: 22, paddingHorizontal: spacing.xs, gap: spacing.s }}>
        <Text style={{ flex: 1, fontSize: typeScale.body, fontWeight: 600, color: t.text, textAlign: 'start' }}>{tr('monitor.privacy.title')}</Text>
        <Button small plain label={tr('monitor.privacy.open')} onClick={() => wm.open('monitor', { args: { tab: 'network', nonce: Date.now() } })} />
      </View>
      <GroupBox>
        <Row label={tr('monitor.privacy.connections')} hint={tr('monitor.privacy.connectionsHint')}>
          <Toggle small on={m.connections !== false} onChange={(on) => put({ connections: on })} />
        </Row>
        <Row label={tr('monitor.privacy.geo')} hint={tr('monitor.privacy.geoHint')}>
          <Toggle small on={m.geo !== false} onChange={(on) => put({ geo: on })} />
        </Row>
      </GroupBox>
      <Text style={{ fontSize: typeScale.caption, color: t.textDim, lineHeight: 1.4, marginHorizontal: spacing.xs, textAlign: 'start' }}>{tr('monitor.privacy.local')}</Text>
    </View>
  );
}

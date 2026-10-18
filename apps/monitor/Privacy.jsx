// Privacy.jsx — el ítem "Monitor" de Ajustes → Privacidad (la página la arma otro módulo y el
// Monitor se registra con registerPrivacySection, si existe). Dos interruptores, guardados en
// ajustes: ver las conexiones de cada app y ubicarlas en el mapa con la base local de países.
import { View, Text, Toggle, Button, useTheme, useLocale, useSettings, wm } from '@lordos/sdk';

function Row({ label, hint, children }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 10, gap: 16 }}>
      <View style={{ flex: 1 }}>
        <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{label}</Text>
        {hint ? <Text style={{ fontSize: 11, color: t.textDim, marginTop: 3, lineHeight: 1.35 }}>{hint}</Text> : null}
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
    <View>
      <Text style={{ fontSize: 14, fontWeight: 700, color: t.text, marginTop: 8 }}>{tr('monitor.privacy.title')}</Text>
      <Row label={tr('monitor.privacy.connections')} hint={tr('monitor.privacy.connectionsHint')}>
        <Toggle on={m.connections !== false} onChange={(on) => put({ connections: on })} />
      </Row>
      <View style={{ height: 1, backgroundColor: t.separator }} />
      <Row label={tr('monitor.privacy.geo')} hint={tr('monitor.privacy.geoHint')}>
        <Toggle on={m.geo !== false} onChange={(on) => put({ geo: on })} />
      </Row>
      <Text style={{ fontSize: 11, color: t.textDim, lineHeight: 1.45, marginTop: 4 }}>{tr('monitor.privacy.local')}</Text>
      <View style={{ flexDirection: 'row', marginTop: 10 }}>
        <Button label={tr('monitor.privacy.open')} onClick={() => wm.open('monitor', { args: { tab: 'network', nonce: Date.now() } })} />
      </View>
    </View>
  );
}

// Ajustes → Portapapeles: historial guardado, límites, privacidad (sdk/clipboard.js).
import {
  View, Text, Button, Toggle, Stepper, TextInput, useTheme, useLocale, useSettings, useStore, clipboard, clipboardHistory, clipboardPanel,
} from '@lordos/sdk';
import { useState } from 'react';

const DEFAULTS = { save: true, max: 500, maxMB: 200, expireDays: 30, ignoreApps: [], clearOnLogout: false };
const Divider = () => <View style={{ height: 1, backgroundColor: useTheme().separator }} />;

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

export function ClipboardSettings() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [s, set] = useSettings();
  const c = { ...DEFAULTS, ...(s.clipboard || {}) };
  const put = (patch) => set({ clipboard: { ...c, ...patch } });
  const list = useStore(clipboardHistory);
  const [app, setApp] = useState('');
  const origins = [...new Set(list.map((x) => x.origin).filter((o) => o && !['lordos', 'terminal', 'linux'].includes(o) && !c.ignoreApps.includes(o)))].slice(0, 8);
  return (
    <View style={{ paddingBottom: 24 }}>
      <Row label={tr('settings.clipboard.save')} hint={tr('settings.clipboard.saveHint')}>
        <Toggle on={c.save} onChange={(save) => put({ save })} />
      </Row>
      <Divider />
      <Row label={tr('settings.clipboard.max')}>
        <Stepper value={c.max} min={50} max={5000} step={50} onChange={(max) => put({ max })} width={60} />
      </Row>
      <Row label={tr('settings.clipboard.maxMB')} hint={tr('settings.clipboard.maxMBHint')}>
        <Stepper value={c.maxMB} min={20} max={2000} step={20} onChange={(maxMB) => put({ maxMB })} format={(v) => `${v} MB`} width={70} />
      </Row>
      <Row label={tr('settings.clipboard.expire')} hint={tr('settings.clipboard.expireHint')}>
        <Stepper value={c.expireDays} min={0} max={365} step={1} onChange={(expireDays) => put({ expireDays })}
          format={(v) => (v ? tr('settings.clipboard.days', { count: v }) : tr('settings.clipboard.never'))} width={130} />
      </Row>
      <Divider />
      <Row label={tr('settings.clipboard.clearOnLogout')}>
        <Toggle on={c.clearOnLogout} onChange={(clearOnLogout) => put({ clearOnLogout })} />
      </Row>
      <Row label={tr('settings.clipboard.ignore')} hint={tr('settings.clipboard.ignoreHint')} />
      <View style={{ gap: 6 }}>
        {c.ignoreApps.map((a) => (
          <View key={a} style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
            <Text style={{ flex: 1, fontSize: 13, color: t.text }}>{a}</Text>
            <Button label="−" onClick={() => put({ ignoreApps: c.ignoreApps.filter((x) => x !== a) })} />
          </View>
        ))}
        {origins.length > 0 && (
          <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 6 }}>
            {origins.map((o) => <Button key={o} label={`+ ${o}`} onClick={() => put({ ignoreApps: [...c.ignoreApps, o] })} />)}
          </View>
        )}
        <View style={{ flexDirection: 'row', gap: 8 }}>
          <TextInput value={app} onChange={(v) => setApp(v.replace(/[^A-Za-z0-9._-]/g, '').slice(0, 63))} placeholder={tr('settings.clipboard.appId')} style={{ flex: 1 }} />
          <Button label={tr('settings.clipboard.add')} onClick={() => { if (app && !c.ignoreApps.includes(app)) put({ ignoreApps: [...c.ignoreApps, app] }); setApp(''); }} />
        </View>
      </View>
      <Divider />
      <Row label={tr('settings.clipboard.history', { count: list.length })} hint={tr('settings.clipboard.historyHint')}>
        <Button label={tr('settings.clipboard.open')} onClick={() => clipboardPanel.set('full')} />
        <Button label={tr('settings.clipboard.clearAll')} onClick={() => clipboard.clear({ keepPinned: false })} />
      </Row>
      <Text style={{ fontSize: 11, color: t.textDim, lineHeight: 1.4 }}>{tr('settings.clipboard.privacy')}</Text>
    </View>
  );
}

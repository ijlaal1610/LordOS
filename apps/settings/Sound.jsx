// Ajustes → Sonido: salidas, entradas (con medidor), volumen por app (PipeWire, sdk/sound.js).
import {
  View, Text, useTheme, useLocale, Slider, Toggle, Button, Segmented, useSound, useSoundPeak, sound, defaultSink,
  microphones, defaultSource,
} from '@lordos/sdk';
import { useEffect } from 'react';

const Divider = () => <View style={{ height: 1, backgroundColor: useTheme().separator }} />;

function Volume({ kind, item, width = 220 }) {
  const t = useTheme();
  const pct = Math.round(item.level * 100);
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10, direction: 'ltr' }}>
      <Slider value={Math.min(1.5, item.level)} max={1.5} width={width} onChange={(v) => sound.setVolume(kind, item.index, v)} />
      <Text style={{ width: 40, fontSize: 12, color: t.textDim, textAlign: 'end' }}>{`${pct} %`}</Text>
      <Toggle on={!item.mute} onChange={(on) => sound.setMute(kind, item.index, !on)} />
    </View>
  );
}

// Lista de dispositivos: el predeterminado va marcado; un clic lo elige.
function Devices({ kind, list }) {
  const t = useTheme();
  return (
    <View style={{ gap: 4, marginVertical: 6 }}>
      {list.map((d) => (
        <View key={d.index} onClick={() => !d.default && sound.setDefault(kind, d.name)} style={{ flexDirection: 'row', alignItems: 'center', gap: 10,
          paddingHorizontal: 10, paddingVertical: 7, borderRadius: 7, backgroundColor: d.default ? t.accent : t.hover }}>
          <Text style={{ flex: 1, fontSize: 13, color: d.default ? '#fff' : t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{d.description || d.name}</Text>
          {d.state === 'running' && <Text style={{ fontSize: 11, color: d.default ? '#fff' : t.textDim }}>●</Text>}
        </View>
      ))}
    </View>
  );
}

function Ports({ kind, dev }) {
  if (!dev || dev.ports.length < 2) return null;
  return (
    <Segmented value={dev.activePort} onChange={(p) => sound.setPort(kind, dev.index, p)}
      options={dev.ports.map((p) => [p.name, p.description || p.name])} />
  );
}

function Meter({ source }) {
  const t = useTheme();
  const peak = useSoundPeak();
  useEffect(() => { if (source) sound.meter(source.index); return () => sound.meter(-1); }, [source?.index]);
  const v = peak.source === source?.index ? Math.min(1, peak.level) : 0;
  return (
    <View style={{ width: 220, height: 6, borderRadius: 3, backgroundColor: t.control, direction: 'ltr' }}>
      <View style={{ width: Math.round(220 * v), height: 6, borderRadius: 3, backgroundColor: v > 0.85 ? '#ff453a' : '#30d158' }} />
    </View>
  );
}

function Row({ label, children }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 10, gap: 16 }}>
      <Text style={{ flex: 1, fontSize: 13, fontWeight: 600, color: t.text }}>{label}</Text>
      {children}
    </View>
  );
}

export function SoundSettings() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const st = useSound();
  if (!sound.available() || !st.connected) {
    return <Text style={{ fontSize: 13, color: t.textDim, marginTop: 8 }}>{tr('settings.sound.unavailable')}</Text>;
  }
  const out = defaultSink(st);
  const mics = microphones(st);
  const mic = defaultSource(st);
  const apps = st.inputs;
  return (
    <View style={{ paddingBottom: 20 }}>
      <Text style={{ fontSize: 14, fontWeight: 700, color: t.text, marginTop: 8 }}>{tr('settings.sound.output')}</Text>
      {st.sinks.length === 0 ? <Text style={{ fontSize: 12, color: t.textDim, marginVertical: 6 }}>{tr('settings.sound.noOutputs')}</Text>
        : <Devices kind="sink" list={st.sinks} />}
      {out && <Row label={tr('settings.sound.volume')}><Volume kind="sink" item={out} /></Row>}
      {out && out.ports.length > 1 && <Row label={tr('settings.sound.port')}><Ports kind="sink" dev={out} /></Row>}
      {out && <Row label={tr('settings.sound.test')}><Button label={tr('settings.sound.play')} onClick={() => sound.play('notify')} /></Row>}
      <Divider />
      <Text style={{ fontSize: 14, fontWeight: 700, color: t.text, marginTop: 14 }}>{tr('settings.sound.input')}</Text>
      {mics.length === 0 ? <Text style={{ fontSize: 12, color: t.textDim, marginVertical: 6 }}>{tr('settings.sound.noInputs')}</Text>
        : <Devices kind="source" list={mics} />}
      {mic && <Row label={tr('settings.sound.volume')}><Volume kind="source" item={mic} /></Row>}
      {mic && <Row label={tr('settings.sound.level')}><Meter source={mic} /></Row>}
      <Divider />
      <Text style={{ fontSize: 14, fontWeight: 700, color: t.text, marginTop: 14 }}>{tr('settings.sound.apps')}</Text>
      {apps.length === 0 && <Text style={{ fontSize: 12, color: t.textDim, marginVertical: 6 }}>{tr('settings.sound.noApps')}</Text>}
      {apps.map((a) => (
        <Row key={a.index} label={a.app || a.name}>
          {a.hasVolume ? <Volume kind="input" item={a} width={180} /> : <Toggle on={!a.mute} onChange={(on) => sound.setMute('input', a.index, !on)} />}
        </Row>
      ))}
    </View>
  );
}

// Settings → Displays: resolution, refresh rate, rotation, the main display, brightness and Night
// Shift (docs/SETTINGS.md, "Displays"). Changes apply live with the system's 15-second "Keep these
// display settings?" countdown (sdk/hardware.jsx) and are saved per display (EDID identity).
import {
  View, Text, useTheme, useLocale, useSettings, Slider, Toggle, Segmented, Button, admin, useDisplays, applyDisplay, DisplayGlyph,
  nightPrefs, nightActive, setNightManual, previewNight, nightTick, sunTimes, formatTime,
} from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';
import { GroupBox, SettingRow, PopUp, Note, Stack, TimeField } from './ui.jsx';

const hz = (mhz) => {
  const v = mhz / 1000;
  return Math.abs(v - Math.round(v)) < 0.005 ? String(Math.round(v)) : v.toFixed(2);
};

// The display drawn in the arrangement area: proportional to its resolution, with its name.
function Arrangement({ outs, current, onPick, t, tr }) {
  const maxW = 150, maxH = 96;
  return (
    <View style={{ height: 150, borderRadius: 10, alignItems: 'center', justifyContent: 'center', flexDirection: 'row', gap: 18,
      backgroundColor: t.mode === 'dark' ? 'rgba(0,0,0,0.22)' : 'rgba(0,0,0,0.05)', border: `1px solid ${t.separator}` }}>
      {outs.map((o) => {
        const w = o.mode?.w || o.preferred.w || 16, h = o.mode?.h || o.preferred.h || 10;
        const k = Math.min(maxW / w, maxH / h);
        const on = o.id === current?.id;
        return (
          <View key={o.id} onClick={() => !on && onPick(o)} style={{ alignItems: 'center', gap: 6 }}>
            <View style={{ width: Math.round(w * k), height: Math.round(h * k), borderRadius: 6, alignItems: 'center', justifyContent: 'center',
              background: on ? 'linear-gradient(135deg, #3b6fd8, #8f4fd1)' : 'linear-gradient(135deg, #5b5f6a, #3a3d45)',
              border: on ? `2px solid ${t.accent}` : `1px solid ${t.separator}`, boxShadow: '0 4px 14px rgba(0,0,0,0.3)' }}>
              {on ? <View style={{ position: 'absolute', left: 3, right: 3, top: 3, height: 4, borderRadius: 2, backgroundColor: 'rgba(255,255,255,0.75)' }} /> : null}
              <Text style={{ fontSize: 11, fontWeight: 600, color: '#fff' }}>{o.mode ? `${o.mode.w}×${o.mode.h}` : tr('prefs.display.notInUse')}</Text>
            </View>
            <Text style={{ fontSize: 11, color: on ? t.text : t.textDim }}>{o.name}</Text>
          </View>
        );
      })}
    </View>
  );
}

function Brightness({ list }) {
  const { t: tr } = useLocale();
  const [vals, setVals] = useState({});
  const timer = useRef(0);
  const [err, setErr] = useState('');
  if (!list.length) return <SettingRow label={tr('prefs.display.brightness')} hint={tr('prefs.display.noBacklight')} disabled />;
  return list.map((b) => {
    const v = vals[b.name] ?? b.value;
    const set = (x) => {
      const n = Math.max(1, Math.round(x));          // 0 turns some panels fully off
      setVals((o) => ({ ...o, [b.name]: n }));
      clearTimeout(timer.current);
      timer.current = setTimeout(() => admin.run('display-brightness', `${b.name} ${n}`, undefined, { quiet: true }).then(() => setErr(''), (e) => setErr(e.message)), 120);
    };
    return (
      <SettingRow key={b.name} label={tr('prefs.display.brightness')} hint={err || (list.length > 1 ? b.name : '')}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8, direction: 'ltr' }}>
          <Text style={{ fontSize: 12, color: '#8e8e93' }}>☼</Text>
          <Slider value={v} min={0} max={b.max} width={200} onChange={set} />
          <Text style={{ fontSize: 15, color: '#8e8e93' }}>☀</Text>
        </View>
      </SettingRow>
    );
  });
}

function NightShift() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [s, set] = useSettings();
  const p = nightPrefs(s);
  const [preview, setPreview] = useState(null);
  const tm = useRef(0);
  const patch = (x) => set({ night: { ...p, ...x } });
  const sun = p.schedule === 'sun' ? sunTimes() : null;
  const active = nightActive();
  useEffect(() => { nightTick(); }, [s.night]);
  const fmt = (m) => { const d = new Date(); d.setHours(Math.floor(m / 60), m % 60, 0, 0); return formatTime(d); };
  const scheduleHint = p.schedule === 'sun'
    ? (sun ? tr('prefs.night.sunHint', { set: fmt(sun.sunset), rise: fmt(sun.sunrise) }) : tr('prefs.night.noSun'))
    : p.schedule === 'custom' ? tr('prefs.night.customHint') : tr('prefs.night.offHint');
  return (
    <GroupBox title={tr('prefs.night.title')} footer={tr('prefs.night.footer')}>
      <SettingRow label={tr('prefs.night.schedule')} hint={scheduleHint}>
        <PopUp value={p.schedule} onChange={(schedule) => patch({ schedule, manual: null })}
          options={[['off', tr('prefs.night.off')], ['sun', tr('prefs.night.sun')], ['custom', tr('prefs.night.custom')]]} />
      </SettingRow>
      {p.schedule === 'custom' && (
        <SettingRow label={tr('prefs.night.from')}>
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
            <TimeField value={p.from} onChange={(from) => patch({ from })} format={fmt} />
            <Text style={{ fontSize: 12, color: t.textDim }}>{tr('prefs.night.to')}</Text>
            <TimeField value={p.to} onChange={(to) => patch({ to })} format={fmt} />
          </View>
        </SettingRow>
      )}
      <SettingRow label={tr('prefs.night.now')}
        hint={p.manual && Date.now() < p.manual.until ? tr(p.manual.on ? 'prefs.night.manualOn' : 'prefs.night.manualOff', { time: formatTime(new Date(p.manual.until)) }) : ''}>
        <Toggle on={active} onChange={(on) => setNightManual(on)} />
      </SettingRow>
      <SettingRow label={tr('prefs.night.temp')}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8, direction: 'ltr' }}>
          <Text style={{ fontSize: 11, color: t.textDim }}>{tr('prefs.night.lessWarm')}</Text>
          {/* the slider goes from less warm (left) to more warm (right) */}
          <Slider value={6500 - (preview ?? p.temp)} min={6500 - 6000} max={6500 - 1900} width={170}
            onChange={(v) => { const k = Math.round(6500 - v); setPreview(k); previewNight(k); clearTimeout(tm.current);
              tm.current = setTimeout(() => { setPreview(null); patch({ temp: k }); }, 400); }} />
          <Text style={{ fontSize: 11, color: t.textDim }}>{tr('prefs.night.moreWarm')}</Text>
        </View>
      </SettingRow>
    </GroupBox>
  );
}

export function DisplaysSettings() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const d = useDisplays();
  const [msg, setMsg] = useState('');
  if (!d) return <Note>{tr('prefs.display.unavailable')}</Note>;
  const outs = d.outputs || [];
  const cur = outs.find((o) => o.current) || outs[0];
  if (!d.onMonitor || !cur) {
    return (
      <Stack>
        <Note>{tr('prefs.display.notOnMonitor')}</Note>
        <NightShift />
      </Stack>
    );
  }
  const mode = cur.mode || { w: cur.preferred.w, h: cur.preferred.h, refresh: 0 };
  const res = cur.resolutions || [];
  const curRes = res.find((r) => r.w === mode.w && r.h === mode.h) || res[0];
  const go = (o, x) => { if (!applyDisplay(o, x)) setMsg(tr('prefs.display.failed')); else setMsg(''); };
  const native = (r) => r.w === cur.preferred.w && r.h === cur.preferred.h;
  const sizeLine = [cur.connector, cur.diagonal ? tr('prefs.display.inches', { n: (cur.diagonal / 10).toFixed(1) }) : '', `${mode.w} × ${mode.h}`].filter(Boolean).join(' · ');
  return (
    <Stack>
      <Arrangement outs={outs} current={cur} t={t} tr={tr} onPick={(o) => go(o, { conn: o.id, w: 0, h: 0, refresh: 0, rotation: 0 })} />
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 12, marginTop: -6 }}>
        <DisplayGlyph size={34} color={t.accent} />
        <View style={{ flex: 1 }}>
          <Text style={{ fontSize: 14, fontWeight: 700, color: t.text, textAlign: 'start' }}>{cur.name}</Text>
          <Text style={{ fontSize: 11, color: t.textDim, textAlign: 'start' }}>{sizeLine}</Text>
        </View>
      </View>
      <GroupBox>
        <SettingRow label={tr('prefs.display.resolution')}>
          <PopUp width={230} value={`${curRes?.w}x${curRes?.h}`} onChange={(v) => { const [w, h] = v.split('x').map(Number); go(cur, { w, h, refresh: 0, rotation: d.rotation }); }}
            options={res.map((r) => [`${r.w}x${r.h}`, native(r) ? tr('prefs.display.native', { w: r.w, h: r.h }) : `${r.w} × ${r.h}`])} />
        </SettingRow>
        <SettingRow label={tr('prefs.display.refresh')}>
          <PopUp value={mode.refresh} onChange={(refresh) => go(cur, { w: mode.w, h: mode.h, refresh, rotation: d.rotation })}
            options={(curRes?.rates || []).map((r) => [r, tr('prefs.display.hz', { hz: hz(r) })])} disabled={(curRes?.rates || []).length < 2} />
        </SettingRow>
        <SettingRow label={tr('prefs.display.rotation')} hint={d.rotation ? tr('prefs.display.rotationHint') : ''}>
          <PopUp value={d.rotation} onChange={(rotation) => go(cur, { w: mode.w, h: mode.h, refresh: mode.refresh, rotation })}
            options={[[0, tr('prefs.display.rotStandard')], [1, '90°'], [2, '180°'], [3, '270°']]} />
        </SettingRow>
        <SettingRow label={tr('prefs.display.scale')} hint={tr('prefs.display.scaleHint')} disabled>
          <Segmented small value="100" onChange={() => {}} options={[['100', '100 %'], ['125', '125 %'], ['150', '150 %'], ['200', '200 %']]} />
        </SettingRow>
      </GroupBox>
      {msg ? <Note tone="warn">{msg}</Note> : null}
      <GroupBox>
        <Brightness list={d.backlight || []} />
      </GroupBox>
      <NightShift />
      {d.softTint ? <Note>{tr('prefs.night.software')}</Note> : null}
      <GroupBox title={tr('prefs.display.multiple')} footer={tr('prefs.display.multipleHint')}>
        <SettingRow label={tr('prefs.display.useAs')} disabled>
          <Segmented small value="extend" onChange={() => {}} options={[['extend', tr('prefs.display.extend')], ['mirror', tr('prefs.display.mirror')]]} />
        </SettingRow>
        {outs.filter((o) => o.id !== cur.id).map((o) => (
          <SettingRow key={o.id} label={o.name} hint={o.connector}>
            <Button small label={tr('prefs.display.makeMain')} onClick={() => go(o, { conn: o.id, w: 0, h: 0, refresh: 0, rotation: 0 })} />
          </SettingRow>
        ))}
        {outs.length < 2 ? <SettingRow label={tr('prefs.display.onlyOne')} /> : null}
      </GroupBox>
    </Stack>
  );
}

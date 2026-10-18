// Settings → Displays: arrangement of several displays (extend or mirror), resolution, refresh rate,
// rotation, scale (100–200 %, sharp), the main display, brightness and Night Shift (docs/SETTINGS.md,
// "Displays", and docs/DISPLAYS.md). Changes apply live with the system's 15-second "Keep these
// display settings?" countdown (sdk/hardware.jsx) and are saved per display (EDID identity).
import {
  View, Text, useTheme, useLocale, useSettings, Slider, Toggle, Segmented, Button, admin, useDisplays, applyDisplay, DisplayGlyph, addListener,
  arrangeDisplay, setDisplayMirror, setMainDisplay, setCaptureAllDisplays,
  nightPrefs, nightActive, setNightManual, previewNight, nightTick, sunTimes, formatTime,
} from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';
import { GroupBox, SettingRow, PopUp, Note, Stack, TimeField } from './ui.jsx';

const hz = (mhz) => {
  const v = mhz / 1000;
  return Math.abs(v - Math.round(v)) < 0.005 ? String(Math.round(v)) : v.toFixed(2);
};

// The arrangement (docs/DISPLAYS.md): every active display at its place in the desktop, proportional to
// its logical size. Drag one to move it next to the others (lord-server snaps it: edges touch, no
// overlap); click one to choose it below. The white bar marks the main display (menu bar, Dock).
function Arrangement({ outs, sel, onSelect, mirror, t, tr }) {
  const act = outs.filter((o) => o.active && o.w);
  const W = 520, H = 170;
  const [drag, setDrag] = useState(null);       // { id, x, y } in logical px while dragging
  const st = useRef(null);
  useEffect(() => {
    const offMove = addListener('pointermove', (e) => {
      const d = st.current;
      if (!d) return;
      if (!d.moved && Math.hypot(e.x - d.px, e.y - d.py) < 4) return;
      d.moved = true;
      const r = arrangeDisplay(d.id, d.ox + (e.x - d.px) / d.k, d.oy + (e.y - d.py) / d.k, false);
      if (r) setDrag({ id: d.id, x: r.x, y: r.y });
    });
    const offUp = addListener('pointerup', (e) => {
      const d = st.current;
      st.current = null;
      if (!d) return;
      if (d.moved) arrangeDisplay(d.id, d.ox + (e.x - d.px) / d.k, d.oy + (e.y - d.py) / d.k, true);
      setTimeout(() => setDrag(null), 400);   // until the new list arrives
    });
    return () => { offMove(); offUp(); };
  }, []);
  if (!act.length) return null;
  // mirror: the displays are drawn one behind the other; extend: at their place
  const pos = (o, i) => (mirror ? { x: i * 18, y: i * 14 } : drag && drag.id === o.id ? drag : { x: o.x, y: o.y });
  const boxes = act.map((o, i) => ({ o, ...pos(o, i) }));
  const x0 = Math.min(...boxes.map((b) => b.x)), y0 = Math.min(...boxes.map((b) => b.y));
  const x1 = Math.max(...boxes.map((b) => b.x + b.o.w)), y1 = Math.max(...boxes.map((b) => b.y + b.o.h));
  const k = Math.min((W - 40) / Math.max(1, x1 - x0), (H - 30) / Math.max(1, y1 - y0));
  const offX = (W - (x1 - x0) * k) / 2, offY = (H - (y1 - y0) * k) / 2;
  return (
    <View style={{ height: H, borderRadius: 10, direction: 'ltr', overflow: 'hidden',
      backgroundColor: t.mode === 'dark' ? 'rgba(0,0,0,0.22)' : 'rgba(0,0,0,0.05)', border: `1px solid ${t.separator}` }}>
      {boxes.map(({ o, x, y }) => {
        const on = o.id === sel;
        return (
          <View key={o.id}
            onMouseDown={(e) => { onSelect(o.id); if (!mirror && act.length > 1) st.current = { id: o.id, px: e.x, py: e.y, ox: o.x, oy: o.y, k, moved: false }; }}
            style={{ position: 'absolute', left: offX + (x - x0) * k, top: offY + (y - y0) * k, width: Math.max(24, o.w * k - 2), height: Math.max(16, o.h * k - 2),
              borderRadius: 5, alignItems: 'center', justifyContent: 'center', cursor: !mirror && act.length > 1 ? 'grab' : 'pointer',
              background: on ? 'linear-gradient(135deg, #3b6fd8, #8f4fd1)' : 'linear-gradient(135deg, #5b5f6a, #3a3d45)',
              border: on ? `2px solid ${t.accent}` : `1px solid ${t.separator}`, boxShadow: '0 4px 14px rgba(0,0,0,0.3)' }}>
            {o.main ? <View style={{ position: 'absolute', left: 3, right: 3, top: 3, height: 4, borderRadius: 2, backgroundColor: 'rgba(255,255,255,0.8)' }} /> : null}
            <Text style={{ fontSize: 11, fontWeight: 700, color: '#fff', textAlign: 'center' }}>{o.name}</Text>
            <Text style={{ fontSize: 10, color: 'rgba(255,255,255,0.8)', textAlign: 'center' }}>{`${o.mode.w}×${o.mode.h} · ${o.scale} %`}</Text>
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
  const [s, set] = useSettings();
  const [msg, setMsg] = useState('');
  const [selId, setSel] = useState(null);
  if (!d) return <Note>{tr('prefs.display.unavailable')}</Note>;
  const outs = d.outputs || [];
  const act = outs.filter((o) => o.active);
  const cur = outs.find((o) => o.id === selId && o.active) || outs.find((o) => o.main) || act[0] || outs[0];
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
  const scale = cur.scale || 100, rot = cur.rotation || 0, rec = cur.recommended || 100;
  const looks = scale !== 100 && cur.w ? tr('prefs.display.looksLike', { w: cur.w, h: cur.h }) : '';
  const sizeLine = [cur.connector, cur.diagonal ? tr('prefs.display.inches', { n: (cur.diagonal / 10).toFixed(1) }) : '', `${mode.w} × ${mode.h}`, looks].filter(Boolean).join(' · ');
  const scales = [100, 125, 150, 175, 200].map((n) => [n, n === rec ? tr('prefs.display.scaleRecommended', { n }) : tr('prefs.display.scaleN', { n })]);
  const several = act.length > 1 || outs.length > 1;
  return (
    <Stack>
      <Arrangement outs={outs} sel={cur.id} onSelect={setSel} mirror={d.mirror} t={t} tr={tr} />
      {act.length > 1 && !d.mirror ? <Text style={{ fontSize: 11, color: t.textDim, textAlign: 'start', marginTop: -12 }}>{tr('prefs.display.arrangeHint')}</Text> : null}
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 12, marginTop: -6 }}>
        <DisplayGlyph size={34} color={t.accent} />
        <View style={{ flex: 1 }}>
          <Text style={{ fontSize: 14, fontWeight: 700, color: t.text, textAlign: 'start' }}>{cur.name}{cur.main && several ? ` · ${tr('prefs.display.mainLabel')}` : ''}</Text>
          <Text style={{ fontSize: 11, color: t.textDim, textAlign: 'start' }}>{sizeLine}</Text>
        </View>
        {several && !cur.main && cur.active ? <Button small label={tr('prefs.display.makeMain')} onClick={() => setMainDisplay(cur.id)} /> : null}
      </View>
      <GroupBox>
        <SettingRow label={tr('prefs.display.scale')} hint={tr('prefs.display.scaleHint')}>
          <PopUp value={scale} onChange={(sc) => go(cur, { w: mode.w, h: mode.h, refresh: mode.refresh, rotation: rot, scale: sc })} options={scales} />
        </SettingRow>
        <SettingRow label={tr('prefs.display.resolution')}>
          <PopUp width={230} value={`${curRes?.w}x${curRes?.h}`} onChange={(v) => { const [w, h] = v.split('x').map(Number); go(cur, { w, h, refresh: 0, rotation: rot, scale }); }}
            options={res.map((r) => [`${r.w}x${r.h}`, native(r) ? tr('prefs.display.native', { w: r.w, h: r.h }) : `${r.w} × ${r.h}`])} />
        </SettingRow>
        <SettingRow label={tr('prefs.display.refresh')}>
          <PopUp value={mode.refresh} onChange={(refresh) => go(cur, { w: mode.w, h: mode.h, refresh, rotation: rot, scale })}
            options={(curRes?.rates || []).map((r) => [r, tr('prefs.display.hz', { hz: hz(r) })])} disabled={(curRes?.rates || []).length < 2} />
        </SettingRow>
        <SettingRow label={tr('prefs.display.rotation')} hint={rot ? tr('prefs.display.rotationHint') : ''}>
          <PopUp value={rot} onChange={(rotation) => go(cur, { w: mode.w, h: mode.h, refresh: mode.refresh, rotation, scale })}
            options={[[0, tr('prefs.display.rotStandard')], [1, '90°'], [2, '180°'], [3, '270°']]} />
        </SettingRow>
      </GroupBox>
      {msg ? <Note tone="warn">{msg}</Note> : null}
      {several ? (
        <GroupBox title={tr('prefs.display.multiple')} footer={tr('prefs.display.multipleHint')}>
          <SettingRow label={tr('prefs.display.useAs')} disabled={act.length < 2 && !d.mirror}>
            <Segmented small value={d.mirror ? 'mirror' : 'extend'} onChange={(v) => setDisplayMirror(v === 'mirror')}
              options={[['extend', tr('prefs.display.extend')], ['mirror', tr('prefs.display.mirror')]]} />
          </SettingRow>
          <SettingRow label={tr('prefs.display.menuBarAll')} disabled={d.mirror}>
            <Toggle on={!!s.menuBarAllDisplays} onChange={(on) => set({ menuBarAllDisplays: on })} />
          </SettingRow>
          <SettingRow label={tr('prefs.display.capture')} hint={tr('prefs.display.captureHint')}>
            <Segmented small value={d.captureAll ? 'all' : 'main'} onChange={(v) => setCaptureAllDisplays(v === 'all')}
              options={[['main', tr('prefs.display.captureMain')], ['all', tr('prefs.display.captureAll')]]} />
          </SettingRow>
          {outs.filter((o) => !o.active).map((o) => (
            <SettingRow key={o.id} label={o.name} hint={tr('prefs.display.noCrtc', { connector: o.connector })} disabled />
          ))}
        </GroupBox>
      ) : null}
      <GroupBox>
        <Brightness list={d.backlight || []} />
      </GroupBox>
      <NightShift />
      {d.softTint ? <Note>{tr('prefs.night.software')}</Note> : null}
    </Stack>
  );
}

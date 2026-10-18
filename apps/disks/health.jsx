// health.jsx — disk health (SMART). Read only on request (disk-smart action), never in the
// background, so the disk is not woken up and nothing stalls.
import { View, Text, Button, useTheme, useLocale, admin, formatBytes, formatNumber } from '@lordos/sdk';
import { useEffect, useState } from 'react';

const cache = new Map();          // disk → last result (while the app is open)

const HEALTH_COLOR = { ok: '#30d158', warning: '#ff9f0a', failing: '#ff453a', unknown: '#8e8e93' };
const KNOWN = [1, 3, 4, 5, 7, 9, 10, 12, 177, 183, 184, 187, 188, 190, 192, 193, 194, 196, 197, 198, 199, 231, 233, 241, 242];

function attrName(tr, id) { return KNOWN.includes(id) ? tr(`disks.attr.a${id}`) : tr('disks.attr.unknown', { id }); }

function readSmart(name) {
  const res = { summary: null, attrs: [] };
  return admin.run('disk-smart', name, (line) => {
    try {
      if (line.startsWith('smart ')) res.summary = JSON.parse(line.slice(6));
      else if (line.startsWith('attr ')) res.attrs.push(JSON.parse(line.slice(5)));
    } catch (e) { /* malformed line */ }
  }, { quiet: true }).then((ms) => ({ ...res, ms }));
}

export function HealthCard({ disk }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [st, setSt] = useState(() => cache.get(disk.name) || null);
  const [showAll, setShowAll] = useState(false);
  useEffect(() => { setSt(cache.get(disk.name) || null); }, [disk.name]);
  const supported = /^(sd|hd|nvme)/.test(disk.name) && disk.transport !== 'loop';
  const check = () => {
    setSt({ loading: true });
    readSmart(disk.name).then((r) => { cache.set(disk.name, r); setSt(r); }, (e) => { const r = { error: e.message }; cache.set(disk.name, r); setSt(r); });
  };
  const s = st && st.summary;
  const line = (label, value) => (value == null || value === -1 ? null : (
    <View key={label} style={{ flexDirection: 'row', paddingVertical: 4, gap: 12 }}>
      <Text style={{ width: 170, fontSize: 12, color: t.textDim, textAlign: 'start' }}>{label}</Text>
      <Text style={{ flex: 1, fontSize: 12, color: t.text, textAlign: 'start' }}>{value}</Text>
    </View>));
  return (
    <View style={{ borderRadius: 10, backgroundColor: t.hover, padding: 14, gap: 10 }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
        <Text style={{ flex: 1, fontSize: 13, fontWeight: 600, color: t.text, textAlign: 'start' }}>{tr('disks.health')}</Text>
        {supported ? <Button label={st && !st.loading ? tr('disks.checkAgain') : tr('disks.checkHealth')} onClick={() => !(st && st.loading) && check()} /> : null}
      </View>
      {!supported ? <Text style={{ fontSize: 12, color: t.textDim, textAlign: 'start' }}>{tr('disks.noSmart')}</Text> : null}
      {st && st.loading ? <Text style={{ fontSize: 12, color: t.textDim, textAlign: 'start' }}>{tr('disks.reading')}</Text> : null}
      {st && st.error ? <Text style={{ fontSize: 12, color: '#ff9f0a', textAlign: 'start' }}>{st.error}</Text> : null}
      {s ? (
        <View style={{ gap: 4 }}>
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
            <View style={{ width: 12, height: 12, borderRadius: 6, backgroundColor: HEALTH_COLOR[s.health] || HEALTH_COLOR.unknown }} />
            <Text style={{ fontSize: 14, fontWeight: 700, color: t.text }}>{tr(`disks.healthState.${s.health}`)}</Text>
          </View>
          <Text style={{ fontSize: 11, color: t.textDim, textAlign: 'start' }}>{tr(`disks.healthHint.${s.health}`)}</Text>
          {line(tr('disks.temperature'), s.temp >= 0 ? tr('disks.celsius', { v: s.temp }) : null)}
          {line(tr('disks.powerOn'), s.hours >= 0 ? tr('disks.hours', { count: s.hours, v: formatNumber(s.hours, 0), d: formatNumber(s.hours / 24, 0) }) : null)}
          {line(tr('disks.powerCycles'), s.cycles >= 0 ? formatNumber(s.cycles, 0) : null)}
          {s.kind === 'ata' ? [
            line(tr('disks.reallocated'), s.reallocated >= 0 ? formatNumber(s.reallocated, 0) : null),
            line(tr('disks.pending'), s.pending >= 0 ? formatNumber(s.pending, 0) : null),
            line(tr('disks.uncorrectable'), s.uncorrectable >= 0 ? formatNumber(s.uncorrectable, 0) : null),
            line(tr('disks.selfStatus'), s.status === 1 ? tr('disks.statusOk') : s.status === 0 ? tr('disks.statusFail') : null),
          ] : [
            line(tr('disks.lifeUsed'), tr('disks.pctValue', { v: s.used })),
            line(tr('disks.spare'), tr('disks.spareValue', { v: s.spare, min: s.spareThreshold })),
            line(tr('disks.dataRead'), formatBytes(s.read)),
            line(tr('disks.dataWritten'), formatBytes(s.written)),
            line(tr('disks.mediaErrors'), formatNumber(s.mediaErrors, 0)),
            line(tr('disks.unsafeShutdowns'), formatNumber(s.unsafeShutdowns, 0)),
          ]}
          {st.attrs.length ? (
            <View style={{ marginTop: 6, gap: 2 }}>
              <View onClick={() => setShowAll(!showAll)}>
                <Text style={{ fontSize: 12, color: t.accent, textAlign: 'start' }}>{showAll ? tr('disks.hideAttrs') : tr('disks.showAttrs', { n: st.attrs.length })}</Text>
              </View>
              {showAll ? (
                <View style={{ marginTop: 4 }}>
                  <View style={{ flexDirection: 'row', gap: 8, paddingVertical: 3 }}>
                    {[['disks.attrName', 2], ['disks.attrValue', 1], ['disks.attrWorst', 1], ['disks.attrThresh', 1], ['disks.attrRaw', 1.4]].map(([k, f]) => (
                      <Text key={k} style={{ flex: f, fontSize: 11, fontWeight: 600, color: t.textDim, textAlign: 'start' }}>{tr(k)}</Text>))}
                  </View>
                  {st.attrs.map((a) => (
                    <View key={a.id} style={{ flexDirection: 'row', gap: 8, paddingVertical: 2 }}>
                      <Text style={{ flex: 2, fontSize: 11, color: a.failing ? '#ff453a' : t.text, textAlign: 'start' }}>{`${a.id} · ${attrName(tr, a.id)}${a.prefail ? ' *' : ''}`}</Text>
                      <Text style={{ flex: 1, fontSize: 11, color: t.text, textAlign: 'start' }}>{String(a.value)}</Text>
                      <Text style={{ flex: 1, fontSize: 11, color: t.text, textAlign: 'start' }}>{String(a.worst)}</Text>
                      <Text style={{ flex: 1, fontSize: 11, color: t.text, textAlign: 'start' }}>{String(a.thresh)}</Text>
                      <Text style={{ flex: 1.4, fontSize: 11, color: t.text, textAlign: 'start' }}>{String(a.raw)}</Text>
                    </View>
                  ))}
                  <Text style={{ fontSize: 10, color: t.textDim, marginTop: 4, textAlign: 'start' }}>{tr('disks.prefailNote')}</Text>
                </View>
              ) : null}
            </View>
          ) : null}
          <Text style={{ fontSize: 10, color: t.textDim, marginTop: 4, textAlign: 'start' }}>{tr('disks.readIn', { ms: st.ms })}</Text>
        </View>
      ) : null}
    </View>
  );
}

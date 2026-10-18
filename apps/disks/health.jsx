// health.jsx — disk health (SMART). Read only on request (disk-smart action), never in the
// background, so the disk is not woken up and nothing stalls.
import { View, Text, Button, useTheme, useLocale, admin, formatBytes, formatNumber, GroupBox, FormRow, spacing, typeScale } from '@lordos/sdk';
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
    <FormRow key={label} label={label} labelWidth={170} minHeight={30}>
      <Text style={{ fontSize: typeScale.body, color: t.text, textAlign: 'start' }}>{value}</Text>
    </FormRow>));
  const dim = (text, color) => <Text style={{ fontSize: typeScale.callout, color: color || t.textDim, lineHeight: 1.4, textAlign: 'start' }}>{text}</Text>;
  // first row: the state (or what to do) and the check button
  const head = (
    <View key="head" style={{ minHeight: 44, flexDirection: 'row', alignItems: 'center', gap: spacing.m, paddingVertical: spacing.s }}>
      {s ? <View style={{ width: 10, height: 10, borderRadius: 5, backgroundColor: HEALTH_COLOR[s.health] || HEALTH_COLOR.unknown }} /> : null}
      <View style={{ flex: 1, minWidth: 0, gap: 2 }}>
        <Text style={{ fontSize: typeScale.body, fontWeight: 600, color: t.text, textAlign: 'start' }}>{s ? tr(`disks.healthState.${s.health}`) : tr('disks.health')}</Text>
        {s ? dim(tr(`disks.healthHint.${s.health}`))
          : !supported ? dim(tr('disks.noSmart'))
          : st && st.loading ? dim(tr('disks.reading'))
          : st && st.error ? dim(st.error, '#ff9f0a') : null}
      </View>
      {supported ? <Button small label={st && !st.loading ? tr('disks.checkAgain') : tr('disks.checkHealth')} disabled={!!(st && st.loading)} onClick={check} /> : null}
    </View>
  );
  const rows = !s ? [] : [
    line(tr('disks.temperature'), s.temp >= 0 ? tr('disks.celsius', { v: s.temp }) : null),
    line(tr('disks.powerOn'), s.hours >= 0 ? tr('disks.hours', { count: s.hours, v: formatNumber(s.hours, 0), d: formatNumber(s.hours / 24, 0) }) : null),
    line(tr('disks.powerCycles'), s.cycles >= 0 ? formatNumber(s.cycles, 0) : null),
    ...(s.kind === 'ata' ? [
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
    ]),
  ].filter(Boolean);
  const cols = [['disks.attrName', 2], ['disks.attrValue', 1], ['disks.attrWorst', 1], ['disks.attrThresh', 1], ['disks.attrRaw', 1.4]];
  const attrs = s && st.attrs.length ? (
    <View key="attrs" style={{ paddingVertical: spacing.s, gap: 2 }}>
      <View onClick={() => setShowAll(!showAll)} style={{ alignSelf: 'flex-start' }}>
        <Text style={{ fontSize: typeScale.callout, color: t.accent, textAlign: 'start' }}>{showAll ? tr('disks.hideAttrs') : tr('disks.showAttrs', { n: st.attrs.length })}</Text>
      </View>
      {showAll ? (
        <View style={{ marginTop: spacing.xs }}>
          <View style={{ flexDirection: 'row', gap: spacing.s, paddingVertical: 4 }}>
            {cols.map(([k, f]) => <Text key={k} style={{ flex: f, fontSize: typeScale.caption, fontWeight: 600, color: t.textDim, textAlign: 'start' }}>{tr(k)}</Text>)}
          </View>
          {st.attrs.map((a) => (
            <View key={a.id} style={{ flexDirection: 'row', gap: spacing.s, paddingVertical: 3 }}>
              <Text style={{ flex: 2, fontSize: typeScale.caption, color: a.failing ? '#ff453a' : t.text, whiteSpace: 'nowrap', overflow: 'hidden', textAlign: 'start' }}>{`${a.id} · ${attrName(tr, a.id)}${a.prefail ? ' *' : ''}`}</Text>
              {[a.value, a.worst, a.thresh].map((v, i) => <Text key={i} style={{ flex: 1, fontSize: typeScale.caption, color: t.text, textAlign: 'start' }}>{String(v)}</Text>)}
              <Text style={{ flex: 1.4, fontSize: typeScale.caption, color: t.text, textAlign: 'start' }}>{String(a.raw)}</Text>
            </View>
          ))}
          <Text style={{ fontSize: typeScale.caption, color: t.textDim, marginTop: spacing.xs, textAlign: 'start' }}>{tr('disks.prefailNote')}</Text>
        </View>
      ) : null}
    </View>
  ) : null;
  return (
    <GroupBox footer={s ? tr('disks.readIn', { ms: st.ms }) : null}>
      {[head, ...rows, attrs].filter(Boolean)}
    </GroupBox>
  );
}

// System Information (0.8.5, docs/BENCHMARK.md): every piece of hardware detected and what LordOS
// makes of it. Overview, Hardware (a source list and a detail pane) and Compatibility. The report
// comes from lord.sysinfo.report() (server/src/sysreport, the same collector as lordos-sysreport and
// lordos-fetch), read once on open and again with Refresh; model.js works out the GPU layers and the
// Compatibility panel. Nothing here holds serial numbers, addresses or names: the report has none.
import {
  View, Text, ScrollView, useTheme, useLocale, LordOSLogo, systemReport, systemInfo, lordosVersion, formatBytes, formatNumber,
  Toolbar, ToolbarSpacer, ToolbarButton, Segmented, SidebarItem, GroupBox, FormRow, EmptyState, Hairline, spacing, typeScale,
} from '@lordos/sdk';
import { useMemo, useState } from 'react';
import { cardLayers, sessionLayers, compatibility, hardwareSections, throttleFlags, machineName, compositionReason } from './model.js';

// The app's icon: a chip on the brand gradient.
export function Icon({ size: s }) {
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.22, alignItems: 'center', justifyContent: 'center',
      background: 'linear-gradient(135deg, #B46CFF, #4F46E5)' }}>
      <View style={{ width: s * 0.5, height: s * 0.5, borderRadius: s * 0.08, border: `${Math.max(1, s * 0.04)}px solid rgba(255,255,255,0.95)`,
        alignItems: 'center', justifyContent: 'center' }}>
        <View style={{ width: s * 0.22, height: s * 0.22, borderRadius: s * 0.03, backgroundColor: '#FFD60A' }} />
      </View>
      {[0.32, 0.5, 0.68].map((p) => [
        <View key={`t${p}`} style={{ position: 'absolute', left: s * p - s * 0.02, top: s * 0.16, width: s * 0.04, height: s * 0.08, backgroundColor: 'rgba(255,255,255,0.8)' }} />,
        <View key={`b${p}`} style={{ position: 'absolute', left: s * p - s * 0.02, bottom: s * 0.16, width: s * 0.04, height: s * 0.08, backgroundColor: 'rgba(255,255,255,0.8)' }} />,
      ])}
    </View>
  );
}

const camel = (s) => String(s).replace(/-([a-z0-9])/g, (_, c) => c.toUpperCase());
const STATE_COLOR = { yes: '#30d158', works: '#30d158', limits: '#ff9f0a', no: '#ff453a', absent: '#8e8e93', unchecked: '#8e8e93' };
const ghz = (mhz) => formatNumber(mhz / 1000, 2);
// "Intel(R) Core(TM) i7 CPU @ 2.80GHz" → "Intel Core i7 @ 2.80GHz", as About shows it
const cpuName = (s) => (s || '').replace(/\(R\)|\(TM\)|\bCPU\b|\bProcessor\b/g, '').replace(/\s+/g, ' ').trim();

function Badge({ state }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6, paddingHorizontal: 8, height: 22, borderRadius: 11,
      backgroundColor: t.mode === 'dark' ? 'rgba(255,255,255,0.07)' : 'rgba(0,0,0,0.05)' }}>
      <View style={{ width: 8, height: 8, borderRadius: 4, backgroundColor: STATE_COLOR[state] || '#8e8e93' }} />
      <Text style={{ fontSize: typeScale.caption, fontWeight: 500, color: t.text, whiteSpace: 'nowrap' }}>{tr(`sysinfo.state.${state}`)}</Text>
    </View>
  );
}

function Row({ label, value }) {
  const t = useTheme();
  return (
    <FormRow label={label} labelWidth={170} minHeight={30}>
      <Text style={{ fontSize: typeScale.body, color: value ? t.text : t.textDim, textAlign: 'start' }}>{value || '—'}</Text>
    </FormRow>
  );
}

// A layer or compatibility row: name, state and what it means.
function StateRow({ label, state, detail }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: spacing.m, minHeight: 40, paddingVertical: 6 }}>
      <Text style={{ width: 170, fontSize: typeScale.body, color: t.text, textAlign: 'start' }}>{label}</Text>
      <Badge state={state} />
      <Text style={{ flex: 1, minWidth: 0, fontSize: typeScale.callout, color: t.textDim, textAlign: 'start', lineHeight: 1.35 }}>{detail || ''}</Text>
    </View>
  );
}

// ---- Overview ----

function Card({ title, value, sub }) {
  const t = useTheme();
  return (
    <View style={{ flex: 1, minWidth: 0, minHeight: 104, gap: 4, padding: spacing.m, borderRadius: t.groupRadius, border: `1px solid ${t.separator}`,
      backgroundColor: t.mode === 'dark' ? 'rgba(255,255,255,0.04)' : 'rgba(0,0,0,0.025)' }}>
      <Text style={{ fontSize: typeScale.caption, fontWeight: 600, color: t.textDim, textAlign: 'start' }}>{title}</Text>
      <Text style={{ fontSize: typeScale.headline, fontWeight: 600, color: t.text, textAlign: 'start' }}>{value || '—'}</Text>
      {sub ? <Text style={{ fontSize: typeScale.callout, color: t.textDim, textAlign: 'start', lineHeight: 1.35 }}>{sub}</Text> : null}
    </View>
  );
}

function Overview({ r, info }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const ver = lordosVersion(info);
  const c = r.cpu || {};
  const s = r.session || {};
  const reason = compositionReason(s);
  const gpu = (r.gpus || []).find((g) => g.connected) || (r.gpus || [])[0];
  const disks = r.storage || [];
  const sw = r.software || {};
  const m = r.machine || {};
  const kind = m.virtualization ? tr('sysinfo.kind.vm', { name: m.virtualization }) : m.chassis ? tr(`sysinfo.kind.${camel(m.chassis)}`) : '';
  return (
    <ScrollView style={{ flex: 1 }}>
      <View style={{ padding: spacing.xl, gap: spacing.xl }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: spacing.l }}>
          <LordOSLogo size={64} />
          <View style={{ flex: 1, minWidth: 0, gap: 2 }}>
            <Text style={{ fontSize: typeScale.largeTitle, fontWeight: 600, color: t.text, textAlign: 'start' }}>{machineName(r) || tr('sysinfo.thisComputer')}</Text>
            <Text style={{ fontSize: typeScale.callout, color: t.textDim, textAlign: 'start' }}>
              {[kind, ver.channel ? tr('sysinfo.lordosChannel', { v: ver.version, channel: ver.channel }) : tr('sysinfo.lordos', { v: ver.version }), sw.os].filter(Boolean).join(' · ')}
            </Text>
            {s.fixture ? <Text style={{ fontSize: typeScale.caption, color: '#ff9f0a', textAlign: 'start' }}>{tr('sysinfo.fixture')}</Text> : null}
          </View>
        </View>
        <View style={{ flexDirection: 'row', gap: spacing.m }}>
          <Card title={tr('sysinfo.section.cpu')} value={cpuName(c.model) || c.arch}
            sub={[tr('sysinfo.coresThreads', { cores: c.cores, threads: c.threads }), c.maxMHz ? tr('sysinfo.upTo', { ghz: ghz(c.maxMHz) }) : ''].filter(Boolean).join(' · ')} />
          <Card title={tr('sysinfo.section.memory')} value={formatBytes((r.memory?.totalKB || 0) * 1024)}
            sub={(r.memory?.zram || []).length ? tr('sysinfo.withZram', { size: formatBytes(r.memory.zram[0].sizeKB * 1024) }) : r.memory?.swapKB ? tr('sysinfo.withSwap', { size: formatBytes(r.memory.swapKB * 1024) }) : ''} />
          <Card title={tr('sysinfo.section.graphics')} value={gpu ? (gpu.device || gpu.vendor || gpu.driver) : tr('sysinfo.none')}
            sub={s.composition === 'gpu' ? tr('sysinfo.composedGpu') : tr('sysinfo.composedCpu', { reason: tr(`sysinfo.reason.${camel(reason)}`) })} />
        </View>
        <View style={{ flexDirection: 'row', gap: spacing.m }}>
          <Card title={tr('sysinfo.section.displays')} value={(r.displays || []).length ? (r.displays || []).map((d) => d.name || d.connector).join(', ') : tr('sysinfo.none')}
            sub={(r.displays || []).map((d) => d.nativeWidth ? `${d.nativeWidth} × ${d.nativeHeight}` : d.mode).filter(Boolean).join(', ')} />
          <Card title={tr('sysinfo.section.storage')} value={formatBytes(disks.reduce((a, d) => a + d.bytes, 0))}
            sub={tr('sysinfo.disks', { count: disks.length })} />
          <Card title={tr('sysinfo.isolation')} value={sw.landlockAbi > 0 ? tr('sysinfo.landlock', { abi: sw.landlockAbi }) : tr('sysinfo.landlockUnknown')}
            sub={[sw.bpfLsm ? tr('sysinfo.bpfLsm') : '', sw.cgroup2 ? tr('sysinfo.cgroup2') : '', sw.services?.['lord-netperm'] ? tr('sysinfo.appFirewall') : ''].filter(Boolean).join(' · ')} />
        </View>
      </View>
    </ScrollView>
  );
}

// ---- Hardware ----

function layerDetail(tr, l) {
  if (l.note) return tr(`sysinfo.note.${camel(l.note)}`, l.vars || {});
  if (l.state !== 'yes') return '';
  return { device: () => l.vars.name, driver: () => l.vars.driver, kms: () => tr('sysinfo.detail.kms', l.vars), egl: () => l.vars.renderer }[l.id]?.() || '';
}

function Graphics({ r }) {
  const { t: tr } = useLocale();
  return (
    <View style={{ gap: spacing.xl }}>
      {(r.gpus || []).map((g) => (
        <GroupBox key={g.card} title={tr('sysinfo.cardTitle', { card: g.card, bus: g.bus })}>
          {cardLayers(g).map((l) => <StateRow key={l.id} label={tr(`sysinfo.layer.${l.id}`)} state={l.state} detail={layerDetail(tr, l)} />)}
        </GroupBox>
      ))}
      <GroupBox title={tr('sysinfo.thisSession')} footer={tr('sysinfo.uncheckedFooter')}>
        {sessionLayers(r.session).map((l) => <StateRow key={l.id} label={tr(`sysinfo.layer.${l.id}`)} state={l.state}
          detail={l.id === 'composition' && l.note ? tr(`sysinfo.reason.${camel(l.note)}`) : layerDetail(tr, l)} />)}
      </GroupBox>
    </View>
  );
}

function List({ items, render }) {
  return <View style={{ gap: spacing.l }}>{items.map(render)}</View>;
}

function Section({ id, r }) {
  const { t: tr } = useLocale();
  const yesNo = (v) => (v ? tr('sysinfo.yes') : tr('sysinfo.no'));
  const m = r.machine || {}, c = r.cpu || {}, mem = r.memory || {}, sw = r.software || {};
  switch (id) {
    case 'computer': return (
      <GroupBox>
        <Row label={tr('sysinfo.f.vendor')} value={m.vendor} />
        <Row label={tr('sysinfo.f.product')} value={[m.product, m.version].filter(Boolean).join(' ')} />
        <Row label={tr('sysinfo.f.model')} value={m.model} />
        <Row label={tr('sysinfo.f.board')} value={[m.boardVendor, m.boardName].filter(Boolean).join(' ')} />
        <Row label={tr('sysinfo.f.kind')} value={m.virtualization ? tr('sysinfo.kind.vm', { name: m.virtualization }) : m.chassis ? tr(`sysinfo.kind.${camel(m.chassis)}`) : ''} />
        <Row label={tr('sysinfo.f.firmware')} value={[m.firmware ? tr(`sysinfo.firmware.${m.firmware}`) : '', m.biosVendor, m.biosVersion, m.biosDate].filter(Boolean).join(' · ')} />
        <Row label={tr('sysinfo.f.secureBoot')} value={m.secureBoot == null ? '' : m.secureBoot ? tr('sysinfo.on') : tr('sysinfo.off')} />
      </GroupBox>);
    case 'cpu': return (
      <GroupBox>
        <Row label={tr('sysinfo.f.model')} value={cpuName(c.model)} />
        <Row label={tr('sysinfo.f.vendor')} value={c.vendor} />
        <Row label={tr('sysinfo.f.arch')} value={c.arch} />
        <Row label={tr('sysinfo.f.layout')} value={tr('sysinfo.layout', { sockets: c.sockets, cores: c.cores, threads: c.threads })} />
        <Row label={tr('sysinfo.f.frequency')} value={c.maxMHz ? tr('sysinfo.freqRange', { min: ghz(c.minMHz), max: ghz(c.maxMHz) }) : ''} />
        <Row label={tr('sysinfo.f.governor')} value={c.governor} />
        <Row label={tr('sysinfo.f.cache')} value={[c.l1dKB && `L1d ${c.l1dKB} KB`, c.l1iKB && `L1i ${c.l1iKB} KB`, c.l2KB && `L2 ${c.l2KB} KB`, c.l3KB && `L3 ${c.l3KB} KB`].filter(Boolean).join(' · ')} />
        <Row label={tr('sysinfo.f.instructions')} value={c.flags} />
        <Row label={tr('sysinfo.f.microcode')} value={c.microcode} />
      </GroupBox>);
    case 'memory': return (
      <GroupBox>
        <Row label={tr('sysinfo.f.total')} value={formatBytes((mem.totalKB || 0) * 1024)} />
        <Row label={tr('sysinfo.f.swap')} value={mem.swapKB ? formatBytes(mem.swapKB * 1024) : tr('sysinfo.none')} />
        {(mem.zram || []).map((z, i) => <Row key={i} label={tr('sysinfo.f.zram')} value={`${formatBytes(z.sizeKB * 1024)}${z.algorithm ? ` · ${z.algorithm}` : ''}`} />)}
      </GroupBox>);
    case 'graphics': return <Graphics r={r} />;
    case 'displays': return <List items={r.displays || []} render={(d, i) => (
      <GroupBox key={i} title={d.connector}>
        <Row label={tr('sysinfo.f.monitor')} value={[d.name, d.manufacturer].filter(Boolean).join(' · ')} />
        <Row label={tr('sysinfo.f.native')} value={d.nativeWidth ? `${d.nativeWidth} × ${d.nativeHeight}` : d.mode} />
        <Row label={tr('sysinfo.f.size')} value={d.diagonalTenths ? tr('sysinfo.diagonal', { inch: formatNumber(d.diagonalTenths / 10, 1), w: d.widthCm, h: d.heightCm }) : ''} />
        <Row label={tr('sysinfo.f.year')} value={d.year ? String(d.year) : ''} />
        <Row label={tr('sysinfo.f.card')} value={d.card} />
      </GroupBox>)} />;
    case 'storage': return <List items={r.storage || []} render={(d) => (
      <GroupBox key={d.name} title={d.name}>
        <Row label={tr('sysinfo.f.model')} value={[d.vendor, d.model].filter(Boolean).join(' ')} />
        <Row label={tr('sysinfo.f.size')} value={formatBytes(d.bytes)} />
        <Row label={tr('sysinfo.f.connection')} value={[d.transport ? tr(`sysinfo.bus.${d.transport}`) : '', d.link].filter(Boolean).join(' · ')} />
        <Row label={tr('sysinfo.f.kind')} value={d.removable ? tr('sysinfo.removable') : d.rotational ? tr('sysinfo.hdd') : tr('sysinfo.ssd')} />
      </GroupBox>)} />;
    case 'network': return (
      <GroupBox>
        {(r.network || []).map((n, i) => <Row key={i} label={tr(`sysinfo.net.${n.kind}`)}
          value={[n.driver, n.up ? (n.speedMbps ? tr('sysinfo.upAt', { mbps: n.speedMbps }) : tr('sysinfo.up')) : tr('sysinfo.down')].filter(Boolean).join(' · ')} />)}
      </GroupBox>);
    case 'audio': return <GroupBox>{(r.audio || []).map((a, i) => <Row key={i} label={tr('sysinfo.f.card')} value={a} />)}</GroupBox>;
    case 'bluetooth': return <GroupBox>{(r.bluetooth || []).map((b, i) => <Row key={i} label={tr('sysinfo.f.controller')} value={b.driver} />)}</GroupBox>;
    case 'power': return <List items={r.power || []} render={(p, i) => (
      <GroupBox key={i} title={tr(`sysinfo.power.${p.kind}`)}>
        {p.kind === 'battery' ? [
          <Row key="m" label={tr('sysinfo.f.model')} value={[p.vendor, p.model].filter(Boolean).join(' ')} />,
          <Row key="t" label={tr('sysinfo.f.technology')} value={p.technology} />,
          <Row key="c" label={tr('sysinfo.f.charge')} value={p.capacity != null ? `${p.capacity} %` : ''} />,
          <Row key="h" label={tr('sysinfo.f.health')} value={p.health != null ? tr('sysinfo.health', { health: p.health }) : ''} />,
          <Row key="s" label={tr('sysinfo.f.status')} value={p.status} />,
        ] : <Row label={tr('sysinfo.f.plugged')} value={p.online == null ? '' : yesNo(p.online)} />}
      </GroupBox>)} />;
    case 'thermal': return (
      <GroupBox>
        {(r.thermal || []).map((x, i) => <Row key={i} label={x.sensor}
          value={[x.milliC != null ? `${formatNumber(x.milliC / 1000, 1)} °C` : '', x.fans ? tr('sysinfo.fans', { count: x.fans }) : ''].filter(Boolean).join(' · ')} />)}
      </GroupBox>);
    case 'pci': return (
      <GroupBox>
        {(r.pci || []).map((p, i) => <Row key={i} label={p.className || p.class} value={[[p.vendor, p.device].filter(Boolean).join(' ') || `${p.vendorId}:${p.deviceId}`, p.driver].filter(Boolean).join(' · ')} />)}
      </GroupBox>);
    case 'usb': return (
      <GroupBox>
        {(r.usb || []).map((u, i) => <Row key={i} label={u.speedMbps ? tr('sysinfo.usbSpeed', { mbps: u.speedMbps }) : 'USB'}
          value={[[u.vendor, u.product].filter(Boolean).join(' ') || `${u.vendorId}:${u.productId}`, u.driver].filter(Boolean).join(' · ')} />)}
      </GroupBox>);
    case 'raspberryPi': {
      const pi = r.raspberryPi;
      const f = throttleFlags(pi.throttled);
      return (
        <GroupBox>
          <Row label={tr('sysinfo.f.model')} value={pi.model} />
          <Row label={tr('sysinfo.f.revision')} value={pi.revision} />
          <Row label={tr('sysinfo.f.power')} value={!f ? '' : f.underVoltageNow ? tr('sysinfo.pi.underVoltageNow') : f.underVoltage ? tr('sysinfo.pi.underVoltageSeen') : tr('sysinfo.pi.powerOk')} />
          <Row label={tr('sysinfo.f.throttling')} value={!f ? '' : f.throttledNow || f.cappedNow ? tr('sysinfo.pi.throttledNow') : f.throttled || f.capped ? tr('sysinfo.pi.throttledSeen') : tr('sysinfo.pi.throttleOk')} />
        </GroupBox>);
    }
    case 'software': return (
      <View style={{ gap: spacing.xl }}>
        <GroupBox>
          <Row label={tr('sysinfo.f.system')} value={sw.os} />
          <Row label={tr('sysinfo.f.kernel')} value={sw.kernel} />
          <Row label={tr('sysinfo.f.lsm')} value={sw.lsm} />
          <Row label={tr('sysinfo.f.landlock')} value={sw.landlockAbi == null ? '' : sw.landlockAbi ? String(sw.landlockAbi) : tr('sysinfo.unavailable')} />
          <Row label={tr('sysinfo.f.cgroup2')} value={yesNo(sw.cgroup2)} />
        </GroupBox>
        <GroupBox title={tr('sysinfo.services')}>
          {Object.entries(sw.services || {}).map(([k, v]) => <Row key={k} label={k} value={v ? tr('sysinfo.running') : tr('sysinfo.notRunning')} />)}
        </GroupBox>
        <GroupBox title={tr('sysinfo.kernelOptions')}>
          {Object.entries(sw.kernelConfig || {}).map(([k, v]) => <Row key={k} label={k.replace(/^CONFIG_/, '')} value={['y', 'm', 'n'].includes(v) ? tr(`sysinfo.kconf.${v}`) : tr('sysinfo.unknown')} />)}
        </GroupBox>
      </View>);
    default: return null;
  }
}

function Hardware({ r }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const sections = useMemo(() => hardwareSections(r), [r]);
  const [sel, setSel] = useState('computer');
  const cur = sections.includes(sel) ? sel : sections[0];
  return (
    <View style={{ flex: 1, flexDirection: 'row' }}>
      <View style={{ width: 200, backgroundColor: t.sidebarBg }}>
        <ScrollView style={{ flex: 1 }} contentStyle={{ padding: spacing.s, gap: 1 }}>
          {sections.map((id) => <SidebarItem key={id} label={tr(`sysinfo.section.${id}`)} selected={id === cur} onClick={() => setSel(id)} />)}
        </ScrollView>
        <Hairline edge="end" />
      </View>
      <ScrollView style={{ flex: 1 }}>
        <View style={{ padding: spacing.xl, gap: spacing.l }}>
          <Text style={{ fontSize: typeScale.title, fontWeight: 600, color: t.text, textAlign: 'start' }}>{tr(`sysinfo.section.${cur}`)}</Text>
          <Section id={cur} r={r} />
        </View>
      </ScrollView>
    </View>
  );
}

// ---- Compatibility ----

function compatNote(tr, c) {
  if (!c.note) return c.vars && c.vars.count != null && c.state === 'works' ? tr(`sysinfo.compat.count.${c.id}`, c.vars) : '';
  if (c.note.startsWith('cpu.')) return tr('sysinfo.compat.cpuBecause', { reason: tr(`sysinfo.reason.${camel(c.note.slice(4))}`) });
  return tr(`sysinfo.compat.${c.note.replace('.', '_')}`, c.vars);
}

function Compatibility({ r }) {
  const { t: tr } = useLocale();
  const rows = useMemo(() => compatibility(r), [r]);
  return (
    <ScrollView style={{ flex: 1 }}>
      <View style={{ padding: spacing.xl, gap: spacing.l }}>
        <GroupBox footer={tr('sysinfo.compatFooter')}>
          {rows.map((c) => <StateRow key={c.id} label={tr(`sysinfo.component.${c.id}`)} state={c.state} detail={compatNote(tr, c)} />)}
        </GroupBox>
      </View>
    </ScrollView>
  );
}

export default function SystemInformation() {
  const { t: tr } = useLocale();
  const [tab, setTab] = useState('overview');
  const [n, setN] = useState(0);
  const r = useMemo(() => systemReport(), [n]);
  const info = useMemo(() => systemInfo(), [n]);
  if (!r) return <EmptyState mascot="confused" title={tr('sysinfo.unavailable')} />;
  return (
    <View style={{ flex: 1 }}>
      <Toolbar>
        <ToolbarSpacer />
        <Segmented appearance="neutral" value={tab} onChange={setTab}
          options={[['overview', tr('sysinfo.tab.overview')], ['hardware', tr('sysinfo.tab.hardware')], ['compat', tr('sysinfo.tab.compat')]]} />
        <ToolbarSpacer />
        <ToolbarButton icon="↻" title={tr('sysinfo.refresh')} onClick={() => setN(n + 1)} />
      </Toolbar>
      {tab === 'overview' ? <Overview r={r} info={info} /> : tab === 'hardware' ? <Hardware r={r} /> : <Compatibility r={r} />}
    </View>
  );
}

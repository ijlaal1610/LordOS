// Ajustes → General (datos del equipo y del software) y Actualizaciones (Debian y LordOS).
import {
  View, Text, Button, useTheme, useLocale, useSystemInfo, useServerStats, system, screen, formatBytes, formatDuration, formatNumber,
  admin, wm, LordOSLogo,
} from '@lordos/sdk';
import { useState, version as reactVersion } from 'react';

// Tarjeta con filas "clave … valor", como en macOS.
export function Card({ title, rows }) {
  const t = useTheme();
  return (
    <View style={{ marginTop: 18 }}>
      {title && <Text style={{ fontSize: 13, fontWeight: 700, color: t.text, marginBottom: 6 }}>{title}</Text>}
      <View style={{ borderRadius: 10, border: `1px solid ${t.separator}`, backgroundColor: t.hover }}>
        {rows.filter(Boolean).map(([k, v], i) => (
          <View key={k} style={{ flexDirection: 'row', alignItems: 'center', paddingHorizontal: 12, height: 38, gap: 12,
            borderTop: i ? `1px solid ${t.separator}` : '0 solid transparent' }}>
            <Text style={{ fontSize: 13, color: t.text }}>{k}</Text>
            <Text style={{ flex: 1, fontSize: 13, color: t.textDim, textAlign: 'end', whiteSpace: 'nowrap', overflow: 'hidden' }}>{v}</Text>
          </View>
        ))}
      </View>
    </View>
  );
}

const Logo = ({ size }) => <LordOSLogo size={size} />;

export function General() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const info = useSystemInfo(5000);
  const st = useServerStats(3000);
  const cpu = info.cpu.replace(/\(R\)|\(TM\)|CPU|Processor/g, '').replace(/\s+/g, ' ').trim();
  return (
    <View style={{ paddingBottom: 24 }}>
      <View style={{ alignItems: 'center', marginTop: 8 }}>
        <Logo size={84} />
        <Text style={{ fontSize: 24, fontWeight: 700, color: t.text, marginTop: 12 }}>LordOS</Text>
        <Text style={{ fontSize: 12, color: t.textDim, marginTop: 4 }}>{tr('about.version', { v: system.version, m: 'M7' })}</Text>
        <Text style={{ fontSize: 11, color: t.textDim, marginTop: 2 }}>{`${info.hostname} · ${info.os}`}</Text>
      </View>
      <Card title={tr('settings.general.machine')} rows={[
        [tr('settings.general.name'), info.hostname],
        [tr('about.cpu'), tr('about.cpuValue', { cpu: cpu.slice(0, 40), count: info.cores })],
        [tr('about.memory'), tr('shell.cc.memDetail', { used: formatBytes((info.memTotalKB - info.memAvailableKB) * 1024), total: formatBytes(info.memTotalKB * 1024) })],
        info.diskTotal ? [tr('settings.general.storage'), tr('settings.general.storageValue', { free: formatBytes(info.diskFree), total: formatBytes(info.diskTotal) })] : null,
        [tr('about.display'), `${screen.width} × ${screen.height}`],
        [tr('about.uptime'), formatDuration(info.uptime)],
      ]} />
      <Card title={tr('settings.general.software')} rows={[
        ['LordOS', system.version],
        [tr('about.os'), info.os],
        [tr('about.kernel'), `${info.kernel} (${info.arch})`],
        [tr('about.server'), 'lord-server (C · DRM/KMS · evdev · Wayland)'],
        [tr('about.engine'), `QuickJS + React ${reactVersion}`],
        [tr('about.ram'), `${formatNumber(st.rssKB / 1024, 1)} MB (JS ${formatNumber(st.jsKB / 1024, 1)} MB)`],
      ]} />
    </View>
  );
}

export function Updates() {
  const t = useTheme();
  const { t: tr, formatDate } = useLocale();
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState(null);
  const pkg = typeof lord.pkg !== 'undefined' && lord.pkg.available();
  const ups = pkg ? lord.pkg.upgrades() : [];
  const at = pkg ? lord.pkg.lastUpdate() : 0;
  const check = () => {
    setBusy(true);
    admin.run('pkg-update').then(() => setMsg({ ok: true, text: tr('settings.updates.checked') }), (e) => setMsg({ ok: false, text: e.message }))
      .finally(() => setBusy(false));
  };
  return (
    <View style={{ paddingBottom: 24 }}>
      <Card rows={[
        [tr('settings.updates.debian'), ups.length ? tr('settings.updates.available', { count: ups.length }) : tr('settings.updates.none')],
        [tr('settings.updates.last'), at ? formatDate(new Date(at * 1000), 'long') : '—'],
      ]} />
      {msg && <Text style={{ fontSize: 12, marginTop: 10, color: msg.ok ? '#30d158' : '#ff453a' }}>{msg.text}</Text>}
      <View style={{ flexDirection: 'row', gap: 8, marginTop: 14 }}>
        <Button label={busy ? tr('settings.updates.checking') : tr('settings.updates.check')} onClick={() => !busy && admin.available() && check()} />
        <Button primary label={tr('settings.updates.open')} onClick={() => wm.open('store', { args: { page: 'updates' } })} />
      </View>
      {!admin.available() && <Text style={{ fontSize: 11, color: '#ff9f0a', marginTop: 8 }}>{tr('settings.admin.unavailable')}</Text>}
      <Text style={{ fontSize: 11, color: t.textDim, marginTop: 14, lineHeight: 1.4 }}>{tr('settings.updates.hint')}</Text>
    </View>
  );
}

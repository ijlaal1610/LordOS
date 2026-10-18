// Acerca de LordOS — datos reales del sistema (/proc) y del servidor gráfico.
import { version as reactVersion } from 'react';
import { View, Text, LordOSLogo, screen, system, useTheme, useSystemInfo, useServerStats, formatBytes, formatDuration, formatNumber, t as tr } from '@lordos/sdk';

export function Icon({ size }) {
  return <LordOSLogo size={size} />;
}

function InfoRow({ k, v }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', justifyContent: 'space-between', paddingVertical: 3, gap: 12 }}>
      <Text style={{ fontSize: 12, color: t.textDim }}>{k}</Text>
      <Text style={{ fontSize: 12, fontWeight: 500, color: t.text, whiteSpace: 'nowrap' }}>{v}</Text>
    </View>
  );
}

export default function About() {
  const t = useTheme();
  const info = useSystemInfo(5000);
  const st = useServerStats(2000);
  const cpu = info.cpu.replace(/\(R\)|\(TM\)|CPU|Processor/g, '').replace(/\s+/g, ' ').trim();
  return (
    <View style={{ flex: 1, alignItems: 'center', paddingTop: 22 }}>
      <Icon size={76} />
      <Text style={{ fontSize: 24, fontWeight: 700, color: t.text, marginTop: 12 }}>LordOS</Text>
      <Text style={{ fontSize: 12, color: t.textDim, marginTop: 4 }}>{tr('about.version', { v: system.version, m: 'M7' })}</Text>
      <View style={{ width: 360, marginTop: 16 }}>
        <InfoRow k={tr('about.os')} v={info.os} />
        <InfoRow k={tr('about.kernel')} v={`${info.kernel} (${info.arch})`} />
        <InfoRow k={tr('about.host')} v={tr('about.hostValue', { host: info.hostname, user: info.user })} />
        <InfoRow k={tr('about.cpu')} v={tr('about.cpuValue', { cpu: cpu.slice(0, 34), count: info.cores })} />
        <InfoRow k={tr('about.memory')} v={tr('shell.cc.memDetail', { used: formatBytes((info.memTotalKB - info.memAvailableKB) * 1024), total: formatBytes(info.memTotalKB * 1024) })} />
        <InfoRow k={tr('about.uptime')} v={formatDuration(info.uptime)} />
        <View style={{ height: 1, backgroundColor: t.separator, marginVertical: 8 }} />
        <InfoRow k={tr('about.server')} v="lord-server (C · DRM/KMS · evdev)" />
        <InfoRow k={tr('about.engine')} v={`QuickJS + React ${reactVersion}`} />
        <InfoRow k={tr('about.display')} v={`${screen.width} × ${screen.height}`} />
        <InfoRow k={tr('about.ram')} v={`${formatNumber(st.rssKB / 1024, 1)} MB (JS ${formatNumber(st.jsKB / 1024, 1)} MB)`} />
        <InfoRow k={tr('about.render')} v={tr('about.renderValue', { ms: formatNumber(st.layoutMs + st.paintMs, 1), count: st.nodes })} />
      </View>
      <Text style={{ fontSize: 10, color: t.textDim, marginTop: 10, textAlign: 'center', lineHeight: 1.4 }}>{tr('about.credits')}</Text>
    </View>
  );
}

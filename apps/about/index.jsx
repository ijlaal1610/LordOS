// About LordOS: real system data (/proc) and display server stats.
import { version as reactVersion } from 'react';
import { View, Text, LordOSLogo, screen, lordosVersion, useTheme, useSystemInfo, useServerStats, formatBytes, formatDuration, formatNumber, typeScale, t as tr } from '@lordos/sdk';

export function Icon({ size }) {
  return <LordOSLogo size={size} />;
}

// One line of the spec list, like macOS "About This Mac": the label in a trailing-aligned column
// and the value at its side.
function InfoRow({ k, v }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', minHeight: 20, gap: 10 }}>
      <Text style={{ width: 118, fontSize: typeScale.callout, color: t.textDim, textAlign: 'end', whiteSpace: 'nowrap', overflow: 'hidden' }}>{k}</Text>
      <View style={{ flex: 1, minWidth: 0, overflow: 'hidden' }}>
        <Text style={{ fontSize: typeScale.callout, fontWeight: 500, color: t.text, whiteSpace: 'nowrap' }}>{v}</Text>
      </View>
    </View>
  );
}

export default function About() {
  const t = useTheme();
  const info = useSystemInfo(5000);
  const st = useServerStats(2000);
  const cpu = info.cpu.replace(/\(R\)|\(TM\)|CPU|Processor/g, '').replace(/\s+/g, ' ').trim();
  const ver = lordosVersion(info);
  return (
    <View style={{ flex: 1, alignItems: 'center', paddingHorizontal: 20, paddingTop: 20, paddingBottom: 16 }}>
      <Icon size={64} />
      <Text style={{ fontSize: typeScale.largeTitle, fontWeight: 600, color: t.text, marginTop: 12 }}>LordOS</Text>
      <Text style={{ fontSize: typeScale.callout, color: t.textDim, marginTop: 2 }}>{ver.channel ? tr('about.versionChannel', { v: ver.version, channel: ver.channel }) : tr('about.version', { v: ver.version })}</Text>
      {ver.build ? <Text style={{ fontSize: typeScale.caption, color: t.textDim, marginTop: 1 }}>{tr('about.build', { b: ver.build })}</Text> : null}
      <Text style={{ fontSize: typeScale.callout, color: t.textDim, marginTop: 10, maxWidth: 320, textAlign: 'center', lineHeight: 17 }}>{tr('about.tagline')}</Text>
      <View style={{ alignSelf: 'stretch', marginTop: 16, gap: 2 }}>
        <InfoRow k={tr('about.os')} v={info.os} />
        <InfoRow k={tr('about.kernel')} v={`${info.kernel} (${info.arch})`} />
        <InfoRow k={tr('about.host')} v={tr('about.hostValue', { host: info.hostname, user: info.user })} />
        <InfoRow k={tr('about.cpu')} v={tr('about.cpuValue', { cpu: cpu.slice(0, 34) || tr('about.cpuUnknown'), count: info.cores })} />
        <InfoRow k={tr('about.memory')} v={tr('shell.cc.memDetail', { used: formatBytes((info.memTotalKB - info.memAvailableKB) * 1024), total: formatBytes(info.memTotalKB * 1024) })} />
        <InfoRow k={tr('about.uptime')} v={formatDuration(info.uptime)} />
        <View style={{ height: 1, backgroundColor: t.separator, marginVertical: 8, marginHorizontal: 24 }} />
        <InfoRow k={tr('about.server')} v="lord-server (C · DRM/KMS · evdev)" />
        <InfoRow k={tr('about.engine')} v={`QuickJS + React ${reactVersion}`} />
        <InfoRow k={tr('about.display')} v={`${screen.width} × ${screen.height}`} />
        <InfoRow k={tr('about.ram')} v={`${formatNumber(st.rssKB / 1024, 1)} MB (JS ${formatNumber(st.jsKB / 1024, 1)} MB)`} />
        <InfoRow k={tr('about.render')} v={tr('about.renderValue', { ms: formatNumber(st.layoutMs + st.paintMs, 1), count: st.nodes })} />
      </View>
      <View style={{ flex: 1, minHeight: 12 }} />
      <Text style={{ fontSize: typeScale.caption, color: t.textDim, textAlign: 'center', lineHeight: 1.4, maxWidth: 360 }}>{tr('about.credits')}</Text>
    </View>
  );
}

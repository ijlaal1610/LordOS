// Settings → Sharing: remote desktop (lord-sessiond, docs/REMOTE.md). Off by default;
// turning it on, choosing who can log in and viewing or cutting connections goes through the
// administration helper (remote-* actions).
import {
  View, Text, Button, Toggle, Segmented, useTheme, useLocale, useStore, admin, remote, remoteViewers, useSystemInfo, spacing, typeScale,
} from '@lordos/sdk';
import { useEffect, useState } from 'react';
import { Stack, Section, SettingRow, Note, Message, StatusDot, tint } from './ui.jsx';


// This machine's LAN IPv4 addresses (to build the URL).
function lanAddresses() {
  if (typeof lord.net === 'undefined') return [];
  const out = [];
  for (const i of lord.net.interfaces()) {
    if (i.type === 'loopback' || /^(wg|ovpn|ips|ppp|oc|fvpn)-/.test(i.name)) continue;
    for (const a of i.addresses) if (a.family === 4 && a.scope === 'global') out.push(a.address);
  }
  return out;
}

const ago = (sec, tr) => {
  const m = Math.max(0, Math.round((Date.now() / 1000 - sec) / 60));
  return m < 1 ? tr('settings.sharing.justNow') : tr('settings.sharing.minutes', { count: m });
};

export function SharingSettings() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const info = useSystemInfo(60000);
  const viewers = useStore(remoteViewers);
  const [st, setSt] = useState(() => remote.status());
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState(null);
  useEffect(() => { const id = setInterval(() => setSt(remote.status()), 2000); return () => clearInterval(id); }, []);
  const isAdmin = admin.available() && info.admin;
  const act = (action, arg = '', ok) => {
    setBusy(true);
    setMsg(null);
    return admin.run(action, arg).then(
      () => { setMsg(ok ? { ok: true, text: ok } : null); setSt(remote.status()); },
      (e) => setMsg({ ok: false, text: e.message })).finally(() => setBusy(false));
  };
  const ips = lanAddresses();
  const urls = ips.map((ip) => `https://${ip}:${st.port}`);
  return (
    <Stack>
      <Section title={tr('settings.sharing.remote')}>
        <SettingRow label={tr('settings.sharing.remoteToggle')} hint={busy ? tr('settings.sharing.working') : tr('settings.sharing.remoteHint')}>
          <Toggle small on={st.enabled} onChange={(on) => !busy && isAdmin && act(on ? 'remote-enable' : 'remote-disable')} />
        </SettingRow>
        {st.enabled && (
          <View style={{ paddingVertical: spacing.m, gap: 6 }}>
            <Text style={{ fontSize: typeScale.caption, color: t.textDim, textAlign: 'start' }}>{tr('settings.sharing.openFrom')}</Text>
            {urls.map((u) => <Text key={u} style={{ fontSize: typeScale.headline, fontWeight: 600, color: t.accent, direction: 'ltr', textAlign: 'start' }}>{u}</Text>)}
            {!urls.length && <Text style={{ fontSize: typeScale.callout, color: t.textDim, textAlign: 'start' }}>{tr('settings.sharing.noLan')}</Text>}
            {st.fingerprint ? (
              <View style={{ marginTop: spacing.xs, gap: 2 }}>
                <Text style={{ fontSize: typeScale.caption, color: t.textDim, textAlign: 'start' }}>{tr('settings.sharing.fingerprint')}</Text>
                <Text style={{ fontSize: 10, color: t.text, fontFamily: 'monospace', lineHeight: 1.4, direction: 'ltr', textAlign: 'start' }}>{st.fingerprint}</Text>
              </View>
            ) : null}
          </View>
        )}
        <SettingRow label={tr('settings.sharing.who')} hint={tr(st.allow === 'group' ? 'settings.sharing.whoGroupHint' : 'settings.sharing.whoAllHint')}>
          <Segmented small value={st.allow} onChange={(allow) => isAdmin && act('remote-config', `allow=${allow}`)}
            options={[['all', tr('settings.sharing.allUsers')], ['group', tr('settings.sharing.onlyGroup')]]} />
        </SettingRow>
      </Section>
      {!isAdmin && <Note tone="warn">{tr('settings.admin.unavailable')}</Note>}
      {!st.installed && <Note tone="warn">{tr('settings.sharing.notInstalled')}</Note>}
      <Message msg={msg} />
      <Section title={tr('settings.sharing.connected')} footer={tr('settings.sharing.security')}>
        {!st.clients.length ? <SettingRow label={tr('settings.sharing.nobody')} labelStyle={{ color: t.textDim }} /> : st.clients.map((c) => (
          <SettingRow key={c.pid} icon={<StatusDot color={tint.ok} />} label={`${c.user} · ${c.ip}`}
            hint={`${viewers.some((v) => v.ip === c.ip && v.user === c.user) ? tr('settings.sharing.thisSession') : tr('settings.sharing.ownSession')} · ${ago(c.since, tr)}`}>
            {isAdmin && <Button small label={tr('settings.sharing.disconnect')} onClick={() => act('remote-kick', String(c.pid))} />}
          </SettingRow>
        ))}
      </Section>
    </Stack>
  );
}

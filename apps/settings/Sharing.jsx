// Ajustes → Compartir: escritorio remoto (lord-sessiond, docs/REMOTO.md). Apagado por defecto;
// prenderlo, elegir quién puede entrar y ver o cortar las conexiones pasa por el ayudante de
// administración (acciones remote-*).
import {
  View, Text, Button, Toggle, Segmented, useTheme, useLocale, useStore, admin, remote, remoteViewers, useSystemInfo,
} from '@lordos/sdk';
import { useEffect, useState } from 'react';

const Divider = () => <View style={{ height: 1, backgroundColor: useTheme().separator }} />;

function Row({ label, hint, children }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 12, gap: 16 }}>
      <View style={{ flex: 1 }}>
        <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{label}</Text>
        {hint ? <Text style={{ fontSize: 11, color: t.textDim, marginTop: 3, lineHeight: 1.35 }}>{hint}</Text> : null}
      </View>
      {children}
    </View>
  );
}

// Direcciones IPv4 de la LAN de este equipo (para armar la URL).
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
    <View style={{ paddingBottom: 24 }}>
      <Text style={{ fontSize: 14, fontWeight: 700, color: t.text, marginTop: 8 }}>{tr('settings.sharing.remote')}</Text>
      <Row label={tr('settings.sharing.remoteToggle')} hint={tr('settings.sharing.remoteHint')}>
        <Toggle on={st.enabled} onChange={(on) => !busy && isAdmin && act(on ? 'remote-enable' : 'remote-disable')} />
      </Row>
      {!isAdmin && <Text style={{ fontSize: 11, color: '#ff9f0a' }}>{tr('settings.admin.unavailable')}</Text>}
      {!st.installed && <Text style={{ fontSize: 11, color: '#ff9f0a', lineHeight: 1.4 }}>{tr('settings.sharing.notInstalled')}</Text>}
      {busy && <Text style={{ fontSize: 12, color: t.textDim }}>{tr('settings.sharing.working')}</Text>}
      {msg && <Text style={{ fontSize: 12, color: msg.ok ? '#30d158' : '#ff453a', marginTop: 4 }}>{msg.text}</Text>}
      {st.enabled && (
        <View style={{ marginTop: 6, padding: 12, borderRadius: 10, backgroundColor: t.hover, gap: 6 }}>
          <Text style={{ fontSize: 12, color: t.textDim }}>{tr('settings.sharing.openFrom')}</Text>
          {urls.map((u) => <Text key={u} style={{ fontSize: 15, fontWeight: 700, color: t.accent }}>{u}</Text>)}
          {!urls.length && <Text style={{ fontSize: 12, color: t.textDim }}>{tr('settings.sharing.noLan')}</Text>}
          {st.fingerprint ? (
            <View style={{ marginTop: 4 }}>
              <Text style={{ fontSize: 11, color: t.textDim }}>{tr('settings.sharing.fingerprint')}</Text>
              <Text style={{ fontSize: 10, color: t.text, fontFamily: 'monospace', lineHeight: 1.4 }}>{st.fingerprint}</Text>
            </View>
          ) : null}
        </View>
      )}
      <Divider />
      <Row label={tr('settings.sharing.who')} hint={tr(st.allow === 'group' ? 'settings.sharing.whoGroupHint' : 'settings.sharing.whoAllHint')}>
        <Segmented value={st.allow} onChange={(allow) => isAdmin && act('remote-config', `allow=${allow}`)}
          options={[['all', tr('settings.sharing.allUsers')], ['group', tr('settings.sharing.onlyGroup')]]} />
      </Row>
      <Divider />
      <Text style={{ fontSize: 14, fontWeight: 700, color: t.text, marginTop: 14 }}>{tr('settings.sharing.connected')}</Text>
      {!st.clients.length && <Text style={{ fontSize: 12, color: t.textDim, marginVertical: 8 }}>{tr('settings.sharing.nobody')}</Text>}
      {st.clients.map((c) => (
        <View key={c.pid} style={{ flexDirection: 'row', alignItems: 'center', gap: 12, paddingVertical: 8 }}>
          <View style={{ width: 8, height: 8, borderRadius: 4, backgroundColor: '#30d158' }} />
          <View style={{ flex: 1 }}>
            <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{`${c.user} · ${c.ip}`}</Text>
            <Text style={{ fontSize: 11, color: t.textDim }}>{
              `${viewers.some((v) => v.ip === c.ip && v.user === c.user) ? tr('settings.sharing.thisSession') : tr('settings.sharing.ownSession')} · ${ago(c.since, tr)}`}</Text>
          </View>
          {isAdmin && <Button label={tr('settings.sharing.disconnect')} onClick={() => act('remote-kick', String(c.pid))} />}
        </View>
      ))}
      <Text style={{ fontSize: 11, color: t.textDim, marginTop: 14, lineHeight: 1.45 }}>{tr('settings.sharing.security')}</Text>
    </View>
  );
}

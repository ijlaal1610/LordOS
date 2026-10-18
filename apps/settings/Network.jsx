// Ajustes → Red: interfaces, Wi-Fi y VPN (WireGuard propio, OpenVPN). Lee el estado sin root
// (lord.net, netlink) y cambia por el ayudante de administración (acciones net-*). Ver docs/RED.md.
import {
  View, Text, useTheme, useLocale, Button, Toggle, Segmented, TextInput, SectionTitle,
  admin, fs, formatBytes, keyring, AutofillButton,
} from '@lordos/sdk';
import { useEffect, useState } from 'react';

// Interfaces de VPN (WireGuard, OpenVPN, IPsec, PPTP, OpenConnect, Fortinet): se muestran en VPN.
const isVpnIf = (i) => ['wireguard', 'tun', 'xfrm', 'ppp'].includes(i.type) || /^(wg|ovpn|ips|ppp|oc|fvpn)-/.test(i.name);
const has = () => typeof lord.net !== 'undefined';
const Divider = () => <View style={{ height: 1, backgroundColor: useTheme().separator }} />;

// Se vuelve a leer cuando el kernel avisa (link/addr/route), y cada 2 s para el tráfico.
let netGen = 0;
const netSubs = new Set();
globalThis.__lordNet = () => { netGen++; for (const fn of netSubs) fn(netGen); };
function useNet(intervalMs = 2000) {
  const [, setG] = useState(0);
  useEffect(() => {
    const fn = (g) => setG(g);
    netSubs.add(fn);
    const id = setInterval(() => setG((g) => g + 1), intervalMs);
    return () => { netSubs.delete(fn); clearInterval(id); };
  }, []);
  return has() ? { ifs: lord.net.interfaces(), routes: lord.net.routes(), dns: lord.net.dns() } : { ifs: [], routes: [], dns: { servers: [] } };
}

// Avisos de los procesos de red (DHCP, Wi-Fi, VPN) para mostrar estado.
const events = [];
const eventSubs = new Set();
admin.onEvent((text) => {
  if (!text.startsWith('net ')) return;
  events.push(text);
  if (events.length > 50) events.shift();
  for (const fn of eventSubs) fn(text);
});
function useNetEvent(fn) { useEffect(() => { eventSubs.add(fn); return () => eventSubs.delete(fn); }, []); }

function Row({ label, children }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 8, gap: 12 }}>
      <Text style={{ width: 150, fontSize: 13, color: t.textDim }}>{label}</Text>
      <View style={{ flex: 1, flexDirection: 'row', alignItems: 'center', gap: 8 }}>{children}</View>
    </View>
  );
}

function Message({ msg }) {
  if (!msg) return null;
  return <Text style={{ fontSize: 12, color: msg.ok ? '#30d158' : '#ff453a', marginVertical: 6 }}>{msg.text}</Text>;
}

const run = (setMsg, action, arg, okText) => admin.run(action, arg, undefined, { quiet: true }).then(
  (r) => { setMsg({ ok: true, text: okText || r }); return r; },
  (e) => { setMsg({ ok: false, text: e.message }); return null; });

const TYPES = ['ethernet', 'wifi', 'wireguard', 'tun', 'bridge', 'veth', 'vlan', 'bond', 'dummy'];
const typeLabel = (type, tr) => (TYPES.includes(type) ? tr(`network.type.${type}`) : type);

const statusOf = (i, tr) => (!i.up ? tr('network.status.off') : !i.carrier && i.type !== 'wireguard' && i.type !== 'tun' ? tr('network.status.noCable')
  : i.addresses.some((a) => a.scope === 'global') ? tr('network.status.connected') : tr('network.status.noAddress'));
const dotColor = (i) => (!i.up ? '#8e8e93' : i.addresses.some((a) => a.scope === 'global') ? '#30d158' : '#ff9f0a');

// ---- una interfaz ----

function InterfacePage({ name, net }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const i = net.ifs.find((x) => x.name === name);
  const [msg, setMsg] = useState(null);
  const saved = has() ? lord.net.config(name) : null;
  const [form, setForm] = useState(null);
  useEffect(() => { setMsg(null); setForm(null); }, [name]);
  if (!i) return <Text style={{ color: t.textDim }}>{tr('network.gone', { name })}</Text>;
  const gw = net.routes.find((r) => r.dst === 'default' && r.dev === name);
  const isAdmin = admin.available();
  const sys = i.managed === 'system';
  const f = form || { method: saved?.method || 'dhcp', address: saved?.addresses?.[0] || '', gateway: saved?.gateway || '', dns: (saved?.dns || []).join(' ') };
  const apply = () => {
    const lines = [name, `method=${f.method}`];
    if (f.method === 'static') {
      if (f.address) lines.push(`address=${f.address.trim()}`);
      if (f.gateway) lines.push(`gateway=${f.gateway.trim()}`);
      for (const d of f.dns.split(/[\s,]+/).filter(Boolean)) lines.push(`dns=${d}`);
    }
    run(setMsg, 'net-config', lines.join('\n'), tr('network.applied')).then(() => setForm(null));
  };
  return (
    <View>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10, marginBottom: 6 }}>
        <View style={{ width: 10, height: 10, borderRadius: 5, backgroundColor: dotColor(i) }} />
        <Text style={{ flex: 1, fontSize: 16, fontWeight: 700, color: t.text }}>{name}</Text>
        {!sys && isAdmin && <Toggle on={i.up} onChange={(on) => run(setMsg, on ? 'net-up' : 'net-down', name)} />}
      </View>
      <Text style={{ fontSize: 12, color: t.textDim, marginBottom: 8 }}>{`${typeLabel(i.type, tr)} · ${statusOf(i, tr)}`}</Text>
      <Message msg={msg} />
      {i.addresses.filter((a) => a.scope !== 'host').map((a) => (
        <Row key={`${a.address}/${a.prefix}`} label={a.family === 4 ? 'IPv4' : 'IPv6'}>
          <Text style={{ fontSize: 13, color: t.text }}>{`${a.address}/${a.prefix}`}</Text>
          {a.dynamic && <Text style={{ fontSize: 11, color: t.textDim }}>{tr('network.dynamic')}</Text>}
        </Row>
      ))}
      {gw && <Row label={tr('network.gateway')}><Text style={{ fontSize: 13, color: t.text }}>{gw.gateway}</Text></Row>}
      {gw && net.dns.servers.length > 0 && <Row label="DNS"><Text style={{ fontSize: 13, color: t.text }}>{net.dns.servers.join(', ')}</Text></Row>}
      {i.mac && i.type !== 'wireguard' && <Row label="MAC"><Text style={{ fontSize: 13, color: t.text }}>{i.mac}</Text></Row>}
      <Row label={tr('network.traffic')}>
        <Text style={{ fontSize: 13, color: t.text }}>{tr('network.trafficValue', { rx: formatBytes(i.rxBytes), tx: formatBytes(i.txBytes) })}</Text>
      </Row>
      <Divider />
      <SectionTitle>{tr('network.config')}</SectionTitle>
      {sys ? (
        <View style={{ gap: 8, padding: 4 }}>
          <Text style={{ fontSize: 12, color: t.textDim, lineHeight: 1.4 }}>{tr('network.systemManaged')}</Text>
          {isAdmin && <Button style={{ alignSelf: 'flex-start' }} label={tr('network.manage')} onClick={() => run(setMsg, 'net-manage', name, tr('network.managedNow'))} />}
        </View>
      ) : i.type === 'wireguard' || i.type === 'tun' ? (
        <Text style={{ fontSize: 12, color: t.textDim, padding: 4 }}>{tr('network.vpnInterface')}</Text>
      ) : (
        <View>
          <Row label={tr('network.method')}>
            <Segmented value={f.method} onChange={(method) => setForm({ ...f, method })}
              options={[['dhcp', tr('network.dhcp')], ['static', tr('network.static')], ['off', tr('network.off')]]} />
          </Row>
          {f.method === 'static' && (
            <View>
              <Row label={tr('network.address')}><TextInput value={f.address} onChange={(address) => setForm({ ...f, address })} placeholder="192.168.1.20/24" style={{ width: 220 }} /></Row>
              <Row label={tr('network.gateway')}><TextInput value={f.gateway} onChange={(gateway) => setForm({ ...f, gateway })} placeholder="192.168.1.1" style={{ width: 220 }} /></Row>
              <Row label="DNS"><TextInput value={f.dns} onChange={(dns) => setForm({ ...f, dns })} placeholder="1.1.1.1 9.9.9.9" style={{ width: 220 }} /></Row>
            </View>
          )}
          <View style={{ flexDirection: 'row', gap: 8, marginTop: 6 }}>
            <Button primary label={tr('network.apply')} onClick={() => isAdmin && apply()} />
            {i.managed === 'lordos' && isAdmin && saved && <Button label={tr('network.unmanage')} onClick={() => run(setMsg, 'net-unmanage', name)} />}
          </View>
          {!isAdmin && <Text style={{ fontSize: 11, color: '#ff9f0a', marginTop: 6 }}>{tr('network.needAdmin')}</Text>}
        </View>
      )}
    </View>
  );
}

// ---- Wi-Fi ----

const bars = (dbm) => (dbm >= -55 ? '▂▄▆█' : dbm >= -67 ? '▂▄▆' : dbm >= -78 ? '▂▄' : '▂');

function WifiPage({ net }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const radios = net.ifs.filter((i) => i.type === 'wifi');
  const [dev, setDev] = useState(radios[0]?.name || '');
  const [list, setList] = useState([]);
  const [scanning, setScanning] = useState(false);
  const [pick, setPick] = useState(null);
  const [pass, setPass] = useState('');
  const [msg, setMsg] = useState(null);
  const known = has() ? lord.net.wifiKnown() : [];
  const cur = net.ifs.find((i) => i.name === dev);
  const scan = () => {
    if (!dev) return;
    setScanning(true);
    const found = [];
    admin.run('net-wifi-scan', dev, (line) => { try { found.push(JSON.parse(line)); } catch (e) { /* texto */ } }, { quiet: true })
      .then(() => setList(found.sort((a, b) => b.signal - a.signal)), (e) => setMsg({ ok: false, text: e.message }))
      .finally(() => setScanning(false));
  };
  useEffect(() => { scan(); }, [dev]);
  useNetEvent((text) => {
    const m = /^net wifi (\S+) (connected|disconnected|wrong-key|no-sae)\s?(.*)$/.exec(text);
    if (!m || m[1] !== dev) return;
    if (m[2] === 'connected') setMsg({ ok: true, text: tr('network.wifi.connected', { ssid: m[3] }) });
    else if (m[2] === 'wrong-key') setMsg({ ok: false, text: tr('network.wifi.wrongKey') });
    else if (m[2] === 'no-sae') setMsg({ ok: false, text: tr('network.wifi.noSae') });
  });
  if (!radios.length) return <Text style={{ fontSize: 13, color: t.textDim }}>{tr('network.wifi.none')}</Text>;
  const connect = (n, password) => {
    setPick(null);
    setPass('');
    // the password also goes to the session keyring (Passwords → Session keyring; docs/PASSWORDS.md)
    if (password && n.ssid) keyring.set('settings', { service: `wifi:${n.ssid}`, secret: password, label: `Wi-Fi ${n.ssid}` }).catch(() => {});
    run(setMsg, 'net-wifi-connect', `${dev}\n${n.ssid}\n${password || ''}`, tr('network.wifi.connecting', { ssid: n.ssid }));
  };
  return (
    <View>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10, marginBottom: 8 }}>
        <Text style={{ flex: 1, fontSize: 16, fontWeight: 700, color: t.text }}>Wi-Fi</Text>
        {radios.length > 1 && <Segmented value={dev} onChange={setDev} options={radios.map((r) => [r.name, r.name])} />}
        <Button label={scanning ? tr('network.wifi.scanning') : tr('network.wifi.scan')} onClick={() => !scanning && scan()} />
        {cur?.up && <Button label={tr('network.wifi.disconnect')} onClick={() => run(setMsg, 'net-wifi-disconnect', dev)} />}
      </View>
      <Message msg={msg} />
      {list.length === 0 && !scanning && <Text style={{ fontSize: 12, color: t.textDim }}>{tr('network.wifi.noNetworks')}</Text>}
      {list.map((n, idx) => (
        <View key={`${n.bssid}${idx}`}>
          {idx > 0 && <Divider />}
          <View onClick={() => (n.security === 'open' || n.known ? connect(n, '') : setPick(n))}
            style={{ flexDirection: 'row', alignItems: 'center', gap: 10, paddingVertical: 9, paddingHorizontal: 4 }}>
            <Text style={{ width: 34, fontSize: 12, color: t.accent, direction: 'ltr' }}>{bars(n.signal)}</Text>
            <Text style={{ flex: 1, fontSize: 13, fontWeight: 600, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{n.ssid || tr('network.wifi.hidden')}</Text>
            {n.known && <Text style={{ fontSize: 11, color: t.textDim }}>{tr('network.wifi.saved')}</Text>}
            <Text style={{ fontSize: 11, color: t.textDim }}>{n.security === 'open' ? tr('network.wifi.open') : `🔒 ${n.security.toUpperCase()}`}</Text>
            <Text style={{ width: 70, fontSize: 11, color: t.textDim, textAlign: 'end' }}>{`${n.signal} dBm`}</Text>
          </View>
          {pick === n && (
            <View style={{ flexDirection: 'row', gap: 8, paddingBottom: 10, paddingHorizontal: 4 }}>
              <TextInput secure autoFocus value={pass} onChange={setPass} placeholder={tr('network.wifi.password')} onSubmit={() => connect(n, pass)}
                onCancel={() => setPick(null)} style={{ flex: 1 }} />
              <AutofillButton app="settings" service={`wifi:${n.ssid}`} ssid={n.ssid} onFill={({ password }) => setPass(password || '')} />
              <Button primary label={tr('network.wifi.join')} onClick={() => connect(n, pass)} />
            </View>
          )}
        </View>
      ))}
      {known.length > 0 && <SectionTitle>{tr('network.wifi.known')}</SectionTitle>}
      {known.map((k) => (
        <View key={k.ssid} style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 6, paddingHorizontal: 4 }}>
          <Text style={{ flex: 1, fontSize: 13, color: t.text }}>{k.ssid}</Text>
          {admin.available() && <Button label={tr('network.wifi.forget')} onClick={() => run(setMsg, 'net-wifi-forget', k.ssid)} />}
        </View>
      ))}
    </View>
  );
}

// ---- VPN ----

// Configuraciones encontradas en la carpeta personal (Descargas, Documentos, Escritorio).
function findVpnFiles() {
  const out = [];
  for (const dir of ['', 'Descargas', 'Downloads', 'Documentos', 'Documents', 'Escritorio', 'Desktop']) {
    let list = [];
    try { list = fs.list(dir || '.'); } catch (e) { continue; }
    for (const e of list) if (!e.dir && /\.(conf|ovpn)$/i.test(e.name) && e.size < 256 * 1024) out.push({ rel: dir ? `${dir}/${e.name}` : e.name, name: e.name });
  }
  return out;
}

const cleanName = (s) => s.replace(/\.(conf|ovpn)$/i, '').replace(/[^A-Za-z0-9_-]/g, '-').slice(0, 24) || 'vpn';

const VPN_LABEL = { wireguard: 'WireGuard', openvpn: 'OpenVPN', ipsec: 'IPsec / IKEv2', pptp: 'PPTP', openconnect: 'Cisco AnyConnect', fortinet: 'Fortinet' };
const FILE_TYPES = ['wireguard', 'openvpn'];

// Campos del formulario de cada tipo (los que no vienen en un archivo). [clave, etiqueta, opciones]
const FORM = {
  ipsec: [['server'], ['auth', null, ['eap', 'psk']], ['user', null, null, (f) => f.auth !== 'psk'], ['password', null, null, (f) => f.auth !== 'psk', true],
    ['psk', null, null, (f) => f.auth === 'psk', true], ['remote_id', null, null, null, false, true], ['routes', null, null, null, false, true]],
  pptp: [['server'], ['user'], ['password', null, null, null, true], ['routes', null, null, null, false, true]],
  openconnect: [['server'], ['protocol', null, ['anyconnect', 'gp', 'pulse']], ['user'], ['password', null, null, null, true],
    ['usergroup', null, null, null, false, true], ['servercert', null, null, null, false, true]],
  fortinet: [['host'], ['port', null, null, null, false, true], ['user'], ['password', null, null, null, true], ['trusted-cert', null, null, null, false, true],
    ['routes', null, null, null, false, true]],
};
const FORM_DEFAULTS = { ipsec: { auth: 'eap' }, openconnect: { protocol: 'anyconnect' }, fortinet: { port: '443' } };

function VpnImport({ onDone }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const types = has() && lord.net.vpnTypes ? lord.net.vpnTypes() : FILE_TYPES.map((type) => ({ type, available: true }));
  const [type, setType] = useState(null);
  const [files] = useState(findVpnFiles);
  const [file, setFile] = useState(null);
  const [name, setName] = useState('');
  const [form, setForm] = useState({});
  const [msg, setMsg] = useState(null);
  const [enabling, setEnabling] = useState(null);
  const [, setTick] = useState(0);
  const pick = (tp) => { setType(tp.type); setForm({ ...(FORM_DEFAULTS[tp.type] || {}) }); setFile(null); setMsg(null); setName(''); };
  const enable = (tp) => {
    setEnabling(tp.type);
    admin.run('net-vpn-enable', tp.type, undefined, { quiet: true }).then(
      () => { setMsg({ ok: true, text: tr('network.vpn.enabled', { name: VPN_LABEL[tp.type] }) }); setTick((x) => x + 1); },
      (e) => setMsg({ ok: false, text: e.message })).finally(() => setEnabling(null));
  };
  const doImport = () => {
    let config;
    const vpnName = cleanName(name || (file ? file.name : type));
    if (FILE_TYPES.includes(type)) {
      try { config = type === 'openvpn' ? lord.net.prepareOvpn(`${fs.home()}/${file.rel}`) : fs.read(file.rel); } catch (e) { setMsg({ ok: false, text: e.message }); return; }
    } else {
      const fields = FORM[type].filter(([k, , , show]) => !show || show(form));
      const missing = fields.find(([k, , , , , optional]) => !optional && !String(form[k] || '').trim());
      if (missing) { setMsg({ ok: false, text: tr('network.vpn.missing', { field: tr(`network.vpn.field.${missing[0]}`) }) }); return; }
      config = fields.filter(([k]) => String(form[k] || '').trim()).map(([k]) => `${k}=${String(form[k]).trim()}`).join('\n');
    }
    run(setMsg, 'net-vpn-import', `${type}\n${vpnName}\n${config}`, tr('network.vpn.imported')).then((r) => r != null && onDone());
  };
  const cur = types.find((x) => x.type === type);
  return (
    <View style={{ padding: 12, borderRadius: 10, backgroundColor: t.hover, gap: 8, marginBottom: 12 }}>
      <Text style={{ fontSize: 13, fontWeight: 700, color: t.text }}>{tr('network.vpn.importTitle')}</Text>
      <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 6 }}>
        {types.map((tp) => (
          <View key={tp.type} onClick={() => pick(tp)} style={{ paddingHorizontal: 10, paddingVertical: 6, borderRadius: 7, gap: 2,
            backgroundColor: type === tp.type ? t.accent : t.control }}>
            <Text style={{ fontSize: 12, fontWeight: 600, color: type === tp.type ? '#fff' : t.text }}>{VPN_LABEL[tp.type] || tp.type}</Text>
            {(tp.optional || tp.insecure) && <Text style={{ fontSize: 10, color: type === tp.type ? '#fff' : tp.insecure ? '#ff9f0a' : t.textDim }}>
              {tp.insecure ? tr('network.vpn.insecure') : tp.available ? tr('network.vpn.optionalOn') : tr('network.vpn.optionalOff')}</Text>}
          </View>
        ))}
      </View>
      {cur && cur.optional && !cur.available && (
        <View style={{ gap: 6 }}>
          <Text style={{ fontSize: 12, color: t.text, lineHeight: 1.4 }}>{tr('network.vpn.enableHint', { name: VPN_LABEL[cur.type], pkg: cur.package })}</Text>
          <Button primary style={{ alignSelf: 'flex-start' }} label={enabling ? tr('network.vpn.enabling') : tr('network.vpn.enable')}
            onClick={() => !enabling && admin.available() && enable(cur)} />
        </View>
      )}
      {cur && cur.insecure && <Text style={{ fontSize: 11, color: '#ff9f0a', lineHeight: 1.4 }}>{tr('network.vpn.pptpWarning')}</Text>}
      {cur && cur.available && FILE_TYPES.includes(type) && (
        <View style={{ gap: 4 }}>
          <Text style={{ fontSize: 11, color: t.textDim, lineHeight: 1.35 }}>{tr('network.vpn.importHint')}</Text>
          {files.filter((f) => (type === 'openvpn' ? /\.ovpn$/i : /\.conf$/i).test(f.rel)).map((f) => (
            <View key={f.rel} onClick={() => { setFile(f); setName(cleanName(f.name)); }} style={{ paddingHorizontal: 8, paddingVertical: 6, borderRadius: 6,
              backgroundColor: file === f ? t.accent : 'transparent' }}>
              <Text style={{ fontSize: 12, color: file === f ? '#fff' : t.text }}>{`~/${f.rel}`}</Text>
            </View>
          ))}
          {!files.some((f) => (type === 'openvpn' ? /\.ovpn$/i : /\.conf$/i).test(f.rel)) && <Text style={{ fontSize: 12, color: t.textDim }}>{tr('network.vpn.noFiles')}</Text>}
        </View>
      )}
      {cur && cur.available && FORM[type] && (
        <View>
          {FORM[type].filter(([, , , show]) => !show || show(form)).map(([k, , options, , secret, optional]) => (
            <Row key={k} label={`${tr(`network.vpn.field.${k}`)}${optional ? ` (${tr('network.vpn.optional')})` : ''}`}>
              {options ? <Segmented value={form[k]} onChange={(v) => setForm({ ...form, [k]: v })} options={options.map((o) => [o, tr(`network.vpn.opt.${o}`)])} />
                : <TextInput secure={secret} value={form[k] || ''} onChange={(v) => setForm({ ...form, [k]: v.replace(/[\r\n]/g, '') })} style={{ width: 260 }} />}
            </Row>
          ))}
        </View>
      )}
      {cur && cur.available && (FORM[type] || file) && (
        <View style={{ flexDirection: 'row', gap: 8, alignItems: 'center' }}>
          <Text style={{ fontSize: 12, color: t.textDim }}>{tr('network.vpn.name')}</Text>
          <TextInput value={name} onChange={(v) => setName(v.replace(/[^A-Za-z0-9_-]/g, '').slice(0, 24))} placeholder={type} style={{ width: 180 }} />
          <Button primary label={tr('network.vpn.import')} onClick={doImport} />
        </View>
      )}
      <Message msg={msg} />
      <Button style={{ alignSelf: 'flex-start' }} label={tr('network.close')} onClick={onDone} />
    </View>
  );
}

function VpnPage({ net }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [importing, setImporting] = useState(false);
  const [msg, setMsg] = useState(null);
  const [status, setStatus] = useState({});
  const [auth, setAuth] = useState(null);         // { name, user, pass }
  const [, setTick] = useState(0);
  const list = has() ? lord.net.vpnList() : [];
  const refresh = () => {
    const st = {};
    admin.run('net-vpn-status', '', (line) => { try { const j = JSON.parse(line); st[j.name] = j; } catch (e) { /* texto */ } }, { quiet: true })
      .then(() => setStatus(st), () => {});
  };
  useEffect(() => { refresh(); const id = setInterval(refresh, 4000); return () => clearInterval(id); }, []);
  useNetEvent((text) => {
    const m = /^net vpn (\S+) (\S+)\s?(.*)$/.exec(text);
    if (!m) return;
    if (m[2] === 'connected') setMsg({ ok: true, text: tr('network.vpn.connectedMsg', { name: m[1] }) });
    else if (m[2] === 'need-auth') setAuth({ name: m[1], user: '', pass: '' });
    else if (m[2] === 'auth-failed') setMsg({ ok: false, text: tr('network.vpn.authFailed', { name: m[1] }) });
    else if (m[2] === 'error' || m[2] === 'log') setMsg({ ok: false, text: `${m[1]}: ${m[3] || tr('network.vpn.error')}` });
    refresh();
    setTick((x) => x + 1);
  });
  const isAdmin = admin.available();
  const up = (v) => run(setMsg, 'net-vpn-up', v.name).then(refresh);
  const down = (v) => run(setMsg, 'net-vpn-down', v.name).then(refresh);
  return (
    <View>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10, marginBottom: 8 }}>
        <Text style={{ flex: 1, fontSize: 16, fontWeight: 700, color: t.text }}>VPN</Text>
        {isAdmin && !importing && <Button label={tr('network.vpn.add')} onClick={() => setImporting(true)} />}
      </View>
      {importing && <VpnImport onDone={() => { setImporting(false); setTick((x) => x + 1); }} />}
      <Message msg={msg} />
      {auth && (
        <View style={{ padding: 12, borderRadius: 10, backgroundColor: t.hover, gap: 8, marginBottom: 10 }}>
          <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{tr('network.vpn.authTitle', { name: auth.name })}</Text>
          <TextInput value={auth.user} onChange={(user) => setAuth({ ...auth, user })} placeholder={tr('network.vpn.user')} />
          <View style={{ flexDirection: 'row', gap: 8, alignItems: 'center' }}>
            <TextInput secure value={auth.pass} onChange={(pass) => setAuth({ ...auth, pass })} placeholder={tr('network.vpn.password')} style={{ flex: 1 }} />
            <AutofillButton app="settings" service={`vpn:${auth.name}`} onFill={({ username, password }) => setAuth({ ...auth, user: username || auth.user, pass: password || '' })} />
          </View>
          <View style={{ flexDirection: 'row', gap: 8 }}>
            <Button primary label={tr('network.vpn.saveConnect')} onClick={() => {
              const a = auth;
              setAuth(null);
              if (a.pass) keyring.set('settings', { service: `vpn:${a.name}`, account: a.user, secret: a.pass, label: `VPN ${a.name}` }).catch(() => {});
              run(setMsg, 'net-vpn-credentials', `${a.name}\n${a.user}\n${a.pass}`).then((r) => r != null && up({ name: a.name }));
            }} />
            <Button label={tr('network.cancel')} onClick={() => setAuth(null)} />
          </View>
        </View>
      )}
      {list.length === 0 && !importing && <Text style={{ fontSize: 12, color: t.textDim }}>{tr('network.vpn.none')}</Text>}
      {list.map((v, idx) => {
        const st = status[v.name] || {};
        const on = !!st.active;
        const peer = st.peers?.[0];
        const iface = net.ifs.find((i) => i.name === v.ifname);
        return (
          <View key={v.name}>
            {idx > 0 && <Divider />}
            <View style={{ flexDirection: 'row', alignItems: 'center', gap: 12, paddingVertical: 10 }}>
              <View style={{ width: 10, height: 10, borderRadius: 5, backgroundColor: on ? '#30d158' : st.running ? '#ff9f0a' : '#8e8e93' }} />
              <View style={{ flex: 1, gap: 2 }}>
                <Text style={{ fontSize: 14, fontWeight: 600, color: t.text }}>{v.name}</Text>
                <Text style={{ fontSize: 11, color: t.textDim }}>{`${VPN_LABEL[v.type] || v.type} · ${v.server || ''}${v.user ? ` · ${v.user}` : ''}${v.fullTunnel && v.type !== 'openconnect' ? ` · ${tr('network.vpn.fullTunnel')}` : ''}${v.insecure ? ` · ⚠ ${tr('network.vpn.insecure')}` : ''}`}</Text>
                {on && peer?.lastHandshake ? <Text style={{ fontSize: 11, color: t.textDim }}>{tr('network.vpn.handshake', {
                  s: Math.max(0, Math.round(Date.now() / 1000 - peer.lastHandshake)), rx: formatBytes(peer.rxBytes), tx: formatBytes(peer.txBytes) })}</Text> : null}
                {on && v.type !== 'wireguard' && iface ? <Text style={{ fontSize: 11, color: t.textDim }}>{
                  `${iface.addresses.find((a) => a.family === 4)?.address || ''} · ${tr('network.trafficValue', { rx: formatBytes(iface.rxBytes), tx: formatBytes(iface.txBytes) })}`}</Text> : null}
                {v.type === 'wireguard' && v.publicKey ? <Text style={{ fontSize: 10, color: t.textDim }}>{tr('network.vpn.publicKey', { key: v.publicKey })}</Text> : null}
              </View>
              <Toggle on={on || !!st.running} onChange={(x) => (x ? up(v) : down(v))} />
              {isAdmin && v.type === 'openvpn' && v.needsAuth && <Button label={tr('network.vpn.credentials')} onClick={() => setAuth({ name: v.name, user: '', pass: '' })} />}
              {isAdmin && <Button label={tr('network.vpn.delete')} onClick={() => run(setMsg, 'net-vpn-delete', v.name).then(() => setTick((x) => x + 1))} />}
            </View>
          </View>
        );
      })}
    </View>
  );
}

// ---- sección de Ajustes ----

// Arriba, una fila con las conexiones, Wi-Fi y VPN; abajo, la que se eligió.
let lastPage = null;
export function NetworkSettings({ page: initial }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const net = useNet();
  const shown = net.ifs.filter((i) => !isVpnIf(i) && i.type !== 'loopback');
  const [page, setPageState] = useState(initial || lastPage || (shown[0] ? `if:${shown[0].name}` : 'wifi'));
  const setPage = (p) => { lastPage = p; setPageState(p); };
  useEffect(() => { if (initial) setPage(initial); }, [initial]);
  if (!has()) return <Text style={{ color: t.textDim, marginTop: 8 }}>{tr('network.unavailable')}</Text>;
  const tabs = [...shown.map((i) => [`if:${i.name}`, i.name, dotColor(i)]), ['wifi', 'Wi-Fi'], ['vpn', 'VPN']];
  return (
    <View style={{ paddingBottom: 20 }}>
      <View style={{ flexDirection: 'row', gap: 6, marginTop: 4, marginBottom: 14 }}>
        {tabs.map(([id, label, dot]) => (
          <View key={id} onClick={() => setPage(id)} style={{ flexDirection: 'row', alignItems: 'center', gap: 6, paddingHorizontal: 12, height: 28,
            borderRadius: 7, backgroundColor: page === id ? t.accent : t.control }}>
            {dot && <View style={{ width: 7, height: 7, borderRadius: 4, backgroundColor: dot }} />}
            <Text style={{ fontSize: 12, fontWeight: 600, color: page === id ? '#fff' : t.text }}>{label}</Text>
          </View>
        ))}
      </View>
      {page.startsWith('if:') && <InterfacePage name={page.slice(3)} net={net} />}
      {page === 'wifi' && <WifiPage net={net} />}
      {page === 'vpn' && <VpnPage net={net} />}
    </View>
  );
}

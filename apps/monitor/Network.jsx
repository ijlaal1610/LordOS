// Network.jsx — the Monitor's Network tab, laid out like a per-app network monitor, with TangoOS colors:
//   izquierda: apps con conexiones (búsqueda, barra de tráfico, columna de regla reservada para
//              la fase 2), cada una desplegable con sus destinos; abajo, totales y barras de
//              subida/bajada por segundo;
//   centro:    mapa con un arco desde este equipo a cada país y un punto por país;
//   derecha:   resumen (sin selección) o el detalle de la app elegida.
// Todo es de solo lectura. El país sale de la base local (DB-IP Lite); ninguna IP sale del
// equipo, por eso no hay DNS inverso (consultarlo mandaría las IPs al servidor DNS).
//
// Modelo para la fase 2 (firewall): cada conexión trae `verdict` ('allow' hoy) y se agrupa por
// app con una clave estable (`appKey`: 'lordos', 'desktop:<id>' o la ruta del ejecutable). Las
// reglas futuras son { appKey, host | país, puerto, protocolo, dirección, verdict }: la columna
// de regla de la lista y los contadores de permitidas/denegadas ya están en su lugar.
import {
  View, Text, Image, ScrollView, SearchField, useTheme, useLocale, useStore, monitor, monitorConnections, useMonitor,
  formatBytes, formatNumber, tOptional, useSystemInfo, wm, direction, firewall, firewallState, useLauncherApps, Lumo,
} from '@lordos/sdk';
import { useEffect, useMemo, useRef, useState } from 'react';
import { C, fmt, AppGlyph, useCardStyle } from './index.jsx';
import { Glyph } from './icons.jsx';
import { canvas, drawBars } from './draw.js';
import { MapView } from './MapView.jsx';

export const appKeyOf = (c) => (c.self ? 'lordos' : c.app || c.exe || c.name || '?');
const countryName = (cc) => (cc ? tOptional(`countries.${cc}`) || cc : '');

// Agrupa las conexiones por app; los bytes por segundo salen de la diferencia con la lectura anterior.
function useGroups(conns) {
  const prev = useRef({ t: 0, bytes: new Map() });
  return useMemo(() => {
    if (!conns) return { apps: [], list: [] };
    const dt = prev.current.t ? Math.max(0.2, (conns.t - prev.current.t) / 1000) : 0;
    const before = prev.current.bytes, now = new Map();
    const apps = new Map();
    for (const c of conns.list) {
      const key = appKeyOf(c);
      let g = apps.get(key);
      if (!g) {
        g = { key, item: c, name: c.self ? null : c.appName || c.name || '?', exe: c.exe, pids: new Set(), conns: [], hosts: new Map(),
          sent: 0, recv: 0, rateTx: 0, rateRx: 0, listen: [], incoming: 0, verdict: 'allow' };
        apps.set(key, g);
      }
      if (!g.exe && c.exe) g.exe = c.exe;
      if (c.pid > 0) g.pids.add(c.pid);
      g.conns.push(c);
      const s = c.sent || 0, r = c.recv || 0;
      now.set(c.id, [s, r]);
      g.sent += s;
      g.recv += r;
      const old = before.get(c.id);
      if (old && dt) { g.rateTx += Math.max(0, s - old[0]) / dt; g.rateRx += Math.max(0, r - old[1]) / dt; }
      if (c.dir === 'listen') { g.listen.push(c); continue; }
      if (c.dir === 'in') g.incoming++;
      const h = g.hosts.get(c.remote) || { remote: c.remote, cc: c.cc, scope: c.scope, ports: new Set(), sent: 0, recv: 0, count: 0 };
      h.ports.add(`${c.proto.toUpperCase()} ${c.rport}`);
      h.sent += s;
      h.recv += r;
      h.count++;
      g.hosts.set(c.remote, h);
    }
    prev.current = { t: conns.t, bytes: now };
    const list = [...apps.values()].sort((a, b) => b.sent + b.recv - (a.sent + a.recv) || b.conns.length - a.conns.length);
    return { apps: list, list: conns.list };
  }, [conns]);
}

// Agrega a cada app su regla y sus contadores del firewall (lord.fw), y las apps que no tienen
// conexiones abiertas pero sí intentos (una app denegada no llega a tener sockets).
function withFirewall(groups, fw, launcher) {
  const counts = fw?.counts || {};
  const out = groups.map((g) => ({ ...g, rule: firewall.appRule(g.key), allowed: counts[g.key]?.[0] || 0, denied: counts[g.key]?.[1] || 0 }));
  const have = new Set(out.map((g) => g.key));
  for (const [key, v] of Object.entries(counts)) {
    if (have.has(key) || key === 'lordos' || !(v[0] || v[1])) continue;
    const la = launcher.find((a) => a.id === key || `lpk:${a.id}` === key);
    const name = la?.name || key.replace(/^(desktop|lpk):/, '');
    out.push({ key, item: { app: key, appName: name, name, appIcon: la?.icon || '' }, name, exe: '', pids: new Set(), conns: [], hosts: new Map(), sent: 0, recv: 0,
      rateTx: 0, rateRx: 0, listen: [], incoming: 0, verdict: 'deny', rule: firewall.appRule(key), allowed: v[0] || 0, denied: v[1] || 0 });
  }
  return out;
}

// ---- columna izquierda ----

// Interruptor de la columna Regla: prendido = la app puede usar la red. Escribe la regla
// { app, out, any, allow|deny } (lord.fw); ir y volver no acumula reglas. Las apps sin scope propia
// (un programa suelto, identificado por su ruta) no se pueden filtrar: el interruptor queda fijo.
function RuleSwitch({ g, selected, tr }) {
  const on = g.rule !== 'deny';
  const can = firewall.available() && firewall.enforceable(g.key);
  const color = on ? '#30d158' : '#ff453a';
  return (
    <View onClick={(e) => { e.stopPropagation(); if (can) firewall.setAppRule(g.key, on ? 'deny' : 'allow'); }}
      style={{ width: 26, height: 14, borderRadius: 7, backgroundColor: selected ? 'rgba(255,255,255,0.35)' : on ? 'rgba(48,209,88,0.35)' : 'rgba(255,69,58,0.35)',
        opacity: can ? 1 : 0.45, justifyContent: 'center', alignItems: on ? 'flex-end' : 'flex-start', paddingHorizontal: 2, direction: 'ltr' }}>
      <View style={{ width: 10, height: 10, borderRadius: 5, backgroundColor: selected ? '#fff' : color }} />
    </View>
  );
}

function AppRow({ g, open, selected, onToggle, onSelect, maxBytes, tr }) {
  const t = useTheme();
  const label = g.key === 'lordos' ? tr('monitor.lordos') : g.name;
  const total = g.sent + g.recv;
  const hosts = [...g.hosts.values()].sort((a, b) => b.sent + b.recv - (a.sent + a.recv) || b.count - a.count);
  const active = g.conns.length - g.listen.length;
  return (
    <View>
      <View onClick={onSelect} style={{ flexDirection: 'row', alignItems: 'center', gap: 8, height: 40, paddingHorizontal: 8, borderRadius: 8,
        backgroundColor: selected ? t.accent : 'transparent' }}>
        <View onClick={(e) => { e.stopPropagation(); onToggle(); }} style={{ width: 14, alignItems: 'center' }}>
          <Text style={{ fontSize: 10, color: selected ? '#fff' : t.textDim }}>{open ? '▾' : direction() === 'rtl' ? '◂' : '▸'}</Text>
        </View>
        <AppGlyph item={g.item} size={20} />
        <View style={{ flex: 1, minWidth: 0, gap: 3 }}>
          <View style={{ flexDirection: 'row', gap: 6 }}>
            <Text style={{ flex: 1, fontSize: 12, fontWeight: 600, color: selected ? '#fff' : t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{label}</Text>
            <View style={{ paddingHorizontal: 5, height: 14, borderRadius: 7, justifyContent: 'center', backgroundColor: selected ? 'rgba(255,255,255,0.25)' : t.control }}>
              <Text style={{ fontSize: 9, fontWeight: 600, color: selected ? '#fff' : t.textDim }}>{formatNumber(active, 0)}</Text>
            </View>
            {g.denied ? (
              <View style={{ paddingHorizontal: 5, height: 14, borderRadius: 7, justifyContent: 'center', backgroundColor: selected ? 'rgba(255,255,255,0.25)' : 'rgba(255,69,58,0.22)' }}>
                <Text style={{ fontSize: 9, fontWeight: 600, color: selected ? '#fff' : '#ff453a' }}>{`⊘ ${formatNumber(g.denied, 0)}`}</Text>
              </View>
            ) : null}
          </View>
          {/* barra de tráfico: recibido + enviado, relativa a la app que más movió */}
          <View style={{ height: 4, borderRadius: 2, backgroundColor: selected ? 'rgba(255,255,255,0.25)' : t.control, flexDirection: 'row', overflow: 'hidden', direction: 'ltr' }}>
            <View style={{ width: `${(g.recv / Math.max(1, maxBytes)) * 100}%`, height: 4, backgroundColor: C.rx }} />
            <View style={{ width: `${(g.sent / Math.max(1, maxBytes)) * 100}%`, height: 4, backgroundColor: C.tx }} />
          </View>
        </View>
        <Text style={{ width: 54, fontSize: 10, color: selected ? '#fff' : t.textDim, textAlign: 'end', whiteSpace: 'nowrap' }}>{total ? formatBytes(total) : '—'}</Text>
        {/* regla: permitir / denegar la red a la app (lord-netperm la aplica en el kernel) */}
        <RuleSwitch g={g} selected={selected} tr={tr} />
      </View>
      {open && (
        <View style={{ ...(direction() === 'rtl' ? { paddingRight: 46, paddingLeft: 8 } : { paddingLeft: 46, paddingRight: 8 }), paddingBottom: 4, gap: 1 }}>
          {hosts.slice(0, 30).map((h) => (
            <View key={h.remote} style={{ flexDirection: 'row', alignItems: 'center', gap: 6, height: 22 }}>
              <View style={{ width: 6, height: 6, borderRadius: 3, backgroundColor: h.scope === 'internet' ? t.accent : t.textDim }} />
              <Text style={{ flex: 1, fontSize: 11, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden', fontFamily: 'monospace' }}>{h.remote}</Text>
              <Text style={{ fontSize: 10, color: t.textDim, whiteSpace: 'nowrap' }}>{h.cc ? countryName(h.cc) : tr(`monitor.net.scope.${h.scope}`)}</Text>
            </View>
          ))}
          {!hosts.length && <Text style={{ fontSize: 11, color: t.textDim, height: 22 }}>{tr('monitor.net.onlyListening', { ports: g.listen.map((c) => c.lport).join(', ') })}</Text>}
          {hosts.length > 30 && <Text style={{ fontSize: 10, color: t.textDim }}>{tr('monitor.net.more', { count: hosts.length - 30 })}</Text>}
        </View>
      )}
    </View>
  );
}

// Barras de subida/bajada de los últimos 60 s (promedio por segundo).
function TrafficBars({ samples }) {
  const box = useRef(null);
  const [w, setW] = useState(0);
  useEffect(() => {
    if (!box.current) return;
    const nw = Math.round(lord.getLayout(box.current.id)[2]);
    if (nw > 0 && Math.abs(nw - w) > 1) setW(nw);
  });
  const pairs = [];
  for (let i = 0; i + 4 <= samples.length; i += 4) {
    const seg = samples.slice(i, i + 4);
    pairs.push([seg.reduce((a, m) => a + m.net.rx, 0) / 4, seg.reduce((a, m) => a + m.net.tx, 0) / 4]);
  }
  while (pairs.length < 60) pairs.unshift([0, 0]);
  const key = pairs.map((p) => `${Math.round(p[0])},${Math.round(p[1])}`).join(';');
  const pixels = useMemo(() => (w ? drawBars(canvas(w, 44), pairs, [C.rx, C.tx]) : null), [w, key]);
  return (
    <View ref={box} style={{ height: 44, direction: 'ltr' }}>
      {pixels && <Image pixels={pixels} style={{ width: w, height: 44 }} />}
    </View>
  );
}

// ---- lugares (países con conexiones) ----

function placesOf(groups, appKey) {
  const out = new Map();
  for (const g of groups) {
    if (appKey && g.key !== appKey) continue;
    for (const h of g.hosts.values()) {
      if (h.scope !== 'internet' || !h.cc) continue;
      const p = out.get(h.cc) || { cc: h.cc, count: 0, sent: 0, recv: 0, apps: new Map(), hosts: [] };
      p.count += h.count;
      p.sent += h.sent;
      p.recv += h.recv;
      p.apps.set(g.key, g);
      p.hosts.push({ ...h, app: g });
      out.set(h.cc, p);
    }
  }
  return [...out.values()].sort((x, y) => y.count - x.count);
}

// ---- columna derecha ----

function Section({ title, children }) {
  const t = useTheme();
  return (
    <View style={{ gap: 6 }}>
      <Text style={{ fontSize: 11, fontWeight: 600, color: t.textDim }}>{title}</Text>
      {children}
    </View>
  );
}

function KV({ k, v, mono, color }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', gap: 8, alignItems: 'flex-start' }}>
      <Text style={{ fontSize: 12, color: t.textDim, whiteSpace: 'nowrap' }}>{k}</Text>
      <Text style={{ flex: 1, fontSize: 12, fontWeight: 600, color: color || t.text, textAlign: 'end', fontFamily: mono ? 'monospace' : undefined, lineHeight: 1.35 }}>{v}</Text>
    </View>
  );
}

function Summary({ groups, conns, last, tr, fw }) {
  const t = useTheme();
  let fwAllowed = 0, fwDenied = 0;
  for (const v of Object.values(fw?.counts || {})) { fwAllowed += v[0] || 0; fwDenied += v[1] || 0; }
  const fwOn = !!fw?.status?.bpf;
  const all = conns?.list || [];
  const open = all.filter((c) => c.dir !== 'listen');
  const incoming = all.filter((c) => c.dir === 'in').length;
  const listening = all.filter((c) => c.dir === 'listen').length;
  const hosts = [];
  for (const g of groups) for (const h of g.hosts.values()) hosts.push({ ...h, app: g });
  hosts.sort((a, b) => b.sent + b.recv - (a.sent + a.recv));
  return (
    <>
      <Section title={tr('monitor.net.traffic')}>
        <KV k={`↓ ${tr('monitor.received')}`} v={fmt.rate(tr, last.net.rx)} color={C.rx} />
        <KV k={`↑ ${tr('monitor.sent')}`} v={fmt.rate(tr, last.net.tx)} color={C.tx} />
        <KV k={tr('monitor.net.sinceBoot')} v={`↓ ${formatBytes(last.net.rxTotal)} · ↑ ${formatBytes(last.net.txTotal)}`} />
      </Section>
      <Section title={tr('monitor.net.connections')}>
        <KV k={tr('monitor.net.allowed')} v={fwOn ? formatNumber(fwAllowed, 0) : formatNumber(open.length, 0)} />
        <KV k={tr('monitor.net.denied')} v={fwOn ? formatNumber(fwDenied, 0) : tr('firewall.off')} color={fwOn && fwDenied ? '#ff453a' : t.textDim} />
        <KV k={tr('monitor.net.incoming')} v={formatNumber(incoming, 0)} />
        <KV k={tr('monitor.net.listening')} v={formatNumber(listening, 0)} />
        {conns?.others ? <KV k={tr('monitor.net.others')} v={formatNumber(conns.others, 0)} color={t.textDim} /> : null}
        {fwOn && !fwDenied ? (
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8, marginTop: 4 }}>
            <Lumo pose="shield" size={40} />
            <Text style={{ flex: 1, fontSize: 11, color: t.textDim, lineHeight: 1.35 }}>{tr('monitor.net.allGood')}</Text>
          </View>
        ) : null}
      </Section>
      <Section title={tr('monitor.net.topApps')}>
        {groups.slice(0, 5).map((g) => (
          <View key={g.key} style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
            <AppGlyph item={g.item} size={14} />
            <Text style={{ flex: 1, fontSize: 12, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{g.key === 'lordos' ? tr('monitor.lordos') : g.name}</Text>
            <Text style={{ fontSize: 11, color: t.textDim }}>{formatBytes(g.sent + g.recv)}</Text>
          </View>
        ))}
        {!groups.length && <Text style={{ fontSize: 12, color: t.textDim }}>{tr('monitor.net.none')}</Text>}
      </Section>
      <Section title={tr('monitor.net.topHosts')}>
        {hosts.slice(0, 6).map((h) => (
          <View key={`${h.app.key}${h.remote}`} style={{ gap: 1 }}>
            <View style={{ flexDirection: 'row', gap: 6 }}>
              <Text style={{ flex: 1, fontSize: 11, color: t.text, fontFamily: 'monospace', whiteSpace: 'nowrap', overflow: 'hidden' }}>{h.remote}</Text>
              <Text style={{ fontSize: 11, color: t.textDim }}>{formatBytes(h.sent + h.recv)}</Text>
            </View>
            <Text style={{ fontSize: 10, color: t.textDim, whiteSpace: 'nowrap' }}>{`${h.cc ? countryName(h.cc) : tr(`monitor.net.scope.${h.scope}`)} · ${h.app.key === 'lordos' ? tr('monitor.lordos') : h.app.name}`}</Text>
          </View>
        ))}
        {!hosts.length && <Text style={{ fontSize: 12, color: t.textDim }}>{tr('monitor.net.none')}</Text>}
      </Section>
    </>
  );
}

function Detail({ g, tr, user }) {
  const t = useTheme();
  const ports = new Set(), local = new Set();
  for (const c of g.conns) {
    if (c.dir === 'listen') local.add(`${c.proto.toUpperCase()} ${c.lport}`);
    else ports.add(`${c.proto.toUpperCase()} ${c.rport}`);
  }
  const hosts = [...g.hosts.values()].sort((a, b) => b.sent + b.recv - (a.sent + a.recv));
  return (
    <>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
        <AppGlyph item={g.item} size={32} />
        <View style={{ flex: 1, minWidth: 0 }}>
          <Text style={{ fontSize: 15, fontWeight: 600, color: t.text }}>{g.key === 'lordos' ? tr('monitor.lordos') : g.name}</Text>
          <Text style={{ fontSize: 11, color: t.textDim }}>{tr('monitor.net.connCount', { count: g.conns.length - g.listen.length })}</Text>
        </View>
      </View>
      <Section title={tr('monitor.net.process')}>
        <Text style={{ fontSize: 12, color: t.textDim }}>{tr('monitor.net.path')}</Text>
        <Text style={{ fontSize: 11, color: t.text, fontFamily: 'monospace', lineHeight: 1.35 }}>{g.exe || '—'}</Text>
        <KV k={tr('monitor.user')} v={user || '—'} />
        <KV k={tr('monitor.pid')} v={[...g.pids].join(', ') || '—'} />
        <KV k={tr('monitor.net.rule')} v={tr(g.rule === 'deny' ? 'firewall.rule.deny' : g.rule === 'allow' ? 'firewall.rule.allow' : 'firewall.rule.none')}
          color={g.rule === 'deny' ? '#ff453a' : '#30d158'} />
        {g.allowed || g.denied ? <KV k={tr('firewall.counts')} v={tr('firewall.countsValue', { allowed: g.allowed || 0, denied: g.denied || 0 })} /> : null}
      </Section>
      <Section title={tr('monitor.net.data')}>
        <KV k={`↓ ${tr('monitor.received')}`} v={`${formatBytes(g.recv)} · ${fmt.rate(tr, g.rateRx)}`} color={C.rx} />
        <KV k={`↑ ${tr('monitor.sent')}`} v={`${formatBytes(g.sent)} · ${fmt.rate(tr, g.rateTx)}`} color={C.tx} />
      </Section>
      <Section title={tr('monitor.net.ports')}>
        <Text style={{ fontSize: 12, color: t.text, lineHeight: 1.4 }}>{[...ports].join(' · ') || '—'}</Text>
        {local.size ? <Text style={{ fontSize: 11, color: t.textDim, lineHeight: 1.4 }}>{tr('monitor.net.listensOn', { ports: [...local].join(' · ') })}</Text> : null}
      </Section>
      <Section title={tr('monitor.net.addresses')}>
        {hosts.slice(0, 10).map((h) => (
          <View key={h.remote} style={{ flexDirection: 'row', gap: 6 }}>
            <Text style={{ flex: 1, fontSize: 11, color: t.text, fontFamily: 'monospace', whiteSpace: 'nowrap', overflow: 'hidden' }}>{h.remote}</Text>
            <Text style={{ fontSize: 10, color: t.textDim, whiteSpace: 'nowrap' }}>{h.cc ? countryName(h.cc) : tr(`monitor.net.scope.${h.scope}`)}</Text>
          </View>
        ))}
        {!hosts.length && <Text style={{ fontSize: 12, color: t.textDim }}>—</Text>}
      </Section>
    </>
  );
}

function PlaceDetail({ p, tr }) {
  const t = useTheme();
  const hosts = [...p.hosts].sort((a, b) => b.sent + b.recv - (a.sent + a.recv));
  return (
    <>
      <View style={{ gap: 2 }}>
        <Text style={{ fontSize: 15, fontWeight: 600, color: t.text }}>{countryName(p.cc)}</Text>
        <Text style={{ fontSize: 11, color: t.textDim }}>{tr('monitor.net.connCount', { count: p.count })}</Text>
      </View>
      <Section title={tr('monitor.net.data')}>
        <KV k={`↓ ${tr('monitor.received')}`} v={formatBytes(p.recv)} color={C.rx} />
        <KV k={`↑ ${tr('monitor.sent')}`} v={formatBytes(p.sent)} color={C.tx} />
      </Section>
      <Section title={tr('monitor.net.placeApps')}>
        {[...p.apps.values()].map((g) => (
          <View key={g.key} style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
            <AppGlyph item={g.item} size={14} />
            <Text style={{ flex: 1, fontSize: 12, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{g.key === 'lordos' ? tr('monitor.lordos') : g.name}</Text>
          </View>
        ))}
      </Section>
      <Section title={tr('monitor.net.addresses')}>
        {hosts.slice(0, 14).map((h) => (
          <View key={`${h.app.key}${h.remote}`} style={{ gap: 1 }}>
            <View style={{ flexDirection: 'row', gap: 6 }}>
              <Text style={{ flex: 1, fontSize: 11, color: t.text, fontFamily: 'monospace', whiteSpace: 'nowrap', overflow: 'hidden' }}>{h.remote}</Text>
              <Text style={{ fontSize: 11, color: t.textDim }}>{formatBytes(h.sent + h.recv)}</Text>
            </View>
            <Text style={{ fontSize: 10, color: t.textDim, whiteSpace: 'nowrap' }}>{`${[...h.ports].join(' · ')} · ${h.app.key === 'lordos' ? tr('monitor.lordos') : h.app.name}`}</Text>
          </View>
        ))}
      </Section>
    </>
  );
}

// ---- pestaña ----

export function NetworkTab({ last, prefs }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const card = useCardStyle();
  const conns = useStore(monitorConnections);
  const fw = useStore(firewallState);
  const info = monitor.info();
  const sys = useSystemInfo(60000);
  const { apps: connApps } = useGroups(conns);
  const launcher = useLauncherApps();
  const apps = useMemo(() => withFirewall(connApps, fw, launcher), [connApps, fw, launcher]);
  const [q, setQ] = useState('');
  const [open, setOpen] = useState(() => new Set());
  const [selected, setSelected] = useState(null);       // app elegida (clave)
  const [place, setPlace] = useState(null);             // país elegido en el mapa
  const enabled = prefs.connections !== false;
  const needle = q.trim().toLowerCase();
  const shown = apps.filter((g) => !needle || `${g.name || tr('monitor.lordos')} ${g.exe || ''} ${[...g.hosts.keys()].join(' ')} ${[...g.hosts.values()].map((h) => countryName(h.cc)).join(' ')}`.toLowerCase().includes(needle));
  const maxBytes = Math.max(1, ...apps.map((g) => g.sent + g.recv));
  const sel = apps.find((g) => g.key === selected) || null;
  const places = placesOf(apps, sel ? sel.key : null);
  const selPlace = places.find((p) => p.cc === place) || null;
  const toggle = (key) => setOpen((s) => { const n = new Set(s); if (n.has(key)) n.delete(key); else n.add(key); return n; });
  const column = { borderRadius: 12, backgroundColor: card.bg, border: `1px solid ${t.separator}`, padding: 12 };
  return (
    <View style={{ flex: 1, flexDirection: 'row', gap: 12, paddingHorizontal: 20, paddingVertical: 16 }}>
      {/* left */}
      <View style={{ ...column, width: 320, gap: 8, padding: 10 }}>
        <SearchField value={q} onChange={setQ} placeholder={tr('monitor.net.search')} />
        <View style={{ flexDirection: 'row', paddingHorizontal: 8 }}>
          <Text style={{ flex: 1, fontSize: 10, fontWeight: 600, color: t.textDim }}>{tr('monitor.net.apps', { count: apps.length })}</Text>
          <Text style={{ width: 54, fontSize: 10, fontWeight: 600, color: t.textDim, textAlign: 'end' }}>{tr('monitor.net.dataCol')}</Text>
          <Text style={{ width: 34, fontSize: 10, fontWeight: 600, color: t.textDim, textAlign: 'end' }}>{tr('monitor.net.ruleCol')}</Text>
        </View>
        <ScrollView style={{ flex: 1 }}>
          {!enabled && <Text style={{ fontSize: 12, color: t.textDim, padding: 8, lineHeight: 1.4 }}>{tr('monitor.net.disabled')}</Text>}
          {enabled && !conns && <Text style={{ fontSize: 12, color: t.textDim, padding: 8 }}>{tr('monitor.loading')}</Text>}
          {enabled && conns && !conns.ok && <Text style={{ fontSize: 12, color: '#ff9f0a', padding: 8 }}>{tr('monitor.net.noDiag')}</Text>}
          {shown.map((g) => (
            <AppRow key={g.key} g={g} tr={tr} open={open.has(g.key)} selected={selected === g.key} maxBytes={maxBytes}
              onToggle={() => toggle(g.key)} onSelect={() => { setPlace(null); setSelected(selected === g.key ? null : g.key); }} />
          ))}
          {enabled && conns && !shown.length && <Text style={{ fontSize: 12, color: t.textDim, padding: 8 }}>{tr('monitor.net.none')}</Text>}
        </ScrollView>
        <View style={{ height: 1, backgroundColor: t.separator }} />
        <View style={{ flexDirection: 'row', gap: 14, paddingHorizontal: 4 }}>
          <View style={{ flex: 1 }}>
            <Text style={{ fontSize: 10, color: t.textDim }}>{`↓ ${tr('monitor.received')}`}</Text>
            <Text style={{ fontSize: 14, fontWeight: 600, color: C.rx }}>{fmt.rate(tr, last.net.rx)}</Text>
          </View>
          <View style={{ flex: 1 }}>
            <Text style={{ fontSize: 10, color: t.textDim }}>{`↑ ${tr('monitor.sent')}`}</Text>
            <Text style={{ fontSize: 14, fontWeight: 600, color: C.tx }}>{fmt.rate(tr, last.net.tx)}</Text>
          </View>
        </View>
        <TrafficBarsFromStore />
        <View style={{ flexDirection: 'row', paddingHorizontal: 2 }}>
          <Text style={{ flex: 1, fontSize: 9, color: t.textDim }}>{tr('monitor.agoSeconds', { n: 60 })}</Text>
          <Text style={{ fontSize: 9, color: t.textDim }}>{tr('monitor.now')}</Text>
        </View>
      </View>
      {/* centro: el mapa ocupa toda la altura */}
      <View style={{ ...column, flex: 1, minWidth: 0, gap: 8, padding: 10 }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8, paddingHorizontal: 2 }}>
          <Glyph name="globe" color={t.accent} />
          <Text style={{ flex: 1, fontSize: 12, fontWeight: 600, color: t.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>
            {(sel ? tr('monitor.net.mapOf', { app: sel.key === 'lordos' ? tr('monitor.lordos') : sel.name }) : tr('monitor.net.map')) + ' · ' +
              tr('monitor.net.mapLegend', { countries: places.length, count: places.reduce((a, p) => a + p.count, 0) })}
          </Text>
          {(sel || selPlace) && <View onClick={() => { setSelected(null); setPlace(null); }}><Text style={{ fontSize: 11, color: t.accent }}>{tr('monitor.net.showAll')}</Text></View>}
        </View>
        <MapView places={places} home={info?.home} selected={place} onSelect={setPlace} fitKey={selected || ''} geoOn={prefs.geo !== false && info?.geo} />
        <View style={{ flexDirection: 'row', gap: 8, paddingHorizontal: 2 }}>
          <Text style={{ flex: 1, fontSize: 9, color: t.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{tr('monitor.net.privacyShort')}</Text>
          <View onClick={() => wm.open('settings', { args: { section: 'privacy', nonce: Date.now() } })}><Text style={{ fontSize: 9, color: t.accent, whiteSpace: 'nowrap' }}>{tr('monitor.net.privacyLink')}</Text></View>
        </View>
      </View>
      {/* derecha */}
      <View style={{ ...column, width: 270 }}>
        <ScrollView style={{ flex: 1 }}>
          <View style={{ gap: 16 }}>
            {selPlace ? <PlaceDetail p={selPlace} tr={tr} /> : sel ? <Detail g={sel} tr={tr} user={sys.user} /> : <Summary groups={apps} conns={conns} last={last} tr={tr} fw={fw} />}
          </View>
        </ScrollView>
      </View>
    </View>
  );
}

function TrafficBarsFromStore() {
  const samples = useMonitor({ everyMs: 1000 });
  return <TrafficBars samples={samples} />;
}

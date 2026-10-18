// Firewall.jsx — Ajustes → Privacidad → "Red de las apps" (se registra con registerPrivacySection,
// como el ítem del Monitor). Ver docs/FIREWALL.md, paso 4.
//   - estado de lord-netperm (el servicio que aplica las reglas en el kernel) y el modo;
//   - Apps confiables: se permiten sin anotar (las denegaciones explícitas igual valen);
//   - Apps restringidas: una casilla por restricción (sin internet = "solo LAN", sin LAN, sin red,
//     sin conexiones entrantes, y las que no son de red: cámara, micrófono…).
// Las reglas las guarda lord-server (lord.fw); esta pantalla no escribe archivos.
import { View, Text, Button, Segmented, ScrollView, useTheme, useLocale, useStore, useLauncherApps, firewall, firewallState } from '@lordos/sdk';
import { useState } from 'react';

// id de firewall de una app del lanzador: las internas corren dentro de LordOS.
const fwId = (a) => (a.kind === 'linux' ? a.id : a.kind === 'package' ? `lpk:${a.id}` : 'lordos');

function useAppIndex() {
  const list = useLauncherApps();
  const byId = new Map();
  for (const a of list) {
    const id = fwId(a);
    if (id !== 'lordos' && !byId.has(id)) byId.set(id, a);
  }
  return byId;
}

function AppLabel({ id, apps, tr }) {
  const t = useTheme();
  const a = apps.get(id);
  const Icon = a?.Icon;
  return (
    <View style={{ flex: 1, flexDirection: 'row', alignItems: 'center', gap: 8, minWidth: 0 }}>
      {Icon ? <Icon size={20} /> : <View style={{ width: 20, height: 20, borderRadius: 5, backgroundColor: t.control }} />}
      <View style={{ flex: 1, minWidth: 0 }}>
        <Text style={{ fontSize: 13, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{id === 'lordos' ? tr('monitor.lordos') : a?.name || id.replace(/^(desktop|lpk):/, '')}</Text>
        <Text style={{ fontSize: 10, color: t.textDim, whiteSpace: 'nowrap', overflow: 'hidden', fontFamily: 'monospace' }}>{id}</Text>
      </View>
    </View>
  );
}

// Lista desplegable para agregar una app que todavía no está en ninguna de las dos listas.
function AddApp({ apps, exclude, onPick, tr }) {
  const t = useTheme();
  const [open, setOpen] = useState(false);
  const choices = ['lordos', ...apps.keys()].filter((id) => !exclude.has(id));
  return (
    <View style={{ gap: 4 }}>
      <View style={{ flexDirection: 'row' }}>
        <Button label={open ? tr('firewall.close') : tr('firewall.add')} onClick={() => setOpen(!open)} />
      </View>
      {open && (
        <View style={{ height: 180, borderRadius: 8, border: `1px solid ${t.separator}` }}>
          <ScrollView style={{ flex: 1 }}>
            {choices.map((id) => (
              <View key={id} onClick={() => { setOpen(false); onPick(id); }} style={{ flexDirection: 'row', paddingHorizontal: 8, paddingVertical: 4 }}>
                <AppLabel id={id} apps={apps} tr={tr} />
              </View>
            ))}
            {!choices.length && <Text style={{ fontSize: 12, color: t.textDim, padding: 8 }}>{tr('firewall.noMoreApps')}</Text>}
          </ScrollView>
        </View>
      )}
    </View>
  );
}

function Check({ on, label, onChange }) {
  const t = useTheme();
  return (
    <View onClick={() => onChange(!on)} style={{ flexDirection: 'row', alignItems: 'center', gap: 5, paddingVertical: 2, paddingHorizontal: 6, borderRadius: 6,
      backgroundColor: on ? 'rgba(255,69,58,0.18)' : t.control }}>
      <View style={{ width: 12, height: 12, borderRadius: 3, border: `1px solid ${on ? '#ff453a' : t.textDim}`, backgroundColor: on ? '#ff453a' : 'transparent',
        alignItems: 'center', justifyContent: 'center' }}>
        {on ? <Text style={{ fontSize: 9, color: '#fff', fontWeight: 700 }}>✓</Text> : null}
      </View>
      <Text style={{ fontSize: 11, color: on ? t.text : t.textDim }}>{label}</Text>
    </View>
  );
}

export function FirewallPrivacy() {
  const t = useTheme();
  const { t: tr } = useLocale();
  useStore(firewallState);                        // se redibuja al cambiar una regla
  const apps = useAppIndex();
  if (!firewall.available()) return null;
  const st = firewall.state();
  const status = firewall.status() || {};
  const trusted = st.trusted || [];
  const restricted = st.restricted || {};
  const inLists = new Set([...trusted, ...Object.keys(restricted)]);
  const statusText = !status.daemon ? tr('firewall.status.noDaemon') : !status.bpf ? tr('firewall.status.noBpf', { error: status.error || '' }) :
    tr('firewall.status.on', { dns: tr(`firewall.dns.${status.dns || 'none'}`) });
  const setFlag = (id, flag, on) => {
    const cur = restricted[id] || [];
    const next = on ? [...cur, flag] : cur.filter((f) => f !== flag);
    firewall.setRestricted(id, next.length ? next : []);
  };
  return (
    <View style={{ gap: 10, marginTop: 18 }}>
      <Text style={{ fontSize: 14, fontWeight: 700, color: t.text }}>{tr('firewall.title')}</Text>
      <Text style={{ fontSize: 11, color: status.daemon && status.bpf ? t.textDim : '#ff9f0a', lineHeight: 1.4 }}>{statusText}</Text>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 12 }}>
        <View style={{ flex: 1 }}>
          <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{tr('firewall.mode')}</Text>
          <Text style={{ fontSize: 11, color: t.textDim, marginTop: 3, lineHeight: 1.35 }}>{tr(st.mode === 'off' ? 'firewall.mode.offHint' : 'firewall.mode.silentHint')}</Text>
        </View>
        <Segmented value={st.mode === 'off' ? 'off' : 'silent'} onChange={(m) => firewall.setMode(m)}
          options={[['silent', tr('firewall.mode.silent')], ['off', tr('firewall.mode.off')]]} />
      </View>

      <View style={{ height: 1, backgroundColor: t.separator }} />
      <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{tr('firewall.trusted')}</Text>
      <Text style={{ fontSize: 11, color: t.textDim, lineHeight: 1.35 }}>{tr('firewall.trustedHint')}</Text>
      {trusted.map((id) => (
        <View key={id} style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
          <AppLabel id={id} apps={apps} tr={tr} />
          <Button label={tr('firewall.remove')} onClick={() => firewall.setTrusted(id, false)} />
        </View>
      ))}
      {!trusted.length && <Text style={{ fontSize: 12, color: t.textDim }}>{tr('firewall.none')}</Text>}
      <AddApp apps={apps} exclude={inLists} tr={tr} onPick={(id) => firewall.setTrusted(id, true)} />

      <View style={{ height: 1, backgroundColor: t.separator }} />
      <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{tr('firewall.restricted')}</Text>
      <Text style={{ fontSize: 11, color: t.textDim, lineHeight: 1.35 }}>{tr('firewall.restrictedHint')}</Text>
      {Object.keys(restricted).sort().map((id) => (
        <View key={id} style={{ gap: 6, paddingVertical: 4 }}>
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
            <AppLabel id={id} apps={apps} tr={tr} />
            <Button label={tr('firewall.remove')} onClick={() => firewall.setRestricted(id, [])} />
          </View>
          <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 6, paddingHorizontal: 28 }}>
            {firewall.RESTRICTIONS.map((f) => (
              <Check key={f} on={restricted[id].includes(f)} label={tr(`firewall.r.${f}`)} onChange={(on) => setFlag(id, f, on)} />
            ))}
          </View>
        </View>
      ))}
      {!Object.keys(restricted).length && <Text style={{ fontSize: 12, color: t.textDim }}>{tr('firewall.none')}</Text>}
      <AddApp apps={apps} exclude={inLists} tr={tr} onPick={(id) => firewall.setRestricted(id, ['no-internet'])} />
      <Text style={{ fontSize: 11, color: t.textDim, lineHeight: 1.45 }}>{tr('firewall.footer')}</Text>
    </View>
  );
}

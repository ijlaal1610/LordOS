// Ajustes → Privacidad — permisos por app, como «Privacidad y seguridad» de macOS. Lista cada
// permiso con las apps que lo pidieron y deja revocarlo. El Monitor agrega su propia sección
// por registerPrivacySection (ver sdk/privacy.js); esta página no conoce al Monitor.
import { View, Text, useTheme, useLocale, Segmented, ScrollView, useStore, perms, permsRev, privacySections, useIndicators, packages, appName } from '@lordos/sdk';
import { getApp } from '@lordos/sdk';

// Permisos que se muestran con su glifo y color (el orden es el de la página).
const SHOWN = [
  ['camera', '📷', '#30d158'], ['microphone', '🎙', '#ff9f0a'], ['screen', '▦', '#0a84ff'],
  ['files', '📂', '#64d2ff'], ['network', '⇅', '#0a84ff'], ['clipboard', '⎘', '#5e5ce6'],
  ['notifications', '🔔', '#ff375f'], ['apps', '▦', '#8e8e93'], ['listen', '🛜', '#34c759'],
  ['usb', '⇆', '#8e8e93'], ['bluetooth', '✴', '#0a84ff'],
];

function Badge({ glyph, color }) {
  return (
    <View style={{ width: 22, height: 22, borderRadius: 6, backgroundColor: color, alignItems: 'center', justifyContent: 'center' }}>
      <Text style={{ fontSize: 12, lineHeight: 22 }}>{glyph}</Text>
    </View>
  );
}

// Apps que declararon un permiso (manifest) o que ya tienen una decisión guardada.
function appsFor(perm, decisions) {
  const set = new Map();
  for (const p of packages.list()) if ((p.permissions || []).includes(perm)) set.set(p.id, p.name);
  for (const d of decisions) if (d.perm === perm) set.set(d.app, appName(getApp(d.app)) || d.app);
  return [...set.entries()];
}

function PermSection({ perm, glyph, color }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  useStore(permsRev);
  const decisions = perms.list();
  const apps = appsFor(perm, decisions);
  if (!apps.length) return null;
  return (
    <View style={{ gap: 2, paddingVertical: 10 }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10, marginBottom: 6 }}>
        <Badge glyph={glyph} color={color} />
        <Text style={{ fontSize: 14, fontWeight: 700, color: t.text }}>{tr(`privacy.perm.${perm}`)}</Text>
      </View>
      <Text style={{ fontSize: 11, color: t.textDim, marginBottom: 8, marginInlineStart: 32 }}>{tr(`privacy.permHint.${perm}`)}</Text>
      {apps.map(([id, name]) => {
        const state = perms.get(id, perm);
        return (
          <View key={id} style={{ flexDirection: 'row', alignItems: 'center', gap: 12, paddingVertical: 6, marginInlineStart: 32 }}>
            <Text style={{ flex: 1, fontSize: 13, color: t.text }}>{name}</Text>
            <Segmented value={state} onChange={(s) => perms.set(id, perm, s)}
              options={[['allow', tr('privacy.allowShort')], ['ask', tr('privacy.askShort')], ['deny', tr('privacy.denyShort')]]} />
          </View>
        );
      })}
    </View>
  );
}

function InUse() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const list = useIndicators();
  if (!list.length) return null;
  return (
    <View style={{ borderRadius: 10, padding: 12, marginBottom: 12, backgroundColor: t.hover, gap: 6 }}>
      {list.map((d) => (
        <View key={d.kind} style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
          <View style={{ width: 9, height: 9, borderRadius: 5, backgroundColor: d.kind === 'camera' ? '#30d158' : '#ff9f0a' }} />
          <Text style={{ fontSize: 12, color: t.text }}>{tr(`privacy.inUse.${d.kind}`)}: {d.apps.join(', ')}</Text>
        </View>
      ))}
    </View>
  );
}

export function PrivacySettings() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const sections = useStore(privacySections);
  return (
    <ScrollView>
      <Text style={{ fontSize: 12, color: t.textDim, marginBottom: 12, lineHeight: 1.4 }}>{tr('privacy.intro')}</Text>
      <InUse />
      {/* huecos registrados por otras partes (p. ej. el Monitor: su interruptor on/off) */}
      {sections.map((s) => (
        <View key={s.id} style={{ marginBottom: 12 }}><s.Component /></View>
      ))}
      {SHOWN.map(([perm, glyph, color]) => <PermSection key={perm} perm={perm} glyph={glyph} color={color} />)}
    </ScrollView>
  );
}

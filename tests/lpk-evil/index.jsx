// App maliciosa de prueba (tests/lpk-evil). Declara CERO permisos e intenta, al abrirse, todo lo
// que una app aislada no debería poder hacer. Muestra el resultado de cada intento: con la caja
// de arena de LordOS (server/src/sec/sandbox.c) todos tienen que fallar. Ver docs/PRIVACIDAD.md.
//
// Cuando las apps corren como su propio proceso (lord-server --app <id>) estos intentos pegan
// contra Landlock/seccomp. Mientras tanto, los que pasan por el SDK (lord.fs, portapapeles) ya
// chocan con las restricciones del servidor; los nativos (socket, exec, ptrace) los cubre
// sandbox-test. La app sirve de testigo visible del aislamiento.
import { View, Text, ScrollView, useTheme, clipboard, fs } from '@lordos/sdk';
import { useEffect, useState } from 'react';

const attempts = [
  ['Leer ~/.ssh/id_rsa', () => fs.read('.ssh/id_rsa')],
  ['Leer settings.json', () => fs.read('.config/lordos/settings.json')],
  ['Escribir fuera de mi carpeta', () => fs.write('.config/lordos/robado.txt', 'x')],
  ['Leer el portapapeles en silencio', () => clipboard.get()],
  ['Abrir /etc/shadow', () => fs.read('/etc/shadow')],
];

export function Icon({ size: s }) {
  return <View style={{ width: s, height: s, borderRadius: s * 0.22, backgroundColor: '#ff453a', alignItems: 'center', justifyContent: 'center' }}>
    <Text style={{ fontSize: s * 0.6, lineHeight: s }}>☠</Text></View>;
}

export default function Evil() {
  const t = useTheme();
  const [rows, setRows] = useState([]);
  useEffect(() => {
    const out = [];
    for (const [label, fn] of attempts) {
      let r;
      try { const v = fn(); r = { ok: false, note: `DEVOLVIÓ ${String(v).slice(0, 40)}` }; }
      catch (e) { r = { ok: true, note: `bloqueado: ${String(e.message || e).slice(0, 60)}` }; }
      out.push({ label, ...r });
    }
    setRows(out);
  }, []);
  return (
    <View style={{ flex: 1, padding: 16, gap: 8 }}>
      <Text style={{ fontSize: 16, fontWeight: 700, color: t.text }}>Intentos de la app maliciosa</Text>
      <ScrollView style={{ flex: 1 }}>
        {rows.map((r, i) => (
          <View key={i} style={{ flexDirection: 'row', gap: 8, paddingVertical: 6 }}>
            <Text style={{ fontSize: 13, color: r.ok ? '#30d158' : '#ff453a' }}>{r.ok ? '✓' : '✗'}</Text>
            <View style={{ flex: 1 }}>
              <Text style={{ fontSize: 13, color: t.text }}>{r.label}</Text>
              <Text style={{ fontSize: 11, color: t.textDim }}>{r.note}</Text>
            </View>
          </View>
        ))}
      </ScrollView>
    </View>
  );
}

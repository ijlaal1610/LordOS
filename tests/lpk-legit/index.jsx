// App legítima de prueba (tests/lpk-legit): pide el micrófono y archivos, y funciona después del
// consentimiento. Sus textos van con t() y sus catálogos (locales/<idioma>.json del paquete), así
// sirve también para probar el idioma y la dirección (RTL) en vivo dentro del proceso aislado.
// Ver docs/PRIVACIDAD.md.
import { View, Text, Button, useTheme, useLocale, useAppWindow, requestPermission, markDeviceUse, onPermissionRevoked, openFile, wm, micStart, micStop, micLevel } from '@lordos/sdk';
import { useState, useEffect } from 'react';

export function Icon({ size: s }) {
  return <View style={{ width: s, height: s, borderRadius: s * 0.22, backgroundColor: '#ff9f0a', alignItems: 'center', justifyContent: 'center' }}>
    <Text style={{ fontSize: s * 0.6, lineHeight: s }}>🎙</Text></View>;
}

export default function Recorder() {
  const th = useTheme();
  const { t } = useLocale();
  const { id } = useAppWindow();
  const [rec, setRec] = useState(false);
  const [status, setStatus] = useState('');
  const [level, setLevel] = useState(0);
  useEffect(() => {
    if (!rec) return undefined;
    const h = setInterval(() => setLevel(micLevel()), 100);
    return () => clearInterval(h);
  }, [rec]);
  useEffect(() => () => markDeviceUse(id, 'microphone', false), [id]);
  useEffect(() => onPermissionRevoked((perm) => { if (perm === 'microphone') { micStop(); setRec(false); setLevel(0); setStatus('stopped'); } }), []);
  useEffect(() => {
    wm.setMenus(id, [{ label: t('grabadora.menu'), items: [
      { label: t('grabadora.record'), onClick: toggle },
      { label: t('grabadora.attach'), onClick: attach },
      '-',
      { label: t('grabadora.about'), onClick: () => setStatus('about') },
    ] }]);
  });

  const toggle = async () => {
    if (rec) { setRec(false); micStop(); setLevel(0); markDeviceUse(id, 'microphone', false); setStatus('stopped'); return; }
    const ok = await requestPermission(id, 'microphone');
    if (!ok) { setStatus('noMic'); return; }
    micStart();
    setRec(true);
    markDeviceUse(id, 'microphone', true);
    setStatus('recording');
  };
  const [attached, setAttached] = useState('');
  const attach = async () => {
    const f = await openFile();                 // el diálogo lo dibuja el sistema; la app recibe solo el fd
    if (!f) { setStatus('canceled'); return; }
    setAttached(`${f.name} · ${f.arrayBuffer().byteLength} B`);
    setStatus('attached');
  };
  return (
    <View style={{ flex: 1, padding: 20, gap: 14, alignItems: 'center', justifyContent: 'center' }}>
      <Text style={{ fontSize: 48 }}>{rec ? '🔴' : '🎙'}</Text>
      {rec ? <View style={{ width: 240, height: 10, borderRadius: 5, backgroundColor: th.hover, overflow: 'hidden' }}>
        <View style={{ width: `${Math.min(100, Math.round(level * 400))}%`, height: '100%', backgroundColor: '#30d158' }} />
      </View> : null}
      <View style={{ flexDirection: 'row', gap: 10 }}>
        <Button primary label={rec ? t('grabadora.stop') : t('grabadora.record')} onClick={toggle} />
        <Button label={t('grabadora.attach')} onClick={attach} />
      </View>
      <Text style={{ fontSize: 12, color: th.textDim, textAlign: 'center' }}>{status ? t(`grabadora.${status}`, { name: attached }) : ''}</Text>
    </View>
  );
}

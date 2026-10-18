// Generator.jsx — generador de contraseñas y frases de contraseña (configurable). Genera en
// lord-vault (getrandom; frases con la lista de 7776 palabras de la EFF) y muestra la fuerza.
import { View, Text, useTheme, useStore, Button, Toggle, Segmented, Slider, Stepper, TextInput, t } from '@lordos/sdk';
import { useEffect, useState } from 'react';
import { app, req, copySecret, setMeta } from './api.js';
import { StrengthBar, Sheet, Label } from './ui.jsx';

export const GEN_DEFAULTS = { mode: 'password', length: 20, upper: true, lower: true, digits: true, symbols: true, noAmbiguous: false,
  words: 5, sep: '-', capitalize: true, addDigit: true };

function Opt({ label, children }) {
  const th = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', minHeight: 30 }}>
      <Text style={{ flex: 1, fontSize: 13, color: th.text }}>{label}</Text>
      {children}
    </View>
  );
}

export function GeneratorPanel({ onUse, compact }) {
  const th = useTheme();
  const meta = useStore(app, (s) => s.meta);
  const [o, setO] = useState({ ...GEN_DEFAULTS, ...(meta?.gen || {}) });
  const [res, setRes] = useState(null);
  const set = (patch) => setO((x) => ({ ...x, ...patch }));
  const gen = async () => {
    const args = o.mode === 'words' ? { words: o.words, sep: o.sep, capitalize: o.capitalize, addDigit: o.addDigit }
      : { length: o.length, upper: o.upper, lower: o.lower, digits: o.digits, symbols: o.symbols, noAmbiguous: o.noAmbiguous };
    try { setRes(await req('generate', args)); } catch (e) { setRes(null); }
  };
  useEffect(() => { gen(); }, [JSON.stringify(o)]);
  const save = () => setMeta({ gen: o }).catch(() => {});
  return (
    <View style={{ gap: 10 }}>
      <View style={{ padding: 14, borderRadius: 10, backgroundColor: th.control, minHeight: 64, justifyContent: 'center', direction: 'ltr' }}>
        <Text style={{ fontSize: compact ? 15 : 18, fontFamily: 'monospace', color: th.text, lineHeight: 24 }}>{res?.password || ''}</Text>
      </View>
      {res && <StrengthBar score={res.score} bits={res.bits} />}
      <View style={{ flexDirection: 'row', gap: 8 }}>
        <Button label={t('passwords.gen.again')} onClick={gen} />
        <Button label={t('passwords.gen.copy')} onClick={() => res && copySecret(res.password, t('passwords.gen.password'))} />
        {onUse && <Button primary label={t('passwords.gen.use')} onClick={() => { if (res) { save(); onUse(res.password); } }} />}
      </View>
      <Segmented value={o.mode} onChange={(mode) => set({ mode })} options={[['password', t('passwords.gen.modePassword')], ['words', t('passwords.gen.modeWords')]]} />
      {o.mode === 'password' ? (
        <View style={{ gap: 4 }}>
          <Opt label={t('passwords.gen.length', { n: o.length })}><Slider value={o.length} min={8} max={64} onChange={(v) => set({ length: Math.round(v) })} /></Opt>
          <Opt label={t('passwords.gen.upper')}><Toggle on={o.upper} onChange={(upper) => set({ upper })} /></Opt>
          <Opt label={t('passwords.gen.lower')}><Toggle on={o.lower} onChange={(lower) => set({ lower })} /></Opt>
          <Opt label={t('passwords.gen.digits')}><Toggle on={o.digits} onChange={(digits) => set({ digits })} /></Opt>
          <Opt label={t('passwords.gen.symbols')}><Toggle on={o.symbols} onChange={(symbols) => set({ symbols })} /></Opt>
          <Opt label={t('passwords.gen.noAmbiguous')}><Toggle on={o.noAmbiguous} onChange={(noAmbiguous) => set({ noAmbiguous })} /></Opt>
        </View>
      ) : (
        <View style={{ gap: 4 }}>
          <Opt label={t('passwords.gen.words')}><Stepper value={o.words} min={3} max={12} onChange={(words) => set({ words })} /></Opt>
          <Opt label={t('passwords.gen.sep')}><TextInput value={o.sep} onChange={(sep) => set({ sep: sep.slice(0, 3) })} style={{ width: 70 }} /></Opt>
          <Opt label={t('passwords.gen.capitalize')}><Toggle on={o.capitalize} onChange={(capitalize) => set({ capitalize })} /></Opt>
          <Opt label={t('passwords.gen.addDigit')}><Toggle on={o.addDigit} onChange={(addDigit) => set({ addDigit })} /></Opt>
          <Label>{t('passwords.gen.wordsHint')}</Label>
        </View>
      )}
    </View>
  );
}

export function GeneratorSheet({ onClose, onUse }) {
  return (
    <Sheet title={t('passwords.gen.title')} onClose={onClose} width={440}>
      <GeneratorPanel onUse={onUse ? (p) => { onUse(p); onClose(); } : null} />
    </Sheet>
  );
}

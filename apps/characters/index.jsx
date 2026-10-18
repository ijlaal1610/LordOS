// Emojis y símbolos — la versión en ventana del selector del sistema (❖.): emojis por categoría
// con búsqueda en español, inglés y portugués, tonos de piel y recientes, más los símbolos de
// Unicode por bloque. Un clic elige; «Copiar» lo deja en el portapapeles.
import { View, Text, TextInput, Button, Segmented, ScrollView, useTheme, clipboard, emoji, EmojiGrid, EmojiToneBar, t as tr } from '@lordos/sdk';
import { useMemo, useState } from 'react';

export function Icon({ size: s }) {
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(160deg, #5ac8fa, #5856d6)',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)`, overflow: 'hidden' }}>
      {/* carita propia (círculo y rasgos) y un símbolo */}
      <View style={{ position: 'absolute', left: s * 0.14, top: s * 0.16, width: s * 0.5, height: s * 0.5, borderRadius: s * 0.25, backgroundColor: '#ffd60a' }} />
      <View style={{ position: 'absolute', left: s * 0.27, top: s * 0.3, width: s * 0.06, height: s * 0.09, borderRadius: s * 0.03, backgroundColor: '#3a2a00' }} />
      <View style={{ position: 'absolute', left: s * 0.45, top: s * 0.3, width: s * 0.06, height: s * 0.09, borderRadius: s * 0.03, backgroundColor: '#3a2a00' }} />
      <View style={{ position: 'absolute', left: s * 0.27, top: s * 0.47, width: s * 0.24, height: s * 0.07, borderRadius: s * 0.04, backgroundColor: '#3a2a00' }} />
      <Text style={{ position: 'absolute', left: s * 0.56, top: s * 0.46, fontSize: s * 0.36, fontWeight: 700, color: '#ffffff' }}>Ω</Text>
    </View>
  );
}

// bloques de símbolos (nombre en characters.block.<id>)
const SYMBOL_BLOCKS = [
  ['arrows', 0x2190, 0x21ff], ['math', 0x2200, 0x22ff], ['technical', 0x2300, 0x23ff], ['currency', 0x20a0, 0x20c0], ['letterlike', 0x2100, 0x214f],
  ['numberForms', 0x2150, 0x218b], ['punctuation', 0x2010, 0x205e], ['superSub', 0x2070, 0x209c], ['enclosed', 0x2460, 0x24ff], ['boxDrawing', 0x2500, 0x257f],
  ['blockElements', 0x2580, 0x259f], ['shapes', 0x25a0, 0x25ff], ['miscSymbols', 0x2600, 0x26ff], ['dingbats', 0x2700, 0x27bf], ['suppArrows', 0x27f0, 0x27ff],
  ['braille', 0x2800, 0x28ff], ['greek', 0x391, 0x3c9], ['latin1', 0xa1, 0xff], ['cjkSymbols', 0x3000, 0x303f], ['halfwidth', 0xff01, 0xff5e],
];

function Preview({ ch, th }) {
  if (!ch) return <View style={{ width: 220 }} />;
  const cps = [...ch].map((c) => `U+${c.codePointAt(0).toString(16).toUpperCase().padStart(4, '0')}`).join(' ');
  const name = emoji.name(ch);
  return (
    <View style={{ width: 220, alignItems: 'center', gap: 10, padding: 16, backgroundColor: th.sidebarBg }}>
      <View style={{ width: 140, height: 140, borderRadius: 16, alignItems: 'center', justifyContent: 'center', backgroundColor: th.control }}>
        <Text style={{ fontSize: 80, color: th.text, lineHeight: 120 }}>{ch}</Text>
      </View>
      {name ? <Text style={{ fontSize: 13, fontWeight: 600, color: th.text, textAlign: 'center' }}>{name}</Text> : null}
      <Text style={{ fontSize: 11, color: th.textDim, textAlign: 'center' }}>{cps}</Text>
      <Button primary label={tr('characters.copy')} onClick={() => clipboard.set(ch)} />
    </View>
  );
}

export default function Characters() {
  const th = useTheme();
  const [tab, setTab] = useState('emoji');
  const [q, setQ] = useState('');
  const [group, setGroup] = useState(() => (emoji.recents().length ? 'recent' : 'smileys'));
  const [block, setBlock] = useState(0);
  const [tone, setTone] = useState(emoji.tone());
  const [pick, setPick] = useState(null);
  const groups = emoji.groups();
  const list = useMemo(() => (q.trim() ? emoji.search(q, 400) : group === 'recent' ? emoji.recents() : groups.find((g) => g.id === group)?.list || []), [q, group]);
  const chars = useMemo(() => {
    const [, a, b] = SYMBOL_BLOCKS[block];
    const out = [];
    for (let c = a; c <= b; c++) out.push(String.fromCodePoint(c));
    return out;
  }, [block]);
  const choose = (ch) => { setPick(ch); if (tab === 'emoji') emoji.remember(ch.replace(/[\u{1F3FB}-\u{1F3FF}]/gu, '')); };
  const side = tab === 'emoji'
    ? [['recent', tr('emoji.group.recent')], ...groups.map((g) => [g.id, tr(`emoji.group.${g.id}`)])]
    : SYMBOL_BLOCKS.map((b, i) => [i, tr(`characters.block.${b[0]}`)]);
  return (
    <View style={{ flex: 1 }}>
      <View style={{ height: 44, flexDirection: 'row', alignItems: 'center', gap: 10, paddingHorizontal: 12 }}>
        <Segmented value={tab} onChange={(v) => { setTab(v); setPick(null); }} options={[['emoji', tr('characters.tab.emoji')], ['symbols', tr('characters.tab.symbols')]]} />
        <View style={{ flex: 1 }} />
        {tab === 'emoji' && <EmojiToneBar tone={tone} onTone={(n) => { setTone(n); emoji.setTone(n); }} th={th} />}
        {tab === 'emoji' && <TextInput value={q} onChange={setQ} placeholder={tr('emoji.search')} onCancel={() => setQ('')} style={{ width: 200, height: 26 }} />}
      </View>
      <View style={{ height: 1, backgroundColor: th.separator }} />
      <View style={{ flex: 1, flexDirection: 'row' }}>
        <View style={{ width: 180, backgroundColor: th.sidebarBg, padding: 6 }}>
          <ScrollView style={{ flex: 1 }}>
            {side.map(([id, label]) => {
              const on = tab === 'emoji' ? (!q.trim() && group === id) : block === id;
              return (
                <View key={id} onClick={() => { if (tab === 'emoji') { setQ(''); setGroup(id); } else setBlock(id); }}
                  style={{ height: 26, paddingHorizontal: 8, flexDirection: 'row', alignItems: 'center', gap: 8, borderRadius: 6, backgroundColor: on ? th.accent : 'transparent' }}>
                  <Text style={{ fontSize: 13, color: on ? '#fff' : th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{label}</Text>
                </View>
              );
            })}
          </ScrollView>
        </View>
        <View style={{ flex: 1, padding: 10, direction: 'ltr' }}>
          {tab === 'emoji' ? (
            list.length ? <ScrollView style={{ flex: 1 }}><EmojiGrid list={list} sel={-1} onPick={choose} tone={tone} size={44} th={th} /></ScrollView>
              : <Text style={{ margin: 20, fontSize: 12, color: th.textDim, textAlign: 'center' }}>{group === 'recent' && !q.trim() ? tr('emoji.noRecent') : tr('emoji.none')}</Text>
          ) : (
            <ScrollView style={{ flex: 1 }}>
              <View style={{ flexDirection: 'row', flexWrap: 'wrap' }}>
                {chars.map((ch) => (
                  <View key={ch} onClick={() => choose(ch)} style={{ width: 40, height: 40, alignItems: 'center', justifyContent: 'center', borderRadius: 6, backgroundColor: pick === ch ? th.accent : 'transparent' }}>
                    <Text style={{ fontSize: 20, color: pick === ch ? '#fff' : th.text }}>{ch}</Text>
                  </View>
                ))}
              </View>
            </ScrollView>
          )}
        </View>
        <Preview ch={pick} th={th} />
      </View>
    </View>
  );
}

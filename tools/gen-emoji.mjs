// gen-emoji.mjs — arma los datos del selector de emojis de LordOS a partir de Unicode y CLDR.
//
//   node tools/gen-emoji.mjs <carpeta>
//
// La carpeta tiene que tener (se bajan una vez, no van en el repositorio):
//   emoji-test.txt                       https://unicode.org/Public/emoji/15.1/emoji-test.txt
//   ann-{en,es,pt}.json, der-{en,es,pt}.json   cldr-json 45: cldr-annotations-full y
//                                        cldr-annotations-derived-full (annotations.json)
// Escribe:
//   sdk/emojidata.js                     los emojis por categoría (sin textos) y cuáles admiten tono de piel
//   assets/locales/{en,es,pt}/emojikw.json   nombre y palabras clave de cada emoji por idioma
// Licencia de los datos: Unicode License v3 (Unicode y CLDR), ver docs/DOCUMENTOS.md.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const src = process.argv[2];
if (!src) { console.error('uso: node tools/gen-emoji.mjs <carpeta con emoji-test.txt y las anotaciones de CLDR>'); process.exit(1); }

const GROUP_ID = {
  'Smileys & Emotion': 'smileys', 'People & Body': 'people', 'Animals & Nature': 'animals', 'Food & Drink': 'food',
  'Travel & Places': 'travel', Activities: 'activities', Objects: 'objects', Symbols: 'symbols', Flags: 'flags',
};
const TONES = ['1f3fb', '1f3fc', '1f3fd', '1f3fe', '1f3ff'];

const lines = fs.readFileSync(path.join(src, 'emoji-test.txt'), 'utf8').split('\n');
let group = null;
const groups = new Map();
const all = new Set();
const toned = new Set();          // base (sin tono) que tiene variantes con tono
for (const line of lines) {
  const g = /^# group: (.+)$/.exec(line);
  if (g) { group = GROUP_ID[g[1]] || null; continue; }
  const m = /^([0-9A-F ]+?)\s*; fully-qualified\s*#\s*(\S+)/.exec(line);
  if (!m || !group) continue;
  const cps = m[1].toLowerCase().split(' ');
  const tone = cps.findIndex((c) => TONES.includes(c));
  if (tone >= 0) {
    // variante con tono: se marca la base (sin ningún tono) y no se lista aparte
    const base = cps.filter((c) => !TONES.includes(c));
    const ch = String.fromCodePoint(...base.map((h) => parseInt(h, 16)));
    if (all.has(ch)) toned.add(ch);
    else {
      // la base puede llevar FE0F que la variante no tiene
      const alt = String.fromCodePoint(...[base[0], 'fe0f', ...base.slice(1)].map((h) => parseInt(h, 16)));
      if (all.has(alt)) toned.add(alt);
    }
    continue;
  }
  const ch = String.fromCodePoint(...cps.map((h) => parseInt(h, 16)));
  if (!groups.has(group)) groups.set(group, []);
  groups.get(group).push(ch);
  all.add(ch);
}

const key = (ch) => [...ch].map((c) => c.codePointAt(0).toString(16)).filter((h) => h !== 'fe0f').join('_');

let js = '// emojidata.js — generado por tools/gen-emoji.mjs desde Unicode emoji-test.txt 15.1 (no editar).\n';
js += '// Por categoría, en el orden de Unicode. TONED: los que admiten tono de piel (se les agrega U+1F3FB…U+1F3FF).\n';
js += 'export const EMOJI_GROUPS = [\n';
for (const [g, list] of groups) js += `  [${JSON.stringify(g)}, ${JSON.stringify(list.join(' '))}],\n`;
js += '];\n';
js += `export const TONED = new Set(${JSON.stringify([...toned].join(' '))}.split(' '));\n`;
fs.writeFileSync(path.join(root, 'sdk/emojidata.js'), js);

for (const lang of ['en', 'es', 'pt']) {
  const ann = { ...JSON.parse(fs.readFileSync(path.join(src, `der-${lang}.json`), 'utf8')).annotationsDerived.annotations,
    ...JSON.parse(fs.readFileSync(path.join(src, `ann-${lang}.json`), 'utf8')).annotations.annotations };
  const enAnn = lang === 'en' ? ann : { ...JSON.parse(fs.readFileSync(path.join(src, 'der-en.json'), 'utf8')).annotationsDerived.annotations,
    ...JSON.parse(fs.readFileSync(path.join(src, 'ann-en.json'), 'utf8')).annotations.annotations };
  const out = {};
  for (const ch of all) {
    const bare = ch.replace(/️/g, '');
    const a = ann[ch] || ann[bare] || enAnn[ch] || enAnn[bare];
    if (!a) continue;
    const name = (a.tts || [])[0] || '';
    const kws = (a.default || []).filter((k) => k !== name);
    out[key(ch)] = [name, ...kws].join(' | ');
  }
  // las tres lenguas con las mismas claves (i18n-check): lo que falta, en inglés
  fs.writeFileSync(path.join(root, `assets/locales/${lang}/emojikw.json`), `${JSON.stringify(out, null, 0).replace(/","/g, '",\n"')}\n`);
  console.log(`${lang}: ${Object.keys(out).length} emojis`);
}
console.log(`grupos: ${[...groups].map(([g, l]) => `${g} ${l.length}`).join(', ')}; con tono: ${toned.size}`);

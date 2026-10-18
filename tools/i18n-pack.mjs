// i18n-pack.mjs — arma un paquete de idioma de LordOS (un solo archivo JSON).
//
//   node tools/i18n-pack.mjs new fr "Français" French one-other fr_FR
//        → lordos-lang-fr.json con todas las claves en inglés para traducir
//   node tools/i18n-pack.mjs export pt
//        → lordos-lang-pt.json con el idioma pt tal como está
//
// El paquete se instala en LordOS copiándolo a ~/Descargas y usando Ajustes → Idioma y
// región → "Agregar"; queda en ~/.local/share/lordos/locales/<id>/. Reglas de plural
// disponibles: one-other, zero-one-other, none, east-slavic, polish, czech-slovak, arabic
// (ver sdk/i18n.js). La dirección ("rtl" para árabe, hebreo, persa, urdu…) sale del id.
// Revisarlo con: node tools/i18n-check.mjs lordos-lang-fr.json
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const dir = path.join(root, 'assets/locales');
const read = (id) => {
  const cats = {};
  let meta = {};
  for (const f of fs.readdirSync(path.join(dir, id))) {
    const obj = JSON.parse(fs.readFileSync(path.join(dir, id, f), 'utf8'));
    if (f === 'meta.json') meta = obj; else cats[f.slice(0, -5)] = obj;
  }
  return { meta, catalogs: cats };
};

const [cmd, id, name, english, plural = 'one-other', region = ''] = process.argv.slice(2);
if (!/^[a-z]{2,3}(-[A-Za-z]{2,4})?$/.test(id || '')) {
  console.error('uso: i18n-pack.mjs new <id> "<nombre>" <nombre en inglés> [plural] [región]  |  export <id>');
  process.exit(2);
}
let pack;
if (cmd === 'export') pack = read(id);
else if (cmd === 'new') {
  const en = read('en');
  // Idiomas que se escriben de derecha a izquierda: la interfaz se da vuelta entera.
  const RTL = ['ar', 'he', 'fa', 'ur', 'yi', 'ps', 'dv', 'sd', 'ug', 'ckb', 'syr'];
  const direction = RTL.includes(id.split('-')[0]) ? 'rtl' : 'ltr';
  pack = { meta: { id, name: name || id, english: english || id, plural, region, direction }, catalogs: en.catalogs };
} else { console.error('comando desconocido'); process.exit(2); }
const out = `lordos-lang-${id}.json`;
fs.writeFileSync(out, `${JSON.stringify(pack, null, 2)}\n`);
console.log(`${out}: ${Object.values(pack.catalogs).reduce((n, c) => n + Object.keys(c).length, 0)} textos`);

// i18n-check.mjs — revisa los catálogos de idioma de LordOS.
//
//   node tools/i18n-check.mjs            todos los idiomas de assets/locales
//   node tools/i18n-check.mjs pt         solo uno (o un paquete: ruta a lordos-lang-xx.json)
//
// Informa: claves que se usan en el código (t('espacio.clave')) y no están en el catálogo de
// referencia (en), claves que le faltan o le sobran a cada idioma respecto de en, y
// variables {x} que no coinciden. Sale con código 1 si falta algo.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const dir = path.join(root, 'assets/locales');
const REF = 'en';

function loadLang(id) {
  if (id.endsWith('.json') && fs.existsSync(id)) {
    const pack = JSON.parse(fs.readFileSync(id, 'utf8'));
    const { format, ...cats } = pack.catalogs;      // format.json no son textos
    return { id: pack.meta.id, cats };
  }
  const cats = {};
  for (const f of fs.readdirSync(path.join(dir, id))) {
    if (f.endsWith('.json') && f !== 'meta.json' && f !== 'format.json') cats[f.slice(0, -5)] = JSON.parse(fs.readFileSync(path.join(dir, id, f), 'utf8'));
  }
  return { id, cats };
}

const flat = (cats) => Object.fromEntries(Object.entries(cats).flatMap(([ns, o]) => Object.entries(o).map(([k, v]) => [`${ns}.${k}`, v])));
const vars = (v) => [...new Set((typeof v === 'object' ? Object.values(v).join(' ') : String(v)).match(/\{\w+\}/g) || [])].sort().join(',');

// claves usadas en el código
function usedKeys() {
  const out = new Map();
  const walk = (d) => {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) { if (!['node_modules', 'assets'].includes(e.name)) walk(p); continue; }
      if (!/\.(jsx?|mjs)$/.test(e.name) || e.name.endsWith('.bundle.js')) continue;
      const src = fs.readFileSync(p, 'utf8').replace(/^\s*\/\/.*$/gm, '');
      for (const m of src.matchAll(/\b(?:t|T|tr)\(\s*'([a-z][\w-]*\.[\w.-]+)'/g)) out.set(m[1], path.relative(root, p));
    }
  };
  for (const d of ['apps', 'themes', 'sdk']) walk(path.join(root, d));
  return out;
}

const ref = flat(loadLang(REF).cats);
let bad = 0;
for (const [k, file] of usedKeys()) if (!(k in ref)) { console.log(`✗ ${k} (usada en ${file}) no está en ${REF}`); bad++; }

const langs = process.argv[2] ? [process.argv[2]] : fs.readdirSync(dir).filter((d) => d !== REF && fs.existsSync(path.join(dir, d, 'meta.json')));
for (const id of langs) {
  const l = loadLang(id);
  const cur = flat(l.cats);
  const missing = Object.keys(ref).filter((k) => !(k in cur));
  const extra = Object.keys(cur).filter((k) => !(k in ref));
  const varDiff = Object.keys(ref).filter((k) => k in cur && vars(ref[k]) !== vars(cur[k]));
  console.log(`${l.id}: ${Object.keys(cur).length}/${Object.keys(ref).length} claves` +
    `${missing.length ? `, faltan ${missing.length}` : ''}${extra.length ? `, sobran ${extra.length}` : ''}${varDiff.length ? `, variables distintas en ${varDiff.length}` : ''}`);
  for (const k of missing) console.log(`  falta   ${k}`);
  for (const k of extra) console.log(`  sobra   ${k}`);
  for (const k of varDiff) console.log(`  vars    ${k}: ${vars(ref[k])} ≠ ${vars(cur[k])}`);
  bad += missing.length + varDiff.length;
}
process.exit(bad ? 1 : 0);

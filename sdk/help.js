// help.js — artículos de la app Ayuda (assets/help/<idioma>/<id>.md) y el recorrido de bienvenida.
//
//   help.list()            → [{ id, title, app, section, order, keywords }] en el idioma de la interfaz
//   help.read(id)          → { id, title, app, section, keywords, body } o null
//   help.open(id)          → abre Ayuda en ese artículo
//   help.search(texto)     → [{ id, title, score, snippet }]
//   help.startTour()       → abre «Bienvenido a LordOS»
//
// Cada artículo es markdown con un encabezado:
//   ---
//   title: Reloj
//   app: clock              (la app que explica, para el botón «Abrir»)
//   section: apps           (grupo en la barra lateral: start, desktop, apps, system, settings)
//   order: 10
//   keywords: alarma, temporizador
//   ---
// Los enlaces especiales del texto: [Abrir Ajustes → Red](settings:network), [Archivos](app:files)
// y [otro artículo](help:spotlight). Spotlight indexa los artículos (registerSpotlightSource).
// Los lee lord.help (server/src/ui/js_help.c), solo de la carpeta assets/help de LordOS.
import { locale, t } from './i18n.js';
import { settings } from './settings.js';
import { wm, getApp, isLiveSystem } from './windows.js';
import { registerSpotlightSource } from './spotlight.js';

const has = () => typeof lord.help !== 'undefined';
const langOf = () => String(locale.get().lang || 'en').split('-')[0];
const cache = new Map();                    // 'es' → { list, bodies: Map }

function parse(txt) {
  const out = { body: txt, meta: {} };
  const m = /^---\n([\s\S]*?)\n---\n?/.exec(txt);
  if (!m) return out;
  for (const line of m[1].split('\n')) {
    const i = line.indexOf(':');
    if (i > 0) out.meta[line.slice(0, i).trim()] = line.slice(i + 1).trim();
  }
  out.body = txt.slice(m[0].length);
  return out;
}

function langData(lang) {
  if (cache.has(lang)) return cache.get(lang);
  let list = [];
  try { list = has() ? lord.help.list(lang) : []; } catch (e) { list = []; }
  const d = { list: list.sort((a, b) => (a.order || 0) - (b.order || 0) || a.title.localeCompare(b.title)), bodies: new Map() };
  cache.set(lang, d);
  return d;
}

// el idioma de la interfaz; si no tiene artículos, inglés
function current() {
  const l = langOf();
  const d = langData(l);
  return d.list.length ? [l, d] : ['en', langData('en')];
}

const fold = (s) => String(s || '').toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g, '');

export const help = {
  available: has,
  list: () => current()[1].list,
  read(id) {
    const [lang, d] = current();
    if (d.bodies.has(id)) return d.bodies.get(id);
    let txt = null;
    try { txt = has() ? lord.help.read(lang, id) : null; } catch (e) { txt = null; }
    if (txt == null) return null;
    const p = parse(txt);
    const a = { id, title: p.meta.title || id, app: p.meta.app || '', section: p.meta.section || '', keywords: p.meta.keywords || '', body: p.body };
    d.bodies.set(id, a);
    return a;
  },
  open(id) { wm.open('help', { args: { article: id } }); },
  startTour() { wm.open('help', { args: { tour: true } }); },
  search(query) {
    const q = fold(query).trim();
    if (q.length < 2) return [];
    const out = [];
    for (const it of help.list()) {
      const a = help.read(it.id);
      if (!a) continue;
      const title = fold(a.title), kw = fold(a.keywords), body = fold(a.body);
      let score = title.startsWith(q) ? 90 : title.includes(q) ? 70 : kw.includes(q) ? 55 : 0;
      const at = body.indexOf(q);
      if (!score && at >= 0) score = 30;
      if (!score) continue;
      let snippet = '';
      if (at >= 0) {
        const raw = a.body.replace(/[#*`>[\]()_]/g, '').replace(/\s+/g, ' ');
        const k = fold(raw).indexOf(q);
        let from = Math.max(0, k - 60);
        while (from > 0 && from < k && raw[from - 1] !== ' ') from++;     // empieza en una palabra entera
        snippet = `${from > 0 ? '…' : ''}${raw.slice(from, k + 140).trim()}`;
      }
      out.push({ id: a.id, title: a.title, score, snippet: snippet || a.body.replace(/^#.*$/m, '').replace(/\s+/g, ' ').trim().slice(0, 180) });
    }
    return out.sort((x, y) => y.score - x.score);
  },
  // Qué hacer con un enlace del texto. Devuelve true si lo atendió.
  link(url) {
    const u = String(url || '');
    let m;
    if ((m = /^settings:([a-z]+)$/.exec(u))) { wm.open('settings', { args: { section: m[1] } }); return true; }
    if ((m = /^app:([a-z0-9-]+)$/.exec(u))) { if (getApp(m[1])) wm.open(m[1]); return true; }
    if ((m = /^help:([a-z0-9-]+)$/.exec(u))) { help.open(m[1]); return true; }
    return false;
  },
};

// Spotlight: los artículos de Ayuda
registerSpotlightSource('help', (q, query) => {
  if (!getApp('help') || q.length < 2) return [];
  return help.search(query).slice(0, 4).map((r) => ({
    key: `help:${r.id}`, score: r.score * 0.6, title: r.title, subtitle: t('shell.spotlight.inHelp'), glyph: '?',
    preview: { kind: 'text', text: r.snippet }, run: () => help.open(r.id),
  }));
});

// Primer ingreso: «Bienvenido a LordOS», una sola vez (settings.welcomeShown). En el live lo que
// se abre es el instalador.
export function maybeStartTour() {
  if (settings.get().welcomeShown || isLiveSystem() || !getApp('help') || !has()) return;
  settings.set({ welcomeShown: true });
  setTimeout(() => help.startTour(), 1200);
}

// al cambiar el idioma, se vuelven a leer
locale.subscribe(() => cache.clear());

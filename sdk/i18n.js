// i18n.js — idioma y región de LordOS para temas y apps.
//
//   t('terminal.menu.newTab')                 → "Nueva pestaña"
//   t('files.items', { count: 3 })            → "3 elementos" (plural según el idioma)
//   t('about.version', { v: '0.5.0' })        → "Versión 0.5.0"
//   formatDate(d, 'short') / formatTime(d) / formatNumber(1234.5)
//   direction() → 'rtl' | 'ltr'      textDirection('مرحبا') → 'rtl'
//
// Los textos están en catálogos JSON que el servidor lee en tiempo de ejecución:
//   assets/locales/<idioma>/<espacio>.json            (incluidos con LordOS)
//   ~/.local/share/lordos/locales/<idioma>/<espacio>.json   (paquetes de idioma del usuario)
// El espacio es la primera parte de la clave ('terminal', 'files', 'shell'…). Un idioma
// nuevo es una carpeta con esos archivos y un meta.json; no hace falta tocar código.
// Si falta una clave se usa el inglés y, si tampoco está, la clave misma.
//
// No hay Intl en QuickJS: plurales y formatos de fecha/número salen de reglas y datos
// propios (format.json de cada idioma).
import { createStore, useStore } from './store.js';
import { settings } from './settings.js';

export const FALLBACK = 'en';

// Reglas de plural (CLDR, simplificadas a enteros). Un idioma elige la suya en meta.json.
const PLURALS = {
  'one-other': (n) => (n === 1 ? 'one' : 'other'),                   // en, es, de, it, nl, sv…
  'zero-one-other': (n) => (n === 0 || n === 1 ? 'one' : 'other'),   // pt, fr
  none: () => 'other',                                               // ja, zh, ko, th, vi, id
  'east-slavic': (n) => {                                            // ru, uk, be
    const m10 = n % 10, m100 = n % 100;
    if (m10 === 1 && m100 !== 11) return 'one';
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return 'few';
    return 'many';
  },
  polish: (n) => {
    const m10 = n % 10, m100 = n % 100;
    if (n === 1) return 'one';
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return 'few';
    return 'many';
  },
  'czech-slovak': (n) => (n === 1 ? 'one' : n >= 2 && n <= 4 ? 'few' : 'other'),
  arabic: (n) => {
    const m100 = n % 100;
    if (n === 0) return 'zero';
    if (n === 1) return 'one';
    if (n === 2) return 'two';
    if (m100 >= 3 && m100 <= 10) return 'few';
    if (m100 >= 11) return 'many';
    return 'other';
  },
};

const cache = new Map();          // 'es/terminal' → objeto plano
const metas = new Map();          // 'es' → meta.json

function catalog(lang, ns) {
  const k = `${lang}/${ns}`;
  if (cache.has(k)) return cache.get(k);
  let obj = null;
  try {
    const txt = lord.locale.catalog(lang, ns);
    obj = txt ? JSON.parse(txt) : null;
  } catch (e) { console.warn(`i18n: catálogo ${k} inválido:`, e.message); }
  cache.set(k, obj);
  return obj;
}

// Idiomas disponibles: [{ id, name (en su idioma), english, plural, user }]
export function languages() {
  const out = [];
  for (const l of lord.locale.languages()) {
    let m = {};
    try { m = JSON.parse(l.meta); } catch (e) { continue; }
    metas.set(l.id, m);
    if (!out.some((x) => x.id === l.id)) out.push({ id: l.id, name: m.name || l.id, english: m.english || '', plural: m.plural, region: m.region, user: l.user });
  }
  return out.sort((a, b) => a.name.localeCompare(b.name));
}

function meta(lang) {
  if (!metas.has(lang)) languages();
  return metas.get(lang) || {};
}

// Idioma inicial: el elegido en Ajustes, o el de la región del usuario (LANG=pt_BR.UTF-8 → pt).
function initial() {
  const s = settings.get().language;
  if (s) return s;
  try {
    const l = lord.locale.get();
    const lang = (l.user.LANG || l.system.LANG || '').split(/[_.@]/)[0];
    if (lang && lang !== 'C' && lord.locale.catalog(lang, 'shell')) return lang;
  } catch (e) { /* sin región */ }
  return 'es';
}

export const locale = createStore({ lang: initial(), region: settings.get().region || '' });

// El servidor elige la fuente CJK según el idioma (un mismo ideograma se dibuja distinto en
// chino, japonés y coreano) y, si el idioma es de derecha a izquierda (meta.json "direction": "rtl"),
// da vuelta la interfaz: filas, alineación inicial, menús (como en macOS).
function applyNative({ lang }) {
  try {
    lord.text.setLanguage(lang);
    lord.text.setDirection(meta(lang).direction === 'rtl');
  } catch (e) { /* servidor sin lord.text */ }
}
applyNative(locale.get());
locale.subscribe(() => applyNative(locale.get()));

// Dirección de la interfaz ('rtl' en árabe, hebreo, persa…) y de un texto suelto (la de su
// primer carácter fuerte: un nombre de archivo en árabe dentro de una interfaz en inglés).
export const direction = () => (meta(locale.get().lang).direction === 'rtl' ? 'rtl' : 'ltr');
export const textDirection = (s) => { try { return lord.text.direction(String(s ?? '')); } catch (e) { return 'ltr'; } };

settings.subscribe(() => {
  const s = settings.get();
  const cur = locale.get();
  if ((s.language && s.language !== cur.lang) || (s.region || '') !== cur.region)
    locale.set({ lang: s.language || cur.lang, region: s.region || '' });
});

function lookup(lang, key) {
  const dot = key.indexOf('.');
  if (dot < 0) return undefined;
  const c = catalog(lang, key.slice(0, dot));
  return c ? c[key.slice(dot + 1)] : undefined;
}

const fill = (str, vars) => String(str).replace(/\{(\w+)\}/g, (m, k) => (vars && vars[k] != null ? String(vars[k]) : m));

export function t(key, vars) {
  const { lang } = locale.get();
  let v = lookup(lang, key);
  if (v === undefined && lang !== FALLBACK) v = lookup(FALLBACK, key);
  if (v === undefined) return fill(key, vars);
  if (v && typeof v === 'object') {
    const n = Math.abs(Number(vars?.count ?? 0));
    const rule = PLURALS[meta(lang).plural] || PLURALS['one-other'];
    v = v[rule(n)] ?? v.other ?? Object.values(v)[0];
  }
  return fill(v, vars);
}

// Traducción si existe, o null (para textos con respaldo propio, ej. theme.json).
export function tOptional(key, vars) {
  const { lang } = locale.get();
  const v = lookup(lang, key) ?? lookup(FALLBACK, key);
  return v === undefined ? null : t(key, vars);
}

// Nombre de una app (manifest.json → apps.<id> del catálogo, si existe).
export const appName = (app) => (app ? (lookup(locale.get().lang, `apps.${app.id}`) ?? app.name) : '');

// ---- formatos (fecha, hora, números) ----

const clock24 = () => settings.get().clock?.hour24 || 'auto';

// Como en macOS: nombres de días y meses en el idioma de la interfaz; orden de la fecha,
// separadores y reloj de 12/24 h según la región (es_AR, pt_BR, en_US…).
function formats() {
  const { lang, region } = locale.get();
  const names = catalog(lang, 'format') || catalog(FALLBACK, 'format') || {};
  const rlang = region ? region.split('_')[0] : lang;
  const rbase = catalog(rlang, 'format') || names;
  const r = { ...rbase, ...(rbase.regions?.[region] || {}) };
  const own = rlang === lang;
  return {
    ...names,
    date: { ...(names.date || {}), numeric: r.date?.numeric || names.date?.numeric, ...(own ? r.date : {}) },
    // Settings → Date & Time can force a 24-hour or 12-hour clock over the region's
    decimal: r.decimal, group: r.group, hour12: clock24() === '24' ? false : clock24() === '12' ? true : r.hour12,
    am: own ? r.am : names.am || r.am, pm: own ? r.pm : names.pm || r.pm,
  };
}

const pad = (n) => String(n).padStart(2, '0');

// estilo: 'short' (jue 15 oct), 'long' (jueves, 15 de octubre de 2026), 'numeric' (15/10/2026)
export function formatDate(d, style = 'short') {
  const f = formats();
  const day = d.getDate(), month = d.getMonth(), year = d.getFullYear(), wd = d.getDay();
  const pattern = f.date?.[style] || '{dd}/{mm}/{yyyy}';
  return fill(pattern, {
    weekday: f.weekdays?.[wd] ?? wd, wd: f.weekdaysShort?.[wd] ?? wd, day, dd: pad(day),
    month: f.months?.[month] ?? month + 1, mon: f.monthsShort?.[month] ?? month + 1, mm: pad(month + 1), yyyy: year,
  });
}

// Weekday name (0 = Sunday) in the interface language: weekdayName(1) → "Mon".
export function weekdayName(wd, short = true) {
  const f = formats();
  return (short ? f.weekdaysShort : f.weekdays)?.[wd] ?? String(wd);
}

export function formatTime(d, { seconds = false } = {}) {
  const f = formats();
  let h = d.getHours();
  const m = pad(d.getMinutes()), s = seconds ? `:${pad(d.getSeconds())}` : '';
  if (f.hour12) {
    const ap = h < 12 ? f.am || 'AM' : f.pm || 'PM';
    h = h % 12 || 12;
    return `${h}:${m}${s} ${ap}`;
  }
  return `${pad(h)}:${m}${s}`;
}

export function formatNumber(n, digits = null) {
  const f = formats();
  const dec = f.decimal || '.', group = f.group ?? ',';
  const fixed = digits == null ? String(n) : Number(n).toFixed(digits);
  const [i, frac] = fixed.split('.');
  const neg = i.startsWith('-');
  const int = (neg ? i.slice(1) : i).replace(/\B(?=(\d{3})+(?!\d))/g, group);
  return `${neg ? '-' : ''}${int}${frac ? dec + frac : ''}`;
}

// Hook: re-renderiza cuando cambia el idioma o la región.
export function useLocale() {
  const l = useStore(locale);
  return { ...l, dir: direction(), t, formatDate, formatTime, formatNumber };
}

export const setLanguage = (lang) => settings.set({ language: lang });

// Después de instalar o editar un paquete de idioma.
export function reloadCatalogs() {
  cache.clear();
  metas.clear();
  locale.set((l) => ({ ...l }));
}

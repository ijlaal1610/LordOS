// emoji.jsx — emojis y símbolos para todo el sistema.
//
//   ❖. (o Ctrl+❖Espacio) abre un selector chico donde está el cursor: búsqueda por palabras en
//   español, inglés y portugués (CLDR), categorías, recientes y tono de piel. Elegir uno lo escribe
//   en el campo con foco por el mismo camino que el método de entrada (una tecla que escribe ese
//   texto); en una app Linux lo pega con el portapapeles; si no hay campo, lo copia.
//   La app «Emojis y símbolos» (apps/characters) muestra lo mismo en una ventana, más los bloques de
//   símbolos de Unicode.
//
//   emoji.groups() → [{ id, list: [carácter…] }]   emoji.search(texto) → [carácter…]
//   emoji.name(carácter) → nombre en el idioma de la interfaz
//   emoji.withTone(carácter, tono 0..5)             emoji.insert(texto)   emoji.open() / close()
import { createElement as h, useEffect, useMemo, useRef, useState } from 'react';
import { View, Text, addListener, screen } from '@lordos/runtime';
import { createStore, useStore } from './store.js';
import { settings } from './settings.js';
import { useTheme } from './theme.js';
import { t, locale } from './i18n.js';
import { ime } from './ime.js';
import { TextInput, ScrollView, focusToken, restoreFocus } from './components.jsx';
import { clipboard, toast } from './clipboard.js';
import { wm } from './windows.js';
import { EMOJI_GROUPS, TONED } from './emojidata.js';

const TONE_CP = [null, 0x1f3fb, 0x1f3fc, 0x1f3fd, 0x1f3fe, 0x1f3ff];
let groupsCache = null;
const groups = () => groupsCache || (groupsCache = EMOJI_GROUPS.map(([id, s]) => ({ id, list: s.split(' ') })));
const keyOf = (ch) => [...ch].map((c) => c.codePointAt(0).toString(16)).filter((x) => x !== 'fe0f').join('_');

// palabras clave: las del idioma de la interfaz primero, y las de los otros dos para buscar
const kwCache = new Map();
function keywords(lang) {
  if (!kwCache.has(lang)) {
    let cat = {};
    try { const txt = lord.locale.catalog(lang, 'emojikw'); cat = txt ? JSON.parse(txt) : {}; } catch (e) { cat = {}; }
    const norm = {};
    for (const [k, v] of Object.entries(cat)) norm[k] = v;
    kwCache.set(lang, norm);
  }
  return kwCache.get(lang);
}
const fold = (s) => String(s).toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g, '');
let index = null;              // [{ ch, text }] con todas las palabras plegadas
function buildIndex() {
  if (index) return index;
  const langs = ['es', 'en', 'pt'];
  const cats = langs.map(keywords);
  index = [];
  for (const g of groups()) for (const ch of g.list) {
    const k = keyOf(ch);
    index.push({ ch, text: fold(cats.map((c) => c[k] || '').join(' | ')) });
  }
  return index;
}

const uiLang = () => String(locale.get().lang || 'es').split(/[-_]/)[0];

export const emoji = {
  groups,
  name(ch) {
    const k = keyOf(ch.replace(/[\u{1F3FB}-\u{1F3FF}]/gu, ''));      // sin el tono de piel
    const own = keywords(uiLang())[k] || keywords('en')[k] || '';
    return own.split(' | ')[0];
  },
  search(q, limit = 200) {
    const words = fold(q).split(/\s+/).filter(Boolean);
    if (!words.length) return [];
    const out = [];
    // primero los que empiezan una palabra con lo escrito, después los que lo contienen
    const scored = [];
    for (const e of buildIndex()) {
      if (!words.every((w) => e.text.includes(w))) continue;
      const starts = words.every((w) => e.text.startsWith(w) || e.text.includes(` ${w}`) || e.text.includes(`|${w}`));
      scored.push([starts ? 0 : 1, e.ch]);
    }
    scored.sort((a, b) => a[0] - b[0]);
    for (const [, ch] of scored) { out.push(ch); if (out.length >= limit) break; }
    return out;
  },
  toned: (ch) => TONED.has(ch),
  withTone(ch, tone) {
    if (!tone || !TONED.has(ch)) return ch;
    const cps = [...ch].map((c) => c.codePointAt(0));
    const rest = cps.slice(1).filter((c, i) => !(i === 0 && c === 0xfe0f));
    return String.fromCodePoint(cps[0], TONE_CP[tone], ...rest);
  },
  recents: () => settings.get().emojiRecent || [],
  remember(ch) {
    const list = [ch, ...(settings.get().emojiRecent || []).filter((x) => x !== ch)].slice(0, 32);
    settings.set({ emojiRecent: list });
  },
  tone: () => settings.get().emojiTone || 0,
  setTone: (n) => settings.set({ emojiTone: n }),
  // Escribe el texto donde estaba el foco antes de abrir el selector.
  insert(text, to = target) {
    const tok = to?.text;
    if (tok) restoreFocus(tok);
    setTimeout(() => {
      if (to?.text || to?.ime) {
        // igual que lo que confirma un método de entrada (js.c ime_commit): una tecla con ese texto
        globalThis.__lordKey(0, 1, text, text, '', 0);
        globalThis.__lordKey(0, 0, text, text, '', 0);
        return;
      }
      if (to?.linux && typeof lord.wayland !== 'undefined' && lord.wayland.hasKeyboard()) {
        clipboard.set(text);
        if (lord.wayland.paste()) return;
      }
      clipboard.set(text);
      toast.set(t('emoji.copied', { text }));
    }, 0);
  },
  open: () => openPanel(),
  close: () => panel.set(false),
};

// ---- el selector ----

export const panel = createStore(false);
let target = null;
let superDown = false;
function openPanel() {
  const top = wm.focused();
  target = { text: focusToken(), ime: ime.owner(), linux: !!(top && String(top).startsWith('linux#')), caret: ime.caret() };
  panel.set(true);
}
addListener('keydown', (e) => {
  if (e.code === 125 || e.code === 126) { superDown = true; return; }
  const meta = superDown || e.meta;
  if (meta && !e.repeat && ((e.code === 52 && !e.shift && !e.alt) || (e.code === 57 && e.ctrl))) {
    if (panel.get()) panel.set(false); else openPanel();
  } else if (e.key === 'Escape' && panel.get()) panel.set(false);
});
addListener('keyup', (e) => { if (e.code === 125 || e.code === 126) superDown = false; });

const GROUP_ICON = { recent: '🕘', smileys: '😀', people: '👋', animals: '🐻', food: '🍔', travel: '🚗', activities: '⚽', objects: '💡', symbols: '❤️', flags: '🏁' };
const COLS = 9, CELL = 36;

export function Grid({ list, sel, onPick, onHover, tone, size = CELL, th }) {
  const rows = [];
  for (let i = 0; i < list.length; i += COLS) rows.push(list.slice(i, i + COLS));
  return h(View, { style: { gap: 0 } }, rows.map((r, ri) => h(View, { key: ri, style: { flexDirection: 'row' } },
    r.map((ch, ci) => {
      const i = ri * COLS + ci;
      const shown = emoji.withTone(ch, tone);
      return h(View, {
        key: ch, onClick: () => onPick(shown), onMouseEnter: () => onHover?.(i),
        style: { width: size, height: size, borderRadius: 7, alignItems: 'center', justifyContent: 'center', backgroundColor: sel === i ? th.accent + '55' : 'transparent' },
      }, h(Text, { style: { fontSize: Math.round(size * 0.62), lineHeight: size } }, shown));
    }))));
}

export function ToneBar({ tone, onTone, th }) {
  return h(View, { style: { flexDirection: 'row', gap: 4 } }, TONE_CP.map((cp, i) => h(View, {
    key: i, onClick: () => onTone(i),
    style: { width: 22, height: 22, borderRadius: 11, alignItems: 'center', justifyContent: 'center', border: `2px solid ${tone === i ? th.accent : 'transparent'}` },
  }, h(Text, { style: { fontSize: 14 } }, cp ? String.fromCodePoint(0x270b, cp) : '✋'))));
}

function Panel() {
  const th = useTheme();
  const [q, setQ] = useState('');
  const [group, setGroup] = useState(() => (emoji.recents().length ? 'recent' : 'smileys'));
  const [sel, setSel] = useState(0);
  const [tone, setToneState] = useState(emoji.tone());
  const [hover, setHover] = useState(null);
  const scroll = useRef(null);
  const list = useMemo(() => {
    if (q.trim()) return emoji.search(q);
    if (group === 'recent') return emoji.recents();
    return groups().find((g) => g.id === group)?.list || [];
  }, [q, group]);
  useEffect(() => { setSel(0); scroll.current?.scrollTo(0); }, [q, group]);
  const pick = (ch) => { emoji.remember(ch.replace(/[\u{1F3FB}-\u{1F3FF}]/gu, '')); panel.set(false); emoji.insert(ch, target); };
  const onKey = (e) => {
    if (e.key === 'ArrowRight') { setSel((s) => Math.min(list.length - 1, s + 1)); return true; }
    if (e.key === 'ArrowLeft') { setSel((s) => Math.max(0, s - 1)); return true; }
    if (e.key === 'ArrowDown') { setSel((s) => Math.min(list.length - 1, s + COLS)); return true; }
    if (e.key === 'ArrowUp') { setSel((s) => Math.max(0, s - COLS)); return true; }
    if (e.key === 'Tab') {
      const ids = ['recent', ...groups().map((g) => g.id)];
      setQ('');
      setGroup((g) => ids[(ids.indexOf(g) + (e.shift ? ids.length - 1 : 1)) % ids.length]);
      return true;
    }
    return false;
  };
  // donde está el cursor del campo; si no, al medio de la pantalla
  const W = COLS * CELL + 20, H = 400;
  const c = target?.caret;
  const left = Math.max(8, Math.min(screen.width - W - 8, c ? c.x - 20 : screen.width / 2 - W / 2));
  const top = c ? (c.y + c.h + H + 8 < screen.height ? c.y + c.h + 6 : Math.max(8, c.y - H - 6)) : Math.max(40, screen.height / 2 - H / 2);
  const shownSel = list[sel];
  const label = hover != null && list[hover] ? list[hover] : shownSel;
  const ids = ['recent', ...groups().map((g) => g.id)];
  return h(View, {
    emojiPanel: true,
    style: { position: 'absolute', left, top, width: W, height: H, borderRadius: 12, backgroundColor: th.windowBg, border: `1px solid ${th.separator}`,
      boxShadow: '0 14px 40px rgba(0,0,0,0.45)', padding: 10, gap: 8 },
  },
  h(TextInput, { value: q, onChange: setQ, autoFocus: true, placeholder: t('emoji.search'), onCancel: () => panel.set(false),
    onSubmit: () => { if (list[sel]) pick(emoji.withTone(list[sel], tone)); }, onKeyDown: onKey, style: { height: 28 } }),
  !q.trim() && h(View, { style: { flexDirection: 'row', justifyContent: 'space-between', direction: 'ltr' } }, ids.map((id) => h(View, {
    key: id, onClick: () => setGroup(id),
    style: { width: 30, height: 26, borderRadius: 6, alignItems: 'center', justifyContent: 'center', backgroundColor: group === id ? th.hover : 'transparent' },
  }, h(Text, { style: { fontSize: 15 } }, GROUP_ICON[id])))),
  h(Text, { style: { fontSize: 11, fontWeight: 700, color: th.textDim } }, q.trim() ? t('emoji.results', { count: list.length }) : t(`emoji.group.${group}`)),
  h(View, { style: { flex: 1, direction: 'ltr' } },
    list.length ? h(ScrollView, { style: { flex: 1 }, scrollRef: scroll }, h(Grid, { list, sel, onPick: pick, onHover: setHover, tone, th }))
      : h(Text, { style: { margin: 16, fontSize: 12, color: th.textDim, textAlign: 'center' } }, group === 'recent' && !q.trim() ? t('emoji.noRecent') : t('emoji.none'))),
  h(View, { style: { height: 1, backgroundColor: th.separator } }),
  h(View, { style: { flexDirection: 'row', alignItems: 'center', gap: 8 } },
    h(Text, { style: { fontSize: 22, width: 30 } }, label ? emoji.withTone(label, tone) : ''),
    h(Text, { style: { flex: 1, fontSize: 11, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' } }, label ? emoji.name(label) : ''),
    h(ToneBar, { tone, onTone: (n) => { setToneState(n); emoji.setTone(n); }, th }),
    h(View, { onClick: () => { panel.set(false); wm.open('characters'); }, style: { paddingHorizontal: 4 } }, h(Text, { style: { fontSize: 11, color: th.accent } }, t('emoji.more')))));
}

export function EmojiPanel() {
  const on = useStore(panel);
  useEffect(() => {
    if (!on) return undefined;
    const off = addListener('pointerdown', (e) => {
      // un clic afuera lo cierra (el panel es lo último que se dibuja)
      let n = e.target;
      while (n && !n.props?.emojiPanel) n = n.parent;
      if (!n) panel.set(false);
    });
    return off;
  }, [on]);
  return on ? h(Panel) : null;
}

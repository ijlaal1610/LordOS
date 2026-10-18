// ime.js — métodos de entrada (coreano, japonés, chino). El motor está en el servidor
// (server/src/ime.c); acá se sabe quién tiene el foco y se dibuja la ventana de candidatos.
//
//   ime.attach(token, caret, { inline })   un campo de texto o una terminal toma el foco;
//                                          caret() → { x, y, h } en pantalla
//   ime.detach(token)                      lo pierde (se descarta la composición)
//   useIme()                               { engine, preedit, candidates, selected, converting }
//
// Sin un foco de texto las teclas pasan tal cual (los atajos de las apps siguen andando).
// <ImeOverlay /> lo monta startShell: todos los temas lo tienen sin hacer nada.
import { createElement as h } from 'react';
import { View, Text } from '@lordos/runtime';
import { createStore, useStore } from './store.js';
import { useTheme } from './theme.js';

const EMPTY = { engine: '', preedit: '', candidates: [], selected: 0, converting: false, segments: null };
export const imeState = createStore(EMPTY);
globalThis.__lordIme = (s) => imeState.set({ ...EMPTY, ...s });

// Pila de focos: el último que se adjunta recibe el texto (un campo de búsqueda encima de la
// terminal); al soltarse vuelve el anterior.
let stack = [];
const top = () => stack[stack.length - 1] || null;
const native = (fn) => { try { fn(); } catch (e) { /* servidor sin lord.ime */ } };
const clear = () => imeState.set({ ...imeState.get(), preedit: '', candidates: [], converting: false });

export const ime = {
  attach(token, caret, { inline = false } = {}) {
    const prev = top();
    stack = [...stack.filter((e) => e.token !== token), { token, caret, inline }];
    if (prev && prev.token !== token) { native(() => lord.ime.reset()); clear(); }
    native(() => lord.ime.setContext(true));
  },
  detach(token) {
    const was = top();
    stack = stack.filter((e) => e.token !== token);
    if (was?.token !== token) return;
    native(() => lord.ime.setContext(false));       // descarta la composición
    clear();
    if (stack.length) native(() => lord.ime.setContext(true));
  },
  caret: () => top()?.caret?.() || null,
  owner: () => top()?.token || null,
};

export const useIme = () => useStore(imeState);

// Composición subrayada; en la conversión japonesa por frase, el segmento con foco resaltado
// (←/→ cambian de segmento, Shift+←/→ lo achican o agrandan).
export function Preedit({ st, size = 15, color, accent }) {
  const seg = st.segments;
  const part = (text, focus, key) => text ? h(View, { key, style: { backgroundColor: focus ? accent + '55' : 'transparent', borderRadius: 2 } },
    h(Text, { style: { fontSize: size, color, whiteSpace: 'nowrap' } }, text),
    h(View, { style: { height: focus ? 2 : 1, backgroundColor: accent } })) : null;
  if (!seg) return part(st.preedit, false, 'p');
  return h(View, { style: { flexDirection: 'row', direction: 'ltr' } }, part(seg[0], false, 'a'), part(seg[1], true, 'b'), part(seg[2], false, 'c'));
}

const PAGE = 9;

export function ImeOverlay() {
  const st = useIme();
  const t = useTheme();
  const showPreedit = st.preedit && !top()?.inline;
  const many = st.candidates.length > 1 || (st.converting && st.candidates.length);
  if (!showPreedit && !many) return null;
  const c = ime.caret();
  if (!c) return null;
  const first = Math.floor(st.selected / PAGE) * PAGE;
  const page = st.candidates.slice(first, first + PAGE);
  return h(View, {
    style: {
      position: 'absolute', left: Math.max(4, c.x - 6), top: c.y + c.h + 4, direction: 'ltr', pointerEvents: 'none',
      paddingHorizontal: 8, paddingVertical: 5, borderRadius: 8, gap: 4, backgroundColor: t.windowBg,
      border: `1px solid ${t.separator}`, boxShadow: '0 8px 24px rgba(0,0,0,0.35)',
    },
  },
  showPreedit && h(View, { style: { alignSelf: 'flex-start' } }, h(Preedit, { st, color: t.text, accent: t.accent })),
  many && h(View, { style: { flexDirection: 'row', gap: 4 } },
    page.map((s, i) => h(View, {
      key: i,
      style: { flexDirection: 'row', alignItems: 'center', gap: 3, paddingHorizontal: 5, paddingVertical: 2, borderRadius: 5,
        backgroundColor: first + i === st.selected ? t.accent : 'transparent' },
    },
    h(Text, { style: { fontSize: 11, color: first + i === st.selected ? '#fff' : t.textDim } }, String(i + 1)),
    h(Text, { style: { fontSize: 15, color: first + i === st.selected ? '#fff' : t.text, whiteSpace: 'nowrap' } }, s))),
    st.candidates.length > PAGE && h(Text, { style: { fontSize: 11, alignSelf: 'center', color: t.textDim } },
      `${first / PAGE + 1}/${Math.ceil(st.candidates.length / PAGE)}`)));
}

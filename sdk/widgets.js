// widgets.js — la capa de widgets del escritorio (por ejemplo, las notas de la app Notas).
//
// Una app registra un tipo y publica sus widgets; el tema dibuja la capa con <DesktopWidgets />:
// una vez entre el fondo y las ventanas (layer="below") y otra encima de las ventanas para los
// fijados al frente (layer="above").
//
//   registerWidgetType('note', NoteCard)              // NoteCard({ widget }) dibuja uno
//   setWidgets('note', [{ id, x, y, w, h, pinned }])  // reemplaza los de ese tipo
import { createElement as h } from 'react';
import { View } from '@lordos/runtime';
import { createStore, useStore } from './store.js';

const types = new Map();
export const desktopWidgets = createStore({});        // tipo → [widget]

export function registerWidgetType(type, Component) {
  types.set(type, Component);
  desktopWidgets.set((s) => ({ ...s }));
}

export function setWidgets(type, list) {
  desktopWidgets.set((s) => ({ ...s, [type]: list }));
}

export function DesktopWidgets({ layer = 'below' }) {
  const all = useStore(desktopWidgets);
  const items = [];
  for (const [type, list] of Object.entries(all)) {
    const C = types.get(type);
    if (!C || !list) continue;
    for (const w of list) if (!!w.pinned === (layer === 'above')) items.push(h(C, { key: `${type}:${w.id}`, widget: w }));
  }
  if (!items.length) return null;
  return h(View, { style: { position: 'absolute', left: 0, top: 0, width: 0, height: 0 } }, ...items);
}

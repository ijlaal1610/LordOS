// statusitems.js — íconos de estado en la barra del tema (como los "menu extras" de macOS).
//
//   const off = registerStatusItem('clock', ClockStatus, 10)   // ClockStatus({ color }) dibuja el suyo
//   <StatusItems color={t.menuText} />                          // el tema los muestra donde quiera
//
// Es opcional: un tema que no dibuja <StatusItems /> simplemente no los muestra. Los temas default
// y classic los ponen junto a los indicadores de privacidad. Un ítem que no tiene nada que mostrar
// devuelve null.
import { createElement as h } from 'react';
import { View } from '@lordos/runtime';
import { createStore, useStore } from './store.js';

export const statusItems = createStore([]);          // [{ id, Component, order }]

export function registerStatusItem(id, Component, order = 0) {
  statusItems.set((l) => [...l.filter((x) => x.id !== id), { id, Component, order }].sort((a, b) => a.order - b.order));
  return () => statusItems.set((l) => l.filter((x) => x.id !== id));
}

export function StatusItems({ color, height = 22 }) {
  const list = useStore(statusItems);
  if (!list.length) return null;
  return h(View, { style: { flexDirection: 'row', alignItems: 'center', height } },
    ...list.map(({ id, Component }) => h(Component, { key: id, color, height })));
}

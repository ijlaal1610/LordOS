// store.js — estado global mínimo con suscripción, para usar desde React con useStore().
import { useSyncExternalStore } from 'react';

export function createStore(initial) {
  let state = initial;
  const subs = new Set();
  return {
    get: () => state,
    set(patch) {
      // función: nuevo estado; objeto sobre objeto: se combina; null u otro valor: reemplaza
      const next = typeof patch === 'function' ? patch(state)
        : patch !== null && typeof patch === 'object' && state !== null && typeof state === 'object' && !Array.isArray(patch) ? { ...state, ...patch }
          : patch;
      if (next === state) return;
      state = next;
      for (const fn of subs) fn();
    },
    subscribe(fn) { subs.add(fn); return () => subs.delete(fn); },
  };
}

export function useStore(store, select = (s) => s) {
  return useSyncExternalStore(store.subscribe, () => select(store.get()));
}

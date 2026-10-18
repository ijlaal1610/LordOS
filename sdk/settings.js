// settings.js — ajustes del usuario, guardados en ~/.config/lordos/settings.json.
import { createStore, useStore } from './store.js';

const DEFAULTS = {
  mode: 'dark',          // 'dark' | 'light'
  accent: 'azul',
  effects: true,         // transparencias con blur
  wallpaper: 'aurora',
  keyboard: 'latam',     // antes de M5; ahora keyboards (ver keyboard.js)
  keyboards: null,       // [{ layout, variant }] de xkb-data
  keyboardIndex: 0,
  language: null,        // idioma de la interfaz ('es', 'en', 'pt'…); null = el de la región
  region: '',            // región de formatos ('es_AR'…); '' = la del idioma
  theme: null,           // id del tema elegido (carpeta en themes/)
};

function load() {
  try {
    const txt = lord.settings.load();
    return txt ? { ...DEFAULTS, ...JSON.parse(txt) } : { ...DEFAULTS };
  } catch (e) {
    console.warn('ajustes ilegibles, se usan los valores por defecto:', e);
    return { ...DEFAULTS };
  }
}

export const settings = createStore(load());

let pending = 0;
settings.subscribe(() => {
  clearTimeout(pending);
  pending = setTimeout(() => lord.settings.save(JSON.stringify(settings.get(), null, 2)), 300);
});

export function useSettings() {
  return [useStore(settings), settings.set];
}

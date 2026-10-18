// settings.js — user settings, stored in ~/.config/lordos/settings.json.
import { createStore, useStore } from './store.js';

const DEFAULTS = {
  mode: 'dark',          // 'dark' | 'light'
  accent: 'azul',
  effects: true,         // transparency with blur
  reduceMotion: false,   // decorative animations off (Settings → Appearance)
  wallpaper: 'aurora',
  keyboard: 'latam',     // before M5; now keyboards (see keyboard.js)
  keyboards: null,       // [{ layout, variant }] from xkb-data
  keyboardIndex: 0,
  language: null,        // interface language ('es', 'en', 'pt'…); null = the region's
  region: '',            // formats region ('es_AR'…); '' = the language's
  theme: null,           // id of the chosen theme (folder in themes/)
};

function load() {
  try {
    const txt = lord.settings.load();
    return txt ? { ...DEFAULTS, ...JSON.parse(txt) } : { ...DEFAULTS };
  } catch (e) {
    console.warn('unreadable settings, using the defaults:', e);
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

// keyboard.js — distribuciones de teclado del usuario (libxkbcommon en el servidor).
//
// settings.keyboards = [{ layout: 'latam', variant: '' }, { layout: 'us', variant: '', ime: 'kana' }]
// settings.keyboardIndex = la activa. keyboard.next() pasa a la siguiente (botón de la barra
// o ❖⌥Espacio). Cualquier distribución de xkb-data sirve: keyboard.list() devuelve todas con
// sus variantes. Con `ime` es un método de entrada (ime.c) sobre el teclado latino `layout`:
// hangul (coreano), kana (japonés), pinyin (chino simplificado).
import { setLayout, addListener } from '@lordos/runtime';
import { settings } from './settings.js';

const native = () => typeof lord.keyboard !== 'undefined' && lord.keyboard.current().xkb;

export function configured() {
  const s = settings.get();
  if (Array.isArray(s.keyboards) && s.keyboards.length) return s.keyboards;
  return [{ layout: s.keyboard || 'latam', variant: '' }];      // ajustes de antes de M5
}

let applied = '';
let imeStatus = 0;      // 1 = el método de entrada no encontró su diccionario
export const IME_ENGINES = [
  { id: 'hangul', lang: 'ko', label: '한' },
  { id: 'kana', lang: 'ja', label: 'あ' },
  { id: 'pinyin', lang: 'zh', label: '拼' },
];
function apply() {
  const list = configured();
  const idx = Math.min(settings.get().keyboardIndex || 0, list.length - 1);
  const key = JSON.stringify([list, idx]);
  if (key === applied) return;
  applied = key;
  if (native() || lord.keyboard?.set) {
    const ok = lord.keyboard.set(list.map((k) => k.layout).join(','), list.map((k) => k.variant || '').join(','));
    if (ok) lord.keyboard.setGroup(idx);
    imeStatus = lord.ime ? lord.ime.set(list[idx].ime || '') : -1;
    if (ok) return;
  }
  setLayout(list[idx].layout === 'us' ? 'us' : 'latam');        // respaldo sin XKB
}

let catalog = null;
export const keyboard = {
  list() { if (!catalog) catalog = lord.keyboard?.list?.() || []; return catalog; },
  configured,
  active: () => configured()[Math.min(settings.get().keyboardIndex || 0, configured().length - 1)],
  next() {
    const n = configured().length;
    settings.set({ keyboardIndex: ((settings.get().keyboardIndex || 0) + 1) % n });
  },
  set: (keyboards) => settings.set({ keyboards, keyboardIndex: 0 }),
  // Etiqueta corta para la barra: "LA", "US", "BR", "한", "あ", "拼"…
  label: (k) => IME_ENGINES.find((e) => e.id === k.ime)?.label || (k.label || k.layout).slice(0, 2).toUpperCase(),
  engines: IME_ENGINES,
  // ¿Falta el diccionario del método de entrada activo? (instalar el paquete del idioma)
  missingDictionary: () => imeStatus === 1,
};

// ❖⌥Espacio o Ctrl+⌥Espacio pasan a la siguiente distribución o método de entrada (como
// ⌃⌥Espacio en macOS). ❖Espacio y Ctrl+Espacio son de Spotlight (sdk/spotlight.js).
addListener('keydown', (e) => {
  if ((e.meta || e.ctrl) && e.alt && !e.shift && e.key === ' ' && !e.repeat && configured().length > 1) keyboard.next();
});

apply();
settings.subscribe(apply);

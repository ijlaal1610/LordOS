// theme.js — contrato entre temas y apps. El tema provee los tokens; las apps los usan
// con useTheme() y así se ven bien con cualquier tema.
import { createContext, createElement, useContext, useEffect } from 'react';
import { settings } from './settings.js';

// Tokens mínimos que todo tema debe proveer (ver themes/README.md).
export const DEFAULT_TOKENS = {
  mode: 'dark',
  accent: '#0a84ff',
  text: '#f5f5f7',
  textDim: 'rgba(235,235,245,0.55)',
  windowBg: '#1f1f23',
  sidebarBg: '#26262b',
  separator: 'rgba(255,255,255,0.08)',
  hover: 'rgba(255,255,255,0.07)',
  control: '#3a3a40',
  inputBg: '#2c2c31',
  radius: 8,
  wallpapers: [],
};

const ThemeContext = createContext(DEFAULT_TOKENS);

// Las apps .lpk aisladas (otro proceso) reciben estos mismos tokens en vivo por el broker
// (lord.broker.publish, ver docs/PRIVACIDAD.md). Solo lo publica el tema, no el app host.
let published = '';
function publishTokens(value) {
  try {
    if (typeof lord === 'undefined' || !lord.broker || typeof lord.broker.publish !== 'function') return;
    const json = JSON.stringify(value, (k, v) => (typeof v === 'function' ? undefined : v));
    if (json !== published) { published = json; lord.broker.publish('theme', json); }
  } catch (e) { /* tokens no serializables */ }
}

// Las apps Linux (GTK, GNOME, Qt) leen modo, acento y orden de los botones de la barra por
// org.freedesktop.portal.Settings, que atiende lord-server (ui/portal.c). gtkButtons es el orden de
// GTK: los semáforos a la izquierda, o los botones a la derecha en un tema estilo Windows.
const GNOME_ACCENTS = { azul: 'blue', violeta: 'purple', rosa: 'pink', naranja: 'orange', verde: 'green', grafito: 'slate',
  rojo: 'red', amarillo: 'yellow', turquesa: 'teal', celeste: 'blue' };
let portalSent = '';
function syncPortal(value) {
  try {
    if (typeof lord === 'undefined' || !lord.portal) return;
    const name = settings.get().accent || '';
    const msg = { dark: value.mode !== 'light', accent: /^#[0-9a-f]{6}$/i.test(value.accent) ? value.accent : '#0a84ff',
      accentName: GNOME_ACCENTS[name] || 'blue', buttons: value.gtkButtons || 'close,minimize,maximize:' };
    const json = JSON.stringify(msg);
    if (json !== portalSent) { portalSent = json; lord.portal.set(msg); }
  } catch (e) { /* sin portal */ }
}

export function ThemeProvider({ tokens, children }) {
  const value = { ...DEFAULT_TOKENS, ...tokens };
  useEffect(() => { publishTokens(value); syncPortal(value); });
  return createElement(ThemeContext.Provider, { value }, children);
}

export const useTheme = () => useContext(ThemeContext);

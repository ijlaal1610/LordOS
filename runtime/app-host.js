// app-host.js — el «host» que corre dentro del proceso de una app .lpk aislada
// (lord-server --app <id>, ver server/src/app/appclient.c). Trae React, el runtime y el SDK
// (con el objeto `lord` del broker, que da fs/clipboard/perms verificados por lord-server),
// carga el app.js del paquete y lo dibuja. La app usa `@lordos/sdk` igual que siempre.
//
// Tema, idioma y dirección (RTL) llegan de lord-server: el estado al arrancar
// (lord.broker.state()) y después en vivo (__lordBrokerEvent), así la app se ve como el resto
// del escritorio y cambia junto con él (modo claro/oscuro, acento, fondo, idioma).
import * as React from 'react';
import * as jsxRuntime from 'react/jsx-runtime';
import * as runtime from '@lordos/runtime';
import * as sdk from '@lordos/sdk';

// Los módulos compartidos que la app pide con globalThis.__lordos.require (como en el tema).
const shared = { react: React, 'react/jsx-runtime': jsxRuntime, '@lordos/runtime': runtime, '@lordos/sdk': sdk };
globalThis.__lordos = globalThis.__lordos || {};
globalThis.__lordos.require = (name) => {
  if (!(name in shared)) throw new Error(`módulo no disponible: ${name}`);
  return shared[name];
};

const { createElement: h, Fragment } = React;
const { render } = runtime;
const { ThemeProvider, DEFAULT_TOKENS, PermissionPrompt, createStore, useStore, locale } = sdk;

const tokens = createStore(DEFAULT_TOKENS);

function applyLocale(l) {
  if (!l || !l.lang) return;
  const cur = locale.get();
  if (cur.lang !== l.lang || (cur.region || '') !== (l.region || '')) locale.set({ lang: l.lang, region: l.region || '' });
}

function applyState(st) {
  if (!st) return;
  if (st.theme) tokens.set(st.theme);
  applyLocale(st.locale);
}

applyState(lord.broker.state());          // estado actual de la sesión, antes del primer cuadro

globalThis.__lordBrokerEvent = (kind, data) => {
  if (kind === 'theme' && data) tokens.set(data);
  else if (kind === 'locale') applyLocale(data);
  else if (kind === 'state') applyState(data);
  else if (kind === 'revoke' && data && data.perm) {
    // el usuario revocó un permiso: se apaga el indicador y se avisa a la app
    try { sdk.markDeviceUse(lord.appId, data.perm, false); } catch (e) { /* */ }
    try { for (const fn of revokeListeners) fn(data.perm); } catch (e) { /* */ }
  }
  else if (kind === 'menu' && data && data.item) { try { sdk.appMenuClick(data.item); } catch (e) { /* */ } }
};
const revokeListeners = new Set();
globalThis.__lordOnRevoke = (fn) => { revokeListeners.add(fn); return () => revokeListeners.delete(fn); };

let mod = null;
function App() {
  if (!mod) mod = lord.loadApp();         // evalúa app.js una sola vez → { default, Icon }
  if (!mod || typeof mod.default !== 'function') return h(runtime.Text, { style: { color: '#fff', padding: 20 } }, 'La app no exporta un componente');
  return h(mod.default);
}

function Root() {
  const tk = useStore(tokens);
  const { lang, region } = useStore(locale);
  // al cambiar de idioma se vuelve a montar la app, como en el tema (los textos se arman al dibujar)
  return h(ThemeProvider, { tokens: tk },
    h(Fragment, { key: `${lang}|${region}` },
      h(runtime.View, { style: { flex: 1, backgroundColor: tk.windowBg } }, h(App)),
      h(PermissionPrompt)));
}

render(h(Root));

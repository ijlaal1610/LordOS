// privacy.js — permisos por app estilo TCC de macOS, indicadores de cámara/micrófono y el marco
// de la página Ajustes → Privacidad (ver docs/PRIVACIDAD.md).
//
// El estado de los permisos lo guarda el servidor (lord.perms, server/src/sec/perms.c) en un
// archivo que las apps no pueden escribir. Acá está:
//   - la cola de avisos de permiso, que DIBUJA EL SISTEMA (PermissionPrompt), nunca la app;
//   - requestPermission(app, perm): devuelve una promesa con true/false, preguntando una sola
//     vez (después vale la decisión guardada);
//   - los indicadores de cámara (punto verde) y micrófono (naranja) de la barra de menú;
//   - registerPrivacySection(): un hueco para que otras partes (p. ej. el Monitor) agreguen su
//     propia sección a la página Privacidad, sin tocar su código.
import { createElement as h, Fragment } from 'react';
import { View, Text } from '@lordos/runtime';
import { createStore, useStore } from './store.js';
import { t, locale, direction } from './i18n.js';
import { appName } from './i18n.js';
import { getApp } from './windows.js';

const api = () => (typeof lord !== 'undefined' && lord.perms) || null;

export const PERMISSIONS = (() => { try { return api() ? lord.perms.names() : []; } catch (e) { return []; } })();

// ---- estado de permisos ----
export const perms = {
  names: () => PERMISSIONS,
  get: (app, perm) => { try { return api() ? lord.perms.get(app, perm) : 'ask'; } catch (e) { return 'ask'; } },
  set(app, perm, state) { try { return api() ? lord.perms.set(app, perm, state) : false; } finally { permsRev.set((v) => v + 1); } },
  list: () => { try { return api() ? lord.perms.list() : []; } catch (e) { return []; } },
};
export const permsRev = createStore(0);   // sube al cambiar algo (para redibujar Privacidad)

// ---- cola de avisos de permiso (los dibuja el sistema) ----
const queue = createStore([]);            // [{ id, app, perm, resolve }]
let seq = 1;

const inApp = () => typeof lord !== 'undefined' && lord.broker && typeof lord.broker.request === 'function';

// Pide un permiso: si ya hay decisión, la respeta; si no, encola un aviso del sistema.
// Dentro de una app aislada (lord-server --app), va por el broker: el aviso lo dibuja
// lord-server, no la app.
export function requestPermission(app, perm) {
  if (inApp()) { try { return Promise.resolve(!!lord.broker.request(perm)); } catch (e) { return Promise.resolve(false); } }
  const st = perms.get(app, perm);
  if (st === 'allow') return Promise.resolve(true);
  if (st === 'deny') return Promise.resolve(false);
  return new Promise((resolve) => {
    queue.set((q) => [...q, { id: seq++, app, perm, resolve }]);
  });
}

// El servidor (compositor) recibe los avisos de las apps aisladas por el broker: muestra el
// aviso del sistema y, con la respuesta, llama a lord.broker.resolve. También los indicadores.
if (typeof globalThis !== 'undefined') {
  globalThis.__lordBroker = (kind, data) => {
    try {
      if (kind === 'prompt') {
        queue.set((q) => [...q, { id: seq++, app: data.app, perm: data.perm, broker: data.reqid }]);
      } else if (kind === 'menu') {
        if (typeof globalThis.__lordApplyAppMenus === 'function') globalThis.__lordApplyAppMenus(data.app);
      } else if (kind === 'pick') {
        if (typeof globalThis.__lordBrokerPick === 'function') globalThis.__lordBrokerPick(data.reqid, data.app, data.save, data.suggest);
      } else if (kind === 'device') {
        if (data.off) brokerDevicesByApp(data.app, null, false);
        else brokerDevicesByApp(data.app, data.kind, !!data.on);
      }
    } catch (e) { /* aviso mal formado */ }
  };
}

function answer(item, allow) {
  queue.set((q) => q.filter((x) => x.id !== item.id));
  if (item.broker != null) {
    // aviso de una app aislada: lord-server guarda la decisión y le responde a la app
    try { lord.broker.resolve(item.broker, allow); } catch (e) { /* servidor sin broker */ }
  } else {
    perms.set(item.app, item.perm, allow ? 'allow' : 'deny');
    if (item.resolve) item.resolve(allow);
  }
}

// Aviso del sistema: ««App» quiere usar el micrófono — Permitir / No permitir».
export function PermissionPrompt() {
  const q = useStore(queue);
  if (!q.length) return null;
  const item = q[0];
  const name = appName(getApp(item.app)) || item.app;
  const msg = t(`privacy.ask.${item.perm}`, { app: name });
  return h(View, { style: { position: 'absolute', left: 0, top: 0, right: 0, bottom: 0, alignItems: 'center', justifyContent: 'flex-start',
    backgroundColor: 'rgba(0,0,0,0.35)', zIndex: 100000 } },
    h(View, { style: { marginTop: 80, width: 320, borderRadius: 14, padding: 20, gap: 14, backgroundColor: '#2b2b30',
      border: '1px solid rgba(255,255,255,0.12)', boxShadow: '0 20px 60px rgba(0,0,0,0.5)', alignItems: 'center' } },
      h(View, { style: { width: 44, height: 44, borderRadius: 10, backgroundColor: item.perm === 'camera' ? '#30d158' : item.perm === 'microphone' ? '#ff9f0a' : '#0a84ff',
        alignItems: 'center', justifyContent: 'center' } }, h(Text, { style: { fontSize: 22 } }, glyph(item.perm))),
      h(Text, { style: { fontSize: 14, fontWeight: 700, color: '#f5f5f7', textAlign: 'center', lineHeight: 1.4 } }, msg),
      h(Text, { style: { fontSize: 11, color: 'rgba(235,235,245,0.55)', textAlign: 'center' } }, t('privacy.ask.where')),
      h(View, { style: { flexDirection: 'row', gap: 10, marginTop: 4 } },
        h(Btn, { label: t('privacy.deny'), onClick: () => answer(item, false) }),
        h(Btn, { primary: true, label: t('privacy.allow'), onClick: () => answer(item, true) }))));
}

function Btn({ label, primary, onClick }) {
  return h(View, { onClick, style: { paddingVertical: 7, paddingHorizontal: 16, borderRadius: 8,
    backgroundColor: primary ? '#0a84ff' : 'rgba(255,255,255,0.1)' } },
    h(Text, { style: { fontSize: 13, fontWeight: 600, color: '#fff' } }, label));
}

const glyph = (p) => (p === 'camera' ? '📷' : p === 'microphone' ? '🎙' : p === 'screen' ? '▦' : p === 'network' ? '⇅' : '●');

// ---- indicadores de cámara/micrófono ----
// Fuente propia (apps .lpk aisladas: lo sabe el broker y lo refleja acá con markDeviceUse) +
// fuente del Monitor (apps Linux, por PipeWire: globalThis.__lordos.monitorDevices, fase 2).
const brokerDevices = createStore([]);    // [{ app, kind: 'camera'|'microphone' }]

export function markDeviceUse(app, kind, on) {
  // Dentro de una app aislada, se avisa por el broker (lord-server muestra el indicador).
  if (typeof lord !== 'undefined' && lord.broker && typeof lord.broker.device === 'function') {
    try { lord.broker.device(kind, !!on); } catch (e) { /* */ }
    return;
  }
  brokerDevices.set((list) => {
    const rest = list.filter((d) => !(d.app === app && d.kind === kind));
    return on ? [...rest, { app, kind }] : rest;
  });
}

// Desde el servidor (compositor): el broker avisó que una app usa (o dejó de usar) un
// dispositivo. 'off' con kind null apaga todos los de esa app (la app se cerró).
function brokerDevicesByApp(app, kind, on) {
  brokerDevices.set((list) => {
    const rest = list.filter((d) => d.app !== app || (kind && d.kind !== kind));
    return on && kind ? [...rest, { app, kind }] : rest;
  });
}

function monitorDeviceStore() {
  try { return globalThis.__lordos && globalThis.__lordos.monitorDevices; } catch (e) { return null; }
}

// [{ kind, apps: [nombre] }] para cada dispositivo en uso.
export function useIndicators() {
  const own = useStore(brokerDevices);
  const md = monitorDeviceStore();
  const ext = md ? useStore(md) : [];
  const all = [...own, ...(Array.isArray(ext) ? ext : [])];
  const byKind = {};
  for (const d of all) {
    const name = d.name || appName(getApp(d.app)) || d.app;
    (byKind[d.kind] = byKind[d.kind] || []).push(name);
  }
  return Object.entries(byKind).map(([kind, apps]) => ({ kind, apps }));
}

// Componente para la barra de menú (los dos temas lo incluyen). Un punto verde para la cámara y
// uno naranja para el micrófono; al tocarlo muestra qué app lo usa.
export function PrivacyIndicators({ size = 8 }) {
  const list = useIndicators();
  const [open, setOpen] = useStoreLocal();
  if (!list.length) return null;
  return h(View, { style: { flexDirection: 'row', alignItems: 'center', gap: 6, paddingHorizontal: 6 }, onClick: () => setOpen(!open) },
    list.map((d) => h(View, { key: d.kind, style: { width: size, height: size, borderRadius: size / 2,
      backgroundColor: d.kind === 'camera' ? '#30d158' : '#ff9f0a' } })),
    open ? h(View, { style: { position: 'absolute', top: 22, right: 0, minWidth: 180, borderRadius: 10, padding: 10, gap: 6,
      backgroundColor: '#2b2b30', border: '1px solid rgba(255,255,255,0.12)', boxShadow: '0 12px 40px rgba(0,0,0,0.45)', zIndex: 99999 } },
      list.map((d) => h(View, { key: d.kind, style: { gap: 2 } },
        h(Text, { style: { fontSize: 11, fontWeight: 700, color: d.kind === 'camera' ? '#30d158' : '#ff9f0a' } },
          t(`privacy.inUse.${d.kind}`)),
        d.apps.map((a, i) => h(Text, { key: i, style: { fontSize: 12, color: '#f5f5f7' } }, a))))) : null);
}

// mini-estado local sin hooks de React (el componente del tema puede no tener useState disponible
// en su import); usamos un store de módulo.
const openStore = createStore(false);
function useStoreLocal() { return [useStore(openStore), openStore.set]; }

// ---- huecos de la página Privacidad (p. ej. el Monitor agrega el suyo) ----
export const privacySections = createStore([]);   // [{ id, order, Component }]
export function registerPrivacySection(section) {
  if (!section || !section.id || typeof section.Component !== 'function') return;
  privacySections.set((list) => [...list.filter((s) => s.id !== section.id), section].sort((a, b) => (a.order || 50) - (b.order || 50)));
}

// El idioma, la región y la dirección (RTL) también van en vivo a las apps aisladas.
function publishLocale() {
  try {
    if (typeof lord === 'undefined' || !lord.broker || typeof lord.broker.publish !== 'function') return;
    const l = locale.get();
    lord.broker.publish('locale', JSON.stringify({ lang: l.lang, region: l.region || '', dir: direction() }));
  } catch (e) { /* sin broker */ }
}
publishLocale();
locale.subscribe(publishLocale);

// Las apps aisladas pueden enterarse de que el usuario les revocó un permiso (para soltar la
// cámara o el micrófono). En proceso no pasa nada (la revocación es inmediata en el broker).
export function onPermissionRevoked(fn) {
  try { return typeof globalThis.__lordOnRevoke === 'function' ? globalThis.__lordOnRevoke(fn) : () => {}; } catch (e) { return () => {}; }
}

// Micrófono para una app aislada: lord-server es el dueño del flujo de PipeWire y le pasa a la app
// un anillo de SOLO LECTURA (ver docs/PRIVACIDAD.md). micLevel() da el nivel (RMS 0..1).
export function micStart() { try { return !!(lord.broker && lord.broker.micStart && lord.broker.micStart()); } catch (e) { return false; } }
export function micStop() { try { lord.broker && lord.broker.micStop && lord.broker.micStop(); } catch (e) { /* */ } }
export function micLevel() { try { return (lord.broker && lord.broker.micLevel) ? lord.broker.micLevel() : 0; } catch (e) { return 0; } }

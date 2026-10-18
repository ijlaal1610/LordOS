// packages.js — apps nativas LordOS instaladas como paquete (.lpk; ver docs/APPS-NATIVAS.md).
//
// Un paquete trae su app ya empaquetada (tools/lpk.mjs) sin React ni el SDK adentro: usa los
// del tema, que se comparten acá por globalThis.__lordos.require(). La app se carga recién
// cuando se abre su primera ventana.
//
//   packages.list()                 → [{ id, name, version, description, permissions, size… }]
//   await packages.inspect(ruta)    → { manifest, files, size }   (antes de instalar, para confirmar)
//   packages.install(ruta) / packages.remove(id)
//   const { instance } = await loadWasm('fractal', 'mandel.wasm', imports)   // desde la app
import * as React from 'react';
import * as jsxRuntime from 'react/jsx-runtime';
import * as runtime from '@lordos/runtime';
import * as sdk from './index.js';
import { createStore } from './store.js';
import { locale } from './i18n.js';
import { setPackageApps, windows, wm, appIdOf } from './windows.js';
import { iconFromFile } from './apps.js';

const h = React.createElement;
const shared = {
  react: React, 'react/jsx-runtime': jsxRuntime, '@lordos/runtime': runtime, '@lordos/sdk': sdk,
};
globalThis.__lordos = {
  require(name) {
    if (!(name in shared)) throw new Error(`módulo no disponible para apps instaladas: ${name}`);
    return shared[name];
  },
};

const has = () => typeof lord.apps !== 'undefined' && typeof lord.apps.packages === 'function';
const loaded = new Map();               // id → { default, Icon } | { error }

function load(id) {
  if (!loaded.has(id)) {
    try {
      const mod = lord.apps.loadPackage(id);
      loaded.set(id, mod && typeof mod.default === 'function' ? mod : { error: 'app.js no exporta un componente' });
    } catch (e) { loaded.set(id, { error: String(e?.message || e) }); }
  }
  return loaded.get(id);
}

// Con modo app (lord-server --app), una app .lpk NUNCA se evalúa en el contexto del tema: si algo
// abre su ventana en proceso (wm.open, «Nueva ventana» del Dock…), se lanza el proceso aislado y
// se cierra esta ventana vacía. Ver docs/PRIVACIDAD.md.
const isolated = () => typeof lord.apps !== 'undefined' && typeof lord.apps.launchPackage === 'function';

function Redirect({ id }) {
  const win = sdk.useAppWindow();
  React.useEffect(() => {
    // already running: a plain open focuses its window; «New window» (wm.open(id, { new: true }))
    // asks the running process for another window (lord-server forwards it over the broker)
    const running = windows.get().filter((w) => w.args?.pkg && w.args?.appId === id);
    const wantsNew = win && win.id && String(win.id).includes('#');
    if (running.length && !wantsNew) wm.focus(running[running.length - 1].id);
    else lord.apps.launchPackage(id);
    if (win && win.id) wm.close(win.id, true);
  }, []);
  return null;
}

// A built-in app that runs as its own sandboxed process (manifest "process": "isolated", shipped as
// <assets>/sysapps/<id>, see tools/build-theme.mjs): opening its window in the theme starts the
// process instead and closes this placeholder. Without app mode (or without the built system
// package) the app runs in-process as before.
export function isolatedApp(id, Fallback) {
  return function IsolatedApp(props) {
    const win = sdk.useAppWindow();
    const [inproc, setInproc] = React.useState(false);
    React.useEffect(() => {
      // already running: focus its window (a single window: it holds a vault)
      const running = windows.get().filter((w) => w.args?.pkg && w.args?.appId === id);
      if (isolated() && running.length) { wm.focus(running[running.length - 1].id); if (win && win.id) wm.close(win.id, true); }
      else if (isolated() && lord.apps.launchPackage(id)) { if (win && win.id) wm.close(win.id, true); }
      else setInproc(true);
    }, []);
    return inproc && Fallback ? h(Fallback, props) : null;
  };
}

function lazyComponent(id) {
  return function PackageApp(props) {
    if (isolated()) return h(Redirect, { id });
    const mod = load(id);
    if (mod.error) {
      return h(runtime.View, { style: { flex: 1, padding: 20 } },
        h(runtime.Text, { style: { fontSize: 13, color: '#ff453a' } }, sdk.t('shell.packages.loadError', { error: mod.error })));
    }
    return h(mod.default, props);
  };
}

const localized = (v, lang) => (v && typeof v === 'object' ? v[lang] || v[lang.split('-')[0]] || v.en || Object.values(v)[0] : v) || '';

export const packageList = createStore([]);

function read() {
  if (!has()) return [];
  const lang = locale.get().lang;
  const out = [];
  for (const p of lord.apps.packages()) {
    let m;
    try { m = JSON.parse(p.manifest); } catch (e) { continue; }
    if (m.id !== p.id) continue;
    const name = localized(m.name, lang) || p.id;
    out.push({
      id: p.id, name, version: String(m.version || ''), description: localized(m.description, lang),
      permissions: Array.isArray(m.permissions) ? m.permissions.map(String) : [],
      size: Array.isArray(m.size) ? m.size : [640, 420], minSize: Array.isArray(m.minSize) ? m.minSize : undefined,
      transparent: !!m.transparent, dock: false, package: true, dir: p.dir, category: typeof m.category === 'string' ? m.category : undefined,
      Icon: iconFromFile(m.icon ? `${p.dir}/${m.icon}` : '', name), Component: lazyComponent(p.id),
    });
  }
  return out;
}

export function refreshPackages() {
  const list = read();
  packageList.set(list);
  setPackageApps(list);
}

export const packages = {
  list: () => packageList.get(),
  inspect: (path) => lord.apps.inspectPackage(path),
  install(path) {
    const r = lord.apps.installPackage(path);
    loaded.delete(r.manifest.id);       // una versión nueva se vuelve a cargar al abrirla
    for (const w of windows.get()) if (appIdOf(w.id) === r.manifest.id) wm.close(w.id, true);
    refreshPackages();
    return r;
  },
  remove(id) {
    for (const w of windows.get()) if (appIdOf(w.id) === id) wm.close(w.id, true);
    const ok = lord.apps.removePackage(id);
    loaded.delete(id);
    refreshPackages();
    return ok;
  },
};

// Archivo de una app instalada (ArrayBuffer) y su módulo WebAssembly ya instanciado.
export const packageFile = (appId, path) => lord.apps.packageFile(appId, path);
export function loadWasm(appId, path, imports = {}) {
  if (typeof WebAssembly === 'undefined') return Promise.reject(new Error('WebAssembly no está disponible'));
  return WebAssembly.instantiate(packageFile(appId, path), imports);
}

locale.subscribe(refreshPackages);

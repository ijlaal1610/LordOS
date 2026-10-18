// monitor.js — métricas del sistema para la app Monitor (lord.monitor, ver docs/MONITOR.md).
//
//   const samples = useMonitor({ procs: true, conns: false, geo: true });   // últimas 240 muestras (60 s)
//   const procs = useStore(monitorProcesses);    // { t, total, list: [{ pid, name, cpu, rss, app… }] }
//   const conns = useStore(monitorConnections);  // { t, others, list: [{ id, proto, remote, cc, app… }] }
//   monitor.info()                               // { cpuModel, cores, geo, home: { lat, lon, cc } }
//
// lord-server muestrea en un hilo propio solo mientras haya algún useMonitor montado (y lo
// consulte): con la app cerrada no cuesta nada. Las listas de procesos y conexiones se arman
// solo si algún useMonitor las pide, una vez por segundo.
//
// Fase 2 (no implementada todavía): monitor.deviceUsers() → [{ kind: 'camera' | 'microphone',
// app, name, pid }] y el store monitorDevices; y el firewall con reglas por app y por dominio,
// que se apoyan en los mismos registros de conexión (campo `verdict`, hoy siempre 'allow').
import { useEffect, useRef, useState } from 'react';
import { createStore, useStore } from './store.js';
import { locale } from './i18n.js';

export const MONITOR_HISTORY = 240;             // 60 s a 4 muestras por segundo
export const monitorSamples = createStore([]);
export const monitorProcesses = createStore(null);
export const monitorConnections = createStore(null);
export const monitorDevices = createStore([]);  // fase 2

const available = () => typeof lord.monitor !== 'undefined';
const wants = new Map();                        // useMonitor montado → { procs, conns }
let timer = null;
let lastWant = '';

function wanted() {
  let procs = false, conns = false, geo = false;
  for (const w of wants.values()) { procs ||= w.procs; conns ||= w.conns; geo ||= w.conns && w.geo; }
  return { procs, conns, geo };
}

function poll() {
  const w = wanted();
  const key = `${w.procs}|${w.conns}|${w.geo}`;
  if (key !== lastWant) { lastWant = key; lord.monitor.want(w.procs, w.conns, w.geo); }
  const s = lord.monitor.sample();
  if (s) monitorSamples.set((list) => (list.length >= MONITOR_HISTORY ? [...list.slice(1 - MONITOR_HISTORY), s] : [...list, s]));
  const lang = locale.get().lang;
  if (w.procs) { const p = lord.monitor.processes(lang); if (p) monitorProcesses.set(() => p); }
  if (w.conns) {
    const c = lord.monitor.connections(lang);
    if (c) {
      for (const x of c.list) x.verdict = 'allow';           // un socket que existe ya pasó el firewall
      monitorConnections.set(() => c);
      fwPoll();
    }
  }
}

function subscribe(token, w) {
  wants.set(token, w);
  if (timer || !available()) return;
  lord.monitor.start();
  lastWant = '';
  poll();
  timer = setInterval(poll, 250);
}

function unsubscribe(token) {
  wants.delete(token);
  if (wants.size || !timer) return;
  clearInterval(timer);
  timer = null;
  lord.monitor.want(false, false, false);
  lord.monitor.stop();
  monitorSamples.set([]);                       // la historia empieza de nuevo al volver a abrir
  monitorProcesses.set(() => null);
  monitorConnections.set(() => null);
}

// geo: buscar el país de cada conexión en la base local (assets/monitor, sin red).
// everyMs: cada cuánto se vuelve a dibujar quien llama (la muestra sigue siendo cada 250 ms; con
// la ventana en segundo plano conviene 1000: la historia no pierde puntos y se dibuja menos).
// everyMs = 0: solo pide los datos, sin volver a dibujar (devuelve null).
export function useMonitor({ procs = false, conns = false, geo = true, everyMs = 250 } = {}) {
  const token = useRef(null);
  if (!token.current) token.current = {};
  useEffect(() => () => unsubscribe(token.current), []);
  useEffect(() => { subscribe(token.current, { procs, conns, geo }); }, [procs, conns, geo]);
  const [list, setList] = useState(() => monitorSamples.get());
  useEffect(() => {
    if (!everyMs) return undefined;
    let last = 0, timer = null;
    const push = () => { timer = null; last = Date.now(); setList(monitorSamples.get()); };
    push();
    const off = monitorSamples.subscribe(() => {
      if (timer) return;
      const wait = everyMs - (Date.now() - last);
      if (wait <= 20) push(); else timer = setTimeout(push, wait);
    });
    return () => { off(); if (timer) clearTimeout(timer); };
  }, [everyMs]);
  return everyMs ? list : null;
}

// ---- firewall por app (lord.fw, docs/FIREWALL.md) ----
// lord-netperm (root) aplica las reglas en el kernel y cuenta las conexiones nuevas de cada app;
// lord-server se las pasa a JS. firewallState: { status, counts: { app: [permitidas, denegadas] },
// events: [las últimas 200 conexiones nuevas], rev }. Sube rev al cambiar una regla.
const fwApi = () => (typeof lord !== 'undefined' && lord.fw) || null;
export const firewallState = createStore({ status: null, counts: {}, events: [], rev: 0 });
let fwLast = 0;
function fwPoll(force) {
  const api = fwApi();
  if (!api) return;
  const now = Date.now();
  if (!force && now - fwLast < 1000) return;
  fwLast = now;
  try {
    const p = api.poll();
    const status = api.status();
    firewallState.set((s) => ({ ...s, status, counts: p.counts || {}, events: p.events.length ? [...s.events, ...p.events].slice(-200) : s.events }));
  } catch (e) { /* sin lord.fw */ }
}
const fwBump = (r) => { firewallState.set((s) => ({ ...s, rev: s.rev + 1 })); fwPoll(true); return r; };
const fwCall = (fn, dflt) => { try { const api = fwApi(); return api ? fn(api) : dflt; } catch (e) { return dflt; } };
// Una regla solo se puede aplicar a una app con scope propia: LordOS, una app .desktop o una .lpk.
const fwEnforceable = (app) => app === 'lordos' || /^(desktop|lpk):/.test(app || '');
export const firewall = {
  available: () => !!fwApi(),
  enforceable: fwEnforceable,
  status: () => fwCall((f) => f.status(), null),
  state: () => fwCall((f) => f.state(), { mode: 'silent', trusted: [], restricted: {} }),
  appRule: (app) => (fwEnforceable(app) ? fwCall((f) => f.appRule(app), null) : null),
  setAppRule: (app, v) => fwBump(fwCall((f) => f.setAppRule(app, v), false)),
  setTrusted: (app, on) => fwBump(fwCall((f) => f.setTrusted(app, !!on), false)),
  setRestricted: (app, flags) => fwBump(fwCall((f) => f.setRestricted(app, flags), false)),
  setMode: (m) => fwBump(fwCall((f) => f.setMode(m), false)),
  rules: (app) => fwCall((f) => f.rules(app), []),
  addRule: (r) => fwBump(fwCall((f) => f.addRule(r), false)),
  removeRule: (id) => fwBump(fwCall((f) => f.removeRule(id), false)),
  refresh: () => fwPoll(true),
  RESTRICTIONS: ['no-internet', 'no-lan', 'no-network', 'no-incoming', 'no-camera', 'no-mic', 'no-screen', 'no-usb', 'no-bluetooth', 'no-files', 'no-notifications'],
};

let infoCache = null;
export const monitor = {
  available,
  info: () => (infoCache ||= available() ? lord.monitor.info() : null),
  deviceUsers: () => [],                        // fase 2
};

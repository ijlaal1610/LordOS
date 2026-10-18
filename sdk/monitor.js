// monitor.js — system metrics for the Monitor app (lord.monitor, see docs/MONITOR.md).
//
//   const samples = useMonitor({ procs: true, conns: false, geo: true });   // last 240 samples (60 s)
//   const procs = useStore(monitorProcesses);    // { t, total, list: [{ pid, name, cpu, rss, app… }] }
//   const conns = useStore(monitorConnections);  // { t, others, list: [{ id, proto, remote, cc, app… }] }
//   monitor.info()                               // { cpuModel, cores, geo, home: { lat, lon, cc } }
//
// lord-server samples in its own thread only while some useMonitor is mounted (and
// polls it): with the app closed it costs nothing. The process and connection lists are built
// only if some useMonitor asks for them, once per second.
//
// Phase 2 (not implemented yet): monitor.deviceUsers() → [{ kind: 'camera' | 'microphone',
// app, name, pid }] and the monitorDevices store; and the firewall with per-app and per-domain rules,
// which rely on the same connection records (the `verdict` field, always 'allow' today).
import { useEffect, useRef, useState } from 'react';
import { createStore, useStore } from './store.js';
import { locale } from './i18n.js';

export const MONITOR_HISTORY = 240;             // 60 s at 4 samples per second
export const monitorSamples = createStore([]);
export const monitorProcesses = createStore(null);
export const monitorConnections = createStore(null);
export const monitorDevices = createStore([]);  // phase 2

const available = () => typeof lord.monitor !== 'undefined';
const wants = new Map();                        // mounted useMonitor → { procs, conns }
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
      for (const x of c.list) x.verdict = 'allow';           // a socket that exists already passed the firewall
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
  monitorSamples.set([]);                       // the history starts over when reopened
  monitorProcesses.set(() => null);
  monitorConnections.set(() => null);
}

// geo: look up each connection's country in the local database (assets/monitor, no network).
// everyMs: how often the caller is redrawn (sampling stays every 250 ms; with
// the window in the background 1000 is better: the history loses no points and draws less).
// everyMs = 0: only requests the data, without redrawing (returns null).
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

// ---- per-app firewall (lord.fw, docs/FIREWALL.md) ----
// lord-netperm (root) applies the rules in the kernel and counts each app's new connections;
// lord-server passes them to JS. firewallState: { status, counts: { app: [allowed, denied] },
// events: [the last 200 new connections], rev }. rev goes up when a rule changes.
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
  } catch (e) { /* no lord.fw */ }
}
const fwBump = (r) => { firewallState.set((s) => ({ ...s, rev: s.rev + 1 })); fwPoll(true); return r; };
const fwCall = (fn, dflt) => { try { const api = fwApi(); return api ? fn(api) : dflt; } catch (e) { return dflt; } };
// A rule can only be applied to an app with its own scope: LordOS, a .desktop app or an .lpk.
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
  deviceUsers: () => [],                        // phase 2
};

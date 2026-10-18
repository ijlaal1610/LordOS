// alarms.js — alarmas, temporizador y cronómetro de la sesión (app Reloj, docs/SISTEMA.md).
//
// Viven en el SDK y no en la ventana del Reloj: startShell llama a startAlarms() y las alarmas
// suenan aunque la app esté cerrada (o la sesión esté sin monitor). El temporizador y el
// cronómetro también siguen al cerrar la ventana.
//
//   settings.clock = { alarms: [{ id, h, m, days: [0..6] (0 = domingo; vacío = una vez), label, on,
//                                 snooze (ms o 0), fired (ms de la última vez que sonó) }],
//                      cities: ['Europe/Madrid', …] }
//   clockAlarms.add({ h, m, days, label }) / update(id, cambios) / remove(id) / nextFire(alarma)
//   clockTimer (store) + timerStart(segundos) / timerPause() / timerResume() / timerReset()
//   stopwatch (store) + swStart() / swStop() / swLap() / swReset() / swElapsed()
//
// Sin sondeos: un solo setTimeout hasta lo próximo que tiene que sonar. Los timers del servidor
// van con CLOCK_MONOTONIC, que no avanza mientras el equipo está suspendido; por eso la espera se
// corta en tramos (como mucho 1 minuto si algo suena dentro de la hora, si no 10 minutos) y en
// cada despertar se mira la hora real: si saltó (cambio de hora, suspensión), se recalcula.
// Lo que debió sonar hace menos de 10 minutos suena igual; lo más viejo se da por perdido.
import { createElement as h, useEffect, useState } from 'react';
import { View, Text } from '@lordos/runtime';
import { createStore, useStore } from './store.js';
import { settings } from './settings.js';
import { t, formatTime } from './i18n.js';
import { notify, dismissNotification } from './notify.js';
import { sound } from './sound.js';
import { registerStatusItem } from './statusitems.js';
import { wm } from './windows.js';

const MIN = 60000, LATE = 10 * MIN, SNOOZE = 9 * MIN, RING_MS = 60000;

const conf = () => settings.get().clock || {};
const alarmsOf = () => (Array.isArray(conf().alarms) ? conf().alarms : []);
const saveAlarms = (alarms) => settings.set({ clock: { ...conf(), alarms } });

const atDay = (base, addDays, hh, mm) => { const d = new Date(base); d.setHours(0, 0, 0, 0); d.setDate(d.getDate() + addDays); d.setHours(hh, mm, 0, 0); return d.getTime(); };
const dayOk = (a, ms) => !a.days?.length || a.days.includes(new Date(ms).getDay());

// Próxima vez que suena (ms), o null si está apagada.
export function nextFire(a, now = Date.now()) {
  if (!a.on) return null;
  if (a.snooze && a.snooze > now) return a.snooze;
  for (let d = 0; d <= 7; d++) {
    const ms = atDay(now, d, a.h, a.m);
    if (ms > now && dayOk(a, ms)) return ms;
  }
  return null;
}

// La última vez que tendría que haber sonado (≤ ahora), para saber si quedó una pendiente.
function lastDue(a, now) {
  if (!a.on) return null;
  if (a.snooze) return a.snooze <= now ? a.snooze : null;
  for (let d = 0; d <= 7; d++) {
    const ms = atDay(now, -d, a.h, a.m);
    if (ms <= now && dayOk(a, ms)) return ms;
  }
  return null;
}

let nid = Date.now() % 100000;
export const clockAlarms = {
  list: alarmsOf,
  add({ h: hh = 7, m: mm = 0, days = [], label = '' }) {
    const a = { id: `a${nid++}`, h: hh, m: mm, days, label, on: true, snooze: 0, fired: Date.now() };
    saveAlarms([...alarmsOf(), a]);
    return a.id;
  },
  update(id, patch) {
    // al prenderla o cambiar la hora no tiene que sonar por algo "pendiente" de antes
    saveAlarms(alarmsOf().map((a) => (a.id === id ? { ...a, ...patch, snooze: 'snooze' in patch ? patch.snooze : 0, fired: Date.now() } : a)));
  },
  remove(id) { saveAlarms(alarmsOf().filter((a) => a.id !== id)); },
  nextFire,
};

// ---- temporizador y cronómetro (en memoria de la sesión) ----

export const clockTimer = createStore({ state: 'idle', duration: 0, endsAt: 0, left: 0 });
export function timerStart(seconds) {
  const ms = Math.max(1, Math.round(seconds)) * 1000;
  clockTimer.set({ state: 'running', duration: ms, endsAt: Date.now() + ms, left: ms });
  arm();
}
export function timerPause() {
  const s = clockTimer.get();
  if (s.state === 'running') clockTimer.set({ state: 'paused', left: Math.max(0, s.endsAt - Date.now()) });
  arm();
}
export function timerResume() {
  const s = clockTimer.get();
  if (s.state === 'paused') clockTimer.set({ state: 'running', endsAt: Date.now() + s.left });
  arm();
}
export function timerReset() { clockTimer.set({ state: 'idle', endsAt: 0, left: 0 }); arm(); }
export const timerLeft = (s = clockTimer.get()) => (s.state === 'running' ? Math.max(0, s.endsAt - Date.now()) : s.state === 'paused' ? s.left : s.duration);

export const stopwatch = createStore({ running: false, start: 0, acc: 0, laps: [] });
export const swElapsed = (s = stopwatch.get()) => s.acc + (s.running ? Date.now() - s.start : 0);
export const swStart = () => { const s = stopwatch.get(); if (!s.running) stopwatch.set({ running: true, start: Date.now() }); };
export const swStop = () => { const s = stopwatch.get(); if (s.running) stopwatch.set({ running: false, acc: swElapsed(s) }); };
export const swLap = () => { const s = stopwatch.get(); stopwatch.set({ laps: [swElapsed(s), ...s.laps].slice(0, 200) }); };
export const swReset = () => stopwatch.set({ running: false, start: 0, acc: 0, laps: [] });

// ---- sonar ----

export const ringing = createStore([]);       // ids de notificación que están sonando
function ring(notifyOpts) {
  let iv = null, stopAt = Date.now() + RING_MS;
  const id = notify({
    ...notifyOpts, sticky: true, sound: true,
    onClose: () => { clearInterval(iv); ringing.set((l) => l.filter((x) => x !== id)); },
  });
  ringing.set((l) => [...l, id]);
  iv = setInterval(() => { if (Date.now() > stopAt) { clearInterval(iv); return; } sound.play('notify'); }, 2500);
  return id;
}

function fireAlarm(a, due) {
  const label = a.label || t('clock.alarm.default');
  ring({
    app: 'clock', title: label, body: t('clock.alarm.ringing', { time: formatTime(new Date(due)) }),
    actions: [
      { id: 'stop', label: t('clock.alarm.stop'), primary: true, run: () => {} },
      { id: 'snooze', label: t('clock.alarm.snooze', { n: SNOOZE / MIN }), run: () => clockAlarms.update(a.id, { on: true, snooze: Date.now() + SNOOZE }) },
    ],
  });
}

function fireTimer() {
  clockTimer.set({ state: 'idle', endsAt: 0, left: 0 });
  ring({
    app: 'clock', title: t('clock.timer.done'), body: t('clock.timer.doneBody'),
    actions: [
      { id: 'stop', label: t('clock.alarm.stop'), primary: true, run: () => {} },
      { id: 'again', label: t('clock.timer.again'), run: () => timerStart(clockTimer.get().duration / 1000) },
    ],
  });
}

// ---- programación ----

let handle = null, started = false;
export const nextAlarm = createStore(null);    // { at, label } la próxima alarma (para la barra)

function check() {
  const now = Date.now();
  const list = alarmsOf();
  let changed = false;
  const after = list.map((a) => {
    const due = lastDue(a, now + 1000);
    if (due == null || due <= (a.fired || 0) || now - due > LATE) return a;
    fireAlarm(a, due);
    changed = true;
    const once = !a.days?.length;
    return { ...a, fired: due, snooze: 0, on: once ? false : a.on };
  });
  if (changed) saveAlarms(after);
  const tm = clockTimer.get();
  if (tm.state === 'running' && tm.endsAt <= now + 250) fireTimer();
}

export function arm() {
  clearTimeout(handle);
  if (!started) return;
  check();                 // puede volver a entrar acá (guardar las alarmas avisa a la suscripción)
  clearTimeout(handle);
  const now = Date.now();
  let next = Infinity, nextA = null;
  for (const a of alarmsOf()) {
    const ms = nextFire(a, now);
    if (ms != null && ms < next) { next = ms; nextA = a; }
  }
  nextAlarm.set(nextA ? { at: next, label: nextA.label } : null);
  const tm = clockTimer.get();
  if (tm.state === 'running') next = Math.min(next, tm.endsAt);
  if (next === Infinity) return;
  const wait = next - now;
  const cap = wait < 60 * MIN ? MIN : LATE;
  handle = setTimeout(arm, Math.max(0, Math.min(wait, cap)));
}

// La barra: ⏱ con lo que falta del temporizador, o ⏰ si hay una alarma en las próximas 24 h.
function TimerLeft({ color }) {
  const s = useStore(clockTimer);
  useTick(s.state === 'running');
  const ms = timerLeft(s);
  const sec = Math.ceil(ms / 1000);
  const txt = `${sec >= 3600 ? `${Math.floor(sec / 3600)}:` : ''}${String(Math.floor(sec / 60) % 60).padStart(sec >= 3600 ? 2 : 1, '0')}:${String(sec % 60).padStart(2, '0')}`;
  return h(Text, { style: { fontSize: 12, fontWeight: 600, color, fontFamily: 'monospace' } }, `⏱ ${txt}`);
}
// vuelve a dibujar cada segundo, solo mientras corre
function useTick(on) {
  const [, set] = useState(0);
  useEffect(() => { if (!on) return undefined; const id = setInterval(() => set((x) => x + 1), 1000); return () => clearInterval(id); }, [on]);
}

function ClockStatus({ color, height }) {
  const tm = useStore(clockTimer);
  const na = useStore(nextAlarm);
  const soon = na && na.at - Date.now() < 24 * 60 * MIN;
  if (tm.state === 'idle' && !soon) return null;
  return h(View, {
    onClick: () => wm.open('clock', { args: { tab: tm.state !== 'idle' ? 'timer' : 'alarms' } }),
    style: { height, paddingHorizontal: 7, flexDirection: 'row', alignItems: 'center', gap: 6 },
  },
  tm.state !== 'idle' ? h(TimerLeft, { color }) : null,
  soon && tm.state === 'idle' ? h(Text, { style: { fontSize: 12, color } }, `⏰ ${formatTime(new Date(na.at))}`) : null);
}

// La llama startShell una vez. Las alarmas se vuelven a programar cuando cambian.
export function startAlarms() {
  if (started) return;
  started = true;
  let last = JSON.stringify(alarmsOf());
  settings.subscribe(() => {
    const now = JSON.stringify(alarmsOf());
    if (now !== last) { last = now; arm(); }
  });
  registerStatusItem('clock', ClockStatus, 10);
  arm();
}

export { dismissNotification };

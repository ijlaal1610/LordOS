// notify.js — avisos de la sesión (como las notificaciones de macOS o GNOME).
//
//   const id = notify({ app: 'backup', title: 'Copia terminada', body: '1234 archivos',
//                       actions: [{ id: 'open', label: 'Abrir', run: () => wm.open('backup') }],
//                       sound: true, sticky: false, timeout: 6000 })
//   dismissNotification(id)
//   useNotifications() → los avisos en pantalla; notifyHistory (store) → los de esta sesión
//
// <NotificationBanner /> lo monta startShell, así que se ve con cualquier tema: arriba, del
// lado del final de la línea (a la derecha; a la izquierda en árabe o hebreo), debajo de la barra
// del tema (workArea). Un aviso se va solo a los `timeout` ms, salvo los `sticky` (alarmas), que
// esperan a que se toque una acción o la ×. Un clic en el aviso corre la primera acción.
// Los textos los pasa la app ya traducidos (t()). El historial vive solo en memoria.
import { createElement as h, useEffect, useState } from 'react';
import { View, Text } from '@lordos/runtime';
import { createStore, useStore } from './store.js';
import { useTheme } from './theme.js';
import { direction, formatTime, appName } from './i18n.js';
import { getApp, workArea } from './windows.js';
import { sound } from './sound.js';
import { screen } from '@lordos/runtime';

const MAX_SHOWN = 4, HISTORY = 50;
export const notifications = createStore([]);        // en pantalla, el más nuevo primero
export const notifyHistory = createStore([]);
let seq = 1;
const timers = new Map();

export function notify({ app = '', title = '', body = '', actions = [], sound: withSound = true, sticky = false, timeout = 6000, onClose } = {}) {
  const id = seq++;
  const n = { id, app: String(app), title: String(title), body: String(body), actions: Array.isArray(actions) ? actions.slice(0, 3) : [], sticky: !!sticky, time: Date.now(), onClose };
  notifications.set((l) => [n, ...l].slice(0, MAX_SHOWN));
  notifyHistory.set((l) => [{ id, app: n.app, title: n.title, body: n.body, time: n.time }, ...l].slice(0, HISTORY));
  if (withSound) sound.play('notify');
  if (!sticky) timers.set(id, setTimeout(() => dismissNotification(id), Math.max(1500, timeout)));
  return id;
}

export function dismissNotification(id, reason = 'closed') {
  clearTimeout(timers.get(id));
  timers.delete(id);
  const n = notifications.get().find((x) => x.id === id);
  if (!n) return;
  notifications.set((l) => l.filter((x) => x.id !== id));
  try { n.onClose?.(reason); } catch (e) { console.error('notify onClose:', e); }
}

export const useNotifications = () => useStore(notifications);
export const clearNotifyHistory = () => notifyHistory.set([]);

function runAction(n, a) {
  dismissNotification(n.id, a.id || 'action');
  try { a.run?.(); } catch (e) { console.error('notify:', e); }
}

function Banner({ n, th }) {
  const [hover, setHover] = useState(false);
  const app = n.app ? getApp(n.app) : null;
  const first = n.actions[0];
  return h(View, {
    onMouseEnter: () => setHover(true), onMouseLeave: () => setHover(false),
    onClick: () => (first ? runAction(n, first) : dismissNotification(n.id)),
    style: { width: 340, padding: 12, gap: 8, borderRadius: 14, backgroundColor: th.windowBg, border: `1px solid ${th.separator}`,
      boxShadow: '0 10px 30px rgba(0,0,0,0.4)' },
  },
  h(View, { style: { flexDirection: 'row', gap: 10, alignItems: 'flex-start' } },
    app?.Icon ? h(app.Icon, { size: 34 }) : h(View, { style: { width: 34, height: 34, borderRadius: 8, backgroundColor: th.accent } }),
    h(View, { style: { flex: 1, gap: 2 } },
      h(View, { style: { flexDirection: 'row', gap: 6 } },
        h(Text, { style: { flex: 1, fontSize: 11, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden', textAlign: 'start' } }, app ? appName(app) : ''),
        h(Text, { style: { fontSize: 11, color: th.textDim } }, formatTime(new Date(n.time)))),
      h(Text, { style: { fontSize: 13, fontWeight: 700, color: th.text, textAlign: 'start' } }, n.title),
      n.body ? h(Text, { style: { fontSize: 12, color: th.text, lineHeight: 1.35, textAlign: 'start' } }, n.body) : null),
    h(View, {
      onClick: (e) => { e.stopPropagation(); dismissNotification(n.id); },
      style: { width: 18, height: 18, borderRadius: 9, alignItems: 'center', justifyContent: 'center', backgroundColor: hover ? th.control : 'transparent' },
    }, h(Text, { style: { fontSize: 11, color: th.textDim } }, '×'))),
  n.actions.length ? h(View, { style: { flexDirection: 'row', gap: 8, justifyContent: 'flex-end' } },
    ...n.actions.map((a, i) => h(View, {
      key: a.id || i, onClick: (e) => { e.stopPropagation(); runAction(n, a); },
      style: { height: 26, paddingHorizontal: 12, borderRadius: 7, justifyContent: 'center', backgroundColor: a.primary ? th.accent : th.control },
    }, h(Text, { style: { fontSize: 12, fontWeight: 600, color: a.primary ? '#fff' : th.text } }, a.label)))) : null);
}

export function NotificationBanner() {
  const list = useNotifications();
  const th = useTheme();
  const [, force] = useState(0);
  useEffect(() => { if (list.length) force((x) => x + 1); }, [list.length]);
  if (!list.length) return null;
  const area = workArea() || { x: 0, y: 0, w: screen.width, h: screen.height };
  const rtl = direction() === 'rtl';
  const left = rtl ? (area.x || 0) + 12 : (area.x || 0) + (area.w || screen.width) - 352;
  return h(View, { style: { position: 'absolute', left, top: (area.y || 0) + 10, width: 340, gap: 10 } },
    ...list.map((n) => h(Banner, { key: n.id, n, th })));
}

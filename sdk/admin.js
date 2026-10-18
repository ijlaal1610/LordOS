// admin.js — acciones de administración por medio del ayudante privilegiado del servidor
// (server/src/admin.c). Lista cerrada: región, zona horaria, idiomas, paquetes, red y VPN (la
// tabla `actions` de admin.c). Las que no son de red solo funcionan si el usuario de la sesión
// es administrador (grupo sudo). Varias pueden correr a la vez.
//
//   await admin.run('locale-gen', 'pt_BR.UTF-8', (linea) => mostrarProgreso(linea))
//   const off = admin.onEvent((texto) => ...)    // avisos sin pedido: "net link eth0 up"...
import { createStore } from './store.js';

const pending = new Map();              // id → { action, arg, onLog, resolve, reject }
const eventListeners = new Set();
// La acción visible más reciente (para mostrar progreso): { action, arg, log: [] } o null.
export const adminBusy = createStore(null);

function refreshBusy() {
  const last = [...pending.values()].filter((p) => !p.quiet).pop();
  adminBusy.set(last ? { action: last.action, arg: last.arg, log: last.log } : null);
}

globalThis.__lordAdmin = (id, kind, text) => {
  if (id === 0) {
    for (const fn of eventListeners) { try { fn(text); } catch (e) { console.log(`admin.onEvent: ${e}`); } }
    return;
  }
  const p = pending.get(id);
  if (!p) return;
  if (kind === 'LOG') {
    p.onLog?.(text);
    p.log = [...p.log, text].slice(-200);
    if (!p.quiet) refreshBusy();
    return;
  }
  pending.delete(id);
  refreshBusy();
  if (kind === 'OK') p.resolve(text); else p.reject(new Error(text));
};

export const admin = {
  available: () => lord.admin.available(),
  // quiet: no se muestra como "en curso" (consultas rápidas, como el estado de la red).
  run(action, arg = '', onLog, { quiet = false } = {}) {
    return new Promise((resolve, reject) => {
      const id = lord.admin.request(action, String(arg));
      if (!id) { reject(new Error('el ayudante de administración no está disponible')); return; }
      pending.set(id, { action, arg: quiet ? '' : arg, onLog, resolve, reject, quiet, log: [] });   // quiet: puede llevar una contraseña
      if (!quiet) refreshBusy();
    });
  },
  onEvent(fn) { eventListeners.add(fn); return () => eventListeners.delete(fn); },
  busy: adminBusy,
};

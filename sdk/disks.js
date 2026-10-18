// disks.js — discos y particiones (app Discos; ver docs/SISTEMA.md, «Discos»).
//
//   const [lista, recargar] = useDisks()    // se actualiza sola al montar, desmontar o enchufar
//   disks.list()  disks.tools()             // lord.disks, de solo lectura y sin root
//
// Lo que cambia algo va por el ayudante con las acciones cerradas disk-* (admin.run): disk-smart,
// disk-mount, disk-unmount, disk-eject, disk-format, disk-label, disk-erase, disk-write-image y
// disk-cancel. Los avisos de cambios (mountinfo y udev, sin sondeo) solo están prendidos mientras
// alguien usa useDisks().
import { useEffect, useState } from 'react';

const listeners = new Set();
let timer = null;

globalThis.__lordDisks = () => {
  // los avisos de udev llegan de a varios: se juntan en uno
  if (timer) clearTimeout(timer);
  timer = setTimeout(() => {
    timer = null;
    for (const fn of listeners) { try { fn(); } catch (e) { console.log(`disks: ${e}`); } }
  }, 150);
};

const api = () => (typeof lord !== 'undefined' && lord.disks ? lord.disks : null);

export const disks = {
  available: () => !!api(),
  list: () => (api() ? api().list() || [] : []),
  tools: () => (api() ? api().tools() || {} : {}),
  watch(fn) {
    listeners.add(fn);
    if (listeners.size === 1) api()?.watch(true);
    return () => {
      listeners.delete(fn);
      if (!listeners.size) api()?.watch(false);
    };
  },
};

export function useDisks() {
  const [list, setList] = useState(disks.list);
  useEffect(() => disks.watch(() => setList(disks.list())), []);
  return [list, () => setList(disks.list())];
}

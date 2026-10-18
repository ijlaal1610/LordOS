// backup.js — Copias de seguridad (lord.backup, ver server/src/backup y docs/SISTEMA.md).
//
//   const st = useBackup()            estado en vivo: { state, progress, last, next, dest, … }
//   backup.config() / backup.configure({ dest, interval, pauseOnBattery, exclude })
//   backup.runNow() / pause(on) / cancel()
//   backup.list() → [{ id, time }]    backup.ls(id, rel) → [{ name, dir, link, size, mtime }]
//   backup.restore(id, rel, { replace, suffix })   ('' = todo; el resultado llega en estado.restore)
//   backup.unlock(frase, crear) / backup.lock()     copias cifradas (resultado en estado.unlock)
//
// La copia la hace un hilo de lord-server con la prioridad más baja; el estado llega solo
// (__lordBackup), sin consultar. Al terminar mal (o bien, si la pidió el usuario) se avisa con
// notify(), aunque la app esté cerrada.
import { createStore, useStore } from './store.js';
import { notify } from './notify.js';
import { t } from './i18n.js';
import { formatBytes } from './system.js';
import { wm } from './windows.js';

const has = () => typeof lord !== 'undefined' && lord.backup;
const read = () => { try { return has() ? lord.backup.status() : null; } catch (e) { return null; } };

export const backupStatus = createStore(read());
let asked = false;            // «Copiar ahora»: se avisa también si termina bien
let restoring = false;
let lockedNoticed = false;    // «desbloqueá las copias»: una vez por sesión

function changed(prev, st) {
  if (!prev || !st) return;
  const open = [{ id: 'open', label: t('backup.notify.open'), run: () => wm.open('backup') }];
  if (st.last.when && st.last.when !== prev.last.when) {
    if (st.last.ok === 1 && asked) {
      notify({ app: 'backup', title: t('backup.notify.doneTitle'),
        body: t('backup.notify.doneBody', { count: st.last.files, size: formatBytes(st.last.bytes) }), actions: open, sound: false });
    } else if (st.last.ok === 0) {
      notify({ app: 'backup', title: t('backup.notify.failTitle'), body: st.last.error || '', actions: open });
    }
    asked = false;
  }
  if (st.state === 'locked' && prev.state !== 'locked' && !lockedNoticed) {
    lockedNoticed = true;
    notify({ app: 'backup', title: t('backup.notify.lockedTitle'), body: t('backup.notify.lockedBody'), actions: open });
  }
  if (restoring && st.restore.done && !prev.restore.done) {
    restoring = false;
    notify({ app: 'backup', title: t(st.restore.ok ? 'backup.notify.restoredTitle' : 'backup.notify.restoreFailTitle'),
      body: t('backup.notify.restoredBody', { count: st.restore.files, both: st.restore.keptBoth }), actions: open, sound: false });
  }
}

globalThis.__lordBackup = (st) => {
  const prev = backupStatus.get();
  backupStatus.set(st);
  try { changed(prev, st); } catch (e) { console.log(`backup: ${e}`); }
};

export const backup = {
  available: () => !!has(),
  status: read,
  config: () => (has() ? lord.backup.config() : null),
  configure: (c) => lord.backup.configure(c),
  runNow: () => { asked = true; return lord.backup.runNow(); },
  pause: (on) => lord.backup.pause(!!on),
  cancel: () => lord.backup.cancel(),
  list: () => (has() ? lord.backup.list() || [] : []),
  ls: (id, rel = '') => lord.backup.ls(id, rel),
  path: (id) => lord.backup.path(id),
  restore: (id, rel, opts = {}) => {
    restoring = true;
    return lord.backup.restore(id, rel, { suffix: t('backup.restoredSuffix'), ...opts });
  },
  remove: (id) => lord.backup.remove(id),
  // cifrado (Monocypher): la frase se convierte en la clave en el hilo de la copia; queda solo en memoria
  unlock: (pass, create = false) => lord.backup.unlock(String(pass), !!create),
  lock: () => lord.backup.lock(),
};

export const useBackup = () => useStore(backupStatus);

// remote.js — remote desktop: who is watching this session from the network, and the on-screen notice.
//
// lord-server notifies (__lordRemote) every time someone connects or disconnects through the
// remote desktop (lord-sessiond, docs/REMOTE.md). <RemoteBadge /> is mounted by startShell: a
// fixed notice at the top right while there are connections, in every theme.
//
//   useStore(remoteViewers) → [{ ip, user }]
//   remote.status()         → { enabled, installed, port, allow, fingerprint, clients: [...], sessions: [...], monitor }
import { createElement as h } from 'react';
import { View, Text } from '@lordos/runtime';
import { createStore, useStore } from './store.js';
import { t } from './i18n.js';

export const remoteViewers = createStore([]);
globalThis.__lordRemote = (json) => {
  try { remoteViewers.set(JSON.parse(json)); } catch (e) { remoteViewers.set([]); }
};

function parseConfig(text) {
  const c = { port: 5673, allow: 'all' };
  for (const line of String(text || '').split('\n')) {
    const m = /^(\w+)=(.*)$/.exec(line.trim());
    if (m && m[1] === 'port') c.port = Number(m[2]) || 5673;
    if (m && m[1] === 'allow') c.allow = m[2] === 'group' ? 'group' : 'all';
  }
  return c;
}

export const remote = {
  available: () => typeof lord.remote !== 'undefined',
  status() {
    if (typeof lord.remote === 'undefined') return { enabled: false, installed: false, clients: [], sessions: [], monitor: '' };
    const s = lord.remote.status();
    const cfg = parseConfig(s.config);
    let st = null;
    try { st = s.status ? JSON.parse(s.status) : null; } catch (e) { st = null; }
    return {
      installed: !!s.installed, enabled: !!(st && st.running), port: st?.port || cfg.port, allow: st?.allow || cfg.allow,
      fingerprint: s.fingerprint || '', clients: st?.clients || [], sessions: st?.sessions || [], monitor: st?.monitor || '',
    };
  },
};

export function RemoteBadge() {
  const list = useStore(remoteViewers);
  if (!list.length) return null;
  const who = list.map((c) => (c.user ? `${c.user} · ${c.ip}` : c.ip)).join(', ');
  return h(View, {
    style: {
      position: 'absolute', right: 12, top: 34, flexDirection: 'row', alignItems: 'center', gap: 8, paddingHorizontal: 12, height: 28,
      borderRadius: 14, backgroundColor: 'rgba(48,209,88,0.92)', boxShadow: '0 6px 18px rgba(0,0,0,0.35)', pointerEvents: 'none',
    },
  },
  h(View, { style: { width: 8, height: 8, borderRadius: 4, backgroundColor: '#fff' } }),
  h(Text, { style: { fontSize: 12, fontWeight: 600, color: '#fff', whiteSpace: 'nowrap' } }, t('shell.remote.watching', { who })));
}

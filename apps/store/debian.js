// debian.js — la parte de Debian de la Tienda. Lee el índice propio (lord.pkg, sin root;
// server/src/pkg) y modifica por el ayudante de administración (pkg-update, pkg-install,
// pkg-remove, pkg-upgrade, pkg-install-file). Antes de cambiar algo muestra el plan: qué se
// instala, actualiza o quita, y cuánto baja y ocupa. Ver docs/PAQUETES.md.
import { createElement as h, useEffect, useState } from 'react';
import {
  View, Text, Image, Button, useTheme, useStore, createStore, admin, formatBytes, formatDate,
  refreshApps, launchApp, useLauncherApps, LetterIcon, t, locale,
} from '@lordos/sdk';

const available = () => typeof lord.pkg !== 'undefined';
const ready = () => available() && lord.pkg.available();
const lang2 = () => (locale.get().lang || 'en').split('-')[0];

// Estado compartido: generación (cambia cuando cambia el índice o lo instalado) y la tarea en curso.
const state = createStore({ gen: 0, job: null, msg: null });
const bump = () => state.set((s) => ({ ...s, gen: s.gen + 1 }));
admin.onEvent((text) => {
  if (text === 'pkg index' || text === 'pkg changed') { bump(); if (text === 'pkg changed') refreshApps(); }
});

// Corre una acción de paquetes mostrando su progreso ("progress <fase> <hecho> <total>").
function runJob(action, arg, title) {
  state.set((s) => ({ ...s, job: { title, phase: '', done: 0, total: 0, log: [] }, msg: null }));
  const onLog = (line) => {
    const m = /^progress (\w+) (\d+) (\d+)$/.exec(line);
    state.set((s) => ({ ...s, job: s.job && (m
      ? { ...s.job, phase: m[1], done: Number(m[2]), total: Number(m[3]) }
      : /^plan /.test(line) ? s.job : { ...s.job, log: [...s.job.log, line].slice(-4) }) }));
  };
  return admin.run(action, arg, onLog).then(
    (ok) => { state.set((s) => ({ ...s, job: null, msg: { ok: true, text: ok } })); bump(); refreshApps(); return true; },
    (e) => { state.set((s) => ({ ...s, job: null, msg: { ok: false, text: e.message } })); bump(); return false; },
  );
}

export const useDebian = () => {
  const s = useStore(state);
  const upgrades = ready() ? lord.pkg.upgrades() : [];
  return { ...s, upgrades };
};

// ---- piezas ----

function PkgIcon({ app, name, size = 48 }) {
  const icon = app?.icon || (available() ? lord.pkg.icon(name) : null);
  if (icon) return h(Image, { src: icon, style: { width: size, height: size, objectFit: 'contain' } });
  return h(LetterIcon, { size, name: app?.name || name });
}

function Progress() {
  const th = useTheme();
  const { job, msg } = useStore(state);
  if (!job && !msg) return null;
  if (!job) return h(Text, { style: { fontSize: 12, color: msg.ok ? '#30d158' : '#ff453a', marginBottom: 10 } }, msg.text);
  const f = job.total ? Math.min(1, job.done / job.total) : 0;
  const phase = job.phase ? t(`store.phase.${job.phase}`) : t('store.working');
  return h(View, { style: { padding: 12, borderRadius: 10, backgroundColor: th.hover, gap: 6, marginBottom: 12 } },
    h(Text, { style: { fontSize: 13, fontWeight: 600, color: th.text } }, `${job.title} — ${phase}`),
    h(View, { style: { height: 6, borderRadius: 3, backgroundColor: th.control, direction: 'ltr' } },
      h(View, { style: { width: `${Math.round(f * 100)}%`, height: 6, borderRadius: 3, backgroundColor: th.accent } })),
    job.phase === 'download' && job.total ? h(Text, { style: { fontSize: 11, color: th.textDim } }, `${formatBytes(job.done)} / ${formatBytes(job.total)}`) : null,
    ...job.log.map((l, i) => h(Text, { key: i, style: { fontSize: 11, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' } }, l)));
}

// Resumen de un plan: lo que pasa y cuánto ocupa.
function PlanView({ plan }) {
  const th = useTheme();
  if (!plan.ok) return h(Text, { style: { fontSize: 12, color: '#ff453a' } }, plan.error || t('store.plan.impossible'));
  const by = (a) => plan.items.filter((i) => i.action === a);
  const line = (label, list) => (list.length ? h(Text, { style: { fontSize: 12, color: th.text, lineHeight: 1.4 } },
    `${label}: ${list.slice(0, 14).map((i) => i.name).join(', ')}${list.length > 14 ? ` ${t('store.plan.more', { count: list.length - 14 })}` : ''}`) : null);
  const delta = plan.installedDelta;
  return h(View, { style: { gap: 4 } },
    line(t('store.plan.install', { count: by('install').length }), by('install')),
    line(t('store.plan.upgrade', { count: by('upgrade').length }), by('upgrade')),
    line(t('store.plan.remove', { count: by('remove').length }), by('remove')),
    h(Text, { style: { fontSize: 12, color: th.textDim } }, t('store.plan.sizes', {
      download: formatBytes(plan.download), disk: `${delta < 0 ? '−' : '+'}${formatBytes(Math.abs(delta))}`,
    })));
}

// ---- ficha de un paquete ----

function Details({ name, onBack }) {
  const th = useTheme();
  const { gen, job } = useStore(state);
  const [plan, setPlan] = useState(null);
  const info = ready() ? lord.pkg.info(name, { lang: lang2() }) : null;
  useEffect(() => setPlan(null), [name, gen]);
  const launcher = useLauncherApps();
  if (!info) return h(Text, { style: { color: th.textDim } }, t('store.debian.notFound', { name }));
  const app = info.app;
  const isAdmin = admin.available();
  // la app de escritorio instalada que corresponde (para "Abrir")
  const desk = app && launcher.find((a) => a.kind === 'linux' && (`${a.desktopId}.desktop` === app.id || a.desktopId === app.id));
  const ask = (p) => setPlan(p);
  const doInstall = () => ask({ kind: 'install', p: lord.pkg.plan({ install: [name] }) });
  const doRemove = () => ask({ kind: 'remove', p: lord.pkg.plan({ remove: [name] }) });
  const confirm = () => {
    const extra = plan.p.items.some((i) => i.action === 'remove' && !i.requested);
    if (plan.kind === 'install') runJob('pkg-install', extra ? `${name} --allow-remove` : name, t('store.job.install', { name: app?.name || name }));
    else runJob('pkg-remove', extra ? `${name} --cascade` : name, t('store.job.remove', { name: app?.name || name }));
    setPlan(null);
  };
  const desc = (info.appDescription || info.description || '').trim();
  return h(View, { style: { gap: 12 } },
    h(View, { onClick: onBack, style: { alignSelf: 'flex-start', paddingVertical: 4 } }, h(Text, { style: { fontSize: 13, color: th.accent } }, `‹ ${t('store.back')}`)),
    h(Progress),
    h(View, { style: { flexDirection: 'row', gap: 16, alignItems: 'center' } },
      h(PkgIcon, { app, name, size: 72 }),
      h(View, { style: { flex: 1, gap: 4 } },
        h(Text, { style: { fontSize: 20, fontWeight: 700, color: th.text } }, app?.name || name),
        h(Text, { style: { fontSize: 13, color: th.textDim } }, app?.summary || info.summary),
        h(Text, { style: { fontSize: 11, color: th.textDim } }, t('store.debian.meta', {
          name, version: info.version, size: formatBytes(info.installedSize), suite: info.suite || '',
        }))),
      h(View, { style: { gap: 8, alignItems: 'flex-end' } },
        !info.installed && h(Button, { primary: true, label: t('store.install'), onClick: () => isAdmin && !job && doInstall() }),
        info.installed && desk && h(Button, { primary: true, label: t('store.open'), onClick: () => launchApp(desk.id) }),
        info.upgradable && h(Button, { label: t('store.upgrade'), onClick: () => isAdmin && !job && doInstall() }),
        info.installed && !info.essential && h(Button, { label: t('store.remove'), onClick: () => isAdmin && !job && doRemove() }))),
    !isAdmin && h(Text, { style: { fontSize: 11, color: '#ff9f0a' } }, t('store.debian.needAdmin')),
    plan && h(View, { style: { padding: 12, borderRadius: 10, backgroundColor: th.hover, gap: 8 } },
      h(Text, { style: { fontSize: 13, fontWeight: 600, color: th.text } }, t(plan.kind === 'install' ? 'store.plan.titleInstall' : 'store.plan.titleRemove')),
      h(PlanView, { plan: plan.p }),
      h(View, { style: { flexDirection: 'row', gap: 8 } },
        plan.p.ok && h(Button, { primary: true, label: t('store.plan.confirm'), onClick: confirm }),
        h(Button, { label: t('store.cancel'), onClick: () => setPlan(null) }))),
    info.installed && h(Text, { style: { fontSize: 12, color: th.textDim } }, t('store.debian.installedVersion', { version: info.installed })),
    desc ? h(Text, { style: { fontSize: 13, color: th.text, lineHeight: 1.45 } }, desc.slice(0, 1600)) : null,
    info.homepage ? h(Text, { style: { fontSize: 11, color: th.textDim } }, info.homepage) : null);
}

// ---- grilla de apps ----

function Card({ a, onOpen }) {
  const th = useTheme();
  return h(View, { onClick: onOpen, style: { width: 196, padding: 10, gap: 6, borderRadius: 10, backgroundColor: th.hover, flexDirection: 'row', alignItems: 'center' } },
    h(PkgIcon, { app: a.app || a, name: a.package || a.name, size: 44 }),
    h(View, { style: { flex: 1, gap: 2 } },
      h(Text, { style: { fontSize: 13, fontWeight: 600, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' } }, (a.app || a).name || a.name),
      h(Text, { style: { fontSize: 11, color: th.textDim, maxHeight: 28, overflow: 'hidden', lineHeight: 1.25 } }, (a.app || a).summary || a.summary || ''),
      a.installed ? h(Text, { style: { fontSize: 10, color: '#30d158' } }, t('store.installedBadge')) : null));
}

const Grid = ({ items, onOpen }) => h(View, { style: { flexDirection: 'row', flexWrap: 'wrap', gap: 10 } },
  ...items.map((a) => h(Card, { key: `${a.package || a.name}:${a.id || ''}`, a, onOpen: () => onOpen(a.package || a.name) })));

const Chips = ({ items }) => h(View, { style: { flexDirection: 'row', flexWrap: 'wrap', gap: 6 } }, ...items);

const CATS = ['AudioVideo', 'Development', 'Education', 'Game', 'Graphics', 'Network', 'Office', 'Science', 'System', 'Utility'];

function Discover({ query }) {
  const th = useTheme();
  const { gen } = useStore(state);
  const [cat, setCat] = useState('Utility');
  const [open, setOpen] = useState(null);
  useEffect(() => setOpen(null), [query]);
  if (!available()) return h(Text, { style: { fontSize: 13, color: th.textDim } }, t('store.debian.unavailable'));
  if (!ready()) {
    return h(View, { style: { gap: 10 } }, h(Progress),
      h(Text, { style: { fontSize: 13, color: th.text } }, t('store.debian.noIndex')),
      h(Button, { primary: true, label: t('store.debian.update'), onClick: () => runJob('pkg-update', '', t('store.job.update')) }));
  }
  if (open) return h(Details, { name: open, onBack: () => setOpen(null) });
  const q = query.trim();
  void gen;
  if (q) {
    const res = lord.pkg.search(q, { limit: 60, lang: lang2() });
    const apps = res.filter((r) => r.app), others = res.filter((r) => !r.app);
    return h(View, { style: { gap: 10 } },
      h(Progress),
      h(Text, { style: { fontSize: 20, fontWeight: 700, color: th.text } }, t('store.debian.results', { q })),
      apps.length ? h(Grid, { items: apps.map((r) => ({ ...r, package: r.name })), onOpen: setOpen }) : null,
      others.length ? h(Text, { style: { fontSize: 11, fontWeight: 600, color: th.textDim, marginTop: 8 } }, t('store.debian.otherPackages')) : null,
      ...others.slice(0, 30).map((r) => h(View, { key: r.name, onClick: () => setOpen(r.name), style: { flexDirection: 'row', gap: 10, paddingVertical: 6 } },
        h(Text, { style: { width: 200, fontSize: 12, fontWeight: 600, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' } }, r.name),
        h(Text, { style: { flex: 1, fontSize: 12, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' } }, r.summary),
        r.installed ? h(Text, { style: { fontSize: 11, color: '#30d158' } }, t('store.installedBadge')) : null)),
      res.length === 0 ? h(Text, { style: { fontSize: 13, color: th.textDim } }, t('store.debian.none')) : null);
  }
  const counts = lord.pkg.categories();
  const list = lord.pkg.apps({ category: cat, limit: 60, lang: lang2() });
  return h(View, { style: { gap: 12 } },
    h(Progress),
    h(Text, { style: { fontSize: 22, fontWeight: 700, color: th.text } }, t('store.discover.title')),
    h(Chips, { items: CATS.filter((c) => counts[c]).map((c) => h(View, { key: c, onClick: () => setCat(c), style: {
        paddingHorizontal: 10, paddingVertical: 5, borderRadius: 14, backgroundColor: c === cat ? th.accent : th.control } },
      h(Text, { style: { fontSize: 12, color: c === cat ? '#fff' : th.text } }, `${t(`store.cat.${c}`)} · ${counts[c]}`))) }),
    h(Grid, { items: list, onOpen: setOpen }));
}

function Updates() {
  const th = useTheme();
  const { job } = useStore(state);
  const [plan, setPlan] = useState(null);
  if (!ready()) return h(Text, { style: { fontSize: 13, color: th.textDim } }, available() ? t('store.debian.noIndex') : t('store.debian.unavailable'));
  const ups = lord.pkg.upgrades();
  return h(View, { style: { gap: 10 } },
    h(Progress),
    h(View, { style: { flexDirection: 'row', alignItems: 'center' } },
      h(Text, { style: { flex: 1, fontSize: 22, fontWeight: 700, color: th.text } }, t('store.updates.title')),
      h(Button, { label: t('store.debian.update'), onClick: () => !job && runJob('pkg-update', '', t('store.job.update')) }),
      ups.length ? h(Button, { primary: true, label: t('store.updates.all'), style: { marginLeft: 8 }, onClick: () => !job && setPlan(lord.pkg.plan({ upgrade: true })) }) : null),
    plan && h(View, { style: { padding: 12, borderRadius: 10, backgroundColor: th.hover, gap: 8 } },
      h(PlanView, { plan }),
      h(View, { style: { flexDirection: 'row', gap: 8 } },
        plan.ok && h(Button, { primary: true, label: t('store.plan.confirm'), onClick: () => {
          const extra = plan.items.some((i) => i.action === 'remove');
          runJob('pkg-upgrade', extra ? '--allow-remove' : '', t('store.job.upgrade')); setPlan(null);
        } }),
        h(Button, { label: t('store.cancel'), onClick: () => setPlan(null) }))),
    ups.length === 0 ? h(Text, { style: { fontSize: 13, color: th.textDim } }, t('store.updates.none')) : null,
    ...ups.map((u) => h(View, { key: u.name, style: { flexDirection: 'row', alignItems: 'center', gap: 10, paddingVertical: 6 } },
      h(PkgIcon, { name: u.name, size: 28 }),
      h(View, { style: { flex: 1 } },
        h(Text, { style: { fontSize: 13, fontWeight: 600, color: th.text } }, u.name),
        h(Text, { style: { fontSize: 11, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' } }, `${u.from} → ${u.to} · ${u.summary}`)),
      h(Text, { style: { fontSize: 11, color: th.textDim } }, formatBytes(u.size)))));
}

// Pie de la barra lateral: cuándo se revisó la lista por última vez.
function Status() {
  const th = useTheme();
  useStore(state);
  if (!ready()) return null;
  const at = lord.pkg.lastUpdate();
  return h(Text, { style: { fontSize: 10, color: th.textDim, lineHeight: 1.35 } },
    at ? t('store.debian.lastUpdate', { date: formatDate(new Date(at * 1000), 'short') }) : '');
}

// Paquete de Debian que trae una app Linux instalada (para "Quitar" en Instaladas).
function packageOfApp(a) {
  if (!ready()) return null;
  const want = new Set([a.desktopId, `${a.desktopId}.desktop`]);
  return lord.pkg.installed({}).find((p) => p.app && want.has(p.app.id))?.name || null;
}

export const debian = {
  available: ready,
  Discover,
  Updates,
  Status,
  installFile: (path) => runJob('pkg-install-file', path, t('store.job.installFile')).then((ok) => { if (!ok) throw new Error(state.get().msg?.text || ''); }),
  removeApp(a) {
    const name = packageOfApp(a);
    if (name) runJob('pkg-remove', name, t('store.job.remove', { name: a.name }));
  },
  canRemove: (a) => !!packageOfApp(a),
  Progress,
};

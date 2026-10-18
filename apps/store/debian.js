// debian.js — the Debian part of the Store. It reads our own index (lord.pkg, no root;
// server/src/pkg) and makes changes through the administration helper (pkg-update, pkg-install,
// pkg-remove, pkg-upgrade, pkg-install-file). Before changing anything it shows the plan: what is
// installed, upgraded or removed, and how much is downloaded and used. See docs/PACKAGES.md.
import { createElement as h, useEffect, useState } from 'react';
import {
  View, Text, Image, Button, EmptyState, GroupBox, useTheme, useStore, createStore, admin, formatBytes, formatDate,
  refreshApps, launchApp, useLauncherApps, LetterIcon, typeScale, spacing, direction, t, locale, errorText, statusText,
} from '@lordos/sdk';

const available = () => typeof lord.pkg !== 'undefined';
const ready = () => available() && lord.pkg.available();
const lang2 = () => (locale.get().lang || 'en').split('-')[0];

// Shared state: generation (changes when the index or what is installed changes) and the running job.
const state = createStore({ gen: 0, job: null, msg: null });
const bump = () => state.set((s) => ({ ...s, gen: s.gen + 1 }));
admin.onEvent((text) => {
  if (text === 'pkg index' || text === 'pkg changed') { bump(); if (text === 'pkg changed') refreshApps(); }
});

// Runs a package action showing its progress ("progress <phase> <done> <total>"). The other LOG
// lines and the final OK are status code lines (server/src/pkg/pkg.c), shown with statusText().
function runJob(action, arg, title) {
  state.set((s) => ({ ...s, job: { title, phase: '', done: 0, total: 0, log: [] }, msg: null }));
  const onLog = (line) => {
    const m = /^progress (\w+) (\d+) (\d+)$/.exec(line);
    state.set((s) => ({ ...s, job: s.job && (m
      ? { ...s.job, phase: m[1], done: Number(m[2]), total: Number(m[3]) }
      : /^plan /.test(line) ? s.job : { ...s.job, log: [...s.job.log, statusText(line)].slice(-4) }) }));
  };
  return admin.run(action, arg, onLog).then(
    (ok) => { state.set((s) => ({ ...s, job: null, msg: { ok: true, text: statusText(ok) } })); bump(); refreshApps(); return true; },
    (e) => { state.set((s) => ({ ...s, job: null, msg: { ok: false, text: e.message } })); bump(); return false; },
  );
}

export const useDebian = () => {
  const s = useStore(state);
  const upgrades = ready() ? lord.pkg.upgrades() : [];
  return { ...s, upgrades };
};

// ---- pieces ----

// A rounded inset box (plan, progress), like GroupBox without rows.
const boxStyle = (th, extra) => ({ padding: spacing.xl, borderRadius: 10, backgroundColor: th.mode === 'dark' ? 'rgba(255,255,255,0.04)' : 'rgba(0,0,0,0.025)',
  border: `1px solid ${th.separator}`, gap: spacing.s, ...extra });

// A key/value row of an info box: the label at the start, the value right-aligned and dim.
const InfoRow = ({ th, label, value }) => h(View, { style: { minHeight: 34, flexDirection: 'row', alignItems: 'center', gap: spacing.m, paddingVertical: 4 } },
  h(Text, { style: { fontSize: typeScale.body, color: th.text, whiteSpace: 'nowrap', textAlign: 'start' } }, label),
  h(Text, { style: { flex: 1, minWidth: 0, fontSize: typeScale.body, color: th.textDim, textAlign: 'end', whiteSpace: 'nowrap', overflow: 'hidden' } }, value));

// A section header (13/600, sentence case) above an inset box.
const Header = ({ th, children }) => h(Text, { style: { fontSize: typeScale.body, fontWeight: 600, color: th.text, paddingHorizontal: spacing.xs, marginBottom: 6, textAlign: 'start' } }, children);
const titleStyle = (th) => ({ fontSize: typeScale.largeTitle, fontWeight: 600, color: th.text });

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
  return h(View, { style: boxStyle(th, { gap: 6, marginBottom: 4 }) },
    h(Text, { style: { fontSize: 13, fontWeight: 600, color: th.text } }, `${job.title} — ${phase}`),
    h(View, { style: { height: 6, borderRadius: 3, backgroundColor: th.control, direction: 'ltr' } },
      h(View, { style: { width: `${Math.round(f * 100)}%`, height: 6, borderRadius: 3, backgroundColor: th.accent } })),
    job.phase === 'download' && job.total ? h(Text, { style: { fontSize: 11, color: th.textDim } }, `${formatBytes(job.done)} / ${formatBytes(job.total)}`) : null,
    ...job.log.map((l, i) => h(Text, { key: i, style: { fontSize: 11, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' } }, l)));
}

// Summary of a plan: what happens and how much space it takes.
function PlanView({ plan }) {
  const th = useTheme();
  if (!plan.ok) return h(Text, { style: { fontSize: 12, color: '#ff453a' } }, (plan.error && errorText(plan.error)) || t('store.plan.impossible'));
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

// ---- package page ----

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
  // the matching installed desktop app (for "Open")
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
  const busy = !!job;
  const primary = !info.installed
    ? h(Button, { primary: true, label: t('store.install'), disabled: !isAdmin || busy, onClick: () => isAdmin && !job && doInstall() })
    : desk ? h(Button, { primary: true, label: t('store.open'), onClick: () => launchApp(desk.id) }) : null;
  return h(View, { style: { gap: spacing.xl } },
    h(Button, { plain: true, small: true, label: `‹ ${t('store.back')}`, onClick: onBack, style: { alignSelf: 'flex-start', [direction() === 'rtl' ? 'marginRight' : 'marginLeft']: -6 } }),
    h(Progress),
    // header: icon, name, summary and the actions under them (App Store style)
    h(View, { style: { flexDirection: 'row', gap: spacing.l, alignItems: 'flex-start' } },
      h(View, { style: { borderRadius: 16, overflow: 'hidden', boxShadow: '0 2px 8px rgba(0,0,0,0.25)' } }, h(PkgIcon, { app, name, size: 72 })),
      h(View, { style: { flex: 1, minWidth: 0, gap: spacing.xs } },
        h(Text, { style: { ...titleStyle(th), textAlign: 'start' } }, app?.name || name),
        h(Text, { style: { fontSize: typeScale.body, color: th.textDim, lineHeight: 1.35, textAlign: 'start' } }, app?.summary || info.summary),
        h(View, { style: { flexDirection: 'row', flexWrap: 'wrap', alignItems: 'center', gap: spacing.s, marginTop: spacing.s } },
          primary,
          info.upgradable && h(Button, { label: t('store.upgrade'), disabled: !isAdmin || busy, onClick: () => isAdmin && !job && doInstall() }),
          info.installed && !info.essential && h(Button, { label: t('store.remove'), disabled: !isAdmin || busy, onClick: () => isAdmin && !job && doRemove() }),
          info.installed ? h(Text, { style: { fontSize: typeScale.caption, fontWeight: 500, color: '#30d158', marginLeft: spacing.xs } }, t('store.installedBadge')) : null))),
    !isAdmin && h(Text, { style: { fontSize: typeScale.caption, color: '#ff9f0a', textAlign: 'start' } }, t('store.debian.needAdmin')),
    plan && h(View, { style: boxStyle(th) },
      h(Text, { style: { fontSize: typeScale.headline, fontWeight: 600, color: th.text, textAlign: 'start' } }, t(plan.kind === 'install' ? 'store.plan.titleInstall' : 'store.plan.titleRemove')),
      h(PlanView, { plan: plan.p }),
      h(View, { style: { flexDirection: 'row', gap: spacing.s, justifyContent: 'flex-end', marginTop: spacing.m } },
        h(Button, { label: t('store.cancel'), onClick: () => setPlan(null) }),
        plan.p.ok && h(Button, { primary: true, label: t('store.plan.confirm'), onClick: confirm }))),
    desc ? h(View, null,
      h(Header, { th }, t('store.info.description')),
      h(Text, { style: { fontSize: typeScale.body, color: th.text, lineHeight: 1.45, maxWidth: 620, paddingHorizontal: spacing.xs, textAlign: 'start' } }, desc.slice(0, 1600))) : null,
    h(View, null,
      h(Header, { th }, t('store.info.title')),
      h(GroupBox, null,
        h(InfoRow, { th, label: t('store.info.package'), value: name }),
        h(InfoRow, { th, label: t('store.info.version'), value: info.version }),
        info.installed ? h(InfoRow, { th, label: t('store.info.installed'), value: info.installed }) : null,
        h(InfoRow, { th, label: t('store.info.size'), value: formatBytes(info.installedSize) }),
        info.suite ? h(InfoRow, { th, label: t('store.info.suite'), value: `Debian ${info.suite}` }) : null,
        info.homepage ? h(InfoRow, { th, label: t('store.info.homepage'), value: info.homepage }) : null)));
}

// ---- app grid ----

function Card({ a, onOpen }) {
  const th = useTheme();
  return h(View, { onClick: onOpen, style: { width: 200, padding: 12, gap: 10, borderRadius: 10, backgroundColor: th.hover, border: `1px solid ${th.separator}`,
    flexDirection: 'row', alignItems: 'center' } },
    h(PkgIcon, { app: a.app || a, name: a.package || a.name, size: 44 }),
    h(View, { style: { flex: 1, minWidth: 0, gap: 2 } },
      h(Text, { style: { fontSize: 13, fontWeight: 600, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' } }, (a.app || a).name || a.name),
      h(Text, { style: { fontSize: 11, color: th.textDim, maxHeight: 28, overflow: 'hidden', lineHeight: 1.25 } }, (a.app || a).summary || a.summary || ''),
      a.installed ? h(Text, { style: { fontSize: 11, fontWeight: 500, color: '#30d158' } }, t('store.installedBadge')) : null));
}

const Grid = ({ items, onOpen }) => h(View, { style: { flexDirection: 'row', flexWrap: 'wrap', gap: 12 } },
  ...items.map((a) => h(Card, { key: `${a.package || a.name}:${a.id || ''}`, a, onOpen: () => onOpen(a.package || a.name) })));

const Chips = ({ items }) => h(View, { style: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 } }, ...items);

// A drawn bag, monochrome, for the Store's empty states.
function StoreGlyph() {
  const th = useTheme();
  const c = th.textDim;
  return h(View, { style: { width: 48, height: 48, marginBottom: 4 } },
    h(View, { style: { position: 'absolute', left: 15, top: 2, width: 18, height: 16, borderRadius: 9, border: `3px solid ${c}` } }),
    h(View, { style: { position: 'absolute', left: 4, top: 12, width: 40, height: 34, borderRadius: 7, backgroundColor: c } }));
}

const CATS = ['AudioVideo', 'Development', 'Education', 'Game', 'Graphics', 'Network', 'Office', 'Science', 'System', 'Utility'];

function Discover({ query }) {
  const th = useTheme();
  const { gen } = useStore(state);
  const [cat, setCat] = useState('Utility');
  const [open, setOpen] = useState(null);
  useEffect(() => setOpen(null), [query]);
  if (!available()) return h(EmptyState, { mascot: 'confused', title: t('store.debian.unavailable'), style: { minHeight: 360 } });
  if (!ready()) {
    return h(View, { style: { gap: 12 } }, h(Progress),
      h(EmptyState, { icon: h(StoreGlyph), title: t('store.debian.noIndexTitle'), hint: t('store.debian.noIndex'), style: { minHeight: 380 },
        action: h(Button, { primary: true, label: t('store.debian.update'), onClick: () => runJob('pkg-update', '', t('store.job.update')) }) }));
  }
  if (open) return h(Details, { name: open, onBack: () => setOpen(null) });
  const q = query.trim();
  void gen;
  if (q) {
    const res = lord.pkg.search(q, { limit: 60, lang: lang2() });
    const apps = res.filter((r) => r.app), others = res.filter((r) => !r.app);
    return h(View, { style: { gap: spacing.l } },
      h(Progress),
      h(Text, { style: { ...titleStyle(th), textAlign: 'start' } }, t('store.debian.results', { q })),
      apps.length ? h(Grid, { items: apps.map((r) => ({ ...r, package: r.name })), onOpen: setOpen }) : null,
      others.length ? h(View, { style: { marginTop: spacing.s } },
        h(Header, { th }, t('store.debian.otherPackages')),
        h(GroupBox, null, ...others.slice(0, 30).map((r) => h(View, { key: r.name, onClick: () => setOpen(r.name),
          style: { flexDirection: 'row', alignItems: 'center', gap: spacing.m, minHeight: 44, paddingVertical: 6 } },
          h(View, { style: { flex: 1, minWidth: 0, gap: 2 } },
            h(Text, { style: { fontSize: typeScale.body, fontWeight: 600, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden', textAlign: 'start' } }, r.name),
            h(Text, { style: { fontSize: typeScale.callout, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden', textAlign: 'start' } }, r.summary)),
          r.installed ? h(Text, { style: { fontSize: typeScale.caption, fontWeight: 500, color: '#30d158' } }, t('store.installedBadge')) : null,
          h(Text, { style: { fontSize: 15, color: th.textDim } }, direction() === 'rtl' ? '‹' : '›'))))) : null,
      res.length === 0 ? h(EmptyState, { icon: h(StoreGlyph), title: t('store.debian.none'), hint: t('store.debian.noneHint'), style: { minHeight: 300 } }) : null);
  }
  const counts = lord.pkg.categories();
  const list = lord.pkg.apps({ category: cat, limit: 60, lang: lang2() });
  return h(View, { style: { gap: 16 } },
    h(Progress),
    h(Text, { style: titleStyle(th) }, t('store.discover.title')),
    h(Chips, { items: CATS.filter((c) => counts[c]).map((c) => h(View, { key: c, onClick: () => setCat(c), style: {
        paddingHorizontal: 12, height: 26, justifyContent: 'center', borderRadius: 13, backgroundColor: c === cat ? th.accent : th.control } },
      h(Text, { style: { fontSize: 12, color: c === cat ? '#fff' : th.text } }, `${t(`store.cat.${c}`)} · ${counts[c]}`))) }),
    h(Grid, { items: list, onOpen: setOpen }));
}

function Updates() {
  const th = useTheme();
  const { job } = useStore(state);
  const [plan, setPlan] = useState(null);
  if (!ready()) {
    return available()
      ? h(EmptyState, { icon: h(StoreGlyph), title: t('store.debian.noIndexTitle'), hint: t('store.debian.noIndex'), style: { minHeight: 380 },
        action: h(Button, { primary: true, label: t('store.debian.update'), onClick: () => !job && runJob('pkg-update', '', t('store.job.update')) }) })
      : h(EmptyState, { mascot: 'confused', title: t('store.debian.unavailable'), style: { minHeight: 360 } });
  }
  const ups = lord.pkg.upgrades();
  return h(View, { style: { gap: 12 } },
    h(Progress),
    h(View, { style: { flexDirection: 'row', alignItems: 'center', gap: 8 } },
      h(Text, { style: { flex: 1, ...titleStyle(th) } }, t('store.updates.title')),
      h(Button, { label: t('store.debian.update'), onClick: () => !job && runJob('pkg-update', '', t('store.job.update')) }),
      ups.length ? h(Button, { primary: true, label: t('store.updates.all'), onClick: () => !job && setPlan(lord.pkg.plan({ upgrade: true })) }) : null),
    plan && h(View, { style: boxStyle(th) },
      h(PlanView, { plan }),
      h(View, { style: { flexDirection: 'row', gap: 8, justifyContent: 'flex-end', marginTop: 4 } },
        h(Button, { label: t('store.cancel'), onClick: () => setPlan(null) }),
        plan.ok && h(Button, { primary: true, label: t('store.plan.confirm'), onClick: () => {
          const extra = plan.items.some((i) => i.action === 'remove');
          runJob('pkg-upgrade', extra ? '--allow-remove' : '', t('store.job.upgrade')); setPlan(null);
        } }))),
    ups.length === 0 ? h(EmptyState, { mascot: 'celebrate', title: t('store.updates.none'), style: { minHeight: 300 } }) : null,
    ...ups.map((u) => h(View, { key: u.name, style: { flexDirection: 'row', alignItems: 'center', gap: 12, paddingVertical: 8 } },
      h(PkgIcon, { name: u.name, size: 32 }),
      h(View, { style: { flex: 1, minWidth: 0, gap: 2 } },
        h(Text, { style: { fontSize: 13, fontWeight: 600, color: th.text } }, u.name),
        h(Text, { style: { fontSize: 11, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' } }, `${u.from} → ${u.to} · ${u.summary}`)),
      h(Text, { style: { fontSize: 11, color: th.textDim } }, formatBytes(u.size)))));
}

// Sidebar footer: when the list was last checked.
function Status() {
  const th = useTheme();
  useStore(state);
  if (!ready()) return null;
  const at = lord.pkg.lastUpdate();
  return h(Text, { style: { fontSize: 11, color: th.textDim, lineHeight: 1.35 } },
    at ? t('store.debian.lastUpdate', { date: formatDate(new Date(at * 1000), 'short') }) : '');
}

// Debian package that brings an installed Linux app (for "Remove" in Installed).
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

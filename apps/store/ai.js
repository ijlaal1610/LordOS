// ai.js — the Store's Artificial Intelligence category (0.8.5): LordOS's curated list of AI apps
// (apt/catalog.json, bundled at build time). An app whose maker has a signed APT repository
// ("delivery": "direct") comes from that repository: the first install adds it, with the maker's key
// whose fingerprint the catalog pins (pkg-source-add, server/src/pkg/sources_admin.c), over HTTPS. The
// others come from LordOS's own signed repository (lordos.org/apt), built or copied by LordOS from
// verified sources (docs/UPDATES.md, "Mirrored apps (catalog)"). Installing goes through the package
// engine like any Debian package: the plan first, then pkg-install.
import { createElement as h, useState } from 'react';
import { View, Text, Button, GroupBox, useTheme, useStore, useSystemInfo, admin, locale, typeScale, spacing, t } from '@lordos/sdk';
import catalog from '../../apt/catalog.json';
import { debian } from './debian.js';

const lang2 = () => (locale.get().lang || 'en').split('-')[0];
// the kernel's machine name as Debian names the architecture
const DEB_ARCH = { x86_64: 'amd64', aarch64: 'arm64' };
const summary = (a) => (a.summary && (a.summary[lang2()] || a.summary.en)) || '';
const installed = (pkg) => { try { return !!(lord.pkg.info(pkg, {}) || {}).installed; } catch (e) { return false; } };
const offered = (pkg) => { try { return !!lord.pkg.info(pkg, {}); } catch (e) { return false; } };

function Row({ a, onOpen, here }) {
  const th = useTheme();
  const fits = !here || !a.arches || a.arches.includes(here);
  const inst = installed(a.package);
  return h(View, { onClick: () => fits && onOpen(a.package), style: { flexDirection: 'row', alignItems: 'center', gap: spacing.m, paddingVertical: spacing.s, opacity: fits ? 1 : 0.5 } },
    h(View, { style: { width: 40, height: 40, borderRadius: 10, backgroundColor: th.accent, alignItems: 'center', justifyContent: 'center' } },
      h(Text, { style: { fontSize: 18, fontWeight: 700, color: '#fff' } }, (a.name || '?').slice(0, 1))),
    h(View, { style: { flex: 1, minWidth: 0, gap: 2 } },
      h(Text, { style: { fontSize: typeScale.body, fontWeight: 600, color: th.text, textAlign: 'start' } }, a.name),
      h(Text, { style: { fontSize: typeScale.callout, color: th.textDim, textAlign: 'start' } }, summary(a)),
      h(Text, { style: { fontSize: typeScale.caption, color: th.textDim, textAlign: 'start' } },
        [t(`store.ai.kind.${a.kind}`), a.vendor, a.license, !fits ? t('store.ai.notForArch', { arch: here }) : null].filter(Boolean).join(' · '))),
    inst ? h(Text, { style: { fontSize: typeScale.caption, fontWeight: 500, color: '#30d158' } }, t('store.installedBadge'))
      : h(Button, { small: true, label: t('store.ai.view'), disabled: !fits, onClick: () => onOpen(a.package) }));
}

// An app from its maker's repository that isn't in the package lists yet: say whose repository is
// added, then add it and fetch the lists (both admin actions), and show the usual details.
function AddSource({ a, onDone, onBack }) {
  const th = useTheme();
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');
  const add = () => {
    setBusy(true);
    setErr('');
    admin.run('pkg-source-add', a.id).then(() => admin.run('pkg-update')).then(onDone, (e) => setErr(e.message)).finally(() => setBusy(false));
  };
  return h(View, { style: { gap: spacing.l, maxWidth: 640 } },
    h(Button, { plain: true, small: true, label: `‹ ${t('store.back')}`, onClick: onBack, style: { alignSelf: 'flex-start' } }),
    h(Text, { style: { fontSize: typeScale.largeTitle, fontWeight: 600, color: th.text, textAlign: 'start' } }, a.name),
    h(Text, { style: { fontSize: typeScale.body, color: th.text, lineHeight: 1.45, textAlign: 'start' } },
      t('store.ai.sourceIntro', { name: a.name, vendor: a.vendor || '', url: (a.source && a.source.url) || '' })),
    h(Text, { style: { fontSize: 11, color: th.textDim, fontFamily: 'monospace', direction: 'ltr', textAlign: 'start' } },
      `${(a.source && a.source.url) || ''}\n${(a.source && a.source.fingerprint) || ''}`),
    err ? h(Text, { style: { fontSize: typeScale.caption, color: '#ff453a', textAlign: 'start' } }, err) : null,
    h(View, { style: { flexDirection: 'row', gap: spacing.s } },
      h(Button, { primary: true, label: busy ? t('store.ai.adding') : t('store.ai.addSource'), disabled: busy || !admin.available(), onClick: add })));
}

// A LordOS-built app that isn't in the package lists yet (they were fetched before the catalog had it):
// fetching them again (pkg-update) brings it, instead of the package page's "not found".
function NotListed({ a, onDone, onBack }) {
  const th = useTheme();
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');
  const update = () => {
    setBusy(true);
    setErr('');
    admin.run('pkg-update').then(onDone, (e) => setErr(e.message)).finally(() => setBusy(false));
  };
  return h(View, { style: { gap: spacing.l, maxWidth: 640 } },
    h(Button, { plain: true, small: true, label: `‹ ${t('store.back')}`, onClick: onBack, style: { alignSelf: 'flex-start' } }),
    h(Text, { style: { fontSize: typeScale.largeTitle, fontWeight: 600, color: th.text, textAlign: 'start' } }, a.name),
    h(Text, { style: { fontSize: typeScale.body, color: th.text, lineHeight: 1.45, textAlign: 'start' } }, t('store.ai.notListed', { name: a.name })),
    err ? h(Text, { style: { fontSize: typeScale.caption, color: '#ff453a', textAlign: 'start' } }, err) : null,
    h(View, { style: { flexDirection: 'row', gap: spacing.s } },
      h(Button, { primary: true, label: busy ? t('store.ai.updating') : t('store.ai.updateLists'), disabled: busy || !admin.available(), onClick: update })));
}

export function AiPage({ query }) {
  const th = useTheme();
  useStore(locale);
  const [open, setOpen] = useState(null);
  const info = useSystemInfo(60000);
  const here = DEB_ARCH[info.arch] || info.arch || '';
  const [, setTick] = useState(0);
  if (open) {
    const a = (catalog.apps || []).find((x) => x.package === open);
    const direct = a && a.source && a.source.delivery === 'direct';
    if (direct && !offered(open)) return h(AddSource, { a, onBack: () => setOpen(null), onDone: () => setTick((x) => x + 1) });
    if (a && !direct && !offered(open)) return h(NotListed, { a, onBack: () => setOpen(null), onDone: () => setTick((x) => x + 1) });
    // removing an app from its maker's repository also removes that repository, unless another
    // installed catalog app uses the same one (Claude Code and Claude Desktop share a key, not a repo)
    const onRemoved = () => {
      if (!direct) return;
      const sameRepo = (catalog.apps || []).some((x) => x.id !== a.id && x.source && x.source.url === a.source.url && installed(x.package));
      if (!sameRepo) admin.run('pkg-source-remove', a.id).catch(() => {});
    };
    return h(debian.Details, { name: open, onBack: () => setOpen(null), onRemoved });
  }
  const q = (query || '').trim().toLowerCase();
  const apps = (catalog.apps || []).filter((a) => !q || `${a.name} ${summary(a)} ${a.vendor || ''}`.toLowerCase().includes(q));
  const missing = apps.some((a) => !offered(a.package) && !(a.source && a.source.delivery === 'direct'));
  return h(View, { style: { gap: spacing.l } },
    h(Text, { style: { fontSize: typeScale.largeTitle, fontWeight: 600, color: th.text, textAlign: 'start' } }, t('store.ai.title')),
    h(Text, { style: { fontSize: typeScale.body, color: th.textDim, lineHeight: 1.45, maxWidth: 640, textAlign: 'start' } }, t('store.ai.intro')),
    missing ? h(Text, { style: { fontSize: typeScale.caption, color: '#ff9f0a', textAlign: 'start' } }, t('store.ai.refresh')) : null,
    h(GroupBox, null, ...apps.map((a) => h(Row, { key: a.id, a, here, onOpen: setOpen }))));
}

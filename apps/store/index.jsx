// Tienda — apps de LordOS (paquetes .lpk, sin root) y de Debian (motor de paquetes propio,
// server/src/pkg; instalar y quitar pasan por el ayudante de administración).
import {
  View, Text, Image, useTheme, useLocale, useAppWindow, useStore, Button, TextInput, ScrollView, SidebarItem, SectionTitle,
  packages, packageList, refreshPackages, useLauncherApps, launchApp, fs, formatBytes, wm,
} from '@lordos/sdk';
import { useEffect, useMemo, useState } from 'react';
import { debian, useDebian } from './debian.js';

// Nivel de confianza de un paquete según su firma (ver docs/FIRMA.md).
const TRUST = {
  lordos: { color: '#30d158', glyph: '◆' },
  verified: { color: '#0a84ff', glyph: '✓' },
  signed: { color: '#8e8e93', glyph: '✎' },
  unsigned: { color: '#ff9f0a', glyph: '⚠' },
};
function TrustBadge({ sig }) {
  const { t: tr } = useLocale();
  const level = sig?.trust || 'unsigned';
  const c = TRUST[level] || TRUST.unsigned;
  const label = level === 'lordos' ? tr('store.trust.lordos')
    : level === 'verified' ? tr('store.trust.verified', { name: sig.signer })
    : level === 'signed' ? tr('store.trust.signed', { name: sig.signer })
    : tr('store.trust.unsigned');
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
      <View style={{ width: 16, height: 16, borderRadius: 8, backgroundColor: c.color, alignItems: 'center', justifyContent: 'center' }}>
        <Text style={{ fontSize: 10, color: '#fff', lineHeight: 16 }}>{c.glyph}</Text>
      </View>
      <Text style={{ fontSize: 11, color: c.color }}>{label}</Text>
    </View>
  );
}

export function Icon({ size: s }) {
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(180deg, #4fb4ff, #1463d6)',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)`, alignItems: 'center', justifyContent: 'center' }}>
      <View style={{ width: s * 0.56, height: s * 0.46, marginTop: s * 0.1, borderRadius: s * 0.08, backgroundColor: '#fff' }} />
      <View style={{ position: 'absolute', top: s * 0.2, left: s * 0.34, width: s * 0.32, height: s * 0.26, borderRadius: s * 0.16,
        border: `${Math.max(2, s * 0.06)}px solid #fff` }} />
      <View style={{ position: 'absolute', top: s * 0.5, left: s * 0.36, width: s * 0.28, height: s * 0.07, borderRadius: s * 0.035, backgroundColor: '#1d72e0' }} />
    </View>
  );
}

const Divider = () => <View style={{ height: 1, backgroundColor: useTheme().separator }} />;

// Fila de una app: ícono, nombre, detalle y acciones a la derecha.
export function AppRow({ icon, name, detail, extra, children, onClick }) {
  const t = useTheme();
  return (
    <View onClick={onClick} style={{ flexDirection: 'row', alignItems: 'center', gap: 12, paddingVertical: 10, paddingHorizontal: 4 }}>
      {icon}
      <View style={{ flex: 1, gap: 2 }}>
        <Text style={{ fontSize: 14, fontWeight: 600, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{name}</Text>
        {detail ? <Text style={{ fontSize: 12, color: t.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{detail}</Text> : null}
        {extra ? <Text style={{ fontSize: 11, color: t.textDim }}>{extra}</Text> : null}
      </View>
      <View style={{ flexDirection: 'row', gap: 8 }}>{children}</View>
    </View>
  );
}

export function Message({ msg }) {
  if (!msg) return null;
  return <Text style={{ fontSize: 12, color: msg.ok ? '#30d158' : '#ff453a', marginVertical: 8 }}>{msg.text}</Text>;
}

// ---- apps LordOS (.lpk) ----

// Paquetes encontrados en la carpeta personal (Descargas, Escritorio y la raíz).
function findPackageFiles() {
  const out = [];
  for (const dir of ['', 'Descargas', 'Downloads', 'Escritorio', 'Desktop']) {
    let list = [];
    try { list = fs.list(dir || '.'); } catch (e) { continue; }
    for (const e of list) if (!e.dir && /\.(lpk|deb)$/i.test(e.name)) out.push({ path: `${fs.home()}/${dir ? `${dir}/` : ''}${e.name}`, name: e.name, size: e.size });
  }
  return out;
}

// Confirmación antes de instalar un archivo: lo que dice el paquete y qué pide.
function InstallFile({ path, onDone }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const isDeb = /\.deb$/i.test(path);
  const [info, setInfo] = useState(null);
  const [msg, setMsg] = useState(null);
  const [busy, setBusy] = useState(false);
  useEffect(() => {
    setMsg(null);
    if (isDeb) { setInfo(null); return; }
    try { setInfo(packages.inspect(path)); } catch (e) { setInfo(null); setMsg({ ok: false, text: tr('store.lordos.invalid', { error: e.message }) }); }
  }, [path]);
  const m = info?.manifest;
  const lang = useLocale().lang;
  const loc = (v) => (v && typeof v === 'object' ? v[lang] || v[lang.split('-')[0]] || v.en || Object.values(v)[0] : v) || '';
  const installed = m && packageList.get().find((p) => p.id === m.id);
  const install = () => {
    if (isDeb) {
      setBusy(true);
      debian.installFile(path).then(() => { setMsg({ ok: true, text: tr('store.done.installed') }); onDone?.(); },
        (e) => setMsg({ ok: false, text: e.message })).finally(() => setBusy(false));
      return;
    }
    try {
      packages.install(path);
      setMsg({ ok: true, text: tr('store.lordos.installed', { name: loc(m.name) }) });
      onDone?.();
    } catch (e) { setMsg({ ok: false, text: e.message }); }
  };
  return (
    <View style={{ padding: 14, borderRadius: 10, backgroundColor: t.hover, gap: 8, marginBottom: 12 }}>
      <Text style={{ fontSize: 15, fontWeight: 700, color: t.text }}>{tr('store.file.title')}</Text>
      <Text style={{ fontSize: 12, color: t.textDim }}>{path}</Text>
      {m && (
        <View style={{ gap: 4 }}>
          <Text style={{ fontSize: 14, fontWeight: 600, color: t.text }}>{loc(m.name)} {m.version}</Text>
          {m.description ? <Text style={{ fontSize: 12, color: t.text }}>{loc(m.description)}</Text> : null}
          <Text style={{ fontSize: 12, color: t.textDim }}>{tr('store.file.size', { size: formatBytes(info.size), files: info.files })}</Text>
          <Text style={{ fontSize: 12, color: t.textDim }}>
            {tr('store.file.permissions', { list: (m.permissions || []).length ? m.permissions.join(', ') : tr('store.file.noPermissions') })}</Text>
          <TrustBadge sig={info.signature} />
          {installed && !msg?.ok && <Text style={{ fontSize: 12, color: t.textDim }}>{tr('store.file.replaces', { version: installed.version })}</Text>}
        </View>
      )}
      {isDeb && <Text style={{ fontSize: 12, color: t.text }}>{tr('store.file.deb')}</Text>}
      <Message msg={msg} />
      <View style={{ flexDirection: 'row', gap: 8 }}>
        {(m || isDeb) && !msg?.ok && <Button primary label={busy ? tr('store.working') : tr('store.install')} onClick={() => !busy && install()} />}
        <Button label={tr('store.close')} onClick={() => onDone?.(true)} />
      </View>
    </View>
  );
}

function LordOSPage({ query }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const list = useStore(packageList);
  const [files, setFiles] = useState(() => findPackageFiles().filter((f) => /\.lpk$/i.test(f.name)));
  const [pick, setPick] = useState(null);
  const [msg, setMsg] = useState(null);
  const q = query.trim().toLowerCase();
  const shown = list.filter((p) => !q || `${p.name} ${p.description} ${p.id}`.toLowerCase().includes(q));
  return (
    <View>
      <Text style={{ fontSize: 22, fontWeight: 700, color: t.text, marginBottom: 4 }}>{tr('store.lordos.title')}</Text>
      <Text style={{ fontSize: 12, color: t.textDim, marginBottom: 12 }}>{tr('store.lordos.subtitle')}</Text>
      {pick && <InstallFile path={pick} onDone={(cancel) => { if (cancel) setPick(null); setFiles(findPackageFiles().filter((f) => /\.lpk$/i.test(f.name))); }} />}
      <Message msg={msg} />
      <SectionTitle>{tr('store.lordos.installedTitle')}</SectionTitle>
      {shown.length === 0 && <Text style={{ fontSize: 13, color: t.textDim, margin: 10 }}>{tr('store.lordos.none')}</Text>}
      {shown.map((p, i) => (
        <View key={p.id}>
          {i > 0 && <Divider />}
          <AppRow icon={<p.Icon size={44} />} name={p.name} detail={p.description} extra={tr('store.version', { version: p.version })}>
            <Button primary label={tr('store.open')} onClick={() => launchApp(p.id)} />
            <Button label={tr('store.remove')} onClick={() => { packages.remove(p.id); setMsg({ ok: true, text: tr('store.lordos.removed', { name: p.name }) }); }} />
          </AppRow>
          <PkgManage pkg={p} onMsg={setMsg} />
        </View>
      ))}
      <SectionTitle>{tr('store.lordos.filesTitle')}</SectionTitle>
      {files.length === 0 && <Text style={{ fontSize: 12, color: t.textDim, margin: 10 }}>{tr('store.lordos.noFiles')}</Text>}
      {files.map((f) => (
        <AppRow key={f.path} icon={<Icon size={32} />} name={f.name} detail={`${f.path.replace(fs.home(), '~')} · ${formatBytes(f.size)}`}>
          <Button label={tr('store.install')} onClick={() => setPick(f.path)} />
        </AppRow>
      ))}
    </View>
  );
}

// Confianza, «Modificada por vos», Restaurar original y Compartir mi versión (docs/FIRMA.md).
function PkgManage({ pkg, onMsg }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [st, setSt] = useState(null);
  const [rev, setRev] = useState(0);
  useEffect(() => { try { setSt(lord.sign.status(pkg.id)); } catch (e) { setSt(null); } }, [pkg.id, pkg.version, rev]);
  if (!st || !st.installed) return null;
  const restore = () => {
    try { lord.sign.restore(pkg.id); refreshPackages(); setRev((v) => v + 1); onMsg({ ok: true, text: tr('store.restore.done', { name: pkg.name }) }); }
    catch (e) { onMsg({ ok: false, text: e.message }); }
  };
  const share = () => {
    try {
      const key = lord.sign.userKey();
      const base = `${pkg.id}@${st.version} by ${st.signer || tr('store.trust.unknownAuthor')}`;
      const manifest = { ...pkgManifest(pkg), id: pkg.id, version: `${st.version}+mia`, basedOn: base };
      const out = lord.sign.share(pkg.id, JSON.stringify(manifest, null, 2), `Descargas/${pkg.id}-mia.lpk`);
      onMsg({ ok: true, text: tr('store.share.done', { path: out.replace(fs.home(), '~'), key: key.public.slice(0, 16) }) });
    } catch (e) { onMsg({ ok: false, text: e.message }); }
  };
  return (
    <View style={{ marginInlineStart: 56, marginBottom: 8, gap: 6 }}>
      {st.hasOriginal ? <TrustBadge sig={st} /> : <Text style={{ fontSize: 11, color: t.textDim }}>{tr('store.manage.noOriginal')}</Text>}
      {st.modified && (
        <View style={{ gap: 4 }}>
          <Text style={{ fontSize: 12, color: '#ff9f0a', fontWeight: 600 }}>{tr('store.manage.modified', { count: st.changedCount })}</Text>
          {st.changed.slice(0, 6).map((c) => (
            <Text key={c.path} style={{ fontSize: 11, color: t.textDim }}>
              {tr(`store.manage.kind.${c.kind}`)} {c.path}
            </Text>
          ))}
          <View style={{ flexDirection: 'row', gap: 8, marginTop: 4 }}>
            {st.hasOriginal && <Button label={tr('store.manage.restore')} onClick={restore} />}
            <Button label={tr('store.manage.share')} onClick={share} />
          </View>
        </View>
      )}
    </View>
  );
}
const pkgManifest = (pkg) => ({ name: pkg.name, description: pkg.description, icon: 'icon.png', size: pkg.size, minSize: pkg.minSize, permissions: pkg.permissions });

// ---- apps instaladas (todas: LordOS, paquetes y Linux) ----

function InstalledPage({ query }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const apps = useLauncherApps();
  const q = query.trim().toLowerCase();
  const shown = apps.filter((a) => !q || `${a.name} ${a.comment || ''} ${a.id}`.toLowerCase().includes(q));
  const kind = (a) => tr(`store.kind.${a.kind}`);
  return (
    <View>
      <Text style={{ fontSize: 22, fontWeight: 700, color: t.text, marginBottom: 12 }}>{tr('store.installed.title')}</Text>
      {shown.map((a, i) => (
        <View key={a.id}>
          {i > 0 && <Divider />}
          <AppRow icon={<a.Icon size={36} />} name={a.name} detail={a.comment || a.genericName || ''} extra={kind(a)}>
            <Button label={tr('store.open')} onClick={() => launchApp(a.id)} />
            {a.kind === 'linux' && debian.canRemove(a) && <Button label={tr('store.remove')} onClick={() => debian.removeApp(a)} />}
          </AppRow>
        </View>
      ))}
    </View>
  );
}

// ---- ventana ----

export default function Store() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const { args } = useAppWindow();
  const [page, setPage] = useState('discover');
  const [query, setQuery] = useState('');
  const [file, setFile] = useState(null);
  const deb = useDebian();
  useEffect(() => {
    if (args?.install) setFile(args.install);
    if (args?.page) setPage(args.page);
    if (args?.search) { setQuery(args.search); setPage('discover'); }       // desde Spotlight
  }, [args?.nonce]);
  const pages = [
    ['discover', tr('store.page.discover')],
    ['lordos', tr('store.page.lordos')],
    ['installed', tr('store.page.installed')],
    ['updates', deb.upgrades?.length ? tr('store.page.updatesCount', { count: deb.upgrades.length }) : tr('store.page.updates')],
  ];
  const Page = { discover: debian.Discover, lordos: LordOSPage, installed: InstalledPage, updates: debian.Updates }[page];
  return (
    <View style={{ flex: 1, flexDirection: 'row' }}>
      <View style={{ width: 200, backgroundColor: t.sidebarBg, padding: 10, gap: 4 }}>
        <TextInput value={query} onChange={(v) => { setQuery(v); if (v && page !== 'discover' && page !== 'installed' && page !== 'lordos') setPage('discover'); }}
          placeholder={tr('store.search')} onCancel={() => setQuery('')} />
        <View style={{ height: 6 }} />
        {pages.map(([id, label]) => <SidebarItem key={id} label={label} selected={page === id} onClick={() => setPage(id)} />)}
        <View style={{ flex: 1 }} />
        <debian.Status />
      </View>
      <ScrollView style={{ flex: 1, padding: 18 }}>
        {file && <InstallFile path={file} onDone={(cancel) => cancel && setFile(null)} />}
        <Page query={query} onPage={setPage} />
      </ScrollView>
    </View>
  );
}

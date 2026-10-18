// Ajustes — apariencia, fondo de pantalla, idioma y región, teclado, temas y sesión.
import {
  View, Text, Image, useTheme, useSettings, themes, session, keyboard, admin, languages, setLanguage, useLocale,
  reloadCatalogs, fs, useStore, SidebarItem, Segmented, Toggle, Button, TextInput, ScrollView, useSystemInfo, useAppWindow, useTrackpads,
} from '@lordos/sdk';
import { useEffect, useState } from 'react';
import { SoundSettings } from './Sound.jsx';
import { SharingSettings } from './Sharing.jsx';
import { UsersSettings } from './Users.jsx';
import { NetworkSettings } from './Network.jsx';
import { General, Updates } from './General.jsx';
import { ClipboardSettings } from './Clipboard.jsx';
import { PrivacySettings } from './Privacy.jsx';
import { PrintersSettings } from './Printers.jsx';
import { TrackpadSettings } from './Trackpad.jsx';
import { DockSettings } from './Dock.jsx';

export function Icon({ size: s }) {
  const dark = '#3b3e46';
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(180deg, #c4c8d0, #6c717c)',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)`, overflow: 'hidden' }}>
      <View style={{ position: 'absolute', left: s * 0.44, top: s * 0.14, width: s * 0.12, height: s * 0.72, borderRadius: s * 0.04, backgroundColor: dark }} />
      <View style={{ position: 'absolute', left: s * 0.14, top: s * 0.44, width: s * 0.72, height: s * 0.12, borderRadius: s * 0.04, backgroundColor: dark }} />
      <View style={{ position: 'absolute', left: s * 0.21, top: s * 0.21, width: s * 0.58, height: s * 0.58, borderRadius: s * 0.29, backgroundColor: dark }} />
      <View style={{ position: 'absolute', left: s * 0.36, top: s * 0.36, width: s * 0.28, height: s * 0.28, borderRadius: s * 0.14,
        background: 'linear-gradient(180deg, #e5e7eb, #9ca3af)' }} />
    </View>
  );
}

function Row({ label, hint, children }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 12, gap: 16 }}>
      <View style={{ flex: 1 }}>
        <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{label}</Text>
        {hint ? <Text style={{ fontSize: 11, color: t.textDim, marginTop: 3, lineHeight: 1.35 }}>{hint}</Text> : null}
      </View>
      {children}
    </View>
  );
}

const Divider = () => <View style={{ height: 1, backgroundColor: useTheme().separator }} />;

// Lista con búsqueda (zonas horarias, regiones, distribuciones de teclado).
function SearchList({ items, value, onPick, placeholder, height = 180 }) {
  const t = useTheme();
  const [q, setQ] = useState('');
  const needle = q.trim().toLowerCase();
  const shown = (needle ? items.filter(([id, label]) => `${id} ${label}`.toLowerCase().includes(needle)) : items).slice(0, 300);
  return (
    <View style={{ gap: 6 }}>
      <TextInput value={q} onChange={setQ} placeholder={placeholder} onCancel={() => setQ('')} />
      <View style={{ height, borderRadius: 8, border: `1px solid ${t.separator}` }}>
        <ScrollView style={{ padding: 4 }}>
          {shown.map(([id, label, extra]) => (
            <View key={id} onClick={() => onPick(id)} style={{ height: 24, flexDirection: 'row', alignItems: 'center', paddingHorizontal: 8,
              borderRadius: 5, backgroundColor: id === value ? t.accent : 'transparent' }}>
              <Text style={{ flex: 1, fontSize: 12, color: id === value ? '#fff' : t.text, whiteSpace: 'nowrap' }}>{label}</Text>
              {extra ? <Text style={{ fontSize: 11, color: id === value ? '#fff' : t.textDim }}>{extra}</Text> : null}
            </View>
          ))}
        </ScrollView>
      </View>
    </View>
  );
}

function Appearance() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [s, set] = useSettings();
  return (
    <View>
      <Row label={tr('settings.mode')}>
        <Segmented value={s.mode} onChange={(mode) => set({ mode })} options={[['dark', tr('settings.dark')], ['light', tr('settings.light')]]} />
      </Row>
      <Divider />
      <Row label={tr('shell.cc.accent')}>
        <View style={{ flexDirection: 'row', gap: 8 }}>
          {Object.entries(t.accents || {}).map(([name, c]) => (
            <View key={name} onClick={() => set({ accent: name })} style={{ width: 22, height: 22, borderRadius: 11,
              backgroundColor: c, border: s.accent === name ? `2px solid ${t.text}` : '0 solid transparent' }} />
          ))}
        </View>
      </Row>
      <Divider />
      <Row label={tr('settings.effects')} hint={tr('settings.effectsHint')}>
        <Toggle on={s.effects} onChange={(effects) => set({ effects })} />
      </Row>
    </View>
  );
}

function Wallpapers() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [s, set] = useSettings();
  const list = t.wallpapers || [];
  if (!list.length) return <Text style={{ fontSize: 13, color: t.textDim }}>{tr('settings.noWallpapers')}</Text>;
  return (
    <View style={{ flexDirection: 'row', gap: 14, paddingTop: 8 }}>
      {list.map((w) => (
        <View key={w.id} onClick={() => set({ wallpaper: w.id })} style={{ alignItems: 'center', gap: 6 }}>
          <View style={{ width: 112, height: 70, borderRadius: 8, overflow: 'hidden',
            border: s.wallpaper === w.id ? `3px solid ${t.accent}` : `1px solid ${t.separator}` }}>
            <Image src={w.src} style={{ width: '100%', height: '100%', objectFit: 'cover' }} />
          </View>
          <Text style={{ fontSize: 12, color: t.text }}>{w.name}</Text>
        </View>
      ))}
    </View>
  );
}

// ---- idioma y región ----

// Progreso del ayudante de administración (generar región, instalar paquetes…).
function AdminLog() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const busy = useStore(admin.busy);
  if (!busy) return null;
  return (
    <View style={{ marginTop: 8, padding: 10, borderRadius: 8, backgroundColor: t.hover }}>
      <Text style={{ fontSize: 12, fontWeight: 600, color: t.text }}>{tr('settings.admin.running', { action: tr(`settings.admin.${busy.action}`), arg: busy.arg })}</Text>
      {busy.log.slice(-6).map((l, i) => <Text key={i} style={{ fontSize: 11, color: t.textDim, whiteSpace: 'nowrap' }}>{l}</Text>)}
    </View>
  );
}

const shortLocale = (name) => name.replace(/\.UTF-8$/i, '');

function LanguageRegion() {
  const t = useTheme();
  const { t: tr, lang, region, formatDate, formatTime, formatNumber } = useLocale();
  const [s, set] = useSettings();
  const [msg, setMsg] = useState(null);
  const [showZones, setShowZones] = useState(false);
  const [showRegions, setShowRegions] = useState(false);
  const langs = languages();
  const [locales] = useState(() => lord.locale.available());
  const [zone, setZone] = useState(() => lord.time.zone());
  const [sys, setSys] = useState(() => lord.locale.get().system.LANG || 'C.UTF-8');
  const isAdmin = admin.available();
  const report = (p, ok) => p.then((m) => { setMsg({ ok: true, text: ok || m }); return true; }, (e) => { setMsg({ ok: false, text: e.message }); return false; });

  // Región de formatos y de los programas (LANG): se genera con glibc si hace falta.
  const regionName = region || langs.find((l) => l.id === lang)?.region || '';
  const generated = (name) => locales.find((l) => l.name === name)?.generated;
  async function useRegion(r) {
    const name = `${r}.UTF-8`;
    if (!generated(name)) {
      const ok = await report(admin.run('locale-gen', name), tr('settings.region.generated', { name: r }));
      if (!ok) return;
      const l = locales.find((x) => x.name === name);
      if (l) l.generated = true;
    }
    lord.locale.set({ LANG: name });
    lord.keyboard?.reloadCompose?.();
    set({ region: r });
  }
  const pickLanguage = (id) => {
    setLanguage(id);
    const r = langs.find((l) => l.id === id)?.region;
    if (r && !s.region) useRegion(r);
  };

  const now = new Date();
  const regionItems = locales.map((l) => [shortLocale(l.name), shortLocale(l.name), l.generated ? '✓' : '']);
  return (
    <View>
      <Row label={tr('settings.lang.title')} hint={tr('settings.lang.hint')} />
      <View style={{ flexDirection: 'row', gap: 8, marginBottom: 8 }}>
        {langs.map((l) => (
          <View key={l.id} onClick={() => pickLanguage(l.id)} style={{ paddingHorizontal: 14, paddingVertical: 8, borderRadius: 8,
            backgroundColor: l.id === lang ? t.accent : t.hover, border: `1px solid ${t.separator}` }}>
            <Text style={{ fontSize: 13, fontWeight: 600, color: l.id === lang ? '#fff' : t.text }}>{l.name}</Text>
            <Text style={{ fontSize: 10, color: l.id === lang ? '#fff' : t.textDim }}>{l.english}{l.user ? ` · ${tr('settings.lang.pack')}` : ''}</Text>
          </View>
        ))}
      </View>
      <Divider />
      <Row label={tr('settings.region.title')} hint={tr('settings.region.hint')}>
        <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{regionName || '—'}</Text>
        <Button label={tr('settings.change')} onClick={() => setShowRegions(!showRegions)} />
      </Row>
      {showRegions && <SearchList items={regionItems} value={regionName} placeholder={tr('settings.region.search')}
        onPick={(r) => { setShowRegions(false); useRegion(r); }} />}
      <View style={{ padding: 10, borderRadius: 8, backgroundColor: t.hover, gap: 3 }}>
        <Text style={{ fontSize: 12, color: t.textDim }}>{tr('settings.region.preview')}</Text>
        <Text style={{ fontSize: 13, color: t.text }}>{formatDate(now, 'long')} · {formatTime(now)}</Text>
        <Text style={{ fontSize: 13, color: t.text }}>{formatDate(now, 'numeric')} · {formatNumber(1234567.89, 2)}</Text>
      </View>
      <Divider />
      <Row label={tr('settings.system.title')} hint={tr('settings.system.hint', { name: sys })}>
        <Button label={tr('settings.system.apply', { name: regionName || '—' })} onClick={() => regionName && report(admin.run('system-locale', `${regionName}.UTF-8`)).then((ok) => ok && setSys(`${regionName}.UTF-8`))} />
      </Row>
      <Divider />
      <Row label={tr('settings.tz.title')} hint={zone}>
        <Button label={tr('settings.change')} onClick={() => setShowZones(!showZones)} />
      </Row>
      {showZones && <SearchList items={lord.time.zones().sort().map((z) => [z, z.replace(/_/g, ' ')])} value={zone} placeholder={tr('settings.tz.search')}
        onPick={(z) => { setShowZones(false); report(admin.run('timezone', z)).then((ok) => { if (ok) { lord.time.reload(); setZone(z); } }); }} />}
      <Divider />
      <Row label={tr('settings.pack.title')} hint={tr('settings.pack.hint')}>
        <Button label={tr('settings.pack.install', { lang: langs.find((l) => l.id === lang)?.name || lang })}
          onClick={() => report(admin.run('langpack', regionName.startsWith(`${lang.split('-')[0]}_`) ? regionName : lang.split('-')[0]))} />
      </Row>
      <PackImport onDone={(text) => { reloadCatalogs(); setMsg({ ok: true, text }); }} />
      {!isAdmin && <Text style={{ fontSize: 11, color: '#ff9f0a', marginTop: 6 }}>{tr('settings.admin.unavailable')}</Text>}
      <AdminLog />
      {msg && <Text style={{ fontSize: 12, marginTop: 8, color: msg.ok ? '#32d74b' : '#ff453a' }}>{msg.text}</Text>}
    </View>
  );
}

// Paquete de idioma de LordOS (lo arma tools/i18n-pack.mjs): lordos-lang-<id>.json en Descargas.
function PackImport({ onDone }) {
  const { t: tr } = useLocale();
  const found = [];
  for (const d of ['Descargas', 'Downloads', '.']) {
    try { for (const e of fs.list(d)) if (/^lordos-lang-[\w-]+\.json$/.test(e.name)) found.push(d === '.' ? e.name : `${d}/${e.name}`); } catch (e) { /* no existe */ }
  }
  if (!found.length) return null;
  const install = (p) => {
    try {
      const pack = JSON.parse(fs.read(p));
      const id = String(pack.meta?.id || '');
      const files = { meta: JSON.stringify(pack.meta, null, 2) };
      for (const [ns, obj] of Object.entries(pack.catalogs || {})) files[ns] = JSON.stringify(obj, null, 2);
      if (!lord.locale.installPack(id, files)) throw new Error(tr('settings.pack.invalid'));
      onDone(tr('settings.pack.imported', { name: pack.meta.name || id }));
    } catch (e) { onDone(`${p}: ${e.message}`); }
  };
  return (
    <Row label={tr('settings.pack.lordos')} hint={found.join(' · ')}>
      {found.map((p) => <Button key={p} label={tr('settings.pack.add', { file: p.split('/').pop() })} onClick={() => install(p)} />)}
    </Row>
  );
}

// ---- teclado ----

function Keyboard() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [s, set] = useSettings();
  const [sample, setSample] = useState('');
  const [adding, setAdding] = useState(false);
  const all = keyboard.list();
  const mine = keyboard.configured();
  const nameOf = (k) => {
    if (k.ime) return tr(`settings.ime.${k.ime}`);
    const l = all.find((x) => x.id === k.layout);
    const v = l?.variants.find((x) => x.id === k.variant);
    return v ? v.name : l ? l.name : k.layout;
  };
  const items = all.flatMap((l) => [[l.id, l.name, l.id], ...l.variants.map((v) => [`${l.id}(${v.id})`, v.name, `${l.id}(${v.id})`])]);
  const add = (id) => {
    const m = /^([\w-]+)(?:\(([\w-]+)\))?$/.exec(id);
    if (!m) return;
    const k = { layout: m[1], variant: m[2] || '' };
    if (!mine.some((x) => x.layout === k.layout && x.variant === k.variant && !x.ime)) keyboard.set([...mine, k].slice(0, 6));
    setAdding(false);
  };
  // Método de entrada: va sobre el teclado latino (us) y se elige como cualquier distribución.
  const addIme = (id) => { if (!mine.some((x) => x.ime === id)) keyboard.set([...mine, { layout: 'us', variant: '', ime: id }].slice(0, 6)); };
  return (
    <View>
      <Row label={tr('settings.kb.layouts')} hint={tr('settings.kb.hint')} />
      {mine.map((k, i) => (
        <View key={`${k.layout}${k.variant}${k.ime || ''}`} style={{ flexDirection: 'row', alignItems: 'center', gap: 10, height: 34 }}>
          <View style={{ width: 30, height: 22, borderRadius: 5, alignItems: 'center', justifyContent: 'center', backgroundColor: i === (s.keyboardIndex || 0) ? t.accent : t.control }}>
            <Text style={{ fontSize: 11, fontWeight: 700, color: i === (s.keyboardIndex || 0) ? '#fff' : t.text }}>{keyboard.label(k)}</Text>
          </View>
          <Text style={{ flex: 1, fontSize: 13, color: t.text }}>{nameOf(k)}</Text>
          {i !== (s.keyboardIndex || 0) && <Button label={tr('settings.kb.use')} onClick={() => set({ keyboardIndex: i })} />}
          {mine.length > 1 && <Button label="−" onClick={() => keyboard.set(mine.filter((_, j) => j !== i))} />}
        </View>
      ))}
      <Button label={tr('settings.kb.add')} onClick={() => setAdding(!adding)} style={{ alignSelf: 'flex-start', marginTop: 6 }} />
      {adding && all.length > 0 && <SearchList items={items} placeholder={tr('settings.kb.search')} onPick={add} height={220} />}
      {!all.length && <Text style={{ fontSize: 11, color: '#ff9f0a' }}>{tr('settings.kb.noxkb')}</Text>}
      <Divider />
      <Row label={tr('settings.ime.title')} hint={tr('settings.ime.hint')} />
      {keyboard.engines.map((e) => (
        <View key={e.id} style={{ flexDirection: 'row', alignItems: 'center', gap: 10, height: 32 }}>
          <View style={{ width: 30, height: 22, borderRadius: 5, alignItems: 'center', justifyContent: 'center', backgroundColor: t.control }}>
            <Text style={{ fontSize: 12, color: t.text }}>{e.label}</Text>
          </View>
          <Text style={{ flex: 1, fontSize: 13, color: t.text }}>{tr(`settings.ime.${e.id}`)}</Text>
          {!mine.some((x) => x.ime === e.id) && <Button label={tr('settings.ime.add')} onClick={() => addIme(e.id)} />}
        </View>
      ))}
      {keyboard.active()?.ime && keyboard.missingDictionary() && <Text style={{ fontSize: 11, color: '#ff9f0a' }}>{tr('settings.ime.noDict')}</Text>}
      <Divider />
      <Row label={tr('settings.kb.test')} hint={tr('settings.kb.testHint')} />
      <TextInput value={sample} onChange={setSample} placeholder={tr('settings.kb.placeholder')} onCancel={() => setSample('')} />
    </View>
  );
}

function Themes() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const list = themes.list();
  return (
    <View style={{ gap: 10, paddingTop: 6 }}>
      {list.map((th) => (
        <View key={th.id} style={{ flexDirection: 'row', alignItems: 'center', padding: 12, borderRadius: 10,
          backgroundColor: t.hover, border: th.current ? `1px solid ${t.accent}` : `1px solid ${t.separator}` }}>
          <View style={{ flex: 1 }}>
            <Text style={{ fontSize: 14, fontWeight: 600, color: t.text }}>{th.name}</Text>
            <Text style={{ fontSize: 12, color: t.textDim, marginTop: 3 }}>{th.description}</Text>
          </View>
          {th.current
            ? <Text style={{ fontSize: 12, fontWeight: 600, color: t.accent }}>{tr('settings.themes.current')}</Text>
            : <Button label={tr('settings.themes.use')} primary onClick={() => themes.use(th.id)} />}
        </View>
      ))}
      <Text style={{ fontSize: 11, color: t.textDim, marginTop: 4 }}>{tr('settings.themes.hint')}</Text>
    </View>
  );
}

function Session() {
  const { t: tr } = useLocale();
  return (
    <View>
      <Row label={tr('shell.menu.logout')} hint={tr('settings.session.hint')}>
        <Button label={tr('shell.menu.logout')} onClick={() => session.quit()} />
      </Row>
    </View>
  );
}

// Grupos de la barra lateral (como en macOS / TangoOS): [id, componente, color del ícono, símbolo].
const GROUPS = [
  [['general', General, '#8e8e93', 'i'], ['appearance', Appearance, '#bf5af2', '◐'], ['wallpaper', Wallpapers, '#0a84ff', '▣'], ['dock', DockSettings, '#1c1c1e', '▁']],
  [['network', NetworkSettings, '#0a84ff', '⇅'], ['sound', SoundSettings, '#ff375f', '♪'], ['printers', PrintersSettings, '#636366', '⎙'], ['clipboard', ClipboardSettings, '#5e5ce6', '⎘'], ['sharing', SharingSettings, '#34c759', '⇄']],
  [['language', LanguageRegion, '#30b0c7', 'A'], ['keyboard', Keyboard, '#636366', '⌨'], ['trackpad', TrackpadSettings, '#8e8e93', '▭']],
  [['privacy', PrivacySettings, '#0a84ff', '🔒'], ['users', UsersSettings, '#0a84ff', '☺'], ['updates', Updates, '#64d2ff', '↻'], ['themes', Themes, '#ff9f0a', '◧'], ['session', Session, '#ff453a', '↪']],
];
const SECTIONS = GROUPS.flat();

function SectionIcon({ color, glyph }) {
  return (
    <View style={{ width: 20, height: 20, borderRadius: 5, backgroundColor: color, alignItems: 'center', justifyContent: 'center' }}>
      <Text style={{ fontSize: 12, fontWeight: 700, color: '#fff', lineHeight: 20 }}>{glyph}</Text>
    </View>
  );
}

// Usuario de la sesión: iniciales, nombre y si es administrador.
function UserCard({ onClick }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const info = useSystemInfo(60000);
  const name = info.fullName || info.user || '';
  const initials = name.split(/\s+/).filter(Boolean).slice(0, 2).map((w) => w[0].toUpperCase()).join('') || '?';
  return (
    <View onClick={onClick} style={{ flexDirection: 'row', alignItems: 'center', gap: 10, padding: 8, borderRadius: 10, backgroundColor: t.hover, marginBottom: 6 }}>
      <View style={{ width: 38, height: 38, borderRadius: 19, alignItems: 'center', justifyContent: 'center', background: 'linear-gradient(180deg, #5ac8fa, #0a6cd6)' }}>
        <Text style={{ fontSize: 14, fontWeight: 700, color: '#fff' }}>{initials}</Text>
      </View>
      <View style={{ flex: 1 }}>
        <Text style={{ fontSize: 13, fontWeight: 700, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{name}</Text>
        <Text style={{ fontSize: 11, color: t.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>
          {`${info.admin ? tr('settings.user.admin') : tr('settings.user.standard')} · ${info.user}@${info.hostname}`}</Text>
      </View>
    </View>
  );
}

// La sección abierta sobrevive a que la interfaz se vuelva a montar al cambiar de idioma.
let lastSection = 'general';

// Otras apps abren una sección: wm.open('settings', { args: { section: 'network', page: 'vpn' } }).
export default function Settings() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const { args } = useAppWindow();
  const [section, setSectionState] = useState(lastSection);
  const [query, setQuery] = useState('');
  const setSection = (s) => { lastSection = s; setSectionState(s); };
  useEffect(() => { if (args?.section && SECTIONS.some(([id]) => id === args.section)) setSection(args.section); }, [args?.nonce]);
  const [, Body] = SECTIONS.find(([id]) => id === section) || SECTIONS[0];
  const q = query.trim().toLowerCase();
  // Trackpad solo si hay un touchpad (los presta lord-sessiond con el monitor)
  const hasPad = useTrackpads().some((d) => d.kind === 'touchpad');
  const visible = (id) => (id !== 'trackpad' || hasPad) && `${tr(`settings.section.${id}`)} ${tr(`settings.keywords.${id}`)}`.toLowerCase().includes(q);
  const groups = GROUPS.map((g) => g.filter(([id]) => visible(id))).filter((g) => g.length);
  return (
    <View style={{ flex: 1, flexDirection: 'row' }}>
      <View style={{ width: 220, backgroundColor: t.sidebarBg, paddingHorizontal: 8, paddingTop: 10 }}>
        <TextInput value={query} onChange={setQuery} placeholder={tr('settings.search')} onCancel={() => setQuery('')}
          onSubmit={() => groups[0] && setSection(groups[0][0][0])} />
        <View style={{ height: 8 }} />
        <UserCard onClick={() => setSection('general')} />
        <ScrollView style={{ flex: 1 }}>
          {groups.map((g, i) => (
            <View key={i} style={{ gap: 1, marginTop: i ? 10 : 2 }}>
              {g.map(([id, , color, glyph]) => (
                <SidebarItem key={id} label={tr(`settings.section.${id}`)} selected={id === section} onClick={() => setSection(id)}
                  icon={<SectionIcon color={color} glyph={glyph} />} />
              ))}
            </View>
          ))}
          {!groups.length && <Text style={{ fontSize: 12, color: t.textDim, margin: 8 }}>{tr('settings.noResults')}</Text>}
        </ScrollView>
      </View>
      <ScrollView style={{ paddingHorizontal: 24, paddingTop: 16 }}>
        <Text style={{ fontSize: 20, fontWeight: 700, color: t.text, marginBottom: 6 }}>{tr(`settings.section.${section}`)}</Text>
        <Body page={section === 'network' ? args?.page : undefined} />
      </ScrollView>
    </View>
  );
}

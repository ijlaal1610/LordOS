// Contraseñas — el gestor de contraseñas de LordOS: entradas por categorías (árbol, con plantillas
// de campos) y etiquetas, favoritas, archivadas, papelera, historial, códigos 2FA, Wi-Fi con QR,
// notas seguras, tarjetas, informe de seguridad, generador, importar/exportar y el llavero de la
// sesión. Todo lo descifrado lo maneja lord-vault, encerrado y sin red; esta ventana está
// protegida (negra en capturas, grabaciones y escritorio remoto). Ver docs/CONTRASENAS.md.
import {
  View, Text, useTheme, useStore, useAppWindow, useKeyboard, Button, TextInput, ScrollView, wm, t, vaultAvailable,
} from '@lordos/sdk';
import { useEffect, useMemo, useRef, useState } from 'react';
import { app, req, refreshStatus, wireEvents, reloadLogin } from './api.js';
import { allCategories, builtinById, catName } from './templates.js';
import { CatIcon, EntryIcon, Chip, shade, Overlays } from './ui.jsx';
import { UnlockView, CreateView } from './Lock.jsx';
import { EntryView, EntryEditor, Center, tagColor } from './Detail.jsx';
import { SecurityView, CodesView } from './Security.jsx';
import { GeneratorSheet } from './Generator.jsx';
import { SettingsSheet, ImportSheet, ExportSheet, CategorySheet, TagSheet } from './Settings.jsx';

// ---- ícono de la app: un candado con tres asteriscos, propio ----

export function Icon({ size: s }) {
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(160deg, #3fd2c7, #1f6fd1)', overflow: 'hidden',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)`, alignItems: 'center' }}>
      {/* arco del candado */}
      <View style={{ position: 'absolute', top: s * 0.14, width: s * 0.4, height: s * 0.4, borderRadius: s * 0.2, border: `${Math.max(2, s * 0.075)}px solid #ffffff` }} />
      {/* cuerpo */}
      <View style={{ position: 'absolute', top: s * 0.38, width: s * 0.62, height: s * 0.44, borderRadius: s * 0.1, backgroundColor: '#ffffff',
        flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: s * 0.05, boxShadow: `0 ${s * 0.02}px ${s * 0.05}px rgba(0,0,0,0.25)` }}>
        {[0, 1, 2].map((i) => <View key={i} style={{ width: s * 0.09, height: s * 0.09, borderRadius: s * 0.045, backgroundColor: '#1f6fd1' }} />)}
      </View>
    </View>
  );
}

// ---- barra lateral ----

function SideRow({ icon, label, count, selected, onClick, depth = 0, color, onDoubleClick, twisty, onTwisty }) {
  const th = useTheme();
  const [hover, setHover] = useState(false);
  return (
    <View onClick={onClick} onDoubleClick={onDoubleClick} onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)}
      style={{ height: 28, paddingLeft: 8 + depth * 14, paddingRight: 8, borderRadius: 6, flexDirection: 'row', alignItems: 'center', gap: 7,
        backgroundColor: selected ? th.accent : hover ? th.hover : 'transparent' }}>
      {twisty != null ? <Text onClick={(e) => { e.stopPropagation?.(); onTwisty?.(); }} style={{ width: 10, fontSize: 9, color: selected ? '#fff' : th.textDim }}>{twisty ? '▾' : '▸'}</Text>
        : depth ? <View style={{ width: 10 }} /> : null}
      {typeof icon === 'string' ? <Text style={{ fontSize: 14, width: 18, textAlign: 'center' }}>{icon}</Text> : icon}
      {color && <View style={{ width: 9, height: 9, borderRadius: 5, backgroundColor: color }} />}
      <Text style={{ flex: 1, fontSize: 13, color: selected ? '#fff' : th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{label}</Text>
      {count != null && count > 0 && <Text style={{ fontSize: 11, color: selected ? '#fff' : th.textDim }}>{String(count)}</Text>}
    </View>
  );
}

function Section({ children }) {
  const th = useTheme();
  return <Text style={{ fontSize: 11, fontWeight: 600, color: th.textDim, marginLeft: 10, marginTop: 12, marginBottom: 3 }}>{children}</Text>;
}

function inCat(entryCat, catId, cats) {
  if (entryCat === catId) return true;
  let c = cats.find((x) => x.id === entryCat);
  for (let guard = 0; c && c.parent && guard < 16; guard++) {
    if (c.parent === catId) return true;
    c = cats.find((x) => x.id === c.parent);
  }
  return false;
}

function Sidebar({ sel, onSel, issues, onNewCat, onEditCat, onEditTag }) {
  const th = useTheme();
  const st = useStore(app);
  const cats = allCategories(st.cats);
  const live = st.entries.filter((e) => !e.trash);
  const [open, setOpen] = useState({});
  const count = (fn) => live.filter(fn).length;
  const tags = [...new Set(live.flatMap((e) => e.tags || []))].sort((a, b) => a.localeCompare(b));
  const childrenOf = (pid) => cats.filter((c) => (c.parent || '') === pid);
  const tree = (pid, depth) => childrenOf(pid).map((c) => {
    const n = count((e) => inCat(e.cat, c.id, cats));
    const kids = childrenOf(c.id);
    if (c.builtin && !n && !kids.length && sel !== `cat:${c.id}`) return null;     // las incluidas vacías no ocupan lugar
    const isOpen = open[c.id] !== false;
    return [
      <SideRow key={c.id} icon={<CatIcon cat={c} size={18} />} label={catName(c)} count={n} depth={depth} selected={sel === `cat:${c.id}`}
        onClick={() => onSel(`cat:${c.id}`)} onDoubleClick={() => !c.builtin && onEditCat(c)}
        twisty={kids.length ? isOpen : null} onTwisty={() => setOpen((o) => ({ ...o, [c.id]: !isOpen }))} />,
      ...(kids.length && isOpen ? tree(c.id, depth + 1) : []),
    ];
  });
  return (
    <ScrollView style={{ flex: 1 }}>
      <View style={{ padding: 8, gap: 1 }}>
        <SideRow icon="🔐" label={t('passwords.side.all')} count={count((e) => !e.arch)} selected={sel === 'all'} onClick={() => onSel('all')} />
        <SideRow icon="⭐" label={t('passwords.side.fav')} count={count((e) => e.fav)} selected={sel === 'fav'} onClick={() => onSel('fav')} />
        <SideRow icon="🗄️" label={t('passwords.side.archived')} count={count((e) => e.arch)} selected={sel === 'archived'} onClick={() => onSel('archived')} />
        <SideRow icon="🔢" label={t('passwords.side.codes')} count={count((e) => e.totp)} selected={sel === 'codes'} onClick={() => onSel('codes')} />
        <SideRow icon="📶" label={t('passwords.side.wifi')} count={count((e) => inCat(e.cat, 'wifi', cats))} selected={sel === 'cat:wifi'} onClick={() => onSel('cat:wifi')} />
        <SideRow icon="📝" label={t('passwords.side.notes')} count={count((e) => inCat(e.cat, 'notes', cats))} selected={sel === 'cat:notes'} onClick={() => onSel('cat:notes')} />
        <SideRow icon="💳" label={t('passwords.side.cards')} count={count((e) => inCat(e.cat, 'cards', cats))} selected={sel === 'cat:cards'} onClick={() => onSel('cat:cards')} />
        <SideRow icon="🛡️" label={t('passwords.side.security')} count={issues} selected={sel === 'security'} onClick={() => onSel('security')} />
        <View style={{ flexDirection: 'row', alignItems: 'center' }}>
          <View style={{ flex: 1 }}><Section>{t('passwords.side.categories')}</Section></View>
          <Text onClick={onNewCat} style={{ fontSize: 15, color: th.accent, marginRight: 10, marginTop: 8 }}>＋</Text>
        </View>
        {tree('', 0)}
        {tags.length > 0 && <Section>{t('passwords.side.tags')}</Section>}
        {tags.map((tg) => (
          <SideRow key={tg} icon="#" color={tagColor(tg)} label={tg} count={count((e) => (e.tags || []).includes(tg))} selected={sel === `tag:${tg}`}
            onClick={() => onSel(`tag:${tg}`)} onDoubleClick={() => onEditTag(tg)} />
        ))}
        <Section>{t('passwords.side.system')}</Section>
        <SideRow icon="👤" label={t('passwords.side.login')} count={st.login.entries.length} selected={sel === 'login'} onClick={() => { reloadLogin(); onSel('login'); }} />
        <SideRow icon="🗑️" label={t('passwords.side.trash')} count={st.entries.filter((e) => e.trash).length} selected={sel === 'trash'} onClick={() => onSel('trash')} />
      </View>
    </ScrollView>
  );
}

// ---- lista ----

function ListRow({ e, cat, selected, onClick }) {
  const th = useTheme();
  const [hover, setHover] = useState(false);
  return (
    <View onClick={onClick} onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)}
      style={{ flexDirection: 'row', alignItems: 'center', gap: 10, paddingHorizontal: 10, paddingVertical: 7, borderRadius: 8,
        backgroundColor: selected ? th.accent : hover ? th.hover : 'transparent' }}>
      <EntryIcon entry={e} cat={cat} size={32} />
      <View style={{ flex: 1, gap: 1 }}>
        <Text style={{ fontSize: 13, fontWeight: 600, color: selected ? '#fff' : th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{e.title || t('passwords.untitled')}</Text>
        <Text style={{ fontSize: 11, color: selected ? 'rgba(255,255,255,0.85)' : th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{e.sub || catName(cat)}</Text>
      </View>
      {e.totp && <Text style={{ fontSize: 10, color: selected ? '#fff' : th.textDim }}>2FA</Text>}
      {e.natt ? <Text style={{ fontSize: 11 }}>📎</Text> : null}
      {e.fav && <Text style={{ fontSize: 12, color: selected ? '#fff' : '#ffd60a' }}>★</Text>}
    </View>
  );
}

// ---- ventana ----

export default function Passwords() {
  const th = useTheme();
  const win = useAppWindow();
  const st = useStore(app);
  const [sel, setSel] = useState('all');
  const [cur, setCur] = useState(null);           // id de la entrada abierta
  const [editing, setEditing] = useState(null);   // null | { entry } | { new: catId }
  const [q, setQ] = useState('');
  const [hits, setHits] = useState(null);         // ids de la búsqueda
  const [searchMs, setSearchMs] = useState(null);
  const [sort, setSort] = useState('title');
  const [sheet, setSheet] = useState(null);       // gen | settings | import | export | cat | tag
  const [sheetArg, setSheetArg] = useState(null);
  const [issues, setIssues] = useState(0);

  // protegida: negra en capturas, grabaciones, escritorio remoto y miniaturas
  useEffect(() => { if (win?.id) wm.setProtected(win.id, true); }, [win?.id]);
  useEffect(() => { wireEvents(); refreshStatus(); }, []);
  useEffect(() => {
    if (st.phase !== 'unlocked') return;
    req('report').then((r) => setIssues(r.weak.length + r.reused.reduce((a, g) => a + g.length, 0) + r.old.length)).catch(() => {});
  }, [st.phase, st.entries.length]);
  useEffect(() => { if (st.phase !== 'unlocked') { setCur(null); setEditing(null); setSheet(null); } }, [st.phase]);
  useEffect(() => {
    if (!q.trim() || sel === 'login') { setHits(null); return undefined; }
    const id = setTimeout(() => {
      req('search', { q }).then((r) => { setHits(new Set(r.ids)); setSearchMs(r.ms); }).catch(() => setHits(null));
    }, 60);
    return () => clearTimeout(id);
  }, [q, sel, st.entries]);

  const lock = () => req('lock').then(refreshStatus).catch(() => {});
  const newEntry = () => {
    const catId = sel.startsWith('cat:') ? sel.slice(4) : sel === 'login' || sel === 'trash' || sel === 'security' || sel === 'codes' ? 'internet' : 'internet';
    setEditing({ new: catId });
    setCur(null);
  };
  const openSheet = (s, arg = null) => { setSheet(s); setSheetArg(arg); };

  useEffect(() => {
    if (!win?.id) return;
    const unlocked = st.phase === 'unlocked';
    wm.setMenus(win.id, [
      { label: t('passwords.menu.file'), items: [
        { label: t('passwords.menu.new'), hint: '❖N', onClick: newEntry, disabled: !unlocked },
        { label: t('passwords.menu.newCat'), onClick: () => openSheet('cat'), disabled: !unlocked }, '-',
        { label: t('passwords.menu.import'), onClick: () => openSheet('import'), disabled: !unlocked },
        { label: t('passwords.menu.export'), onClick: () => openSheet('export'), disabled: !unlocked }, '-',
        { label: t('passwords.menu.lock'), hint: '❖⇧L', onClick: lock, disabled: !unlocked },
      ] },
      { label: t('passwords.menu.tools'), items: [
        { label: t('passwords.menu.generator'), hint: '❖G', onClick: () => openSheet('gen'), disabled: !unlocked },
        { label: t('passwords.menu.security'), onClick: () => setSel('security'), disabled: !unlocked },
        { label: t('passwords.menu.settings'), hint: '❖,', onClick: () => openSheet('settings'), disabled: !unlocked },
      ] },
    ]);
  }, [win?.id, st.phase, sel]);

  useKeyboard((e) => {
    if (!e.meta || st.phase !== 'unlocked') return;
    const k = String(e.base || e.key || '').toLowerCase();
    if (k === 'n') newEntry();
    else if (k === 'g') openSheet('gen');
    else if (k === ',') openSheet('settings');
    else if (k === 'l' && e.shift) lock();
  });

  const cats = useMemo(() => allCategories(st.cats), [st.cats]);
  const wrap = (el) => <View style={{ flex: 1 }}>{el}<Overlays /></View>;
  if (!vaultAvailable()) return <Center text={t('passwords.unavailable')} />;
  if (st.phase === 'loading') return <Center text="" />;
  if (st.phase === 'unavailable') return <Center text={t('passwords.unavailable')} />;
  if (st.phase === 'none') return wrap(<CreateView />);
  if (st.phase === 'locked') return wrap(<UnlockView />);

  const v = sel === 'login' ? 'login' : 'main';
  const base = sel === 'login' ? st.login.entries : st.entries;
  let list = base.filter((e) => {
    if (sel === 'login') return true;
    if (sel === 'trash') return !!e.trash;
    if (e.trash) return false;
    if (sel === 'all') return !e.arch;
    if (sel === 'fav') return e.fav;
    if (sel === 'archived') return e.arch;
    if (sel.startsWith('cat:')) return inCat(e.cat, sel.slice(4), cats);
    if (sel.startsWith('tag:')) return (e.tags || []).includes(sel.slice(4));
    return true;
  });
  if (q.trim()) {
    if (sel === 'login') { const qq = q.toLowerCase(); list = list.filter((e) => `${e.title} ${e.sub} ${e.url || ''}`.toLowerCase().includes(qq)); }
    else if (hits) list = list.filter((e) => hits.has(e.id));
  }
  list = [...list].sort(sort === 'title' ? (a, b) => String(a.title).localeCompare(String(b.title)) : (a, b) => b.modified - a.modified);
  const title = sel === 'all' ? t('passwords.side.all') : sel === 'fav' ? t('passwords.side.fav') : sel === 'archived' ? t('passwords.side.archived')
    : sel === 'trash' ? t('passwords.side.trash') : sel === 'login' ? t('passwords.side.login') : sel.startsWith('tag:') ? `#${sel.slice(4)}`
      : sel.startsWith('cat:') ? catName(cats.find((c) => c.id === sel.slice(4)) || builtinById(sel.slice(4))) : '';
  const fullPane = sel === 'security' || sel === 'codes';
  const catOf = (e) => cats.find((c) => c.id === e.cat) || builtinById(e.cat) || { id: e.cat };

  let detail;
  if (editing) {
    detail = <EntryEditor key={editing.entry?.id || `new-${editing.new}`} v={v} entry={editing.entry} catId={editing.new}
      onDone={(id) => { setEditing(null); setCur(id); }} onCancel={() => setEditing(null)} />;
  } else if (cur) {
    detail = <EntryView key={cur} v={v} id={cur} onEdit={(entry) => setEditing({ entry })} onDeleted={() => setCur(null)} />;
  } else {
    detail = <Center text={list.length ? t('passwords.pick') : ''}><Icon size={64} /></Center>;
  }

  return (
    <View style={{ flex: 1, flexDirection: 'row', backgroundColor: th.windowBg }}>
      <View style={{ width: 230, backgroundColor: th.sidebarBg, borderRight: `1px solid ${th.separator}` }}>
        <View style={{ padding: 10, paddingBottom: 4 }}>
          <TextInput value={q} onChange={setQ} placeholder={t('passwords.search')} onCancel={() => setQ('')} />
        </View>
        <Sidebar sel={sel} onSel={(s) => { setSel(s); setCur(null); setEditing(null); }} issues={issues}
          onNewCat={() => openSheet('cat')} onEditCat={(c) => openSheet('cat', c)} onEditTag={(tg) => openSheet('tag', tg)} />
        <View style={{ flexDirection: 'row', gap: 6, padding: 8, borderTop: `1px solid ${th.separator}` }}>
          <Button label="⚄" onClick={() => openSheet('gen')} style={{ paddingHorizontal: 10 }} />
          <Button label="⚙" onClick={() => openSheet('settings')} style={{ paddingHorizontal: 10 }} />
          <View style={{ flex: 1 }} />
          <Button label={`🔒 ${t('passwords.lock')}`} onClick={lock} />
        </View>
      </View>
      {fullPane ? (
        <View style={{ flex: 1 }}>{sel === 'security'
          ? <SecurityView onOpen={(id) => { setSel('all'); setCur(id); }} />
          : <CodesView onOpen={(id) => { setSel('all'); setCur(id); }} />}</View>
      ) : (
        <>
          <View style={{ width: 300, borderRight: `1px solid ${th.separator}` }}>
            <View style={{ flexDirection: 'row', alignItems: 'center', paddingHorizontal: 12, paddingTop: 12, paddingBottom: 6, gap: 6 }}>
              <View style={{ flex: 1 }}>
                <Text style={{ fontSize: 17, fontWeight: 700, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{title}</Text>
                <Text style={{ fontSize: 11, color: th.textDim }}>{q.trim() && searchMs != null && sel !== 'login'
                  ? t('passwords.list.found', { count: list.length, ms: searchMs.toFixed(1) }) : t('passwords.list.count', { count: list.length })}</Text>
              </View>
              <Text onClick={() => setSort(sort === 'title' ? 'modified' : 'title')} style={{ fontSize: 11, color: th.textDim }}>{sort === 'title' ? 'A–Z' : '🕒'}</Text>
              {sel !== 'login' && sel !== 'trash' && <Button label="＋" onClick={newEntry} style={{ paddingHorizontal: 10 }} />}
            </View>
            {sel === 'login' && <Text style={{ fontSize: 11, color: th.textDim, paddingHorizontal: 12, paddingBottom: 6, lineHeight: 15 }}>{t('passwords.login.hint')}</Text>}
            {sel === 'trash' && list.length > 0 && <Button label={t('passwords.trash.empty')} style={{ marginHorizontal: 12, marginBottom: 6 }}
              onClick={() => req('empty-trash').then(refreshStatus).catch(() => {})} />}
            <ScrollView style={{ flex: 1 }}>
              <View style={{ paddingHorizontal: 6, paddingBottom: 10 }}>
                {list.slice(0, 600).map((e) => <ListRow key={e.id} e={e} cat={catOf(e)} selected={cur === e.id} onClick={() => { setCur(e.id); setEditing(null); }} />)}
                {list.length > 600 && <Text style={{ fontSize: 11, color: th.textDim, padding: 10 }}>{t('passwords.list.more', { count: list.length - 600 })}</Text>}
                {!list.length && <Text style={{ fontSize: 12, color: th.textDim, padding: 14, textAlign: 'center' }}>{q ? t('passwords.list.noResults') : t('passwords.list.empty')}</Text>}
              </View>
            </ScrollView>
          </View>
          <View style={{ flex: 1 }}>{detail}</View>
        </>
      )}
      {sheet === 'gen' && <GeneratorSheet onClose={() => setSheet(null)} />}
      {sheet === 'settings' && <SettingsSheet onClose={() => setSheet(null)} />}
      {sheet === 'import' && <ImportSheet onClose={() => setSheet(null)} />}
      {sheet === 'export' && <ExportSheet onClose={() => setSheet(null)} />}
      {sheet === 'cat' && <CategorySheet cat={sheetArg} onClose={() => setSheet(null)} />}
      {sheet === 'tag' && <TagSheet tag={sheetArg} onClose={() => setSheet(null)} />}
      <Overlays />
      {st.toast && (
        <View style={{ position: 'absolute', bottom: 18, left: 0, right: 0, alignItems: 'center', zIndex: 80 }}>
          <View style={{ paddingHorizontal: 14, paddingVertical: 8, borderRadius: 18, backgroundColor: th.mode === 'dark' ? '#3a3a3c' : '#1c1c1e', boxShadow: '0 6px 20px rgba(0,0,0,0.35)' }}>
            <Text style={{ fontSize: 12, color: '#fff' }}>{st.toast.text}</Text>
          </View>
        </View>
      )}
    </View>
  );
}

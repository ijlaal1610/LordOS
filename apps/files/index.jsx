// Archivos — explorador al estilo Finder: pestañas, cuatro vistas (íconos, lista, columnas y
// galería), barra lateral con Favoritos, Ubicaciones y Etiquetas, Papelera de freedesktop,
// Vista rápida, Ver información, copiar/cortar/pegar, arrastrar y soltar, y búsqueda.
// Las operaciones de disco van por lord.fs (server/src/fsops.c); ver docs/ARCHIVOS.md.
import {
  View, Text, TextInput, useTheme, useAppWindow, useKeyboard, useStore, useLauncherApps, addListener, wm, fs, clipboard, archive, warmDocs,
  formatBytes, t as tr,
} from '@lordos/sdk';
import { useEffect, useMemo, useRef, useState } from 'react';
import { FolderGlyph, DocGlyph, TAG_COLORS } from './glyphs.jsx';
import { IconView, ListView, ColumnView, GalleryView } from './views.jsx';
import { ContextMenu, QuickLook, InfoPanel, Confirm } from './panels.jsx';
import { loadEntries, sortEntries, groupEntries, sameLoc, userDirs } from './locations.js';
import {
  fsVersion, bump, fileClipboard, copyFiles, cutFiles, paste, transfer, freeName, jobState, jobErrors, tagStore, toggleTag, moveTags,
  openersFor, openEntry,
} from './ops.js';
import { archiveVersion, archiveState, extractEntry, extractHere, compress, isArchiveFile } from './archive.js';

export function Icon({ size: s }) {
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(180deg, #74c7ff, #1f6fe8)',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)`, overflow: 'hidden' }}>
      <View style={{ position: 'absolute', left: s * 0.2, top: s * 0.27, width: s * 0.27, height: s * 0.12,
        borderRadius: s * 0.03, backgroundColor: 'rgba(255,255,255,0.75)' }} />
      <View style={{ position: 'absolute', left: s * 0.2, top: s * 0.33, width: s * 0.6, height: s * 0.42,
        borderRadius: s * 0.06, backgroundColor: 'rgba(255,255,255,0.95)' }} />
    </View>
  );
}

const VIEWS = ['icons', 'list', 'columns', 'gallery'];
const VIEW_GLYPH = { icons: '▦', list: '☰', columns: '▥', gallery: '▭' };
let tabSeq = 1;
const newTab = (loc, from) => ({ id: tabSeq++, loc, back: [], fwd: [], view: from?.view || 'icons', sort: from?.sort || { by: 'name', desc: false }, group: from?.group || 'none', chain: [] });
// Estado de cada ventana: sobrevive a que la interfaz se vuelva a montar (cambio de idioma).
const winStates = new Map();

const homeLoc = () => ({ type: 'dir', path: fs.home() });
function argLoc(args) {
  if (!args?.path) return null;
  const home = fs.home();
  const p = String(args.path).replace(/^~(?=\/|$)/, home);
  return { type: 'dir', path: p.startsWith('/') ? p.replace(/\/+$/, '') || '/' : `${home}/${p}` };
}

function locTitle(loc, dirs) {
  switch (loc.type) {
    case 'recent': return tr('files.place.recent');
    case 'apps': return tr('files.place.apps');
    case 'trash': return tr('files.place.trash');
    case 'tag': return tr(`files.tag.${loc.color}`);
    case 'search': return tr('files.searchTitle', { q: loc.query });
    case 'archive': return loc.inner ? fs.basename(loc.inner) : fs.basename(loc.path);
    default: {
      if (loc.path === fs.home()) return tr('files.place.home');
      const d = dirs.find((x) => x.path === loc.path);
      return d ? tr(`files.place.${d.label}`) : fs.basename(loc.path);
    }
  }
}

// ---- piezas de la ventana ----

function ToolButton({ label, onClick, disabled, active, t }) {
  return (
    <View onClick={() => !disabled && onClick()} style={{ minWidth: 28, height: 26, paddingHorizontal: 7, borderRadius: 6, alignItems: 'center', justifyContent: 'center',
      backgroundColor: active ? t.accent : 'transparent', opacity: disabled ? 0.35 : 1 }}>
      <Text style={{ fontSize: 14, fontWeight: 600, color: active ? '#fff' : t.text }}>{label}</Text>
    </View>
  );
}

function SideItem({ label, icon, selected, onClick, t, drop, hot }) {
  return (
    <View onClick={onClick} {...drop}
      style={{ height: 26, flexDirection: 'row', alignItems: 'center', gap: 8, paddingHorizontal: 8, borderRadius: 6,
        backgroundColor: hot ? t.accent : selected ? (t.mode === 'dark' ? 'rgba(255,255,255,0.12)' : 'rgba(0,0,0,0.08)') : 'transparent' }}>
      <View style={{ width: 18, alignItems: 'center' }}>{icon}</View>
      <Text style={{ flex: 1, fontSize: 13, color: hot ? '#fff' : t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{label}</Text>
    </View>
  );
}

const SideTitle = ({ children, t }) => <Text style={{ fontSize: 11, fontWeight: 700, color: t.textDim, marginLeft: 8, marginTop: 12, marginBottom: 2 }}>{children}</Text>;
const Dot = ({ color }) => <View style={{ width: 10, height: 10, borderRadius: 5, backgroundColor: color }} />;
const Line = ({ t }) => <View style={{ height: 1, backgroundColor: t.separator }} />;
const Glyph = ({ ch, t }) => <Text style={{ fontSize: 13, color: t.accent }}>{ch}</Text>;

// ---- la ventana ----

export default function Files() {
  const t = useTheme();
  const { id: wid, args } = useAppWindow();
  const launcherApps = useLauncherApps();
  const version = useStore(fsVersion);
  const tags = useStore(tagStore);
  const arcVer = useStore(archiveVersion);
  const clip = useStore(fileClipboard);
  const jobs = useStore(jobState);
  const errs = useStore(jobErrors);
  const saved = winStates.get(wid);
  const [tabs, setTabs] = useState(() => saved?.tabs || [newTab(argLoc(args) || homeLoc())]);
  const [active, setActive] = useState(() => saved?.active || 0);
  const tab = tabs[Math.min(active, tabs.length - 1)];
  useEffect(() => { winStates.set(wid, { tabs, active }); }, [tabs, active]);
  useEffect(() => () => { if (!wm.get(wid)) winStates.delete(wid); }, []);
  const patchTab = (p) => setTabs((ts) => ts.map((x, i) => (i === active ? { ...x, ...(typeof p === 'function' ? p(x) : p) } : x)));

  const [selected, setSelected] = useState(() => new Set());
  const anchor = useRef(null);
  const [renaming, setRenaming] = useState(null);         // { path, value }
  const [menu, setMenu] = useState(null);                 // { x, y, items }
  const [quick, setQuick] = useState(false);
  const [info, setInfo] = useState(false);
  const [confirm, setConfirm] = useState(null);
  const [query, setQuery] = useState('');
  const [scope, setScope] = useState('here');
  const [debounced, setDebounced] = useState('');
  const [hidden, setHidden] = useState(false);
  const [expanded, setExpanded] = useState(() => new Set());
  const [drag, setDrag] = useState(null);                 // { paths, x, y, alt }
  const [dropTarget, setDropTarget] = useState(null);
  const [volumes, setVolumes] = useState(() => fs.volumes());
  useEffect(() => { setTimeout(warmDocs, 800); }, []);             // Vista rápida de PDF sin esperar al ayudante
  const [msg, setMsg] = useState(null);
  const root = useRef(null);
  const press = useRef(null);
  const typed = useRef({ text: '', at: 0 });

  const dirs = useMemo(() => userDirs(), []);
  const baseLoc = tab.loc;
  const loc = debounced ? { type: 'search', root: scope === 'home' || baseLoc.type !== 'dir' ? fs.home() : baseLoc.path, query: debounced } : baseLoc;
  const [entries, error] = useMemo(() => {
    try { return [loadEntries(loc, { hidden, launcherApps }), null]; } catch (e) { return [[], e.message]; }
  }, [loc.type, loc.path, loc.color, loc.query, loc.root, loc.inner, hidden, version, tags, launcherApps, arcVer]);
  // Recientes y la Papelera van por fecha (lo más nuevo arriba), como en el Finder, salvo que se elija otro orden
  const sortBy = (loc.type === 'recent' || loc.type === 'trash') && !tab.sortChosen ? { by: 'date', desc: true } : tab.sort;
  const sorted = useMemo(() => sortEntries(entries, sortBy), [entries, sortBy.by, sortBy.desc]);
  const groups = useMemo(() => groupEntries(sorted, tab.group, tr), [sorted, tab.group]);
  const flat = useMemo(() => groups.flatMap((g) => g.items), [groups]);
  const byPath = useMemo(() => new Map(flat.map((e) => [e.path, e])), [flat]);
  const curDir = baseLoc.type === 'dir' ? baseLoc.path : null;
  const title = locTitle(loc, dirs);

  function navigate(next, push = true) {
    if (sameLoc(next, tab.loc)) return;
    setSelected(new Set()); setRenaming(null); setQuery(''); setDebounced(''); setExpanded(new Set());
    patchTab((x) => ({ loc: next, back: push ? [...x.back, x.loc].slice(-50) : x.back, fwd: push ? [] : x.fwd, chain: [] }));
  }
  const goBack = () => { if (!tab.back.length) return; const prev = tab.back[tab.back.length - 1]; setSelected(new Set()); patchTab((x) => ({ loc: prev, back: x.back.slice(0, -1), fwd: [x.loc, ...x.fwd], chain: [] })); };
  const goForward = () => { if (!tab.fwd.length) return; const nxt = tab.fwd[0]; setSelected(new Set()); patchTab((x) => ({ loc: nxt, back: [...x.back, x.loc], fwd: x.fwd.slice(1), chain: [] })); };
  const goUp = () => {
    if (baseLoc.type === 'archive') { navigate(baseLoc.inner ? { type: 'archive', path: baseLoc.path, inner: baseLoc.inner.split('/').slice(0, -1).join('/') } : { type: 'dir', path: fs.parent(baseLoc.path) || '/' }); return; }
    if (curDir && curDir !== '/' && curDir !== fs.home()) navigate({ type: 'dir', path: fs.parent(curDir) || '/' });
  };

  // otras apps abren una carpeta: wm.open('files', { args: { path } })
  useEffect(() => { const l = argLoc(args); if (l && args?.nonce && saved) navigate(l); }, [args?.nonce]);
  useEffect(() => { const id = setInterval(() => setVolumes(fs.volumes()), 4000); return () => clearInterval(id); }, []);
  useEffect(() => { const id = setTimeout(() => setDebounced(query.trim()), 280); return () => clearTimeout(id); }, [query]);
  useEffect(() => { if (!msg) return undefined; const id = setTimeout(() => setMsg(null), 4000); return () => clearTimeout(id); }, [msg]);
  useEffect(() => { if (errs.length) { setMsg(errs[errs.length - 1]); jobErrors.set([]); } }, [errs]);
  useEffect(() => { wm.setTitle(wid, title); }, [title]);

  const sel = flat.filter((e) => selected.has(e.path));
  const selPaths = sel.map((e) => e.path);
  const first = sel[0];
  const local = (ev) => (root.current ? ev.local(root.current) : { x: ev.x, y: ev.y });
  const bounds = () => { const l = root.current ? lord.getLayout(root.current.id) : [0, 0, 800, 600]; return { w: l[2], h: l[3] }; };

  // ---- acciones ----
  const report = (e) => { console.log(`archivos: ${e?.message || e}`); setMsg(String(e?.message || e)); };
  const newFolder = (withPaths) => {
    if (!curDir) return;
    const name = freeName(curDir, tr('files.newFolder'));
    try {
      fs.mkdir(`${curDir}/${name}`);
      if (withPaths && withPaths.length) transfer(withPaths, `${curDir}/${name}`, true);
      bump();
      setSelected(new Set([`${curDir}/${name}`]));
      setRenaming({ path: `${curDir}/${name}`, value: name });
    } catch (e) { report(e); }
  };
  const startRename = (e = first) => { if (e && !e.app && !e.trash && curDir !== null) { setSelected(new Set([e.path])); setRenaming({ path: e.path, value: e.name }); } };
  const commitRename = () => {
    const r = renaming;
    setRenaming(null);
    if (!r) return;
    const name = r.value.trim();
    if (!name || name === fs.basename(r.path) || name.includes('/')) return;
    const to = `${fs.parent(r.path)}/${name}`;
    try { fs.rename(r.path, to); moveTags(r.path, to); bump(); setSelected(new Set([to])); } catch (e) { report(e); }
  };
  // las acciones reciben las rutas a usar (el menú contextual las fija al abrirse: la selección
  // de ese momento, o el elemento del clic derecho si no estaba seleccionado)
  const askDelete = (paths = selPaths) => setConfirm({ text: tr('files.confirm.delete', { count: paths.length }), detail: tr('files.confirm.cantUndo'), ok: tr('files.menu.deleteNow'),
    run: () => { for (const p of paths) { try { fs.removeFromTrash(p); } catch (err) { report(err); } } bump(); } });
  const askEmpty = () => setConfirm({ text: tr('files.confirm.empty'), detail: tr('files.confirm.cantUndo'), ok: tr('files.menu.emptyTrash'),
    run: () => { fs.emptyTrash(); bump(); } });
  const toTrash = (paths = selPaths) => {
    if (!paths.length) return;
    if (loc.type === 'trash') { askDelete(paths); return; }
    let n = 0;
    for (const p of paths) { try { fs.trash(p); n++; } catch (e) { report(e); } }
    if (n) { setMsg(tr('files.trashed', { count: n })); bump(); }
  };
  const restore = (paths = selPaths) => { let n = 0; for (const p of paths) { try { fs.restore(p); n++; } catch (err) { report(err); } } if (n) { setMsg(tr('files.restored', { count: n })); bump(); } };
  const duplicate = (paths = selPaths) => { if (curDir && paths.length) transfer(paths, curDir, false); };
  const doPaste = () => { if (curDir) paste(curDir); };
  const ctx = { navigate, linuxApps: launcherApps, t: tr, quickLook: (e) => { setSelected(new Set([e.path])); setQuick(true); },
    openArc: (e) => { setMsg(tr('docs.opening', { name: e.name })); extractEntry(e).then((real) => openEntry({ name: e.name, path: real, size: e.size, mtime: e.mtime }, ctx)).catch((err) => setMsg(tr('docs.extractError', { name: e.name, error: err.code || err.message }))); } };
  const arcReport = (m) => setMsg(m);
  const open = (e) => { if (e.trash) { setSelected(new Set([e.path])); setQuick(true); return; } openEntry(e, ctx); };
  const openSel = (list = sel) => { for (const e of list.slice(0, 8)) open(e); };
  const newTabAt = (l) => { setTabs((ts) => [...ts, newTab(l || tab.loc, tab)]); setActive(tabs.length); setSelected(new Set()); };
  const closeTab = (i) => {
    if (tabs.length === 1) { wm.close(wid); return; }
    setTabs((ts) => ts.filter((_, j) => j !== i));
    setActive((a) => (a >= i && a > 0 ? a - 1 : a));
  };
  const setView = (view) => patchTab({ view });
  const setSort = (by) => patchTab((x) => ({ sortChosen: true, sort: { by, desc: x.sort.by === by ? !x.sort.desc : by === 'date' || by === 'size' } }));

  // ---- selección ----
  const onItemDown = (e, ev) => {
    if (renaming && renaming.path !== e.path) commitRename();
    if (ev.button === 2) return;
    let next;
    if (ev.meta || ev.ctrl) { next = new Set(selected); if (next.has(e.path)) next.delete(e.path); else next.add(e.path); anchor.current = e.path; }
    else if (ev.shift && anchor.current) {
      const a = flat.findIndex((x) => x.path === anchor.current), b = flat.findIndex((x) => x.path === e.path);
      next = new Set(flat.slice(Math.min(a, b), Math.max(a, b) + 1).map((x) => x.path));
    } else { next = selected.has(e.path) ? selected : new Set([e.path]); anchor.current = e.path; }
    setSelected(next);
    if (!e.app && loc.type !== 'trash') press.current = { paths: next.has(e.path) ? [...next] : [e.path], x: ev.x, y: ev.y };
  };

  // ---- arrastrar y soltar (⌥ copia; si no, mueve) ----
  const dropOf = (inst) => { for (let n = inst; n; n = n.parent) { if (n.props?.dropPath) return n.props.dropPath; if (n.props?.dropTrash) return 'trash:'; } return null; };
  useEffect(() => {
    const mv = addListener('pointermove', (e) => {
      const p = press.current;
      if (!p) return;
      if (!p.moved && Math.hypot(e.x - p.x, e.y - p.y) < 6) return;
      p.moved = true;
      const target = dropOf(e.target);
      setDrag({ paths: p.paths, x: e.x, y: e.y, alt: e.alt });
      setDropTarget(target && !p.paths.includes(target) ? target : null);
    });
    const up = addListener('pointerup', (e) => {
      const p = press.current;
      press.current = null;
      if (!p || !p.moved) return;
      const target = dropOf(e.target);
      setDrag(null); setDropTarget(null);
      if (!target || p.paths.includes(target)) return;
      if (target === 'trash:') { let n = 0; for (const x of p.paths) { try { fs.trash(x); n++; } catch (err) { setMsg(err.message); } } if (n) bump(); return; }
      transfer(p.paths, target, !e.alt);
    });
    return () => { mv(); up(); };
  }, []);

  // ---- menús contextuales ----
  const tagSwatches = (paths) => ({ swatches: Object.entries(TAG_COLORS).map(([color, hex]) => ({ color, hex, on: paths.every((p) => (tags[p] || []).includes(color)), onClick: () => toggleTag(paths, color) })) });
  function itemMenu(e) {
    const paths = selected.has(e.path) ? selPaths : [e.path];
    const list = selected.has(e.path) ? sel : [e];
    if (loc.type === 'trash') {
      return [{ label: tr('files.menu.restore'), onClick: () => restore(paths) }, { label: tr('files.menu.deleteNow'), danger: true, onClick: () => askDelete(paths) }, '-',
        { label: tr('files.menu.quickLook'), hint: '␣', onClick: () => setQuick(true) }, { label: tr('files.menu.getInfo'), hint: '❖I', onClick: () => setInfo(true) }];
    }
    if (e.app) return [{ label: tr('files.menu.open'), onClick: () => open(e) }];
    if (e.arc) {
      // adentro de un comprimido: abrir, ver y extraer (no se puede modificar)
      return [{ label: tr('files.menu.open'), onClick: () => open(e) }, { label: tr('files.menu.quickLook'), hint: '␣', onClick: () => setQuick(true) }, '-',
        { label: tr('docs.extractSelected', { count: list.length }), onClick: () => { const dir = fs.parent(e.arc.file); const name = freeName(dir, archive.stem(fs.basename(e.arc.file)));
          setMsg(tr('docs.extracting', { name: fs.basename(e.arc.file) }));
          archive.extract(e.arc.file, `${dir}/${name}`, { only: list.map((x) => x.arc.inner) }).then((r) => { bump(); setMsg(tr('docs.extracted', { name, count: r.files })); }).catch((err) => setMsg(tr('docs.extractError', { name, error: err.code || err.message }))); } }];
    }
    const openers = openersFor(e, launcherApps, tr);
    return [
      { label: tr('files.menu.open'), hint: '❖↓', onClick: () => openSel(list) },
      ...(e.dir ? [{ label: tr('files.menu.openTab'), onClick: () => newTabAt({ type: 'dir', path: e.path }) }] : []),
      ...(!e.dir && openers.length ? [{ label: tr('files.menu.openWith'), submenu: openers.map((o) => ({ label: o.label, onClick: o.run })) }] : []),
      '-',
      { label: tr('files.menu.trash'), hint: '❖⌫', onClick: () => toTrash(paths) },
      '-',
      // comprimir y extraer (M9, lord-doc)
      ...(curDir && list.length === 1 && isArchiveFile(e) ? [{ label: tr('docs.extractHere'), onClick: () => extractHere(e, arcReport, tr) }] : []),
      ...(curDir ? [{ label: list.length === 1 ? tr('docs.compressOne', { name: e.name }) : tr('docs.compressMany', { count: list.length }), onClick: () => compress(paths, curDir, arcReport, tr) }] : []),
      '-',
      { label: tr('files.menu.getInfo'), hint: '❖I', onClick: () => setInfo(true) },
      { label: tr('files.menu.rename'), hint: '↵', onClick: () => startRename(e), disabled: paths.length > 1 || !curDir },
      { label: tr('files.menu.duplicate'), hint: '❖D', onClick: () => duplicate(paths), disabled: !curDir },
      { label: tr('files.menu.quickLook'), hint: '␣', onClick: () => setQuick(true) },
      '-',
      { label: tr('files.menu.copy'), hint: '❖C', onClick: () => copyFiles(paths) },
      { label: tr('files.menu.cut'), hint: '❖X', onClick: () => cutFiles(paths) },
      { label: tr('files.menu.copyPath'), onClick: () => clipboard.set(paths.join('\n')) },
      ...(curDir ? [{ label: tr('files.menu.newFolderWith', { count: paths.length }), onClick: () => newFolder(paths) }] : []),
      '-',
      { header: tr('files.menu.tags') }, tagSwatches(paths),
    ];
  }
  function bgMenu() {
    const viewItems = VIEWS.map((v, i) => ({ label: tr(`files.view.${v}`), hint: `❖${i + 1}`, checked: tab.view === v, onClick: () => setView(v) }));
    const sortItems = ['name', 'kind', 'date', 'size'].map((k) => ({ label: tr(`files.sort.${k}`), checked: tab.sort.by === k, onClick: () => setSort(k) }));
    const groupItems = ['none', 'kind', 'date', 'size'].map((k) => ({ label: tr(`files.groupBy.${k}`), checked: tab.group === k, onClick: () => patchTab({ group: k }) }));
    if (loc.type === 'trash') return [{ label: tr('files.menu.emptyTrash'), danger: true, onClick: askEmpty, disabled: !entries.length }, '-', { label: tr('files.menu.view'), submenu: viewItems }];
    return [
      ...(curDir ? [{ label: tr('files.menu.newFolder'), hint: '❖⇧N', onClick: () => newFolder(null) }, { label: tr('files.menu.paste'), hint: 'Ctrl+V', onClick: doPaste, disabled: !clip }, '-'] : []),
      { label: tr('files.menu.view'), submenu: viewItems },
      { label: tr('files.menu.sortBy'), submenu: sortItems },
      { label: tr('files.menu.groupBy'), submenu: groupItems },
      '-',
      { label: tr('files.menu.showHidden'), hint: '❖⇧.', checked: hidden, onClick: () => setHidden((h) => !h) },
      ...(curDir ? [{ label: tr('files.menu.getInfo'), onClick: () => { setSelected(new Set()); setInfo(true); } }] : []),
    ];
  }
  const onContext = (e, ev) => {
    if (!selected.has(e.path)) setSelected(new Set([e.path]));
    const p = local(ev);
    setMenu({ x: p.x, y: p.y, items: itemMenu(e) });
  };
  const onBgContext = (ev) => { setSelected(new Set()); const p = local(ev); setMenu({ x: p.x, y: p.y, items: bgMenu() }); };

  // ---- teclado ----
  const cols = () => { const w = root.current ? lord.getLayout(root.current.id)[2] - 196 : 600; return Math.max(1, Math.floor((w - 16) / 108)); };
  const moveSel = (d, ev) => {
    if (!flat.length) return;
    const i = first ? flat.findIndex((x) => x.path === (anchor.current && selected.has(anchor.current) ? anchor.current : first.path)) : -1;
    const j = Math.max(0, Math.min(flat.length - 1, i < 0 ? 0 : i + d));
    const p = flat[j].path;
    if (ev?.shift && first) setSelected(new Set([...selected, p])); else setSelected(new Set([p]));
    anchor.current = p;
  };
  useKeyboard((e) => {
    if (renaming || confirm || menu) return;
    const mod = e.meta || e.ctrl;
    const b = (e.base || e.char || '').toLowerCase();
    const space = e.char === ' ' || e.key === ' ' || b === ' ';
    if (quick) {
      if (e.key === 'Escape' || space) { setQuick(false); return; }
      if (e.key === 'ArrowRight' || e.key === 'ArrowDown') { moveSel(1); return; }
      if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') { moveSel(-1); return; }
      return;
    }
    if (mod && e.key === 'Tab') { setActive((a) => (a + (e.shift ? tabs.length - 1 : 1)) % tabs.length); return; }
    if (mod && e.shift && b === 'n') { newFolder(null); return; }
    if (mod && b === 'n') { wm.open('files', { new: true, args: { path: curDir || fs.home() } }); return; }
    if (mod && b === 't') { newTabAt(); return; }
    if (mod && b === 'w') { closeTab(active); return; }
    if (mod && e.shift && (b === '.' || b === '>' || b === ':')) { setHidden((h) => !h); return; }
    if (mod && ['1', '2', '3', '4'].includes(b)) { setView(VIEWS[Number(b) - 1]); return; }
    if (mod && b === 'i') { setInfo((v) => !v); return; }
    if (mod && b === 'd') { duplicate(); return; }
    if (mod && b === 'c') { if (selPaths.length) copyFiles(selPaths); return; }
    if (mod && b === 'x') { if (selPaths.length) cutFiles(selPaths); return; }
    if (e.ctrl && b === 'v') { doPaste(); return; }
    if (mod && b === 'a') { setSelected(new Set(flat.map((x) => x.path))); return; }
    if (mod && b === '[') { goBack(); return; }
    if (mod && b === ']') { goForward(); return; }
    if (mod && e.key === 'ArrowUp') { goUp(); return; }
    if (mod && e.key === 'ArrowDown') { openSel(); return; }
    if (mod && (e.key === 'Backspace' || e.key === 'Delete')) { toTrash(); return; }
    if (e.key === 'Delete') { toTrash(); return; }
    if (e.key === 'Enter') { startRename(); return; }
    if (e.key === 'Escape') { setSelected(new Set()); setInfo(false); return; }
    if (space) { if (sel.length) setQuick(true); return; }
    const grid = tab.view === 'icons' ? cols() : 1;
    if (e.key === 'ArrowRight') { if (tab.view === 'list') { if (first?.dir) setExpanded((x) => new Set([...x, first.path])); } else moveSel(1, e); return; }
    if (e.key === 'ArrowLeft') { if (tab.view === 'list') { if (first?.dir) setExpanded((x) => { const n = new Set(x); n.delete(first.path); return n; }); } else moveSel(-1, e); return; }
    if (e.key === 'ArrowDown') { moveSel(grid, e); return; }
    if (e.key === 'ArrowUp') { moveSel(-grid, e); return; }
    if (e.key === 'Home') { if (flat[0]) setSelected(new Set([flat[0].path])); return; }
    if (e.key === 'End') { if (flat.length) setSelected(new Set([flat[flat.length - 1].path])); return; }
    // escribir el comienzo de un nombre lo selecciona
    if (e.char && !mod && e.char.trim()) {
      const now = Date.now();
      const txt = (now - typed.current.at < 900 ? typed.current.text : '') + e.char.toLowerCase();
      typed.current = { text: txt, at: now };
      const hit = flat.find((x) => x.name.toLowerCase().startsWith(txt));
      if (hit) { setSelected(new Set([hit.path])); anchor.current = hit.path; }
    }
  });

  // ---- menús de la app (barra de menú del tema) ----
  useEffect(() => {
    wm.setMenus(wid, [
      { label: tr('files.menu.file'), items: [
        { label: tr('files.menu.newWindow'), hint: '❖N', onClick: () => wm.open('files', { new: true, args: { path: curDir || fs.home() } }) },
        { label: tr('files.menu.newTab'), hint: '❖T', onClick: () => newTabAt() },
        { label: tr('files.menu.newFolder'), hint: '❖⇧N', onClick: () => newFolder(null), disabled: !curDir }, '-',
        { label: tr('files.menu.open'), hint: '❖↓', onClick: () => openSel(), disabled: !sel.length },
        { label: tr('files.menu.getInfo'), hint: '❖I', onClick: () => setInfo(true) },
        { label: tr('files.menu.rename'), hint: '↵', onClick: () => startRename(), disabled: sel.length !== 1 },
        { label: tr('files.menu.duplicate'), hint: '❖D', onClick: () => duplicate(), disabled: !sel.length || !curDir },
        { label: tr('files.menu.trash'), hint: '❖⌫', onClick: () => toTrash(), disabled: !sel.length }, '-',
        { label: tr('files.menu.closeTab'), hint: '❖W', onClick: () => closeTab(active) },
      ] },
      { label: tr('files.menu.edit'), items: [
        { label: tr('files.menu.copy'), hint: '❖C', onClick: () => copyFiles(selPaths), disabled: !sel.length },
        { label: tr('files.menu.cut'), hint: '❖X', onClick: () => cutFiles(selPaths), disabled: !sel.length },
        { label: tr('files.menu.paste'), hint: 'Ctrl+V', onClick: doPaste, disabled: !clip || !curDir },
        { label: tr('files.menu.selectAll'), hint: '❖A', onClick: () => setSelected(new Set(flat.map((x) => x.path))) },
      ] },
      { label: tr('files.menu.view'), items: [
        ...VIEWS.map((v, i) => ({ label: tr(`files.view.${v}`), hint: `❖${i + 1}`, checked: tab.view === v, onClick: () => setView(v) })), '-',
        { label: tr('files.menu.showHidden'), hint: '❖⇧.', checked: hidden, onClick: () => setHidden((h) => !h) },
      ] },
      { label: tr('files.menu.go'), items: [
        { label: tr('files.menu.back'), hint: '❖[', onClick: goBack, disabled: !tab.back.length },
        { label: tr('files.menu.forward'), hint: '❖]', onClick: goForward, disabled: !tab.fwd.length },
        { label: tr('files.menu.up'), hint: '❖↑', onClick: goUp, disabled: !curDir || curDir === fs.home() }, '-',
        { label: tr('files.place.recent'), onClick: () => navigate({ type: 'recent' }) },
        { label: tr('files.place.home'), onClick: () => navigate(homeLoc()) },
        ...dirs.map((d) => ({ label: tr(`files.place.${d.label}`), onClick: () => navigate({ type: 'dir', path: d.path }) })),
        { label: tr('files.place.apps'), onClick: () => navigate({ type: 'apps' }) },
        { label: tr('files.place.trash'), onClick: () => navigate({ type: 'trash' }) },
      ] },
    ]);
  }, [tab.view, tab.back.length, tab.fwd.length, tab.loc, hidden, sel.length, clip, curDir, active, tabs.length]);

  // ---- vista ----
  const cut = new Set(clip?.mode === 'cut' ? clip.paths : []);
  const viewCtx = {
    selected, cut, renaming, dropTarget, hidden, expanded, showFolder: loc.type === 'search' || loc.type === 'recent' || loc.type === 'tag',
    emptyText: (error === 'archive-invalid' ? tr('docs.err.invalid') : error) || (loc.type === 'archive' && archiveState(loc.path)?.state === 'loading' ? tr('docs.loading') : null) || (loc.type === 'trash' ? tr('files.trashEmpty') : loc.type === 'search' ? tr('files.noResults') : tr('files.empty')),
    onItemDown, onOpen: open, onContext,
    onRenameChange: (v) => setRenaming((r) => (r ? { ...r, value: v } : r)), onRenameCommit: commitRename, onRenameCancel: () => setRenaming(null),
    onToggleExpand: (p) => setExpanded((x) => { const n = new Set(x); if (n.has(p)) n.delete(p); else n.add(p); return n; }),
    isDir: (p) => { const e = byPath.get(p); if (e) return e.dir; try { return lord.fs.stat(p).dir; } catch (err) { return false; } },
    entryAt: (p) => byPath.get(p) || (() => { try { const s = lord.fs.stat(p); return { name: s.name, path: s.path, dir: s.dir, size: s.size, mtime: s.mtime }; } catch (err) { return null; } })(),
  };
  // columnas solo en carpetas; en los demás lugares, la lista
  const view = tab.view === 'columns' && loc.type !== 'dir' ? 'list' : tab.view;
  const entriesOf = (dir) => { try { return lord.fs.list(dir).filter((x) => hidden || !x.name.startsWith('.')).map((x) => ({ ...x, path: `${dir}/${x.name}` })); } catch (e) { return []; } };
  const freeSpace = useMemo(() => { try { return curDir ? fs.free(curDir) : null; } catch (e) { return null; } }, [curDir, version]);
  const quickEntry = quick ? (first || flat[0]) : null;
  const infoEntry = info ? (first || (curDir ? { name: fs.basename(curDir), path: curDir, dir: true, size: 0 } : null)) : null;
  const job = jobs[0];
  const crumbs = (() => {
    if (loc.type !== 'dir') return [{ label: title, loc }];
    const home = fs.home();
    const base = loc.path === home || loc.path.startsWith(`${home}/`) ? home
      : loc.path.startsWith('/run/media/') ? loc.path.split('/').slice(0, 5).join('/') : loc.path.startsWith('/media/') ? loc.path.split('/').slice(0, 4).join('/') : '/';
    const rest = loc.path.slice(base.length).split('/').filter(Boolean);
    const out = [{ label: base === home ? tr('files.place.home') : fs.basename(base), loc: { type: 'dir', path: base } }];
    let acc = base;
    for (const r of rest) { acc = `${acc}/${r}`; out.push({ label: r, loc: { type: 'dir', path: acc } }); }
    return out;
  })();
  const sideSel = (l) => sameLoc(l, baseLoc) && !debounced;
  const dirLoc = (p) => ({ type: 'dir', path: p });
  const dragLocal = drag && root.current ? (() => { const [lx, ly] = lord.getLayout(root.current.id); return { x: drag.x - lx, y: drag.y - ly }; })() : null;
  const dragEntry = drag ? byPath.get(drag.paths[0]) : null;

  return (
    <View ref={root} style={{ flex: 1, flexDirection: 'row' }}>
      {/* barra lateral */}
      <View style={{ width: 196, backgroundColor: t.sidebarBg, paddingHorizontal: 8, paddingTop: 4 }}>
        <SideTitle t={t}>{tr('files.favorites')}</SideTitle>
        <SideItem t={t} label={tr('files.place.recent')} icon={<Glyph ch="◷" t={t} />} selected={sideSel({ type: 'recent' })} onClick={() => navigate({ type: 'recent' })} />
        <SideItem t={t} label={tr('files.place.apps')} icon={<Glyph ch="⌘" t={t} />} selected={sideSel({ type: 'apps' })} onClick={() => navigate({ type: 'apps' })} />
        {dirs.map((d) => (
          <SideItem key={d.path} t={t} label={tr(`files.place.${d.label}`)} icon={<FolderGlyph size={16} />} selected={sideSel(dirLoc(d.path))}
            onClick={() => navigate(dirLoc(d.path))} drop={{ dropPath: d.path }} hot={dropTarget === d.path} />
        ))}
        <SideTitle t={t}>{tr('files.locations')}</SideTitle>
        <SideItem t={t} label={tr('files.place.home')} icon={<Glyph ch="⌂" t={t} />} selected={sideSel(homeLoc())} onClick={() => navigate(homeLoc())}
          drop={{ dropPath: fs.home() }} hot={dropTarget === fs.home()} />
        {volumes.map((v) => (
          <SideItem key={v.path} t={t} label={v.name} icon={<Glyph ch="⏏" t={t} />} selected={sideSel(dirLoc(v.path))} onClick={() => navigate(dirLoc(v.path))}
            drop={{ dropPath: v.path }} hot={dropTarget === v.path} />
        ))}
        <SideItem t={t} label={tr('files.place.trash')} icon={<Glyph ch="🗑" t={t} />} selected={sideSel({ type: 'trash' })} onClick={() => navigate({ type: 'trash' })}
          drop={{ dropTrash: true }} hot={dropTarget === 'trash:'} />
        <SideTitle t={t}>{tr('files.tags')}</SideTitle>
        {Object.entries(TAG_COLORS).map(([color, hex]) => (
          <SideItem key={color} t={t} label={tr(`files.tag.${color}`)} icon={<Dot color={hex} />} selected={sideSel({ type: 'tag', color })} onClick={() => navigate({ type: 'tag', color })} />
        ))}
      </View>

      <View style={{ flex: 1 }}>
        {/* barra de herramientas */}
        <View style={{ height: 44, flexDirection: 'row', alignItems: 'center', gap: 4, paddingHorizontal: 10 }}>
          <View style={{ flexDirection: 'row', direction: 'ltr' }}>
            <ToolButton t={t} label="‹" onClick={goBack} disabled={!tab.back.length} />
            <ToolButton t={t} label="›" onClick={goForward} disabled={!tab.fwd.length} />
          </View>
          <Text style={{ flex: 1, fontSize: 15, fontWeight: 700, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden', marginLeft: 4 }}>{title}</Text>
          <View style={{ flexDirection: 'row', backgroundColor: t.control, borderRadius: 7, padding: 2, direction: 'ltr' }}>
            {VIEWS.map((v) => <ToolButton key={v} t={t} label={VIEW_GLYPH[v]} active={tab.view === v} onClick={() => setView(v)} />)}
          </View>
          <View onClick={(ev) => { const p = local(ev); setMenu({ x: p.x - 120, y: 40, items: [
            { header: tr('files.menu.groupBy') }, ...['none', 'kind', 'date', 'size'].map((k) => ({ label: tr(`files.groupBy.${k}`), checked: tab.group === k, onClick: () => patchTab({ group: k }) })), '-',
            { header: tr('files.menu.sortBy') }, ...['name', 'kind', 'date', 'size'].map((k) => ({ label: tr(`files.sort.${k}`), checked: tab.sort.by === k, onClick: () => setSort(k) })),
            { label: tab.sort.desc ? tr('files.sort.desc') : tr('files.sort.asc'), onClick: () => patchTab((x) => ({ sort: { ...x.sort, desc: !x.sort.desc } })) },
          ] }); }} style={{ height: 26, paddingHorizontal: 9, borderRadius: 6, justifyContent: 'center', backgroundColor: t.control }}>
            <Text style={{ fontSize: 13, color: t.text }}>⇅</Text>
          </View>
          <View onClick={(ev) => { const p = local(ev); setMenu({ x: p.x - 150, y: 40, items: sel.length && first ? itemMenu(first) : bgMenu() }); }}
            style={{ height: 26, width: 30, borderRadius: 6, alignItems: 'center', justifyContent: 'center', backgroundColor: t.control }}>
            <Text style={{ fontSize: 14, color: t.text }}>⋯</Text>
          </View>
          <TextInput value={query} onChange={setQuery} placeholder={tr('files.search')} onCancel={() => setQuery('')} style={{ width: 170, height: 26 }} />
        </View>
        <Line t={t} />
        {query.trim() !== '' && (
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6, paddingHorizontal: 12, height: 30 }}>
            <Text style={{ fontSize: 12, color: t.textDim }}>{tr('files.searchIn')}</Text>
            {(baseLoc.type === 'dir' ? ['here', 'home'] : ['home']).map((s) => (
              <View key={s} onClick={() => setScope(s)} style={{ paddingHorizontal: 10, height: 22, borderRadius: 11, justifyContent: 'center',
                backgroundColor: scope === s || baseLoc.type !== 'dir' ? t.accent : t.control }}>
                <Text style={{ fontSize: 12, color: scope === s || baseLoc.type !== 'dir' ? '#fff' : t.text }}>{s === 'here' ? `«${locTitle(baseLoc, dirs)}»` : tr('files.place.home')}</Text>
              </View>
            ))}
          </View>
        )}
        {query.trim() !== '' && <Line t={t} />}
        {/* pestañas */}
        {tabs.length > 1 && (
          <View style={{ flexDirection: 'row', height: 28, backgroundColor: t.mode === 'dark' ? 'rgba(0,0,0,0.15)' : 'rgba(0,0,0,0.04)' }}>
            {tabs.map((x, i) => (
              <View key={x.id} onClick={() => { setActive(i); setSelected(new Set()); }} {...(x.loc.type === 'dir' ? { dropPath: x.loc.path } : {})}
                style={{ flex: 1, flexDirection: 'row', alignItems: 'center', paddingHorizontal: 8, gap: 6, border: `1px solid ${t.separator}`,
                  backgroundColor: dropTarget && x.loc.path === dropTarget && i !== active ? t.accent : i === active ? t.windowBg : 'transparent' }}>
                <View onClick={(ev) => { ev.stopPropagation(); closeTab(i); }} style={{ width: 16, alignItems: 'center' }}><Text style={{ fontSize: 10, color: t.textDim }}>✕</Text></View>
                <Text style={{ flex: 1, fontSize: 12, fontWeight: i === active ? 600 : 400, color: i === active ? t.text : t.textDim, textAlign: 'center', whiteSpace: 'nowrap', overflow: 'hidden' }}>{locTitle(x.loc, dirs)}</Text>
              </View>
            ))}
            <View onClick={() => newTabAt()} style={{ width: 30, alignItems: 'center', justifyContent: 'center' }}><Text style={{ fontSize: 15, color: t.textDim }}>+</Text></View>
          </View>
        )}
        {tabs.length > 1 && <Line t={t} />}
        {/* contenido */}
        <View style={{ flex: 1 }} onMouseDown={() => { if (renaming) commitRename(); setSelected(new Set()); }} onContextMenu={onBgContext}
          {...(curDir ? { dropPath: curDir } : {})}>
          {view === 'icons' && <IconView groups={groups} ctx={viewCtx} />}
          {view === 'list' && <ListView groups={groups} ctx={viewCtx} sort={sortBy} onSort={setSort} />}
          {view === 'columns' && <ColumnView root={curDir || fs.home()} chain={tab.chain} setChain={(chain) => patchTab({ chain })} entriesOf={entriesOf} ctx={viewCtx} sort={tab.sort} />}
          {view === 'gallery' && <GalleryView entries={flat} ctx={viewCtx} />}
        </View>
        {/* barra de ruta y de estado */}
        <Line t={t} />
        <View style={{ height: 24, flexDirection: 'row', alignItems: 'center', gap: 4, paddingHorizontal: 10 }}>
          {crumbs.map((c, i) => (
            <View key={i} style={{ flexDirection: 'row', alignItems: 'center', gap: 4 }}>
              {i > 0 && <Text style={{ fontSize: 10, color: t.textDim }}>›</Text>}
              <View onClick={() => navigate(c.loc)} {...(c.loc.type === 'dir' ? { dropPath: c.loc.path } : {})}>
                <Text style={{ fontSize: 11, color: i === crumbs.length - 1 ? t.text : t.textDim, whiteSpace: 'nowrap' }}>{c.label}</Text>
              </View>
            </View>
          ))}
        </View>
        <View style={{ height: 24, flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 10, paddingHorizontal: 10,
          backgroundColor: t.mode === 'dark' ? 'rgba(0,0,0,0.12)' : 'rgba(0,0,0,0.03)' }}>
          {job ? (
            <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
              <Text style={{ fontSize: 11, color: t.textDim }}>{tr(job.move ? 'files.job.moving' : 'files.job.copying', { name: fs.basename(job.src) })}</Text>
              <View style={{ width: 120, height: 5, borderRadius: 3, backgroundColor: t.control, direction: 'ltr' }}>
                <View style={{ width: Math.round(120 * (job.total ? Math.min(1, job.bytes / job.total) : 0)), height: 5, borderRadius: 3, backgroundColor: t.accent }} />
              </View>
              <View onClick={() => fs.cancelJob(job.id)}><Text style={{ fontSize: 11, color: t.textDim }}>✕</Text></View>
            </View>
          ) : (
            <Text style={{ fontSize: 11, color: msg ? t.accent : t.textDim }}>
              {msg || [sel.length ? tr('files.status.selected', { count: sel.length, total: flat.length }) : tr('files.items', { count: flat.length }),
                freeSpace ? tr('files.status.free', { free: formatBytes(freeSpace.free) }) : ''].filter(Boolean).join(', ')}
            </Text>
          )}
        </View>
      </View>

      {/* lo que se arrastra */}
      {dragLocal && (
        <View style={{ position: 'absolute', left: dragLocal.x - 20, top: dragLocal.y - 20, pointerEvents: 'none', opacity: 0.85 }}>
          {dragEntry?.dir ? <FolderGlyph size={40} /> : <DocGlyph size={40} kind="other" ext="" dark={t.mode === 'dark'} />}
          {drag.paths.length > 1 && (
            <View style={{ position: 'absolute', right: -6, top: -6, minWidth: 18, height: 18, borderRadius: 9, paddingHorizontal: 4, backgroundColor: '#ff3b30', alignItems: 'center', justifyContent: 'center' }}>
              <Text style={{ fontSize: 10, fontWeight: 700, color: '#fff' }}>{drag.paths.length}</Text>
            </View>
          )}
          {drag.alt && <Text style={{ position: 'absolute', left: 32, top: 26, fontSize: 15, fontWeight: 700, color: '#30d158' }}>+</Text>}
        </View>
      )}
      {quickEntry && <QuickLook e={quickEntry} index={flat.indexOf(quickEntry)} count={flat.length} onClose={() => setQuick(false)} />}
      {infoEntry && <InfoPanel e={infoEntry} onClose={() => setInfo(false)} openers={infoEntry.dir || infoEntry.app ? [] : openersFor(infoEntry, launcherApps, tr)} />}
      {confirm && <Confirm text={confirm.text} detail={confirm.detail} okLabel={confirm.ok} onCancel={() => setConfirm(null)} onOk={() => { const c = confirm; setConfirm(null); c.run(); }} />}
      {menu && <ContextMenu x={menu.x} y={menu.y} items={menu.items} onClose={() => setMenu(null)} bounds={bounds()} />}
    </View>
  );
}

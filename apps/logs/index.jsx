// Registros — visor de registros estilo Consola de macOS (ver docs/SYSTEM.md, «Registros»).
//
// Fuentes (barra lateral):
//   - LordOS: los registros de la sesión (~/.cache/lordos/*.log: session.log…);
//   - Mis registros: el journal del usuario (user-<uid>.journal), leído por lord-server como el
//     usuario con un lector propio (server/src/logs/journal.c);
//   - Sistema y Kernel: el journal del sistema. Si el usuario está en systemd-journal o adm se lee
//     directo; si no, un administrador lo pide por la acción cerrada logs-system del ayudante.
// Filtros por nivel, tiempo, proceso o unidad y texto; en vivo con inotify (sin sondeo); exportar
// la vista filtrada a Documentos. La lista dibuja solo los renglones que se ven.
import {
  View, Text, TextInput, Segmented, Toggle, Button, SidebarItem, SectionTitle, useTheme, useLocale,
  logs, fs, formatDate, formatNumber, getLayout, addListener, direction,
} from '@lordos/sdk';
import { useEffect, useMemo, useRef, useState } from 'react';

const ROW = 22;
const MAX_ROWS = 10000;              // en vivo: más viejos que esto se descartan
const LEVEL_COLOR = { error: '#ff453a', warning: '#ff9f0a', info: null, debug: null };

// ---- ícono: una hoja oscura con renglones de registro (rojo, amarillo, gris) ----
export function Icon({ size: s = 32 }) {
  const lines = [['#ff453a', 0.42], ['#d0d0d6', 0.62], ['#ffd60a', 0.5], ['#d0d0d6', 0.68], ['#30d158', 0.36]];
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, backgroundColor: '#2b2d33', padding: s * 0.16, gap: s * 0.075,
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)`, direction: 'ltr' }}>
      <View style={{ flexDirection: 'row', gap: s * 0.05, marginBottom: s * 0.02 }}>
        {['#ff5f57', '#febc2e', '#28c840'].map((c) => <View key={c} style={{ width: s * 0.08, height: s * 0.08, borderRadius: s * 0.04, backgroundColor: c }} />)}
      </View>
      {lines.map(([c, w], i) => (
        <View key={i} style={{ flexDirection: 'row', gap: s * 0.05, alignItems: 'center' }}>
          <View style={{ width: s * 0.06, height: s * 0.06, borderRadius: s * 0.03, backgroundColor: c }} />
          <View style={{ width: s * w, height: Math.max(1, s * 0.045), borderRadius: s * 0.02, backgroundColor: i % 2 ? 'rgba(255,255,255,0.45)' : 'rgba(255,255,255,0.7)' }} />
        </View>
      ))}
    </View>
  );
}

// ---- utilidades ----
const pad = (n, w = 2) => String(n).padStart(w, '0');
export function levelOf(p) { return p == null || p < 0 ? 'info' : p <= 3 ? 'error' : p === 4 ? 'warning' : p === 7 ? 'debug' : 'info'; }
function clock(ms) { const d = new Date(ms); return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}.${pad(d.getMilliseconds(), 3)}`; }

// Renglones de un .log de LordOS (no tienen hora): nivel por palabras clave.
export function parseLordLog(text, file) {
  const out = [];
  const lines = text.split('\n');
  for (let i = 0; i < lines.length; i++) {
    const m = lines[i];
    if (!m) continue;
    const low = m.toLowerCase();
    const p = /\b(error|err|falla|fallo|failed|abort|crash|segfault)\b/.test(low) ? 3 : /\b(aviso|warning|warn|sin |no se pudo)\b/.test(low) ? 4 : 6;
    const src = /^([a-z][\w.-]{1,30})(\[\d+\])?:/i.exec(m);
    out.push({ t: 0, line: i + 1, p, id: src ? src[1] : file.replace(/\.log$/, ''), u: '', pid: -1, uid: -1, k: 0, b: '', m });
  }
  return out;
}

// Documentos del usuario (xdg-user-dirs; si no, la carpeta con el nombre del idioma que exista)
function documentsDir() {
  const home = fs.home();
  try {
    const m = /^XDG_DOCUMENTS_DIR="\$HOME\/([^"]+)"/m.exec(fs.read('.config/user-dirs.dirs'));
    if (m) return `${home}/${m[1]}`;
  } catch (e) { /* sin xdg-user-dirs */ }
  for (const d of ['Documentos', 'Documents']) { try { if (fs.stat(`${home}/${d}`)) return `${home}/${d}`; } catch (e) { /* no está */ } }
  return home;
}

// ---- lista virtual: dibuja solo lo que se ve ----
function VirtualList({ items, selected, onSelect, stick, onStick, renderRow }) {
  const t = useTheme();
  const box = useRef(null);
  const [h, setH] = useState(400);
  const [off, setOff] = useState(0);
  const total = items.length * ROW, max = Math.max(0, total - h);
  const y = stick ? max : Math.min(off, max);
  useEffect(() => {
    const id = setTimeout(() => { if (box.current) { const v = getLayout(box.current.id)[3]; if (v && Math.abs(v - h) > 1) setH(v); } }, 30);
    return () => clearTimeout(id);
  });
  const scrollTo = (v) => {
    const c = Math.max(0, Math.min(max, v));
    setOff(c);
    onStick(c >= max - 2);
  };
  // arrastrar la barra
  const drag = useRef(null);
  useEffect(() => {
    const offMove = addListener('pointermove', (e) => { if (drag.current) scrollTo(drag.current.y0 + ((e.y - drag.current.py) * total) / Math.max(1, h)); });
    const offUp = addListener('pointerup', () => { drag.current = null; });
    return () => { offMove(); offUp(); };
  });
  const first = Math.max(0, Math.floor(y / ROW) - 2), last = Math.min(items.length, Math.ceil((y + h) / ROW) + 2);
  const rows = [];
  for (let i = first; i < last; i++) rows.push(renderRow(items[i], i, i === selected, () => onSelect(i)));
  const bar = total > h ? Math.max(28, (h * h) / total) : 0;
  return (
    <View ref={box} onWheel={(e) => scrollTo(y + e.deltaY)} style={{ flex: 1, overflow: 'hidden' }}>
      <View style={{ position: 'absolute', left: 0, right: 0, top: first * ROW - y }}>{rows}</View>
      {bar > 0 && (
        <View onMouseDown={(e) => { drag.current = { py: e.y, y0: y }; }}
          style={{ position: 'absolute', right: 2, width: 7, borderRadius: 4, height: bar, top: (h - bar) * (y / (max || 1)),
            backgroundColor: t.mode === 'dark' ? 'rgba(255,255,255,0.3)' : 'rgba(0,0,0,0.28)' }} />
      )}
    </View>
  );
}

function Dot({ p }) {
  const c = LEVEL_COLOR[levelOf(p)];
  return <View style={{ width: 8, height: 8, borderRadius: 4, backgroundColor: c || 'transparent', border: c ? 'none' : '1px solid rgba(128,128,128,0.5)' }} />;
}

// ---- detalle de una entrada ----
function Detail({ e }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  if (!e) return <Text style={{ color: t.textDim, fontSize: 12, padding: 12 }}>{tr('logs.pick')}</Text>;
  const rows = [
    [tr('logs.field.time'), e.t ? `${formatDate(new Date(e.t / 1000), 'long')} ${clock(e.t / 1000)}` : tr('logs.line', { n: e.line })],
    [tr('logs.field.level'), tr(`logs.level.${levelOf(e.p)}`) + (e.p >= 0 ? ` (${e.p})` : '')],
    [tr('logs.field.process'), e.id || '—'],
    [tr('logs.field.pid'), e.pid >= 0 ? String(e.pid) : '—'],
    [tr('logs.field.unit'), e.u || '—'],
    [tr('logs.field.uid'), e.uid >= 0 ? String(e.uid) : '—'],
    [tr('logs.field.boot'), e.b || '—'],
  ];
  return (
    <View style={{ flexDirection: 'row', gap: 16, padding: 12, flex: 1 }}>
      <View style={{ flex: 1, minWidth: 0 }}>
        <Text style={{ fontSize: 12, color: t.text, fontFamily: 'monospace', lineHeight: 1.35, textAlign: 'start' }}>{e.m}</Text>
      </View>
      <View style={{ width: 260, gap: 3 }}>
        {rows.map(([k, v]) => (
          <View key={k} style={{ flexDirection: 'row', gap: 8 }}>
            <Text style={{ width: 80, fontSize: 11, color: t.textDim, textAlign: 'end' }}>{k}</Text>
            <Text style={{ flex: 1, fontSize: 11, color: t.text, textAlign: 'start' }}>{v}</Text>
          </View>
        ))}
      </View>
    </View>
  );
}

const RANGES = { '15m': 15 * 60e6, hour: 3600e6, day: 86400e6, all: 0 };
const LEVELS = { all: null, info: 6, warning: 4, error: 3 };

export default function Registros() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [src, setSrc] = useState(() => ({ kind: 'user' }));        // { kind: 'user'|'system'|'kernel'|'lordos', file }
  const [items, setItems] = useState([]);
  const [level, setLevel] = useState('all');
  const [range, setRange] = useState('hour');
  const [search, setSearch] = useState('');
  const [proc, setProc] = useState('');
  const [live, setLive] = useState(true);
  const [stick, setStick] = useState(true);
  const [sel, setSel] = useState(-1);
  const [state, setState] = useState({ loading: false, error: '', ms: 0 });
  const [notice, setNotice] = useState('');
  const sources = useMemo(() => { try { return logs.sources(); } catch (e) { return { user: 0, system: false }; } }, []);
  const files = useMemo(() => { try { return logs.files().sort((a, b) => b.mtime - a.mtime); } catch (e) { return []; } }, [src.kind]);
  const query = useRef({ seq: 0 });
  const newest = useRef(0);

  const isJournal = src.kind !== 'lordos';
  const procValid = !proc || logs.validMatch(proc);

  const buildQuery = (since) => {
    const q = { limit: 3000 };
    if (LEVELS[level] != null) q.prio = LEVELS[level];
    if (RANGES[range]) q.since = Date.now() * 1000 - RANGES[range];
    if (since) q.since = since;
    if (proc && procValid) q.match = proc;
    if (search) q.grep = search;
    if (src.kind === 'kernel') q.kernel = true;
    return q;
  };

  const runJournal = (q) => {
    if (src.kind === 'user') return logs.query({ ...q, scope: 'user' });
    if (sources.system) return logs.query({ ...q, scope: 'system' });
    return logs.system(q);                    // administradores, por el ayudante
  };

  // carga completa al cambiar la fuente o los filtros (el texto, con una pausa)
  useEffect(() => {
    const seq = ++query.current.seq;
    const id = setTimeout(async () => {
      setState({ loading: true, error: '', ms: 0 });
      const t0 = Date.now();
      try {
        let rows;
        if (isJournal) rows = await runJournal(buildQuery(0));
        else {
          const text = src.file ? logs.tail(src.file, 512 * 1024) || '' : '';
          rows = parseLordLog(text, src.file || '');
          const s = search.toLowerCase(), max = LEVELS[level];
          rows = rows.filter((r) => (max == null || r.p <= max) && (!s || r.m.toLowerCase().includes(s)) && (!proc || r.id === proc));
        }
        if (seq !== query.current.seq) return;
        newest.current = rows.length ? rows[rows.length - 1].t : Date.now() * 1000;
        setItems(rows);
        setSel(-1);
        setStick(true);
        setState({ loading: false, error: '', ms: Date.now() - t0 });
      } catch (e) {
        if (seq !== query.current.seq) return;
        setItems([]);
        const msg = String(e.message || e);
        setState({ loading: false, error: /administrador/.test(msg) ? tr('logs.needAdmin') : msg, ms: 0 });
      }
    }, search || proc ? 300 : 0);
    return () => clearTimeout(id);
  }, [src.kind, src.file, level, range, search, proc]);

  // en vivo: inotify en lord-server avisa; se piden solo las entradas nuevas (a lo sumo ~1 por segundo)
  useEffect(() => {
    if (!live) return undefined;
    let timer = null, busy = false;
    try { logs.follow(true); } catch (e) { return undefined; }
    const pull = async () => {
      timer = null;
      if (busy) { timer = setTimeout(pull, 600); return; }
      busy = true;
      const seq = query.current.seq;
      try {
        let more;
        if (isJournal) more = await runJournal(buildQuery(newest.current));
        else {
          const all = parseLordLog(logs.tail(src.file, 512 * 1024) || '', src.file || '');
          const s = search.toLowerCase(), max = LEVELS[level];
          setItems(all.filter((r) => (max == null || r.p <= max) && (!s || r.m.toLowerCase().includes(s))));
          more = null;
        }
        if (more && more.length && seq === query.current.seq) {
          newest.current = more[more.length - 1].t;
          setItems((old) => { const n = old.concat(more); return n.length > MAX_ROWS ? n.slice(n.length - MAX_ROWS) : n; });
        }
      } catch (e) { /* se reintenta con el próximo aviso */ }
      busy = false;
    };
    const off = logs.onChange((kind, name) => {
      if (isJournal ? kind !== 'journal' : !(kind === 'file' && name === src.file)) return;
      if (!timer) timer = setTimeout(pull, 700);
    });
    return () => { off(); if (timer) clearTimeout(timer); try { logs.follow(false); } catch (e) { /* ya no */ } };
  }, [live, src.kind, src.file, level, range, search, proc]);

  const exportView = () => {
    const lines = items.map((e) => {
      const when = e.t ? `${formatDate(new Date(e.t / 1000), 'short')} ${clock(e.t / 1000)}` : `#${e.line}`;
      return `${when} ${tr(`logs.level.${levelOf(e.p)}`)} ${e.id || '-'}${e.pid >= 0 ? `[${e.pid}]` : ''}: ${e.m}`;
    });
    const d = new Date();
    const name = `${tr('logs.exportName')}-${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}-${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}.txt`;
    const path = `${documentsDir()}/${name}`;
    try { fs.write(path, lines.join('\n') + '\n'); setNotice(tr('logs.exported', { path: path.replace(fs.home(), '~') })); }
    catch (e) { setNotice(tr('logs.exportFailed', { error: String(e.message || e) })); }
    setTimeout(() => setNotice(''), 5000);
  };

  const counts = useMemo(() => {
    let err = 0, warn = 0;
    for (const e of items) { const l = levelOf(e.p); if (l === 'error') err++; else if (l === 'warning') warn++; }
    return { err, warn };
  }, [items]);

  const renderRow = (e, i, on, pick) => {
    const lv = levelOf(e.p);
    const fg = on ? '#fff' : lv === 'debug' ? t.textDim : t.text;
    return (
      <View key={`${e.t}-${e.s ?? e.line}-${i}`} onClick={pick} style={{ height: ROW, flexDirection: 'row', alignItems: 'center', gap: 8, paddingHorizontal: 10,
        backgroundColor: on ? t.accent : i % 2 ? (t.mode === 'dark' ? 'rgba(255,255,255,0.025)' : 'rgba(0,0,0,0.025)') : 'transparent' }}>
        <Dot p={e.p} />
        <Text style={{ width: 92, fontSize: 11, color: on ? '#fff' : t.textDim, fontFamily: 'monospace', whiteSpace: 'nowrap' }}>{e.t ? clock(e.t / 1000) : `#${e.line}`}</Text>
        <Text style={{ width: 140, fontSize: 12, color: fg, whiteSpace: 'nowrap', overflow: 'hidden', textAlign: 'start' }}>{e.id || '—'}</Text>
        <Text style={{ flex: 1, fontSize: 12, color: fg, whiteSpace: 'nowrap', overflow: 'hidden', textAlign: 'start' }}>{e.m.replace(/\n/g, ' ⏎ ')}</Text>
      </View>
    );
  };

  const side = (kind, label, extra) => (
    <SidebarItem key={kind + (extra?.file || '')} label={label} selected={src.kind === kind && (!extra || src.file === extra.file)}
      onClick={() => setSrc({ kind, ...(extra || {}) })} />
  );

  return (
    <View style={{ flex: 1, flexDirection: 'row' }}>
      <View style={{ width: 200, backgroundColor: t.sidebarBg, paddingHorizontal: 8, paddingTop: 8, gap: 2 }}>
        <SectionTitle>{tr('logs.section.journal')}</SectionTitle>
        {side('user', tr('logs.src.user'))}
        {side('system', tr('logs.src.system'))}
        {side('kernel', tr('logs.src.kernel'))}
        <SectionTitle>{tr('logs.section.lordos')}</SectionTitle>
        {files.length ? files.map((f) => side('lordos', f.name.replace(/\.log$/, ''), { file: f.name })) :
          <Text style={{ fontSize: 11, color: t.textDim, paddingHorizontal: 10, textAlign: 'start' }}>{tr('logs.noFiles')}</Text>}
        <View style={{ flex: 1 }} />
        <Text style={{ fontSize: 10, color: t.textDim, margin: 8, textAlign: 'start' }}>
          {src.kind === 'user' ? tr('logs.hint.user') : src.kind === 'lordos' ? tr('logs.hint.lordos') : sources.system ? tr('logs.hint.systemDirect') : tr('logs.hint.systemAdmin')}
        </Text>
      </View>
      <View style={{ flex: 1, minWidth: 0 }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8, padding: 8, borderBottom: `1px solid ${t.separator}`, flexWrap: 'wrap' }}>
          <TextInput value={search} onChange={setSearch} placeholder={tr('logs.search')} style={{ width: 200 }} />
          <TextInput value={proc} onChange={setProc} placeholder={tr('logs.process')} style={{ width: 150, ...(procValid ? {} : { border: '1px solid #ff453a' }) }} />
          <Segmented value={level} onChange={setLevel} options={[['all', tr('logs.lv.all')], ['info', tr('logs.lv.info')], ['warning', tr('logs.lv.warning')], ['error', tr('logs.lv.error')]]} />
          <Segmented value={range} onChange={setRange} options={[['15m', tr('logs.range.15m')], ['hour', tr('logs.range.hour')], ['day', tr('logs.range.day')], ['all', tr('logs.range.all')]]} />
          <View style={{ flex: 1 }} />
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
            <Text style={{ fontSize: 12, color: t.textDim }}>{tr('logs.live')}</Text>
            <Toggle on={live} onChange={setLive} />
          </View>
          <Button label={tr('logs.export')} onClick={exportView} />
        </View>
        <View style={{ flex: 1, minHeight: 0 }}>
          {state.error ? (
            <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', padding: 24, gap: 6 }}>
              <Text style={{ fontSize: 14, fontWeight: 600, color: t.text }}>{tr('logs.errorTitle')}</Text>
              <Text style={{ fontSize: 12, color: t.textDim, textAlign: 'center' }}>{state.error}</Text>
            </View>
          ) : !items.length ? (
            <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center' }}>
              <Text style={{ fontSize: 13, color: t.textDim }}>{state.loading ? tr('logs.loading') : tr('logs.empty')}</Text>
            </View>
          ) : (
            <VirtualList items={items} selected={sel} onSelect={(i) => { setSel(i); setStick(false); }} stick={stick && sel < 0} onStick={setStick} renderRow={renderRow} />
          )}
        </View>
        <View style={{ height: 150, borderTop: `1px solid ${t.separator}`, backgroundColor: t.sidebarBg }}>
          <Detail e={sel >= 0 ? items[sel] : null} />
        </View>
        <View style={{ height: 24, flexDirection: 'row', alignItems: 'center', gap: 14, paddingHorizontal: 10, borderTop: `1px solid ${t.separator}` }}>
          <Text style={{ fontSize: 11, color: t.textDim }}>{tr('logs.count', { count: items.length, n: formatNumber(items.length, 0) })}</Text>
          {counts.err ? <Text style={{ fontSize: 11, color: '#ff453a' }}>{tr('logs.errors', { count: counts.err, n: formatNumber(counts.err, 0) })}</Text> : null}
          {counts.warn ? <Text style={{ fontSize: 11, color: '#ff9f0a' }}>{tr('logs.warnings', { count: counts.warn, n: formatNumber(counts.warn, 0) })}</Text> : null}
          {state.ms ? <Text style={{ fontSize: 11, color: t.textDim }}>{tr('logs.took', { ms: formatNumber(state.ms, 0) })}</Text> : null}
          <View style={{ flex: 1 }} />
          {notice ? <Text style={{ fontSize: 11, color: t.accent }}>{notice}</Text> : null}
          {live ? <Text style={{ fontSize: 11, color: '#30d158' }}>{direction() === 'rtl' ? `${tr('logs.following')} ●` : `● ${tr('logs.following')}`}</Text> : null}
        </View>
      </View>
    </View>
  );
}

// Preferences.jsx — editor de perfiles (como Preferencias → Perfiles de iTerm2), atajos
// configurables e importación de perfiles de iTerm2.
import {
  View, Text, TextInput, Toggle, Segmented, Stepper, Slider, Button, ScrollView, SidebarItem, useSettings, useStore, useTheme, fs,
  t as T,
} from '@lordos/sdk';
import { useState } from 'react';
import { PRESETS, DEFAULT_PROFILE, terminalSettings, saveTerminalSettings, schemeOf, allSchemes, newProfileId, profileName } from './profiles.js';
import { reconfigureAll, patchWin } from './state.js';
import { DEFAULT_BINDINGS, ACTION_IDS, actionLabel, bindings, comboLabel, keyCapture, hint } from './keys.js';
import { importIterm } from './iterm.js';

// Funciones (no constantes): los textos dependen del idioma vigente al dibujar.
const SECTIONS = () => [
  ['general', T('terminal.prefs.sec.general')], ['colors', T('terminal.prefs.sec.colors')], ['text', T('terminal.prefs.sec.text')], ['window', T('terminal.prefs.sec.window')],
  ['terminal', T('terminal.prefs.sec.terminal')], ['keys', T('terminal.prefs.sec.keys')], ['triggers', T('terminal.prefs.sec.triggers')],
];

const ANSI_NAMES = () => ['black', 'red', 'green', 'yellow', 'blue', 'magenta', 'cyan', 'white'].map((c) => T(`terminal.prefs.ansi.${c}`));

function Field({ t, label, children, note }) {
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', minHeight: 34, gap: 12 }}>
      <Text style={{ width: 190, fontSize: 13, color: t.text, textAlign: 'end' }}>{label}</Text>
      <View style={{ flex: 1, flexDirection: 'row', alignItems: 'center', gap: 8 }}>{children}</View>
      {note ? <Text style={{ fontSize: 11, color: t.textDim }}>{note}</Text> : null}
    </View>
  );
}

const Heading = ({ t, children, top = 14 }) => (
  <Text style={{ fontSize: 12, fontWeight: 600, color: t.textDim, marginTop: top, marginBottom: 6 }}>{children}</Text>
);

const isHex = (v) => /^#[0-9a-fA-F]{6}$/.test(v);

function ColorWell({ t, label, value, onChange }) {
  const [edit, setEdit] = useState(null);
  return (
    <View style={{ width: 150, flexDirection: 'row', alignItems: 'center', gap: 6, marginBottom: 6 }}>
      <View onClick={() => setEdit(value)} style={{ width: 26, height: 18, borderRadius: 4, backgroundColor: value, border: `1px solid ${t.separator}` }} />
      {edit != null
        ? <TextInput value={edit} autoFocus onChange={setEdit} style={{ width: 92, height: 22 }}
            onSubmit={(v) => { if (isHex(v)) onChange(v.toLowerCase()); setEdit(null); }} onCancel={() => setEdit(null)} />
        : <Text style={{ fontSize: 11, color: t.textDim }}>{label}</Text>}
    </View>
  );
}

function SchemeCard({ t, id, scheme, selected, onClick }) {
  return (
    <View onClick={onClick} style={{ width: 132, height: 64, borderRadius: 8, padding: 7, backgroundColor: scheme.bg,
      border: selected ? `2px solid ${t.accent}` : `1px solid ${t.separator}` }}>
      <Text style={{ fontSize: 11, fontWeight: 600, color: scheme.fg, whiteSpace: 'nowrap' }}>{scheme.name}</Text>
      <Text style={{ fontSize: 10, color: scheme.ansi[2], marginTop: 2 }}>lord@lordos <Text style={{ color: scheme.ansi[4] }}>~</Text></Text>
      <View style={{ flexDirection: 'row', marginTop: 6, gap: 2 }}>
        {scheme.ansi.slice(0, 8).map((c, i) => <View key={`${id}${i}`} style={{ width: 12, height: 7, borderRadius: 2, backgroundColor: c }} />)}
      </View>
    </View>
  );
}

// Lista desplegable en línea (empuja el contenido; no hay capas flotantes en Preferencias).
function Picker({ t, value, options, onChange, width = 240, placeholder = '—' }) {
  const [open, setOpen] = useState(false);
  const cur = options.find(([v]) => v === value);
  return (
    <View style={{ width }}>
      <View onClick={() => setOpen(!open)} style={{ height: 24, borderRadius: 6, paddingHorizontal: 8, flexDirection: 'row', alignItems: 'center',
        backgroundColor: t.controlBg || 'rgba(128,128,128,0.15)', border: `1px solid ${t.separator}` }}>
        <Text style={{ flex: 1, fontSize: 12, color: t.text, whiteSpace: 'nowrap' }}>{cur ? cur[1] : placeholder}</Text>
        <Text style={{ fontSize: 10, color: t.textDim }}>{open ? '▲' : '▼'}</Text>
      </View>
      {open && (
        <View style={{ height: Math.min(200, options.length * 22 + 6), marginTop: 2, borderRadius: 6, border: `1px solid ${t.separator}`, backgroundColor: t.windowBg }}>
          <ScrollView style={{ padding: 3 }}>
            {options.map(([v, label]) => (
              <View key={String(v)} onClick={() => { onChange(v); setOpen(false); }}
                style={{ height: 22, justifyContent: 'center', paddingHorizontal: 6, borderRadius: 4, backgroundColor: v === value ? t.accent : 'transparent' }}>
                <Text style={{ fontSize: 12, color: v === value ? '#fff' : t.text, whiteSpace: 'nowrap' }}>{label}</Text>
              </View>
            ))}
          </ScrollView>
        </View>
      )}
    </View>
  );
}

// Botón que espera la próxima combinación de teclas.
function ComboButton({ t, combo, onChange, width = 120 }) {
  const waiting = useStore(keyCapture);
  const [mine, setMine] = useState(false);
  const active = mine && waiting;
  return (
    <View onClick={() => { setMine(true); keyCapture.set({ onCombo: (c) => { setMine(false); onChange(c); } }); }}
      style={{ width, height: 22, borderRadius: 5, alignItems: 'center', justifyContent: 'center',
        border: `1px solid ${active ? t.accent : t.separator}`, backgroundColor: active ? 'rgba(10,132,255,0.15)' : 'transparent' }}>
      <Text style={{ fontSize: 12, color: active ? t.accent : t.text }}>{active ? T('terminal.prefs.combo.press') : combo ? comboLabel(combo) : T('terminal.prefs.combo.choose')}</Text>
    </View>
  );
}

const ACTION_OPTIONS = () => ACTION_IDS.map((a) => [a, actionLabel(a)]).sort((a, b) => a[1].localeCompare(b[1]));
const KIND = () => [['text', T('terminal.prefs.sec.text')], ['escape', 'Esc+'], ['hex', 'Hex'], ['ignore', T('terminal.prefs.kind.ignore')], ['action', T('terminal.prefs.kind.action')]];
const kindOf = (m) => (['text', 'escape', 'hex', 'ignore'].includes(m.action) ? m.action : 'action');

function KeyMapEditor({ t, list, onChange }) {
  const upd = (i, patch) => onChange(list.map((m, j) => (j === i ? { ...m, ...patch } : m)));
  return (
    <>
      {list.map((m, i) => (
        <View key={i} style={{ flexDirection: 'row', alignItems: 'flex-start', gap: 6, marginBottom: 6 }}>
          <ComboButton t={t} combo={m.combo} width={92} onChange={(c) => upd(i, { combo: c })} />
          <Segmented value={kindOf(m)} options={KIND()}
            onChange={(k) => upd(i, k === 'action' ? { action: 'newTab', param: '' } : { action: k, param: k === 'hex' ? '0x1b' : '' })} />
          {kindOf(m) === 'action'
            ? <Picker t={t} width={150} value={m.action} options={ACTION_OPTIONS()} onChange={(v) => upd(i, { action: v })} />
            : m.action !== 'ignore' && (
              <TextInput value={m.param || ''} style={{ width: 130 }} onChange={(v) => upd(i, { param: v })}
                placeholder={m.action === 'hex' ? '0x1b 0x5b 0x41' : m.action === 'escape' ? T('terminal.prefs.escPh') : 'git status\\n'} />
            )}
          <Button label="−" onClick={() => onChange(list.filter((_, j) => j !== i))} />
        </View>
      ))}
      <Button label={T('terminal.prefs.keys.add')} style={{ alignSelf: 'flex-start' }} onClick={() => onChange([...list, { combo: '', action: 'text', param: '' }])} />
    </>
  );
}

// ---- importar de iTerm2 ----

function fontsApi() {
  const list = lord.fonts.list();
  const flat = (s) => s.replace(/[\s-]/g, '').toLowerCase();
  return {
    byPs(name) {
      const n = String(name || '');
      const exact = list.find((f) => f.ps && f.ps.toLowerCase() === n.toLowerCase());
      if (exact) return exact;
      const fam = flat(n.split('-')[0]);
      return list.find((f) => flat(f.family) === fam) || null;
    },
  };
}

const IMPORT_DIRS = ['Descargas', 'Downloads', 'Documentos', 'Escritorio', '.'];
const IMPORT_EXT = /\.(json|itermcolors|plist)$/i;

function candidates() {
  const out = [];
  for (const d of IMPORT_DIRS) {
    try {
      for (const e of fs.list(d, { hidden: false })) if (!e.dir && IMPORT_EXT.test(e.name)) out.push(d === '.' ? e.name : `${d}/${e.name}`);
    } catch (e) { /* la carpeta no existe */ }
  }
  return out;
}

function readImport(path) {
  const rel = path.replace(/^~\/?/, '');
  const bytes = fs.read(rel, { binary: true });
  const head = String.fromCharCode(...new Uint8Array(bytes.slice(0, 8)));
  return importIterm(head === 'bplist00' ? bytes : fs.read(rel), path, fontsApi());
}

function ImportPanel({ t, onClose, onDone }) {
  const [path, setPath] = useState('');
  const [result, setResult] = useState(null);
  const [error, setError] = useState(null);
  const [pick, setPick] = useState({});
  const [files] = useState(candidates);
  const load = (p) => {
    setPath(p);
    try {
      const r = readImport(p);
      setResult(r);
      setError(null);
      setPick(Object.fromEntries(r.profiles.map((x) => [x.id, true])));
    } catch (e) { setResult(null); setError(e.message || String(e)); }
  };
  const apply = (makeDefault) => {
    const cfg = terminalSettings();
    let profiles = [...cfg.profiles];
    let defaultProfile = cfg.defaultProfile;
    let added = 0;
    for (const np of result.profiles) {
      if (!pick[np.id]) continue;
      const i = np.itermGuid ? profiles.findIndex((x) => x.itermGuid === np.itermGuid) : -1;
      const prof = i >= 0 ? { ...np, id: profiles[i].id } : np;
      if (i >= 0) profiles[i] = prof; else profiles.push(prof);
      if (makeDefault && np.itermGuid && np.itermGuid === result.defaultGuid) defaultProfile = prof.id;
      added++;
    }
    const schemes = { ...(cfg.schemes || {}) };
    for (const s of result.schemes) schemes[`it_${s.name.toLowerCase().replace(/[^a-z0-9]+/g, '_')}`] = s;
    const keyMap = [...(cfg.keyMap || []).filter((m) => !result.keyMap.some((k) => k.combo === m.combo)), ...result.keyMap];
    saveTerminalSettings({ profiles, defaultProfile, schemes, keyMap });
    reconfigureAll();
    onDone(T('terminal.prefs.import.done', { p: added, s: result.schemes.length, k: result.keyMap.length }),
      result.profiles.find((x) => pick[x.id])?.id);
  };
  return (
    <View style={{ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0, backgroundColor: t.windowBg, padding: 18 }}>
      <Text style={{ fontSize: 16, fontWeight: 700, color: t.text }}>{T('terminal.prefs.import.title')}</Text>
      <Text style={{ fontSize: 12, color: t.textDim, marginTop: 4, lineHeight: 1.4 }}>
        {T('terminal.prefs.import.intro')}
      </Text>
      <View style={{ flexDirection: 'row', gap: 8, marginTop: 12, alignItems: 'center' }}>
        <TextInput value={path} placeholder="~/Descargas/Profiles.json" onChange={setPath} onSubmit={load} style={{ flex: 1 }} />
        <Button label={T('terminal.prefs.import.open')} primary onClick={() => load(path)} />
        <Button label={T('terminal.prefs.cancel')} onClick={onClose} />
      </View>
      <View style={{ flex: 1, flexDirection: 'row', gap: 16, marginTop: 12 }}>
        <View style={{ width: 260 }}>
          <Heading t={t} top={0}>{T('terminal.prefs.import.found')}</Heading>
          {files.length ? files.map((f) => (
            <SidebarItem key={f} label={f} selected={path === `~/${f}`} onClick={() => load(`~/${f}`)} />
          )) : <Text style={{ fontSize: 12, color: t.textDim }}>{T('terminal.prefs.import.none')}</Text>}
        </View>
        <View style={{ flex: 1 }}>
          <ScrollView>
            {error && <Text style={{ fontSize: 13, color: '#ff453a' }}>{T('terminal.prefs.import.failed', { error })}</Text>}
            {result && (
              <>
                {result.profiles.length > 0 && <Heading t={t} top={0}>{T('terminal.prefs.import.profiles', { count: result.profiles.length })}</Heading>}
                {result.profiles.map((p) => {
                  const s = schemeOf(p);
                  return (
                    <View key={p.id} style={{ flexDirection: 'row', alignItems: 'center', gap: 10, height: 30 }}>
                      <Toggle on={!!pick[p.id]} onChange={(v) => setPick({ ...pick, [p.id]: v })} />
                      <View style={{ width: 54, height: 20, borderRadius: 4, backgroundColor: s.bg, flexDirection: 'row', gap: 1, padding: 3 }}>
                        {s.ansi.slice(1, 7).map((c, i) => <View key={i} style={{ flex: 1, backgroundColor: c, borderRadius: 1 }} />)}
                      </View>
                      <Text style={{ fontSize: 13, color: t.text }}>{p.name}{p.itermGuid && p.itermGuid === result.defaultGuid ? '  ★' : ''}</Text>
                      <Text style={{ fontSize: 11, color: t.textDim }}>{[`${p.font || 'JetBrains Mono'} ${p.fontSize} pt`, p.ligatures && T('terminal.prefs.import.ligatures'), p.triggers.length && T('terminal.prefs.import.triggers', { count: p.triggers.length }), p.keyMap.length && T('terminal.prefs.import.keys', { count: p.keyMap.length })].filter(Boolean).join(' · ')}</Text>
                    </View>
                  );
                })}
                {result.schemes.length > 0 && <Heading t={t}>{T('terminal.prefs.import.schemes', { count: result.schemes.length })}</Heading>}
                {[0, 1, 2].map((r) => (
                  <View key={r} style={{ flexDirection: 'row', gap: 8, marginBottom: 8 }}>
                    {result.schemes.slice(r * 4, r * 4 + 4).map((s) => <SchemeCard key={s.name} t={t} id={s.name} scheme={s} selected={false} onClick={() => {}} />)}
                  </View>
                ))}
                {result.warnings.length > 0 && <Heading t={t}>{T('terminal.prefs.import.warnings')}</Heading>}
                {result.warnings.map((w, i) => <Text key={i} style={{ fontSize: 12, color: t.textDim, lineHeight: 1.5 }}>• {w}</Text>)}
                <View style={{ flexDirection: 'row', gap: 8, marginTop: 14 }}>
                  <Button label={T('terminal.prefs.import.do')} primary onClick={() => apply(false)} />
                  {result.defaultGuid && <Button label={T('terminal.prefs.import.doDefault')} onClick={() => apply(true)} />}
                </View>
              </>
            )}
          </ScrollView>
        </View>
      </View>
    </View>
  );
}

// ---- preferencias ----

export function Preferences({ wid, onClose }) {
  const t = useTheme();
  useSettings();        // re-render al guardar
  const cfg = terminalSettings();
  const [sel, setSel] = useState(cfg.defaultProfile);
  const [section, setSection] = useState('general');
  const [importing, setImporting] = useState(false);
  const [notice, setNotice] = useState(null);
  const p = cfg.profiles.find((x) => x.id === sel) || cfg.profiles[0];

  const save = (profiles, extra = {}) => { saveTerminalSettings({ profiles, ...extra }); reconfigureAll(); };
  const set = (patch) => save(cfg.profiles.map((x) => (x.id === p.id ? { ...x, ...patch } : x)));
  const setColor = (key, value, idx) => {
    const base = { ...schemeOf(p), name: T('terminal.prefs.custom') };
    const custom = { ...base, ansi: [...base.ansi] };
    if (key === 'ansi') custom.ansi[idx] = value; else custom[key] = value;
    set({ colors: 'custom', custom });
  };
  const addProfile = (from) => {
    const id = newProfileId();
    const np = { ...(from || DEFAULT_PROFILE), id, itermGuid: '', name: from ? T('terminal.prefs.copyOf', { name: profileName(from) }) : T('terminal.prefs.newProfile') };
    save([...cfg.profiles, np]);
    setSel(id);
  };
  const removeProfile = () => {
    if (cfg.profiles.length < 2) return;
    const rest = cfg.profiles.filter((x) => x.id !== p.id);
    save(rest, { defaultProfile: cfg.defaultProfile === p.id ? rest[0].id : cfg.defaultProfile });
    setSel(rest[0].id);
  };
  const saveShortcut = (action, combos) => {
    const cur = bindings();
    const custom = { ...(cfg.shortcuts || {}) };
    // una combinación pertenece a una sola acción: se la saca de las demás
    for (const [a, list] of Object.entries(cur)) {
      if (a === action || !combos.some((c) => list.includes(c))) continue;
      custom[a] = list.filter((c) => !combos.includes(c));
    }
    custom[action] = combos;
    save(cfg.profiles, { shortcuts: custom });
  };
  const resetShortcut = (action) => {
    const custom = { ...(cfg.shortcuts || {}) };
    delete custom[action];
    save(cfg.profiles, { shortcuts: custom });
  };

  const fontList = (() => { try { return lord.fonts.list(); } catch (e) { return []; } })();
  const families = [...new Set(fontList.filter((f) => !f.color).map((f) => f.family))];
  const monoFamilies = [...new Set(fontList.filter((f) => f.mono).map((f) => f.family))];
  const schemes = allSchemes();
  const imported = Object.entries(schemes).filter(([id]) => !PRESETS[id]);
  const scheme = schemeOf(p);

  const body = {
    general: (
      <>
        <Field t={t} label={T('terminal.prefs.name')}><TextInput value={p.name} onChange={(v) => set({ name: v })} style={{ width: 240 }} /></Field>
        <Field t={t} label={T('terminal.prefs.defaultProfile')}>
          {cfg.defaultProfile === p.id ? <Text style={{ fontSize: 12, color: t.textDim }}>{T('terminal.prefs.isDefault')}</Text>
            : <Button label={T('terminal.prefs.useDefault')} onClick={() => save(cfg.profiles, { defaultProfile: p.id })} />}
        </Field>
        <Field t={t} label={T('terminal.prefs.workingDir')}>
          <Segmented value={p.workingDir} onChange={(v) => set({ workingDir: v })}
            options={[['home', T('terminal.prefs.dir.home')], ['reuse', T('terminal.prefs.dir.reuse')], ['custom', T('terminal.prefs.dir.custom')]]} />
        </Field>
        {p.workingDir === 'custom' && (
          <Field t={t} label={T('terminal.prefs.folder')}><TextInput value={p.customDir} placeholder="~/proyectos" onChange={(v) => set({ customDir: v })} style={{ width: 240 }} /></Field>
        )}
        <Field t={t} label={T('terminal.prefs.initialText')} note="\n = Enter">
          <TextInput value={p.initialText || ''} placeholder={T('terminal.prefs.initialTextPh')} onChange={(v) => set({ initialText: v })} style={{ width: 240 }} />
        </Field>
        <Field t={t} label={T('terminal.prefs.badge')} note={T('terminal.prefs.badgeNote')}>
          <TextInput value={p.badge} placeholder={T('terminal.prefs.badgePh')} onChange={(v) => set({ badge: v })} style={{ width: 200 }} />
        </Field>
        <Field t={t} label={T('terminal.prefs.tabColor')}>
          {['', '#ff453a', '#ff9f0a', '#ffd60a', '#32d74b', '#0a84ff', '#bf5af2'].map((c) => (
            <View key={c || 'none'} onClick={() => set({ tabColor: c })} style={{ width: 20, height: 20, borderRadius: 10,
              backgroundColor: c || 'transparent', border: `2px solid ${p.tabColor === c ? t.text : t.separator}` }} />
          ))}
          {p.tabColor && !['#ff453a', '#ff9f0a', '#ffd60a', '#32d74b', '#0a84ff', '#bf5af2'].includes(p.tabColor) && (
            <View style={{ width: 20, height: 20, borderRadius: 10, backgroundColor: p.tabColor, border: `2px solid ${t.text}` }} />
          )}
        </Field>
        <Field t={t} label={T('terminal.prefs.rules')} note={T('terminal.prefs.rulesNote')}>
          <TextInput value={(p.rules || '').replace(/\n/g, ' ; ')} placeholder="~/prod/** ; root@* ; *.prod.com" style={{ width: 260 }}
            onChange={(v) => set({ rules: v.split(/\s*;\s*/).join('\n') })} />
        </Field>
        <Text style={{ fontSize: 11, color: t.textDim, marginLeft: 202, marginBottom: 6, lineHeight: 1.4 }}>
          Rutas (~/prod/*, /srv/**), usuario@host, host o usuario@ — con * como comodín. Usuario y host los informa el shell (integración bash/zsh/fish) o ssh.
        </Text>
        <Field t={t} label={T('terminal.prefs.confirmClose')}><Toggle on={p.confirmClose} onChange={(v) => set({ confirmClose: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.hotkey')}>
          <Segmented value={cfg.hotkey} onChange={(v) => save(cfg.profiles, { hotkey: v })} options={[['F12', 'F12'], ['Super+`', '❖`'], ['off', T('terminal.prefs.off')]]} />
        </Field>
      </>
    ),
    colors: (
      <>
        <Heading t={t} top={0}>{T('terminal.prefs.schemes')}</Heading>
        <View style={{ gap: 8 }}>
          {[0, 1, 2, 3, 4].map((r) => (
            <View key={r} style={{ flexDirection: 'row', gap: 8 }}>
              {Object.entries(PRESETS).slice(r * 4, r * 4 + 4).map(([id, s]) => (
                <SchemeCard key={id} t={t} id={id} scheme={s} selected={p.colors === id} onClick={() => set({ colors: id })} />
              ))}
            </View>
          ))}
        </View>
        {imported.length > 0 && <Heading t={t}>{T('terminal.prefs.imported')}</Heading>}
        <View style={{ gap: 8 }}>
          {Array.from({ length: Math.ceil(imported.length / 4) }, (_, r) => (
            <View key={r} style={{ flexDirection: 'row', gap: 8 }}>
              {imported.slice(r * 4, r * 4 + 4).map(([id, s]) => (
                <SchemeCard key={id} t={t} id={id} scheme={s} selected={p.colors === id} onClick={() => set({ colors: id })} />
              ))}
            </View>
          ))}
        </View>
        <Heading t={t}>{p.colors === 'custom' ? T('terminal.prefs.customColors') : T('terminal.prefs.editColors')}</Heading>
        <View style={{ flexDirection: 'row', gap: 8 }}>
          <View>
            {[['fg', T('terminal.prefs.sec.text')], ['bg', T('terminal.prefs.c.bg')], ['bold', T('terminal.prefs.c.bold')], ['link', T('terminal.prefs.c.link')]].map(([k, l]) => (
              <ColorWell key={k} t={t} label={l} value={scheme[k] || scheme.fg} onChange={(v) => setColor(k, v)} />
            ))}
          </View>
          <View>
            {[['cursor', T('terminal.prefs.cursor')], ['cursorText', T('terminal.prefs.c.cursorText')], ['selection', T('terminal.prefs.c.selection')], ['selectionText', T('terminal.prefs.c.selectionText')]].map(([k, l]) => (
              <ColorWell key={k} t={t} label={l} value={scheme[k] || scheme.fg} onChange={(v) => setColor(k, v)} />
            ))}
          </View>
          <View>
            {ANSI_NAMES().map((n, i) => <ColorWell key={n} t={t} label={n} value={scheme.ansi[i]} onChange={(v) => setColor('ansi', v, i)} />)}
          </View>
          <View>
            {ANSI_NAMES().map((n, i) => <ColorWell key={n} t={t} label={T('terminal.prefs.bright', { color: n })} value={scheme.ansi[i + 8]} onChange={(v) => setColor('ansi', v, i + 8)} />)}
          </View>
        </View>
        <Text style={{ fontSize: 11, color: t.textDim, marginTop: 4 }}>{T('terminal.prefs.colorHint')}</Text>
      </>
    ),
    text: (
      <>
        <Field t={t} label={T('terminal.prefs.font')} note={`${monoFamilies.length} monoespaciadas instaladas`}>
          <Picker t={t} value={p.font || ''} onChange={(v) => set({ font: v })}
            options={[['', T('terminal.prefs.fontBuiltin')], ...monoFamilies.filter((f) => f !== 'JetBrains Mono').map((f) => [f, f])]} />
        </Field>
        <Field t={t} label={T('terminal.prefs.ligatures')} note="-> => != === (JetBrains Mono, Fira Code…)"><Toggle on={p.ligatures} onChange={(v) => set({ ligatures: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.bidi')} note={T('terminal.prefs.bidiNote')}><Toggle on={p.bidi !== false} onChange={(v) => set({ bidi: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.nonAscii')} note={T('terminal.prefs.nonAsciiNote')}>
          <Picker t={t} value={p.nonAsciiFont || ''} onChange={(v) => set({ nonAsciiFont: v })}
            options={[['', T('terminal.prefs.same')], ...families.map((f) => [f, f])]} />
        </Field>
        <Field t={t} label={T('terminal.prefs.size')}><Stepper value={p.fontSize} min={7} max={40} onChange={(v) => set({ fontSize: v })} format={(v) => `${v} pt`} /></Field>
        <Field t={t} label={T('terminal.prefs.lineSpacing')}>
          <Stepper value={p.lineSpacing} min={0.8} max={2} step={0.05} onChange={(v) => set({ lineSpacing: Math.round(v * 100) / 100 })} format={(v) => `${Math.round(v * 100)}%`} />
        </Field>
        <Field t={t} label={T('terminal.prefs.padding')}><Stepper value={p.padding} min={0} max={40} onChange={(v) => set({ padding: v })} format={(v) => `${v} px`} /></Field>
        <Field t={t} label={T('terminal.prefs.cursor')}>
          <Segmented value={p.cursorStyle} onChange={(v) => set({ cursorStyle: v })} options={[['block', T('terminal.prefs.cur.block')], ['underline', T('terminal.prefs.cur.underline')], ['beam', T('terminal.prefs.cur.beam')]]} />
        </Field>
        <Field t={t} label={T('terminal.prefs.cursorBlink')}><Toggle on={p.cursorBlink} onChange={(v) => set({ cursorBlink: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.cursorGuide')} note={T('terminal.prefs.cursorGuideNote')}><Toggle on={p.cursorGuide} onChange={(v) => set({ cursorGuide: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.boldBright')}><Toggle on={p.boldBright} onChange={(v) => set({ boldBright: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.useBold')} note={T('terminal.prefs.useBoldNote')}><Toggle on={p.useBoldFont} onChange={(v) => set({ useBoldFont: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.useItalic')}><Toggle on={p.useItalic} onChange={(v) => set({ useItalic: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.blinkText')}><Toggle on={p.blinkText} onChange={(v) => set({ blinkText: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.minContrast')} note={`${Math.round(p.minimumContrast * 100)}%`}>
          <Slider value={p.minimumContrast} onChange={(v) => set({ minimumContrast: Math.round(v * 100) / 100 })} />
        </Field>
      </>
    ),
    window: (
      <>
        <Field t={t} label={T('terminal.prefs.transparency')} note={`${Math.round(p.transparency * 100)}%`}>
          <Slider value={p.transparency} max={0.9} onChange={(v) => set({ transparency: Math.round(v * 100) / 100 })} />
        </Field>
        <Field t={t} label={T('terminal.prefs.blur')}><Toggle on={p.blur} onChange={(v) => set({ blur: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.dimInactive')}><Toggle on={p.dimInactive} onChange={(v) => set({ dimInactive: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.statusBar')}><Toggle on={p.statusBar} onChange={(v) => set({ statusBar: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.columns')} note={T('terminal.prefs.columnsNote')}>
          <Stepper value={p.columns || 0} min={0} max={400} step={10} onChange={(v) => set({ columns: v })} format={(v) => (v ? String(v) : '—')} />
        </Field>
        <Field t={t} label={T('terminal.prefs.rows')}>
          <Stepper value={p.rows || 0} min={0} max={200} step={5} onChange={(v) => set({ rows: v })} format={(v) => (v ? String(v) : '—')} />
        </Field>
      </>
    ),
    terminal: (
      <>
        <Field t={t} label={T('terminal.prefs.scrollback')} note={T('terminal.prefs.scrollbackNote')}>
          <Segmented value={p.scrollback} onChange={(v) => set({ scrollback: v })}
            options={[[1000, '1.000'], [10000, '10.000'], [100000, '100.000'], [1000000, T('terminal.prefs.million')]]} />
        </Field>
        <Field t={t} label={T('terminal.prefs.mouseReporting')} note={T('terminal.prefs.mouseNote')}><Toggle on={p.mouseReporting} onChange={(v) => set({ mouseReporting: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.copyOnSelect')}><Toggle on={p.copyOnSelect} onChange={(v) => set({ copyOnSelect: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.altScroll')}><Toggle on={p.alternateScroll} onChange={(v) => set({ alternateScroll: v })} /></Field>
        <Field t={t} label={T('terminal.prefs.bell')}>
          <Segmented value={p.bell} onChange={(v) => set({ bell: v })} options={[['visual', T('terminal.prefs.bellVisual')], ['none', T('terminal.prefs.bellNone')]]} />
        </Field>
        <Field t={t} label={T('terminal.prefs.wordChars')} note={T('terminal.prefs.wordNote')}>
          <TextInput value={p.wordChars} onChange={(v) => set({ wordChars: v.slice(0, 60) })} style={{ width: 140 }} />
        </Field>
      </>
    ),
    keys: (
      <>
        <Field t={t} label={T('terminal.prefs.altEsc')}><Toggle on={p.altSendsEscape} onChange={(v) => set({ altSendsEscape: v })} /></Field>
        <Heading t={t}>{T('terminal.prefs.keys.profile')}</Heading>
        <KeyMapEditor t={t} list={p.keyMap || []} onChange={(keyMap) => set({ keyMap })} />
        <Heading t={t}>{T('terminal.prefs.keys.global')}</Heading>
        <KeyMapEditor t={t} list={cfg.keyMap || []} onChange={(keyMap) => save(cfg.profiles, { keyMap })} />
        <Heading t={t}>{T('terminal.prefs.keys.shortcuts')}</Heading>
        {Object.keys(DEFAULT_BINDINGS).map((a) => {
          const combos = bindings()[a] || [];
          const changed = !!cfg.shortcuts?.[a];
          return (
            <View key={a} style={{ flexDirection: 'row', alignItems: 'center', height: 26, gap: 8 }}>
              <Text style={{ width: 220, fontSize: 12, color: t.text }}>{actionLabel(a)}</Text>
              <ComboButton t={t} combo={combos[0]} onChange={(c) => saveShortcut(a, [c, ...combos.slice(1).filter((x) => x !== c)])} />
              <Text style={{ fontSize: 11, color: t.textDim, width: 170, whiteSpace: 'nowrap' }}>{combos.slice(1).map(comboLabel).join('  ')}</Text>
              {changed && <Button label={T('terminal.prefs.reset')} onClick={() => resetShortcut(a)} />}
            </View>
          );
        })}
      </>
    ),
    triggers: (
      <>
        <Text style={{ fontSize: 12, color: t.textDim, marginBottom: 8, lineHeight: 1.4 }}>
          Cuando una línea nueva coincide con la expresión regular se ejecuta la acción. En el parámetro, \1 \2… son los grupos.
          Resaltar: "#texto,#fondo" (ej. #ffffff,#ff453a). Título: el texto del título.
        </Text>
        {(p.triggers || []).map((tr, i) => {
          const upd = (patch) => set({ triggers: p.triggers.map((x, j) => (j === i ? { ...x, ...patch } : x)) });
          return (
            <View key={i} style={{ flexDirection: 'row', alignItems: 'center', gap: 6, marginBottom: 6 }}>
              <Toggle on={tr.enabled !== false} onChange={(v) => upd({ enabled: v })} />
              <TextInput value={tr.regex} placeholder="regex" onChange={(v) => upd({ regex: v })} style={{ width: 150 }} />
              <Picker t={t} width={130} value={tr.action} onChange={(v) => upd({ action: v })}
                options={[['notify', T('terminal.prefs.tr.notify')], ['bell', T('terminal.prefs.bell')], ['attention', T('terminal.prefs.tr.attention')], ['send', T('terminal.prefs.tr.send')], ['capture', T('terminal.prefs.tr.capture')],
                  ['highlight', T('terminal.prefs.tr.highlight')], ['mark', T('terminal.prefs.tr.mark')], ['title', T('terminal.prefs.tr.title')], ['stop', T('terminal.prefs.tr.stop')]]} />
              <TextInput value={tr.param || ''} placeholder={T('terminal.prefs.param')} onChange={(v) => upd({ param: v })} style={{ width: 130 }} />
              <Button label="−" onClick={() => set({ triggers: p.triggers.filter((_, j) => j !== i) })} />
            </View>
          );
        })}
        <Button label={T('terminal.prefs.tr.add')} onClick={() => set({ triggers: [...(p.triggers || []), { regex: 'error', action: 'notify', param: '', enabled: true }] })} style={{ alignSelf: 'flex-start' }} />
      </>
    ),
  };

  return (
    <View style={{ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0, backgroundColor: t.windowBg, flexDirection: 'row' }}>
      <View style={{ width: 190, backgroundColor: t.sidebarBg, padding: 8 }}>
        <Text style={{ fontSize: 11, fontWeight: 700, color: t.textDim, margin: 6 }}>{T('terminal.prefs.profiles')}</Text>
        <ScrollView style={{ flex: 1 }}>
          {cfg.profiles.map((x) => (
            <SidebarItem key={x.id} label={`${profileName(x)}${x.id === cfg.defaultProfile ? '  ★' : ''}`} selected={x.id === p.id} onClick={() => setSel(x.id)} />
          ))}
        </ScrollView>
        <View style={{ flexDirection: 'row', gap: 6, marginTop: 8 }}>
          <Button label="+" onClick={() => addProfile(null)} />
          <Button label={T('terminal.prefs.duplicate')} onClick={() => addProfile(p)} />
          <Button label="−" onClick={removeProfile} />
        </View>
        <Button label={T('terminal.prefs.importBtn')} onClick={() => setImporting(true)} style={{ marginTop: 6 }} />
        <View style={{ height: 8 }} />
        <Button label={T('terminal.prefs.done')} primary onClick={onClose} />
      </View>
      <View style={{ flex: 1 }}>
        <View style={{ height: 40, flexDirection: 'row', alignItems: 'center', justifyContent: 'center' }}>
          <Segmented value={section} onChange={setSection} options={SECTIONS()} />
        </View>
        {notice && (
          <View style={{ marginHorizontal: 16, padding: 8, borderRadius: 6, backgroundColor: 'rgba(50,215,75,0.18)' }}>
            <Text style={{ fontSize: 12, color: t.text }}>{notice}</Text>
          </View>
        )}
        <ScrollView style={{ padding: 16 }}>{body[section]}</ScrollView>
      </View>
      {importing && (
        <ImportPanel t={t} onClose={() => setImporting(false)}
          onDone={(msg, first) => { setImporting(false); setNotice(msg); if (first) setSel(cfg.profiles.find((x) => x.id === first) ? first : terminalSettings().profiles.find((x) => x.id === first)?.id || sel); }} />
      )}
    </View>
  );
}

export const closePrefs = (wid) => patchWin(wid, () => ({ prefs: false }));
export { hint };

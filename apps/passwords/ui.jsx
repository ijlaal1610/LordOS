// ui.jsx — piezas chicas de la app Contraseñas: íconos de categoría, hojas (diálogos), barra de
// fuerza, imagen de un código QR, un campo de varias líneas, un selector de archivos y menús.
import { View, Text, Image, useTheme, useStore, createStore, Button, ScrollView, TextInput, takeTextFocus, releaseTextFocus, fs, t } from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';
import { builtinById } from './templates.js';

export const RED = '#ff453a', ORANGE = '#ff9f0a', GREEN = '#30d158', YELLOW = '#ffd60a';

// ---- íconos ----

export function CatIcon({ cat, size = 28 }) {
  const b = cat ? builtinById(cat.id) : null;
  const color = cat?.color || b?.color || '#8e8e93';
  const icon = /^[a-z_-]+$/i.test(cat?.icon || '') ? '📁' : cat?.icon || b?.icon || '🗂️';
  return (
    <View style={{ width: size, height: size, borderRadius: size * 0.26, alignItems: 'center', justifyContent: 'center',
      background: `linear-gradient(160deg, ${color}, ${shade(color, -0.25)})` }}>
      <Text style={{ fontSize: size * 0.56 }}>{icon}</Text>
    </View>
  );
}

// Inicial del sitio (o del título) sobre el color de la categoría: como los íconos de sitios sin descargar nada
export function EntryIcon({ entry, cat, size = 32 }) {
  const b = cat ? builtinById(cat.id) : null;
  const color = cat?.color || b?.color || '#8e8e93';
  const host = String(entry.url || '').replace(/^[a-z]+:\/\//i, '').replace(/^www\./, '');
  const useLetter = host || ['internet', 'general', 'email', 'apps', 'secrets', 'keyring'].includes(entry.cat);
  if (!useLetter) return <CatIcon cat={cat} size={size} />;
  const ch = ([...(host || entry.title || '?')][0] || '?').toUpperCase();
  return (
    <View style={{ width: size, height: size, borderRadius: size * 0.26, alignItems: 'center', justifyContent: 'center',
      background: `linear-gradient(160deg, ${color}, ${shade(color, -0.3)})` }}>
      <Text style={{ fontSize: size * 0.5, fontWeight: 700, color: '#fff' }}>{ch}</Text>
    </View>
  );
}

export function shade(hex, amt) {
  const m = /^#?([0-9a-f]{6})$/i.exec(String(hex || ''));
  if (!m) return hex;
  const n = parseInt(m[1], 16);
  const ch = (v) => Math.max(0, Math.min(255, Math.round(v + (amt < 0 ? v * amt : (255 - v) * amt))));
  const r = ch(n >> 16), g = ch((n >> 8) & 255), b = ch(n & 255);
  return `#${((r << 16) | (g << 8) | b).toString(16).padStart(6, '0')}`;
}

// ---- hoja (diálogo encima de la ventana) ----

// Las hojas se dibujan arriba de toda la ventana (no adentro del panel que las abre): cada
// <Sheet> se anota en `overlays` y <Overlays /> (al final de la app) las muestra.
const overlays = createStore([]);           // [{ key, node }]
let overlaySeq = 0;

export function Sheet(props) {
  const key = useRef(null);
  if (!key.current) key.current = `s${++overlaySeq}`;
  useEffect(() => {
    const node = <SheetBox {...props} />;
    overlays.set((l) => (l.some((x) => x.key === key.current) ? l.map((x) => (x.key === key.current ? { key: x.key, node } : x)) : [...l, { key: key.current, node }]));
  });
  useEffect(() => () => overlays.set((l) => l.filter((x) => x.key !== key.current)), []);
  return null;
}

export function Overlays() {
  const l = useStore(overlays);
  return l.map((x) => <View key={x.key} style={{ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0 }}>{x.node}</View>);
}

function SheetBox({ title, onClose, children, width = 460, footer }) {
  const th = useTheme();
  return (
    <View onMouseDown={(e) => e.stopPropagation?.()} style={{ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0, zIndex: 50,
      alignItems: 'center', justifyContent: 'center', backgroundColor: 'rgba(0,0,0,0.38)' }}>
      <View style={{ width, maxHeight: '94%', borderRadius: 14, backgroundColor: th.windowBg, border: `1px solid ${th.separator}`,
        boxShadow: '0 18px 50px rgba(0,0,0,0.5)', overflow: 'hidden' }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', paddingHorizontal: 16, paddingVertical: 12, borderBottom: `1px solid ${th.separator}` }}>
          <Text style={{ flex: 1, fontSize: 15, fontWeight: 700, color: th.text }}>{title}</Text>
          {onClose && <View onClick={onClose} style={{ width: 24, height: 24, borderRadius: 12, alignItems: 'center', justifyContent: 'center', backgroundColor: th.control }}>
            <Text style={{ fontSize: 12, color: th.textDim }}>✕</Text></View>}
        </View>
        <View style={{ padding: 16, gap: 10, flexShrink: 1, overflow: 'hidden' }}>{children}</View>
        {footer && <View style={{ flexDirection: 'row', justifyContent: 'flex-end', gap: 8, paddingHorizontal: 16, paddingVertical: 12, borderTop: `1px solid ${th.separator}` }}>{footer}</View>}
      </View>
    </View>
  );
}

export function Label({ children }) {
  const th = useTheme();
  return <Text style={{ fontSize: 11, fontWeight: 600, color: th.textDim }}>{children}</Text>;
}

export function Hint({ children, color }) {
  const th = useTheme();
  return <Text style={{ fontSize: 12, color: color || th.textDim, lineHeight: 17 }}>{children}</Text>;
}

export function Warning({ children }) {
  return (
    <View style={{ flexDirection: 'row', gap: 8, padding: 10, borderRadius: 9, backgroundColor: 'rgba(255,69,58,0.14)', border: '1px solid rgba(255,69,58,0.45)' }}>
      <Text style={{ fontSize: 15 }}>⚠️</Text>
      <Text style={{ flex: 1, fontSize: 12, color: RED, lineHeight: 17 }}>{children}</Text>
    </View>
  );
}

// ---- fuerza ----

export function strengthColor(score) { return [RED, RED, ORANGE, YELLOW, GREEN][Math.max(0, Math.min(4, score | 0))]; }
export function strengthLabel(score) { return [t('passwords.strength.0'), t('passwords.strength.1'), t('passwords.strength.2'), t('passwords.strength.3'), t('passwords.strength.4')][Math.max(0, Math.min(4, score | 0))]; }

export function StrengthBar({ score, bits }) {
  const th = useTheme();
  return (
    <View style={{ gap: 4 }}>
      <View style={{ flexDirection: 'row', gap: 3, direction: 'ltr' }}>
        {[0, 1, 2, 3].map((i) => (
          <View key={i} style={{ flex: 1, height: 5, borderRadius: 3, backgroundColor: i < Math.max(1, score) ? strengthColor(score) : th.control }} />
        ))}
      </View>
      <Text style={{ fontSize: 11, color: strengthColor(score) }}>{bits != null ? t('passwords.strength.bits', { label: strengthLabel(score), bits }) : strengthLabel(score)}</Text>
    </View>
  );
}

// ---- código QR (filas en hex de lord-vault → píxeles) ----

export function QrImage({ qr, scale = 5 }) {
  if (!qr || !qr.size) return null;
  const quiet = 4, n = qr.size, side = (n + quiet * 2) * scale;
  const data = new Uint8Array(side * side * 4).fill(255);
  for (let y = 0; y < n; y++) {
    const row = qr.rows[y];
    for (let x = 0; x < n; x++) {
      const nib = parseInt(row[x >> 2], 16);
      if (!((nib >> (3 - (x & 3))) & 1)) continue;
      for (let dy = 0; dy < scale; dy++) {
        let o = (((y + quiet) * scale + dy) * side + (x + quiet) * scale) * 4;
        for (let dx = 0; dx < scale; dx++, o += 4) { data[o] = 0; data[o + 1] = 0; data[o + 2] = 0; }
      }
    }
  }
  return <Image pixels={{ width: side, height: side, data }} style={{ width: side, height: side, borderRadius: 8 }} />;
}

// ---- campo de varias líneas (notas, claves privadas, frases semilla) ----

export function TextArea({ value, onChange, placeholder, minHeight = 90, mono, secure }) {
  const th = useTheme();
  const token = useRef({}).current;
  const [focus, setFocus] = useState(false);
  const latest = useRef();
  latest.current = { value, onChange };
  useEffect(() => () => releaseTextFocus(token), []);
  const onKey = (e) => {
    const { value: v, onChange: set } = latest.current;
    if (e.ctrl && e.base === 'v') { set(v + lord.clipboard.get()); return; }
    if (e.key === 'Backspace') set([...v].slice(0, -1).join(''));
    else if (e.key === 'Enter') set(`${v}\n`);
    else if (e.key === 'Tab') set(`${v}  `);
    else if (e.key === 'Escape') { releaseTextFocus(token); setFocus(false); }
    else if (e.char) set(v + e.char);
  };
  const shown = secure && !focus ? String(value || '').replace(/[^\n]/g, '•') : value;
  return (
    <View onMouseDown={() => { takeTextFocus(token, onKey, () => setFocus(false)); setFocus(true); }}
      style={{ minHeight, padding: 8, borderRadius: 7, backgroundColor: th.inputBg, border: `1px solid ${focus ? th.accent : th.separator}` }}>
      {value ? <Text style={{ fontSize: 13, color: th.text, lineHeight: 18, fontFamily: mono ? 'monospace' : undefined }}>{shown}{focus ? '▏' : ''}</Text>
        : <Text style={{ fontSize: 13, color: th.textDim }}>{focus ? '▏' : placeholder || ''}</Text>}
    </View>
  );
}

// ---- elegir un archivo (carpeta personal y discos del usuario: pendrives) ----

export function FilePicker({ title, save, suggest = '', onPick, onClose, filter }) {
  const th = useTheme();
  const [dir, setDir] = useState(null);            // null = lugares
  const [name, setName] = useState(suggest);
  const [sel, setSel] = useState(null);
  let entries = [];
  let places = [];
  if (dir === null) {
    places = [{ name: t('passwords.picker.home'), path: fs.home(), icon: '🏠' }];
    try { for (const v of fs.volumes()) places.push({ name: v.name || v.path, path: v.path, icon: '💾' }); } catch (e) { /* sin discos */ }
  } else {
    try { entries = fs.list(dir).filter((e) => e.dir || !filter || filter(e.name)); } catch (e) { entries = []; }
  }
  const up = () => { const h = fs.home(); if (dir === h || dir === null || !dir.includes('/')) setDir(null); else setDir(fs.parent(dir) || null); setSel(null); };
  const choose = () => {
    if (save) { if (dir && name) onPick(fs.join(dir, name)); return; }
    if (sel) onPick(fs.join(dir, sel));
  };
  return (
    <Sheet title={title} onClose={onClose} width={520} footer={[
      <Button key="c" label={t('passwords.cancel')} onClick={onClose} />,
      <Button key="o" primary label={save ? t('passwords.picker.save') : t('passwords.picker.open')} onClick={choose} />]}>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
        <Button label="‹" onClick={up} />
        <Text style={{ flex: 1, fontSize: 12, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{dir === null ? t('passwords.picker.places') : dir}</Text>
      </View>
      <View style={{ height: 260, borderRadius: 8, border: `1px solid ${th.separator}`, overflow: 'hidden' }}>
        <ScrollView style={{ flex: 1 }}>
          {dir === null ? places.map((p) => (
            <Row key={p.path} icon={p.icon} label={p.name} onClick={() => setDir(p.path)} />
          )) : entries.map((e) => (
            <Row key={e.name} icon={e.dir ? '📁' : '📄'} label={e.name} selected={sel === e.name}
              onClick={() => (e.dir ? (setDir(fs.join(dir, e.name)), setSel(null)) : (setSel(e.name), save && setName(e.name)))}
              onDoubleClick={() => !e.dir && onPick(fs.join(dir, e.name))} />
          ))}
          {dir !== null && !entries.length && <Text style={{ fontSize: 12, color: th.textDim, margin: 10 }}>{t('passwords.picker.empty')}</Text>}
        </ScrollView>
      </View>
      {save && <TextInput value={name} onChange={setName} placeholder={t('passwords.picker.name')} onSubmit={choose} />}
    </Sheet>
  );
}

function Row({ icon, label, onClick, onDoubleClick, selected }) {
  const th = useTheme();
  return (
    <View onClick={onClick} onDoubleClick={onDoubleClick} style={{ flexDirection: 'row', alignItems: 'center', gap: 8, paddingHorizontal: 10, paddingVertical: 6,
      backgroundColor: selected ? th.accent : 'transparent' }}>
      <Text style={{ fontSize: 14 }}>{icon}</Text>
      <Text style={{ flex: 1, fontSize: 13, color: selected ? '#fff' : th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{label}</Text>
    </View>
  );
}

// ---- menú desplegable chico ----

export function Choice({ value, options, onChange, width = 200 }) {
  const th = useTheme();
  const [open, setOpen] = useState(false);
  const cur = options.find((o) => o[0] === value);
  return (
    <View style={{ width }}>
      <View onClick={() => setOpen(!open)} style={{ height: 30, paddingHorizontal: 10, borderRadius: 7, flexDirection: 'row', alignItems: 'center',
        backgroundColor: th.inputBg, border: `1px solid ${th.separator}` }}>
        <Text style={{ flex: 1, fontSize: 13, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{cur ? cur[1] : String(value ?? '')}</Text>
        <Text style={{ fontSize: 10, color: th.textDim }}>▾</Text>
      </View>
      {open && (
        <View style={{ position: 'absolute', top: 32, left: 0, width, zIndex: 60, borderRadius: 8, padding: 4, backgroundColor: th.windowBg,
          border: `1px solid ${th.separator}`, boxShadow: '0 10px 28px rgba(0,0,0,0.4)' }}>
          {options.map(([v, label]) => (
            <View key={String(v)} onClick={() => { setOpen(false); onChange(v); }}
              style={{ paddingHorizontal: 8, paddingVertical: 5, borderRadius: 5, backgroundColor: v === value ? th.accent : 'transparent' }}>
              <Text style={{ fontSize: 13, color: v === value ? '#fff' : th.text }}>{label}</Text>
            </View>
          ))}
        </View>
      )}
    </View>
  );
}

// Botón chico con un símbolo (copiar, mostrar, generar)
export function IconButton({ glyph, onClick, title, active, size = 26 }) {
  const th = useTheme();
  const [hover, setHover] = useState(false);
  return (
    <View onClick={onClick} onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)}
      style={{ minWidth: size, height: size, paddingHorizontal: title ? 8 : 0, borderRadius: 6, flexDirection: 'row', gap: 4, alignItems: 'center',
        justifyContent: 'center', backgroundColor: active ? th.accent : hover ? th.hover : th.control }}>
      <Text style={{ fontSize: 12, color: active ? '#fff' : th.text }}>{glyph}</Text>
      {title ? <Text style={{ fontSize: 12, color: active ? '#fff' : th.text }}>{title}</Text> : null}
    </View>
  );
}

export function Chip({ label, color, onClick, onRemove, selected }) {
  const th = useTheme();
  return (
    <View onClick={onClick} style={{ flexDirection: 'row', alignItems: 'center', gap: 5, height: 22, paddingHorizontal: 8, borderRadius: 11,
      backgroundColor: selected ? th.accent : th.control }}>
      <View style={{ width: 8, height: 8, borderRadius: 4, backgroundColor: color || '#8e8e93' }} />
      <Text style={{ fontSize: 11, color: selected ? '#fff' : th.text }}>{label}</Text>
      {onRemove && <Text onClick={(e) => { e.stopPropagation?.(); onRemove(); }} style={{ fontSize: 10, color: th.textDim }}>✕</Text>}
    </View>
  );
}

// base64 → bytes (QuickJS no trae atob)
export function b64ToBytes(s) {
  const A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  const map = new Uint8Array(128);
  for (let i = 0; i < 64; i++) map[A.charCodeAt(i)] = i;
  const clean = String(s).replace(/[^A-Za-z0-9+/]/g, '');
  const out = new Uint8Array(Math.floor((clean.length * 3) / 4));
  let o = 0;
  for (let i = 0; i + 1 < clean.length; i += 4) {
    const a = map[clean.charCodeAt(i)], b = map[clean.charCodeAt(i + 1)];
    const c = i + 2 < clean.length ? map[clean.charCodeAt(i + 2)] : 0, d = i + 3 < clean.length ? map[clean.charCodeAt(i + 3)] : 0;
    const v = (a << 18) | (b << 12) | (c << 6) | d;
    if (o < out.length) out[o++] = (v >> 16) & 255;
    if (o < out.length && i + 2 < clean.length) out[o++] = (v >> 8) & 255;
    if (o < out.length && i + 3 < clean.length) out[o++] = v & 255;
  }
  return out;
}

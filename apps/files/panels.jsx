// panels.jsx — menú contextual (con submenús), Vista rápida, Ver información y confirmar.
// Todos se dibujan adentro de la ventana, con coordenadas relativas a ella.
import { View, Text, Button, useTheme, formatBytes, formatDate, formatTime, addListener, t as tr } from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';
import { Preview, kindLabel } from './views.jsx';
import { EntryIcon, TagDots } from './glyphs.jsx';
import { tagsOf } from './ops.js';
import { QuickContent } from './quicklook.jsx';

const inside = (inst, ref) => { for (let n = inst; n; n = n.parent) if (n === ref?.current) return true; return false; };
const ITEM_H = 24;

function MenuBox({ items, x, y, width, onClose, onSub, boxRef, bounds }) {
  const t = useTheme();
  const [hover, setHover] = useState(-1);
  const h = items.reduce((s, it) => s + (it === '-' ? 9 : it.header ? 20 : ITEM_H), 10);
  const left = Math.max(4, Math.min(x, bounds.w - width - 4));
  const top = Math.max(4, Math.min(y, bounds.h - h - 4));
  return (
    <View ref={boxRef} style={{ position: 'absolute', left, top, width, paddingVertical: 5, borderRadius: 8, backgroundColor: t.windowBg,
      border: `1px solid ${t.separator}`, boxShadow: '0 10px 28px rgba(0,0,0,0.35)' }}>
      {items.map((it, i) => {
        if (it === '-') return <View key={i} style={{ height: 1, marginVertical: 4, marginHorizontal: 8, backgroundColor: t.separator }} />;
        if (it.header) return <Text key={i} style={{ height: 20, paddingHorizontal: 14, paddingTop: 4, fontSize: 11, fontWeight: 600, color: t.textDim }}>{it.header}</Text>;
        if (it.swatches) {
          return (
            <View key={i} style={{ flexDirection: 'row', gap: 6, height: ITEM_H, alignItems: 'center', paddingHorizontal: 14 }}>
              {it.swatches.map((s) => (
                <View key={s.color} onClick={() => { onClose(); s.onClick(); }} style={{ width: 14, height: 14, borderRadius: 7, backgroundColor: s.hex,
                  border: s.on ? `2px solid ${t.text}` : '0 solid transparent' }} />
              ))}
            </View>
          );
        }
        const on = hover === i && !it.disabled;
        return (
          <View key={i} onMouseEnter={(ev) => { setHover(i); onSub(it.submenu ? { items: it.submenu, x: left + width - 6, y: top + 5 + items.slice(0, i).reduce((s, q) => s + (q === '-' ? 9 : q.header ? 20 : ITEM_H), 0) - 5 } : null); void ev; }}
            onMouseLeave={() => setHover(-1)}
            onClick={() => { if (!it.disabled && !it.submenu) { onClose(); it.onClick?.(); } }}
            style={{ height: ITEM_H, marginHorizontal: 5, paddingHorizontal: 9, borderRadius: 5, flexDirection: 'row', alignItems: 'center', backgroundColor: on ? t.accent : 'transparent' }}>
            <Text style={{ width: 14, fontSize: 11, color: on ? '#fff' : t.text }}>{it.checked ? '✓' : ''}</Text>
            <Text style={{ flex: 1, fontSize: 13, color: on ? '#fff' : it.disabled ? t.textDim : it.danger ? '#ff453a' : t.text, whiteSpace: 'nowrap' }}>{it.label}</Text>
            {it.hint ? <Text style={{ fontSize: 11, color: on ? '#fff' : t.textDim }}>{it.hint}</Text> : null}
            {it.submenu ? <Text style={{ fontSize: 12, color: on ? '#fff' : t.textDim }}>›</Text> : null}
          </View>
        );
      })}
    </View>
  );
}

// Menú contextual: items [{ label, onClick, hint, checked, disabled, danger, submenu } | '-' | { header } | { swatches }]
export function ContextMenu({ x, y, items, onClose, bounds }) {
  const ref = useRef(null), subRef = useRef(null);
  const [sub, setSub] = useState(null);
  useEffect(() => {
    const a = addListener('pointerdown', (e) => { if (!inside(e.target, ref) && !inside(e.target, subRef)) onClose(); });
    const b = addListener('keydown', (e) => { if (e.key === 'Escape') onClose(); });
    return () => { a(); b(); };
  }, []);
  const width = (list) => Math.min(320, Math.max(190, ...list.filter((i) => i && i.label).map((i) => 46 + lord.measureText(i.label, 13, 400) + (i.hint ? 16 + lord.measureText(i.hint, 11, 400) : 0))));
  return (
    <>
      <MenuBox items={items} x={x} y={y} width={width(items)} onClose={onClose} onSub={setSub} boxRef={ref} bounds={bounds} />
      {sub && <MenuBox items={sub.items} x={sub.x} y={sub.y} width={width(sub.items)} onClose={onClose} onSub={() => {}} boxRef={subRef} bounds={bounds} />}
    </>
  );
}

// Vista rápida (Espacio): la imagen, el texto o la ficha del archivo, en grande.
export function QuickLook({ e, onClose, index, count }) {
  const t = useTheme();
  if (!e) return null;
  return (
    <View onMouseDown={onClose} style={{ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0, backgroundColor: 'rgba(0,0,0,0.35)', alignItems: 'center', justifyContent: 'center' }}>
      <View onMouseDown={(ev) => ev.stopPropagation()} style={{ width: '82%', height: '86%', borderRadius: 12, backgroundColor: t.windowBg,
        border: `1px solid ${t.separator}`, boxShadow: '0 16px 40px rgba(0,0,0,0.45)', overflow: 'hidden' }}>
        <View style={{ height: 32, flexDirection: 'row', alignItems: 'center', paddingHorizontal: 12, }}>
          <View onClick={onClose} style={{ width: 12, height: 12, borderRadius: 6, backgroundColor: '#ff5f57' }} />
          <Text style={{ flex: 1, textAlign: 'center', fontSize: 12, fontWeight: 600, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{e.name}</Text>
          <Text style={{ fontSize: 11, color: t.textDim }}>{count > 1 ? `${index + 1} / ${count}` : ''}</Text>
        </View>
        <View style={{ height: 1, backgroundColor: t.separator }} />
        <QuickContent e={e} />
      </View>
    </View>
  );
}

const fmt = (ms) => { if (!ms) return '—'; const d = new Date(ms); return `${formatDate(d, 'long')} ${formatTime(d)}`; };

function Row({ k, v, t }) {
  return (
    <View style={{ flexDirection: 'row', gap: 8, paddingVertical: 3 }}>
      <Text style={{ width: 92, fontSize: 11, color: t.textDim, textAlign: 'end' }}>{k}</Text>
      <Text style={{ flex: 1, fontSize: 11, color: t.text }}>{v}</Text>
    </View>
  );
}

// Ver información (❖I): ficha del archivo; el tamaño de las carpetas se calcula acotado.
export function InfoPanel({ e, onClose, openers }) {
  const t = useTheme();
  const [st, setSt] = useState(null);
  const [du, setDu] = useState(null);
  useEffect(() => {
    setSt(null); setDu(null);
    if (!e || e.app) return undefined;
    try { setSt(lord.fs.stat(e.path)); } catch (err) { setSt({ error: err.message }); }
    if (!e.dir) return undefined;
    const id = setTimeout(() => { try { setDu(lord.fs.du(e.path, 900)); } catch (err) { /* sin permiso */ } }, 30);
    return () => clearTimeout(id);
  }, [e?.path]);
  if (!e) return null;
  const size = e.dir ? (du ? `${formatBytes(du.size)}${du.complete ? '' : '+'} · ${tr('files.info.items', { count: du.files + du.dirs })}` : tr('files.info.calculating')) : formatBytes(st?.size ?? e.size);
  return (
    <View style={{ position: 'absolute', right: 12, top: 12, width: 380, borderRadius: 12, padding: 14, gap: 8, backgroundColor: t.windowBg,
      border: `1px solid ${t.separator}`, boxShadow: '0 14px 36px rgba(0,0,0,0.4)' }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
        <EntryIcon e={e} size={48} dark={t.mode === 'dark'} />
        <View style={{ flex: 1 }}>
          <Text style={{ fontSize: 13, fontWeight: 700, color: t.text }}>{e.name}</Text>
          <Text style={{ fontSize: 11, color: t.textDim }}>{`${kindLabel(e)} · ${size}`}</Text>
        </View>
        <View onClick={onClose} style={{ width: 20, height: 20, borderRadius: 10, alignItems: 'center', justifyContent: 'center', backgroundColor: t.control }}>
          <Text style={{ fontSize: 11, color: t.text }}>✕</Text>
        </View>
      </View>
      <TagDots tags={tagsOf(e.path)} size={9} />
      <View style={{ height: 1, backgroundColor: t.separator }} />
      {st?.error ? <Text style={{ fontSize: 11, color: '#ff453a' }}>{st.error}</Text> : (
        <View>
          <Row t={t} k={tr('files.info.size')} v={size} />
          <Row t={t} k={tr('files.info.where')} v={e.trash ? e.original : (st?.path || e.path).split('/').slice(0, -1).join('/') || '/'} />
          {st?.link && <Row t={t} k={tr('files.info.link')} v={st.target} />}
          {st?.btime ? <Row t={t} k={tr('files.info.created')} v={fmt(st.btime)} /> : null}
          <Row t={t} k={tr('files.info.modified')} v={fmt(st?.mtime)} />
          <Row t={t} k={tr('files.info.opened')} v={fmt(st?.atime)} />
          {st && <Row t={t} k={tr('files.info.perms')} v={`${st.mode} · ${st.user}:${st.group}${st.writable ? '' : ` · ${tr('files.info.readonly')}`}`} />}
          {e.trash && <Row t={t} k={tr('files.info.deleted')} v={(e.deleted || '').replace('T', ' ')} />}
        </View>
      )}
      {openers?.length ? (
        <View style={{ gap: 4 }}>
          <View style={{ height: 1, backgroundColor: t.separator }} />
          <Text style={{ fontSize: 11, fontWeight: 600, color: t.textDim }}>{tr('files.menu.openWith')}</Text>
          <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 6 }}>
            {openers.map((o) => <Button key={o.id} label={o.label} onClick={o.run} />)}
          </View>
        </View>
      ) : null}
    </View>
  );
}

export function Confirm({ text, detail, okLabel, onOk, onCancel }) {
  const t = useTheme();
  return (
    <View style={{ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0, backgroundColor: 'rgba(0,0,0,0.3)', alignItems: 'center', justifyContent: 'center' }}>
      <View style={{ width: 340, padding: 18, gap: 10, borderRadius: 12, backgroundColor: t.windowBg, border: `1px solid ${t.separator}`, boxShadow: '0 14px 36px rgba(0,0,0,0.45)' }}>
        <Text style={{ fontSize: 14, fontWeight: 700, color: t.text }}>{text}</Text>
        {detail ? <Text style={{ fontSize: 12, color: t.textDim, lineHeight: 17 }}>{detail}</Text> : null}
        <View style={{ flexDirection: 'row', justifyContent: 'flex-end', gap: 8, marginTop: 6 }}>
          <Button label={tr('files.cancel')} onClick={onCancel} />
          <Button primary label={okLabel} onClick={onOk} />
        </View>
      </View>
    </View>
  );
}

// views.jsx — las cuatro vistas de Archivos: íconos (tarjetas), lista (con carpetas que se
// despliegan), columnas (Miller, con vista previa) y galería.
import { View, Text, Image, TextInput, ScrollView, useTheme, formatBytes, formatDate, formatTime, t as tr } from '@lordos/sdk';
import { useEffect, useState } from 'react';
import { EntryIcon, TagDots } from './glyphs.jsx';
import { kindOf, isImage, isTextual } from './kinds.js';
import { tagsOf } from './ops.js';
import { sortEntries } from './locations.js';

const when = (ms) => { if (!ms) return '—'; const d = new Date(ms); return `${formatDate(d, 'numeric')} ${formatTime(d)}`; };
// carpeta de un resultado de búsqueda o de Recientes (el último tramo de la ruta)
const where = (p) => { const parts = String(p).split('/'); return parts[parts.length - 2] || '/'; };
export const kindLabel = (e) => (e.app ? tr('files.kind.app') : tr(`files.kind.${kindOf(e)}`));

// Nombre, o el campo para renombrar en el lugar.
function Name({ e, ctx, center, color, lines = 1 }) {
  if (ctx.renaming?.path === e.path) {
    return (
      <View onMouseDown={(ev) => ev.stopPropagation()} style={{ width: center ? 110 : 240 }}>
        <TextInput autoFocus selectAll value={ctx.renaming.value} onChange={ctx.onRenameChange} onSubmit={ctx.onRenameCommit} onCancel={ctx.onRenameCancel}
          style={{ height: 22, paddingHorizontal: 4 }} />
      </View>
    );
  }
  return (
    <Text style={{ fontSize: center ? 12 : 13, color, textAlign: center ? 'center' : 'start', whiteSpace: lines === 1 ? 'nowrap' : 'normal',
      overflow: 'hidden', maxHeight: lines * 16, lineHeight: 16 }}>{e.name}</Text>
  );
}

function itemHandlers(e, ctx) {
  return {
    onMouseDown: (ev) => { ev.stopPropagation(); ctx.onItemDown(e, ev); },
    onDoubleClick: (ev) => { ev.stopPropagation(); ctx.onOpen(e); },
    onContextMenu: (ev) => { ev.stopPropagation(); ctx.onContext(e, ev); },
    ...(e.dir && !e.trash && !e.arc ? { dropPath: e.path } : {}),
  };
}

function GroupTitle({ g, t }) {
  if (!g.label) return null;
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8, paddingHorizontal: 10, paddingTop: 10, paddingBottom: 4 }}>
      <Text style={{ fontSize: 12, fontWeight: 700, color: t.text }}>{g.label}</Text>
      <Text style={{ fontSize: 11, color: t.textDim }}>{g.items.length}</Text>
      <View style={{ flex: 1, height: 1, backgroundColor: t.separator }} />
    </View>
  );
}

// ---- íconos ----

export function IconView({ groups, ctx }) {
  const t = useTheme();
  return (
    <ScrollView style={{ flex: 1 }}>
      <View style={{ padding: 8, paddingBottom: 30 }}>
        {groups.map((g) => (
          <View key={g.key}>
            <GroupTitle g={g} t={t} />
            <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 4 }}>
              {g.items.map((e) => {
                const sel = ctx.selected.has(e.path);
                return (
                  <View key={e.path} {...itemHandlers(e, ctx)}
                    style={{ width: 104, paddingVertical: 8, alignItems: 'center', gap: 4, borderRadius: 8, opacity: ctx.cut.has(e.path) ? 0.45 : 1,
                      backgroundColor: sel && ctx.dropTarget !== e.path ? t.hover : ctx.dropTarget === e.path ? t.accent : 'transparent' }}>
                    <EntryIcon e={e} size={64} dark={t.mode === 'dark'} />
                    <View style={{ maxWidth: 98, paddingHorizontal: 4, paddingVertical: 1, borderRadius: 4, backgroundColor: sel ? t.accent : 'transparent', alignItems: 'center' }}>
                      <Name e={e} ctx={ctx} center lines={2} color={sel ? '#fff' : t.text} />
                    </View>
                    <TagDots tags={tagsOf(e.path)} />
                  </View>
                );
              })}
            </View>
          </View>
        ))}
        {groups.every((g) => !g.items.length) && <Text style={{ margin: 20, fontSize: 13, color: t.textDim, textAlign: 'center' }}>{ctx.emptyText}</Text>}
      </View>
    </ScrollView>
  );
}

// ---- lista ----

const COLS = [['name', 'flex'], ['date', 150], ['size', 86], ['kind', 110]];

function Header({ sort, onSort, t, showFolder }) {
  return (
    <View style={{ flexDirection: 'row', height: 24, alignItems: 'center', paddingHorizontal: 10 }}>
      {COLS.map(([k, w]) => (
        <View key={k} onClick={() => onSort(k)} style={{ ...(w === 'flex' ? { flex: 1 } : { width: w }), flexDirection: 'row', alignItems: 'center', gap: 4, paddingLeft: w === 'flex' ? 22 : 8 }}>
          <Text style={{ fontSize: 11, fontWeight: 600, color: sort.by === k ? t.text : t.textDim }}>{tr(`files.col.${k === 'kind' && showFolder ? 'where' : k}`)}</Text>
          {sort.by === k && <Text style={{ fontSize: 9, color: t.textDim }}>{sort.desc ? '▼' : '▲'}</Text>}
        </View>
      ))}
    </View>
  );
}

function ListRows({ items, depth, ctx, t, sort, alt }) {
  const rows = [];
  for (const e of items) {
    const sel = ctx.selected.has(e.path);
    const open = ctx.expanded.has(e.path);
    const odd = (alt.n++) % 2 === 1;
    rows.push(
      <View key={e.path} {...itemHandlers(e, ctx)}
        style={{ flexDirection: 'row', alignItems: 'center', height: 26, paddingHorizontal: 10, borderRadius: 5, opacity: ctx.cut.has(e.path) ? 0.45 : 1,
          backgroundColor: ctx.dropTarget === e.path ? t.accent : sel ? t.accent : odd ? (t.mode === 'dark' ? 'rgba(255,255,255,0.03)' : 'rgba(0,0,0,0.025)') : 'transparent' }}>
        <View style={{ flex: 1, flexDirection: 'row', alignItems: 'center', gap: 6, paddingLeft: depth * 16 }}>
          <View onMouseDown={(ev) => { ev.stopPropagation(); if (e.dir && !e.trash) ctx.onToggleExpand(e.path); }} style={{ width: 14, alignItems: 'center' }}>
            {e.dir && !e.trash ? <Text style={{ fontSize: 9, color: sel ? '#fff' : t.textDim }}>{open ? '▼' : '▶'}</Text> : null}
          </View>
          <EntryIcon e={e} size={18} dark={t.mode === 'dark'} thumb={false} />
          <View style={{ flex: 1 }}><Name e={e} ctx={ctx} color={sel ? '#fff' : t.text} /></View>
          <TagDots tags={tagsOf(e.path)} size={7} />
        </View>
        <Text style={{ width: 150, paddingLeft: 8, fontSize: 12, color: sel ? '#fff' : t.textDim, whiteSpace: 'nowrap' }}>{e.trash ? (e.deleted || '').replace('T', ' ') : when(e.mtime)}</Text>
        <Text style={{ width: 86, paddingLeft: 8, fontSize: 12, color: sel ? '#fff' : t.textDim, textAlign: 'end' }}>{e.dir && !e.trash ? '—' : e.app ? '' : formatBytes(e.size)}</Text>
        <Text style={{ width: 110, paddingLeft: 8, fontSize: 12, color: sel ? '#fff' : t.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{ctx.showFolder ? where(e.path) : kindLabel(e)}</Text>
      </View>,
    );
    if (open) {
      let kids = [];
      try { kids = sortEntries(lord.fs.list(e.path).filter((x) => ctx.hidden || !x.name.startsWith('.')).map((x) => ({ ...x, path: `${e.path}/${x.name}` })), sort); } catch (err) { /* sin permiso */ }
      rows.push(...ListRows({ items: kids, depth: depth + 1, ctx, t, sort, alt }));
    }
  }
  return rows;
}

export function ListView({ groups, ctx, sort, onSort }) {
  const t = useTheme();
  const alt = { n: 0 };
  return (
    <View style={{ flex: 1 }}>
      <Header sort={sort} onSort={onSort} t={t} showFolder={ctx.showFolder} />
      <View style={{ height: 1, backgroundColor: t.separator }} />
      <ScrollView style={{ flex: 1 }}>
        <View style={{ padding: 4, paddingBottom: 30 }}>
          {groups.map((g) => (
            <View key={g.key}>
              <GroupTitle g={g} t={t} />
              {ListRows({ items: g.items, depth: 0, ctx, t, sort, alt })}
            </View>
          ))}
          {groups.every((g) => !g.items.length) && <Text style={{ margin: 20, fontSize: 13, color: t.textDim, textAlign: 'center' }}>{ctx.emptyText}</Text>}
        </View>
      </ScrollView>
    </View>
  );
}

// ---- vista previa (columnas, galería, Vista rápida) ----

export function Preview({ e, big = false }) {
  const t = useTheme();
  const [text, setText] = useState(null);
  useEffect(() => {
    setText(null);
    if (!e || e.dir || e.app || !isTextual(e) || e.size > 8 * 1024 * 1024) return;
    try { setText(lord.fs.read(e.path, 'text').slice(0, 65536)); } catch (err) { setText(null); }
  }, [e?.path]);
  if (!e) return null;
  return (
    <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', gap: 10, padding: 12 }}>
      {isImage(e) ? <Image src={e.path} style={{ width: '100%', flex: 1, objectFit: 'contain' }} />
        : text != null ? (
          <View style={{ alignSelf: 'stretch', flex: 1, borderRadius: 6, backgroundColor: t.mode === 'dark' ? '#16161a' : '#fbfbfd', border: `1px solid ${t.separator}`, padding: 10, overflow: 'hidden' }}>
            <Text style={{ fontFamily: 'monospace', fontSize: big ? 12 : 10, color: t.text, lineHeight: big ? 17 : 14 }}>{text.split('\n').slice(0, big ? 200 : 60).join('\n')}</Text>
          </View>
        ) : <EntryIcon e={e} size={big ? 160 : 96} dark={t.mode === 'dark'} />}
      <Text style={{ fontSize: 13, fontWeight: 600, color: t.text, textAlign: 'center' }}>{e.name}</Text>
      <Text style={{ fontSize: 11, color: t.textDim }}>{`${kindLabel(e)}${e.dir || e.app ? '' : ` · ${formatBytes(e.size)}`}${e.mtime ? ` · ${when(e.mtime)}` : ''}`}</Text>
    </View>
  );
}

// ---- columnas (Miller) ----

export function ColumnView({ root, chain, setChain, entriesOf, ctx, sort }) {
  const t = useTheme();
  const dirs = [root, ...chain.filter((p) => ctx.isDir(p))];
  const last = chain[chain.length - 1];
  const file = last && !ctx.isDir(last) ? ctx.entryAt(last) : null;
  // las que entran: las últimas 3 carpetas
  const shown = dirs.slice(-3);
  const offset = dirs.length - shown.length;
  return (
    <View style={{ flex: 1, flexDirection: 'row', direction: 'ltr' }}>
      {shown.map((dir, ci) => {
        const items = sortEntries(entriesOf(dir), sort);
        const level = offset + ci;
        return (
          <View key={dir} dropPath={dir} style={{ width: 221, flexDirection: 'row' }}>
            <View style={{ flex: 1 }}>
            <ScrollView style={{ flex: 1 }}>
              <View style={{ padding: 4 }}>
                {items.map((e) => {
                  const inChain = chain[level] === e.path;
                  const sel = ctx.selected.has(e.path) || inChain;
                  return (
                    <View key={e.path} {...itemHandlers(e, ctx)}
                      onMouseDown={(ev) => { ev.stopPropagation(); setChain([...chain.slice(0, level), e.path]); ctx.onItemDown(e, ev); }}
                      style={{ flexDirection: 'row', alignItems: 'center', gap: 6, height: 24, paddingHorizontal: 6, borderRadius: 5,
                        backgroundColor: ctx.dropTarget === e.path || sel ? (ctx.selected.has(e.path) ? t.accent : t.hover) : 'transparent' }}>
                      <EntryIcon e={e} size={16} dark={t.mode === 'dark'} thumb={false} />
                      <View style={{ flex: 1 }}><Name e={e} ctx={ctx} color={ctx.selected.has(e.path) ? '#fff' : t.text} /></View>
                      {e.dir && <Text style={{ fontSize: 10, color: ctx.selected.has(e.path) ? '#fff' : t.textDim }}>›</Text>}
                    </View>
                  );
                })}
              </View>
            </ScrollView>
            </View>
            <View style={{ width: 1, backgroundColor: t.separator }} />
          </View>
        );
      })}
      <View style={{ flex: 1 }}>{file ? <Preview e={file} /> : null}</View>
    </View>
  );
}

// ---- galería ----

export function GalleryView({ entries, ctx }) {
  const t = useTheme();
  const cur = entries.find((e) => ctx.selected.has(e.path)) || entries[0];
  const idx = Math.max(0, entries.indexOf(cur));
  const from = Math.max(0, idx - 4);
  return (
    <View style={{ flex: 1 }}>
      <View style={{ flex: 1 }}>{cur ? <Preview e={cur} big /> : <Text style={{ margin: 20, color: t.textDim, textAlign: 'center' }}>{ctx.emptyText}</Text>}</View>
      <View style={{ height: 1, backgroundColor: t.separator }} />
      <View style={{ height: 92, flexDirection: 'row', gap: 6, paddingHorizontal: 10, alignItems: 'center', justifyContent: 'center', direction: 'ltr' }}>
        {entries.slice(from, from + 9).map((e) => {
          const sel = e === cur;
          return (
            <View key={e.path} {...itemHandlers(e, ctx)}
              style={{ width: 76, height: 76, alignItems: 'center', justifyContent: 'center', borderRadius: 8,
                border: `2px solid ${sel ? t.accent : 'transparent'}`, backgroundColor: sel ? t.hover : 'transparent' }}>
              <EntryIcon e={e} size={60} dark={t.mode === 'dark'} />
            </View>
          );
        })}
      </View>
    </View>
  );
}

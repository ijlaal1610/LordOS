// views.jsx — the four Files views: icons (cards), list (with expandable folders),
// columns (Miller, with a preview) and gallery.
import { View, Text, Image, TextInput, ScrollView, EmptyState, useTheme, direction, formatBytes, formatDate, formatTime, typeScale, t as tr } from '@lordos/sdk';
import { useEffect, useState } from 'react';
import { EntryIcon, TagDots } from './glyphs.jsx';
import { kindOf, isImage, isTextual } from './kinds.js';
import { tagsOf } from './ops.js';
import { sortEntries } from './locations.js';

const when = (ms) => { if (!ms) return '—'; const d = new Date(ms); return `${formatDate(d, 'numeric')} ${formatTime(d)}`; };
// folder of a search or Recents result (the last segment of the path)
const where = (p) => { const parts = String(p).split('/'); return parts[parts.length - 2] || '/'; };
export const kindLabel = (e) => (e.app ? tr('files.kind.app') : tr(`files.kind.${kindOf(e)}`));

// Name, or the field for renaming in place.
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
      overflow: 'hidden', maxHeight: lines * 16, lineHeight: 16, textOverflow: 'ellipsis', numberOfLines: lines }}>{e.name}</Text>
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
    <View style={{ paddingHorizontal: 8, paddingTop: 14, paddingBottom: 6, gap: 6 }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
        <Text style={{ fontSize: typeScale.body, fontWeight: 600, color: t.text }}>{g.label}</Text>
        <Text style={{ fontSize: typeScale.caption, color: t.textDim }}>{g.items.length}</Text>
      </View>
      <View style={{ height: 1, backgroundColor: t.separator }} />
    </View>
  );
}

// ---- icons ----

export function IconView({ groups, ctx }) {
  const t = useTheme();
  return (
    <ScrollView style={{ flex: 1 }}>
      <View style={{ padding: 12, paddingBottom: 32 }}>
        {groups.map((g) => (
          <View key={g.key}>
            <GroupTitle g={g} t={t} />
            <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8 }}>
              {g.items.map((e) => {
                const sel = ctx.selected.has(e.path);
                return (
                  <View key={e.path} {...itemHandlers(e, ctx)}
                    style={{ width: 100, paddingVertical: 6, alignItems: 'center', gap: 5, opacity: ctx.cut.has(e.path) ? 0.45 : 1 }}>
                    <View style={{ padding: 4, borderRadius: 7, backgroundColor: ctx.dropTarget === e.path ? t.accent : sel ? (t.mode === 'dark' ? 'rgba(255,255,255,0.12)' : 'rgba(0,0,0,0.08)') : 'transparent' }}>
                      <EntryIcon e={e} size={64} dark={t.mode === 'dark'} />
                    </View>
                    <View style={{ maxWidth: 96, paddingHorizontal: 5, paddingVertical: 1, borderRadius: 5, backgroundColor: sel ? t.accent : 'transparent', alignItems: 'center' }}>
                      <Name e={e} ctx={ctx} center lines={2} color={sel ? '#fff' : t.text} />
                    </View>
                    <TagDots tags={tagsOf(e.path)} />
                  </View>
                );
              })}
            </View>
          </View>
        ))}
        {groups.every((g) => !g.items.length) && <EmptyState title={ctx.emptyText} style={{ minHeight: 260 }} />}
      </View>
    </ScrollView>
  );
}

// ---- list ----

const COLS = [['name', 'flex'], ['date', 150], ['size', 80], ['kind', 120]];

function Header({ sort, onSort, t, showFolder }) {
  return (
    <View style={{ flexDirection: 'row', height: 27, alignItems: 'center', paddingHorizontal: 18, borderBottomWidth: 1, borderBottomColor: t.separator }}>
      {COLS.map(([k, w], i) => (
        <View key={k} onClick={() => onSort(k)} style={{ ...(w === 'flex' ? { flex: 1 } : { width: w }), height: 26, flexDirection: 'row', alignItems: 'center', gap: 4,
          justifyContent: k === 'size' ? 'flex-end' : 'flex-start', paddingHorizontal: w === 'flex' ? 20 : 8 }}>
          {i > 0 && <View style={{ position: 'absolute', top: 6, bottom: 6, insetInlineStart: 0, width: 1, backgroundColor: t.separator }} />}
          <Text style={{ fontSize: typeScale.caption, fontWeight: sort.by === k ? 600 : 500, color: sort.by === k ? t.text : t.textDim, whiteSpace: 'nowrap' }}>{tr(`files.col.${k === 'kind' && showFolder ? 'where' : k}`)}</Text>
          {sort.by === k && <Text style={{ fontSize: 8, color: t.textDim }}>{sort.desc ? '▼' : '▲'}</Text>}
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
        style={{ flexDirection: 'row', alignItems: 'center', height: 24, paddingHorizontal: 10, borderRadius: 5, opacity: ctx.cut.has(e.path) ? 0.45 : 1,
          backgroundColor: ctx.dropTarget === e.path ? t.accent : sel ? t.accent : odd ? (t.mode === 'dark' ? 'rgba(255,255,255,0.03)' : 'rgba(0,0,0,0.025)') : 'transparent' }}>
        <View style={{ flex: 1, flexDirection: 'row', alignItems: 'center', gap: 6, paddingLeft: depth * 16 }}>
          <View onMouseDown={(ev) => { ev.stopPropagation(); if (e.dir && !e.trash) ctx.onToggleExpand(e.path); }} style={{ width: 14, alignItems: 'center' }}>
            {e.dir && !e.trash ? <Text style={{ fontSize: 9, color: sel ? '#fff' : t.textDim }}>{open ? '▼' : '▶'}</Text> : null}
          </View>
          <EntryIcon e={e} size={16} dark={t.mode === 'dark'} thumb={false} />
          <View style={{ flex: 1, minWidth: 0 }}><Name e={e} ctx={ctx} color={sel ? '#fff' : t.text} /></View>
          <TagDots tags={tagsOf(e.path)} size={7} />
        </View>
        <Text style={{ width: 150, paddingHorizontal: 8, fontSize: typeScale.callout, color: sel ? '#fff' : t.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{e.trash ? (e.deleted || '').replace('T', ' ') : when(e.mtime)}</Text>
        <Text style={{ width: 80, paddingHorizontal: 8, fontSize: typeScale.callout, color: sel ? '#fff' : t.textDim, textAlign: 'end', whiteSpace: 'nowrap' }}>{e.dir && !e.trash ? '—' : e.app ? '' : formatBytes(e.size)}</Text>
        <Text style={{ width: 120, paddingHorizontal: 8, fontSize: typeScale.callout, color: sel ? '#fff' : t.textDim, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{ctx.showFolder ? where(e.path) : kindLabel(e)}</Text>
      </View>,
    );
    if (open) {
      let kids = [];
      try { kids = sortEntries(lord.fs.list(e.path).filter((x) => ctx.hidden || !x.name.startsWith('.')).map((x) => ({ ...x, path: `${e.path}/${x.name}` })), sort); } catch (err) { /* no permission */ }
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
      <ScrollView style={{ flex: 1 }}>
        <View style={{ paddingHorizontal: 8, paddingTop: 4, paddingBottom: 32 }}>
          {groups.map((g) => (
            <View key={g.key}>
              <GroupTitle g={g} t={t} />
              {ListRows({ items: g.items, depth: 0, ctx, t, sort, alt })}
            </View>
          ))}
            {groups.every((g) => !g.items.length) && <EmptyState title={ctx.emptyText} style={{ minHeight: 240 }} />}
        </View>
      </ScrollView>
    </View>
  );
}

// ---- preview (columns, gallery, Quick Look) ----

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
    <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', gap: 6, padding: 16 }}>
      {isImage(e) ? <Image src={e.path} style={{ width: '100%', flex: 1, objectFit: 'contain', borderRadius: 6 }} />
        : text != null ? (
          <View style={{ alignSelf: 'stretch', flex: 1, borderRadius: 8, backgroundColor: t.mode === 'dark' ? '#16161a' : '#fbfbfd', border: `1px solid ${t.separator}`, padding: 12, overflow: 'hidden', direction: 'ltr' }}>
            <Text style={{ fontFamily: 'monospace', fontSize: big ? 12 : 10, color: t.text, lineHeight: big ? 17 : 14 }}>{text.split('\n').slice(0, big ? 200 : 60).join('\n')}</Text>
          </View>
        ) : <EntryIcon e={e} size={big ? 160 : 96} dark={t.mode === 'dark'} />}
      <Text style={{ fontSize: typeScale.body, fontWeight: 600, color: t.text, textAlign: 'center', marginTop: 6 }}>{e.name}</Text>
      <Text style={{ fontSize: typeScale.caption, color: t.textDim, textAlign: 'center' }}>{`${kindLabel(e)}${e.dir || e.app ? '' : ` · ${formatBytes(e.size)}`}${e.mtime ? ` · ${when(e.mtime)}` : ''}`}</Text>
    </View>
  );
}

// ---- columns (Miller) ----

export function ColumnView({ root, chain, setChain, entriesOf, ctx, sort }) {
  const t = useTheme();
  const dirs = [root, ...chain.filter((p) => ctx.isDir(p))];
  const last = chain[chain.length - 1];
  const file = last && !ctx.isDir(last) ? ctx.entryAt(last) : null;
  // the ones that fit: the last 3 folders
  const shown = dirs.slice(-3);
  const offset = dirs.length - shown.length;
  return (
    <View style={{ flex: 1, flexDirection: 'row', direction: 'ltr' }}>
      {shown.map((dir, ci) => {
        const items = sortEntries(entriesOf(dir), sort);
        const level = offset + ci;
        return (
          <View key={dir} dropPath={dir} style={{ width: 220, flexDirection: 'row' }}>
            <View style={{ flex: 1 }}>
            <ScrollView style={{ flex: 1 }}>
              <View style={{ paddingHorizontal: 6, paddingVertical: 6 }}>
                {items.map((e) => {
                  const inChain = chain[level] === e.path;
                  const sel = ctx.selected.has(e.path) || inChain;
                  return (
                    <View key={e.path} {...itemHandlers(e, ctx)}
                      onMouseDown={(ev) => { ev.stopPropagation(); setChain([...chain.slice(0, level), e.path]); ctx.onItemDown(e, ev); }}
                      style={{ flexDirection: 'row', alignItems: 'center', gap: 6, height: 24, paddingHorizontal: 8, borderRadius: 5,
                        backgroundColor: ctx.dropTarget === e.path || sel ? (ctx.selected.has(e.path) ? t.accent : t.hover) : 'transparent' }}>
                      <EntryIcon e={e} size={16} dark={t.mode === 'dark'} thumb={false} />
                      <View style={{ flex: 1, minWidth: 0 }}><Name e={e} ctx={ctx} color={ctx.selected.has(e.path) ? '#fff' : t.text} /></View>
                      {e.dir && <Text style={{ fontSize: 12, color: ctx.selected.has(e.path) ? '#fff' : t.textDim }}>›</Text>}
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

// ---- gallery ----

export function GalleryView({ entries, ctx }) {
  const t = useTheme();
  const cur = entries.find((e) => ctx.selected.has(e.path)) || entries[0];
  const idx = Math.max(0, entries.indexOf(cur));
  const from = Math.max(0, idx - 4);
  return (
    <View style={{ flex: 1 }}>
      <View style={{ flex: 1, padding: 8 }}>{cur ? <Preview e={cur} big /> : <EmptyState title={ctx.emptyText} />}</View>
      <View style={{ height: 97, borderTopWidth: 1, borderTopColor: t.separator, flexDirection: 'row', gap: 8, paddingHorizontal: 12, alignItems: 'center', justifyContent: 'center', direction: 'ltr', overflow: 'hidden' }}>
        {entries.slice(from, from + 9).map((e) => {
          const sel = e === cur;
          return (
            <View key={e.path} {...itemHandlers(e, ctx)}
              style={{ width: 72, height: 72, alignItems: 'center', justifyContent: 'center', borderRadius: 8,
                border: `2px solid ${sel ? t.accent : 'transparent'}`, backgroundColor: sel ? t.hover : 'transparent' }}>
              <EntryIcon e={e} size={56} dark={t.mode === 'dark'} />
            </View>
          );
        })}
      </View>
    </View>
  );
}

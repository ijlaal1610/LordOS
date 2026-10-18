// ui.jsx — building blocks shared by the hardware pages of Settings (Displays, Bluetooth, Battery,
// Notifications, Date & Time, Default Apps), on top of the SDK's GroupBox (docs/DESIGN.md): a row
// with a hint, a pop-up button (macOS NSPopUpButton), a page header and a note.
import { View, Text, useTheme, addListener, GroupBox, spacing, typeScale, createStore, useStore, screen, ScrollView } from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';

export { GroupBox };

// A row of a GroupBox: label (and an optional hint below it) at the start, the control at the end.
export function SettingRow({ label, hint, children, icon, disabled, onClick, minHeight = 40 }) {
  const t = useTheme();
  return (
    <View onClick={disabled ? undefined : onClick} style={{ minHeight, flexDirection: 'row', alignItems: 'center', gap: spacing.m, paddingVertical: 7,
      opacity: disabled ? 0.45 : 1 }}>
      {icon || null}
      <View style={{ flex: 1, gap: 2 }}>
        <Text style={{ fontSize: typeScale.body, color: t.text, textAlign: 'start' }}>{label}</Text>
        {hint ? <Text style={{ fontSize: typeScale.caption, color: t.textDim, lineHeight: 1.35, textAlign: 'start' }}>{hint}</Text> : null}
      </View>
      {children}
    </View>
  );
}

// Page header: a big icon, the title and one line of explanation (like the top of a macOS pane).
export function PageHeader({ icon, title, subtitle, children }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: spacing.l, padding: spacing.l, borderRadius: 10,
      backgroundColor: t.mode === 'dark' ? 'rgba(255,255,255,0.04)' : 'rgba(0,0,0,0.025)', border: `1px solid ${t.separator}` }}>
      {icon}
      <View style={{ flex: 1, gap: 3 }}>
        <Text style={{ fontSize: typeScale.headline, fontWeight: 700, color: t.text, textAlign: 'start' }}>{title}</Text>
        {subtitle ? <Text style={{ fontSize: typeScale.callout, color: t.textDim, lineHeight: 1.4, textAlign: 'start' }}>{subtitle}</Text> : null}
      </View>
      {children}
    </View>
  );
}

// A rounded square with a glyph, as in the sidebar (also used in rows).
export function Badge({ glyph, color, size = 26 }) {
  return (
    <View style={{ width: size, height: size, borderRadius: size * 0.24, backgroundColor: color, alignItems: 'center', justifyContent: 'center' }}>
      <Text style={{ fontSize: size * 0.55, fontWeight: 700, color: '#fff', lineHeight: size }}>{glyph}</Text>
    </View>
  );
}

// Informational text under a group, or a warning (tone 'warn').
export function Note({ children, tone }) {
  const t = useTheme();
  return <Text style={{ fontSize: typeScale.caption, color: tone === 'warn' ? '#ff9f0a' : t.textDim, lineHeight: 1.4, marginHorizontal: spacing.s, textAlign: 'start' }}>{children}</Text>;
}

// The page: a column of groups. It also hosts the open pop-up menu (PopUpLayer), drawn last so it
// covers the rows below its button (zIndex only orders siblings).
export function Stack({ children, gap = 20 }) {
  return (
    <View style={{ gap, paddingBottom: 24 }}>
      {children}
      <PopUpLayer />
    </View>
  );
}

const popup = createStore(null);       // { node, options, value, onChange, width }

// Pop-up button: shows the current value with ▲▼ and opens the list of options below it (or above,
// near the bottom of the screen). options = [[value, label]]; a value of '-' is a separator.
export function PopUp({ value, options, onChange, width = 190, disabled }) {
  const t = useTheme();
  const [hover, setHover] = useState(false);
  const btn = useRef(null);
  const open = useStore(popup, (p) => !!p && p.node === btn.current);
  useEffect(() => () => { if (popup.get()?.node === btn.current) popup.set(null); }, []);
  const cur = options.find(([v]) => v === value);
  const toggle = () => { if (disabled) return; popup.set(open ? null : { node: btn.current, options, value, onChange, width }); };
  return (
    <View ref={btn} onClick={toggle} onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)}
      style={{ width, height: 24, paddingHorizontal: 9, borderRadius: 6, flexDirection: 'row', alignItems: 'center', gap: 6,
        backgroundColor: t.control, opacity: disabled ? 0.45 : hover || open ? 0.88 : 1, boxShadow: '0 1px 1px rgba(0,0,0,0.12)' }}>
      <Text style={{ flex: 1, fontSize: 12, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden', textAlign: 'start' }}>{cur ? cur[1] : ''}</Text>
      <View style={{ width: 16, height: 18, borderRadius: 4, backgroundColor: t.accent, alignItems: 'center', justifyContent: 'center' }}>
        <Text style={{ fontSize: 8, color: '#fff', lineHeight: 8 }}>▲</Text>
        <Text style={{ fontSize: 8, color: '#fff', lineHeight: 8 }}>▼</Text>
      </View>
    </View>
  );
}

function PopUpLayer() {
  const t = useTheme();
  const p = useStore(popup);
  const origin = useRef(null), menu = useRef(null);
  const [, relayout] = useState(0);
  useEffect(() => {
    if (!p) return undefined;
    relayout((x) => x + 1);             // the origin is known after the first layout
    return addListener('pointerdown', (e) => {
      const inside = (n) => { if (!n) return false; const [x, y, w, h] = lord.getLayout(n.id); return e.x >= x && e.x <= x + w && e.y >= y && e.y <= y + h; };
      if (!inside(menu.current) && !inside(p.node)) popup.set(null);
    });
  }, [p]);
  const anchor = <View ref={origin} style={{ position: 'absolute', left: 0, top: 0, width: 0, height: 0 }} />;
  if (!p || !p.node || !origin.current) return anchor;
  const [ox, oy] = lord.getLayout(origin.current.id);
  const [bx, by, bw, bh] = lord.getLayout(p.node.id);
  const rows = p.options.length;
  const mh = rows * 24 + 12;
  const below = by + bh + 4 + Math.min(mh, 320) < screen.height - 70;
  const height = Math.min(mh, 320);
  const top = below ? by + bh + 4 - oy : by - 4 - height - oy;
  return (
    <>
      {anchor}
      <View ref={menu} style={{ position: 'absolute', left: bx - ox, top, width: Math.max(bw, p.width), height, padding: 5, borderRadius: 8, overflow: 'hidden',
        backgroundColor: t.mode === 'dark' ? '#2c2c31' : '#ffffff', border: `1px solid ${t.separator}`, boxShadow: '0 10px 30px rgba(0,0,0,0.35)' }}>
        <ScrollView style={{ flex: 1 }}>
          {p.options.map(([v, label], i) => (v === '-' ? <View key={`s${i}`} style={{ height: 1, marginVertical: 4, marginHorizontal: 6, backgroundColor: t.separator }} />
            : <PopRow key={String(v)} label={label} checked={v === p.value} onClick={() => { popup.set(null); if (v !== p.value) p.onChange(v); }} />))}
        </ScrollView>
      </View>
    </>
  );
}

function PopRow({ label, checked, onClick }) {
  const t = useTheme();
  const [hover, setHover] = useState(false);
  return (
    <View onClick={onClick} onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)}
      style={{ height: 24, paddingHorizontal: 6, borderRadius: 5, flexDirection: 'row', alignItems: 'center', gap: 4, backgroundColor: hover ? t.accent : 'transparent' }}>
      <Text style={{ width: 14, fontSize: 11, color: hover ? '#fff' : t.text }}>{checked ? '✓' : ''}</Text>
      <Text style={{ flex: 1, fontSize: 12, whiteSpace: 'nowrap', color: hover ? '#fff' : t.text, textAlign: 'start' }}>{label}</Text>
    </View>
  );
}

// "HH:MM" ↔ minutes after midnight, and a compact time field made of two steppers.
export const hhmm = (m) => `${String(Math.floor(m / 60) % 24).padStart(2, '0')}:${String(m % 60).padStart(2, '0')}`;

export function TimeField({ value, onChange, format }) {
  const t = useTheme();
  const step = (d) => onChange((((value + d) % 1440) + 1440) % 1440);
  const Arrow = ({ glyph, d }) => (
    <View onClick={() => step(d)} style={{ width: 16, height: 11, alignItems: 'center', justifyContent: 'center' }}>
      <Text style={{ fontSize: 7, color: t.textDim }}>{glyph}</Text>
    </View>
  );
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: 2, direction: 'ltr' }}>
      <View style={{ height: 24, minWidth: 66, paddingHorizontal: 8, borderRadius: 6, backgroundColor: t.control, justifyContent: 'center' }}>
        <Text style={{ fontSize: 12, color: t.text, textAlign: 'center' }}>{format ? format(value) : hhmm(value)}</Text>
      </View>
      <View>
        <Arrow glyph="▲" d={15} />
        <Arrow glyph="▼" d={-15} />
      </View>
    </View>
  );
}

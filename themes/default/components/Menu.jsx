// Drop-down / context menu. Closes when clicking outside.
import { View, Text, addListener, useTheme, screen, direction } from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';

const inside = (inst, ref) => {
  for (let n = inst; n; n = n.parent) if (n === ref?.current) return true;
  return false;
};

function Item({ item, onClose, onSub }) {
  const t = useTheme();
  const [hover, setHover] = useState(false);
  const self = useRef(null);
  if (item === '-') return <View style={{ height: 1, marginVertical: 4, marginHorizontal: 8, backgroundColor: t.separator }} />;
  if (item.header) return <Text style={{ height: 22, paddingHorizontal: 15, paddingTop: 5, fontSize: 11, fontWeight: 600, color: t.textDim }}>{item.header}</Text>;
  const off = item.disabled;
  const enter = () => {
    setHover(true);
    if (item.submenu && self.current) { const [x, y, w] = lord.getLayout(self.current.id); onSub({ items: item.submenu, x: x + w - 4, y: y - 5 }); }
    else onSub(null);
  };
  return (
    <View ref={self} onMouseEnter={enter} onMouseLeave={() => setHover(false)}
      onClick={() => { if (!off && !item.submenu) { onClose(); item.onClick?.(); } }}
      style={{ height: 24, paddingHorizontal: 10, marginHorizontal: 5, borderRadius: 5, flexDirection: 'row', alignItems: 'center',
        backgroundColor: hover && !off ? t.accent : 'transparent' }}>
      <Text style={{ width: 16, fontSize: 12, color: hover && !off ? '#fff' : t.text }}>{item.checked ? '✓' : ''}</Text>
      <Text style={{ flex: 1, fontSize: 13, color: hover && !off ? '#fff' : off ? t.textDim : t.text, whiteSpace: 'nowrap' }}>{item.label}</Text>
      {item.hint ? <Text style={{ fontSize: 12, color: hover && !off ? '#fff' : t.textDim }}>{item.hint}</Text> : null}
      {item.submenu ? <Text style={{ fontSize: 12, color: hover ? '#fff' : t.textDim }}>›</Text> : null}
    </View>
  );
}

// Width according to the longest text (translations change each item's length).
function fitWidth(items, min) {
  let w = min;
  for (const it of items) {
    if (it === '-' || !it?.label) continue;
    const need = 10 + 16 + lord.measureText(it.label, 13, 400) + (it.hint ? 18 + lord.measureText(it.hint, 12, 400) : 0) + 10 + 10;
    if (need > w) w = need;
  }
  return Math.min(Math.ceil(w), screen.width - 8);
}

// above: y is the bottom edge of the menu (it opens upwards, as from the Dock).
// side: 'left' opens the menu to the left of x and 'right' to the right, regardless of the language
// (the side Dock; screen sides are physical). centered: y is the center of the menu.
export function Menu({ x, y, items, onClose, anchor, width: minWidth = 220, above = false, sub = false, nodeRef, side = null, centered = false }) {
  const t = useTheme();
  const own = useRef(null);
  const ref = nodeRef || own;
  const subRef = useRef(null);
  const [subMenu, setSubMenu] = useState(null);
  const width = fitWidth(items, minWidth);
  // In Arabic/Hebrew x is the right edge: the menu opens to the left.
  if (side ? side === 'left' : direction() === 'rtl') x -= width;
  // Always inside the screen.
  const height = items.reduce((h, it) => h + (it === '-' ? 9 : it?.header ? 22 : 24), 10);
  if (above) y -= height;
  else if (centered) y -= height / 2;
  x = Math.max(4, Math.min(x, screen.width - width - 4));
  y = Math.max(4, Math.min(y, screen.height - height - 4));
  useEffect(() => {
    if (sub) return undefined;              // the submenu is closed by its parent menu
    const offPtr = addListener('pointerdown', (e) => { if (!inside(e.target, ref) && !inside(e.target, subRef) && !inside(e.target, anchor)) onClose(); });
    const offKey = addListener('keydown', (e) => { if (e.key === 'Escape') onClose(); });
    return () => { offPtr(); offKey(); };
  }, [onClose]);
  return (
    <>
    <View ref={ref} style={{
      position: 'absolute', left: x, top: y, width, paddingVertical: 5, borderRadius: 9,
      backgroundColor: t.panelBg, backdropFilter: `blur(${t.blur}px)`, border: `1px solid ${t.windowBorder}`,
      boxShadow: '0 12px 32px rgba(0,0,0,0.35)',
    }}>
      {items.map((it, i) => <Item key={i} item={it} onClose={onClose} onSub={setSubMenu} />)}
    </View>
    {subMenu && <Menu sub nodeRef={subRef} x={subMenu.x} y={subMenu.y} items={subMenu.items} onClose={onClose} width={160} />}
    </>
  );
}

// style.js — converts CSS/React-style styles into lord-server's native properties.
// Colors → ARGB number, shorthands (margin, padding, border, boxShadow, background…) → simple properties.

const NAMED = {
  transparent: 0x00000000, white: 0xffffffff, black: 0xff000000, red: 0xffff0000,
  green: 0xff008000, blue: 0xff0000ff, gray: 0xff808080, grey: 0xff808080,
  yellow: 0xffffff00, orange: 0xffffa500, purple: 0xff800080,
};

const clamp255 = (n) => Math.max(0, Math.min(255, Math.round(n)));

export function color(v) {
  if (typeof v === 'number') return v >>> 0;
  const s = String(v).trim().toLowerCase();
  if (s in NAMED) return NAMED[s] >>> 0;
  if (s[0] === '#') {
    let h = s.slice(1);
    if (h.length === 3 || h.length === 4) h = [...h].map((c) => c + c).join('');
    const rgb = parseInt(h.slice(0, 6), 16);
    const a = h.length === 8 ? parseInt(h.slice(6, 8), 16) : 255;
    return ((a << 24) >>> 0) + rgb;
  }
  const m = s.match(/^rgba?\(([^)]+)\)$/);
  if (m) {
    const p = m[1].split(/[\s,/]+/).filter(Boolean).map(parseFloat);
    const a = p.length > 3 ? clamp255(p[3] * 255) : 255;
    return (((a << 24) >>> 0) + (clamp255(p[0]) << 16) + (clamp255(p[1]) << 8) + clamp255(p[2])) >>> 0;
  }
  return 0;
}

// Splits on spaces/commas outside parentheses: "0 8px 24px rgba(0,0,0,.4)".
function tokens(s, sep = /\s/) {
  const out = [];
  let depth = 0, cur = '';
  for (const ch of String(s)) {
    if (ch === '(') depth++;
    if (ch === ')') depth--;
    if (depth === 0 && sep.test(ch)) { if (cur.trim()) out.push(cur.trim()); cur = ''; } else cur += ch;
  }
  if (cur.trim()) out.push(cur.trim());
  return out;
}

const num = (v) => (typeof v === 'number' ? v : parseFloat(v) || 0);

function dim(v) {
  if (typeof v === 'number') return v;
  const s = String(v).trim();
  if (s === 'auto' || s.endsWith('%')) return s;
  return parseFloat(s) || 0;
}

function box(v) {
  if (typeof v === 'number') return [v, v, v, v];
  const p = tokens(v).map(num);
  if (p.length === 1) return [p[0], p[0], p[0], p[0]];
  if (p.length === 2) return [p[0], p[1], p[0], p[1]];
  if (p.length === 3) return [p[0], p[1], p[2], p[1]];
  return p.slice(0, 4);
}

const ANGLES = { 'to top': 0, 'to right': 90, 'to bottom': 180, 'to left': 270,
  'to top right': 45, 'to bottom right': 135, 'to bottom left': 225, 'to top left': 315 };

function gradient(v) {
  const m = String(v).match(/^linear-gradient\((.*)\)$/s);
  if (!m) return null;
  const parts = tokens(m[1], /,/);
  let angle = 180;
  if (/deg$/.test(parts[0])) angle = parseFloat(parts.shift());
  else if (parts[0] in ANGLES) angle = ANGLES[parts.shift()];
  const stop = (p) => color(tokens(p)[0]);
  return [angle, stop(parts[0]), stop(parts[parts.length - 1])];
}

const COLOR_KEYS = new Set(['backgroundColor', 'borderColor', 'color', 'shadowColor']);
const DIM_KEYS = new Set(['width', 'height', 'minWidth', 'minHeight', 'maxWidth', 'maxHeight',
  'left', 'top', 'right', 'bottom', 'flexBasis']);
const WEIGHTS = { normal: 400, medium: 500, semibold: 600, bold: 700 };

function expand(k, v, out) {
  switch (k) {
    case 'margin': case 'padding': {
      const [t, r, b, l] = box(v);
      out[k + 'Top'] = t; out[k + 'Right'] = r; out[k + 'Bottom'] = b; out[k + 'Left'] = l;
      return;
    }
    case 'marginHorizontal': case 'paddingHorizontal': {
      const base = k.replace('Horizontal', '');
      out[base + 'Left'] = out[base + 'Right'] = num(v);
      return;
    }
    case 'marginVertical': case 'paddingVertical': {
      const base = k.replace('Vertical', '');
      out[base + 'Top'] = out[base + 'Bottom'] = num(v);
      return;
    }
    case 'inset': {
      const [t, r, b, l] = box(v);
      out.top = t; out.right = r; out.bottom = b; out.left = l;
      return;
    }
    case 'flex':
      out.flexGrow = num(v); out.flexShrink = 1; out.flexBasis = 0;
      return;
    case 'border': {
      const p = tokens(v);
      out.borderWidth = num(p[0]);
      out.borderColor = color(p[p.length - 1]);
      return;
    }
    case 'boxShadow': {
      if (v === 'none') { out.shadowColor = 0; return; }
      const p = tokens(v);
      const nums = p.filter((t) => /^-?[\d.]/.test(t)).map(num);
      const c = p.find((t) => !/^-?[\d.]/.test(t));
      out.shadowOffsetX = nums[0] || 0; out.shadowOffsetY = nums[1] || 0; out.shadowBlur = nums[2] || 0;
      out.shadowColor = c ? color(c) : 0x80000000;
      return;
    }
    case 'backdropFilter': {
      const m = String(v).match(/blur\(\s*([\d.]+)/);
      out.backdropBlur = m ? parseFloat(m[1]) : 0;
      return;
    }
    case 'background': case 'backgroundImage': {
      const g = gradient(v);
      if (g) out.gradient = g; else out.backgroundColor = color(v);
      return;
    }
    case 'transform': {
      const list = Array.isArray(v) ? v : [];
      for (const t of list) {
        if ('translateX' in t) out.translateX = num(t.translateX);
        if ('translateY' in t) out.translateY = num(t.translateY);
      }
      if (typeof v === 'string') {
        const m = v.match(/translate\(\s*([-\d.]+)px?\s*,\s*([-\d.]+)/);
        if (m) { out.translateX = +m[1]; out.translateY = +m[2]; }
      }
      return;
    }
    case 'fontWeight':
      out.fontWeight = typeof v === 'number' ? v : WEIGHTS[v] || parseFloat(v) || 400;
      return;
    case 'fontFamily':
      // two families: Inter ('sans-serif') and JetBrains Mono ('monospace', or a name containing "mono")
      out.fontFamily = /mono|courier|consol/i.test(String(v)) ? 'monospace' : 'sans-serif';
      return;
    default:
      if (COLOR_KEYS.has(k)) out[k] = color(v);
      else if (DIM_KEYS.has(k)) out[k] = dim(v);
      else if (typeof v === 'string' && /^-?[\d.]+(px)?$/.test(v)) out[k] = parseFloat(v);
      else out[k] = v;
  }
}

export function flatten(style) {
  if (!style) return {};
  if (Array.isArray(style)) return Object.assign({}, ...style.map(flatten));
  return style;
}

export function normalize(style) {
  const s = flatten(style), out = {};
  for (const k in s) {
    const v = s[k];
    if (v === undefined || v === null || v === false) continue;
    expand(k, v, out);
  }
  return out;
}

const same = (a, b) => a === b || (Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((x, i) => x === b[i]));

// Sends the server only the properties that changed.
export function applyStyle(id, prev, next) {
  for (const k in prev) if (!(k in next)) lord.setStyle(id, k, undefined);
  for (const k in next) if (!same(prev[k], next[k])) lord.setStyle(id, k, next[k]);
}

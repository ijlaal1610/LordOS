// components.jsx — controles base para apps. Toman colores de useTheme(), así que se
// adaptan a cualquier tema.
import { View, Text, addListener } from '@lordos/runtime';
import { createContext, useContext, useEffect, useRef, useState } from 'react';
import { useTheme } from './theme.js';
import { getApp, windows } from './windows.js';
import { useStore } from './store.js';
import { textDirection } from './i18n.js';
import { ime, useIme, Preedit } from './ime.js';

export function Button({ label, onClick, primary, style }) {
  const t = useTheme();
  const [hover, setHover] = useState(false);
  return (
    <View onClick={onClick} onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)}
      style={[{ height: 28, paddingHorizontal: 14, borderRadius: 7, justifyContent: 'center', alignItems: 'center',
        backgroundColor: primary ? t.accent : t.control, opacity: hover ? 0.85 : 1 }, style]}>
      <Text style={{ fontSize: 13, fontWeight: 500, color: primary ? '#fff' : t.text }}>{label}</Text>
    </View>
  );
}

export function Toggle({ on, onChange }) {
  const t = useTheme();
  return (
    <View onClick={() => onChange(!on)} style={{ width: 42, height: 24, borderRadius: 12,
      backgroundColor: on ? t.accent : t.control }}>
      <View style={{ position: 'absolute', top: 2, left: on ? 20 : 2, width: 20, height: 20, borderRadius: 10,
        backgroundColor: '#fff', boxShadow: '0 1px 3px rgba(0,0,0,0.35)' }} />
    </View>
  );
}

export function Segmented({ options, value, onChange }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', backgroundColor: t.control, borderRadius: 8, padding: 2 }}>
      {options.map(([v, label]) => (
        <View key={v} onClick={() => onChange(v)} style={{ paddingHorizontal: 14, height: 26, borderRadius: 6,
          justifyContent: 'center', backgroundColor: value === v ? t.accent : 'transparent' }}>
          <Text style={{ fontSize: 12, fontWeight: 600, color: value === v ? '#fff' : t.text }}>{label}</Text>
        </View>
      ))}
    </View>
  );
}

export function SidebarItem({ label, selected, onClick, icon }) {
  const t = useTheme();
  const [hover, setHover] = useState(false);
  return (
    <View onClick={onClick} onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)}
      style={{ height: 28, paddingHorizontal: 10, borderRadius: 6, flexDirection: 'row', alignItems: 'center', gap: 8,
        backgroundColor: selected ? t.accent : hover ? t.hover : 'transparent' }}>
      {icon}
      <Text style={{ fontSize: 13, fontWeight: 500, color: selected ? '#fff' : t.text }}>{label}</Text>
    </View>
  );
}

export function SectionTitle({ children }) {
  const t = useTheme();
  return <Text style={{ fontSize: 11, fontWeight: 600, color: t.textDim, marginLeft: 10, marginTop: 10, marginBottom: 4 }}>{children}</Text>;
}

// Contenedor con desplazamiento por rueda del mouse y barra indicadora.
// scrollRef (opcional): { scrollTo(y), offset(), inner } para saltar a una posición (un índice).
export function ScrollView({ style, children, scrollRef }) {
  const t = useTheme();
  const outer = useRef(null), inner = useRef(null);
  const [offset, setOffset] = useState(0);
  const [metrics, setMetrics] = useState({ view: 0, content: 0 });
  const measure = () => {
    if (!outer.current || !inner.current) return null;
    const view = lord.getLayout(outer.current.id)[3], content = lord.getLayout(inner.current.id)[3];
    if (view !== metrics.view || content !== metrics.content) setMetrics({ view, content });
    return { view, content };
  };
  useEffect(() => { const id = setTimeout(measure, 50); return () => clearTimeout(id); });
  const max = Math.max(0, metrics.content - metrics.view);
  const y = Math.min(offset, max);
  const bar = metrics.content > 0 ? Math.max(24, (metrics.view * metrics.view) / metrics.content) : 0;
  if (scrollRef) scrollRef.current = { scrollTo: (v) => { measure(); setOffset(Math.max(0, v)); }, offset: () => y, inner };
  return (
    <View ref={outer} onWheel={(e) => {
      const m = measure() || metrics;
      setOffset((o) => Math.max(0, Math.min(Math.max(0, m.content - m.view), Math.min(o, max) + e.deltaY)));
    }} style={[{ overflow: 'hidden', flex: 1 }, style]}>
      <View ref={inner} style={{ translateY: -y }}>{children}</View>
      {max > 0 && (
        <View style={{ position: 'absolute', right: 3, width: 5, borderRadius: 3, height: bar,
          top: (metrics.view - bar) * (y / max), backgroundColor: t.mode === 'dark' ? 'rgba(255,255,255,0.3)' : 'rgba(0,0,0,0.3)' }} />
      )}
    </View>
  );
}

// ---- foco de teclado ----

let focused = null;          // token del TextInput activo
let focusedNow = false;      // se enfocó durante el evento de mouse en curso
const handlers = new Map();
const focusSubs = new Set();
const setFocus = (tok) => { focused = tok; for (const fn of focusSubs) fn(); };

addListener('pointerdown', () => { if (!focusedNow) setFocus(null); focusedNow = false; });
addListener('keydown', (e) => { if (focused && handlers.has(focused)) handlers.get(focused)(e); });

export const hasTextFocus = () => focused !== null;

// Para un campo de texto propio (por ejemplo, una nota editada en el escritorio): mientras tenga
// el foco recibe las teclas, y useKeyboard/las apps no (como con un TextInput). Llamar desde el
// onMouseDown del campo; se suelta con un clic en otro lado o con releaseTextFocus.
export function takeTextFocus(token, handler, onChange) {
  handlers.set(token, handler);
  if (onChange) focusSubs.add(onChange);
  focusedNow = true;
  setFocus(token);
}
export function releaseTextFocus(token, onChange) {
  if (onChange) focusSubs.delete(onChange);
  handlers.delete(token);
  if (focused === token) setFocus(null);
}
export const textFocusOwner = () => focused;
// Para paneles que toman el teclado un momento (Spotlight, portapapeles): recordar el campo con
// foco, devolvérselo al cerrar y pegarle el portapapeles como si fuera Ctrl+V.
export const focusToken = () => focused;
export function restoreFocus(tok) { if (tok && handlers.has(tok)) setFocus(tok); }
export function pasteIntoFocused() {
  if (!focused || !handlers.has(focused)) return false;
  handlers.get(focused)({ ctrl: true, base: 'v', key: 'v', char: '' });
  return true;
}

// onKeyDown(e) opcional: devolver true si la tecla ya se manejó (ej.: Shift+Enter).
// secure: campo de contraseña (muestra •, no pasa por el método de entrada ni se puede copiar).
// selectAll: empieza con todo el texto seleccionado (la primera tecla lo reemplaza; las flechas
// lo deseleccionan), como al renombrar en el Finder.
export function TextInput({ value, onChange, placeholder, onSubmit, onCancel, onKeyDown, autoFocus, style, secure, size = 13, selectAll = false }) {
  const t = useTheme();
  const token = useRef({}).current;
  const [isFocused, setIsFocused] = useState(false);
  const [blink, setBlink] = useState(true);
  const latest = useRef();
  const caret = useRef(null);
  const secureRef = useRef(secure);
  secureRef.current = secure;
  const [allSel, setAllSel] = useState(selectAll);
  const allRef = useRef(selectAll);
  allRef.current = allSel;
  latest.current = { value, onChange, onSubmit, onCancel, onKeyDown };

  useEffect(() => {
    // con foco, las teclas pasan por el método de entrada (coreano, japonés, chino)
    const caretRect = () => { if (!caret.current) return null; const [x, y, , h] = lord.getLayout(caret.current.id); return { x, y, h }; };
    const sync = () => {
      setIsFocused(focused === token);
      if (focused === token && !secureRef.current) ime.attach(token, caretRect, { inline: true }); else ime.detach(token);
    };
    focusSubs.add(sync);
    handlers.set(token, (e) => {
      const p = latest.current;
      if (p.onKeyDown && p.onKeyDown(e)) { setBlink(true); return; }
      const all = allRef.current;
      if (all && (e.char || e.key === 'Backspace' || e.key === 'Delete' || (e.ctrl && e.base === 'v'))) { setAllSel(false); allRef.current = false; p.value = ''; }
      else if (all && /^Arrow|^Home$|^End$/.test(e.key || '')) { setAllSel(false); allRef.current = false; return; }
      if (e.ctrl && e.base === 'v') { p.onChange(p.value + lord.clipboard.get().replace(/\n/g, ' ')); return; }
      if (e.key === 'Backspace' || e.key === 'Delete') p.onChange(all ? '' : e.key === 'Delete' ? p.value : [...p.value].slice(0, -1).join(''));
      else if (e.key === 'Enter') p.onSubmit?.(p.value);
      else if (e.key === 'Escape') p.onCancel?.();
      else if (e.char) p.onChange(p.value + e.char);
      setBlink(true);
    });
    if (autoFocus) setFocus(token);
    return () => { focusSubs.delete(sync); handlers.delete(token); ime.detach(token); if (focused === token) setFocus(null); };
  }, []);
  const imeSt = useIme();
  const preedit = isFocused ? imeSt.preedit : '';

  useEffect(() => {
    if (!isFocused) return undefined;
    const id = setInterval(() => setBlink((b) => !b), 530);
    return () => clearInterval(id);
  }, [isFocused]);

  return (
    <View onMouseDown={() => { focusedNow = true; setFocus(token); }}
      style={[{ height: Math.max(30, size + 17), paddingHorizontal: 10, borderRadius: 7, flexDirection: 'row', alignItems: 'center',
        // el cursor va al final lógico: a la izquierda si el texto es árabe o hebreo
        direction: secure ? 'ltr' : value ? textDirection(value) : 'inherit',
        backgroundColor: t.inputBg, border: `1px solid ${isFocused ? t.accent : t.separator}`, overflow: 'hidden' }, style]}>
      {value ? <Text style={{ fontSize: size, color: allSel && isFocused ? '#fff' : t.text, whiteSpace: 'nowrap',
        backgroundColor: allSel && isFocused ? t.accent : 'transparent' }}>{secure ? '•'.repeat([...value].length) : value}</Text> : null}
      {preedit && !secure ? <Preedit st={imeSt} size={size} color={t.text} accent={t.accent} /> : null}
      {isFocused && <View ref={caret} style={{ width: 1.5, height: size + 3, marginLeft: 1, backgroundColor: blink ? t.accent : 'transparent' }} />}
      {!value && !preedit && <Text style={{ fontSize: size, color: t.textDim, whiteSpace: 'nowrap' }}>{placeholder || ''}</Text>}
    </View>
  );
}

// Número con botones − y + (ej.: tamaño de fuente).
export function Stepper({ value, onChange, min = -Infinity, max = Infinity, step = 1, format = (v) => String(v), width = 56 }) {
  const t = useTheme();
  const set = (v) => onChange(Math.max(min, Math.min(max, Math.round(v / step) * step)));
  const Btn = ({ label, d }) => (
    <View onClick={() => set(value + d)} style={{ width: 26, height: 26, alignItems: 'center', justifyContent: 'center' }}>
      <Text style={{ fontSize: 15, fontWeight: 600, color: t.text }}>{label}</Text>
    </View>
  );
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', backgroundColor: t.control, borderRadius: 7, height: 26 }}>
      <Btn label="−" d={-step} />
      <Text style={{ width, textAlign: 'center', fontSize: 12, fontWeight: 600, color: t.text }}>{format(value)}</Text>
      <Btn label="+" d={step} />
    </View>
  );
}

// Deslizador horizontal (arrastrar o hacer clic sobre la barra).
export function Slider({ value, onChange, min = 0, max = 1, width = 160 }) {
  const t = useTheme();
  const ref = useRef(null);
  const drag = useRef(false);
  const latest = useRef(onChange);
  latest.current = onChange;
  const at = (x) => {
    const [lx, , w] = lord.getLayout(ref.current.id);
    const f = Math.max(0, Math.min(1, (x - lx) / w));
    latest.current(min + f * (max - min));
  };
  useEffect(() => {
    const a = addListener('pointermove', (e) => { if (drag.current) at(e.x); });
    const b = addListener('pointerup', () => { drag.current = false; });
    return () => { a(); b(); };
  }, []);
  const f = (value - min) / (max - min || 1);
  return (
    <View ref={ref} onMouseDown={(e) => { drag.current = true; at(e.x); }} style={{ width, height: 20, justifyContent: 'center', direction: 'ltr' }}>
      <View style={{ height: 4, borderRadius: 2, backgroundColor: t.control }}>
        <View style={{ width: Math.max(0, f * width), height: 4, borderRadius: 2, backgroundColor: t.accent }} />
      </View>
      <View style={{ position: 'absolute', left: f * (width - 16), top: 2, width: 16, height: 16, borderRadius: 8,
        backgroundColor: '#fff', boxShadow: '0 1px 3px rgba(0,0,0,0.4)' }} />
    </View>
  );
}

// ---- contexto de la ventana de cada app ----

const AppWindowContext = createContext({ id: null, focused: false, args: null });
export const useAppWindow = () => useContext(AppWindowContext);

// Estado de una ventana que sobrevive a que la app se vuelva a montar (pasar a pantalla completa
// o a imagen en imagen la cambia de lugar en el árbol). Se borra al cerrar la ventana.
const winStates = new Map();          // id de ventana → Map(clave → valor)
windows.subscribe(() => {
  const ids = new Set(windows.get().map((w) => w.id));
  for (const k of [...winStates.keys()]) if (!ids.has(k)) winStates.delete(k);
});
export function useWindowState(key, initial) {
  const { id } = useAppWindow();
  if (!winStates.has(id)) winStates.set(id, new Map());
  const m = winStates.get(id);
  const [v, setV] = useState(() => (m.has(key) ? m.get(key) : typeof initial === 'function' ? initial() : initial));
  m.set(key, v);
  const set = useRef(null);
  if (!set.current) set.current = (x) => setV((prev) => { const n = typeof x === 'function' ? x(prev) : x; m.set(key, n); return n; });
  return [v, set.current];
}

// El tema usa <AppHost id focused /> dentro de su marco de ventana.
export function AppHost({ id, focused }) {
  const app = getApp(id);
  const args = useStore(windows, (ws) => ws.find((w) => w.id === id)?.args || null);
  const prot = useStore(windows, (ws) => !!ws.find((w) => w.id === id)?.protected);
  const mode = useStore(windows, (ws) => { const w = ws.find((x) => x.id === id); return w?.fullscreen ? 'fullscreen' : w?.pip ? 'pip' : 'window'; });
  if (!app) return null;
  const C = app.Component;
  // el contenido va en un nodo propio: `protected` lo pinta negro en las capturas (wm.setProtected)
  return (
    <AppWindowContext.Provider value={{ id, focused, args, mode }}>
      <View protected={prot} style={{ flex: 1, alignSelf: 'stretch' }}>
        <C />
      </View>
    </AppWindowContext.Provider>
  );
}

// Ventanas a pantalla completa e imagen en imagen (wm.setFullscreen / wm.setPip): encima del
// tema, sin su marco. Las pone startShell; el tema no tiene que hacer nada.
export function FloatingWindows() {
  const list = useStore(windows);
  const fs = list.filter((w) => w.fullscreen && !w.minimized);
  const pips = list.filter((w) => w.pip && !w.minimized && !w.fullscreen);
  const drag = useRef(null);
  useEffect(() => {
    const offMove = addListener('pointermove', (e) => {
      const d = drag.current;
      if (!d) return;
      const f = d.resize ? { ...d.frame, w: Math.max(160, d.frame.w + e.x - d.x), h: Math.max(90, d.frame.h + e.y - d.y) }
        : { ...d.frame, x: d.frame.x + e.x - d.x, y: d.frame.y + e.y - d.y };
      windows.set((ws) => ws.map((w) => (w.id === d.id ? { ...w, pip: f } : w)));
    });
    const offUp = addListener('pointerup', () => { drag.current = null; });
    const offKey = addListener('keydown', (e) => {
      if (e.key !== 'Escape' || hasTextFocus()) return;
      const top = windows.get().filter((w) => w.fullscreen).pop();
      if (top) windows.set((ws) => ws.map((w) => (w.id === top.id ? { ...w, fullscreen: false } : w)));
    });
    return () => { offMove(); offUp(); offKey(); };
  }, []);
  if (!fs.length && !pips.length) return null;
  return (
    <>
      {pips.map((w) => (
        <View key={w.id} style={{ position: 'absolute', left: w.pip.x, top: w.pip.y, width: w.pip.w, height: w.pip.h, borderRadius: 10,
          overflow: 'hidden', backgroundColor: '#000', boxShadow: '0 18px 48px rgba(0,0,0,0.55)', pointerEvents: 'auto' }}
          onMouseDown={(e) => { drag.current = { id: w.id, x: e.x, y: e.y, frame: w.pip }; }}>
          <AppHost id={w.id} focused={false} />
          <View onMouseDown={(e) => { e.stopPropagation?.(); drag.current = { id: w.id, x: e.x, y: e.y, frame: w.pip, resize: true }; }}
            style={{ position: 'absolute', right: 0, bottom: 0, width: 18, height: 18 }} />
        </View>
      ))}
      {fs.map((w) => (
        <View key={w.id} style={{ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0, backgroundColor: '#000' }}>
          <AppHost id={w.id} focused />
        </View>
      ))}
    </>
  );
}

// Teclado para la app cuya ventana está al frente (y sin un TextInput activo).
export function useKeyboard(handler) {
  const { focused: active } = useAppWindow();
  const ref = useRef(handler);
  ref.current = handler;
  useEffect(() => {
    if (!active) return undefined;
    return addListener('keydown', (e) => { if (!hasTextFocus()) ref.current(e); });
  }, [active]);
}

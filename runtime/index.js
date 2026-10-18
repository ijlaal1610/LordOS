// @lordos/runtime — renderer de React para lord-server (mismo modelo que React Native):
// los componentes <View>, <Text> e <Image> son nodos nativos dibujados por el servidor en C.
import Reconciler from 'react-reconciler';
import { ConcurrentRoot, DefaultEventPriority, NoEventPriority } from 'react-reconciler/constants';
import { createContext, useEffect, useState } from 'react';
import { normalize, applyStyle } from './style.js';
import { register, unregister, currentPriority, addListener } from './events.js';

export { addListener };
export { setLayout, getLayout, layouts, modifiers } from './keymap.js';
export const View = 'view';
export const Text = 'text';
export const Image = 'image';
// <Terminal session={id} />: grilla de una sesión de lord.term dibujada en C.
export const Terminal = 'terminal';
// <Surface surface={id} />: ventana de una app Linux (Wayland) que compone lord-server.
export const Surface = 'surface';
// <MediaView media={id} />: video, foto, captura o cámara de lord.media (docs/MEDIOS.md); lo dibuja C.
export const MediaView = 'media';
export const screen = { width: lord.screen.width, height: lord.screen.height };
// La pantalla puede cambiar de tamaño (sesión vista desde otro monitor o desde el navegador):
// lord-server actualiza lord.screen y llama a __lordScreen.
const screenListeners = new Set();
let rootNode = 0;
export function onScreenChange(fn) { screenListeners.add(fn); return () => screenListeners.delete(fn); }
globalThis.__lordScreen = () => {
  const old = { width: screen.width, height: screen.height };
  screen.width = lord.screen.width;
  screen.height = lord.screen.height;
  if (rootNode) { lord.setStyle(rootNode, 'width', screen.width); lord.setStyle(rootNode, 'height', screen.height); }
  for (const fn of screenListeners) { try { fn(screen, old); } catch (e) { console.error('pantalla:', e?.message || e); } }
};

function applyProps(inst, prev, next) {
  const style = normalize(next.style);
  applyStyle(inst.id, inst.style, style);
  inst.style = style;
  if (prev.src !== next.src) lord.setProp(inst.id, 'src', next.src ?? null);
  if (prev.session !== next.session) lord.setProp(inst.id, 'session', next.session ?? 0);
  if (prev.surface !== next.surface) lord.setProp(inst.id, 'surface', next.surface ?? 0);
  if (prev.media !== next.media) lord.setProp(inst.id, 'media', next.media ?? 0);
  // protected: el nodo (y lo de adentro) sale negro en capturas, grabaciones y escritorio remoto
  if (!!prev.protected !== !!next.protected) lord.setProp(inst.id, 'protected', !!next.protected);
  // <Image pixels={{ width, height, data }} />: lienzo RGBA (un objeto nuevo por cada cuadro)
  if (prev.pixels !== next.pixels && next.pixels) lord.setProp(inst.id, 'pixels', next.pixels);
}

function attach(parent, child) {
  child.parent = parent.isContainer ? null : parent;
}

// Prioridad de la actualización en curso (React 19 la fija y la consulta por acá).
let updatePriority = NoEventPriority;

// Interfaz del host de react-reconciler 0.34 (React 19), modo mutación como React Native.
const host = {
  rendererPackageName: '@lordos/runtime',
  rendererVersion: '0.7.0',
  supportsMutation: true,
  supportsPersistence: false,
  supportsHydration: false,
  isPrimaryRenderer: true,
  noTimeout: -1,
  scheduleTimeout: (fn, ms) => setTimeout(fn, ms),
  cancelTimeout: (id) => clearTimeout(id),
  supportsMicrotasks: true,
  scheduleMicrotask: (fn) => queueMicrotask(fn),

  createInstance(type, props) {
    const inst = { id: lord.createNode(type), type, props, style: {}, parent: null };
    register(inst);
    applyProps(inst, {}, props);
    return inst;
  },
  createTextInstance(text) {
    const inst = { id: lord.createNode('raw'), type: 'raw', props: {}, style: {}, parent: null };
    lord.setText(inst.id, text);
    return inst;
  },
  appendInitialChild(parent, child) { lord.appendChild(parent.id, child.id); attach(parent, child); },
  appendChild(parent, child) { lord.appendChild(parent.id, child.id); attach(parent, child); },
  appendChildToContainer(container, child) { lord.appendChild(container.id, child.id); attach(container, child); },
  insertBefore(parent, child, before) { lord.insertBefore(parent.id, child.id, before.id); attach(parent, child); },
  insertInContainerBefore(container, child, before) { lord.insertBefore(container.id, child.id, before.id); attach(container, child); },
  removeChild(parent, child) { lord.removeChild(parent.id, child.id); },
  removeChildFromContainer(container, child) { lord.removeChild(container.id, child.id); },
  detachDeletedInstance(inst) { unregister(inst); lord.destroy(inst.id); },
  clearContainer() {},

  finalizeInitialChildren: () => false,
  // React 19: sin prepareUpdate; commitUpdate recibe el tipo y las props vieja y nueva
  commitUpdate(inst, _type, prev, next) { applyProps(inst, prev, next); inst.props = next; },
  commitTextUpdate(inst, _old, text) { lord.setText(inst.id, text); },
  resetTextContent() {},
  shouldSetTextContent: () => false,
  hideInstance(inst) { lord.setStyle(inst.id, 'display', 'none'); },
  unhideInstance(inst) { lord.setStyle(inst.id, 'display', inst.style.display ?? 'flex'); },
  hideTextInstance(inst) { lord.setText(inst.id, ''); },
  unhideTextInstance(inst, text) { lord.setText(inst.id, text); },

  getRootHostContext: () => ({}),
  getChildHostContext: (ctx) => ctx,
  getPublicInstance: (inst) => inst,
  prepareForCommit: () => null,
  resetAfterCommit() {},
  preparePortalMount() {},
  setCurrentUpdatePriority(p) { updatePriority = p; },
  getCurrentUpdatePriority: () => updatePriority,
  resolveUpdatePriority: () => updatePriority || currentPriority() || DefaultEventPriority,
  resolveEventType: () => null,
  resolveEventTimeStamp: () => -1.1,
  trackSchedulerEvent() {},
  shouldAttemptEagerTransition: () => false,
  requestPostPaintCallback() {},
  // nada suspende el commit (no hay hojas de estilo ni imágenes que esperar: se cargan en C)
  maySuspendCommit: () => false,
  maySuspendCommitOnUpdate: () => false,
  maySuspendCommitInSyncRender: () => false,
  preloadInstance: () => true,
  startSuspendingCommit: () => null,
  suspendInstance() {},
  suspendOnActiveViewTransition() {},
  waitForCommitToBeReady: () => null,
  getSuspendedCommitReason: () => null,
  // formularios y transiciones de <form> (no hay DOM)
  NotPendingTransition: null,
  HostTransitionContext: createContext(null),
  resetFormInstance() {},
  // refs de Fragment: un objeto vacío (no se usan)
  createFragmentInstance: () => ({}),
  updateFragmentInstanceFiber() {},
  commitNewChildToFragmentInstance() {},
  deleteChildFromFragmentInstance() {},
  getInstanceFromNode: () => null,
  beforeActiveInstanceBlur() {},
  afterActiveInstanceBlur() {},
  prepareScopeUpdate() {},
  getInstanceFromScope: () => null,
};

const reconciler = Reconciler(host);

export function render(element) {
  const rootId = lord.createNode('view');
  rootNode = rootId;
  lord.setStyle(rootId, 'width', screen.width);
  lord.setStyle(rootId, 'height', screen.height);
  lord.setRoot(rootId);
  const container = { id: rootId, isContainer: true };
  const report = (kind) => (e) => console.error(`react (${kind}):`, e?.message ? `${e.message}\n${e.stack || ''}` : e);
  // React 19: errores sin capturar, capturados por un boundary y recuperables, por separado
  const root = reconciler.createContainer(container, ConcurrentRoot, null, false, null, 'lord',
    report('error'), report('capturado'), report('recuperable'), null);
  reconciler.updateContainer(element, root, null, null);
  return root;
}

// ---- SDK mínimo para temas (crece en M3) ----

// Reloj formateado: React solo re-renderiza cuando cambia el texto (ej. una vez por minuto).
export function useClock(format = (d) => d.toISOString(), intervalMs = 1000) {
  const [text, setText] = useState(() => format(new Date()));
  useEffect(() => {
    const id = setInterval(() => setText(format(new Date())), intervalMs);
    return () => clearInterval(id);
  }, [intervalMs]);
  return text;
}

export function useStats(intervalMs = 2000) {
  const [stats, setStats] = useState(() => lord.stats());
  useEffect(() => {
    const id = setInterval(() => setStats(lord.stats()), intervalMs);
    return () => clearInterval(id);
  }, [intervalMs]);
  return stats;
}

export const system = { version: lord.version };

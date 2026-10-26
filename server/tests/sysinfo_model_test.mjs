// sysinfo_model_test.mjs — System Information's model (apps/sysinfo/model.js, 0.8.5): the GPU layers,
// the reasons for CPU composition, the Compatibility panel and the Hardware sections. Run by
// tests/run_suites.sh (needs node).
import { compositionReason, cardLayers, sessionLayers, compatibility, hardwareSections, throttleFlags, machineName } from '../../apps/sysinfo/model.js';

let n = 0, bad = 0;
const eq = (what, got, want) => {
  n++;
  const g = JSON.stringify(got), w = JSON.stringify(want);
  if (g !== w) { bad++; console.log(`✗ ${what}\n    got  ${g}\n    want ${w}`); }
};
const states = (rows) => Object.fromEntries(rows.map((r) => [r.id, r.state]));
const notes = (rows) => Object.fromEntries(rows.map((r) => [r.id, r.note]));

// ---- why the CPU composes ----
eq('gpu: no reason', compositionReason({ composition: 'gpu', gpuInit: 'on' }), null);
eq('session reasons win', ['headless', 'no-display', 'rotated', 'capture'].map((c) => compositionReason({ composition: c, gpuInit: 'on' })),
  ['headless', 'no-display', 'rotated', 'capture']);
eq('gpu-off: the init outcome', compositionReason({ composition: 'gpu-off', gpuInit: 'software' }), 'software');
eq('nothing known', compositionReason({}), 'unknown');

// ---- layers ----
const amd = { card: 'card0', bus: 'pci', vendorId: '1002', deviceId: '67df', vendor: 'AMD', device: 'RX 580', driver: 'amdgpu', renderNode: true, connectors: 2, connected: 1 };
eq('a display card', states(cardLayers(amd)), { device: 'yes', driver: 'yes', kms: 'yes', render: 'yes' });
eq('device name', cardLayers(amd)[0].vars.name, 'AMD RX 580');
const v3d = { card: 'card0', bus: 'platform', driver: 'v3d', renderNode: true, connectors: 0, connected: 0 };
eq('a render-only card (Pi 5 v3d)', [states(cardLayers(v3d)).kms, cardLayers(v3d)[2].note, cardLayers(v3d)[0].vars.name], ['no', 'renderOnly', '?:?']);
eq('no driver', states(cardLayers({ ...amd, driver: '' })).driver, 'no');

const on = { composition: 'gpu', gpuInit: 'on', renderer: 'AMD Radeon RX 580', renderNode: '/dev/dri/renderD128', dmabuf: true, scanouts: 12 };
eq('session on the GPU', states(sessionLayers(on)), { egl: 'yes', composition: 'yes', scanout: 'yes', dmabuf: 'yes', decode: 'unchecked', encode: 'unchecked', vulkan: 'unchecked' });
eq('scanout notes', [sessionLayers(on)[2].note, sessionLayers({ ...on, scanouts: 0 })[2].note], ['scanoutSeen', 'scanoutNone']);
eq('forced llvmpipe is marked software', sessionLayers({ ...on, renderer: 'llvmpipe (LLVM 19.1.7, 256 bits)' })[0].note, 'software');
const soft = { composition: 'gpu-off', gpuInit: 'software', renderer: 'llvmpipe (LLVM 19.1.7, 256 bits)', dmabuf: false, scanouts: 0 };
eq('software refused', [states(sessionLayers(soft)).egl, sessionLayers(soft)[0].note, sessionLayers(soft)[1].note], ['no', 'softwareRefused', 'software']);
eq('headless', notes(sessionLayers({ composition: 'headless', gpuInit: '' })).composition, 'headless');

// ---- compatibility ----
const vm = {
  machine: { vendor: 'QEMU', product: 'Standard PC', virtualization: 'kvm' },
  gpus: [{ card: 'card0', driver: 'virtio-pci', connectors: 1, connected: 1 }],
  displays: [{ connector: 'Virtual-1', nativeWidth: 1280, nativeHeight: 800 }],
  storage: [{ name: 'sda', bytes: 34e9 }],
  network: [{ kind: 'ethernet', driver: 'virtio_net', up: true }],
  audio: ['HDA Intel'], bluetooth: [], power: [],
  software: { landlockAbi: 6, bpfLsm: true, cgroup2: true },
  session: { composition: 'gpu-off', gpuInit: 'no-3d' },
};
const c = compatibility(vm);
eq('a VM', states(c), { gpu: 'limits', display: 'works', storage: 'works', network: 'works', wifi: 'absent', bluetooth: 'absent', audio: 'works', power: 'absent', isolation: 'works', suspend: 'unchecked' });
eq('the GPU note names the reason', notes(c).gpu, 'cpu.no-3d');
eq('counts', [c.find((r) => r.id === 'display').vars.count, c.find((r) => r.id === 'storage').vars.count], [1, 1]);

const laptop = {
  ...vm,
  gpus: [{ card: 'card0', driver: 'i915', connectors: 1, connected: 1, renderNode: true }],
  displays: [{ connector: 'eDP-1', nativeWidth: 0, mode: '1920x1200' }],
  network: [{ kind: 'wifi', driver: 'iwlwifi', up: false }, { kind: 'ethernet', driver: 'r8152', up: false }],
  bluetooth: [{ driver: 'btusb' }],
  power: [{ kind: 'ac', online: 0 }, { kind: 'battery', capacity: 76, health: 64 }],
  software: { landlockAbi: 0, bpfLsm: false, cgroup2: true },
  session: { composition: 'gpu', renderer: 'Mesa Intel(R) Xe Graphics' },
};
const l = compatibility(laptop);
eq('a laptop', states(l), { gpu: 'works', display: 'limits', storage: 'works', network: 'limits', wifi: 'works', bluetooth: 'works', audio: 'works', power: 'limits', isolation: 'limits', suspend: 'unchecked' });
eq('laptop notes', [notes(l).display, notes(l).network, notes(l).power, notes(l).isolation], ['noEdid', 'notConnected', 'batteryWorn', 'missing.landlock']);
eq('gpu note carries the renderer', l[0].vars.renderer, 'Mesa Intel(R) Xe Graphics');
eq('healthy battery', compatibility({ ...laptop, power: [{ kind: 'battery', health: 92 }] }).find((r) => r.id === 'power'), { id: 'power', state: 'works', note: 'batteryHealth', vars: { health: 92 } });
eq('Landlock unknown (a fixture) with the rest present', compatibility({ ...vm, software: { landlockAbi: null, bpfLsm: true, cgroup2: true } }).find((r) => r.id === 'isolation').state, 'works');
eq('a Wi-Fi card without a driver', compatibility({ ...vm, network: [{ kind: 'wifi', driver: '', up: false }] }).find((r) => r.id === 'wifi').note, 'noDriver');
eq('headless: displays not checked', compatibility({ ...vm, displays: [], session: { composition: 'headless' } }).find((r) => r.id === 'display'),
  { id: 'display', state: 'unchecked', note: 'headless', vars: {} });
eq('no GPU at all', notes(compatibility({ ...vm, gpus: [] })).gpu, 'noGpu');
eq('an empty report does not throw', compatibility({}).length, 10);

// ---- sections, Pi flags, names ----
eq('sections of a VM', hardwareSections({ ...vm, pci: [{}], usb: [] }), ['computer', 'cpu', 'memory', 'graphics', 'displays', 'storage', 'network', 'audio', 'pci', 'software']);
eq('a Pi adds its section', hardwareSections({ raspberryPi: { model: 'Raspberry Pi 5' } }).includes('raspberryPi'), true);
eq('throttle 0x50000', throttleFlags(0x50000), { underVoltageNow: false, cappedNow: false, throttledNow: false, softLimitNow: false,
  underVoltage: true, capped: false, throttled: true, softLimit: false });
eq('throttle unknown', [throttleFlags(null), throttleFlags(-1)], [null, null]);
eq('names', [machineName(vm), machineName({ machine: { model: 'Raspberry Pi 5 Model B Rev 1.0' } }), machineName({})], ['QEMU Standard PC', 'Raspberry Pi 5 Model B Rev 1.0', '']);

console.log(`sysinfo-model-test: ${n - bad} ok, ${bad} failed`);
process.exit(bad ? 1 : 0);

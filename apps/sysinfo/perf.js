// LordBench's model (0.8.6, docs/BENCHMARK.md "0.8.6 — LordBench"): the measurements by category, how a
// result compares with the reference machines (×N of each, never one score), the three verdicts
// (performance, compatibility, stability), and what changed against the previous result. Pure
// functions over the result JSON lord.sysinfo.bench writes (server/src/bench/lbrun.c); tested in
// server/tests/sysinfo_perf_test.mjs.
import { REFERENCES } from './references.js';

export const CATEGORIES = [
  { id: 'cpu', ids: ['cpu.sha256.1', 'cpu.sha256.n', 'cpu.matmul.1', 'cpu.matmul.n', 'cpu.xz.1', 'cpu.xz.n', 'cpu.js.1', 'cpu.js.n'] },
  { id: 'memory', ids: ['mem.copy', 'mem.triad', 'mem.latency'] },
  { id: 'gpu', ids: ['gpu.scene', 'gpu.fill', 'gpu.upload', 'gpu.layers'] },
  { id: 'storage', ids: ['disk.seqwrite', 'disk.seqread', 'disk.random4k'] },
  { id: 'sustained', ids: ['sustained.start', 'sustained.end'] },
];

export { REFERENCES };

// ×N of the reference for one measurement (latency: the reference's time over ours); null if either
// is missing
export function ratio(res, ref, id) {
  const a = res && res.results && res.results[id], b = ref && ref.results && ref.results[id];
  if (!a || !b || !(a.value > 0) || !(b.value > 0)) return null;
  return a.lowerBetter ? b.value / a.value : a.value / b.value;
}

// per category, the geometric mean of the ratios it has; null when none
export function categoryRatios(res, ref) {
  const out = {};
  for (const c of CATEGORIES) {
    const rs = c.ids.map((id) => ratio(res, ref, id)).filter((x) => x != null);
    out[c.id] = rs.length ? Math.exp(rs.reduce((s, x) => s + Math.log(x), 0) / rs.length) : null;
  }
  return out;
}

// the references a result can be compared with: the same LordBench version
export function comparableReferences(res) {
  return REFERENCES.filter((r) => res && r.lordbench === res.lordbench);
}

// ---- verdicts ----

const throttled = (c) => c && c.rpiThrottled != null && (c.rpiThrottled & 0x7) !== 0;

export function stability(res) {
  if (!res) return { state: 'unknown', notes: [] };
  const notes = [];
  let state = 'pass';
  const warn = (id, vars = {}) => { notes.push({ id, vars }); if (state === 'pass') state = 'warn'; };
  if (res.status === 'failed') { state = 'fail'; notes.push({ id: 'failed', vars: { why: res.stopReason || '' } }); }
  else if (res.status === 'stopped') warn(res.stopReason === 'thermal' ? 'stoppedThermal' : res.stopReason === 'throttled' ? 'stoppedThrottled' : 'stopped');
  const failed = Object.entries(res.skipped || {}).filter(([, why]) => why === 'failed').map(([id]) => id);
  if (failed.length) warn('measureFailed', { count: failed.length });
  const b = res.before || {}, a = res.after || {};
  if (b.throttleEvents != null && a.throttleEvents != null && a.throttleEvents > b.throttleEvents) warn('throttleSeen', { count: a.throttleEvents - b.throttleEvents });
  if (throttled(a)) warn('piThrottled');
  if (a.rpiThrottled != null && (a.rpiThrottled & 1)) warn('underVoltage');
  const s = res.results || {};
  if (s['sustained.start'] && s['sustained.end']) {
    const drop = 1 - s['sustained.end'].value / s['sustained.start'].value;
    if (drop > 0.1) warn('sustainedDrop', { pct: Math.round(drop * 100) });
  }
  if (res.sustained && res.sustained.throttleAtS != null) warn('sustainedThrottle', { s: res.sustained.throttleAtS, temp: res.sustained.throttleTempC });
  if (res.quiet && !res.quiet.quiet) warn('notQuiet', { pct: Math.round(res.quiet.busyPct) });
  return { state, notes };
}

// the Compatibility panel's rows (model.js compatibility()), counted by state
export function compatibilityVerdict(rows) {
  const count = { works: 0, limits: 0, no: 0, absent: 0, unchecked: 0 };
  for (const r of rows || []) if (count[r.state] != null) count[r.state]++;
  const state = count.no ? 'fail' : count.limits ? 'warn' : rows && rows.length ? 'pass' : 'unknown';
  return { state, count };
}

// ---- comparing two results ----

// conditions that change the numbers; listed next to a comparison when they differ
export function conditionDiffs(a, b) {
  if (!a || !b) return [];
  const pick = (r) => ({
    power: r.before && r.before.power, governor: r.before && r.before.governor, profile: r.before && r.before.platformProfile,
    gpuKind: r.gpuKind, storage: r.storage && r.storage.kind, threads: r.threads, live: r.live, lordos: r.lordos, kernel: r.kernel,
    composition: r.session && r.session.composition,
  });
  const x = pick(a), y = pick(b);
  return Object.keys(x).filter((k) => x[k] !== y[k] && (x[k] != null || y[k] != null)).map((k) => ({ id: k, now: x[k], before: y[k] }));
}

// change against the previous result, per measurement (+ is better), only with the same LordBench
// version and mode
export function changes(now, prev) {
  if (!now || !prev || now.lordbench !== prev.lordbench || now.mode !== prev.mode) return null;
  const out = {};
  for (const id of Object.keys(now.results || {})) {
    const r = ratio(now, prev, id);
    if (r != null) out[id] = r - 1;
  }
  return out;
}

// the sustained test's samples as [seconds, GFLOP/s, °C or null]
export function sustainedSeries(res) {
  return (res && res.sustained && Array.isArray(res.sustained.samples) ? res.sustained.samples : [])
    .filter((s) => Array.isArray(s) && s.length === 3 && s[1] >= 0);
}

// the refresh budget of the slowest active display, ms (for the compositor's frame times)
export function refreshBudget(res) {
  const ds = (res && res.session && res.session.displays) || [];
  const hz = ds.map((d) => (d.mode && d.mode.refresh ? d.mode.refresh / 1000 : 0)).filter((x) => x > 0);
  return hz.length ? 1000 / Math.min(...hz) : null;
}

export function fileName(at, taken) {
  const d = new Date((at || 0) * 1000);
  const p = (n) => String(n).padStart(2, '0');
  const base = `lordbench-${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`;
  for (let i = 1; i < 100; i++) {
    const name = i === 1 ? `${base}.json` : `${base}-${i}.json`;
    if (!taken(name)) return name;
  }
  return null;
}

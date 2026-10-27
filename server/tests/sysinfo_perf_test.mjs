// sysinfo_perf_test.mjs — LordBench's model (apps/sysinfo/perf.js, 0.8.6): ratios against the reference
// machines, category means, the stability and compatibility verdicts, comparisons with the previous
// result and the conditions that differ. Run by tests/run_suites.sh (needs node).
import { CATEGORIES, REFERENCES, ratio, categoryRatios, comparableReferences, stability, compatibilityVerdict, conditionDiffs, changes, sustainedSeries, refreshBudget, fileName } from '../../apps/sysinfo/perf.js';

let n = 0, bad = 0;
const eq = (what, got, want) => {
  n++;
  const g = JSON.stringify(got), w = JSON.stringify(want);
  if (g !== w) { bad++; console.log(`✗ ${what}\n    got  ${g}\n    want ${w}`); }
};
const near = (what, got, want) => eq(what, got != null && Math.abs(got - want) < 1e-9, true);

const res = (over = {}) => ({
  lordbench: 1, mode: 'full', status: 'done', stopReason: '', threads: 4, live: false, gpuKind: 'hardware', lordos: '0.8.6', kernel: 'k',
  quiet: { quiet: true, busyPct: 2 }, before: { power: 'mains', governor: 'schedutil' }, after: { power: 'mains' }, storage: { kind: 'nvme' },
  session: { composition: 'gpu', displays: [{ mode: { w: 1920, h: 1080, refresh: 60000 } }, { mode: { w: 2560, h: 1440, refresh: 144000 } }] },
  results: { 'cpu.sha256.1': { value: 200 }, 'cpu.matmul.1': { value: 8 }, 'mem.latency': { value: 45, lowerBetter: true },
    'sustained.start': { value: 50 }, 'sustained.end': { value: 49 } },
  skipped: {}, ...over,
});
const ref = { lordbench: 1, results: { 'cpu.sha256.1': { value: 100 }, 'cpu.matmul.1': { value: 2 }, 'mem.latency': { value: 90 } } };

// every measurement lord-bench reports is in exactly one category
const ids = CATEGORIES.flatMap((c) => c.ids);
eq('20 measurements, no repeats', [ids.length, new Set(ids).size], [20, 20]);
eq('references have every measurement', REFERENCES.every((r) => ids.every((id) => r.results[id] && r.results[id].value > 0)), true);

near('×2 SHA', ratio(res(), ref, 'cpu.sha256.1'), 2);
near('latency: lower is better', ratio(res(), ref, 'mem.latency'), 2);
eq('missing on one side', ratio(res(), ref, 'gpu.scene'), null);
const cr = categoryRatios(res(), ref);
near('CPU: geometric mean of ×2 and ×4', cr.cpu, Math.sqrt(8));
eq('no GPU numbers: null', cr.gpu, null);
eq('only the same LordBench version', comparableReferences({ lordbench: 99 }).length, 0);

// stability
eq('clean run passes', stability(res()), { state: 'pass', notes: [] });
eq('failed', stability(res({ status: 'failed', stopReason: 'helper' })).state, 'fail');
eq('thermal stop', stability(res({ status: 'stopped', stopReason: 'thermal' })).notes[0].id, 'stoppedThermal');
eq('throttle counters grew', stability(res({ before: { throttleEvents: 2 }, after: { throttleEvents: 5 } })).notes, [{ id: 'throttleSeen', vars: { count: 3 } }]);
eq('Pi throttling and under-voltage', stability(res({ after: { rpiThrottled: 0x50005 } })).notes.map((x) => x.id), ['piThrottled', 'underVoltage']);
eq('Pi: only past events', stability(res({ after: { rpiThrottled: 0x50000 } })).state, 'pass');
eq('sustained dropped 20 %', stability(res({ results: { 'sustained.start': { value: 50 }, 'sustained.end': { value: 40 } } })).notes, [{ id: 'sustainedDrop', vars: { pct: 20 } }]);
eq('not quiet', stability(res({ quiet: { quiet: false, busyPct: 37.4 } })).notes, [{ id: 'notQuiet', vars: { pct: 37 } }]);
eq('a measurement failed', stability(res({ skipped: { 'gpu.scene': 'failed', 'disk.seqread': 'mode' } })).notes, [{ id: 'measureFailed', vars: { count: 1 } }]);

eq('compatibility: limits warn', compatibilityVerdict([{ state: 'works' }, { state: 'limits' }, { state: 'absent' }]),
  { state: 'warn', count: { works: 1, limits: 1, no: 0, absent: 1, unchecked: 0 } });
eq('compatibility: nothing', compatibilityVerdict([]).state, 'unknown');

// comparisons
eq('same conditions', conditionDiffs(res(), res()), []);
eq('battery and governor differ', conditionDiffs(res({ before: { power: 'battery', governor: 'powersave' } }), res()),
  [{ id: 'power', now: 'battery', before: 'mains' }, { id: 'governor', now: 'powersave', before: 'schedutil' }]);
const ch = changes(res({ results: { 'cpu.sha256.1': { value: 220 }, 'mem.latency': { value: 50, lowerBetter: true } } }), res());
near('+10 %', ch['cpu.sha256.1'], 0.1);
near('latency got worse', ch['mem.latency'], 45 / 50 - 1);
eq('another mode: no comparison', changes(res({ mode: 'quick' }), res()), null);
eq('another LordBench: no comparison', changes(res({ lordbench: 2 }), res()), null);

eq('samples, bad ones dropped', sustainedSeries({ sustained: { samples: [[10, 19.8, null], [20, -1, 50], 'x', [30, 19.7, 61.5]] } }), [[10, 19.8, null], [30, 19.7, 61.5]]);
near('the slowest display sets the budget', refreshBudget(res()), 1000 / 60);
eq('no displays', refreshBudget(res({ session: {} })), null);
const at = Date.UTC(2026, 9, 27, 12) / 1000;
eq('file name', fileName(at, () => false), 'lordbench-2026-10-27.json');
eq('file name taken', fileName(at, (x) => x === 'lordbench-2026-10-27.json'), 'lordbench-2026-10-27-2.json');

console.log(`sysinfo-perf-test: ${n - bad} ok, ${bad} failed`);
process.exit(bad ? 1 : 0);

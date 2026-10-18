// fwrule_input_test.mjs — the Settings / Monitor rule sheet's parser (sdk/fwrule.js).
//
//   node tests/fwrule_input_test.mjs        (from server/; run_suites.sh runs it when node exists)
//
// 1. Every destination in tests/fw_dest_cases.txt gives what the C store gives (build/fwdest-test
//    checks the same file against fw_parse_dest + fw_format_dest).
// 2. Ports, protocols, actions, the app default, and the precedence order (rules.c "consider").
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const { parseDest, parsePort, checkRule, sortByPrecedence, specificity, isDefaultRule } = await import(path.join(here, '../../sdk/fwrule.js'));

let n = 0, bad = 0;
const eq = (name, got, want) => {
  n++;
  const g = JSON.stringify(got), w = JSON.stringify(want);
  if (g !== w) { bad++; console.log(`✗ ${name}: want ${w}, got ${g}`); }
};

// 1. the shared destination cases
for (const line of fs.readFileSync(path.join(here, 'fw_dest_cases.txt'), 'utf8').split('\n')) {
  if (!line || line[0] === '#') continue;
  const [input, want] = line.split('\t');
  const d = parseDest(input);
  eq(`dest ${input}`, d.ok ? d.dst : '!', want);
}

// kinds and errors the sheet explains
eq('kind exact', parseDest('example.com').kind, 'domain');
eq('kind wildcard', parseDest('*.example.com').kind, 'wildcard');
eq('kind address', parseDest('192.0.2.7').kind, 'address');
eq('kind network', parseDest('10.0.0.0/8').kind, 'network');
eq('kind v6 network', parseDest('2001:db8::/32').kind, 'network');
eq('err empty', parseDest('  ').error, 'empty');
eq('err wildcard', parseDest('*example.com').error, 'wildcard');
eq('err hostBits', parseDest('10.0.0.1/8').error, 'hostBits');
eq('err prefix', parseDest('10.0.0.0/33').error, 'prefix');
eq('err address', parseDest('256.1.1.1').error, 'address');
eq('err domain', parseDest('exa mple.com').error, 'domain');
eq('trimmed', parseDest('  Example.com  ').dst, 'example.com');

// 2. ports
eq('port empty', parsePort(''), { ok: true, port: '' });
eq('port one', parsePort('443').port, '443');
eq('port range', parsePort('8000-8080').port, '8000-8080');
eq('port range spaces', parsePort('8000 - 8080').port, '8000-8080');
eq('port same', parsePort('53-53').port, '53');
for (const p of ['0', '65536', '080', '9-1', 'abc', '1-', '-1', '1-2-3', '+80']) eq(`port bad ${p}`, parsePort(p).ok, false);

// whole rules
eq('rule ok', checkRule({ dst: '*.Example.com', port: '', proto: '', action: 'deny' }),
  { ok: true, kind: 'wildcard', rule: { dst: '*.example.com', port: '', proto: '', action: 'deny', dir: 'out' } });
eq('rule bad dst', checkRule({ dst: 'x..y', action: 'allow' }).field, 'dst');
eq('rule bad port', checkRule({ dst: 'example.com', port: '70000', action: 'allow' }).field, 'port');
eq('rule bad proto', checkRule({ dst: 'example.com', proto: 'icmp', action: 'allow' }).field, 'proto');
eq('rule bad action', checkRule({ dst: 'example.com', action: 'maybe' }).field, 'action');
eq('rule default refused', checkRule({ dst: 'any', action: 'deny' }).error, 'isDefault');
eq('rule any with port', checkRule({ dst: 'any', port: '25', proto: 'tcp', action: 'deny' }).ok, true);
eq('isDefault', isDefaultRule({ dir: 'out', dst: 'any', port: '', proto: '' }), true);
eq('isDefault port', isDefaultRule({ dir: 'out', dst: 'any', port: '443', proto: '' }), false);

// precedence: exact > wildcard (more labels first) > network (longer prefix) > lan/internet > any;
// then the smaller port range, a concrete protocol, deny before allow, the newest (higher id)
eq('spec order', [specificity({ dst: 'a.example.com' }) > specificity({ dst: '*.a.example.com' }),
  specificity({ dst: '*.a.example.com' }) > specificity({ dst: '*.example.com' }),
  specificity({ dst: '*.com' }) > specificity({ dst: '192.0.2.7' }),
  specificity({ dst: '192.0.2.7' }) > specificity({ dst: '192.0.2.0/24' }),
  specificity({ dst: '0.0.0.0/0' }) > specificity({ dst: 'internet' }),
  specificity({ dst: 'lan' }) > specificity({ dst: 'any' })], [true, true, true, true, true, true]);
const rules = [
  { id: 1, dst: 'any', port: '', proto: '', action: 'deny' },
  { id: 2, dst: '*.example.com', port: '', proto: '', action: 'allow' },
  { id: 3, dst: '*.example.com', port: '443', proto: '', action: 'allow' },
  { id: 4, dst: 'api.example.com', port: '', proto: '', action: 'allow' },
  { id: 5, dst: '10.0.0.0/8', port: '', proto: '', action: 'deny' },
  { id: 6, dst: '10.1.0.0/16', port: '', proto: '', action: 'allow' },
  { id: 7, dst: '*.example.com', port: '443', proto: 'tcp', action: 'allow' },
  { id: 8, dst: '*.example.com', port: '443', proto: 'tcp', action: 'deny' },
  { id: 9, dst: '*.example.com', port: '443', proto: 'tcp', action: 'deny' },
];
eq('sorted', sortByPrecedence(rules).map((r) => r.id), [4, 9, 8, 7, 3, 2, 6, 5, 1]);

console.log(`fwrule-input-test: ${n} checks, ${bad} failed`);
process.exit(bad ? 1 : 0);

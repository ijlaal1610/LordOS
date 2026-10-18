// Backups — scheduled snapshots of the home folder to an external disk or a folder, like Time
// Machine (see docs/SYSTEM.md). The engine lives in lord-server (lord.backup): a lowest-priority
// thread that copies only what changed and hard-links the rest. This app picks the destination,
// schedule and exclusions, shows progress, and lets you browse backups by date and restore a
// file, a folder or everything.
import {
  View, Text, ScrollView, Segmented, Toggle, Button, TextInput, useTheme, useBackup, backup, fs, wm, t as tr, tOptional,
  formatBytes, formatDate, formatTime, formatNumber, direction,
} from '@lordos/sdk';
import { useEffect, useMemo, useState } from 'react';

// ---- icon: a clock with a returning arrow (drawn with View) ----
export function Icon({ size }) {
  const s = size, ring = s * 0.62, bw = Math.max(1.5, s * 0.06);
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, alignItems: 'center', justifyContent: 'center',
      background: 'linear-gradient(180deg, #2e8b57 0%, #1d5c3a 100%)', direction: 'ltr' }}>
      <View style={{ width: ring, height: ring, borderRadius: ring / 2, border: `${bw}px solid #fff`, alignItems: 'center', justifyContent: 'center' }}>
        <View style={{ position: 'absolute', left: ring / 2 - bw / 2 - bw, top: ring * 0.16, width: bw, height: ring * 0.36, backgroundColor: '#fff', borderRadius: bw / 2 }} />
        <View style={{ position: 'absolute', left: ring / 2 - bw, top: ring / 2 - bw * 1.5, width: ring * 0.3, height: bw, backgroundColor: '#fff', borderRadius: bw / 2 }} />
      </View>
      {/* the arrow's gap (top left) and its head */}
      <View style={{ position: 'absolute', left: s * 0.13, top: s * 0.17, width: s * 0.2, height: s * 0.2, backgroundColor: '#25744a', borderRadius: s * 0.04 }} />
      <View style={{ position: 'absolute', left: s * 0.16, top: s * 0.3, width: s * 0.17, height: bw * 1.4, backgroundColor: '#fff', borderRadius: bw }} />
      <View style={{ position: 'absolute', left: s * 0.26, top: s * 0.2, width: bw * 1.4, height: s * 0.13, backgroundColor: '#fff', borderRadius: bw }} />
      {/* disk at the bottom */}
      <View style={{ position: 'absolute', bottom: s * 0.08, width: s * 0.5, height: s * 0.07, borderRadius: s * 0.03, backgroundColor: 'rgba(255,255,255,0.55)' }} />
    </View>
  );
}

// ---- helpers ----
const when = (sec) => {
  if (!sec) return '';
  const d = new Date(sec * 1000), now = new Date();
  const same = d.toDateString() === now.toDateString();
  return same ? tr('backup.todayAt', { time: formatTime(d) }) : `${formatDate(d, 'short')} ${formatTime(d)}`;
};
// error codes from C (backup.c) → translated text; anything else is shown as is
const errText = (e) => (e ? tOptional(`backup.err.${e}`) || e : '');
const pct = (a, b) => (b > 0 ? Math.min(1, a / b) : 0);
const join = (a, b) => (a ? `${a}/${b}` : b);

function Bar({ value }) {
  const th = useTheme();
  return (
    <View style={{ height: 6, borderRadius: 3, backgroundColor: th.control, overflow: 'hidden', direction: 'ltr' }}>
      <View style={{ width: `${Math.round(value * 1000) / 10}%`, height: 6, borderRadius: 3, backgroundColor: th.accent }} />
    </View>
  );
}

function Sep({ vertical }) {
  const th = useTheme();
  return <View style={vertical ? { width: 1, alignSelf: 'stretch', backgroundColor: th.separator } : { height: 1, backgroundColor: th.separator }} />;
}

function Row({ children, style }) {
  return <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10, ...style }}>{children}</View>;
}

function Label({ children, dim, size = 13, weight = 400, style }) {
  const th = useTheme();
  return <Text style={{ fontSize: size, fontWeight: weight, color: dim ? th.textDim : th.text, textAlign: 'start', ...style }}>{children}</Text>;
}

function Card({ title, children }) {
  const th = useTheme();
  return (
    <View style={{ borderRadius: 10, border: `1px solid ${th.separator}`, padding: 14, gap: 10, backgroundColor: th.mode === 'dark' ? 'rgba(255,255,255,0.03)' : 'rgba(0,0,0,0.02)' }}>
      {title ? <Label weight={600}>{title}</Label> : null}
      {children}
    </View>
  );
}

function Confirm({ title, body, ok, onOk, onCancel, danger }) {
  const th = useTheme();
  return (
    <View style={{ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0, backgroundColor: 'rgba(0,0,0,0.35)', alignItems: 'center', justifyContent: 'center' }}>
      <View style={{ width: 420, padding: 18, gap: 12, borderRadius: 12, backgroundColor: th.windowBg, border: `1px solid ${th.separator}`, boxShadow: '0 12px 40px rgba(0,0,0,0.45)' }}>
        <Label size={15} weight={700}>{title}</Label>
        <Label dim style={{ lineHeight: 1.4 }}>{body}</Label>
        <Row style={{ justifyContent: 'flex-end' }}>
          <Button label={tr('backup.cancel')} onClick={onCancel} />
          <Button label={ok} primary onClick={onOk} style={danger ? { backgroundColor: '#d9443b' } : null} />
        </Row>
      </View>
    </View>
  );
}

// ---- header: status and buttons ----
function Header({ st }) {
  const th = useTheme();
  const [err, setErr] = useState('');
  const running = st && (st.state === 'running' || st.state === 'paused');
  const p = st?.progress || {};
  let line;
  if (!st) line = tr('backup.unavailable');
  else if (!st.dest) line = tr('backup.noDest');
  else if (st.state === 'running') line = tr('backup.running', { files: formatNumber(p.files || 0, 0), total: formatNumber(p.totalFiles || 0, 0) });
  else if (st.state === 'paused') line = st.battery ? tr('backup.pausedBattery') : tr('backup.paused');
  else if (st.state === 'restoring') line = tr('backup.restoring');
  else if (st.state === 'deleting') line = tr('backup.deleting');
  else if (st.state === 'locked') line = tr('backup.lockedLine');
  else if (st.state === 'unlocking') line = tr('backup.encWorking');
  else if (st.state === 'nodest' || !st.destAvailable) line = tr('backup.destMissing');
  else line = st.lastOk ? tr('backup.lastOk', { when: when(st.lastOk) }) : tr('backup.never');
  const next = st && st.next && !running && st.state !== 'restoring' ? tr('backup.next', { when: when(st.next) }) : '';
  const act = (fn) => { setErr(''); try { fn(); } catch (e) { setErr(String(e.message || e)); } };
  return (
    <View>
    <View style={{ padding: 18, paddingBottom: 12, gap: 10 }}>
      <Row style={{ gap: 14 }}>
        <Icon size={54} />
        <View style={{ flex: 1, gap: 3 }}>
          <Label size={18} weight={700}>{tr('backup.title')}</Label>
          <Label dim>{line}</Label>
          {next ? <Label dim size={12}>{next}</Label> : null}
          {st && st.last && st.last.ok === 0 && st.last.error && !running ? <Label size={12} style={{ color: '#e5534b' }}>{tr('backup.lastError', { error: st.last.error })}</Label> : null}
          {err ? <Label size={12} style={{ color: '#e5534b' }}>{err}</Label> : null}
        </View>
        {running ? (
          <Row>
            <Button label={st.userPause ? tr('backup.resume') : tr('backup.pause')} onClick={() => act(() => backup.pause(!st.userPause))} />
            <Button label={tr('backup.stop')} onClick={() => act(() => backup.cancel())} />
          </Row>
        ) : (
          <Button label={tr('backup.now')} primary onClick={() => act(() => backup.runNow())} />
        )}
      </Row>
      {running ? (
        <View style={{ gap: 5 }}>
          <Bar value={pct(p.bytes || 0, p.totalBytes || 0)} />
          <Row>
            <Label dim size={11} style={{ flex: 1, whiteSpace: 'nowrap', overflow: 'hidden' }}>{p.file || ''}</Label>
            <Label dim size={11}>{tr('backup.bytes', { done: formatBytes(p.bytes || 0), total: formatBytes(p.totalBytes || 0) })}</Label>
          </Row>
        </View>
      ) : null}
    </View>
    <Sep />
    </View>
  );
}

// ---- Settings ----
function Settings({ st }) {
  const th = useTheme();
  const [cfg, setCfg] = useState(() => backup.config() || { dest: '', interval: 'hourly', pauseOnBattery: true, exclude: [] });
  const [vols, setVols] = useState(() => { try { return fs.volumes(); } catch (e) { return []; } });
  const [folder, setFolder] = useState('');
  const [ex, setEx] = useState('');
  const [err, setErr] = useState('');
  const home = useMemo(() => { try { return fs.home(); } catch (e) { return ''; } }, []);
  const save = (c) => {
    setErr('');
    try { backup.configure(c); setCfg(backup.config() || c); } catch (e) { setErr(String(e.message || e)); }
  };
  const expand = (p) => (p.startsWith('~') ? home + p.slice(1) : p);
  const short = (p) => (home && p.startsWith(home) ? `~${p.slice(home.length)}` : p);
  useEffect(() => { const id = setInterval(() => { try { setVols(fs.volumes()); } catch (e) { /* no disks */ } }, 5000); return () => clearInterval(id); }, []);
  const removable = vols.filter((v) => v.path !== home && v.path !== '/');
  return (
    <ScrollView style={{ flex: 1 }}>
      <View style={{ padding: 18, gap: 14 }}>
        <Card title={tr('backup.dest')}>
          <Label dim size={12}>{tr('backup.destHelp')}</Label>
          {removable.map((v) => {
            const on = cfg.dest === v.path;
            return (
              <View key={v.path} onClick={() => save({ ...cfg, dest: v.path })}
                style={{ flexDirection: 'row', alignItems: 'center', gap: 10, padding: 8, borderRadius: 8, backgroundColor: on ? th.accent : th.control }}>
                <Text style={{ fontSize: 16, color: on ? '#fff' : th.text }}>⛁</Text>
                <View style={{ flex: 1 }}>
                  <Text style={{ fontSize: 13, fontWeight: 600, color: on ? '#fff' : th.text, textAlign: 'start' }}>{v.name || v.path}</Text>
                  <Text style={{ fontSize: 11, color: on ? 'rgba(255,255,255,0.8)' : th.textDim, textAlign: 'start' }}>
                    {tr('backup.volFree', { free: formatBytes(v.free || 0), total: formatBytes(v.total || 0), fs: v.fs || '' })}
                  </Text>
                </View>
                {['vfat', 'exfat', 'msdos', 'ntfs', 'ntfs3', 'fuseblk'].includes(v.fs) ? <Text style={{ fontSize: 11, color: on ? '#fff' : '#d29922' }}>{tr('backup.noLinks')}</Text> : null}
              </View>
            );
          })}
          {!removable.length ? <Label dim size={12}>{tr('backup.noDisks')}</Label> : null}
          <Row>
            <TextInput value={folder} onChange={setFolder} placeholder={tr('backup.folderPlaceholder')} style={{ flex: 1 }}
              onSubmit={() => folder && save({ ...cfg, dest: expand(folder.trim()) })} />
            <Button label={tr('backup.useFolder')} onClick={() => folder && save({ ...cfg, dest: expand(folder.trim()) })} />
          </Row>
          {cfg.dest ? (
            <Row>
              <Label size={12} style={{ flex: 1 }}>{tr('backup.current', { path: short(cfg.dest) })}{st && st.destTotal > 0 ? ` · ${tr('backup.free', { free: formatBytes(st.destFree), total: formatBytes(st.destTotal) })}` : ''}</Label>
              <Button label={tr('backup.stopUsing')} onClick={() => save({ ...cfg, dest: '' })} />
            </Row>
          ) : null}
        </Card>
        <Card title={tr('backup.schedule')}>
          <Segmented value={cfg.interval} onChange={(v) => save({ ...cfg, interval: v })}
            options={[['hourly', tr('backup.hourly')], ['daily', tr('backup.daily')], ['weekly', tr('backup.weekly')], ['manual', tr('backup.manual')]]} />
          <Label dim size={12}>{tr('backup.retention')}</Label>
          <Row>
            <Label style={{ flex: 1 }}>{tr('backup.battery')}</Label>
            <Toggle on={cfg.pauseOnBattery !== false} onChange={(v) => save({ ...cfg, pauseOnBattery: v })} />
          </Row>
          <Label dim size={12}>{tr('backup.priority')}</Label>
        </Card>
        <Card title={tr('backup.exclude')}>
          <Label dim size={12}>{tr('backup.excludeHelp')}</Label>
          {(cfg.exclude || []).map((x) => (
            <Row key={x}>
              <Label style={{ flex: 1 }}>~/{x}</Label>
              <Button label={tr('backup.remove')} onClick={() => save({ ...cfg, exclude: cfg.exclude.filter((y) => y !== x) })} />
            </Row>
          ))}
          <Row>
            <TextInput value={ex} onChange={setEx} placeholder={tr('backup.excludePlaceholder')} style={{ flex: 1 }}
              onSubmit={() => { const v = ex.trim().replace(/^~\/?/, '').replace(/\/+$/, ''); if (v) { save({ ...cfg, exclude: [...(cfg.exclude || []), v] }); setEx(''); } }} />
            <Button label={tr('backup.add')} onClick={() => { const v = ex.trim().replace(/^~\/?/, '').replace(/\/+$/, ''); if (v) { save({ ...cfg, exclude: [...(cfg.exclude || []), v] }); setEx(''); } }} />
          </Row>
        </Card>
        <Encryption st={st} cfg={cfg} save={save} />
        {err ? <Label size={12} style={{ color: '#e5534b' }}>{err}</Label> : null}
      </View>
    </ScrollView>
  );
}

// ---- Encryption (Monocypher: chunked XChaCha20-Poly1305, key from Argon2id) ----
function Encryption({ st, cfg, save }) {
  const th = useTheme();
  const [p1, setP1] = useState('');
  const [p2, setP2] = useState('');
  const [err, setErr] = useState('');
  const [wantOn, setWantOn] = useState(false);
  const destEnc = !!(st && st.destEncrypted);
  const locked = !!(st && st.locked);
  const busy = st && st.state === 'unlocking';
  const u = st?.unlock;
  useEffect(() => { if (u && u.done && u.ok) { setP1(''); setP2(''); setWantOn(false); } }, [u && u.done, u && u.ok]);
  const run = (fn) => { setErr(''); try { fn(); } catch (e) { setErr(String(e.message || e)); } };
  const turnOn = () => {
    if (p1.length < 8) { setErr(tr('backup.encShort')); return; }
    if (p1 !== p2) { setErr(tr('backup.encMismatch')); return; }
    run(() => { if (!cfg.encrypt) save({ ...cfg, encrypt: true }); backup.unlock(p1, true); });
  };
  const setup = !destEnc && (wantOn || cfg.encrypt);
  const msg = busy ? tr('backup.encWorking') : u && u.done && !u.ok ? errText(u.error) : err;
  return (
    <Card title={tr('backup.encryption')}>
      {!destEnc && !setup ? (
        <Row>
          <Label dim style={{ flex: 1 }}>{tr('backup.encOffHelp')}</Label>
          <Toggle on={false} onChange={() => (cfg.dest ? setWantOn(true) : setErr(tr('backup.noDest')))} />
        </Row>
      ) : null}
      {setup ? (
        <View style={{ gap: 8 }}>
          <Label dim size={12}>{tr('backup.encWarn')}</Label>
          <TextInput secure value={p1} onChange={setP1} placeholder={tr('backup.encPass')} />
          <TextInput secure value={p2} onChange={setP2} placeholder={tr('backup.encPass2')} onSubmit={turnOn} />
          <Row style={{ justifyContent: 'flex-end' }}>
            <Button label={tr('backup.encNo')} onClick={() => { setWantOn(false); setP1(''); setP2(''); if (cfg.encrypt) save({ ...cfg, encrypt: false }); }} />
            <Button label={tr('backup.encTurnOn')} primary onClick={turnOn} />
          </Row>
        </View>
      ) : null}
      {destEnc && locked ? (
        <View style={{ gap: 8 }}>
          <Label dim size={12}>{tr('backup.encLocked')}</Label>
          <Row>
            <TextInput secure value={p1} onChange={setP1} placeholder={tr('backup.encPass')} style={{ flex: 1 }} onSubmit={() => run(() => backup.unlock(p1, false))} />
            <Button label={tr('backup.encUnlock')} primary onClick={() => run(() => backup.unlock(p1, false))} />
          </Row>
        </View>
      ) : null}
      {destEnc && !locked ? (
        <Row>
          <Label style={{ flex: 1 }}>{tr('backup.encOn')}</Label>
          <Button label={tr('backup.encLock')} onClick={() => run(() => backup.lock())} />
        </Row>
      ) : null}
      {msg ? <Label size={12} style={{ color: busy ? th.textDim : '#e5534b' }}>{msg}</Label> : null}
    </Card>
  );
}

// ---- Backups: timeline and browser ----
function Browser({ st }) {
  const th = useTheme();
  const [snaps, setSnaps] = useState(() => backup.list());
  const [sel, setSel] = useState(null);
  const [rel, setRel] = useState('');
  const [items, setItems] = useState([]);
  const [pick, setPick] = useState(null);
  const [replace, setReplace] = useState(false);
  const [ask, setAsk] = useState(null);       // 'all' | 'delete'
  const [err, setErr] = useState('');
  const lastWhen = st?.last?.when, stateNow = st?.state;
  useEffect(() => { setSnaps(backup.list()); }, [lastWhen, stateNow]);
  const list = useMemo(() => [...snaps].reverse(), [snaps]);
  useEffect(() => { if (!sel && list.length) setSel(list[0].id); if (sel && !snaps.find((s) => s.id === sel)) setSel(list[0]?.id || null); }, [list]);
  useEffect(() => {
    setPick(null);
    if (!sel) { setItems([]); return; }
    try { setItems(backup.ls(sel, rel)); setErr(''); } catch (e) { setItems([]); setErr(String(e.message || e)); }
  }, [sel, rel]);
  const busy = st && st.state !== 'idle' && st.state !== 'nodest';
  const restore = (r) => { setErr(''); try { backup.restore(sel, r, { replace }); } catch (e) { setErr(String(e.message || e)); } };
  // grouped by day
  const groups = [];
  for (const s of list) {
    const d = new Date(s.time * 1000);
    const key = d.toDateString();
    if (!groups.length || groups[groups.length - 1].key !== key) groups.push({ key, date: d, items: [] });
    groups[groups.length - 1].items.push(s);
  }
  const crumbs = rel ? rel.split('/') : [];
  const rr = st?.restore;
  if (!list.length) {
    return (
      <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', gap: 8, padding: 30 }}>
        <Label size={15} weight={600}>{tr('backup.emptyTitle')}</Label>
        <Label dim style={{ textAlign: 'center' }}>{st?.dest ? tr('backup.emptyBody') : tr('backup.emptyNoDest')}</Label>
      </View>
    );
  }
  return (
    <View style={{ flex: 1, flexDirection: 'row' }}>
      <View style={{ width: 220, backgroundColor: th.sidebarBg }}>
        <ScrollView style={{ flex: 1 }}>
          <View style={{ padding: 10, gap: 2 }}>
            {groups.map((g) => (
              <View key={g.key} style={{ gap: 2 }}>
                <Text style={{ fontSize: 11, fontWeight: 600, color: th.textDim, marginTop: 8, marginBottom: 2, textAlign: 'start' }}>{formatDate(g.date, 'dayMonth')}</Text>
                {g.items.map((s) => (
                  <View key={s.id} onClick={() => { setSel(s.id); setRel(''); }}
                    style={{ height: 26, paddingHorizontal: 10, borderRadius: 6, justifyContent: 'center', backgroundColor: sel === s.id ? th.accent : 'transparent' }}>
                    <Text style={{ fontSize: 13, color: sel === s.id ? '#fff' : th.text, textAlign: 'start' }}>{formatTime(new Date(s.time * 1000))}</Text>
                  </View>
                ))}
              </View>
            ))}
          </View>
        </ScrollView>
        <Sep />
        <View style={{ padding: 10 }}>
          <Label dim size={11}>{tr('backup.count', { count: list.length })}</Label>
        </View>
      </View>
      <Sep vertical />
      <View style={{ flex: 1, minWidth: 0 }}>
        <Row style={{ paddingHorizontal: 14, height: 40, gap: 4 }}>
          <View onClick={() => setRel('')}><Text style={{ fontSize: 13, color: rel ? th.accent : th.text, fontWeight: 600 }}>{tr('backup.home')}</Text></View>
          {crumbs.map((c, i) => (
            <Row key={i} style={{ gap: 4 }}>
              <Text style={{ fontSize: 13, color: th.textDim }}>{direction() === 'rtl' ? '‹' : '›'}</Text>
              <View onClick={() => setRel(crumbs.slice(0, i + 1).join('/'))}>
                <Text style={{ fontSize: 13, color: i === crumbs.length - 1 ? th.text : th.accent }}>{c}</Text>
              </View>
            </Row>
          ))}
          <View style={{ flex: 1 }} />
          <Button label={tr('backup.openFiles')} onClick={() => { const p = backup.path(sel); if (p) wm.open('files', { new: true, args: { path: rel ? `${p}/${rel}` : p } }); }} />
        </Row>
        <Sep />
        <ScrollView style={{ flex: 1 }}>
          <View style={{ paddingVertical: 4 }}>
            {items.map((it) => {
              const on = pick === it.name;
              return (
                <View key={it.name} onClick={() => setPick(it.name)} onDoubleClick={() => it.dir && setRel(join(rel, it.name))}
                  style={{ flexDirection: 'row', alignItems: 'center', gap: 10, height: 28, paddingHorizontal: 14, backgroundColor: on ? th.accent : 'transparent' }}>
                  <Text style={{ width: 18, fontSize: 13, color: on ? '#fff' : it.dir ? '#5ea7ff' : th.textDim }}>{it.dir ? '▸' : it.link ? '↪' : '•'}</Text>
                  <Text style={{ flex: 1, fontSize: 13, color: on ? '#fff' : th.text, whiteSpace: 'nowrap', overflow: 'hidden', textAlign: 'start' }}>{it.name}</Text>
                  <Text style={{ width: 90, fontSize: 12, color: on ? '#fff' : th.textDim, textAlign: 'end' }}>{it.dir ? '' : formatBytes(it.size)}</Text>
                  <Text style={{ width: 150, fontSize: 12, color: on ? '#fff' : th.textDim, textAlign: 'end' }}>{when(it.mtime)}</Text>
                </View>
              );
            })}
            {!items.length ? <Label dim style={{ padding: 14 }}>{err || tr('backup.emptyFolder')}</Label> : null}
          </View>
        </ScrollView>
        <Sep />
        <View style={{ padding: 12, gap: 8 }}>
          {rr && rr.done && rr.id === sel ? (
            <Label size={12} dim>{rr.ok ? tr('backup.restoredInfo', { count: rr.files, both: rr.keptBoth, replaced: rr.replaced })
              : tr('backup.restoreFailed', { error: errText(rr.error) || tr('backup.restoreErrors', { count: rr.errors }) })}</Label>
          ) : null}
          {err && items.length ? <Label size={12} style={{ color: '#e5534b' }}>{err}</Label> : null}
          <Row>
            <Row style={{ flex: 1 }}>
              <Toggle on={replace} onChange={setReplace} />
              <Label size={12}>{tr('backup.replace')}</Label>
            </Row>
            <Button label={tr('backup.delete')} onClick={() => !busy && setAsk('delete')} />
            <Button label={tr('backup.restoreAll')} onClick={() => !busy && setAsk('all')} />
            <Button label={pick ? tr('backup.restoreItem', { name: pick }) : rel ? tr('backup.restoreFolder') : tr('backup.restorePick')} primary
              onClick={() => !busy && (pick ? restore(join(rel, pick)) : rel ? restore(rel) : null)} />
          </Row>
        </View>
      </View>
      {ask === 'all' ? (
        <Confirm title={tr('backup.restoreAllTitle')} ok={tr('backup.restoreAll')}
          body={replace ? tr('backup.restoreAllReplace', { when: when(snaps.find((s) => s.id === sel)?.time) }) : tr('backup.restoreAllKeep', { when: when(snaps.find((s) => s.id === sel)?.time) })}
          onCancel={() => setAsk(null)} onOk={() => { setAsk(null); restore(''); }} />
      ) : null}
      {ask === 'delete' ? (
        <Confirm danger title={tr('backup.deleteTitle')} ok={tr('backup.delete')} body={tr('backup.deleteBody', { when: when(snaps.find((s) => s.id === sel)?.time) })}
          onCancel={() => setAsk(null)} onOk={() => { setAsk(null); try { backup.remove(sel); } catch (e) { setErr(String(e.message || e)); } }} />
      ) : null}
    </View>
  );
}

export default function Backup() {
  const st = useBackup();
  const [tab, setTab] = useState(() => (st && st.dest ? 'snapshots' : 'settings'));
  return (
    <View style={{ flex: 1 }}>
      <Header st={st} />
      <Row style={{ paddingHorizontal: 18, paddingVertical: 10, justifyContent: 'center' }}>
        <Segmented value={tab} onChange={setTab} options={[['snapshots', tr('backup.tabSnapshots')], ['settings', tr('backup.tabSettings')]]} />
      </Row>
      {tab === 'snapshots' ? <Browser st={st} /> : <Settings st={st} />}
    </View>
  );
}

// Ajustes → Impresoras y escáneres. CUPS administra la cola y los controladores; esta pantalla
// le habla IPP por su socket local con el cliente propio de lord-server (lord.print). Agregar,
// quitar y elegir la predeterminada los autoriza la política de CUPS (grupo lpadmin, que tienen
// los administradores). Las impresoras IPP Everywhere / AirPrint y los escáneres eSCL de la red
// se encuentran por mDNS. Ver docs/IMPRESION.md.
import { View, Text, Button, TextInput, useTheme, wm, print, queueName, t as tr } from '@lordos/sdk';
import { useEffect, useState } from 'react';

const Divider = () => <View style={{ height: 1, backgroundColor: useTheme().separator }} />;

function errText(e) {
  if (e.code === 'forbidden') return tr('print.err.forbidden');
  if (e.code === 'no-cups') return tr('print.noCups');
  if (e.code === 'not-found') return tr('print.err.notFound');
  return tr('print.err.other', { error: e.detail || e.code || e.message });
}

function stateText(p) {
  if (p.state === 5) return tr('print.state.stopped');
  if (p.state === 4) return tr('print.state.printing');
  if (!p.accepting) return tr('print.state.rejecting');
  return tr('print.state.idle');
}

const JOB_STATE = { 3: 'pending', 4: 'held', 5: 'processing', 6: 'stopped', 7: 'canceled', 8: 'aborted', 9: 'completed' };

function PrinterGlyph({ size = 34, color = '#8e8e93' }) {
  return (
    <View style={{ width: size, height: size, alignItems: 'center', justifyContent: 'center' }}>
      <View style={{ position: 'absolute', top: size * 0.08, width: size * 0.5, height: size * 0.3, backgroundColor: '#ffffff', border: '1px solid rgba(0,0,0,0.25)' }} />
      <View style={{ position: 'absolute', top: size * 0.32, width: size * 0.86, height: size * 0.38, borderRadius: size * 0.08, backgroundColor: color }} />
      <View style={{ position: 'absolute', top: size * 0.6, width: size * 0.56, height: size * 0.3, backgroundColor: '#ffffff', border: '1px solid rgba(0,0,0,0.25)' }} />
    </View>
  );
}

export function PrintersSettings() {
  const th = useTheme();
  const [data, setData] = useState(null);           // { printers, default }
  const [jobs, setJobs] = useState([]);
  const [sel, setSel] = useState(null);
  const [error, setError] = useState(null);
  const [found, setFound] = useState(null);         // { printers, scanners }
  const [searching, setSearching] = useState(false);
  const [busy, setBusy] = useState(null);
  const [manual, setManual] = useState(null);       // { name, uri, info, location }
  const [msg, setMsg] = useState(null);
  const cups = print.hasCups();

  const reload = () => print.printers().then((r) => { setData(r); setError(null); if (!sel && r.printers[0]) setSel(r.default || r.printers[0].name); }).catch((e) => { setData({ printers: [], default: '' }); setError(errText(e)); });
  const reloadJobs = () => print.jobs().then((r) => setJobs(r.jobs)).catch(() => {});
  const discover = () => { setSearching(true); print.discover().then(setFound).catch(() => setFound({ printers: [], scanners: [] })).finally(() => setSearching(false)); };
  useEffect(() => { if (cups) { reload(); reloadJobs(); } discover(); }, []);
  useEffect(() => { if (!cups) return undefined; const id = setInterval(() => { reloadJobs(); reload(); }, 3000); return () => clearInterval(id); }, [sel]);
  useEffect(() => { if (!msg) return undefined; const id = setTimeout(() => setMsg(null), 5000); return () => clearTimeout(id); }, [msg]);

  const act = (key, p, ok) => { setBusy(key); p.then(() => { setMsg(ok); reload(); }).catch((e) => setMsg(errText(e))).finally(() => setBusy(null)); };
  const addFound = (f) => {
    const name = queueName(f.name);
    act(`add:${f.uri}`, print.add(name, f.uri, f.name, f.note || ''), tr('print.added', { name: f.name }));
  };
  const known = (uri) => data?.printers.some((p) => p.uri === uri);
  const printers = data?.printers || [];
  const cur = printers.find((p) => p.name === sel);
  const curJobs = jobs.filter((j) => !sel || j.printer === sel);

  if (!cups) {
    return (
      <View style={{ gap: 10 }}>
        <Text style={{ fontSize: 13, color: th.textDim, lineHeight: 18 }}>{tr('print.noCupsLong')}</Text>
      </View>
    );
  }
  return (
    <View style={{ gap: 12 }}>
      {error ? <Text style={{ fontSize: 12, color: '#ff453a' }}>{error}</Text> : null}
      {/* impresoras de CUPS */}
      <View style={{ borderRadius: 10, backgroundColor: th.control, padding: 4 }}>
        {data == null ? <Text style={{ margin: 10, fontSize: 12, color: th.textDim }}>{tr('print.loading')}</Text>
          : !printers.length ? <Text style={{ margin: 10, fontSize: 12, color: th.textDim }}>{tr('print.noPrinters')}</Text>
            : printers.map((p) => (
              <View key={p.name} onClick={() => setSel(p.name)} style={{ flexDirection: 'row', alignItems: 'center', gap: 10, padding: 8, borderRadius: 8,
                backgroundColor: sel === p.name ? (th.mode === 'dark' ? 'rgba(255,255,255,0.1)' : 'rgba(0,0,0,0.06)') : 'transparent' }}>
                <PrinterGlyph color={p.state === 5 ? '#ff453a' : p.state === 4 ? '#30d158' : '#8e8e93'} />
                <View style={{ flex: 1 }}>
                  <View style={{ flexDirection: 'row', gap: 8, alignItems: 'center' }}>
                    <Text style={{ fontSize: 13, fontWeight: 600, color: th.text }}>{p.info || p.name}</Text>
                    {data.default === p.name ? <View style={{ paddingHorizontal: 6, height: 16, borderRadius: 8, justifyContent: 'center', backgroundColor: th.accent }}>
                      <Text style={{ fontSize: 10, color: '#fff' }}>{tr('print.defaultBadge')}</Text></View> : null}
                  </View>
                  <Text style={{ fontSize: 11, color: th.textDim }}>{[stateText(p), p.model, p.location].filter(Boolean).join(' · ')}</Text>
                  {p.message ? <Text style={{ fontSize: 11, color: th.textDim }}>{p.message}</Text> : null}
                </View>
                {p.queued ? <Text style={{ fontSize: 11, color: th.textDim }}>{tr('print.queued', { count: p.queued })}</Text> : null}
              </View>
            ))}
      </View>
      {cur && (
        <View style={{ flexDirection: 'row', gap: 8, flexWrap: 'wrap' }}>
          <Button label={tr('print.setDefault')} onClick={() => act('default', print.setDefault(cur.name), tr('print.defaultSet', { name: cur.info || cur.name }))} />
          <Button label={tr('print.testPage')} onClick={() => print.open({ title: tr('print.testTitle'), text: tr('print.testText', { name: cur.info || cur.name, model: cur.model || '' }) })} />
          <Button label={busy === 'remove' ? tr('print.removing') : tr('print.remove')} onClick={() => act('remove', print.remove(cur.name), tr('print.removed', { name: cur.info || cur.name }))} />
        </View>
      )}
      {/* cola */}
      {cur && (
        <View style={{ gap: 6 }}>
          <Text style={{ fontSize: 13, fontWeight: 700, color: th.text }}>{tr('print.queueOf', { name: cur.info || cur.name })}</Text>
          {!curJobs.length ? <Text style={{ fontSize: 12, color: th.textDim }}>{tr('print.queueEmpty')}</Text> : curJobs.map((j) => (
            <View key={j.id} style={{ flexDirection: 'row', alignItems: 'center', gap: 10, paddingVertical: 4 }}>
              <Text style={{ width: 40, fontSize: 11, color: th.textDim }}>{`#${j.id}`}</Text>
              <Text style={{ flex: 1, fontSize: 12, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{j.name || tr('print.untitled')}</Text>
              <Text style={{ fontSize: 11, color: th.textDim }}>{`${j.user} · ${tr(`print.job.${JOB_STATE[j.state] || 'pending'}`)}${j.kb ? ` · ${j.kb} KB` : ''}`}</Text>
              <Button label={tr('print.cancelJob')} onClick={() => act(`c${j.id}`, print.cancel(j.id).then(reloadJobs), tr('print.jobCanceled', { id: j.id }))} />
            </View>
          ))}
        </View>
      )}
      <Divider />
      {/* agregar */}
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
        <Text style={{ flex: 1, fontSize: 13, fontWeight: 700, color: th.text }}>{tr('print.addTitle')}</Text>
        <Button label={searching ? tr('print.searching') : tr('print.searchAgain')} onClick={discover} />
        <Button label={tr('print.manual')} onClick={() => setManual(manual ? null : { name: '', uri: 'ipp://', info: '', location: '' })} />
      </View>
      <Text style={{ fontSize: 11, color: th.textDim, lineHeight: 16 }}>{tr('print.addHint')}</Text>
      {found?.printers.length ? found.printers.map((f) => (
        <View key={f.uri} style={{ flexDirection: 'row', alignItems: 'center', gap: 10, paddingVertical: 4 }}>
          <PrinterGlyph size={26} />
          <View style={{ flex: 1 }}>
            <Text style={{ fontSize: 12, fontWeight: 600, color: th.text }}>{f.name}</Text>
            <Text style={{ fontSize: 11, color: th.textDim }}>{[f.model, f.uri, f.color === 'T' ? tr('print.color') : null, f.duplex === 'T' ? tr('print.duplex') : null].filter(Boolean).join(' · ')}</Text>
          </View>
          {known(f.uri) ? <Text style={{ fontSize: 11, color: th.textDim }}>{tr('print.alreadyAdded')}</Text>
            : <Button primary label={busy === `add:${f.uri}` ? tr('print.adding') : tr('print.add')} onClick={() => addFound(f)} />}
        </View>
      )) : found ? <Text style={{ fontSize: 12, color: th.textDim }}>{tr('print.noneFound')}</Text> : null}
      {manual && (
        <View style={{ gap: 8, padding: 10, borderRadius: 10, backgroundColor: th.control }}>
          <TextInput value={manual.uri} onChange={(v) => setManual({ ...manual, uri: v })} placeholder="ipp://192.168.1.20/ipp/print" style={{ height: 26 }} />
          <TextInput value={manual.info} onChange={(v) => setManual({ ...manual, info: v })} placeholder={tr('print.fieldName')} style={{ height: 26 }} />
          <TextInput value={manual.location} onChange={(v) => setManual({ ...manual, location: v })} placeholder={tr('print.fieldLocation')} style={{ height: 26 }} />
          <View style={{ flexDirection: 'row', justifyContent: 'flex-end', gap: 8 }}>
            <Button label={tr('print.cancel')} onClick={() => setManual(null)} />
            <Button primary label={busy === 'manual' ? tr('print.adding') : tr('print.add')} onClick={() => {
              const info = manual.info.trim() || manual.uri.replace(/^\w+:\/\//, '');
              act('manual', print.add(queueName(info), manual.uri.trim(), info, manual.location.trim()), tr('print.added', { name: info }));
              setManual(null);
            }} />
          </View>
        </View>
      )}
      <Divider />
      {/* escáneres */}
      <Text style={{ fontSize: 13, fontWeight: 700, color: th.text }}>{tr('print.scanners')}</Text>
      {found?.scanners.length ? found.scanners.map((s) => (
        <View key={`${s.addr}:${s.port}`} style={{ flexDirection: 'row', alignItems: 'center', gap: 10, paddingVertical: 4 }}>
          <Text style={{ fontSize: 18 }}>⌸</Text>
          <View style={{ flex: 1 }}>
            <Text style={{ fontSize: 12, fontWeight: 600, color: th.text }}>{s.name}</Text>
            <Text style={{ fontSize: 11, color: th.textDim }}>{[s.model, `${s.addr}:${s.port}`, s.tls ? tr('print.tlsUnsupported') : null].filter(Boolean).join(' · ')}</Text>
          </View>
          {!s.tls && s.lan ? <Button primary label={tr('print.scan')} onClick={() => wm.open('scanner', { args: { scanner: s } })} /> : null}
        </View>
      )) : found ? <Text style={{ fontSize: 12, color: th.textDim }}>{tr('print.noScanners')}</Text> : <Text style={{ fontSize: 12, color: th.textDim }}>{tr('print.searching')}</Text>}
      {msg ? <Text style={{ fontSize: 12, color: th.accent }}>{msg}</Text> : null}
    </View>
  );
}

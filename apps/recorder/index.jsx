// Grabadora — notas de voz (diseño propio, al estilo de las grabadoras de escritorio).
//
// Graba el micrófono por PipeWire: el stream es de lord-server (después del permiso) y
// lord-media escribe el WAV. Lista de grabaciones en ~/Música/Grabaciones, con forma de onda,
// reproducir, renombrar, recortar y exportar a Opus, MP3 o WAV (lord-media, con FFmpeg).
import {
  View, Text, Image, ScrollView, Button, TextInput, Segmented, useTheme, useKeyboard,
  t as tr, media, usePlayer, userDirs, fs, mediaFormatTime, formatDate, formatTime, addListener, useSound, microphones, defaultSource,
} from '@lordos/sdk';
import { useEffect, useMemo, useRef, useState } from 'react';

const APP = 'recorder';
const AUDIO = /\.(wav|opus|ogg|mp3|m4a|flac)$/i;

export function Icon({ size: s }) {
  const bars = [0.18, 0.34, 0.52, 0.3, 0.6, 0.42, 0.24, 0.12];
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(160deg, #2b2b30, #0e0e10)',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)`, overflow: 'hidden' }}>
      {/* forma de onda roja sobre negro */}
      {bars.map((h, i) => (
        <View key={i} style={{ position: 'absolute', left: s * (0.16 + i * 0.09), top: s * (0.5 - h / 2), width: s * 0.05, height: s * h,
          borderRadius: s * 0.025, backgroundColor: i === 4 ? '#fff' : '#ff453a' }} />
      ))}
    </View>
  );
}

function dir() {
  const d = `${userDirs().music}/${tr('recorder.folder')}`;
  try { fs.mkdir(userDirs().music); } catch (e) { /* ya está */ }
  try { fs.mkdir(d); } catch (e) { /* ya está */ }
  return d;
}
function freeName(base, ext) {
  for (let n = 1; n < 1000; n++) {
    const p = `${dir()}/${base}${n > 1 ? ` ${n}` : ''}.${ext}`;
    try { fs.stat(p); } catch (e) { return p; }
  }
  return `${dir()}/${base} ${Date.now()}.${ext}`;
}
const baseName = (p) => String(p).split('/').pop();
const stem = (p) => baseName(p).replace(/\.[^.]+$/, '');

// Forma de onda en un lienzo RGBA (más liviano que cientos de cajas).
function Wave({ peaks, width, height, color, played = 0, sel }) {
  const px = useMemo(() => {
    const w = Math.max(1, Math.round(width)), h = Math.max(1, Math.round(height));
    const data = new Uint8Array(w * h * 4);
    const c = [parseInt(color.slice(1, 3), 16), parseInt(color.slice(3, 5), 16), parseInt(color.slice(5, 7), 16)];
    const n = peaks.length || 1;
    const top = Math.max(50, ...peaks);                 // se dibuja relativo al pico (como las grabadoras)
    for (let x = 0; x < w; x++) {
      const v = peaks.length ? peaks[Math.min(n - 1, Math.floor((x / w) * n))] / top : 0;
      const bh = Math.max(1, Math.round(v * (h - 2)));
      const y0 = Math.round((h - bh) / 2);
      const f = x / w;
      const inSel = !sel || (f >= sel[0] && f <= sel[1]);
      const a = (x % 3 === 2) ? 0 : f < played ? 255 : inSel ? 170 : 60;
      for (let y = y0; y < y0 + bh; y++) { const i = (y * w + x) * 4; data[i] = c[0]; data[i + 1] = c[1]; data[i + 2] = c[2]; data[i + 3] = a; }
    }
    return { width: w, height: h, data };
  }, [peaks, width, height, color, played, sel && sel[0], sel && sel[1]]);
  return <Image pixels={px} style={{ width, height }} />;
}

export default function Recorder() {
  const th = useTheme();
  const snd = useSound();
  const mics = microphones(snd);
  const [source, setSource] = useState('');
  const [rec, setRec] = useState(0);
  const [paused, setPaused] = useState(false);
  const [levels, setLevels] = useState([]);
  const [list, setList] = useState([]);
  const [sel, setSel] = useState(null);
  const [wave, setWave] = useState(null);
  const [playId, setPlayId] = useState(0);
  const [trim, setTrim] = useState(null);           // [desde, hasta] 0..1
  const [renaming, setRenaming] = useState(null);
  const [format, setFormat] = useState('opus');
  const [msg, setMsg] = useState(null);
  const rp = usePlayer(rec);
  const pp = usePlayer(playId);
  const waveBox = useRef(null);
  const drag = useRef(null);

  const reload = () => {
    let l = [];
    try { l = fs.list(dir()).filter((e) => !e.dir && AUDIO.test(e.name)).map((e) => ({ path: `${dir()}/${e.name}`, name: e.name, mtime: e.mtime, size: e.size })); } catch (e) { /* sin carpeta */ }
    setList(l.sort((a, b) => b.mtime - a.mtime));
  };
  useEffect(reload, []);
  useEffect(() => { if (rp.state === 'recording') setLevels((l) => [...l.slice(-180), rp.level]); }, [rp.ms]);
  useEffect(() => {
    if (rp.state === 'done' || rp.closed) { media.close(rec); setRec(0); setPaused(false); reload(); if (rp.done?.path) setSel(rp.done.path); }
  }, [rp.state, rp.closed]);
  useEffect(() => () => { media.close(rec); media.close(playId); }, []);

  // forma de onda de la elegida
  useEffect(() => {
    setWave(null); setTrim(null);
    media.close(playId); setPlayId(0);
    if (!sel) return;
    media.waveform(sel, 400).then((w) => setWave(w));
  }, [sel]);

  const start = async () => {
    if (rec) { media.stop(rec); return; }
    const path = freeName(tr('recorder.name', { date: formatDate(new Date()), time: formatTime(new Date()).replace(/:/g, '.') }), 'wav');
    const id = await media.audioRecord(APP, path, { source, appName: tr('apps.recorder') });
    if (!id) { setMsg(tr('recorder.denied')); setTimeout(() => setMsg(null), 3000); return; }
    setLevels([]);
    setRec(id);
  };
  const pause = () => { media.control(rec, paused ? 'play' : 'pause'); setPaused(!paused); };
  const play = () => {
    if (playId) { media.control(playId, pp.playing ? 'pause' : 'play'); return; }
    const id = media.play(APP, sel, { music: true, appName: tr('apps.recorder') });
    setPlayId(id || 0);
    if (trim && wave) setTimeout(() => media.control(id, 'seek', trim[0] * wave.duration), 200);
  };
  useEffect(() => { if (pp.state === 'ended') { media.close(playId); setPlayId(0); } }, [pp.state]);
  useEffect(() => { if (trim && wave && playId && pp.pos > trim[1] * wave.duration) media.control(playId, 'pause'); }, [pp.pos]);

  useEffect(() => {
    const offMove = addListener('pointermove', (e) => {
      if (!drag.current || !waveBox.current) return;
      const [lx, , w] = lord.getLayout(waveBox.current.id);
      const f = Math.max(0, Math.min(1, (e.x - lx) / w));
      setTrim((t) => { const n = [...(t || [0, 1])]; n[drag.current.edge] = f; if (n[0] > n[1]) n.reverse(); return n; });
    });
    const offUp = addListener('pointerup', () => { drag.current = null; });
    return () => { offMove(); offUp(); };
  }, []);

  const exportAs = async (fmt, trimmed) => {
    const suffix = trimmed ? ` (${tr('recorder.trimmed')})` : '';
    const out = freeName(`${stem(sel)}${suffix}`, fmt === 'opus' ? 'opus' : fmt);
    const o = { format: fmt };
    if (trimmed && trim && wave) { o.from = Math.round(trim[0] * wave.duration); o.to = Math.round(trim[1] * wave.duration); }
    setMsg(tr('recorder.working'));
    const r = await media.transcode(APP, sel, out, o);
    setMsg(r.ok ? tr('recorder.exported', { name: baseName(r.path) }) : tr('recorder.exportError'));
    setTimeout(() => setMsg(null), 3500);
    if (r.ok) { reload(); if (trimmed) { setSel(r.path); } }
  };
  const doRename = () => {
    const name = (renaming || '').trim();
    if (name && sel) {
      const ext = baseName(sel).split('.').pop();
      const to = `${dir()}/${name.replace(/\//g, '-')}.${ext}`;
      try { fs.rename(sel, to); setSel(to); } catch (e) { setMsg(tr('recorder.renameError')); setTimeout(() => setMsg(null), 3000); }
      reload();
    }
    setRenaming(null);
  };
  useKeyboard((e) => {
    if (renaming !== null) return;
    if (e.key === ' ' && sel && !rec) play();
    if ((e.key === 'r' || e.key === 'R') && !e.ctrl && !e.meta) start();
  });

  const cur = list.find((x) => x.path === sel);
  const W = 520;
  return (
    <View style={{ flex: 1, flexDirection: 'row' }}>
      <View style={{ width: 240, backgroundColor: th.sidebarBg }}>
        <View style={{ height: 40, justifyContent: 'center', paddingHorizontal: 14 }}>
          <Text style={{ fontSize: 15, fontWeight: 700, color: th.text }}>{tr('recorder.all')}</Text>
        </View>
        <ScrollView style={{ flex: 1 }}>
          {!list.length && <Text style={{ fontSize: 12, color: th.textDim, padding: 14 }}>{tr('recorder.empty')}</Text>}
          {list.map((x) => (
            <View key={x.path} onClick={() => setSel(x.path)} style={{ paddingHorizontal: 14, paddingVertical: 8, gap: 2,
              backgroundColor: x.path === sel ? th.accent : 'transparent' }}>
              <Text style={{ fontSize: 13, fontWeight: 600, color: x.path === sel ? '#fff' : th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{stem(x.path)}</Text>
              <Text style={{ fontSize: 11, color: x.path === sel ? 'rgba(255,255,255,0.8)' : th.textDim }}>
                {`${formatDate(new Date(x.mtime))} · ${baseName(x.path).split('.').pop().toUpperCase()}`}
              </Text>
            </View>
          ))}
        </ScrollView>
        <View style={{ padding: 12, gap: 8, alignItems: 'center' }}>
          {rec ? (
            <View style={{ alignSelf: 'stretch', height: 50, flexDirection: 'row', alignItems: 'center', gap: 1, direction: 'ltr' }}>
              {levels.map((l, i) => <View key={i} style={{ width: 1, height: Math.max(1, Math.min(48, Math.sqrt(l) * 60)), backgroundColor: '#ff453a' }} />)}
            </View>
          ) : null}
          {rec ? <Text style={{ fontSize: 22, fontWeight: 300, color: th.text }}>{mediaFormatTime(rp.ms)}</Text> : null}
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 14 }}>
            {rec ? <View onClick={pause} style={{ width: 36, height: 36, borderRadius: 18, backgroundColor: th.control, alignItems: 'center', justifyContent: 'center' }}>
              <Text style={{ fontSize: 14, color: th.text }}>{paused ? '▶' : '⏸'}</Text>
            </View> : null}
            <View onClick={start} style={{ width: 54, height: 54, borderRadius: 27, border: `3px solid ${th.separator}`, alignItems: 'center', justifyContent: 'center' }}>
              <View style={{ width: rec ? 20 : 40, height: rec ? 20 : 40, borderRadius: rec ? 4 : 20, backgroundColor: '#ff3b30' }} />
            </View>
          </View>
          {mics.length > 1 && !rec && (
            <Segmented value={source || defaultSource(snd)?.name || ''} onChange={setSource}
              options={mics.slice(0, 3).map((m) => [m.name, m.description.length > 14 ? `${m.description.slice(0, 13)}…` : m.description])} />
          )}
        </View>
      </View>
      <View style={{ flex: 1, padding: 20, gap: 14 }}>
        {cur ? (
          <>
            {renaming !== null ? (
              <TextInput value={renaming} autoFocus selectAll onChange={setRenaming} onSubmit={doRename} onCancel={() => setRenaming(null)} size={18} />
            ) : (
              <Text onDoubleClick={() => setRenaming(stem(cur.path))} style={{ fontSize: 20, fontWeight: 700, color: th.text }}>{stem(cur.path)}</Text>
            )}
            <Text style={{ fontSize: 12, color: th.textDim }}>{`${formatDate(new Date(cur.mtime), 'long')} · ${wave ? mediaFormatTime(wave.duration) : '…'}`}</Text>
            <View ref={waveBox} style={{ width: W, height: 110, borderRadius: 10, backgroundColor: th.hover, justifyContent: 'center', direction: 'ltr' }}
              onMouseDown={(e) => {
                const [lx, , w] = lord.getLayout(waveBox.current.id);
                const f = (e.x - lx) / w;
                if (!trim) { if (playId && wave) media.control(playId, 'seek', f * wave.duration); return; }
                drag.current = { edge: Math.abs(f - trim[0]) < Math.abs(f - trim[1]) ? 0 : 1 };
              }}>
              {wave ? <Wave peaks={wave.peaks} width={W} height={96} color={th.mode === 'dark' ? '#ff6961' : '#e0352b'}
                played={playId && wave.duration ? pp.pos / wave.duration : 0} sel={trim} /> : <Text style={{ fontSize: 12, color: th.textDim, textAlign: 'center' }}>…</Text>}
              {trim && [0, 1].map((k) => (
                <View key={k} style={{ position: 'absolute', left: trim[k] * W - 2, top: 0, width: 4, height: 110, borderRadius: 2, backgroundColor: '#ffcc00' }} />
              ))}
            </View>
            <View style={{ flexDirection: 'row', gap: 8, alignItems: 'center', flexWrap: 'wrap' }}>
              <Button primary label={pp.playing ? tr('recorder.pause') : tr('recorder.play')} onClick={play} />
              <Button label={tr('recorder.rename')} onClick={() => setRenaming(stem(cur.path))} />
              <Button label={trim ? tr('recorder.cancelTrim') : tr('recorder.trim')} onClick={() => setTrim(trim ? null : [0.1, 0.9])} />
              {trim && <Button primary label={tr('recorder.saveTrim')} onClick={() => exportAs(/\.wav$/i.test(sel) ? 'wav' : format, true)} />}
            </View>
            <View style={{ flexDirection: 'row', gap: 10, alignItems: 'center' }}>
              <Text style={{ fontSize: 12, color: th.text }}>{tr('recorder.export')}</Text>
              <Segmented value={format} onChange={setFormat} options={[['opus', 'Opus'], ['mp3', 'MP3'], ['wav', 'WAV']]} />
              <Button label={tr('recorder.exportBtn')} onClick={() => exportAs(format, false)} />
            </View>
          </>
        ) : (
          <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', gap: 8 }}>
            <Text style={{ fontSize: 34 }}>🎙</Text>
            <Text style={{ fontSize: 13, color: th.textDim, textAlign: 'center' }}>{tr('recorder.hint')}</Text>
          </View>
        )}
        {msg && <Text style={{ fontSize: 12, color: th.accent }}>{msg}</Text>}
      </View>
    </View>
  );
}

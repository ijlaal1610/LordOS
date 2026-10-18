// Cámara — vista previa de la webcam (V4L2), fotos y videos (diseño propio, al estilo de las apps
// de cámara de escritorio). El dispositivo lo abre lord-server después del permiso de cámara; lo
// lee y codifica lord-media (aislado). Fotos y videos van a ~/Imágenes/Cámara.
import {
  View, Text, Image, MediaView, Segmented, Toggle, useTheme, useKeyboard, wm,
  t as tr, media, usePlayer, useThumb, userDirs, fs, mediaFormatTime, useSettings,
} from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';

const APP = 'camera';

export function Icon({ size: s }) {
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(160deg, #3a3f47, #15171b)',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)`, overflow: 'hidden', alignItems: 'center', justifyContent: 'center' }}>
      {/* cuerpo de cámara con lente grande */}
      <View style={{ position: 'absolute', left: s * 0.16, top: s * 0.3, width: s * 0.68, height: s * 0.46, borderRadius: s * 0.08, backgroundColor: '#d9dde3' }} />
      <View style={{ position: 'absolute', left: s * 0.24, top: s * 0.23, width: s * 0.18, height: s * 0.09, borderRadius: s * 0.03, backgroundColor: '#d9dde3' }} />
      <View style={{ position: 'absolute', left: s * 0.32, top: s * 0.33, width: s * 0.36, height: s * 0.36, borderRadius: s * 0.18, backgroundColor: '#23262c' }} />
      <View style={{ position: 'absolute', left: s * 0.38, top: s * 0.39, width: s * 0.24, height: s * 0.24, borderRadius: s * 0.12, background: 'linear-gradient(135deg, #5b8cff, #1d2a66)' }} />
      <View style={{ position: 'absolute', left: s * 0.44, top: s * 0.43, width: s * 0.06, height: s * 0.06, borderRadius: s * 0.03, backgroundColor: 'rgba(255,255,255,0.8)' }} />
      <View style={{ position: 'absolute', left: s * 0.72, top: s * 0.35, width: s * 0.06, height: s * 0.06, borderRadius: s * 0.03, backgroundColor: '#30d158' }} />
    </View>
  );
}

const pad = (n) => String(n).padStart(2, '0');
function stamp() {
  const d = new Date();
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}.${pad(d.getMinutes())}.${pad(d.getSeconds())}`;
}
function outDir() {
  const dir = `${userDirs().pictures}/${tr('camera.folder')}`;
  try { fs.mkdir(userDirs().pictures); } catch (e) { /* ya está */ }
  try { fs.mkdir(dir); } catch (e) { /* ya está */ }
  return dir;
}
function freeName(base, ext) {
  const dir = outDir();
  for (let n = 1; n < 1000; n++) {
    const p = `${dir}/${base}${n > 1 ? ` (${n})` : ''}.${ext}`;
    try { fs.stat(p); } catch (e) { return p; }
  }
  return `${dir}/${base} ${Date.now()}.${ext}`;
}

function Shot({ path, onOpen }) {
  const thumb = useThumb(path, 128);
  return (
    <View onClick={onOpen} style={{ width: 56, height: 42, borderRadius: 6, overflow: 'hidden', backgroundColor: '#222' }}>
      {thumb ? <Image src={thumb} style={{ width: 56, height: 42, objectFit: 'cover' }} /> : null}
    </View>
  );
}

export default function Camera() {
  const th = useTheme();
  const [s, update] = useSettings();
  const mirror = (s.camera?.mirror ?? true);
  const [cams, setCams] = useState(() => media.cameras());
  const [dev, setDev] = useState(() => s.camera?.device || media.cameras()[0]?.device || null);
  const [id, setId] = useState(0);
  const [mode, setMode] = useState('photo');
  const [flash, setFlash] = useState(false);
  const [recStart, setRecStart] = useState(0);
  const [shots, setShots] = useState([]);
  const [denied, setDenied] = useState(false);
  const [now, setNow] = useState(Date.now());
  const p = usePlayer(id);
  const idRef = useRef(0);

  // abrir la cámara elegida
  useEffect(() => {
    let alive = true;
    media.close(idRef.current);
    setId(0);
    if (!dev) return undefined;
    media.camera(APP, dev, { width: 1280, height: 720 }).then((nid) => {
      if (!alive) { media.close(nid); return; }
      if (!nid) { setDenied(true); return; }
      setDenied(false);
      idRef.current = nid;
      setId(nid);
    });
    return () => { alive = false; };
  }, [dev]);
  useEffect(() => () => media.close(idRef.current), []);
  useEffect(() => { if (id) media.view(id, { mirror: mirror ? 1 : 0, fit: 'contain', background: 0xff000000 }); }, [id, mirror]);
  // una cámara nueva enchufada
  useEffect(() => { const tmr = setInterval(() => setCams(media.cameras()), 3000); return () => clearInterval(tmr); }, []);
  useEffect(() => { if (!dev && cams.length) setDev(cams[0].device); }, [cams]);
  useEffect(() => { if (!recStart) return undefined; const tmr = setInterval(() => setNow(Date.now()), 250); return () => clearInterval(tmr); }, [recStart]);

  useEffect(() => {
    if (p.photo?.ok) { setShots((l) => [p.photo.path, ...l].slice(0, 12)); }
  }, [p.photo]);
  useEffect(() => {
    if (p.video?.ok) setShots((l) => [p.video.path, ...l].slice(0, 12));
    setRecStart(0);
  }, [p.video]);

  const shoot = () => {
    if (!id) return;
    if (mode === 'photo') {
      media.photo(id, freeName(tr('camera.photoName', { date: stamp() }), 'jpg'), mirror);
      setFlash(true);
      setTimeout(() => setFlash(false), 160);
      return;
    }
    if (recStart) { media.cameraRecord(id, APP, null); return; }
    media.cameraRecord(id, APP, freeName(tr('camera.videoName', { date: stamp() }), 'mp4'), { mic: true, mirror, appName: tr('apps.camera') })
      .then((ok) => { if (ok) setRecStart(Date.now()); });
  };
  useKeyboard((e) => { if (e.key === ' ' || e.key === 'Enter') shoot(); });

  const cam = cams.find((c) => c.device === dev);
  return (
    <View style={{ flex: 1, backgroundColor: '#0b0b0d' }}>
      <View style={{ flex: 1 }}>
        <MediaView media={id} style={{ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0 }} />
        {(!cams.length || denied || p.error) && (
          <View style={{ position: 'absolute', left: 0, right: 0, top: '40%', alignItems: 'center', gap: 6, paddingHorizontal: 24 }}>
            <Text style={{ fontSize: 30 }}>📷</Text>
            <Text style={{ fontSize: 13, color: '#ddd', textAlign: 'center' }}>
              {!cams.length ? tr('camera.none') : denied ? tr('camera.denied') : tr('camera.error', { error: p.error })}
            </Text>
          </View>
        )}
        {flash && <View style={{ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0, backgroundColor: 'rgba(255,255,255,0.85)' }} />}
        {recStart > 0 && (
          <View style={{ position: 'absolute', top: 12, left: 0, right: 0, alignItems: 'center' }}>
            <View style={{ flexDirection: 'row', gap: 8, alignItems: 'center', paddingHorizontal: 12, paddingVertical: 5, borderRadius: 14, backgroundColor: 'rgba(0,0,0,0.6)' }}>
              <View style={{ width: 9, height: 9, borderRadius: 5, backgroundColor: '#ff3b30' }} />
              <Text style={{ fontSize: 12, color: '#fff' }}>{mediaFormatTime(now - recStart)}</Text>
            </View>
          </View>
        )}
      </View>
      <View style={{ height: 78, flexDirection: 'row', alignItems: 'center', gap: 14, paddingHorizontal: 14, backgroundColor: '#18181b' }}>
        <View style={{ flexDirection: 'row', gap: 6, width: 200 }}>
          {shots.slice(0, 3).map((x) => <Shot key={x} path={x} onOpen={() => wm.open(/\.mp4$/.test(x) ? 'player' : 'photos', { new: true, args: { path: x } })} />)}
        </View>
        <View style={{ flex: 1, alignItems: 'center' }}>
          <View onClick={shoot} style={{ width: 56, height: 56, borderRadius: 28, border: '3px solid #fff', alignItems: 'center', justifyContent: 'center' }}>
            <View style={{ width: mode === 'video' && recStart ? 22 : 44, height: mode === 'video' && recStart ? 22 : 44, borderRadius: mode === 'video' && recStart ? 5 : 22,
              backgroundColor: mode === 'video' ? '#ff3b30' : '#fff' }} />
          </View>
        </View>
        <View style={{ width: 200, alignItems: 'flex-end', gap: 6 }}>
          <Segmented value={mode} onChange={(v) => !recStart && setMode(v)} options={[['photo', tr('camera.photo')], ['video', tr('camera.video')]]} />
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
            <Text style={{ fontSize: 11, color: '#bbb' }}>{tr('camera.mirror')}</Text>
            <Toggle on={mirror} onChange={(v) => update({ camera: { ...(s.camera || {}), mirror: v } })} />
          </View>
        </View>
      </View>
      {cams.length > 1 && (
        <View style={{ position: 'absolute', right: 10, top: 10, flexDirection: 'row', gap: 4, padding: 4, borderRadius: 8, backgroundColor: 'rgba(0,0,0,0.55)' }}>
          {cams.map((c) => (
            <View key={c.device} onClick={() => { setDev(c.device); update({ camera: { ...(s.camera || {}), device: c.device } }); }}
              style={{ paddingHorizontal: 8, height: 24, borderRadius: 6, justifyContent: 'center', backgroundColor: c.device === dev ? th.accent : 'transparent' }}>
              <Text style={{ fontSize: 11, color: '#fff' }}>{c.name}</Text>
            </View>
          ))}
        </View>
      )}
      {cam && cams.length === 1 && (
        <View style={{ position: 'absolute', left: 12, top: 12, paddingHorizontal: 8, paddingVertical: 3, borderRadius: 6, backgroundColor: 'rgba(0,0,0,0.5)' }}>
          <Text style={{ fontSize: 11, color: '#ddd' }}>{cam.name}{p.info ? ` · ${p.info.width}×${p.info.height}` : ''}</Text>
        </View>
      )}
    </View>
  );
}

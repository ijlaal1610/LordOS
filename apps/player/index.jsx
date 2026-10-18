// Reproductor — videos y música (al estilo de los reproductores de escritorio, con diseño propio).
//
// La decodificación es de lord-media (aislado); acá están la biblioteca (artista y álbum desde
// las etiquetas, carátulas), la cola, aleatorio y repetir, los controles del video (barra de
// tiempo, volumen, subtítulos .srt y de la pista, pantalla completa, imagen en imagen) y lo que
// se publica para el Centro de control y las teclas multimedia (nowPlaying).
import {
  View, Text, Image, MediaView, ScrollView, SidebarItem, SectionTitle, useTheme, useAppWindow, useKeyboard, useStore, useWindowState, wm,
  t as tr, media, usePlayer, nowPlaying, mediaFormatTime, useThumb, fs, addListener, direction,
} from '@lordos/sdk';
import { useEffect, useMemo, useRef, useState } from 'react';
import { library, scan, albumsOf, artistsOf, parseSrt, AUDIO, VIDEO } from './library.js';

const APP = 'player';
const useWindowNonce = new Map();      // último «Abrir con» atendido por ventana

// Triángulo con barras (el motor no gira cajas): para íconos de reproducir.
function Tri({ x, y, w, h, color, steps = 12 }) {
  const rows = [];
  for (let i = 0; i < steps; i++) {
    const f = 1 - Math.abs((i + 0.5) / steps - 0.5) * 2;
    rows.push(<View key={i} style={{ position: 'absolute', left: x, top: y + (h * i) / steps, width: Math.max(1, w * f), height: h / steps + 0.6, backgroundColor: color }} />);
  }
  return rows;
}

export function Icon({ size: s }) {
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(160deg, #ff7a59, #d6246e)',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)`, overflow: 'hidden' }}>
      {/* pantalla con perforaciones de película */}
      <View style={{ position: 'absolute', left: s * 0.14, top: s * 0.22, width: s * 0.72, height: s * 0.56, borderRadius: s * 0.08, backgroundColor: 'rgba(20,10,30,0.82)' }} />
      {[0, 1, 2, 3].map((i) => (
        <View key={i} style={{ position: 'absolute', left: s * (0.2 + i * 0.17), top: s * 0.25, width: s * 0.08, height: s * 0.05, borderRadius: s * 0.015, backgroundColor: 'rgba(255,255,255,0.35)' }} />
      ))}
      {[0, 1, 2, 3].map((i) => (
        <View key={`b${i}`} style={{ position: 'absolute', left: s * (0.2 + i * 0.17), top: s * 0.70, width: s * 0.08, height: s * 0.05, borderRadius: s * 0.015, backgroundColor: 'rgba(255,255,255,0.35)' }} />
      ))}
      <Tri x={s * 0.42} y={s * 0.36} w={s * 0.24} h={s * 0.28} color="#fff" steps={Math.max(6, Math.round(s / 5))} />
    </View>
  );
}

const isVideo = (p) => VIDEO.test(p || '');
const isAudio = (p) => AUDIO.test(p || '');
const baseName = (p) => String(p || '').split('/').pop();

// Barra de tiempo (y de volumen): se arrastra; fija la dirección LTR (usa la x del puntero).
function Bar({ value, onChange, onCommit, height = 4, color, track, style }) {
  const ref = useRef(null), drag = useRef(false), cb = useRef({});
  cb.current = { onChange, onCommit };
  const at = (x) => {
    const [lx, , w] = lord.getLayout(ref.current.id);
    return Math.max(0, Math.min(1, (x - lx) / (w || 1)));
  };
  useEffect(() => {
    const a = addListener('pointermove', (e) => { if (drag.current) cb.current.onChange?.(at(e.x)); });
    const b = addListener('pointerup', (e) => { if (drag.current) { drag.current = false; cb.current.onCommit?.(at(e.x)); } });
    return () => { a(); b(); };
  }, []);
  const f = Math.max(0, Math.min(1, value || 0));
  return (
    <View ref={ref} onMouseDown={(e) => { drag.current = true; cb.current.onChange?.(at(e.x)); }}
      style={[{ height: 16, justifyContent: 'center', direction: 'ltr' }, style]}>
      <View style={{ height, borderRadius: height / 2, backgroundColor: track }}>
        <View style={{ width: `${f * 100}%`, height, borderRadius: height / 2, backgroundColor: color }} />
      </View>
    </View>
  );
}

function IconBtn({ glyph, onClick, size = 16, color, active }) {
  const th = useTheme();
  const [hover, setHover] = useState(false);
  return (
    <View onClick={onClick} onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)}
      style={{ minWidth: 30, height: 30, paddingHorizontal: 4, borderRadius: 15, alignItems: 'center', justifyContent: 'center',
        backgroundColor: hover ? 'rgba(127,127,127,0.18)' : 'transparent' }}>
      <Text style={{ fontSize: size, color: active ? th.accent : color || th.text }}>{glyph}</Text>
    </View>
  );
}

function Cover({ path, size, radius = 6 }) {
  const th = useTheme();
  const thumb = useThumb(path, 256);
  return (
    <View style={{ width: size, height: size, borderRadius: radius, overflow: 'hidden', backgroundColor: th.control, alignItems: 'center', justifyContent: 'center' }}>
      {thumb ? <Image src={thumb} style={{ width: size, height: size, objectFit: 'cover' }} />
        : <Text style={{ fontSize: size * 0.42, color: th.textDim }}>♪</Text>}
    </View>
  );
}

// Menú simple que se abre hacia arriba (subtítulos, audio).
function Popup({ items, onClose, right = 12, bottom = 64 }) {
  const th = useTheme();
  return (
    <View onMouseDown={(e) => e.stopPropagation?.()} style={{ position: 'absolute', right, bottom, minWidth: 200, padding: 6, borderRadius: 10, gap: 2,
      backgroundColor: th.windowBg, border: `1px solid ${th.separator}`, boxShadow: '0 12px 30px rgba(0,0,0,0.45)', zIndex: 20 }}>
      {items.map((it, i) => (it === '-' ? <View key={i} style={{ height: 1, backgroundColor: th.separator, marginVertical: 3 }} /> : (
        <View key={i} onClick={() => { it.onClick(); onClose(); }} style={{ paddingHorizontal: 10, height: 26, borderRadius: 6, justifyContent: 'center',
          backgroundColor: it.checked ? th.hover : 'transparent' }}>
          <Text style={{ fontSize: 12, color: th.text }}>{it.checked ? `✓ ${it.label}` : it.label}</Text>
        </View>
      )))}
    </View>
  );
}

export default function Player() {
  const th = useTheme();
  const win = useAppWindow();
  const lib = useStore(library);
  const [view, setView] = useWindowState('view', 'songs');           // songs | artists | albums | videos | queue
  const [sel, setSel] = useWindowState('sel', null);                 // artista o álbum elegido
  const [queue, setQueue] = useWindowState('queue', []);               // rutas
  const [index, setIndex] = useWindowState('index', -1);
  const [shuffle, setShuffle] = useWindowState('shuffle', false);
  const [repeat, setRepeat] = useWindowState('repeat', 'off');          // off | all | one
  const [id, setId] = useWindowState('id', 0);
  const [volume, setVolume] = useWindowState('volume', 1);
  const [seekPreview, setSeekPreview] = useState(null);
  const [extSubs, setExtSubs] = useWindowState('extSubs', null);         // .srt al lado del video
  const [subTrack, setSubTrack] = useWindowState('subTrack', -1);         // pista de subtítulos de la película
  const [menu, setMenu] = useState(null);
  const [controls, setControls] = useState(true);
  const [showVideo, setShowVideo] = useWindowState('showVideo', false);
  const [openFor, setOpenFor] = useWindowState('openFor', null);   // ruta del reproductor abierto
  const p = usePlayer(id);
  const current = queue[index] || null;
  const video = !!current && isVideo(current) && p.info && p.info.video >= 0;
  const idRef = useRef(0);
  idRef.current = id;
  const state = useRef({});
  state.current = { queue, index, shuffle, repeat };

  useEffect(() => { scan(); }, []);
  // al cerrar la ventana (no al volver a montarse por pantalla completa o imagen en imagen)
  useEffect(() => () => setTimeout(() => {
    if (wm.get(win.id)) return;
    media.close(idRef.current);
    if (nowPlaying.get()?.window === win.id) nowPlaying.set(null);
  }, 0), []);

  // abrir lo que mandó Archivos («Abrir con»)
  const lastNonce = useRef(null);
  useEffect(() => {
    const path = win.args?.path;
    if (path && win.args.nonce !== lastNonce.current && win.args.nonce !== useWindowNonce.get(win.id)) {
      useWindowNonce.set(win.id, win.args.nonce);
      setQueue([path]); setIndex(0); setShowVideo(isVideo(path));
    }
    lastNonce.current = win.args?.nonce;
  }, [win.args?.nonce]);

  // un reproductor por pista
  useEffect(() => {
    if (current && current === openFor && idRef.current) return;     // ya está abierto (la app se volvió a montar)
    media.close(idRef.current);
    setId(0);
    setExtSubs(null);
    setSubTrack(-1);
    setOpenFor(current);
    if (!current) return;
    const nid = media.play(APP, current, { appName: tr('apps.player'), music: isAudio(current), volume });
    setId(nid || 0);
    if (isVideo(current)) {
      const srt = current.replace(/\.[^./]+$/, '.srt');
      try { setExtSubs(parseSrt(fs.read(srt))); } catch (e) { /* sin .srt */ }
    }
    wm.setTitle(win.id, baseName(current));
  }, [current, index]);

  const next = (auto) => {
    const { queue: q, index: i, shuffle: sh, repeat: rp } = state.current;
    if (!q.length) return;
    if (auto && rp === 'one') { media.control(idRef.current, 'seek', 0); media.control(idRef.current, 'play'); return; }
    let n = sh ? Math.floor(Math.random() * q.length) : i + 1;
    if (n >= q.length) { if (rp !== 'all' && auto) return; n = 0; }
    setIndex(n);
  };
  const prev = () => {
    if (p.pos > 3000) { media.control(id, 'seek', 0); return; }
    setIndex((i) => Math.max(0, i - 1));
  };
  const toggle = () => media.control(id, p.playing ? 'pause' : 'play');

  useEffect(() => { if (p.state === 'ended') next(true); }, [p.state]);

  // para el Centro de control y las teclas multimedia
  const tracks = lib.tracks;
  const meta = useMemo(() => tracks.find((x) => x.path === current) || null, [tracks, current]);
  useEffect(() => {
    if (!current || !id) { if (nowPlaying.get()?.window === win.id) nowPlaying.set(null); return; }
    const tags = p.info?.tags || {};
    nowPlaying.set({
      app: APP, window: win.id, title: meta?.title || tags.title || baseName(current), artist: meta?.artist || tags.artist || '',
      album: meta?.album || tags.album || '', cover: media.thumb(current, 256), playing: p.playing, pos: p.pos, duration: p.duration,
      toggle: () => media.control(idRef.current, nowPlaying.get()?.playing ? 'pause' : 'play'), next: () => next(false), prev, stop: () => media.control(idRef.current, 'pause'),
    });
  }, [current, id, p.playing, Math.floor(p.pos / 1000), p.duration, meta, p.info]);

  // ocultar los controles del video
  const lastMove = useRef(0);
  useEffect(() => {
    if (!video || !p.playing) { setControls(true); return undefined; }
    const tmr = setInterval(() => { if (Date.now() - lastMove.current > 2800 && !menu) setControls(false); }, 500);
    return () => clearInterval(tmr);
  }, [video, p.playing, menu]);
  const poke = () => { lastMove.current = Date.now(); if (!controls) setControls(true); };

  useKeyboard((e) => {
    if (e.key === ' ' ) { toggle(); return; }
    if (e.key === 'ArrowRight') media.control(id, 'seek', p.pos + (e.shift ? 30000 : 5000));
    else if (e.key === 'ArrowLeft') media.control(id, 'seek', Math.max(0, p.pos - (e.shift ? 30000 : 5000)));
    else if (e.key === 'ArrowUp') changeVolume(Math.min(1.5, volume + 0.1));
    else if (e.key === 'ArrowDown') changeVolume(Math.max(0, volume - 0.1));
    else if (String(e.base || e.key).toLowerCase() === 'f' && video) fullscreen();
  });

  const changeVolume = (v) => { setVolume(v); media.control(id, 'volume', v); };
  const fullscreen = () => wm.setFullscreen(win.id, win.mode !== 'fullscreen');
  const pip = () => {
    if (win.mode === 'pip') { wm.setPip(win.id, null); return; }
    const sw = lord.screen.width, sh = lord.screen.height;
    const w = 400, hh = Math.round(w * ((p.info?.streams?.find((s) => s.type === 'video')?.height || 9) / (p.info?.streams?.find((s) => s.type === 'video')?.width || 16)));
    wm.setPip(win.id, { x: sw - w - 24, y: sh - hh - 96, w, h: hh });
  };

  const playList = (paths, i) => { setQueue(paths); setIndex(i); setShowVideo(isVideo(paths[i])); };

  // subtítulos visibles ahora
  const pos = seekPreview != null ? seekPreview : p.pos;
  const subs = subTrack >= 0 ? p.subs : extSubs;
  const cue = subs ? subs.filter((c) => c.start <= p.pos && p.pos < c.end).map((c) => c.text).join('\n') : '';
  const subStreams = (p.info?.streams || []).filter((s) => s.type === 'subtitle' && s.text);
  const audioStreams = (p.info?.streams || []).filter((s) => s.type === 'audio');
  const label = (s, i) => [s.title, s.lang && s.lang !== 'und' ? s.lang : null].filter(Boolean).join(' · ') || tr('player.track', { n: i + 1 });

  const transport = (dark) => {
    const col = dark ? '#fff' : th.text, dim = dark ? 'rgba(255,255,255,0.7)' : th.textDim;
    return (
      <View style={{ gap: 4 }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10, direction: 'ltr' }}>
          <Text style={{ fontSize: 11, color: dim, width: 52, textAlign: 'end' }}>{mediaFormatTime(pos)}</Text>
          <Bar style={{ flex: 1 }} value={p.duration ? pos / p.duration : 0} color={th.accent} track={dark ? 'rgba(255,255,255,0.25)' : th.control}
            onChange={(f) => setSeekPreview(f * p.duration)} onCommit={(f) => { media.control(id, 'seek', Math.round(f * p.duration)); setSeekPreview(null); }} />
          <Text style={{ fontSize: 11, color: dim, width: 52 }}>{mediaFormatTime(p.duration)}</Text>
        </View>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 4, direction: 'ltr' }}>
          {!video && <IconBtn glyph="⤮" color={col} active={shuffle} onClick={() => setShuffle(!shuffle)} />}
          <IconBtn glyph="⏮" color={col} onClick={prev} />
          <IconBtn glyph={p.playing ? '⏸' : '▶'} size={22} color={col} onClick={toggle} />
          <IconBtn glyph="⏭" color={col} onClick={() => next(false)} />
          {!video && <IconBtn glyph={repeat === 'one' ? '🔂' : '🔁'} color={col} active={repeat !== 'off'}
            onClick={() => setRepeat(repeat === 'off' ? 'all' : repeat === 'all' ? 'one' : 'off')} />}
          <View style={{ flex: 1 }} />
          <Text style={{ fontSize: 14, color: col }}>{volume === 0 ? '🔇' : volume < 0.5 ? '🔈' : '🔊'}</Text>
          <Bar style={{ width: 90 }} value={volume / 1.5} color={dark ? '#fff' : th.accent} track={dark ? 'rgba(255,255,255,0.25)' : th.control}
            onChange={(f) => changeVolume(Math.round(f * 150) / 100)} />
          {video && <IconBtn glyph="💬" color={col} active={subTrack >= 0 || !!extSubs} onClick={() => setMenu(menu === 'subs' ? null : 'subs')} />}
          {video && audioStreams.length > 1 && <IconBtn glyph="♫" color={col} onClick={() => setMenu(menu === 'audio' ? null : 'audio')} />}
          {video && <IconBtn glyph="⧉" color={col} active={win.mode === 'pip'} onClick={pip} />}
          {video && <IconBtn glyph={win.mode === 'fullscreen' ? '⤡' : '⤢'} color={col} onClick={fullscreen} />}
        </View>
      </View>
    );
  };

  // ---- video ----
  if (current && (showVideo || win.mode !== 'window') && isVideo(current)) {
    const small = win.mode === 'pip';
    return (
      <View style={{ flex: 1, backgroundColor: '#000' }} onMouseMove={poke} onMouseDown={() => setMenu(null)}>
        <MediaView media={id} style={{ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0 }} />
        {p.error && <View style={{ position: 'absolute', left: 0, right: 0, top: '45%', alignItems: 'center' }}>
          <Text style={{ fontSize: 13, color: '#ff9f9f' }}>{p.error === 'crash' ? tr('player.crashed') : tr('player.error', { error: p.error })}</Text>
        </View>}
        {cue ? (
          <View style={{ position: 'absolute', left: 24, right: 24, bottom: controls && !small ? 96 : 28, alignItems: 'center' }}>
            <View style={{ paddingHorizontal: 10, paddingVertical: 4, borderRadius: 6, backgroundColor: 'rgba(0,0,0,0.55)' }}>
              <Text style={{ fontSize: small ? 12 : 20, color: '#fff', textAlign: 'center', lineHeight: 1.3 }}>{cue}</Text>
            </View>
          </View>
        ) : null}
        {small ? (
          <View style={{ position: 'absolute', right: 6, top: 6, flexDirection: 'row', gap: 2 }}>
            <IconBtn glyph={p.playing ? '⏸' : '▶'} color="#fff" onClick={toggle} />
            <IconBtn glyph="⧉" color="#fff" onClick={pip} />
          </View>
        ) : controls && (
          <View style={{ position: 'absolute', left: 0, right: 0, bottom: 0, paddingHorizontal: 14, paddingTop: 24, paddingBottom: 8,
            background: 'linear-gradient(180deg, rgba(0,0,0,0), rgba(0,0,0,0.75))' }}>
            {transport(true)}
          </View>
        )}
        {controls && !small && win.mode === 'window' && (
          <View style={{ position: 'absolute', left: 10, top: 10 }}>
            <IconBtn glyph={direction() === 'rtl' ? '›' : '‹'} size={20} color="#fff" onClick={() => setShowVideo(false)} />
          </View>
        )}
        {menu === 'subs' && <Popup onClose={() => setMenu(null)} items={[
          { label: tr('player.subs.off'), checked: subTrack < 0 && !extSubs, onClick: () => { setSubTrack(-1); setExtSubs(null); media.control(id, 'subTrack', -1); } },
          ...(extSubs ? [{ label: tr('player.subs.file'), checked: subTrack < 0, onClick: () => { setSubTrack(-1); media.control(id, 'subTrack', -1); } }] : []),
          ...subStreams.map((s, i) => ({ label: label(s, i), checked: subTrack === s.index, onClick: () => { setSubTrack(s.index); media.control(id, 'subTrack', s.index); } })),
        ]} />}
        {menu === 'audio' && <Popup onClose={() => setMenu(null)} items={audioStreams.map((s, i) => ({
          label: `${label(s, i)} (${s.codec}, ${s.channels} ch)`, checked: p.info?.audio === s.index, onClick: () => media.control(id, 'audioTrack', s.index) }))} />}
      </View>
    );
  }

  // ---- biblioteca ----
  const albums = albumsOf(tracks), artists = artistsOf(tracks);
  const row = (x, i, list, extra) => (
    <View key={x.path} onDoubleClick={() => playList(list.map((y) => y.path), i)} onClick={() => playList(list.map((y) => y.path), i)}
      style={{ height: 34, flexDirection: 'row', alignItems: 'center', gap: 10, paddingHorizontal: 12, borderRadius: 6,
        backgroundColor: x.path === current ? th.hover : i % 2 ? 'rgba(127,127,127,0.05)' : 'transparent' }}>
      <Text style={{ width: 22, fontSize: 11, color: x.path === current ? th.accent : th.textDim, textAlign: 'end' }}>{x.path === current ? '♪' : extra ?? i + 1}</Text>
      <Text style={{ flex: 2, fontSize: 12, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{x.title}</Text>
      <Text style={{ flex: 1, fontSize: 12, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{x.artist}</Text>
      <Text style={{ flex: 1, fontSize: 12, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{x.album}</Text>
      <Text style={{ width: 48, fontSize: 11, color: th.textDim, textAlign: 'end' }}>{x.duration ? mediaFormatTime(x.duration) : ''}</Text>
    </View>
  );
  let content;
  if (view === 'albums' || (view === 'artists' && sel)) {
    const list = view === 'artists' ? albums.filter((a) => a.artist === sel || a.tracks.some((x) => x.artist === sel)) : albums;
    if (view === 'albums' && sel) {
      const a = albums.find((x) => x.key === sel);
      content = a ? (
        <View style={{ gap: 14 }}>
          <View style={{ flexDirection: 'row', gap: 16, alignItems: 'flex-end' }}>
            <Cover path={a.tracks[0].path} size={140} radius={10} />
            <View style={{ gap: 4, flex: 1 }}>
              <Text style={{ fontSize: 22, fontWeight: 700, color: th.text }}>{a.album || tr('player.unknownAlbum')}</Text>
              <Text style={{ fontSize: 14, color: th.accent }}>{a.artist || tr('player.unknownArtist')}</Text>
              <Text style={{ fontSize: 12, color: th.textDim }}>{[a.year, tr('player.songs', { count: a.tracks.length })].filter(Boolean).join(' · ')}</Text>
            </View>
          </View>
          <View>{a.tracks.map((x, i) => row(x, i, a.tracks, x.track || i + 1))}</View>
        </View>
      ) : null;
    } else {
      content = (
        <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 18 }}>
          {list.map((a) => (
            <View key={a.key} onClick={() => { setView('albums'); setSel(a.key); }} style={{ width: 150, gap: 6 }}>
              <Cover path={a.tracks[0].path} size={150} radius={8} />
              <Text style={{ fontSize: 12, fontWeight: 600, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{a.album || tr('player.unknownAlbum')}</Text>
              <Text style={{ fontSize: 11, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{a.artist || tr('player.unknownArtist')}</Text>
            </View>
          ))}
        </View>
      );
    }
  } else if (view === 'artists') {
    content = (
      <View>{artists.map((a) => (
        <View key={a.name} onClick={() => setSel(a.name)} style={{ height: 48, flexDirection: 'row', alignItems: 'center', gap: 12, paddingHorizontal: 8, borderRadius: 8 }}>
          <Cover path={a.tracks[0].path} size={36} radius={18} />
          <Text style={{ flex: 1, fontSize: 13, color: th.text }}>{a.name || tr('player.unknownArtist')}</Text>
          <Text style={{ fontSize: 11, color: th.textDim }}>{tr('player.songs', { count: a.tracks.length })}</Text>
        </View>
      ))}</View>
    );
  } else if (view === 'videos') {
    content = (
      <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 16 }}>
        {lib.videos.map((v, i) => <VideoCard key={v.path} v={v} onOpen={() => playList(lib.videos.map((x) => x.path), i)} />)}
      </View>
    );
  } else if (view === 'queue') {
    content = <View>{queue.map((q, i) => row(tracks.find((x) => x.path === q) || { path: q, title: baseName(q), artist: '', album: '' }, i, queue.map((x) => ({ path: x }))))}</View>;
  } else {
    content = <View>{tracks.slice().sort((a, b) => a.title.localeCompare(b.title)).map((x, i, list) => row(x, i, list))}</View>;
  }
  const titles = { songs: tr('player.songsTitle'), artists: sel || tr('player.artists'), albums: tr('player.albums'), videos: tr('player.videos'), queue: tr('player.queue') };

  return (
    <View style={{ flex: 1, flexDirection: 'row' }} onMouseDown={() => setMenu(null)}>
      <View style={{ width: 190, backgroundColor: th.sidebarBg, padding: 8, gap: 2 }}>
        <SectionTitle>{tr('player.library')}</SectionTitle>
        {[['songs', '♪'], ['artists', '👤'], ['albums', '◫'], ['videos', '▶']].map(([k, g]) => (
          <SidebarItem key={k} label={tr(`player.nav.${k}`)} icon={g} selected={view === k} onClick={() => { setView(k); setSel(null); }} />
        ))}
        <SectionTitle>{tr('player.playing')}</SectionTitle>
        <SidebarItem label={tr('player.nav.queue')} icon="≡" selected={view === 'queue'} onClick={() => setView('queue')} />
        {current && isVideo(current) && <SidebarItem label={tr('player.nav.video')} icon="▣" selected={false} onClick={() => setShowVideo(true)} />}
        <View style={{ flex: 1 }} />
        {lib.scanning && <Text style={{ fontSize: 10, color: th.textDim, paddingHorizontal: 8 }}>{tr('player.scanning', { done: lib.done, total: lib.total })}</Text>}
      </View>
      <View style={{ flex: 1 }}>
        <View style={{ height: 44, flexDirection: 'row', alignItems: 'center', paddingHorizontal: 16, gap: 8 }}>
          {sel && <IconBtn glyph={direction() === 'rtl' ? '›' : '‹'} size={18} onClick={() => setSel(null)} />}
          <Text style={{ fontSize: 17, fontWeight: 700, color: th.text }}>{titles[view]}</Text>
        </View>
        <ScrollView style={{ flex: 1, paddingHorizontal: 12 }}>
          {(view === 'videos' ? lib.videos.length : tracks.length) || view === 'queue' ? content : (
            <View style={{ padding: 40, alignItems: 'center', gap: 6 }}>
              <Text style={{ fontSize: 28 }}>{view === 'videos' ? '🎬' : '🎵'}</Text>
              <Text style={{ fontSize: 13, color: th.textDim, textAlign: 'center' }}>{tr(view === 'videos' ? 'player.emptyVideos' : 'player.emptyMusic')}</Text>
            </View>
          )}
          <View style={{ height: 16 }} />
        </ScrollView>
        {current && <View style={{ height: 1, backgroundColor: th.separator }} />}
        {current && (
          <View style={{ height: 84, flexDirection: 'row', alignItems: 'center', gap: 12, paddingHorizontal: 14, backgroundColor: th.sidebarBg }}>
            <View onClick={() => isVideo(current) && setShowVideo(true)}><Cover path={current} size={56} /></View>
            <View style={{ width: 170, gap: 2, overflow: 'hidden' }}>
              <Text style={{ fontSize: 13, fontWeight: 600, color: th.text, whiteSpace: 'nowrap' }}>{meta?.title || p.info?.tags?.title || baseName(current)}</Text>
              <Text style={{ fontSize: 11, color: th.textDim, whiteSpace: 'nowrap' }}>{meta?.artist || p.info?.tags?.artist || ''}</Text>
            </View>
            <View style={{ flex: 1 }}>{transport(false)}</View>
          </View>
        )}
      </View>
    </View>
  );
}

function VideoCard({ v, onOpen }) {
  const th = useTheme();
  const thumb = useThumb(v.path, 320);
  return (
    <View onClick={onOpen} style={{ width: 200, gap: 6 }}>
      <View style={{ width: 200, height: 112, borderRadius: 8, overflow: 'hidden', backgroundColor: '#111', alignItems: 'center', justifyContent: 'center' }}>
        {thumb ? <Image src={thumb} style={{ width: 200, height: 112, objectFit: 'cover' }} /> : <Text style={{ fontSize: 26, color: '#666' }}>▶</Text>}
      </View>
      <Text style={{ fontSize: 12, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{v.title}</Text>
    </View>
  );
}

// Detail.jsx — una entrada: verla (campos con mostrar/copiar, código 2FA con cuenta regresiva,
// QR del Wi-Fi, notas, adjuntos, historial, recordatorio de cambio) y editarla (plantilla de su
// categoría, campos propios, etiquetas, generador, 2FA desde un QR).
import {
  View, Text, Image, useTheme, useStore, Button, TextInput, Toggle, ScrollView, Markdown, wm, formatDate, formatTime, formatBytes, t,
} from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';
import { app, req, copySecret, copyText, revealField, upsertItem, dropItem, toast, errorText, appMode } from './api.js';
import { builtinById, fieldLabel, templateFields, allCategories, catName, FIELD_TYPES } from './templates.js';
import {
  EntryIcon, CatIcon, Sheet, b64ToBytes, fileReq, fileName, Label, Hint, Warning, QrImage, TextArea, FilePicker, Choice, IconButton, Chip, StrengthBar, RED, ORANGE,
} from './ui.jsx';
import { GeneratorSheet } from './Generator.jsx';

const HIDDEN = new Set(['hidden', 'totp', 'pin']);
const DAY = 86400;

// ---- código 2FA ----

function TotpBox({ v, id }) {
  const th = useTheme();
  const [codes, setCodes] = useState([]);
  const [now, setNow] = useState(Date.now() / 1000);
  const base = useRef({ at: 0, server: 0 });
  const load = async () => {
    try {
      const r = await req('totp', { v, ids: [id] });
      base.current = { at: Date.now() / 1000, server: r.now };
      setCodes(r.codes || []);
    } catch (e) { setCodes([]); }
  };
  useEffect(() => { load(); }, [id]);
  useEffect(() => {
    const iv = setInterval(() => {
      const n = Date.now() / 1000;
      setNow(n);
      const c = codes[0];
      if (c && c.period) {
        const srv = base.current.server + (n - base.current.at);
        const left = c.period - (Math.floor(srv) % c.period);
        if (left === c.period || left > c.remaining + 1 + (n - base.current.at)) load();
      }
    }, 1000);
    return () => clearInterval(iv);
  }, [codes]);
  if (!codes.length) return null;
  return codes.map((c) => {
    const elapsed = now - base.current.at;
    const left = Math.max(0, Math.round(c.remaining - elapsed));
    const frac = c.period ? left / c.period : 0;
    const shown = c.code.length === 6 ? `${c.code.slice(0, 3)} ${c.code.slice(3)}` : c.code;
    return (
      <View key={c.k} style={{ gap: 6, padding: 12, borderRadius: 10, backgroundColor: th.control }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
          <Text style={{ flex: 1, fontSize: 26, fontWeight: 600, fontFamily: 'monospace', color: left <= 5 ? RED : th.text, direction: 'ltr' }}>{shown}</Text>
          <Text style={{ fontSize: 13, color: th.textDim }}>{t('passwords.totp.left', { s: left })}</Text>
          <IconButton glyph="⧉" onClick={() => copySecret(c.code, t('passwords.totp.code'))} />
        </View>
        <View style={{ height: 4, borderRadius: 2, backgroundColor: th.windowBg, direction: 'ltr' }}>
          <View style={{ width: `${Math.round(frac * 100)}%`, height: 4, borderRadius: 2, backgroundColor: left <= 5 ? RED : th.accent }} />
        </View>
        {(c.issuer || c.account) && <Text style={{ fontSize: 11, color: th.textDim }}>{[c.issuer, c.account].filter(Boolean).join(' · ')}</Text>}
      </View>
    );
  });
}

// ---- un campo (ver) ----

function FieldRow({ v, entry, f, index }) {
  const th = useTheme();
  const [shown, setShown] = useState(null);
  useEffect(() => { setShown(null); }, [entry.id, f.k]);
  if (f.t === 'totp') return (
    <View style={{ gap: 4 }}>
      <Label>{fieldLabel(f)}</Label>
      <TotpBox v={v} id={entry.id} />
    </View>
  );
  const hidden = HIDDEN.has(f.t);
  const label = fieldLabel(f);
  const reveal = async () => {
    if (shown != null) { setShown(null); return; }
    try { setShown(await revealField(v, entry.id, f.k, index)); } catch (e) { toast(errorText(e.code)); }
  };
  const copy = async () => {
    try {
      const val = hidden ? (shown ?? await revealField(v, entry.id, f.k, index)) : f.v;
      if (hidden) copySecret(val, label); else copyText(val, label);
    } catch (e) { toast(errorText(e.code)); }
  };
  let value = hidden ? (shown ?? (f.has ? '••••••••••' : '')) : f.v;
  if (f.t === 'bool') value = f.v === 'true' || f.v === '1' ? t('passwords.yes') : t('passwords.no');
  if (f.k === 'doc_type' && ['dni', 'passport', 'license', 'health'].includes(f.v)) value = t(`passwords.doc.${f.v}`);
  if (f.k === 'security' && f.v === 'none') value = t('passwords.wifi.open');
  if (!value && !hidden) return null;
  const multi = String(value).includes('\n');
  return (
    <View style={{ gap: 3, paddingVertical: 6, borderBottom: `1px solid ${th.separator}` }}>
      <Label>{label}</Label>
      <View style={{ flexDirection: 'row', alignItems: multi ? 'flex-start' : 'center', gap: 6 }}>
        <Text style={{ flex: 1, fontSize: 14, color: f.t === 'url' ? th.accent : th.text, lineHeight: 19,
          fontFamily: hidden || f.t === 'pin' ? 'monospace' : undefined, direction: hidden ? 'ltr' : undefined }}>{value}</Text>
        {hidden && f.has && <IconButton glyph={shown != null ? '◉' : '👁'} onClick={reveal} />}
        {(f.has || !hidden) && <IconButton glyph="⧉" onClick={copy} />}
      </View>
    </View>
  );
}

// ---- adjuntos ----

function Attachments({ v, entry, onChanged }) {
  const th = useTheme();
  const [saveAtt, setSaveAtt] = useState(null);
  const [view, setView] = useState(null);
  const [adding, setAdding] = useState(false);
  const isImage = (a) => /^image\//.test(a.mime) || /\.(png|jpe?g)$/i.test(a.name);
  const isText = (a) => /^text\//.test(a.mime) || /\.(txt|key|lic|pem|asc|md|json|csv)$/i.test(a.name);
  const open = async (a) => {
    try {
      if (isImage(a)) {
        const r = await req('att-preview', { v, id: entry.id, att: a.id, max: 520 });
        setView({ a, img: { width: r.w, height: r.h, data: b64ToBytes(r.rgba) } });
      } else if (isText(a)) {
        const r = await req('att-text', { v, id: entry.id, att: a.id });
        setView({ a, text: r.text });
      } else setSaveAtt(a);
    } catch (e) { toast(errorText(e.code)); }
  };
  const mimeOf = (name) => (/\.png$/i.test(name) ? 'image/png' : /\.jpe?g$/i.test(name) ? 'image/jpeg' : /\.(txt|key|lic|md)$/i.test(name) ? 'text/plain' : 'application/octet-stream');
  return (
    <View style={{ gap: 6 }}>
      <View style={{ flexDirection: 'row', alignItems: 'center' }}>
        <Text style={{ flex: 1, fontSize: 13, fontWeight: 600, color: th.text }}>{t('passwords.att.title')}</Text>
        <Button label={t('passwords.att.add')} onClick={() => setAdding(true)} />
      </View>
      {!entry.att?.length && <Hint>{t('passwords.att.none')}</Hint>}
      {(entry.att || []).map((a) => (
        <View key={a.id} style={{ flexDirection: 'row', alignItems: 'center', gap: 8, padding: 8, borderRadius: 8, backgroundColor: th.control }}>
          <Text style={{ fontSize: 16 }}>{isImage(a) ? '🖼️' : '📎'}</Text>
          <View style={{ flex: 1 }}>
            <Text style={{ fontSize: 13, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{a.name}</Text>
            <Text style={{ fontSize: 11, color: th.textDim }}>{formatBytes(a.size)}</Text>
          </View>
          {(isImage(a) || isText(a)) && <IconButton glyph="👁" onClick={() => open(a)} />}
          <IconButton glyph="⬇" onClick={() => setSaveAtt(a)} />
          <IconButton glyph="✕" onClick={async () => { try { const r = await req('att-del', { v, id: entry.id, att: a.id }); upsertItem(r.item); onChanged(); } catch (e) { toast(errorText(e.code)); } }} />
        </View>
      ))}
      {view && (
        <Sheet title={view.a.name} onClose={() => setView(null)} width={view.img ? Math.max(360, view.img.width + 40) : 520}>
          {view.img ? <Image pixels={view.img} style={{ width: view.img.width, height: view.img.height, borderRadius: 6 }} />
            : <Text style={{ fontSize: 12, fontFamily: 'monospace', color: th.text, lineHeight: 17 }}>{view.text}</Text>}
        </Sheet>
      )}
      {saveAtt && <FilePicker save suggest={saveAtt.name} title={t('passwords.att.save')} onClose={() => setSaveAtt(null)}
        onPick={async (p) => {
          const a = saveAtt; setSaveAtt(null);
          try { await req('att-get', { v, id: entry.id, att: a.id }, fileReq([p, 'w'])); toast(t('passwords.att.saved')); } catch (e) { toast(errorText(e.code)); }
        }} />}
      {adding && <FilePicker title={t('passwords.att.add')} onClose={() => setAdding(false)}
        onPick={async (p) => {
          setAdding(false);
          const name = fileName(p).split('/').pop();
          try { const r = await req('att-add', { v, id: entry.id, name, mime: mimeOf(name) }, fileReq([p, 'r'])); upsertItem(r.item); onChanged(); toast(t('passwords.att.added')); }
          catch (e) { toast(errorText(e.code)); }
        }} />}
    </View>
  );
}

// ---- historial ----

function History({ v, entry }) {
  const th = useTheme();
  const [open, setOpen] = useState(false);
  const [full, setFull] = useState(null);
  const hist = full || entry.hist || [];
  if (!hist.length) return null;
  return (
    <View style={{ gap: 6 }}>
      <View onClick={() => setOpen(!open)} style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
        <Text style={{ fontSize: 11, color: th.textDim }}>{open ? '▾' : '▸'}</Text>
        <Text style={{ fontSize: 13, fontWeight: 600, color: th.text }}>{t('passwords.hist.title', { count: hist.length })}</Text>
      </View>
      {open && (
        <View style={{ gap: 6 }}>
          {!full && <Button label={t('passwords.hist.reveal')} onClick={async () => { try { setFull((await req('get', { v, id: entry.id, hist: true, reveal: true })).entry.hist); } catch (e) { toast(errorText(e.code)); } }} style={{ alignSelf: 'flex-start' }} />}
          {hist.map((h, i) => (
            <View key={i} style={{ padding: 8, borderRadius: 8, backgroundColor: th.control, gap: 3 }}>
              <Text style={{ fontSize: 11, color: th.textDim }}>{`${formatDate(new Date(h.t * 1000))} ${formatTime(new Date(h.t * 1000))}`}</Text>
              {h.title !== entry.title && <Text style={{ fontSize: 12, color: th.text }}>{`${t('passwords.edit.title')}: ${h.title}`}</Text>}
              {(h.f || []).map((f, k) => (
                <Text key={k} style={{ fontSize: 12, color: th.text }}>{`${fieldLabel(f)}: ${f.v != null ? f.v : f.has ? '••••••' : ''}`}</Text>
              ))}
            </View>
          ))}
        </View>
      )}
    </View>
  );
}

// ---- ver una entrada ----

export function EntryView({ v, id, onEdit, onDeleted }) {
  const th = useTheme();
  const cats = useStore(app, (s) => s.cats);
  const [e, setE] = useState(null);
  const [qr, setQr] = useState(null);
  const [err, setErr] = useState('');
  const load = async () => {
    try { setE((await req('get', { v, id, hist: true })).entry); setErr(''); } catch (x) { setErr(errorText(x.code)); setE(null); }
  };
  useEffect(() => { setQr(null); load(); }, [id, v]);
  if (err) return <Center text={err} />;
  if (!e) return <Center text="" />;
  const cat = allCategories(cats).find((c) => c.id === e.cat) || builtinById(e.cat) || { id: e.cat };
  const b = builtinById(cat.template || e.cat) || builtinById(e.cat);
  const act = async (op, extra = {}) => {
    try {
      const r = await req(op, { v, id: e.id, ...extra });
      if (op === 'purge') { dropItem(e.id); onDeleted(); return; }
      upsertItem(r.item);
      load();
      if (op === 'trash') { toast(t('passwords.toast.trashed')); onDeleted(); }
    } catch (x) { toast(errorText(x.code)); }
  };
  const now = Date.now() / 1000;
  const pwField = e.f.find((f) => f.k === 'password' && f.has);
  const since = e.pwch || e.created;
  const ageDays = pwField && since ? Math.floor((now - since) / DAY) : null;
  const dueIn = e.rotate && ageDays != null ? e.rotate - ageDays : null;
  const seen = {};
  return (
    <ScrollView style={{ flex: 1 }}>
      <View style={{ padding: 22, gap: 14 }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 14 }}>
          <EntryIcon entry={{ ...e, url: e.f.find((f) => f.k === 'url')?.v }} cat={cat} size={58} />
          <View style={{ flex: 1, gap: 4 }}>
            <Text style={{ fontSize: 22, fontWeight: 700, color: th.text }}>{e.title || t('passwords.untitled')}</Text>
            <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 5, alignItems: 'center' }}>
              <Chip label={catName(cat)} color={cat.color || b?.color} />
              {(e.tags || []).map((tg) => <Chip key={tg} label={`#${tg}`} color={tagColor(tg)} />)}
            </View>
          </View>
          {v === 'main' && <IconButton glyph={e.fav ? '★' : '☆'} active={e.fav} onClick={() => act('fav', { on: !e.fav })} />}
          {!e.trash && <Button primary label={t('passwords.edit.button')} onClick={() => onEdit(e)} />}
        </View>
        {e.trash ? <Warning>{t('passwords.trash.inTrash')}</Warning> : null}
        {b?.warning && <Warning>{t('passwords.crypto.warning')}</Warning>}
        {dueIn != null && (
          <View style={{ flexDirection: 'row', gap: 8, padding: 10, borderRadius: 9, backgroundColor: dueIn <= 0 ? 'rgba(255,69,58,0.14)' : th.control }}>
            <Text style={{ fontSize: 14 }}>⏰</Text>
            <Text style={{ flex: 1, fontSize: 12, color: dueIn <= 0 ? RED : th.text }}>
              {dueIn <= 0 ? t('passwords.rotate.overdue', { days: e.rotate, ago: -dueIn }) : t('passwords.rotate.due', { days: e.rotate, left: dueIn })}
            </Text>
          </View>
        )}
        <View>
          {e.f.map((f) => { const i = seen[f.k] = (seen[f.k] ?? -1) + 1; return <FieldRow key={`${f.k}-${i}`} v={v} entry={e} f={f} index={i} />; })}
        </View>
        {(e.cat === 'wifi' || b?.id === 'wifi') && (
          <View style={{ gap: 8, alignItems: 'flex-start' }}>
            <Button label={qr ? t('passwords.wifi.hideQr') : t('passwords.wifi.showQr')} onClick={async () => {
              if (qr) { setQr(null); return; }
              try { setQr(await req('qr', { v, id: e.id })); } catch (x) { toast(errorText(x.code)); }
            }} />
            {qr && <View style={{ padding: 10, borderRadius: 12, backgroundColor: '#fff', gap: 6, alignItems: 'center' }}>
              <QrImage qr={qr} scale={Math.max(3, Math.floor(260 / (qr.size + 8)))} />
              <Text style={{ fontSize: 11, color: '#333' }}>{t('passwords.wifi.scan')}</Text>
            </View>}
          </View>
        )}
        {b?.settingsLink && !appMode && <Button label={t('passwords.vpn.openSettings')} onClick={() => wm.open('settings', { args: { page: 'network' } })} style={{ alignSelf: 'flex-start' }} />}
        {e.notes ? (
          <View style={{ gap: 4 }}>
            <Label>{t('passwords.field.notes')}</Label>
            <View style={{ padding: 10, borderRadius: 8, backgroundColor: th.control }}><Markdown source={e.notes} width={460} /></View>
          </View>
        ) : null}
        {v === 'main' && !e.trash && <Attachments v={v} entry={e} onChanged={load} />}
        <History v={v} entry={e} />
        <Text style={{ fontSize: 11, color: th.textDim }}>
          {t('passwords.dates', { created: formatDate(new Date(e.created * 1000)), modified: `${formatDate(new Date(e.modified * 1000))} ${formatTime(new Date(e.modified * 1000))}` })}
        </Text>
        <View style={{ flexDirection: 'row', gap: 8, flexWrap: 'wrap' }}>
          {v === 'main' && !e.trash && <Button label={e.arch ? t('passwords.unarchive') : t('passwords.archive')} onClick={() => act('archive', { on: !e.arch })} />}
          {!e.trash && v === 'main' && <Button label={t('passwords.trash.move')} onClick={() => act('trash')} />}
          {e.trash ? <Button label={t('passwords.trash.restore')} onClick={() => act('restore')} /> : null}
          {(e.trash || v === 'login') ? <Button label={t('passwords.trash.purge')} onClick={() => act('purge')} style={{ backgroundColor: 'rgba(255,69,58,0.2)' }} /> : null}
        </View>
      </View>
    </ScrollView>
  );
}

export function Center({ text, children }) {
  const th = useTheme();
  return (
    <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', gap: 10, padding: 20 }}>
      {children}
      <Text style={{ fontSize: 13, color: th.textDim, textAlign: 'center' }}>{text}</Text>
    </View>
  );
}

export function tagColor(name) {
  const tags = app.get().tags || [];
  const tg = tags.find((x) => x.name === name);
  if (tg?.color) return tg.color;
  let h = 0;
  for (const ch of String(name)) h = (h * 31 + ch.charCodeAt(0)) >>> 0;
  return ['#0a84ff', '#5e5ce6', '#bf5af2', '#ff375f', '#ff9f0a', '#30d158', '#64d2ff', '#ac8e68'][h % 8];
}

// ---- editar / crear ----

function FieldEditor({ f, onChange, onRemove, onGenerate, onQr }) {
  const th = useTheme();
  const [show, setShow] = useState(false);
  const hidden = HIDDEN.has(f.t) && f.t !== 'totp';
  const set = (val) => onChange({ ...f, v: val, keep: false });
  let input;
  if (f.t === 'bool') input = <Toggle on={f.v === 'true'} onChange={(on) => set(on ? 'true' : 'false')} />;
  else if (f.choices) {
    const opts = f.choices.map((c) => [c, f.k === 'doc_type' ? t(`passwords.doc.${c}`) : f.k === 'security' && c === 'none' ? t('passwords.wifi.open') : c]);
    input = <Choice value={f.v} options={opts} onChange={set} width={240} />;
  } else if (f.multiline || f.t === 'multiline') {
    input = <View style={{ flex: 1 }}>{f.keep ? <Button label={t('passwords.edit.replaceSecret')} onClick={() => set('')} /> : <TextArea value={f.v || ''} onChange={set} mono={hidden} secure={hidden && !show} minHeight={70} />}</View>;
  } else if (f.keep) {
    input = <View style={{ flex: 1, flexDirection: 'row', gap: 6, alignItems: 'center' }}>
      <Text style={{ flex: 1, fontSize: 13, color: th.textDim }}>{t('passwords.edit.keptSecret')}</Text>
      <Button label={t('passwords.edit.replaceSecret')} onClick={() => set('')} />
    </View>;
  } else {
    input = <TextInput value={f.v || ''} onChange={set} secure={hidden && !show} placeholder={f.t === 'totp' ? t('passwords.totp.placeholder') : f.t === 'date' ? t('passwords.edit.datePh') : ''} style={{ flex: 1 }} />;
  }
  return (
    <View style={{ gap: 4 }}>
      <View style={{ flexDirection: 'row', alignItems: 'center' }}>
        <View style={{ flex: 1 }}>{f.custom
          ? <TextInput value={f.l || ''} onChange={(l) => onChange({ ...f, l })} placeholder={t('passwords.edit.fieldName')} size={11} style={{ height: 24, width: 200 }} />
          : <Label>{fieldLabel(f)}</Label>}</View>
        {onRemove && <Text onClick={onRemove} style={{ fontSize: 11, color: th.textDim }}>{t('passwords.edit.removeField')}</Text>}
      </View>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
        {input}
        {hidden && !f.keep && !f.multiline && <IconButton glyph={show ? '◉' : '👁'} onClick={() => setShow(!show)} />}
        {f.k === 'password' && !f.keep && <IconButton glyph="⚄" title={t('passwords.edit.generate')} onClick={onGenerate} />}
        {f.t === 'totp' && <IconButton glyph="▦" title={t('passwords.totp.fromQr')} onClick={onQr} />}
      </View>
    </View>
  );
}

const fieldsFor = (cat, cats) => templateFields(cat, cats).map((f) => ({ ...f, v: f.t === 'bool' ? 'false' : f.choices ? f.choices[0] : '' }));

export function EntryEditor({ v, entry, catId, onDone, onCancel }) {
  const th = useTheme();
  const st = useStore(app);
  const cats = allCategories(st.cats);
  const isNew = !entry;
  const [cat, setCat] = useState(entry?.cat || catId || 'internet');
  const [title, setTitle] = useState(entry?.title || '');
  const [notes, setNotes] = useState(entry?.notes || '');
  const [tags, setTags] = useState(entry?.tags || []);
  const [tagIn, setTagIn] = useState('');
  const [rotate, setRotate] = useState(entry?.rotate || 0);
  const [fields, setFields] = useState(() => {
    if (!entry) return fieldsFor(cats.find((c) => c.id === (catId || 'internet')), cats);
    const tpl = templateFields(cats.find((c) => c.id === entry.cat), cats);
    return entry.f.map((f) => {
      const tf = tpl.find((x) => x.k === f.k);
      return { ...f, multiline: tf?.multiline, choices: tf?.choices, keep: HIDDEN.has(f.t) && f.has && f.v == null, v: f.v ?? '', custom: !tf && !!f.l };
    });
  });
  const [gen, setGen] = useState(null);       // índice del campo que recibe la contraseña generada
  const [qrFor, setQrFor] = useState(null);
  const [adding, setAdding] = useState(false);
  const [newType, setNewType] = useState('text');
  const [err, setErr] = useState('');
  const [busy, setBusy] = useState(false);
  const [strength, setStrength] = useState(null);
  const pw = fields.find((f) => f.k === 'password' && !f.keep)?.v || '';
  useEffect(() => {
    if (!pw) { setStrength(null); return; }
    let live = true;
    req('strength', { password: pw }).then((r) => live && setStrength(r)).catch(() => {});
    return () => { live = false; };
  }, [pw]);
  const changeCat = (c) => {
    setCat(c);
    if (isNew && !fields.some((f) => f.v)) setFields(fieldsFor(cats.find((x) => x.id === c), cats));
  };
  const setField = (i, nf) => setFields((l) => l.map((f, k) => (k === i ? nf : f)));
  const addField = () => {
    const n = fields.filter((f) => /^c\d+$/.test(f.k)).length + 1;
    let k = `c${n}`;
    while (fields.some((f) => f.k === k)) k = `c${Number(k.slice(1)) + 1}`;
    setFields((l) => [...l, { k, t: newType === 'multiline' ? 'text' : newType, multiline: newType === 'multiline', l: '', v: '', custom: true }]);
    setAdding(false);
  };
  const addTag = () => {
    const tg = tagIn.trim().replace(/^#/, '');
    if (tg && !tags.includes(tg)) setTags([...tags, tg]);
    setTagIn('');
  };
  const save = async () => {
    setBusy(true); setErr('');
    const out = {
      cat, title: title.trim(), notes, tags, rotate,
      f: fields.filter((f) => f.keep || String(f.v || '').length || f.custom).map((f) => (f.keep ? { k: f.k, t: f.t, l: f.l || undefined, keep: true }
        : { k: f.k, t: f.multiline && !HIDDEN.has(f.t) ? 'multiline' : f.t, l: f.custom ? (f.l || t('passwords.field.custom')) : undefined, v: String(f.v ?? '') })),
    };
    if (entry) { out.id = entry.id; out.fav = entry.fav; out.arch = entry.arch; }
    try {
      const r = await req('save', { v, entry: out });
      upsertItem(r.item);
      onDone(r.id);
    } catch (e) { setErr(errorText(e.code)); }
    setBusy(false);
  };
  const b = builtinById(cat) || builtinById(cats.find((c) => c.id === cat)?.template || '');
  const catOpts = cats.map((c) => [c.id, `${c.icon || builtinById(c.id)?.icon || '📁'}  ${catName(c)}`]);
  return (
    <View style={{ flex: 1 }}>
      <ScrollView style={{ flex: 1 }}>
        <View style={{ padding: 22, gap: 12 }}>
          <Text style={{ fontSize: 18, fontWeight: 700, color: th.text }}>{isNew ? t('passwords.edit.new') : t('passwords.edit.editing')}</Text>
          <View style={{ flexDirection: 'row', gap: 10, alignItems: 'flex-end' }}>
            <View style={{ flex: 1, gap: 4 }}>
              <Label>{t('passwords.edit.title')}</Label>
              <TextInput value={title} onChange={setTitle} autoFocus={isNew} placeholder={t('passwords.edit.titlePh')} />
            </View>
            <View style={{ gap: 4 }}>
              <Label>{t('passwords.edit.category')}</Label>
              <Choice value={cat} options={catOpts} onChange={changeCat} width={220} />
            </View>
          </View>
          {b?.warning && <Warning>{t('passwords.crypto.warning')}</Warning>}
          {fields.map((f, i) => (
            <FieldEditor key={`${f.k}-${i}`} f={f} onChange={(nf) => setField(i, nf)}
              onRemove={f.custom || !templateFields(cats.find((c) => c.id === cat), cats).some((x) => x.k === f.k) ? () => setFields((l) => l.filter((_, k) => k !== i)) : null}
              onGenerate={() => setGen(i)} onQr={() => setQrFor(i)} />
          ))}
          {strength && <StrengthBar score={strength.score} bits={strength.bits} />}
          {strength?.common && <Text style={{ fontSize: 12, color: RED }}>{t('passwords.report.commonHint')}</Text>}
          {adding ? (
            <View style={{ flexDirection: 'row', gap: 8, alignItems: 'center' }}>
              <Choice value={newType} options={FIELD_TYPES.map((x) => [x, t(`passwords.ftype.${x}`)])} onChange={setNewType} width={180} />
              <Button primary label={t('passwords.edit.addField')} onClick={addField} />
              <Button label={t('passwords.cancel')} onClick={() => setAdding(false)} />
            </View>
          ) : <Button label={t('passwords.edit.addFieldDots')} onClick={() => setAdding(true)} style={{ alignSelf: 'flex-start' }} />}
          <View style={{ gap: 4 }}>
            <Label>{t('passwords.edit.tags')}</Label>
            <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 5, alignItems: 'center' }}>
              {tags.map((tg) => <Chip key={tg} label={`#${tg}`} color={tagColor(tg)} onRemove={() => setTags(tags.filter((x) => x !== tg))} />)}
              <TextInput value={tagIn} onChange={setTagIn} placeholder={t('passwords.edit.tagPh')} onSubmit={addTag} style={{ width: 160, height: 26 }} size={12} />
            </View>
          </View>
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
            <Text style={{ flex: 1, fontSize: 13, color: th.text }}>{t('passwords.rotate.label')}</Text>
            <Choice value={rotate} width={180} onChange={setRotate} options={[[0, t('passwords.rotate.never')], [30, t('passwords.rotate.days', { n: 30 })],
              [60, t('passwords.rotate.days', { n: 60 })], [90, t('passwords.rotate.days', { n: 90 })], [180, t('passwords.rotate.days', { n: 180 })], [365, t('passwords.rotate.year')]]} />
          </View>
          <View style={{ gap: 4 }}>
            <Label>{t('passwords.field.notes')}</Label>
            <TextArea value={notes} onChange={setNotes} placeholder={t('passwords.edit.notesPh')} minHeight={b?.notesFirst ? 180 : 80} />
            <Hint>{t('passwords.edit.markdown')}</Hint>
          </View>
          {isNew && b?.attachments && <Hint>{t('passwords.edit.attachLater')}</Hint>}
          {err ? <Text style={{ fontSize: 12, color: RED }}>{err}</Text> : null}
        </View>
      </ScrollView>
      <View style={{ flexDirection: 'row', justifyContent: 'flex-end', gap: 8, padding: 12, borderTop: `1px solid ${th.separator}` }}>
        <Button label={t('passwords.cancel')} onClick={onCancel} />
        <Button primary label={busy ? t('passwords.saving') : t('passwords.save')} onClick={save} />
      </View>
      {gen != null && <GeneratorSheet onClose={() => setGen(null)} onUse={(p) => setField(gen, { ...fields[gen], v: p, keep: false })} />}
      {qrFor != null && <FilePicker title={t('passwords.totp.fromQr')} filter={(n) => /\.(png|jpe?g)$/i.test(n)} onClose={() => setQrFor(null)}
        onPick={async (p) => {
          const i = qrFor; setQrFor(null);
          try {
            const r = await req('qr-scan', {}, fileReq([p, 'r']));
            if (!r.totp) { toast(t('passwords.totp.notOtp')); return; }
            setField(i, { ...fields[i], v: r.text, keep: false });
            if (!title && r.issuer) setTitle(r.issuer);
            toast(t('passwords.totp.added', { issuer: r.issuer || r.account || '' }));
          } catch (e) { toast(errorText(e.code)); }
        }} />}
    </View>
  );
}

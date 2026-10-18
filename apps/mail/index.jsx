// Mail — a native mail app in the spirit of a classic three-pane client: accounts and folders,
// the message list with previews (conversations grouped), and the reading pane. Everything comes
// from the sandboxed helper lord-maild (docs/MAIL.md): IMAP with IDLE, SMTP, a local cache with
// an offline search index, and HTML turned into our own views without running anything.
import {
  View, Text, useTheme, useStore, useAppWindow, useKeyboard, Button, TextInput, ScrollView, t, wm, fs,
  mailRequest, onMailEvent, mailStatus, mailConfig, mailAccounts, mailAvailable, contactName, getLayout, addListener, clipboard,
} from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';
import { HtmlView, TextView } from './HtmlView.jsx';
import { Compose } from './Compose.jsx';
import { AccountSetup, AccountsPanel, CertDialog, PasswordPrompt, Sheet } from './Setup.jsx';
import { CATEGORY_COLORS, categoryName, listDate, fullDate, folderIcon, folderName, addrShort, formatSize, openPath, revealPath } from './util.js';
import { quote } from './md.js';

const ROW = 76;

// ---- icon: an envelope on a blue tile (our own drawing) ----

export function Icon({ size: s }) {
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(165deg, #3fa9ff, #0a5fd0)', overflow: 'hidden',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)` }}>
      <View style={{ position: 'absolute', left: s * 0.16, top: s * 0.27, width: s * 0.68, height: s * 0.46, borderRadius: s * 0.06, backgroundColor: '#ffffff',
        boxShadow: `0 ${s * 0.02}px ${s * 0.05}px rgba(0,0,0,0.25)` }} />
      {/* the flap: two stacked bands narrowing to a point */}
      {[0, 1, 2, 3, 4].map((i) => (
        <View key={i} style={{ position: 'absolute', left: s * (0.2 + i * 0.06), top: s * (0.31 + i * 0.045), width: s * (0.6 - i * 0.12), height: Math.max(1, s * 0.04),
          borderRadius: s * 0.02, backgroundColor: i === 4 ? '#0a5fd0' : '#cfe6ff' }} />
      ))}
    </View>
  );
}

// ---- the list ----

function useSize(ref, every = 250) {
  const [sz, setSz] = useState({ w: 400, h: 400 });
  useEffect(() => {
    const id = setInterval(() => {
      if (!ref.current) return;
      const [, , w, h] = getLayout(ref.current.id);
      setSz((o) => (Math.abs(o.w - w) > 1 || Math.abs(o.h - h) > 1 ? { w, h } : o));
    }, every);
    return () => clearInterval(id);
  }, []);
  return sz;
}

function Row({ it, selected, onClick, onRightClick, cfg }) {
  const th = useTheme();
  if (!it) return <View style={{ height: ROW }} />;
  const unread = it.unreadN !== undefined ? it.unreadN > 0 : !it.seen;
  const fg = selected ? '#fff' : th.text, dim = selected ? '#ffffffcc' : th.textDim;
  return (
    <View onClick={onClick} onContextMenu={onRightClick} style={{ height: ROW, flexDirection: 'row', paddingHorizontal: 10, paddingVertical: 8, gap: 8,
      backgroundColor: selected ? th.accent : 'transparent' }}>
      <View style={{ width: 8, alignItems: 'center', paddingTop: 5 }}>
        {unread ? <View style={{ width: 8, height: 8, borderRadius: 4, backgroundColor: selected ? '#fff' : th.accent }} /> : null}
      </View>
      <View style={{ flex: 1, gap: 2, overflow: 'hidden' }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
          <Text style={{ flex: 1, fontSize: 13, fontWeight: unread ? 700 : 500, color: fg, whiteSpace: 'nowrap', overflow: 'hidden' }}>{it.from || it.email || t('mail.unknownSender')}</Text>
          {it.n > 1 ? <View style={{ paddingHorizontal: 5, borderRadius: 7, backgroundColor: selected ? '#ffffff33' : th.control }}><Text style={{ fontSize: 10, color: fg }}>{String(it.n)}</Text></View> : null}
          {it.att ? <Text style={{ fontSize: 11, color: dim }}>📎</Text> : null}
          {(it.anyFlag || it.flagged) ? <Text style={{ fontSize: 11, color: selected ? '#fff' : '#ff9f0a' }}>⚑</Text> : null}
          <Text style={{ fontSize: 11, color: dim }}>{listDate(it.date)}</Text>
        </View>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 4 }}>
          {(it.labels || []).map((l) => <View key={l} style={{ width: 8, height: 8, borderRadius: 4, backgroundColor: CATEGORY_COLORS[l - 1] }} />)}
          {it.answered ? <Text style={{ fontSize: 11, color: dim }}>↩</Text> : null}
          <Text style={{ flex: 1, fontSize: 12, fontWeight: unread ? 600 : 400, color: fg, whiteSpace: 'nowrap', overflow: 'hidden' }}>{it.subject || t('mail.noSubject')}</Text>
        </View>
        <Text style={{ fontSize: 12, color: dim, overflow: 'hidden', height: 30, lineHeight: 15 }}>{it.preview}</Text>
      </View>
      {cfg ? null : null}
    </View>
  );
}

// A virtual list: only the visible rows exist; rows load in windows from lord-maild as you scroll.
function MessageList({ src, rev, selected, onSelect, onMenu, cfg }) {
  const th = useTheme();
  const box = useRef(null);
  const { h } = useSize(box);
  const [y, setY] = useState(0);
  const [data, setData] = useState({ total: 0, items: new Map(), ver: null, loading: true });
  const want = useRef(null);
  const srcKey = JSON.stringify(src);
  useEffect(() => { setY(0); setData({ total: 0, items: new Map(), ver: null, loading: true }); }, [srcKey]);
  const first = Math.max(0, Math.floor(y / ROW) - 10), last = Math.ceil((y + h) / ROW) + 20;
  useEffect(() => {
    let live = true;
    const key = `${srcKey}|${first}|${rev}`;
    want.current = key;
    const run = async () => {
      try {
        let r;
        if (src.kind === 'search') r = await mailRequest('search', { q: src.q, count: 1000 });
        else r = await mailRequest('list', { ...(src.kind === 'unified' ? { folder: '*inbox' } : { acct: src.acct, folder: src.folder }), threaded: true,
          offset: first, count: last - first, filter: src.filter || 'all', ...(src.label ? { label: src.label } : {}) });
        if (!live || want.current !== key) return;
        setData((old) => {
          const items = src.kind === 'search' || old.ver !== r.version ? new Map() : new Map(old.items);
          const off = src.kind === 'search' ? 0 : first;
          r.items.forEach((it, k) => items.set(off + k, it));
          return { total: r.total, items, ver: r.version, loading: false };
        });
      } catch (e) { if (live) setData((o) => ({ ...o, loading: false, error: e.code })); }
    };
    const missing = (() => { for (let i = first; i < Math.min(last, data.total || last); i++) if (!data.items.has(i)) return true; return data.ver === null; })();
    if (missing || rev !== data.rev) run();
    return () => { live = false; };
  }, [srcKey, first, rev]);
  const total = data.total;
  const max = Math.max(0, total * ROW - h);
  const yy = Math.min(y, max);
  const rows = [];
  for (let i = Math.max(0, Math.floor(yy / ROW) - 2); i < Math.min(total, Math.ceil((yy + h) / ROW) + 2); i++) {
    const it = data.items.get(i);
    const key = it ? `${it.acct}|${it.folder}|${it.uid}` : `ph${i}`;
    rows.push(
      <View key={key} style={{ position: 'absolute', left: 0, right: 0, top: i * ROW - yy }}>
        <Row it={it} cfg={cfg} selected={!!it && selected === key} onClick={() => it && onSelect(it, i)} onRightClick={(e) => it && onMenu(it, e)} />
        <View style={{ height: 1, marginLeft: 26, backgroundColor: th.separator, marginTop: -1 }} />
      </View>,
    );
  }
  const bar = total * ROW > h ? Math.max(28, (h * h) / (total * ROW)) : 0;
  const drag = useRef(null);
  useEffect(() => {
    const offMove = addListener('pointermove', (e) => { if (drag.current) setY(Math.max(0, Math.min(max, drag.current.y0 + ((e.y - drag.current.py) * total * ROW) / Math.max(1, h)))); });
    const offUp = addListener('pointerup', () => { drag.current = null; });
    return () => { offMove(); offUp(); };
  });
  // keyboard selection needs the neighbours
  MessageList.neighbour = (key, d) => {
    for (const [i, it] of data.items) if (`${it.acct}|${it.folder}|${it.uid}` === key) {
      const n = data.items.get(i + d);
      if (n) { const ny = (i + d) * ROW; if (ny < yy) setY(ny); else if (ny + ROW > yy + h) setY(ny + ROW - h); }
      return n ? { it: n, i: i + d } : null;
    }
    return null;
  };
  return (
    <View ref={box} onWheel={(e) => setY((v) => Math.max(0, Math.min(max, v + e.deltaY)))} style={{ flex: 1, overflow: 'hidden' }}>
      {rows}
      {!total && !data.loading ? (
        <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', padding: 20 }}>
          <Text style={{ fontSize: 13, color: th.textDim, textAlign: 'center' }}>{src.kind === 'search' ? t('mail.search.none') : t('mail.empty')}</Text>
        </View>
      ) : null}
      {bar > 0 && <View onMouseDown={(e) => { drag.current = { py: e.y, y0: yy }; }} style={{ position: 'absolute', right: 2, width: 6, borderRadius: 3, height: bar,
        top: (h - bar) * (yy / (max || 1)), backgroundColor: th.mode === 'dark' ? 'rgba(255,255,255,0.3)' : 'rgba(0,0,0,0.28)' }} />}
    </View>
  );
}

// ---- the reading pane ----

function Banner({ color, children }) {
  return <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8, padding: 8, paddingHorizontal: 12, borderRadius: 8, backgroundColor: color }}>{children}</View>;
}

function Invite({ msg }) {
  const th = useTheme();
  const [inv, setInv] = useState(null);
  const [done, setDone] = useState('');
  const [busy, setBusy] = useState(false);
  useEffect(() => {
    let live = true;
    setInv(null); setDone('');
    mailRequest('invite', { acct: msg.acct, folder: msg.folder, uid: msg.uid }).then((r) => { if (live) setInv(r); }).catch(() => {});
    return () => { live = false; };
  }, [msg.uid, msg.folder]);
  if (!inv) return null;
  const ev = inv.event;
  const when = ev.allDay ? fullDate(ev.start).split(' ').slice(0, -1).join(' ') : `${fullDate(ev.start)} – ${new Date(ev.end).toTimeString().slice(0, 5)}`;
  const answer = async (ps) => {
    setBusy(true);
    try { await mailRequest('invite-respond', { acct: msg.acct, folder: msg.folder, uid: msg.uid, partstat: ps }); setDone(ps); } catch (e) { setDone(`error:${e.message}`); }
    setBusy(false);
  };
  const status = done || inv.myStatus || '';
  const isReply = inv.method === 'REPLY', isCancel = inv.method === 'CANCEL';
  return (
    <View style={{ padding: 12, borderRadius: 10, backgroundColor: th.control, gap: 6 }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
        <View style={{ width: 44, height: 48, borderRadius: 8, backgroundColor: '#fff', overflow: 'hidden', alignItems: 'center' }}>
          <View style={{ height: 14, alignSelf: 'stretch', backgroundColor: '#ff453a', alignItems: 'center', justifyContent: 'center' }}>
            <Text style={{ fontSize: 9, color: '#fff', fontWeight: 700 }}>{new Date(ev.start).toLocaleDateString(undefined, { month: 'short' }).toUpperCase()}</Text>
          </View>
          <Text style={{ fontSize: 20, fontWeight: 700, color: '#222' }}>{String(new Date(ev.start).getDate())}</Text>
        </View>
        <View style={{ flex: 1, gap: 2 }}>
          <Text style={{ fontSize: 14, fontWeight: 700, color: th.text }}>{ev.title || t('mail.invite.untitled')}</Text>
          <Text style={{ fontSize: 12, color: th.textDim }}>{when}</Text>
          {ev.loc ? <Text style={{ fontSize: 12, color: th.textDim }}>{`📍 ${ev.loc}`}</Text> : null}
          {ev.org ? <Text style={{ fontSize: 12, color: th.textDim }}>{t('mail.invite.organizer', { name: ev.org.name || ev.org.email })}</Text> : null}
        </View>
      </View>
      {isReply ? <Text style={{ fontSize: 12, color: th.textDim }}>{t('mail.invite.reply')}</Text>
        : isCancel ? <Text style={{ fontSize: 12, color: '#ff453a' }}>{t('mail.invite.cancelled')}</Text>
          : (
            <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
              <Button primary={status !== 'ACCEPTED'} label={status === 'ACCEPTED' ? `✓ ${t('mail.invite.accepted')}` : t('mail.invite.accept')} disabled={busy} onClick={() => answer('ACCEPTED')} />
              <Button label={status === 'TENTATIVE' ? `✓ ${t('mail.invite.maybe')}` : t('mail.invite.maybe')} disabled={busy} onClick={() => answer('TENTATIVE')} />
              <Button destructive label={status === 'DECLINED' ? `✓ ${t('mail.invite.declined')}` : t('mail.invite.decline')} disabled={busy} onClick={() => answer('DECLINED')} />
              {inv.inCalendar || done === 'ACCEPTED' || done === 'TENTATIVE' ? <Text onClick={() => wm.open('calendar', { args: { at: ev.start, uid: ev.uid, nonce: Date.now() } })} style={{ fontSize: 12, color: th.accent }}>{t('mail.invite.show')}</Text> : null}
            </View>
          )}
      {done.startsWith('error:') ? <Text style={{ fontSize: 12, color: '#ff453a' }}>{done.slice(6)}</Text> : null}
    </View>
  );
}

function Attachments({ msg }) {
  const th = useTheme();
  const [busy, setBusy] = useState(null);
  const atts = (msg.atts || []).filter((a) => !(a.inline && a.cid && msg.html));
  if (!atts.length) return null;
  const fetch = async (a) => {
    const r = await mailRequest('attachment', { acct: msg.acct, folder: msg.folder, uid: msg.uid, idx: a.idx });
    return `${lord.mail.cacheDir()}/${r.path}`;
  };
  const open = async (a) => { setBusy(a.idx); try { openPath(await fetch(a), a.name); } catch (e) { /* shown in the row */ } setBusy(null); };
  const save = async (a) => {
    setBusy(a.idx);
    try {
      const p = await fetch(a);
      const dir = ['Descargas', 'Downloads', 'Transferências'].map((d) => `${fs.home()}/${d}`).find((d) => { try { return fs.stat(d); } catch (e) { return false; } }) || fs.home();
      let dst = `${dir}/${a.name}`;
      for (let k = 2; k < 100; k++) { let ex = false; try { ex = !!fs.stat(dst); } catch (e) { ex = false; } if (!ex) break; dst = `${dir}/${a.name.replace(/(\.[^.]*)?$/, ` (${k})$1`)}`; }
      fs.copy(p, dst);
      setTimeout(() => revealPath(dst), 300);
    } catch (e) { /* ignore */ }
    setBusy(null);
  };
  return (
    <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8 }}>
      {atts.map((a) => (
        <View key={a.idx} style={{ flexDirection: 'row', alignItems: 'center', gap: 8, padding: 8, borderRadius: 8, backgroundColor: th.control, maxWidth: 300 }}>
          <Text style={{ fontSize: 20 }}>{/pdf/.test(a.type) ? '📕' : /^image/.test(a.type) ? '🖼️' : /calendar|ics/.test(a.type + a.name) ? '📅' : '📄'}</Text>
          <View style={{ flex: 1 }}>
            <Text onClick={() => open(a)} style={{ fontSize: 12, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{a.name}</Text>
            <Text style={{ fontSize: 11, color: th.textDim }}>{busy === a.idx ? t('mail.working') : formatSize(a.size)}</Text>
          </View>
          <Button small plain label={t('mail.att.open')} onClick={() => open(a)} />
          <Button small plain label={t('mail.att.save')} onClick={() => save(a)} />
        </View>
      ))}
    </View>
  );
}

function MessageView({ msg, width, onLink, cfg, onTrustSender, collapsed, onExpand }) {
  const th = useTheme();
  const [remote, setRemote] = useState(false);
  const sender = msg.from?.[0]?.email?.toLowerCase() || '';
  const trusted = (cfg.trustedSenders || []).includes(sender);
  useEffect(() => { setRemote(false); }, [msg.uid, msg.folder]);
  const showRemote = remote || trusted;
  useEffect(() => { if (showRemote && msg.html?.remote?.length) lord.mail.allowImages(msg.html.remote); }, [showRemote, msg.uid]);
  const from = msg.from?.[0] || {};
  const nRemote = msg.html?.stats?.remote || 0, nTrackers = msg.html?.stats?.trackers || 0;
  const warns = (msg.html?.links || []).filter((l) => l.w);
  if (collapsed) {
    return (
      <View onClick={onExpand} style={{ padding: 10, borderRadius: 8, backgroundColor: th.control, flexDirection: 'row', gap: 8 }}>
        <Text style={{ fontSize: 13, fontWeight: 600, color: th.text }}>{from.name || from.email}</Text>
        <Text style={{ flex: 1, fontSize: 12, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{(msg.text || '').slice(0, 140).replace(/\s+/g, ' ')}</Text>
        <Text style={{ fontSize: 11, color: th.textDim }}>{listDate(msg.date)}</Text>
      </View>
    );
  }
  return (
    <View style={{ gap: 10 }}>
      <View style={{ flexDirection: 'row', gap: 10, alignItems: 'flex-start' }}>
        <View style={{ width: 40, height: 40, borderRadius: 20, backgroundColor: th.accent, alignItems: 'center', justifyContent: 'center' }}>
          <Text style={{ fontSize: 16, fontWeight: 700, color: '#fff' }}>{([...(from.name || from.email || '?')][0] || '?').toUpperCase()}</Text>
        </View>
        <View style={{ flex: 1, gap: 2 }}>
          <Text style={{ fontSize: 14, fontWeight: 700, color: th.text }}>{from.name || from.email}</Text>
          {from.name ? <Text style={{ fontSize: 11, color: th.textDim }}>{from.email}</Text> : null}
          <Text style={{ fontSize: 12, color: th.textDim }}>{`${t('mail.to')}: ${(msg.to || []).map(addrShort).join(', ')}`}</Text>
          {msg.cc?.length ? <Text style={{ fontSize: 12, color: th.textDim }}>{`${t('mail.cc')}: ${msg.cc.map(addrShort).join(', ')}`}</Text> : null}
        </View>
        <Text style={{ fontSize: 11, color: th.textDim }}>{fullDate(msg.date)}</Text>
      </View>
      {nRemote > 0 && !showRemote && (
        <Banner color={th.control}>
          <Text style={{ fontSize: 12, color: th.text, flex: 1 }}>{t('mail.images.blocked', { n: nRemote })}{nTrackers ? ` ${t('mail.images.trackers', { n: nTrackers })}` : ''}</Text>
          <Button small label={t('mail.images.load')} onClick={() => setRemote(true)} />
          <Button small plain label={t('mail.images.always')} onClick={() => { onTrustSender(sender); setRemote(true); }} />
        </Banner>
      )}
      {warns.length > 0 && (
        <Banner color="#ff9f0a33">
          <Text style={{ fontSize: 12, color: th.text, flex: 1 }}>{`⚠ ${t('mail.links.warning', { n: warns.length })}`}</Text>
        </Banner>
      )}
      {msg.invite ? <Invite msg={msg} /> : null}
      <Attachments msg={msg} />
      {msg.html ? <HtmlView doc={msg.html} msg={{ acct: msg.acct, folder: msg.folder, uid: msg.uid }} remote={showRemote} width={width} onLink={onLink} />
        : <TextView text={msg.text} onLink={onLink} width={width} />}
      {msg.unsubscribe ? <Text style={{ fontSize: 11, color: th.textDim }}>{t('mail.unsubscribeHint')}</Text> : null}
    </View>
  );
}

// ---- the window ----

export default function Mail() {
  const { args } = useAppWindow();
  if (args?.compose) return <Compose draft={args.compose} />;
  return <MailMain />;
}

function compose(draft) { wm.open('mail', { new: true, args: { compose: draft || {} } }); }

function replyDraft(msg, mode) {
  const acct = msg.acct;
  const me = (mailAccounts.get(acct)?.email || '').toLowerCase();
  const from = msg.replyTo?.length ? msg.replyTo : msg.from || [];
  const subj = msg.subject || '';
  const quoteHead = t('mail.quoteHead', { date: fullDate(msg.date), name: msg.from?.[0]?.name || msg.from?.[0]?.email || '' });
  const body = `\n\n${quoteHead}\n${quote(msg.text || '')}`;
  if (mode === 'forward') {
    const head = [`---------- ${t('mail.forwarded')} ----------`, `${t('mail.from')}: ${(msg.from || []).map((a) => (a.name ? `${a.name} <${a.email}>` : a.email)).join(', ')}`,
      `${t('mail.date')}: ${fullDate(msg.date)}`, `${t('mail.subject')}: ${subj}`, `${t('mail.to')}: ${(msg.to || []).map((a) => a.email).join(', ')}`].join('\n');
    return { acct, subject: /^(fwd?|rv|tr):/i.test(subj) ? subj : `Fwd: ${subj}`, body: `\n\n${head}\n\n${msg.text || ''}`, original: { acct, folder: msg.folder, uid: msg.uid, forward: true }, forwardAtts: msg };
  }
  const to = from.filter((a) => a.email.toLowerCase() !== me);
  const cc = mode === 'all' ? [...(msg.to || []), ...(msg.cc || [])].filter((a) => a.email.toLowerCase() !== me && !to.some((x) => x.email === a.email)) : [];
  const refs = [msg.refs, msg.msgid].filter(Boolean).join(' ').trim();
  return { acct, to: to.length ? to : from, cc, subject: /^re:/i.test(subj) ? subj : `Re: ${subj}`, body, inReplyTo: msg.msgid, references: refs,
    original: { acct, folder: msg.folder, uid: msg.uid } };
}

function MailMain() {
  const th = useTheme();
  const { id: wid, args, focused } = useAppWindow();
  const st = useStore(mailStatus);
  const [folders, setFolders] = useState([]);
  const [src, setSrc] = useState({ kind: 'unified' });
  const [listRev, setListRev] = useState(0);
  const [sel, setSel] = useState(null);                 // list item
  const [msg, setMsg] = useState(null);
  const [thread, setThread] = useState([]);
  const [expanded, setExpanded] = useState({});
  const [loadErr, setLoadErr] = useState('');
  const [q, setQ] = useState('');
  const [sheet, setSheet] = useState(null);             // 'setup' | 'accounts' | { cert } | { password } | { link } | { menu }
  const [cfg, setCfg] = useState(mailConfig.get());
  const readingRef = useRef(null);
  const { w: readW } = useSize(readingRef, 400);
  const accounts = cfg.accounts.filter((a) => a.kind === 'mail');

  const loadFolders = () => mailRequest('folders').then((r) => setFolders(r.accounts || [])).catch(() => {});
  useEffect(() => {
    loadFolders();
    let timer = null;
    const off = onMailEvent((ev) => {
      if (ev.ev === 'changed' || ev.ev === 'folders' || ev.ev === 'status' || ev.ev === 'ready') {
        if (!timer) timer = setTimeout(() => { timer = null; loadFolders(); setListRev((v) => v + 1); }, 400);
      }
    });
    return () => { off(); if (timer) clearTimeout(timer); };
  }, []);
  useEffect(() => mailStatus.subscribe(() => setCfg(mailConfig.get())), []);

  // opened from a notification or another app
  useEffect(() => {
    if (args?.acct && args?.folder) {
      setSrc({ kind: 'folder', acct: args.acct, folder: args.folder });
      if (args.uid) openItem({ acct: args.acct, folder: args.folder, uid: args.uid });
    }
  }, [args?.nonce]);

  const openItem = async (it) => {
    setSel(it);
    setLoadErr('');
    setExpanded({});
    try {
      const m = await mailRequest('open', { acct: it.acct, folder: it.folder, uid: it.uid });
      setMsg(m);
      if (it.n > 1 && it.tk) {
        const r = await mailRequest('thread', { acct: it.acct, tk: it.tk }).catch(() => ({ items: [] }));
        setThread(r.items || []);
      } else setThread([]);
      // marked as read after a moment, like other mail apps
      if (!m.seen) setTimeout(() => mailRequest('flag', { acct: it.acct, folder: it.folder, uids: [it.uid], add: ['seen'] }).catch(() => {}), 1200);
    } catch (e) { setMsg(null); setLoadErr(e.code === 'offline' ? t('mail.offlineNotCached') : e.message); }
  };

  const selKey = sel ? `${sel.acct}|${sel.folder}|${sel.uid}` : null;
  const targets = () => {
    if (!sel) return null;
    const list = sel.thread?.length ? sel.thread : [{ acct: sel.acct, folder: sel.folder, uid: sel.uid }];
    return { acct: sel.acct, folder: sel.folder, uids: list.filter((x) => x.folder === sel.folder && x.acct === sel.acct).map((x) => x.uid) };
  };
  const specialOf = (acct, sp) => folders.find((a) => a.id === acct)?.folders.find((f) => f.special === sp)?.name;
  const act = {
    reply: () => msg && compose(replyDraft(msg, 'reply')),
    replyAll: () => msg && compose(replyDraft(msg, 'all')),
    forward: async () => {
      if (!msg) return;
      const d = replyDraft(msg, 'forward');
      // the original's attachments go along (saved from the cache)
      const paths = [];
      for (const a of msg.atts || []) {
        try { const r = await mailRequest('attachment', { acct: msg.acct, folder: msg.folder, uid: msg.uid, idx: a.idx }); paths.push(`${lord.mail.cacheDir()}/${r.path}`); } catch (e) { /* skip */ }
      }
      delete d.forwardAtts;
      compose({ ...d, attachments: paths, noSignature: false });
    },
    archive: async () => {
      const tg = targets(); if (!tg) return;
      let to = specialOf(tg.acct, 'archive');
      if (!to) { await mailRequest('create-folder', { acct: tg.acct, name: 'Archive' }).catch(() => {}); to = 'Archive'; }
      if (to === tg.folder) return;
      await mailRequest('move', { ...tg, to }).catch(() => {});
      setSel(null); setMsg(null); setListRev((v) => v + 1);
    },
    remove: async () => {
      const tg = targets(); if (!tg) return;
      const next = MessageList.neighbour?.(selKey, 1);
      await mailRequest('delete', tg).catch(() => {});
      setListRev((v) => v + 1);
      if (next) openItem(next.it); else { setSel(null); setMsg(null); }
    },
    flag: async () => {
      const tg = targets(); if (!tg) return;
      const on = !(sel.anyFlag || sel.flagged);
      await mailRequest('flag', { ...tg, ...(on ? { add: ['flagged'] } : { remove: ['flagged'] }) }).catch(() => {});
      setSel({ ...sel, anyFlag: on, flagged: on });
    },
    unread: async () => {
      const tg = targets(); if (!tg) return;
      const unread = sel.unreadN !== undefined ? sel.unreadN > 0 : !sel.seen;
      await mailRequest('flag', { ...tg, ...(unread ? { add: ['seen'] } : { remove: ['seen'] }) }).catch(() => {});
    },
    label: async (l) => {
      const tg = targets(); if (!tg) return;
      const has = (sel.labels || []).includes(l);
      await mailRequest('flag', { ...tg, ...(has ? { remove: [`label${l}`] } : { add: [`label${l}`] }) }).catch(() => {});
      setSel({ ...sel, labels: has ? sel.labels.filter((x) => x !== l) : [...(sel.labels || []), l] });
    },
    junk: async () => {
      const tg = targets(); if (!tg) return;
      const to = specialOf(tg.acct, 'junk');
      if (to && to !== tg.folder) { await mailRequest('move', { ...tg, to }).catch(() => {}); setSel(null); setMsg(null); setListRev((v) => v + 1); }
    },
  };

  const onLink = (link) => {
    if (!link?.h) return;
    if (/^mailto:/i.test(link.h)) {
      const [addr, qs] = link.h.slice(7).split('?');
      const subj = /(?:^|&)subject=([^&]*)/i.exec(qs || '');
      compose({ to: [{ name: '', email: decodeURIComponent(addr) }], subject: subj ? decodeURIComponent(subj[1]) : '' });
      return;
    }
    setSheet({ link });
  };

  const trustSender = (email) => {
    if (!email) return;
    const c = mailConfig.get();
    mailConfig.save({ ...c, trustedSenders: [...new Set([...(c.trustedSenders || []), email])] });
    setCfg(mailConfig.get());
  };

  useKeyboard((e) => {
    if (!focused) return false;
    const m = e.ctrl || e.meta;
    const b = (e.base || '').toLowerCase();
    if (m && b === 'n') { compose(); return true; }
    if (m && b === 'r' && !e.shift) { act.reply(); return true; }
    if (m && b === 'r' && e.shift) { act.replyAll(); return true; }
    if (m && b === 'j') { act.forward(); return true; }
    if (m && b === 'e') { act.archive(); return true; }
    if (!m && (e.key === 'Delete' || (e.key === 'Backspace' && e.meta))) { act.remove(); return true; }
    if (!m && (e.key === 'ArrowDown' || e.key === 'ArrowUp') && selKey) { const n = MessageList.neighbour?.(selKey, e.key === 'ArrowDown' ? 1 : -1); if (n) openItem(n.it); return true; }
    return false;
  });

  useEffect(() => {
    wm.setMenus(wid, [
      { label: t('mail.menu.file'), items: [
        { label: t('mail.new'), hint: '❖N', onClick: () => compose() },
        { label: t('mail.menu.addAccount'), onClick: () => setSheet('setup') },
        { label: t('mail.menu.accounts'), onClick: () => setSheet('accounts') }, '-',
        { label: t('mail.menu.sync'), onClick: () => mailRequest('sync', {}).catch(() => {}) },
      ] },
      { label: t('mail.menu.message'), items: [
        { label: t('mail.reply'), hint: '❖R', onClick: act.reply, disabled: !msg },
        { label: t('mail.replyAll'), hint: '❖⇧R', onClick: act.replyAll, disabled: !msg },
        { label: t('mail.forward'), hint: '❖J', onClick: act.forward, disabled: !msg }, '-',
        { label: t('mail.archive'), hint: '❖E', onClick: act.archive, disabled: !sel },
        { label: t('mail.delete'), hint: '⌦', onClick: act.remove, disabled: !sel },
        { label: t('mail.junk'), onClick: act.junk, disabled: !sel },
        { label: t('mail.markUnread'), onClick: act.unread, disabled: !sel },
        { label: t('mail.flag'), onClick: act.flag, disabled: !sel },
      ] },
    ]);
  });

  // ---- sidebar ----
  const sidebar = (
    <View style={{ width: 220, backgroundColor: th.sidebarBg }}>
      <ScrollView style={{ flex: 1 }}>
        <View style={{ padding: 8, gap: 1 }}>
          <Text style={{ fontSize: 11, fontWeight: 600, color: th.textDim, margin: 6 }}>{t('mail.favorites')}</Text>
          <SideRow icon="📥" label={t('mail.unifiedInbox')} count={st.unread} selected={src.kind === 'unified' && !src.filter} onClick={() => { setSrc({ kind: 'unified' }); setQ(''); }} />
          <SideRow icon="🔵" label={t('mail.unread')} selected={src.kind === 'unified' && src.filter === 'unread'} onClick={() => setSrc({ kind: 'unified', filter: 'unread' })} />
          <SideRow icon="⚑" label={t('mail.flagged')} selected={src.kind === 'unified' && src.filter === 'flagged'} onClick={() => setSrc({ kind: 'unified', filter: 'flagged' })} />
          {folders.map((a) => {
            const status = st.accounts[a.id]?.status || a.status;
            const bad = st.authFailed[a.id] || status === 'auth' || status === 'cert' || status === 'offline';
            return (
              <View key={a.id} style={{ marginTop: 10, gap: 1 }}>
                <View style={{ flexDirection: 'row', alignItems: 'center', margin: 6, gap: 6 }}>
                  <Text style={{ flex: 1, fontSize: 11, fontWeight: 600, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{a.name}</Text>
                  {status === 'sync' || status === 'connecting' ? <Text style={{ fontSize: 10, color: th.textDim }}>⟳</Text> : null}
                  {bad ? <Text onClick={() => setSheet(st.authFailed[a.id] || status === 'auth' ? { password: a.id } : st.certs.find((c) => c.acct === a.id) ? { cert: st.certs.find((c) => c.acct === a.id) } : 'accounts')}
                    style={{ fontSize: 11, color: '#ff9f0a' }}>⚠</Text> : null}
                </View>
                {a.folders.filter((f) => !f.noselect).map((f) => (
                  <SideRow key={f.name} icon={folderIcon(f)} label={folderName(f)} count={f.special === 'drafts' ? f.total : f.special === 'sent' || f.special === 'trash' ? 0 : f.unread}
                    indent={f.delim && f.name.split(f.delim).length > 1 && f.special === '' ? 1 : 0}
                    selected={src.kind === 'folder' && src.acct === a.id && src.folder === f.name} onClick={() => { setSrc({ kind: 'folder', acct: a.id, folder: f.name }); setQ(''); }} />
                ))}
              </View>
            );
          })}
          <Text style={{ fontSize: 11, fontWeight: 600, color: th.textDim, margin: 6, marginTop: 12 }}>{t('mail.categories')}</Text>
          {CATEGORY_COLORS.map((c, i) => (
            <SideRow key={c} icon={<View style={{ width: 10, height: 10, borderRadius: 5, backgroundColor: c }} />} label={categoryName(i + 1, cfg)}
              selected={src.kind === 'unified' && src.label === i + 1} onClick={() => setSrc({ kind: 'unified', label: i + 1 })} />
          ))}
        </View>
      </ScrollView>
      <View style={{ flexDirection: 'row', gap: 6, padding: 8 }}>
        <Button small label={`＋ ${t('mail.addAccount')}`} onClick={() => setSheet('setup')} />
        <Button small plain label="⚙" onClick={() => setSheet('accounts')} />
      </View>
    </View>
  );

  const toolbar = (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6, paddingHorizontal: 10, height: 46 }}>
      <Button primary label={`✎ ${t('mail.new')}`} onClick={() => compose()} />
      <View style={{ width: 8 }} />
      <Button label={`↩ ${t('mail.reply')}`} disabled={!msg} onClick={act.reply} />
      <Button label={`↩︎↩︎ ${t('mail.replyAll')}`} disabled={!msg} onClick={act.replyAll} />
      <Button label={`↪ ${t('mail.forward')}`} disabled={!msg} onClick={act.forward} />
      <View style={{ width: 8 }} />
      <Button label={`🗄 ${t('mail.archive')}`} disabled={!sel} onClick={act.archive} />
      <Button label={`🗑 ${t('mail.delete')}`} disabled={!sel} onClick={act.remove} />
      <Button label={`⚑ ${t('mail.flag')}`} disabled={!sel} onClick={act.flag} />
      <Button label={`● ${t('mail.category')}`} disabled={!sel} onClick={() => setSheet({ menu: 'labels' })} />
      <View style={{ flex: 1 }} />
      <View style={{ width: 220 }}>
        <TextInput value={q} onChange={(v) => { setQ(v); if (v.trim().length >= 2) setSrc({ kind: 'search', q: v.trim() }); else if (!v.trim() && src.kind === 'search') setSrc({ kind: 'unified' }); }}
          placeholder={`🔍 ${t('mail.search.placeholder')}`} onCancel={() => { setQ(''); setSrc({ kind: 'unified' }); }} />
      </View>
    </View>
  );

  const listTitle = src.kind === 'search' ? t('mail.search.results', { q: src.q }) : src.kind === 'unified'
    ? (src.label ? categoryName(src.label, cfg) : src.filter === 'unread' ? t('mail.unread') : src.filter === 'flagged' ? t('mail.flagged') : t('mail.unifiedInbox'))
    : folderName(folders.find((a) => a.id === src.acct)?.folders.find((f) => f.name === src.folder) || { display: src.folder || '' });

  const noAccounts = !accounts.length;
  return (
    <View style={{ flex: 1, flexDirection: 'row', backgroundColor: th.windowBg }}>
      {sidebar}
      <View style={{ width: 1, backgroundColor: th.separator }} />
      <View style={{ flex: 1 }}>
        {toolbar}
        <View style={{ height: 1, backgroundColor: th.separator }} />
        <View style={{ flex: 1, flexDirection: 'row' }}>
          <View style={{ width: 330 }}>
            <View style={{ flexDirection: 'row', alignItems: 'center', paddingHorizontal: 12, height: 34 }}>
              <Text style={{ flex: 1, fontSize: 15, fontWeight: 700, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{listTitle}</Text>
            </View>
            <View style={{ height: 1, backgroundColor: th.separator }} />
            {!mailAvailable() ? <Text style={{ margin: 16, fontSize: 13, color: th.textDim }}>{t('mail.unavailable')}</Text>
              : noAccounts ? (
                <View style={{ margin: 18, gap: 10 }}>
                  <Text style={{ fontSize: 13, color: th.textDim }}>{t('mail.noAccounts')}</Text>
                  <Button primary label={t('mail.addAccount')} onClick={() => setSheet('setup')} />
                </View>
              ) : <MessageList src={src} rev={listRev} selected={selKey} onSelect={openItem} onMenu={() => setSheet({ menu: 'labels' })} cfg={cfg} />}
          </View>
          <View style={{ width: 1, backgroundColor: th.separator }} />
          <View ref={readingRef} style={{ flex: 1 }}>
            {msg ? (
              <ScrollView key={`${msg.acct}|${msg.folder}|${msg.uid}`} style={{ flex: 1 }}>
                <View style={{ padding: 18, gap: 12 }}>
                  <Text style={{ fontSize: 19, fontWeight: 700, color: th.text }}>{msg.subject || t('mail.noSubject')}</Text>
                  {(sel?.labels || []).length ? (
                    <View style={{ flexDirection: 'row', gap: 6 }}>
                      {sel.labels.map((l) => <View key={l} style={{ paddingHorizontal: 8, height: 18, borderRadius: 9, justifyContent: 'center', backgroundColor: `${CATEGORY_COLORS[l - 1]}44` }}><Text style={{ fontSize: 11, color: th.text }}>{categoryName(l, cfg)}</Text></View>)}
                    </View>
                  ) : null}
                  {thread.length > 1 ? thread.filter((x) => !(x.folder === msg.folder && x.uid === msg.uid)).map((x) => (
                    <ThreadItem key={`${x.folder}|${x.uid}`} it={x} expanded={expanded[`${x.folder}|${x.uid}`]} onToggle={() => setExpanded((e) => ({ ...e, [`${x.folder}|${x.uid}`]: !e[`${x.folder}|${x.uid}`] }))}
                      width={readW - 36} onLink={onLink} cfg={cfg} onTrustSender={trustSender} />
                  )) : null}
                  <MessageView msg={msg} width={readW - 36} onLink={onLink} cfg={cfg} onTrustSender={trustSender} />
                </View>
              </ScrollView>
            ) : (
              <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center' }}>
                <Text style={{ fontSize: 40, opacity: 0.3 }}>✉</Text>
                <Text style={{ fontSize: 13, color: th.textDim }}>{loadErr || t('mail.noSelection')}</Text>
              </View>
            )}
          </View>
        </View>
      </View>
      {sheet === 'setup' && <AccountSetup onClose={() => { setSheet(null); setCfg(mailConfig.get()); loadFolders(); }} />}
      {sheet === 'accounts' && <AccountsPanel onClose={() => { setSheet(null); setCfg(mailConfig.get()); loadFolders(); }} onAdd={() => setSheet('setup')} />}
      {sheet?.cert && <CertDialog cert={sheet.cert} onClose={() => setSheet(null)} />}
      {!sheet && st.certs[0] && <CertDialog cert={st.certs[0]} onClose={() => mailStatus.set((s) => ({ ...s, certs: s.certs.slice(1) }))} />}
      {sheet?.password && <PasswordPrompt acct={sheet.password} onClose={() => setSheet(null)} />}
      {sheet?.link && <LinkSheet link={sheet.link} onClose={() => setSheet(null)} />}
      {sheet?.menu === 'labels' && (
        <Sheet width={280}>
          <Text style={{ fontSize: 14, fontWeight: 700, color: th.text }}>{t('mail.category')}</Text>
          {CATEGORY_COLORS.map((c, i) => (
            <View key={c} onClick={() => { act.label(i + 1); setSheet(null); }} style={{ flexDirection: 'row', alignItems: 'center', gap: 8, padding: 6, borderRadius: 6 }}>
              <View style={{ width: 12, height: 12, borderRadius: 6, backgroundColor: c }} />
              <Text style={{ flex: 1, fontSize: 13, color: th.text }}>{categoryName(i + 1, cfg)}</Text>
              {(sel?.labels || []).includes(i + 1) ? <Text style={{ color: th.accent }}>✓</Text> : null}
            </View>
          ))}
          <View style={{ flexDirection: 'row', justifyContent: 'flex-end' }}><Button label={t('mail.close')} onClick={() => setSheet(null)} /></View>
        </Sheet>
      )}
    </View>
  );
}

function SideRow({ icon, label, count, selected, onClick, indent = 0 }) {
  const th = useTheme();
  return (
    <View onClick={onClick} style={{ flexDirection: 'row', alignItems: 'center', gap: 8, height: 26, paddingLeft: 8 + indent * 14, paddingRight: 8, borderRadius: 6,
      backgroundColor: selected ? th.accent : 'transparent' }}>
      {typeof icon === 'string' ? <Text style={{ fontSize: 13, width: 18, textAlign: 'center' }}>{icon}</Text> : <View style={{ width: 18, alignItems: 'center' }}>{icon}</View>}
      <Text style={{ flex: 1, fontSize: 13, color: selected ? '#fff' : th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{label}</Text>
      {count ? <Text style={{ fontSize: 11, fontWeight: 600, color: selected ? '#fff' : th.textDim }}>{String(count)}</Text> : null}
    </View>
  );
}

function ThreadItem({ it, expanded, onToggle, width, onLink, cfg, onTrustSender }) {
  const th = useTheme();
  const [m, setM] = useState(null);
  useEffect(() => { if (expanded && !m) mailRequest('open', { acct: it.acct, folder: it.folder, uid: it.uid }).then(setM).catch(() => {}); }, [expanded]);
  if (expanded && m) return <View style={{ gap: 6 }}><Text onClick={onToggle} style={{ fontSize: 11, color: th.accent }}>{t('mail.thread.collapse')}</Text><MessageView msg={m} width={width} onLink={onLink} cfg={cfg} onTrustSender={onTrustSender} /></View>;
  return (
    <View onClick={onToggle} style={{ padding: 10, borderRadius: 8, backgroundColor: th.control, flexDirection: 'row', gap: 8 }}>
      <Text style={{ fontSize: 13, fontWeight: 600, color: th.text }}>{it.from || it.email}</Text>
      <Text style={{ flex: 1, fontSize: 12, color: th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{it.preview}</Text>
      <Text style={{ fontSize: 11, color: th.textDim }}>{listDate(it.date)}</Text>
    </View>
  );
}

// A link: there is no web engine in LordOS, so the address is shown and can be copied. A link
// whose visible text names another site is called out.
function LinkSheet({ link, onClose }) {
  const th = useTheme();
  let host = '';
  try { host = link.h.replace(/^[a-z]+:\/\//i, '').split(/[/?#]/)[0]; } catch (e) { host = ''; }
  return (
    <Sheet width={500}>
      <Text style={{ fontSize: 16, fontWeight: 700, color: link.w ? '#ff453a' : th.text }}>{link.w ? t('mail.links.deceptiveTitle') : t('mail.links.title')}</Text>
      {link.w ? <Text style={{ fontSize: 13, color: th.text }}>{t('mail.links.deceptiveBody', { shown: link.txt || '', host })}</Text> : null}
      <View style={{ padding: 10, borderRadius: 8, backgroundColor: th.control }}><Text style={{ fontSize: 12, color: th.text, fontFamily: 'monospace' }}>{link.h}</Text></View>
      <Text style={{ fontSize: 12, color: th.textDim }}>{t('mail.links.noBrowser')}</Text>
      <View style={{ flexDirection: 'row', justifyContent: 'flex-end', gap: 8 }}>
        <Button label={t('mail.close')} onClick={onClose} />
        <Button primary={!link.w} label={t('mail.links.copy')} onClick={() => { clipboard.set(link.h); onClose(); }} />
      </View>
    </Sheet>
  );
}

export { contactName };

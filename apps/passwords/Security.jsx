// Security.jsx — el informe de seguridad (contraseñas débiles, repetidas y viejas; filtradas según
// la lista de comunes y, si el usuario lo elige, un archivo local de Have I Been Pwned: nunca por
// red) y la vista de todos los códigos 2FA con su cuenta regresiva.
import { View, Text, useTheme, useStore, Button, ScrollView, t } from '@lordos/sdk';
import { useEffect, useRef, useState } from 'react';
import { app, req, errorText, toast, copySecret } from './api.js';
import { allCategories, builtinById } from './templates.js';
import { EntryIcon, Hint, FilePicker, strengthColor, strengthLabel, RED, ORANGE, YELLOW, GREEN, IconButton, fileReq } from './ui.jsx';
import { Center } from './Detail.jsx';

function Card({ title, count, color, children, hint }) {
  const th = useTheme();
  return (
    <View style={{ gap: 6, padding: 14, borderRadius: 12, backgroundColor: th.control }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
        <View style={{ width: 10, height: 10, borderRadius: 5, backgroundColor: color }} />
        <Text style={{ flex: 1, fontSize: 14, fontWeight: 700, color: th.text }}>{title}</Text>
        <Text style={{ fontSize: 20, fontWeight: 700, color: count ? color : th.textDim }}>{String(count)}</Text>
      </View>
      {hint && <Hint>{hint}</Hint>}
      {children}
    </View>
  );
}

function Item({ e, cats, sub, onOpen }) {
  const th = useTheme();
  if (!e) return null;
  const cat = cats.find((c) => c.id === e.cat) || builtinById(e.cat);
  return (
    <View onClick={() => onOpen(e.id)} style={{ flexDirection: 'row', alignItems: 'center', gap: 10, paddingVertical: 5 }}>
      <EntryIcon entry={e} cat={cat} size={26} />
      <View style={{ flex: 1 }}>
        <Text style={{ fontSize: 13, color: th.text, whiteSpace: 'nowrap', overflow: 'hidden' }}>{e.title || t('passwords.untitled')}</Text>
        {sub && <Text style={{ fontSize: 11, color: th.textDim }}>{sub}</Text>}
      </View>
      <Text style={{ fontSize: 12, color: th.textDim }}>›</Text>
    </View>
  );
}

export function SecurityView({ onOpen }) {
  const th = useTheme();
  const st = useStore(app);
  const cats = allCategories(st.cats);
  const [rep, setRep] = useState(null);
  const [hibp, setHibp] = useState(null);
  const [picking, setPicking] = useState(false);
  const [busy, setBusy] = useState(false);
  const run = async (file) => {
    setBusy(true);
    try { setRep(await req('report', {}, file ? fileReq([file, 'r']) : undefined)); } catch (e) { toast(errorText(e.code)); }
    setBusy(false);
  };
  useEffect(() => { run(hibp); }, [st.entries.length]);
  const byId = new Map(st.entries.map((e) => [e.id, e]));
  if (!rep) return <Center text={t('passwords.report.loading')} />;
  const issues = rep.weak.length + rep.reused.reduce((a, g) => a + g.length, 0) + rep.old.length + (rep.breached?.length || 0);
  const total = st.entries.filter((e) => !e.trash).length;
  return (
    <ScrollView style={{ flex: 1 }}>
      <View style={{ padding: 22, gap: 12 }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 14 }}>
          <View style={{ width: 56, height: 56, borderRadius: 28, alignItems: 'center', justifyContent: 'center', backgroundColor: issues ? 'rgba(255,159,10,0.2)' : 'rgba(48,209,88,0.2)' }}>
            <Text style={{ fontSize: 28 }}>{issues ? '⚠️' : '✅'}</Text>
          </View>
          <View style={{ flex: 1, gap: 2 }}>
            <Text style={{ fontSize: 22, fontWeight: 700, color: th.text }}>{t('passwords.report.title')}</Text>
            <Text style={{ fontSize: 12, color: th.textDim }}>{issues ? t('passwords.report.issues', { count: issues, total }) : t('passwords.report.allGood', { total })}</Text>
          </View>
          <Button label={busy ? t('passwords.report.loading') : t('passwords.report.again')} onClick={() => run(hibp)} />
        </View>
        <Card title={t('passwords.report.weak')} count={rep.weak.length} color={RED} hint={t('passwords.report.weakHint')}>
          {rep.weak.map((w) => <Item key={w.id} e={byId.get(w.id)} cats={cats} onOpen={onOpen}
            sub={w.common ? t('passwords.report.common') : `${strengthLabel(w.score)} · ${t('passwords.report.bits', { bits: w.bits })}`} />)}
        </Card>
        <Card title={t('passwords.report.reused')} count={rep.reused.length} color={ORANGE} hint={t('passwords.report.reusedHint')}>
          {rep.reused.map((g, i) => (
            <View key={i} style={{ gap: 2, paddingVertical: 4, borderBottom: `1px solid ${th.separator}` }}>
              <Text style={{ fontSize: 11, color: th.textDim }}>{t('passwords.report.sameIn', { count: g.length })}</Text>
              {g.map((id) => <Item key={id} e={byId.get(id)} cats={cats} onOpen={onOpen} />)}
            </View>
          ))}
        </Card>
        <Card title={t('passwords.report.old')} count={rep.old.length} color={YELLOW} hint={t('passwords.report.oldHint')}>
          {rep.old.map((o) => <Item key={o.id} e={byId.get(o.id)} cats={cats} onOpen={onOpen} sub={t('passwords.report.age', { days: o.days, limit: o.limit })} />)}
        </Card>
        <Card title={t('passwords.report.breached')} count={rep.breached ? rep.breached.length : rep.weak.filter((w) => w.common).length} color={RED}
          hint={hibp ? t('passwords.report.hibpUsing') : t('passwords.report.breachedHint')}>
          {(rep.breached || []).map((b) => <Item key={b.id} e={byId.get(b.id)} cats={cats} onOpen={onOpen} sub={t('passwords.report.seen', { count: b.count })} />)}
          <Button label={t('passwords.report.hibpChoose')} onClick={() => setPicking(true)} style={{ alignSelf: 'flex-start' }} />
        </Card>
        <Hint>{t('passwords.report.offline')}</Hint>
      </View>
      {picking && <FilePicker title={t('passwords.report.hibpChoose')} onClose={() => setPicking(false)}
        onPick={(p) => { setPicking(false); setHibp(typeof p === 'string' ? p : null); run(p); }} />}
    </ScrollView>
  );
}

// ---- todos los códigos 2FA ----

export function CodesView({ onOpen }) {
  const th = useTheme();
  const st = useStore(app);
  const cats = allCategories(st.cats);
  const [codes, setCodes] = useState([]);
  const base = useRef({ at: 0 });
  const [, tick] = useState(0);
  const load = async () => {
    try { const r = await req('totp'); base.current = { at: Date.now() / 1000 }; setCodes(r.codes || []); } catch (e) { setCodes([]); }
  };
  useEffect(() => { load(); }, [st.entries.length]);
  useEffect(() => {
    const iv = setInterval(() => {
      const el = Date.now() / 1000 - base.current.at;
      if (codes.some((c) => c.period && c.remaining - el <= 0)) load(); else tick((x) => x + 1);
    }, 1000);
    return () => clearInterval(iv);
  }, [codes]);
  const byId = new Map(st.entries.map((e) => [e.id, e]));
  if (!codes.length) return <Center text={t('passwords.codes.empty')}><Text style={{ fontSize: 40 }}>🔢</Text></Center>;
  const el = Date.now() / 1000 - base.current.at;
  return (
    <ScrollView style={{ flex: 1 }}>
      <View style={{ padding: 22, gap: 10 }}>
        <Text style={{ fontSize: 22, fontWeight: 700, color: th.text }}>{t('passwords.side.codes')}</Text>
        {codes.map((c) => {
          const e = byId.get(c.id);
          const left = Math.max(0, Math.round(c.remaining - el));
          const shown = c.code.length === 6 ? `${c.code.slice(0, 3)} ${c.code.slice(3)}` : c.code;
          return (
            <View key={`${c.id}-${c.k}`} onClick={() => onOpen(c.id)} style={{ flexDirection: 'row', alignItems: 'center', gap: 12, padding: 12, borderRadius: 12, backgroundColor: th.control }}>
              {e && <EntryIcon entry={e} cat={cats.find((x) => x.id === e.cat)} size={34} />}
              <View style={{ flex: 1, gap: 2 }}>
                <Text style={{ fontSize: 13, color: th.text }}>{e?.title || c.issuer || ''}</Text>
                <Text style={{ fontSize: 11, color: th.textDim }}>{c.account || e?.sub || ''}</Text>
              </View>
              <Text style={{ fontSize: 22, fontWeight: 600, fontFamily: 'monospace', color: left <= 5 ? RED : th.text, direction: 'ltr' }}>{shown}</Text>
              <View style={{ width: 34, alignItems: 'center' }}>
                <Text style={{ fontSize: 12, color: left <= 5 ? RED : th.textDim }}>{`${left}s`}</Text>
                <View style={{ width: 30, height: 3, borderRadius: 2, backgroundColor: th.windowBg, direction: 'ltr' }}>
                  <View style={{ width: Math.round(30 * (c.period ? left / c.period : 0)), height: 3, borderRadius: 2, backgroundColor: left <= 5 ? RED : GREEN }} />
                </View>
              </View>
              <IconButton glyph="⧉" onClick={() => copySecret(c.code, t('passwords.totp.code'))} />
            </View>
          );
        })}
      </View>
    </ScrollView>
  );
}

export { strengthColor };

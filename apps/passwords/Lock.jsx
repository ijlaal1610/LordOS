// Lock.jsx — crear la bóveda (elegir cómo se abre), abrirla (contraseña, archivo llave, llave
// FIDO2, contraseña + archivo llave, clave de recuperación) y mostrar la clave de recuperación
// para imprimirla o guardarla.
import { View, Text, useTheme, useStore, Button, TextInput, Segmented, requestPermission, print, fs, t } from '@lordos/sdk';
import * as React from 'react';
import { useEffect, useState } from 'react';
import { app, req, refreshStatus, errorText, APP_ID, toast, appMode } from './api.js';
import { StrengthBar, Hint, Warning, FilePicker, Label, RED, fileReq, fileName } from './ui.jsx';

function Padlock({ size = 64, open }) {
  return (
    <View style={{ width: size, height: size * 1.1, alignItems: 'center' }}>
      <View style={{ width: size * 0.56, height: size * 0.56, borderRadius: size * 0.28, border: `${Math.max(3, size * 0.09)}px solid #ffffffcc`,
        marginBottom: -size * 0.28, marginLeft: open ? size * 0.3 : 0 }} />
      <View style={{ width: size * 0.84, height: size * 0.58, borderRadius: size * 0.12, background: 'linear-gradient(160deg, #7d7aff, #3a37c4)',
        alignItems: 'center', justifyContent: 'center', boxShadow: '0 6px 18px rgba(0,0,0,0.35)' }}>
        <View style={{ width: size * 0.12, height: size * 0.12, borderRadius: size * 0.06, backgroundColor: '#fff' }} />
        <View style={{ width: size * 0.05, height: size * 0.14, borderRadius: size * 0.02, backgroundColor: '#fff' }} />
      </View>
    </View>
  );
}

// picker: a file picker shown in place of the form, under the header (the first child), so the
// lock screen stays recognisable instead of disappearing behind a dialog. In the app process the
// system dialog picks the file and the form stays.
function Shell({ children, wide, picker }) {
  const th = useTheme();
  const kids = React.Children.toArray(children);
  const inline = picker && !appMode;
  return (
    <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', backgroundColor: th.windowBg }}>
      <View style={{ width: inline ? 520 : wide ? 520 : 380, gap: 12, alignItems: 'stretch' }}>
        {inline ? [kids[0], <React.Fragment key="picker">{picker}</React.Fragment>] : [...kids, picker ? <React.Fragment key="picker">{picker}</React.Fragment> : null]}
      </View>
    </View>
  );
}

// ---- abrir ----

export function UnlockView() {
  const th = useTheme();
  const st = useStore(app);
  const slots = st.status?.main?.slots || [];
  const has = (type) => slots.some((s) => s.type === type);
  const first = has('password') ? 'password' : has('passkey') ? 'passkey' : has('keyfile') ? 'keyfile' : has('fido') ? 'fido' : 'recovery';
  const [method, setMethod] = useState(first);
  const [pw, setPw] = useState('');
  const [rec, setRec] = useState('');
  const [pin, setPin] = useState('');
  const [keyfile, setKeyfile] = useState('');
  const [picking, setPicking] = useState(false);
  const [err, setErr] = useState('');
  const [busy, setBusy] = useState(false);
  const [ms, setMs] = useState(null);
  useEffect(() => { setMethod(first); }, [first]);
  const go = async () => {
    setErr(''); setBusy(true);
    app.set((s) => ({ ...s, touch: false }));
    try {
      const args = { method };
      let files;
      if (method === 'password' || method === 'passkey') args.password = pw;
      if (method === 'recovery') args.recovery = rec;
      if (method === 'keyfile' || method === 'passkey') {
        if (!keyfile) { setBusy(false); setErr(t('passwords.unlock.needKeyfile')); return; }
        files = fileReq([keyfile, 'r']);
      }
      if (method === 'fido') {
        const ok = await requestPermission(APP_ID, 'usb');
        if (!ok) throw Object.assign(new Error('denied'), { code: 'fido-denied' });
        const a = lord.vault.fidoAttach(APP_ID);
        if (!a.ok) throw Object.assign(new Error(a.error), { code: a.error });
        if (pin) args.pin = pin;
      }
      const r = await req('unlock', args, files);
      setMs(r.ms ?? null);
      setPw(''); setRec(''); setPin('');
      await refreshStatus();
    } catch (e) { setErr(errorText(e.code)); }
    if (keyfile && typeof keyfile !== 'string') setKeyfile('');     // an fd is used up: pick it again
    setBusy(false);
    app.set((s) => ({ ...s, touch: false }));
  };
  const opts = [];
  if (has('password')) opts.push(['password', t('passwords.method.password')]);
  if (has('passkey')) opts.push(['passkey', t('passwords.method.passkey')]);
  if (has('keyfile')) opts.push(['keyfile', t('passwords.method.keyfile')]);
  if (has('fido')) opts.push(['fido', t('passwords.method.fido')]);
  if (has('recovery')) opts.push(['recovery', t('passwords.method.recovery')]);
  return (
    <Shell picker={picking && <FilePicker inline title={t('passwords.unlock.chooseKeyfile')} onClose={() => setPicking(false)} onPick={(p) => { setKeyfile(p); setPicking(false); }} />}>
      <View style={{ alignItems: 'center', gap: 6 }}>
        <Padlock size={72} />
        <Text style={{ fontSize: 20, fontWeight: 700, color: th.text }}>{t('passwords.unlock.title')}</Text>
        <Text style={{ fontSize: 12, color: th.textDim, textAlign: 'center' }}>{t('passwords.unlock.subtitle')}</Text>
      </View>
      {opts.length > 1 && <View style={{ alignItems: 'center' }}><Segmented options={opts} value={method} onChange={(m) => { setMethod(m); setErr(''); }} /></View>}
      {(method === 'password' || method === 'passkey') && (
        <TextInput value={pw} onChange={setPw} secure autoFocus placeholder={t('passwords.unlock.password')} onSubmit={go} size={15} />
      )}
      {(method === 'keyfile' || method === 'passkey') && (
        <View style={{ flexDirection: 'row', gap: 8, alignItems: 'center' }}>
          <Text style={{ flex: 1, fontSize: 12, color: keyfile ? th.text : th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{fileName(keyfile) || t('passwords.unlock.noKeyfile')}</Text>
          <Button label={t('passwords.unlock.chooseKeyfile')} onClick={() => setPicking(true)} />
        </View>
      )}
      {method === 'fido' && (
        <View style={{ gap: 8 }}>
          <Hint>{st.touch ? t('passwords.fido.touch') : t('passwords.fido.insert')}</Hint>
          <TextInput value={pin} onChange={setPin} secure placeholder={t('passwords.fido.pinOptional')} onSubmit={go} />
        </View>
      )}
      {method === 'recovery' && (
        <TextInput value={rec} onChange={setRec} placeholder="XXXXX-XXXXX-XXXXX-XXXXX-XXXXX-XXXXX-XXXXX-XXXXX" onSubmit={go} />
      )}
      {err ? <Text style={{ fontSize: 12, color: RED, textAlign: 'center' }}>{err}</Text> : null}
      <Button primary label={busy ? (st.touch ? t('passwords.fido.touchShort') : t('passwords.unlock.working')) : t('passwords.unlock.button')} onClick={go} style={{ height: 34 }} />
      {ms != null && <Hint>{t('passwords.unlock.took', { ms })}</Hint>}
      {st.status?.main?.unreadable && <Warning>{t('passwords.unlock.unreadable')}</Warning>}
    </Shell>
  );
}

// ---- crear ----

export function CreateView() {
  const th = useTheme();
  const st = useStore(app);
  const [method, setMethod] = useState('password');
  const [pw, setPw] = useState('');
  const [pw2, setPw2] = useState('');
  const [score, setScore] = useState(null);
  const [keyPath, setKeyPath] = useState('');
  const [picking, setPicking] = useState(false);
  const [pin, setPin] = useState('');
  const [err, setErr] = useState('');
  const [busy, setBusy] = useState(false);
  const [recovery, setRecovery] = useState(null);
  useEffect(() => {
    if (!pw) { setScore(null); return; }
    let live = true;
    req('strength', { password: pw }).then((r) => live && setScore(r)).catch(() => {});
    return () => { live = false; };
  }, [pw]);
  if (recovery) return <RecoveryView code={recovery} onDone={() => { setRecovery(null); refreshStatus(); }} />;
  const needPw = method === 'password' || method === 'passkey';
  const needKey = method === 'keyfile' || method === 'passkey';
  const go = async () => {
    setErr('');
    if (needPw && pw.length < 8) { setErr(t('passwords.create.short')); return; }
    if (needPw && pw !== pw2) { setErr(t('passwords.create.mismatch')); return; }
    if (needKey && !keyPath) { setErr(t('passwords.create.needKeyPath')); return; }
    setBusy(true);
    try {
      const args = { method };
      let files;
      if (needPw) args.password = pw;
      if (needKey) { args.newkey = true; files = fileReq([keyPath, 'w']); }
      if (method === 'fido') {
        const ok = await requestPermission(APP_ID, 'usb');
        if (!ok) throw Object.assign(new Error('denied'), { code: 'fido-denied' });
        const a = lord.vault.fidoAttach(APP_ID);
        if (!a.ok) throw Object.assign(new Error(a.error), { code: a.error });
        if (pin) args.pin = pin;
      }
      const r = await req('create', args, files);
      setPw(''); setPw2('');
      setRecovery(r.recovery);
    } catch (e) { setErr(errorText(e.code)); }
    setBusy(false);
  };
  return (
    <Shell wide picker={picking && <FilePicker inline save suggest="contrasenas.key" title={t('passwords.create.chooseKeyPath')} onClose={() => setPicking(false)}
        onPick={(p) => { setKeyPath(p); setPicking(false); }} />}>
      <View style={{ alignItems: 'center', gap: 6 }}>
        <Padlock size={64} open />
        <Text style={{ fontSize: 20, fontWeight: 700, color: th.text }}>{t('passwords.create.title')}</Text>
        <Text style={{ fontSize: 12, color: th.textDim, textAlign: 'center', lineHeight: 17 }}>{t('passwords.create.subtitle')}</Text>
      </View>
      <Label>{t('passwords.create.how')}</Label>
      <Segmented value={method} onChange={(m) => { setMethod(m); setErr(''); }} options={[
        ['password', t('passwords.method.password')], ['fido', t('passwords.method.fido')],
        ['keyfile', t('passwords.method.keyfile')], ['passkey', t('passwords.method.passkey')]]} />
      <Hint>{t(`passwords.create.about.${method}`)}</Hint>
      {needPw && (
        <View style={{ gap: 8 }}>
          <TextInput value={pw} onChange={setPw} secure autoFocus placeholder={t('passwords.create.password')} />
          <TextInput value={pw2} onChange={setPw2} secure placeholder={t('passwords.create.repeat')} onSubmit={go} />
          {score && <StrengthBar score={score.score} bits={score.bits} />}
        </View>
      )}
      {needKey && (
        <View style={{ flexDirection: 'row', gap: 8, alignItems: 'center' }}>
          <Text style={{ flex: 1, fontSize: 12, color: keyPath ? th.text : th.textDim, whiteSpace: 'nowrap', overflow: 'hidden' }}>{fileName(keyPath) || t('passwords.create.keyWhere')}</Text>
          <Button label={t('passwords.create.chooseKeyPath')} onClick={() => setPicking(true)} />
        </View>
      )}
      {method === 'fido' && (
        <View style={{ gap: 8 }}>
          <Hint>{st.touch ? t('passwords.fido.touch') : t('passwords.fido.insertCreate')}</Hint>
          <TextInput value={pin} onChange={setPin} secure placeholder={t('passwords.fido.pinOptional')} />
        </View>
      )}
      {err ? <Text style={{ fontSize: 12, color: RED }}>{err}</Text> : null}
      <Button primary label={busy ? (st.touch ? t('passwords.fido.touchShort') : t('passwords.create.working')) : t('passwords.create.button')} onClick={go} style={{ height: 34 }} />
      <Hint>{t('passwords.create.local')}</Hint>
    </Shell>
  );
}

// ---- clave de recuperación ----

export function RecoveryView({ code, onDone }) {
  const th = useTheme();
  const [saving, setSaving] = useState(false);
  const [ok, setOk] = useState(false);
  const text = `${t('passwords.recovery.printTitle')}\n\n${code}\n\n${t('passwords.recovery.printBody')}\n`;
  return (
    <Shell wide picker={saving && <FilePicker inline save suggest="clave-de-recuperacion.txt" title={t('passwords.recovery.save')} onClose={() => setSaving(false)}
        onPick={(p) => {
          try {
            if (typeof p === 'string') fs.write(p, text);
            else { const ok = lord.broker.fdWrite(p.fd, text); lord.broker.fdClose(p.fd); if (!ok) throw new Error('write'); }
            toast(t('passwords.recovery.saved'));
          } catch (e) { toast(errorText('file')); }
          setSaving(false);
        }} />}>
      <Text style={{ fontSize: 20, fontWeight: 700, color: th.text, textAlign: 'center' }}>{t('passwords.recovery.title')}</Text>
      <Hint>{t('passwords.recovery.body')}</Hint>
      <View style={{ padding: 16, borderRadius: 10, backgroundColor: th.control, alignItems: 'center', direction: 'ltr' }}>
        <Text style={{ fontSize: 17, fontWeight: 600, color: th.text, fontFamily: 'monospace' }}>{code}</Text>
      </View>
      <View style={{ flexDirection: 'row', gap: 8, justifyContent: 'center' }}>
        {!appMode && <Button label={t('passwords.recovery.print')} onClick={() => print.open({ title: t('passwords.recovery.printTitle'), text })} />}
        <Button label={t('passwords.recovery.save')} onClick={() => setSaving(true)} />
      </View>
      <View onClick={() => setOk(!ok)} style={{ flexDirection: 'row', gap: 8, alignItems: 'center' }}>
        <View style={{ width: 18, height: 18, borderRadius: 4, border: `1px solid ${th.separator}`, backgroundColor: ok ? th.accent : th.inputBg, alignItems: 'center', justifyContent: 'center' }}>
          {ok && <Text style={{ fontSize: 11, color: '#fff' }}>✓</Text>}
        </View>
        <Text style={{ flex: 1, fontSize: 12, color: th.text }}>{t('passwords.recovery.confirm')}</Text>
      </View>
      <Button primary label={t('passwords.recovery.done')} onClick={() => ok && onDone()} style={{ opacity: ok ? 1 : 0.5, height: 34 }} />
    </Shell>
  );
}

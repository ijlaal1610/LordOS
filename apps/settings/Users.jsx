// Ajustes → Usuarios: las cuentas del equipo (docs/SESIONES.md). Lee la lista sin root
// (lord.users) y cambia por el ayudante de administración: user-add, user-del, user-passwd,
// user-admin, user-avatar, user-autologin. Las contraseñas viajan solo en el argumento de la
// acción (nunca en argv) y con quiet, para que no queden en el aviso de "en curso".
import {
  View, Text, Image, Button, Toggle, TextInput, useTheme, useLocale, admin, useSystemInfo, remote, fs,
} from '@lordos/sdk';
import { useEffect, useState } from 'react';

const Divider = () => <View style={{ height: 1, backgroundColor: useTheme().separator }} />;
const COLORS = ['#5b9cff', '#f0915a', '#4cc37a', '#e0609d', '#a77bf3', '#4cc4d3', '#f2c14e', '#8e8e93'];
const NAME_RE = /^[a-z_][a-z0-9_-]{0,31}$/;

function shade(hex, f) {
  const n = parseInt(hex.slice(1), 16);
  const c = (v) => Math.max(0, Math.min(255, Math.round(v * f)));
  return `#${[n >> 16, (n >> 8) & 255, n & 255].map((v) => c(v).toString(16).padStart(2, '0')).join('')}`;
}
function hash(s) { let h = 0; for (const c of s) h = (h * 31 + c.codePointAt(0)) >>> 0; return h; }
const PALETTE = ['#5b9cff', '#f0915a', '#4cc37a', '#e0609d', '#a77bf3', '#4cc4d3', '#f2c14e'];
const colorOf = (u) => u.color || PALETTE[hash(u.name) % PALETTE.length];
function initials(u) {
  const w = (u.full || u.name).trim().split(/\s+/).filter(Boolean);
  return (w.length > 1 ? w[0][0] + w[1][0] : (w[0] || u.name).slice(0, 1)).toUpperCase();
}

export function UserAvatar({ user, size = 40 }) {
  const c = colorOf(user);
  return (
    <View style={{ width: size, height: size, borderRadius: size / 2, overflow: 'hidden', alignItems: 'center', justifyContent: 'center',
      background: `linear-gradient(170deg, ${c}, ${shade(c, 0.78)})` }}>
      {user.image ? <Image src={user.image} style={{ width: size, height: size, objectFit: 'cover' }} />
        : <Text style={{ fontSize: Math.round(size * 0.4), fontWeight: 600, color: '#fff' }}>{initials(user)}</Text>}
    </View>
  );
}

function Row({ label, hint, children }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 10, gap: 16 }}>
      <View style={{ flex: 1 }}>
        <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{label}</Text>
        {hint ? <Text style={{ fontSize: 11, color: t.textDim, marginTop: 3, lineHeight: 1.35 }}>{hint}</Text> : null}
      </View>
      {children}
    </View>
  );
}

const listUsers = () => (typeof lord.users === 'undefined' ? { users: [], autologin: '' } : lord.users.list());

export function UsersSettings() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const info = useSystemInfo(60000);
  const [data, setData] = useState(listUsers);
  const [sel, setSel] = useState(null);
  const [adding, setAdding] = useState(false);
  const [msg, setMsg] = useState(null);
  const [busy, setBusy] = useState(false);
  const isAdmin = admin.available() && info.admin;
  const reload = () => setData(listUsers());
  const st = remote.status();
  const open = new Set((st.sessions || []).map((s) => s.user));
  const users = isAdmin ? data.users : data.users.filter((u) => u.self);
  const cur = users.find((u) => u.name === sel) || users.find((u) => u.self) || users[0];

  const run = (action, arg, ok) => {
    setBusy(true);
    setMsg(null);
    return admin.run(action, arg, null, { quiet: true }).then(
      () => { setMsg({ ok: true, text: ok }); reload(); return true; },
      (e) => { setMsg({ ok: false, text: e.message }); return false; }).finally(() => setBusy(false));
  };

  return (
    <View style={{ paddingBottom: 24 }}>
      {!isAdmin && <Text style={{ fontSize: 11, color: '#ff9f0a', marginBottom: 6 }}>{tr('settings.users.onlyOwn')}</Text>}
      <View style={{ borderRadius: 10, backgroundColor: t.hover, paddingVertical: 4 }}>
        {users.map((u) => (
          <View key={u.name} onClick={() => { setSel(u.name); setAdding(false); setMsg(null); }} style={{ flexDirection: 'row', alignItems: 'center', gap: 12,
            paddingHorizontal: 12, paddingVertical: 8, borderRadius: 8, backgroundColor: cur && cur.name === u.name && !adding ? t.separator : 'transparent' }}>
            <UserAvatar user={u} size={36} />
            <View style={{ flex: 1 }}>
              <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{u.full || u.name}{u.self ? ` · ${tr('settings.users.you')}` : ''}</Text>
              <Text style={{ fontSize: 11, color: t.textDim }}>
                {[u.name, u.admin ? tr('settings.users.admin') : tr('settings.users.standard'), open.has(u.name) ? tr('settings.users.sessionOpen') : '']
                  .filter(Boolean).join(' · ')}</Text>
            </View>
          </View>
        ))}
      </View>
      {isAdmin && !adding && <View style={{ marginTop: 10, flexDirection: 'row' }}><Button label={tr('settings.users.add')} onClick={() => { setAdding(true); setMsg(null); }} /></View>}
      {busy && <Text style={{ fontSize: 12, color: t.textDim, marginTop: 8 }}>{tr('settings.users.working')}</Text>}
      {msg && <Text style={{ fontSize: 12, marginTop: 8, color: msg.ok ? '#30d158' : '#ff453a' }}>{msg.text}</Text>}
      {adding ? <AddUser onDone={(name) => { setAdding(false); if (name) setSel(name); }} run={run} />
        : cur ? <UserDetail key={cur.name} user={cur} isAdmin={isAdmin} run={run} autologin={data.autologin} admins={data.users.filter((x) => x.admin).length} /> : null}
    </View>
  );
}

function AddUser({ onDone, run }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [f, setF] = useState({ full: '', name: '', pass: '', pass2: '', admin: false });
  const [nameTouched, setNameTouched] = useState(false);
  const put = (p) => setF((o) => ({ ...o, ...p }));
  // nombre de usuario sugerido a partir del nombre completo
  const suggest = (full) => full.toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g, '').split(/\s+/)[0].replace(/[^a-z0-9_-]/g, '').slice(0, 32);
  const err = !NAME_RE.test(f.name) ? tr('settings.users.badName') : !f.pass ? tr('settings.users.noPass')
    : f.pass !== f.pass2 ? tr('settings.users.mismatch') : /[:,=\n]/.test(f.full) ? tr('settings.users.badFull') : '';
  const create = () => run('user-add', [f.name, f.full.trim(), f.pass, f.admin ? '1' : '0'].join('\n'), tr('settings.users.created'))
    .then((ok) => ok && onDone(f.name));
  return (
    <View style={{ marginTop: 14, gap: 8 }}>
      <Text style={{ fontSize: 14, fontWeight: 700, color: t.text }}>{tr('settings.users.newTitle')}</Text>
      <TextInput value={f.full} placeholder={tr('settings.users.fullName')} autoFocus
        onChange={(full) => put(nameTouched ? { full } : { full, name: suggest(full) })} />
      <TextInput value={f.name} placeholder={tr('settings.users.userName')} onChange={(name) => { setNameTouched(true); put({ name: name.toLowerCase() }); }} />
      <TextInput secure value={f.pass} placeholder={tr('settings.users.password')} onChange={(pass) => put({ pass })} />
      <TextInput secure value={f.pass2} placeholder={tr('settings.users.repeat')} onChange={(pass2) => put({ pass2 })} onSubmit={() => !err && create()} />
      <Row label={tr('settings.users.makeAdmin')} hint={tr('settings.users.makeAdminHint')}><Toggle on={f.admin} onChange={(admin) => put({ admin })} /></Row>
      {err ? <Text style={{ fontSize: 11, color: t.textDim }}>{err}</Text> : null}
      <View style={{ flexDirection: 'row', gap: 8 }}>
        <Button label={tr('settings.users.cancel')} onClick={() => onDone(null)} />
        <Button primary label={tr('settings.users.create')} onClick={() => !err && create()} />
      </View>
    </View>
  );
}

function UserDetail({ user, isAdmin, run, autologin, admins }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [pw, setPw] = useState({ old: '', pass: '', pass2: '' });
  const [img, setImg] = useState('');
  const [del, setDel] = useState(false);
  const [keep, setKeep] = useState(true);
  useEffect(() => { setPw({ old: '', pass: '', pass2: '' }); }, [user.name]);
  const canEdit = isAdmin || user.self;
  const pwErr = !pw.pass ? '' : pw.pass !== pw.pass2 ? tr('settings.users.mismatch') : user.self && !pw.old ? tr('settings.users.needOld') : '';
  const lastAdmin = user.admin && admins <= 1;
  const setColor = (c) => run('user-avatar', [user.name, c, ''].join('\n'), tr('settings.users.avatarSaved'));
  return (
    <View style={{ marginTop: 16 }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 14 }}>
        <UserAvatar user={user} size={64} />
        <View>
          <Text style={{ fontSize: 16, fontWeight: 700, color: t.text }}>{user.full || user.name}</Text>
          <Text style={{ fontSize: 12, color: t.textDim }}>{user.name}</Text>
        </View>
      </View>
      {canEdit && (
        <View style={{ marginTop: 12 }}>
          <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{tr('settings.users.avatar')}</Text>
          <View style={{ flexDirection: 'row', gap: 8, marginTop: 8 }}>
            {COLORS.map((c) => (
              <View key={c} onClick={() => setColor(c)} style={{ width: 22, height: 22, borderRadius: 11, backgroundColor: c,
                border: `2px solid ${user.color === c ? t.text : 'transparent'}` }} />
            ))}
          </View>
          <View style={{ flexDirection: 'row', gap: 8, marginTop: 8, alignItems: 'center' }}>
            <TextInput value={img} placeholder={tr('settings.users.imagePath')} onChange={setImg} style={{ flex: 1 }} />
            <Button label={tr('settings.users.useImage')} onClick={() => img && run('user-avatar', [user.name, user.color || '', img.replace(/^~/, fs.home())].join('\n'), tr('settings.users.avatarSaved'))} />
            {user.image ? <Button label={tr('settings.users.removeImage')} onClick={() => run('user-avatar', [user.name, user.color || '', '-'].join('\n'), tr('settings.users.avatarSaved'))} /> : null}
          </View>
        </View>
      )}
      <Divider />
      {canEdit && (
        <View style={{ marginTop: 10, gap: 8 }}>
          <Text style={{ fontSize: 13, fontWeight: 600, color: t.text }}>{tr('settings.users.changePassword')}</Text>
          {user.self ? <TextInput secure value={pw.old} placeholder={tr('settings.users.current')} onChange={(old) => setPw((p) => ({ ...p, old }))} /> : null}
          <TextInput secure value={pw.pass} placeholder={tr('settings.users.newPassword')} onChange={(pass) => setPw((p) => ({ ...p, pass }))} />
          <TextInput secure value={pw.pass2} placeholder={tr('settings.users.repeat')} onChange={(pass2) => setPw((p) => ({ ...p, pass2 }))} />
          {pwErr ? <Text style={{ fontSize: 11, color: t.textDim }}>{pwErr}</Text> : null}
          <View style={{ flexDirection: 'row' }}>
            <Button label={tr('settings.users.changePassword')} onClick={() => !pwErr && pw.pass &&
              run('user-passwd', [user.name, pw.pass, pw.old].join('\n'), tr('settings.users.passwordChanged')).then((ok) => ok && setPw({ old: '', pass: '', pass2: '' }))} />
          </View>
        </View>
      )}
      {isAdmin && (
        <View style={{ marginTop: 10 }}>
          <Divider />
          <Row label={tr('settings.users.admin')} hint={lastAdmin ? tr('settings.users.lastAdmin') : tr('settings.users.makeAdminHint')}>
            <Toggle on={user.admin} onChange={(on) => !(lastAdmin && !on) && run('user-admin', `${user.name}\n${on ? 1 : 0}`, tr('settings.users.saved'))} />
          </Row>
          <Divider />
          <Row label={tr('settings.users.autologin')} hint={tr('settings.users.autologinHint')}>
            <Toggle on={autologin === user.name} onChange={(on) => run('user-autologin', on ? user.name : '', tr('settings.users.saved'))} />
          </Row>
          {!user.self && (
            <View>
              <Divider />
              {!del ? (
                <View style={{ marginTop: 10, flexDirection: 'row' }}><Button label={tr('settings.users.delete')} onClick={() => setDel(true)} /></View>
              ) : (
                <View style={{ marginTop: 10, padding: 12, borderRadius: 10, backgroundColor: t.hover, gap: 8 }}>
                  <Text style={{ fontSize: 13, color: t.text }}>{tr('settings.users.deleteConfirm', { name: user.full || user.name })}</Text>
                  <Row label={tr('settings.users.keepHome')}><Toggle on={keep} onChange={setKeep} /></Row>
                  <View style={{ flexDirection: 'row', gap: 8 }}>
                    <Button label={tr('settings.users.cancel')} onClick={() => setDel(false)} />
                    <Button primary label={tr('settings.users.delete')} onClick={() => run('user-del', `${user.name}\n${keep ? 1 : 0}`, tr('settings.users.deleted'))} />
                  </View>
                </View>
              )}
            </View>
          )}
        </View>
      )}
    </View>
  );
}

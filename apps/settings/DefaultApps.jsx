// Settings → Default Apps: browser, mail, music, video, images, PDF, text editor and terminal, from
// LordOS's own apps and the installed Linux apps (.desktop with MimeType). Saved in
// ~/.config/mimeapps.list (and ~/.config/xdg-terminals.list), so Files' "Open With", double
// clicks, lord.apps.launchWith and Linux apps (xdg-open) agree. See docs/SETTINGS.md.
import { View, Text, useTheme, useLocale, useLauncherApps, DEFAULT_CATEGORIES, defaultFor, setDefaultFor, candidatesFor } from '@lordos/sdk';
import { useState } from 'react';
import { GroupBox, SettingRow, PopUp, Note, Stack, Badge } from './ui.jsx';

const LOOK = {
  browser: ['🌐', '#0a84ff'], mail: ['✉', '#30a0ff'], music: ['♪', '#ff375f'], video: ['▶', '#bf5af2'], images: ['▣', '#ff9f0a'],
  pdf: ['⎙', '#ff453a'], text: ['✎', '#8e8e93'], terminal: ['>_', '#2c2c2e'],
};

export function DefaultAppsSettings() {
  const { t: tr } = useLocale();
  const apps = useLauncherApps();
  const [rev, setRev] = useState(0);
  const [err, setErr] = useState('');
  return (
    <Stack>
      <GroupBox footer={tr('prefs.defaults.footer')}>
        {DEFAULT_CATEGORIES.map((c) => {
          const list = candidatesFor(c.id, apps);
          const cur = defaultFor(c.id);
          const opts = list.map((a) => [a.id, a.native ? a.name : `${a.name}`]);
          if (cur && !opts.some(([id]) => id === cur)) opts.push([cur, tr('prefs.defaults.other', { id: cur.replace(/\.desktop$/, '') })]);
          const [glyph, color] = LOOK[c.id];
          return (
            <SettingRow key={`${c.id}${rev}`} icon={<Badge glyph={glyph} color={color} />} label={tr(`prefs.defaults.cat.${c.id}`)}
              hint={list.length ? '' : tr('prefs.defaults.none')}>
              {opts.length ? <PopUp width={230} value={cur || ''} options={cur ? opts : [['', tr('prefs.defaults.choose')], ...opts]}
                onChange={(id) => { if (id && !setDefaultFor(c.id, id)) setErr(tr('prefs.defaults.failed')); else setErr(''); setRev((r) => r + 1); }} /> : null}
            </SettingRow>
          );
        })}
      </GroupBox>
      {err ? <Note tone="warn">{err}</Note> : null}
      <Note>{tr('prefs.defaults.filesHint')}</Note>
    </Stack>
  );
}

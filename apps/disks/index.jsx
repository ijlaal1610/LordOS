// Disks — the LordOS disk utility, like macOS Disk Utility or GNOME Disks (see docs/SYSTEM.md,
// "Disks"). On the left, the disks and their partitions; on the right, usage, details, health
// (SMART) and the actions: mount, unmount, eject, format, rename, erase and create a bootable USB
// from an image.
//
// lord-server reads the list without root (lord.disks, refreshed by itself on mount and udev
// events). Anything that changes something goes through the helper with the closed disk-*
// actions, which validate again on the root side: never the system disk, and internal disks only
// for an administrator.
import { View, Text, ScrollView, Button, useTheme, useLocale, useSystemInfo, admin, useDisks, disks, formatBytes } from '@lordos/sdk';
import { useEffect, useMemo, useState } from 'react';
import { DisksIcon, DriveGlyph, PartGlyph, fsColor } from './icons.jsx';
import { DiskDialog } from './dialogs.jsx';
import { HealthCard } from './health.jsx';

export function Icon({ size }) { return <DisksIcon size={size} />; }

const FS_NAMES = { vfat: 'FAT32', exfat: 'exFAT', ntfs: 'NTFS', ext4: 'ext4', ext3: 'ext3', ext2: 'ext2', btrfs: 'Btrfs', xfs: 'XFS',
  swap: 'swap', iso9660: 'ISO 9660', udf: 'UDF', crypto_LUKS: 'LUKS', f2fs: 'F2FS' };
export const fsName = (fs) => FS_NAMES[fs] || fs;

export function diskTitle(d) { return d.model || d.name; }
export function partTitle(p, tr) { return p.label || p.partlabel || tr('disks.partNumber', { n: p.number }); }

export function useCanTouch() {
  const info = useSystemInfo(60000);
  const isAdmin = admin.available() && info.admin;
  return { isAdmin, can: (d) => !!d && !d.system && !d.ro && (d.removable || isAdmin) };
}

function Row({ label, value }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', paddingVertical: 5, gap: 12 }}>
      <Text style={{ width: 150, fontSize: 12, color: t.textDim, textAlign: 'start' }}>{label}</Text>
      <Text style={{ flex: 1, fontSize: 12, color: t.text, textAlign: 'start' }}>{value || '—'}</Text>
    </View>
  );
}

// Usage bar: a disk shows its partitions to scale; a partition, used and free space.
function UsageBar({ disk, part }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  if (part) {
    const used = part.used, size = part.fssize || part.size;
    const frac = used != null && size ? Math.min(1, used / size) : null;
    return (
      <View style={{ gap: 6 }}>
        <View style={{ height: 14, borderRadius: 7, backgroundColor: t.control, overflow: 'hidden', flexDirection: 'row' }}>
          {frac != null ? <View style={{ flexGrow: frac, backgroundColor: fsColor(part.fstype) }} /> : null}
          {frac != null ? <View style={{ flexGrow: 1 - frac }} /> : null}
        </View>
        <Text style={{ fontSize: 11, color: t.textDim, textAlign: 'start' }}>
          {frac != null ? tr('disks.usedOf', { used: formatBytes(used), size: formatBytes(size), free: formatBytes(part.avail || 0) }) : tr('disks.notMountedUsage')}
        </Text>
      </View>
    );
  }
  const total = disk.size || 1;
  const parts = disk.partitions;
  const covered = parts.reduce((a, p) => a + p.size, 0);
  return (
    <View style={{ gap: 6 }}>
      <View style={{ height: 14, borderRadius: 7, backgroundColor: t.control, overflow: 'hidden', flexDirection: 'row', gap: 1 }}>
        {parts.length ? parts.map((p) => <View key={p.name} style={{ flexGrow: p.size / total, backgroundColor: fsColor(p.fstype) }} />)
          : disk.fstype ? <View style={{ flexGrow: 1, backgroundColor: fsColor(disk.fstype) }} /> : null}
        {parts.length && covered < total ? <View style={{ flexGrow: (total - covered) / total }} /> : null}
      </View>
      <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 12 }}>
        {parts.map((p) => (
          <View key={p.name} style={{ flexDirection: 'row', alignItems: 'center', gap: 5 }}>
            <PartGlyph fstype={p.fstype} size={9} />
            <Text style={{ fontSize: 11, color: t.textDim }}>{`${partTitle(p, tr)} · ${formatBytes(p.size)}`}</Text>
          </View>
        ))}
        {parts.length && total - covered > 16 * 1048576 ? <Text style={{ fontSize: 11, color: t.textDim }}>{tr('disks.unallocated', { size: formatBytes(total - covered) })}</Text> : null}
      </View>
    </View>
  );
}

const transportName = (tr, d) => tr(`disks.bus.${['usb', 'sata', 'nvme', 'virtio', 'mmc', 'loop', 'scsi'].includes(d.transport) ? d.transport : 'scsi'}`);

function Sidebar({ list, sel, onSel }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const groups = [
    [tr('disks.internal'), list.filter((d) => !d.removable && d.transport !== 'loop')],
    [tr('disks.external'), list.filter((d) => d.removable)],
    [tr('disks.images'), list.filter((d) => !d.removable && d.transport === 'loop')],
  ].filter(([, ds]) => ds.length);
  const item = (key, selected, onClick, glyph, title, sub, indent) => (
    <View key={key} onClick={onClick} style={{ flexDirection: 'row', alignItems: 'center', gap: 8, paddingVertical: 5, paddingHorizontal: 8,
      borderRadius: 6, backgroundColor: selected ? t.accent : 'transparent' }}>
      {indent ? <View style={{ width: indent - 8 }} /> : null}
      {glyph}
      <View style={{ flex: 1, minWidth: 0 }}>
        <Text style={{ fontSize: 12, fontWeight: indent ? 400 : 600, color: selected ? '#fff' : t.text, whiteSpace: 'nowrap', textAlign: 'start' }}>{title}</Text>
        <Text style={{ fontSize: 10, color: selected ? 'rgba(255,255,255,0.8)' : t.textDim, whiteSpace: 'nowrap', textAlign: 'start' }}>{sub}</Text>
      </View>
    </View>
  );
  return (
    <View style={{ width: 250, backgroundColor: t.sidebarBg }}><ScrollView style={{ flex: 1 }}>
      <View style={{ padding: 8, gap: 2 }}>
        {groups.map(([title, ds]) => (
          <View key={title} style={{ gap: 2, marginBottom: 6 }}>
            <Text style={{ fontSize: 11, fontWeight: 600, color: t.textDim, paddingHorizontal: 8, marginTop: 6, marginBottom: 2, textAlign: 'start' }}>{title}</Text>
            {ds.map((d) => (
              <View key={d.name} style={{ gap: 1 }}>
                {item(d.name, sel.disk === d.name && !sel.part, () => onSel({ disk: d.name, part: null }), <DriveGlyph disk={d} size={20} />,
                  diskTitle(d), `${formatBytes(d.size)} · ${transportName(tr, d)}${d.system ? ` · ${tr('disks.systemShort')}` : ''}`, 0)}
                {d.partitions.map((p) => item(p.name, sel.part === p.name, () => onSel({ disk: d.name, part: p.name }), <PartGlyph fstype={p.fstype} />,
                  partTitle(p, tr), `${p.name} · ${formatBytes(p.size)}${p.mountpoint ? ' · ●' : ''}`, 18))}
              </View>
            ))}
          </View>
        ))}
      </View>
    </ScrollView></View>
  );
}

function Toolbar({ disk, part, onDialog, run, busy }) {
  const { t: tr } = useLocale();
  const t = useTheme();
  const { can } = useCanTouch();
  const ok = can(disk);
  const target = part || disk;
  const mountable = target.fstype && target.fstype !== 'swap' && target.fstype !== 'crypto_LUKS';
  const buttons = [];
  if (ok && mountable && !target.mountpoint) buttons.push(['mount', tr('disks.mount'), () => run('disk-mount', target.name, tr('disks.mounted'))]);
  if (ok && target.mountpoint) buttons.push(['unmount', tr('disks.unmount'), () => run('disk-unmount', target.name, tr('disks.unmounted'))]);
  if (ok && !part && disk.removable) buttons.push(['eject', tr('disks.eject'), () => run('disk-eject', disk.name, tr('disks.ejected'))]);
  if (ok) buttons.push(['format', tr('disks.format'), () => onDialog({ kind: 'format' })]);
  if (ok && part && ['vfat', 'exfat', 'ext2', 'ext3', 'ext4', 'ntfs'].includes(part.fstype)) buttons.push(['label', tr('disks.rename'), () => onDialog({ kind: 'label' })]);
  if (ok) buttons.push(['erase', tr('disks.erase'), () => onDialog({ kind: 'erase' })]);
  if (ok && !part) buttons.push(['image', tr('disks.makeBootable'), () => onDialog({ kind: 'image' })]);
  return (
    <View style={{ gap: 6 }}>
      <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8, opacity: busy ? 0.5 : 1 }}>
        {buttons.map(([k, label, fn]) => <Button key={k} label={label} onClick={() => !busy && fn()} />)}
      </View>
      {!ok ? <Text style={{ fontSize: 11, color: t.textDim, textAlign: 'start' }}>
        {disk.system ? tr('disks.systemNote') : disk.ro ? tr('disks.readOnlyNote') : tr('disks.adminNote')}</Text> : null}
    </View>
  );
}

function Details({ disk, part, onDialog }) {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState(null);
  useEffect(() => setMsg(null), [disk.name, part && part.name]);
  const run = (action, arg, okText) => {
    setBusy(true);
    setMsg(null);
    return admin.run(action, arg, null, { quiet: true }).then(
      (r) => { setMsg({ ok: true, text: action === 'disk-mount' ? tr('disks.mountedAt', { path: r }) : okText }); },
      (e) => setMsg({ ok: false, text: e.message })).finally(() => setBusy(false));
  };
  const target = part || disk;
  const title = part ? partTitle(part, tr) : diskTitle(disk);
  const subtitle = part
    ? [part.name, formatBytes(part.size), fsName(part.fstype) || tr('disks.noFs')].filter(Boolean).join(' · ')
    : [disk.name, formatBytes(disk.size), transportName(tr, disk), disk.table ? tr(`disks.table.${disk.table === 'gpt' ? 'gpt' : 'dos'}`) : ''].filter(Boolean).join(' · ');
  return (
    <ScrollView style={{ flex: 1 }}>
      <View style={{ padding: 20, gap: 16 }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 14 }}>
          {part ? <PartGlyph fstype={part.fstype} size={44} /> : <DriveGlyph disk={disk} size={52} />}
          <View style={{ flex: 1, minWidth: 0 }}>
            <Text style={{ fontSize: 18, fontWeight: 700, color: t.text, textAlign: 'start' }}>{title}</Text>
            <Text style={{ fontSize: 12, color: t.textDim, textAlign: 'start' }}>{subtitle}</Text>
          </View>
          {disk.system ? <View style={{ paddingHorizontal: 8, paddingVertical: 3, borderRadius: 6, backgroundColor: t.control }}>
            <Text style={{ fontSize: 11, color: t.text }}>{tr('disks.systemBadge')}</Text></View> : null}
        </View>
        <UsageBar disk={disk} part={part} />
        <Toolbar disk={disk} part={part} onDialog={onDialog} run={run} busy={busy} />
        {busy ? <Text style={{ fontSize: 12, color: t.textDim, textAlign: 'start' }}>{tr('disks.working')}</Text> : null}
        {msg ? <Text style={{ fontSize: 12, color: msg.ok ? '#30d158' : '#ff453a', textAlign: 'start' }}>{msg.text}</Text> : null}
        <View style={{ borderRadius: 10, backgroundColor: t.hover, paddingHorizontal: 14, paddingVertical: 8 }}>
          {part ? (
            <View>
              <Row label={tr('disks.device')} value={`/dev/${part.name}`} />
              <Row label={tr('disks.fsType')} value={part.fstype ? `${fsName(part.fstype)}${part.fsversion ? ` (${part.fsversion})` : ''}` : tr('disks.noFs')} />
              <Row label={tr('disks.label')} value={part.label} />
              <Row label={tr('disks.uuid')} value={part.uuid} />
              <Row label={tr('disks.partName')} value={part.partlabel} />
              <Row label={tr('disks.mountPoint')} value={part.mountpoint || tr('disks.notMounted')} />
              <Row label={tr('disks.offset')} value={formatBytes(part.start)} />
            </View>
          ) : (
            <View>
              <Row label={tr('disks.device')} value={`/dev/${disk.name}`} />
              <Row label={tr('disks.model')} value={[disk.vendor, disk.model].filter(Boolean).join(' ')} />
              <Row label={tr('disks.serial')} value={disk.serial} />
              <Row label={tr('disks.capacity')} value={tr('disks.bytes', { size: formatBytes(disk.size), n: disk.size })} />
              <Row label={tr('disks.connection')} value={transportName(tr, disk)} />
              <Row label={tr('disks.kind')} value={disk.transport === 'loop' ? tr('disks.kindImage') : disk.removable ? tr('disks.kindRemovable') : disk.rotational ? tr('disks.kindHdd') : tr('disks.kindSsd')} />
              <Row label={tr('disks.partTable')} value={disk.table ? tr(`disks.table.${disk.table === 'gpt' ? 'gpt' : 'dos'}`) : tr('disks.noTable')} />
              <Row label={tr('disks.partitions')} value={String(disk.partitions.length)} />
            </View>
          )}
        </View>
        {!part ? <HealthCard disk={disk} /> : null}
        {target.ro ? <Text style={{ fontSize: 11, color: t.textDim, textAlign: 'start' }}>{tr('disks.readOnlyNote')}</Text> : null}
      </View>
    </ScrollView>
  );
}

export default function Disks() {
  const t = useTheme();
  const { t: tr } = useLocale();
  const [raw, reload] = useDisks();
  const list = useMemo(() => [...raw].sort((a, b) => a.name.localeCompare(b.name)), [raw]);
  const [sel, setSel] = useState({ disk: null, part: null });
  const [dialog, setDialog] = useState(null);
  const tools = useMemo(() => disks.tools(), []);
  // pick something on open (the first removable, if any) and keep a valid selection if it disappears
  const disk = list.find((d) => d.name === sel.disk) || list.find((d) => d.removable) || list[0];
  const part = disk && sel.part ? disk.partitions.find((p) => p.name === sel.part) || null : null;
  if (!disks.available()) {
    return <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center' }}><Text style={{ color: t.textDim }}>{tr('disks.unavailable')}</Text></View>;
  }
  return (
    <View style={{ flex: 1, flexDirection: 'row' }}>
      <Sidebar list={list} sel={{ disk: disk && disk.name, part: part && part.name }} onSel={setSel} />
      <View style={{ width: 1, backgroundColor: t.separator }} />
      {disk ? <Details key={disk.name} disk={disk} part={part} onDialog={(d) => setDialog({ ...d, disk, part })} />
        : <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center' }}><Text style={{ color: t.textDim }}>{tr('disks.none')}</Text></View>}
      {dialog ? <DiskDialog {...dialog} tools={tools} onClose={() => { setDialog(null); reload(); }} /> : null}
    </View>
  );
}

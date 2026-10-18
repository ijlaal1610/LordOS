// icons.jsx — ícono de la app Discos y glifos de los discos, dibujados con View (sin fuentes de
// íconos): un disco duro plateado con su plato, el brazo y la luz de actividad.
import { View } from '@lordos/sdk';

export function DisksIcon({ size: s = 32 }) {
  const r = s * 0.225;
  return (
    <View style={{ width: s, height: s, borderRadius: r, overflow: 'hidden', alignItems: 'center', justifyContent: 'center',
      background: 'linear-gradient(160deg, #5e6b7d, #2b323d)', boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)` }}>
      {/* carcasa */}
      <View style={{ width: s * 0.7, height: s * 0.56, borderRadius: s * 0.08, background: 'linear-gradient(180deg, #f2f4f7, #b9c0ca)',
        alignItems: 'center', justifyContent: 'center', boxShadow: `0 ${s * 0.02}px ${s * 0.05}px rgba(0,0,0,0.4)` }}>
        {/* plato */}
        <View style={{ width: s * 0.4, height: s * 0.4, borderRadius: s * 0.2, background: 'linear-gradient(140deg, #ffffff, #8f9aa8)',
          border: `${Math.max(1, s * 0.02)}px solid #7d8794`, alignItems: 'center', justifyContent: 'center' }}>
          <View style={{ width: s * 0.1, height: s * 0.1, borderRadius: s * 0.05, backgroundColor: '#5b6573' }} />
        </View>
        {/* brazo */}
        <View style={{ position: 'absolute', right: s * 0.08, top: s * 0.08, width: s * 0.05, height: s * 0.26, borderRadius: s * 0.025,
          backgroundColor: '#4b5563' }} />
        {/* luz */}
        <View style={{ position: 'absolute', left: s * 0.06, bottom: s * 0.05, width: s * 0.06, height: s * 0.06, borderRadius: s * 0.03,
          backgroundColor: '#30d158' }} />
      </View>
    </View>
  );
}

// Glifo de un disco según su conexión: interno (gris), extraíble/USB (naranja), NVMe (azul), imagen.
export function DriveGlyph({ disk, size = 18 }) {
  const usb = disk.removable || disk.transport === 'usb' || disk.transport === 'mmc';
  const color = disk.transport === 'loop' ? '#a78bfa' : usb ? '#f59e0b' : disk.transport === 'nvme' ? '#3b82f6' : '#8e96a3';
  const s = size;
  return (
    <View style={{ width: s, height: s, alignItems: 'center', justifyContent: 'center' }}>
      <View style={{ width: usb ? s * 0.62 : s * 0.9, height: usb ? s * 0.9 : s * 0.62, borderRadius: s * 0.14, backgroundColor: color,
        alignItems: 'center', justifyContent: usb ? 'flex-start' : 'center' }}>
        {usb ? <View style={{ width: s * 0.34, height: s * 0.2, marginTop: -s * 0.12, backgroundColor: '#c7ccd3', borderRadius: s * 0.03 }} />
          : <View style={{ width: s * 0.3, height: s * 0.3, borderRadius: s * 0.15, backgroundColor: 'rgba(255,255,255,0.55)' }} />}
      </View>
    </View>
  );
}

// Glifo de una partición: una porción con el color del sistema de archivos.
export const FS_COLORS = { vfat: '#22c55e', exfat: '#14b8a6', ntfs: '#3b82f6', ext4: '#f97316', ext3: '#fb923c', ext2: '#fdba74',
  btrfs: '#eab308', xfs: '#ec4899', swap: '#8b5cf6', iso9660: '#a78bfa', crypto_LUKS: '#ef4444', '': '#9ca3af' };
export const fsColor = (fs) => FS_COLORS[fs] || '#64748b';

export function PartGlyph({ fstype, size = 14 }) {
  return <View style={{ width: size, height: size, borderRadius: size * 0.25, backgroundColor: fsColor(fstype) }} />;
}

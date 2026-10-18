// logo.js — el logo de LordOS: una corona blanca sobre violeta (el ícono de "Acerca de", de
// Ajustes → General y del menú del tema). La corona es assets/icons/lordos-crown.png
// (tools/gen-logo.py).
import { createElement as h } from 'react';
import { View, Image } from '@lordos/runtime';

export function LordOSLogo({ size: s = 64 }) {
  return h(View, {
    style: {
      width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(135deg, #b46cff, #4f46e5)',
      boxShadow: s > 24 ? `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)` : 'none', alignItems: 'center', justifyContent: 'center',
    },
  }, h(Image, { src: 'asset:icons/lordos-crown.png', style: { width: s * 0.7, height: s * 0.7, marginTop: -s * 0.02 } }));
}

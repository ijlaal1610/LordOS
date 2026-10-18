// templates.js — categorías incluidas, con la plantilla de campos de cada una, y los tipos de
// campo. Los nombres se traducen al dibujar (t('passwords.cat.<id>'), t('passwords.field.<clave>')):
// por eso son funciones y no constantes con texto.
import { t } from '@lordos/sdk';

// Campos de cada plantilla: k = clave (estable, la usa lord-vault: "password" entra en el informe,
// "totp" da códigos, "expiry" avisa vencimientos), t = tipo.
const F = (k, type = 'text', extra = {}) => ({ k, t: type, ...extra });

export const BUILTIN = [
  { id: 'general', icon: '🗂️', color: '#8e8e93', fields: [F('username'), F('password', 'hidden'), F('url', 'url')] },
  { id: 'internet', icon: '🌐', color: '#0a84ff', fields: [F('url', 'url'), F('username'), F('password', 'hidden'), F('totp', 'totp')] },
  { id: 'email', icon: '✉️', color: '#5e5ce6', fields: [F('email', 'email'), F('password', 'hidden'), F('imap_host'), F('imap_port', 'number'),
    F('smtp_host'), F('smtp_port', 'number'), F('app_password', 'hidden')] },
  { id: 'finance', icon: '🏦', color: '#30d158', fields: [F('bank'), F('client'), F('username'), F('password', 'hidden'), F('transfer_key', 'hidden'),
    F('pin', 'pin'), F('coords', 'hidden', { multiline: true })] },
  { id: 'cards', icon: '💳', color: '#ff9f0a', fields: [F('holder'), F('number', 'hidden'), F('expiry'), F('cvv', 'pin'), F('pin', 'pin'), F('phone')] },
  { id: 'network', icon: '📡', color: '#64d2ff', fields: [F('device'), F('host'), F('port', 'number'), F('username'), F('password', 'hidden')] },
  { id: 'wifi', icon: '📶', color: '#32ade6', fields: [F('ssid'), F('security', 'text', { choices: ['WPA3', 'WPA2', 'WPA', 'WEP', 'none'] }), F('password', 'hidden'),
    F('hidden', 'bool')] },
  { id: 'ssh', icon: '🖥️', color: '#636366', fields: [F('host'), F('port', 'number'), F('username'), F('password', 'hidden'),
    F('private_key', 'hidden', { multiline: true }), F('fingerprint')] },
  { id: 'database', icon: '🗄️', color: '#bf5af2', fields: [F('engine', 'text', { choices: ['PostgreSQL', 'MySQL', 'MariaDB', 'SQLite', 'MongoDB', 'Redis', 'SQL Server', 'Oracle'] }),
    F('host'), F('port', 'number'), F('database'), F('username'), F('password', 'hidden')] },
  { id: 'apps', icon: '📱', color: '#ff375f', fields: [F('app'), F('account'), F('password', 'hidden'), F('totp', 'totp'), F('recovery_codes', 'hidden', { multiline: true })] },
  { id: 'licenses', icon: '🔑', color: '#ffd60a', fields: [F('product'), F('version'), F('license_key', 'hidden', { multiline: true }), F('email', 'email'),
    F('purchase_date', 'date'), F('expiry', 'date')], attachments: true },
  { id: 'api', icon: '🧩', color: '#ac8e68', fields: [F('service'), F('key', 'hidden'), F('secret', 'hidden'), F('expiry', 'date'), F('scopes')] },
  { id: 'identity', icon: '🪪', color: '#ff6961', fields: [F('doc_type', 'text', { choices: ['dni', 'passport', 'license', 'health'] }), F('number'),
    F('holder'), F('issued', 'date'), F('expiry', 'date'), F('issuer')], attachments: true, photos: true },
  { id: 'crypto', icon: '🪙', color: '#f5a623', fields: [F('wallet'), F('address'), F('seed', 'hidden', { multiline: true }), F('password', 'hidden')], warning: true },
  { id: 'vpn', icon: '🛡️', color: '#34c759', fields: [F('server'), F('vpn_type', 'text', { choices: ['WireGuard', 'OpenVPN', 'IPsec/IKEv2', 'AnyConnect'] }),
    F('username'), F('password', 'hidden')], attachments: true, settingsLink: true },
  { id: 'notes', icon: '📝', color: '#ffcc00', fields: [], attachments: true, notesFirst: true },
];

// Las del llavero de la sesión (no se crean a mano)
export const SYSTEM_CATS = [
  { id: 'keyring', icon: '🔐', color: '#8e8e93', fields: [F('service'), F('username'), F('password', 'hidden')] },
  { id: 'secrets', icon: '🐧', color: '#8e8e93', fields: [F('url', 'url'), F('username'), F('password', 'hidden')] },
];

export const FIELD_TYPES = ['text', 'hidden', 'url', 'date', 'number', 'email', 'totp', 'file', 'pin', 'multiline'];

export const ICONS = ['🗂️', '🌐', '✉️', '🏦', '💳', '🖧', '📶', '🖥️', '🗄️', '📱', '🔑', '🧩', '🪪', '🪙', '🛡️', '📝', '📁', '🏠', '💼', '🎓',
  '🎮', '🛒', '✈️', '🏥', '⚙️', '📷', '🎵', '📦', '🔒', '⭐'];
export const COLORS = ['#0a84ff', '#5e5ce6', '#bf5af2', '#ff375f', '#ff453a', '#ff9f0a', '#ffd60a', '#30d158', '#64d2ff', '#ac8e68', '#8e8e93'];

export const builtinById = (id) => BUILTIN.find((c) => c.id === id) || SYSTEM_CATS.find((c) => c.id === id);

// Nombre visible de una categoría (incluida o del usuario)
export function catName(cat) {
  if (!cat) return '';
  if (cat.builtin || builtinById(cat.id) && !cat.name) return t(`passwords.cat.${cat.id}`);
  return cat.name || cat.id;
}

// Etiqueta de un campo: la propia (campos agregados) o la de la plantilla
export function fieldLabel(f) {
  if (f.l) return f.l;
  const k = String(f.k || '');
  return /^c\d+$/.test(k) ? t('passwords.field.custom') : t(`passwords.field.${k}`);
}

// Todas las categorías: incluidas + del usuario (árbol por "parent")
export function allCategories(userCats = []) {
  const builtins = BUILTIN.map((c) => ({ ...c, builtin: true }));
  const users = (userCats || []).filter((c) => c && c.id && !builtinById(c.id)).map((c) => ({ ...c, builtin: false }));
  return [...builtins, ...users];
}

// Campos para una entrada nueva de la categoría (los del usuario heredan los de su plantilla)
export function templateFields(cat, cats) {
  if (!cat) return BUILTIN[0].fields;
  if (cat.fields && cat.fields.length) return cat.fields;
  const base = builtinById(cat.template || '') || (cat.parent && cats ? templateFields(cats.find((c) => c.id === cat.parent), cats) : null);
  return Array.isArray(base) ? base : base?.fields || BUILTIN[0].fields;
}

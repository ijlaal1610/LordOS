#!/usr/bin/env node
// Builds the static site for https://lordos.org into web/dist.
//
//   node web/build.mjs
//
// Needs: Node 18+, cwebp (brew install webp), rsvg-convert (brew install librsvg) and pyftsubset
// (pip install fonttools brotli).
// Pages are pre-rendered per language from one template per page (web/src/*.html) and the
// dictionaries in web/i18n/{en,es,pt}.json (+ web/i18n/apps.json for the apps page). CSS, JS, fonts
// and images get content-hashed names under /assets/ so they can be cached forever; HTML is
// revalidated on every visit (tools/web/deploy.sh).
// The site's CSP forbids inline scripts: behaviour lives in /assets/site.<hash>.js, and the theme
// is applied before paint by the tiny /assets/theme.<hash>.js loaded synchronously in <head>.
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import http from 'node:http';
import { createRequire } from 'node:module';
import { markdown, inline, frontMatter } from './lib/md.mjs';
import { diagram } from './lib/diagram.mjs';

const WEB = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(WEB, '..');
const DIST = path.join(WEB, 'dist');
const CACHE = path.join(WEB, '.cache');
const SITE = 'https://lordos.org';
const LANGS = ['en', 'es', 'pt'];
const PAGES = ['home', 'apps', 'gallery'];
const CONTENT = process.env.CONTENT_DIR ? path.resolve(process.env.CONTENT_DIR) : path.join(path.dirname(fileURLToPath(import.meta.url)), 'content');
// headless Chromium for the paper PDFs: a folder whose node_modules has playwright
const TOOLS = process.env.WEB_TOOLS || process.env.ICON_DEPS || '';
const FINGERPRINT = 'c7155b8e6bbbb180f907381994d06af76d333016cd6f3facf67cadd053ea108e';
const GITHUB = 'https://github.com/lordbasex/LordOS';

const r = (...p) => path.join(REPO, ...p);
const read = (f) => fs.readFileSync(f);
const sha = (buf, n = 10) => createHash('sha256').update(buf).digest('hex').slice(0, n);
const mkdirp = (d) => fs.mkdirSync(d, { recursive: true });
const write = (rel, data) => { const f = path.join(DIST, rel); mkdirp(path.dirname(f)); fs.writeFileSync(f, data); };
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const log = (...a) => console.log('[web]', ...a);
const src = (f) => fs.readFileSync(path.join(WEB, 'src', f), 'utf8');

fs.rmSync(DIST, { recursive: true, force: true });
mkdirp(DIST); mkdirp(path.join(CACHE, 'img')); mkdirp(path.join(CACHE, 'fonts'));

// ---------- hashed assets ----------
function hashed(buf, name, ext) {
  const rel = `assets/${name}.${sha(buf)}.${ext}`;
  write(rel, buf);
  return '/' + rel;
}
const asset = (file, name) => hashed(read(file), name, path.extname(file).slice(1));

function imgSize(file) {
  const b = read(file);
  if (b.readUInt32BE(0) === 0x89504e47) return { w: b.readUInt32BE(16), h: b.readUInt32BE(20) };
  const out = execFileSync('sips', ['-g', 'pixelWidth', '-g', 'pixelHeight', file]).toString();
  return { w: +out.match(/pixelWidth: (\d+)/)[1], h: +out.match(/pixelHeight: (\d+)/)[1] };
}

// WebP of `file` at most `max` px wide; cached by content so rebuilds are fast.
function webp(file, max, q = 80, name) {
  const buf = read(file);
  const { w, h } = imgSize(file);
  const tw = Math.min(w, max), th = Math.round((h * tw) / w);
  const key = sha(Buffer.concat([buf, Buffer.from(`|${tw}|${q}`)]), 16);
  const cached = path.join(CACHE, 'img', key + '.webp');
  if (!fs.existsSync(cached)) {
    const args = ['-quiet', '-q', String(q), '-m', '4', '-metadata', 'none', '-alpha_q', '100'];
    if (tw !== w) args.push('-resize', String(tw), String(th));
    execFileSync('cwebp', [...args, file, '-o', cached]);
  }
  const base = name || path.basename(file).replace(/\.[^.]+$/, '');
  return { url: hashed(read(cached), `img/${base}-${tw}`, 'webp'), w: tw, h: th };
}

// App icon -> 128 px WebP (SVG rasterised with rsvg-convert first).
function icon(file, id) {
  let png = file;
  if (file.endsWith('.svg')) {
    png = path.join(CACHE, 'img', `icon-${sha(read(file), 16)}.png`);
    if (!fs.existsSync(png)) execFileSync('rsvg-convert', ['-w', '128', '-h', '128', '-o', png, file]);
  }
  return webp(png, 128, 90, `app-${id}`);
}

// ---------- fonts ----------
const UNICODES = 'U+0000-00FF,U+0100-017F,U+2000-206F,U+20AC,U+2122,U+2190-2193,U+2212';
function font(cut) {
  const ttf = r('assets/fonts', `PlusJakartaSans-${cut}.ttf`);
  const key = sha(Buffer.concat([read(ttf), Buffer.from(UNICODES)]), 16);
  const out = path.join(CACHE, 'fonts', `${cut}-${key}.woff2`);
  if (!fs.existsSync(out)) {
    execFileSync('pyftsubset', [ttf, `--unicodes=${UNICODES}`, '--flavor=woff2', '--layout-features=kern,liga,calt,ccmp,locl,mark,mkmk', '--no-hinting', '--desubroutinize', `--output-file=${out}`]);
  }
  return hashed(read(out), `fonts/plus-jakarta-sans-${cut.toLowerCase()}`, 'woff2');
}
const fontMedium = font('Medium');
const fontExtraBold = font('ExtraBold');
write('licenses/PlusJakartaSans-OFL.txt', read(r('assets/fonts/PlusJakartaSans-LICENSE.txt')));

// ---------- css / js ----------
const css = src('site.css')
  .replace('__FONT_MEDIUM__', fontMedium).replace('__FONT_EXTRABOLD__', fontExtraBold)
  .replace(/\/\*[\s\S]*?\*\//g, '').replace(/\s*([{};,>])\s*/g, '$1').replace(/;}/g, '}').replace(/\n\s*/g, '\n').replace(/\n+/g, '\n');
const cssUrl = hashed(Buffer.from(css), 'site', 'css');
const minCss = (c) => c.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\s*([{};,>])\s*/g, '$1').replace(/;}/g, '}').replace(/\n\s*/g, '\n').replace(/\n+/g, '\n');
const docCss = minCss(src('doc.css'));
const docCssUrl = hashed(Buffer.from(docCss), 'doc', 'css');
// [[PLACEHOLDER]] markers left by the content agents: highlighted when --allow-placeholders, else the build fails
const ALLOW_PH = process.argv.includes('--allow-placeholders');
const placeholders = new Map();
const PH_RE = /\[\[([A-Z][A-Z0-9_]*)\]\]/g;
const notePh = (text, where) => { for (const m of String(text).matchAll(PH_RE)) placeholders.set(`${where}: [[${m[1]}]]`, true); return text; };
const phIn = (text, where) => notePh(text, where).replace(PH_RE, '\u0001$1\u0001');
const phOut = (html) => html.replace(/\u0001([A-Z0-9_]+)\u0001/g, '<mark class="ph">[[$1]]</mark>');
const jsSrc = src('site.js');
const jsUrl = hashed(Buffer.from(jsSrc), 'site', 'js');
const themeSrc = src('theme.js');
const themeUrl = hashed(Buffer.from(themeSrc), 'theme', 'js');

// ---------- images ----------
const img = {
  logoLight: asset(r('assets/brand/logo-horizontal-light.svg'), 'logo-light'),
  logoDark: asset(r('assets/brand/logo-horizontal-dark.svg'), 'logo-dark'),
  lumoWave: asset(r('assets/brand/mascot/lumo-wave.svg'), 'lumo-wave'),
  lumoShield: asset(r('assets/brand/mascot/lumo-shield.svg'), 'lumo-shield'),
  lumoCelebrate: asset(r('assets/brand/mascot/lumo-celebrate.svg'), 'lumo-celebrate'),
  lumoConfused: asset(r('assets/brand/mascot/lumo-confused.svg'), 'lumo-confused'),
  federico: webp(path.join(WEB, 'src/team/federico-pereira.jpg'), 240, 82).url,
};

function picture(file, alt, { eager = false, sizes = '(max-width: 1148px) 100vw, 1100px', cls = 'shot' } = {}) {
  const small = webp(file, 720), big = webp(file, 1600);
  const srcset = small.w < big.w ? `${small.url} ${small.w}w, ${big.url} ${big.w}w` : `${big.url} ${big.w}w`;
  return `<img class="${cls}" src="${big.url}" srcset="${srcset}" sizes="${sizes}" width="${big.w}" height="${big.h}" alt="${esc(alt)}"${eager ? ' fetchpriority="high"' : ' loading="lazy" decoding="async"'}>`;
}

// ---------- gallery data ----------
const galleryJson = r('docs/screenshots/gallery.json');
let shots;
if (fs.existsSync(galleryJson)) {
  const raw = JSON.parse(fs.readFileSync(galleryJson, 'utf8'));
  shots = (Array.isArray(raw) ? raw : raw.shots || raw.items || raw.images || []).map((s) => ({
    ...s,
    file: fs.existsSync(r(s.file)) ? r(s.file) : r('docs/screenshots', s.file),
  }));
  log(`gallery: ${shots.length} entries from docs/screenshots/gallery.json`);
} else {
  shots = JSON.parse(src('gallery-fallback.json')).map((s) => ({ ...s, file: r(s.file) }));
  log(`gallery: docs/screenshots/gallery.json not found, using the ${shots.length} mockup screenshots`);
}
shots = shots.filter((s) => {
  if (fs.existsSync(s.file)) return true;
  console.warn('[web] gallery: missing file, skipped:', s.file);
  return false;
});
const shotImgs = shots.map((s) => ({ thumb: webp(s.file, 720), full: webp(s.file, 1600) }));
log(`gallery: ${shots.length} images converted`);

// ---------- apps data ----------
const APP_CATS = ['productivity', 'media', 'system', 'privacy'];
const appsFile = path.join(WEB, 'i18n/apps.json');
const apps = fs.existsSync(appsFile)
  ? Object.entries(JSON.parse(fs.readFileSync(appsFile, 'utf8'))).filter(([id]) => !id.startsWith('_')).map(([id, a]) => ({ id, ...a }))
  : [];
for (const a of apps) {
  if (!APP_CATS.includes(a.category)) throw new Error(`apps.json: ${a.id} has unknown category ${a.category}`);
  // icons are drawn in code by each app; tools/web/render-icons.mjs exports them to web/src/app-icons
  const iconPng = path.join(WEB, 'src/app-icons', a.id + '.png');
  const iconSrc = fs.existsSync(iconPng) ? iconPng : a.icon && /\.(svg|png)$/.test(a.icon) && fs.existsSync(r(a.icon)) ? r(a.icon) : null;
  a.iconImg = iconSrc ? icon(iconSrc, a.id) : null;
  if (!a.iconImg) console.warn('[web] apps: no icon for', a.id, a.icon);
  a.shot = a.screenshot && fs.existsSync(r(a.screenshot)) ? { thumb: webp(r(a.screenshot), 720), full: webp(r(a.screenshot), 1600) } : null;
}
if (apps.length) log(`apps: ${apps.length} apps`);
const HAS_APPS = apps.length > 0;

// ---------- pages ----------
const team = JSON.parse(fs.readFileSync(path.join(WEB, 'i18n/team.json'), 'utf8'));
const today = new Date().toISOString().slice(0, 10);
const pagesStrings = JSON.parse(fs.readFileSync(path.join(WEB, 'i18n/pages.json'), 'utf8'));
const dict = Object.fromEntries(LANGS.map((l) => [l, { ...JSON.parse(fs.readFileSync(path.join(WEB, 'i18n', l + '.json'), 'utf8')), pages: pagesStrings[l] }]));
const loc = (v, l) => (v == null ? '' : typeof v === 'string' ? v : v[l] ?? v.en ?? '');

// ---------- feature pages and paper (web/content) ----------
const readJson = (f) => JSON.parse(fs.readFileSync(f, 'utf8'));
const featDir = path.join(CONTENT, 'features');
const featureList = fs.existsSync(featDir)
  ? fs.readdirSync(featDir).filter((f) => f.endsWith('.meta.json')).map((f) => readJson(path.join(featDir, f)))
    .filter((m) => m.slug && LANGS.every((l) => fs.existsSync(path.join(featDir, `${m.slug}.${l}.md`))))
    .sort((a, b) => (a.order ?? 99) - (b.order ?? 99))
  : [];
const paperDir = path.join(CONTENT, 'paper');
const HAS_PAPER = !process.argv.includes('--no-paper') && LANGS.every((l) => fs.existsSync(path.join(paperDir, `paper.${l}.md`)));
const paperMeta = HAS_PAPER && fs.existsSync(path.join(paperDir, 'meta.json')) ? readJson(path.join(paperDir, 'meta.json')) : {};
const paperDiagrams = HAS_PAPER && fs.existsSync(path.join(paperDir, 'diagrams.json')) ? readJson(path.join(paperDir, 'diagrams.json')) : {};
const legalDir = path.join(CONTENT, 'legal');
const legalIndex = fs.existsSync(path.join(legalDir, 'pages.json')) ? readJson(path.join(legalDir, 'pages.json')) : null;
const hasPh = (text) => /\[\[[A-Z][A-Z0-9_]*\]\]/.test(text);
// production: legal pages that still contain [[PLACEHOLDERS]] are left out (--allow-placeholders keeps them, highlighted)
const legalAll = legalIndex ? (legalIndex.pages || []).filter((p) => LANGS.every((l) => fs.existsSync(path.join(legalDir, `${p.slug}.${l}.md`)))) : [];
const LEGAL_MOVED = [['cookies', 'cookies-and-statistics']];
const legalPages = legalAll.filter((p) => !LEGAL_MOVED.some(([from, to]) => p.slug === from && legalAll.some((x) => x.slug === to))).filter((p) => ALLOW_PH || !LANGS.some((l) => hasPh(fs.readFileSync(path.join(legalDir, `${p.slug}.${l}.md`), 'utf8')) || hasPh(JSON.stringify(p))));
const legalHeld = legalAll.filter((p) => !legalPages.includes(p) && !LEGAL_MOVED.some(([from]) => from === p.slug)).map((p) => p.slug);
const LEGAL_GROUPS = ['legal', 'trust'];
const TRUST = new Set(['security', 'code-signing', 'issues', 'support-lifecycle', 'standards', 'code-of-conduct', 'contributing', 'accessibility']);
const legalGroup = (p) => (p.group === 'trust' || p.group === 'legal' ? p.group : TRUST.has(p.slug) ? 'trust' : 'legal');
// one-line description for the /legal/ index: pages.json "description", else the first sentence of the page
function legalDesc(p, l) {
  const d = loc(p.description, l);
  if (d) return d;
  const md = fs.readFileSync(path.join(legalDir, `${p.slug}.${l}.md`), 'utf8').replace(/^---[\s\S]*?---\n/, '');
  const para = md.split(/\n\s*\n/).map((x) => x.trim()).find((x) => x && !x.startsWith('#') && !x.startsWith('|') && !x.startsWith('-')) || '';
  const plain = para.replace(/\[([^\]]+)\]\([^)]*\)/g, '$1').replace(/[`*_]/g, '').replace(/\s+/g, ' ');
  const m = plain.match(/^(.{40,220}?[.!?])(\s|$)/);
  return m ? m[1] : plain.slice(0, 200);
}
const faqFile = path.join(CONTENT, 'faq/faq.json');
const faqJson = fs.existsSync(faqFile) ? readJson(faqFile) : null;
let faqHeld = 0;
if (faqJson && !ALLOW_PH) {
  for (const c of faqJson.categories || []) { const n = (c.items || []).length; c.items = (c.items || []).filter((it) => !hasPh(JSON.stringify(it))); faqHeld += n - c.items.length; }
  faqJson.categories = (faqJson.categories || []).filter((c) => c.items.length);
}
const HAS_FAQ = !!(faqJson && (faqJson.categories || []).length);
const footerFile = path.join(CONTENT, 'footer.json');
const footerJson = fs.existsSync(footerFile) ? readJson(footerFile) : null;
log(`content: ${legalPages.length} legal pages${legalHeld.length ? ` (held back, placeholders: ${legalHeld.join(', ')})` : ''}, FAQ ${HAS_FAQ ? 'yes' : 'no'}${faqHeld ? ` (${faqHeld} questions with placeholders hidden)` : ''}, footer.json ${footerJson ? 'yes' : 'no'}`);
log(`content: ${featureList.length} feature pages, paper ${HAS_PAPER ? 'yes' : 'no'} (from ${path.relative(REPO, CONTENT) || CONTENT})`);
const LUMO = Object.fromEntries(['wave', 'shield', 'run', 'sleep', 'celebrate', 'confused', 'front', 'three-quarter', 'back'].filter((p) => fs.existsSync(r(`assets/brand/mascot/lumo-${p}.svg`))).map((p) => [p, asset(r(`assets/brand/mascot/lumo-${p}.svg`), `lumo-${p}`)]));
let HAS_PDF = false;
if (HAS_PAPER && TOOLS) { try { createRequire(path.join(TOOLS, 'package.json')).resolve('playwright'); HAS_PDF = true; } catch { /* no playwright */ } }
if (HAS_PAPER && !HAS_PDF) console.warn('[web] paper: set WEB_TOOLS to a folder with playwright to build the PDFs; the PDF link is left out');
const prefix = (l) => (l === 'en' ? '/' : `/${l}/`);
const pageUrl = (l, page) => prefix(l) + (page === 'home' ? '' : page + '/');
const get = (obj, key) => key.split('.').reduce((o, k) => (o == null ? o : o[k]), obj);

function render(tpl, vars, t) {
  return tpl
    .replace(/\{\{\{([\w.]+)\}\}\}/g, (_, k) => { const v = get(vars, k); if (v == null) throw new Error('missing raw ' + k); return v; })
    .replace(/\{\{t\.([\w.]+)\}\}/g, (_, k) => { const v = get(t, k); if (v == null) throw new Error(`missing ${t.lang}.${k}`); return esc(v); });
}

function head(t, page, over, doc) {
  const meta = over || {
    home: [t.meta.title, t.meta.description],
    gallery: [t.meta.galleryTitle, t.meta.galleryDescription],
    apps: [t.apps.metaTitle, t.apps.metaDescription],
  }[page];
  const [title, desc] = meta;
  const ogImage = SITE + '/social-card.png';
  const url = SITE + pageUrl(t.lang, page);
  const alts = LANGS.map((l) => `<link rel="alternate" hreflang="${l}" href="${SITE}${pageUrl(l, page)}">`).join('\n');
  return `<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(title)}</title>
<meta name="description" content="${esc(desc)}">
<link rel="canonical" href="${url}">
${alts}
<link rel="alternate" hreflang="x-default" href="${SITE}${pageUrl('en', page)}">
<meta name="color-scheme" content="light dark">
<meta name="theme-color" content="#FBFAFF" media="(prefers-color-scheme: light)">
<meta name="theme-color" content="#0F0E1F" media="(prefers-color-scheme: dark)">
<script src="${themeUrl}"></script>
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="icon" href="/favicon-32.png" type="image/png" sizes="32x32">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
<link rel="preload" href="${fontExtraBold}" as="font" type="font/woff2" crossorigin>
<link rel="preload" href="${fontMedium}" as="font" type="font/woff2" crossorigin>
<link rel="stylesheet" href="${cssUrl}">${doc ? `\n<link rel="stylesheet" href="${docCssUrl}">` : ''}
<meta property="og:type" content="website">
<meta property="og:site_name" content="LordOS">
<meta property="og:title" content="${esc(title)}">
<meta property="og:description" content="${esc(desc)}">
<meta property="og:url" content="${url}">
<meta property="og:locale" content="${t.ogLocale}">
${LANGS.filter((l) => l !== t.lang).map((l) => `<meta property="og:locale:alternate" content="${dict[l].ogLocale}">`).join('\n')}
<meta property="og:image" content="${SITE}/social-card.png">
<meta property="og:image:width" content="1200">
<meta property="og:image:height" content="630">
<meta property="og:image:alt" content="LordOS">
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:title" content="${esc(title)}">
<meta name="twitter:description" content="${esc(desc)}">
<meta name="twitter:image" content="${SITE}/social-card.png">`;
}

const THEME_ICONS = {
  auto: '<svg data-m="auto" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M12 3v18" /><path d="M12 3a9 9 0 010 18z" fill="currentColor"/></svg>',
  light: '<svg data-m="light" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>',
  dark: '<svg data-m="dark" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" aria-hidden="true"><path d="M21 13A9 9 0 1111 3a7 7 0 0010 10z"/></svg>',
};

function header(t, page) {
  const home = prefix(t.lang);
  const h = (frag) => (page === 'home' ? frag : home + frag);
  const cur = (p) => (page === p ? ' aria-current="page"' : '');
  const links = [
    `<a href="${h('#features')}">${esc(t.navFeatures)}</a>`,
    HAS_APPS ? `<a href="${pageUrl(t.lang, 'apps')}"${cur('apps')}>${esc(t.navApps)}</a>` : '',
    `<a href="${pageUrl(t.lang, 'gallery')}"${cur('gallery')}>${esc(t.navGallery)}</a>`,
    HAS_PAPER ? `<a href="${pageUrl(t.lang, 'paper')}"${cur('paper')}>${esc(t.pages.navPaper)}</a>` : '',
    HAS_FAQ ? `<a href="${pageUrl(t.lang, 'faq')}"${cur('faq')}>${esc(t.pages.navFaq)}</a>` : '',
    `<a href="${h('#download')}">${esc(t.navDownload)}</a>`,
    `<a href="${h('#support')}">${esc(t.navSupport)}</a>`,
    `<a href="${pageUrl(t.lang, 'about')}"${cur('about')}>${esc(t.navAbout)}</a>`,
    `<a href="${GITHUB}">GitHub</a>`,
  ].filter(Boolean).join('\n');
  const langs = LANGS.map((l) =>
    `<a class="lang" href="${pageUrl(l, page)}" hreflang="${l}" lang="${l}" title="${esc(dict[l].langName)}"${l === t.lang ? ' aria-current="page"' : ''}>${l.toUpperCase()}</a>`).join('\n');
  return `<header class="top">
<nav class="wrap${page === 'home' ? '' : ' wide'} nav" aria-label="${esc(t.navLabel)}">
<a class="nav-home" href="${home}" aria-label="${esc(t.homeLabel)}"><img class="logo-l" src="${img.logoLight}" alt="LordOS" width="122" height="36"><img class="logo-d" src="${img.logoDark}" alt="LordOS" width="122" height="36"></a>
<div class="nav-links">
${links}
</div>
<div class="tools">
<button type="button" class="theme-btn" data-label="${esc(t.themeLabel)}" data-auto="${esc(t.themeAuto)}" data-light="${esc(t.themeLight)}" data-dark="${esc(t.themeDark)}" aria-label="${esc(t.themeLabel)}">${THEME_ICONS.auto}${THEME_ICONS.light}${THEME_ICONS.dark}<span class="tl">${esc(t.themeAuto)}</span></button>
<div class="langs" role="group" aria-label="${esc(t.langLabel)}">
${langs}
</div>
</div>
</nav>
</header>`;
}

// localized site path for footer.json links: /x/ -> /es/x/; files and absolute URLs unchanged
const sitePath = (href, l) => {
  if (!href || /^(https?:|mailto:)/.test(href)) return href;
  if (href === '/#about' || href === '#about') return pageUrl(l, 'about');
  if (href.startsWith('#')) return prefix(l) + href;
  if (/\.[a-z0-9]{2,5}$/i.test(href.split('#')[0]) || href.startsWith('/.well-known/') || href.startsWith('/licenses/')) return href;
  return prefix(l) + href.replace(/^\//, '');
};
// a footer link to a site page is kept only if that page is built
function linkBuilt(href) {
  if (!href || /^(https?:|mailto:|#)/.test(href)) return true;
  const p = href.split('#')[0].replace(/^\//, '').replace(/\/$/, '');
  if (!p) return true;
  if (/\.[a-z0-9]{2,5}$/i.test(p)) return p !== '.well-known/security.txt' || fs.existsSync(path.join(DIST, p)) || !hasPh(fs.existsSync(path.join(CONTENT, 'well-known/security.txt')) ? fs.readFileSync(path.join(CONTENT, 'well-known/security.txt'), 'utf8') : '');
  return builtPages().has(p);
}
let _built = null;
function builtPages() {
  if (!_built) _built = new Set(['', 'apps', 'gallery', 'about', ...featureList.map((m) => 'features/' + m.slug), ...(HAS_PAPER ? ['paper'] : []), ...(HAS_FAQ ? ['faq'] : []), ...legalPages.map((x) => 'legal/' + x.slug), ...(legalPages.length ? ['legal', ...LEGAL_MOVED.map((m) => 'legal/' + m[0])] : [])]);
  return _built;
}
function langLinks(t, page) {
  return LANGS.map((l) => `<a class="lang" href="${pageUrl(l, page)}" hreflang="${l}" lang="${l}" title="${esc(dict[l].langName)}"${l === t.lang ? ' aria-current="page"' : ''}>${l.toUpperCase()}</a>`).join('\n');
}
function themeButton(t, withText) {
  return `<button type="button" class="theme-btn" data-label="${esc(t.themeLabel)}" data-auto="${esc(t.themeAuto)}" data-light="${esc(t.themeLight)}" data-dark="${esc(t.themeDark)}" aria-label="${esc(t.themeLabel)}">${THEME_ICONS.auto}${THEME_ICONS.light}${THEME_ICONS.dark}${withText ? `<span class="tl">${esc(t.themeAuto)}</span>` : ''}</button>`;
}
function footer(t, page = 'home') {
  const l = t.lang;
  if (!footerJson) {
    return `<footer class="foot">
<div class="wrap">
<img src="${img.logoDark}" alt="LordOS" width="108" height="32" loading="lazy">
<div class="foot-links">
<a href="${GITHUB}">GitHub</a>
<a href="${GITHUB}/blob/main/LICENSE">${esc(t.license)}</a>
<a href="${GITHUB}/blob/main/docs/LORDOS-BRAND.md">${esc(t.brand)}</a>
<a href="/licenses/PlusJakartaSans-OFL.txt">${esc(t.fontsNote)}</a>
</div>
<p>${esc(t.footer)}</p>
</div>
</footer>`;
  }
  const cols = (footerJson.columns || []).map((c, i) => `<nav class="fcol" aria-labelledby="fcol-${i}">
<h2 id="fcol-${i}">${esc(loc(c.title, l))}</h2>
<ul>
${(c.links || []).filter((k) => linkBuilt(k.href)).map((k) => `<li><a href="${esc(sitePath(k.href, l))}"${/^https?:/.test(k.href) ? ' rel="noopener"' : ''}>${phOut(esc(phIn(loc(k.label, l), 'footer.json')))}</a></li>`).join('\n')}
</ul>
</nav>`).join('\n');
  return `<footer class="foot">
<div class="wrap fcols">
<div class="fbrand">
<img src="${img.logoDark}" alt="LordOS" width="108" height="32" loading="lazy">
<p>${esc(loc(footerJson.tagline, l) || t.meta.description)}</p>
</div>
${cols}
</div>
<div class="fbar">
<div class="wrap">
<p>${phOut(esc(phIn(loc(footerJson.copyright, l) || t.footer, 'footer.json')))}${footerJson.note ? ` ${phOut(esc(phIn(loc(footerJson.note, l), 'footer.json')))}` : ''}</p>
<div class="tools">
${themeButton(t, false)}
<div class="langs" role="group" aria-label="${esc(t.langLabel)}">
${langLinks(t, page)}
</div>
</div>
</div>
</div>
</footer>`;
}

const ICONS = ['M4 17l6-6-6-6M12 19h8', 'M12 3l8 4v5c0 5-3.5 8-8 9-4.5-1-8-4-8-9V7z', 'M3 12h18M12 3a14 14 0 010 18M12 3a14 14 0 000 18M12 3a9 9 0 110 18 9 9 0 010-18', 'M7 11V8a5 5 0 0110 0v3M5 11h14v10H5z', 'M4 6h16v12H4zM4 7l8 6 8-6', 'M4 5h16v14H4zM4 9h16M8 5v4'];
const SHOWCASE_IMG = [r('docs/fans-monitor.png'), r('docs/polish/passwords-after-light.png'), r('docs/brand/screenshots/settings-light.png')];
const TEASER_IDS = ['mail', 'calendar', 'files', 'passwords', 'photos', 'player', 'notes', 'monitor', 'terminal', 'store'];

function banner() {
  const data = (l) => esc(JSON.stringify({ text: dict[l].banner, go: dict[l].bannerGo, close: dict[l].bannerClose, href: prefix(l) }));
  return `<div class="lbar" id="lbar" role="region" aria-label="Language" hidden data-es="${data('es')}" data-pt="${data('pt')}"><p></p><a href="/es/"></a><button type="button"></button></div>`;
}

const countPill = (t, n) => `<span class="n" aria-label="${esc(t.gallery.imagesCount.replace('{n}', n))}">${n}</span>`;
const appName = (a, l) => (a.name && (a.name[l] || a.name.en)) || a.id;

function teaser(t) {
  if (!HAS_APPS) return '';
  const pick = TEASER_IDS.map((id) => apps.find((a) => a.id === id && a.iconImg)).filter(Boolean);
  for (const a of apps) if (pick.length < 8 && a.iconImg && !pick.includes(a)) pick.push(a);
  const icons = pick.slice(0, 8).map((a) => `<li><img src="${a.iconImg.url}" width="56" height="56" alt="" loading="lazy">${esc(appName(a, t.lang))}</li>`).join('\n');
  return `<section class="wrap teaser" aria-labelledby="teaser-h">
<div class="teaser-box">
<div class="teaser-txt">
<p class="kicker">${esc(t.apps.teaserKicker)}</p>
<h2 id="teaser-h" class="h2s">${esc(t.apps.teaserTitle)}</h2>
<p>${esc(t.apps.teaserText)}</p>
<a class="btn btn-primary" href="${pageUrl(t.lang, 'apps')}">${esc(t.apps.seeAll)}</a>
</div>
<ul class="teaser-icons">
${icons}
</ul>
</div>
</section>`;
}

const tpl = { home: src('home.html'), gallery: src('gallery.html'), apps: src('apps.html'), notFound: src('404.html'), lightbox: src('lightbox.html'), feature: src('feature.html'), paper: src('paper.html'), legal: src('legal.html'), legalIndex: src('legal-index.html'), faq: src('faq.html'), about: src('about.html') };

for (const l of LANGS) {
  const t = dict[l];
  const common = (page, over, doc) => ({ img, js: jsUrl, header: header(t, page), footer: footer(t, page), head: head(t, page, over, doc), lightbox: render(tpl.lightbox, {}, t) });

  // home
  const features = t.features.map(([title, text], i) => `<article class="feat">
<div class="feat-ico" aria-hidden="true"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="${ICONS[i]}"/></svg></div>
<h3>${esc(title)}</h3>
<p>${esc(text)}</p>
${featureList[i] ? `<a class="more" href="${pageUrl(l, 'features/' + featureList[i].slug)}">${esc(t.pages.learnMore)}<span class="vh">: ${esc(title)}</span> <span aria-hidden="true">→</span></a>` : ''}
</article>`).join('\n');
  const showcase = t.showcase.map(([kicker, title, text, alt], i) => `<div class="show-row${i % 2 ? ' rev' : ''}">
<div class="show-txt">
<p class="kicker">${esc(kicker)}</p>
<h3>${esc(title)}</h3>
<p class="t">${esc(text)}</p>
</div>
<div class="show-img">${picture(SHOWCASE_IMG[i], alt, { sizes: '(max-width: 900px) 100vw, 640px' })}</div>
</div>`).join('\n');
  write(path.join(pageUrl(l, 'home'), 'index.html'), render(tpl.home, {
    ...common('home'), features, showcase, teaser: teaser(t),
    heroImg: (() => {
      // the desktop inside a laptop drawn in CSS; light and dark screenshots follow the theme
      const L = r('docs/screenshots/desktop-overview-light.png'), D = r('docs/screenshots/desktop-overview-dark.png');
      const im = (f, cls, eager) => { const s1 = webp(f, 720), s2 = webp(f, 1600); return `<img class="${cls}" src="${s2.url}" srcset="${s1.url} ${s1.w}w, ${s2.url} ${s2.w}w" sizes="(max-width: 1040px) 92vw, 920px" width="${s2.w}" height="${s2.h}" alt="${esc(t.pages.hero.alt)}"${eager ? ' fetchpriority="high"' : ' loading="lazy"'} decoding="async">`; };
      const pills = t.pages.hero.pills.map((p, k) => `<span class="pill p${k + 1}">${esc(p)}</span>`).join('');
      return `<div class="laptop"><div class="lp-screen"><span class="lp-cam" aria-hidden="true"></span><div class="lp-display">${im(L, 'lp-l', true)}${im(D, 'lp-d', false)}</div></div><div class="lp-base" aria-hidden="true"><span class="lp-notch"></span></div><p class="pills">${pills}</p></div>`;
    })(),
    fingerprint: FINGERPRINT, banner: l === 'en' ? banner() : '',
  }, t));

  // gallery
  const cats = [...new Set(shots.map((s) => s.category))];
  const catLabel = (c) => t.cats[c] || c.charAt(0).toUpperCase() + c.slice(1);
  const catButtons = [`<button type="button" class="g-chip" data-cat="all" aria-pressed="true">${esc(t.gallery.all)} ${countPill(t, shots.length)}</button>`]
    .concat(cats.map((c) => `<button type="button" class="g-chip" data-cat="${esc(c)}" aria-pressed="false">${esc(catLabel(c))} ${countPill(t, shots.filter((s) => s.category === c).length)}</button>`)).join('\n');
  const shotsHtml = shots.map((s, i) => {
    const cap = (s.caption && (s.caption[l] || s.caption.en)) || path.basename(s.file);
    const { thumb, full } = shotImgs[i];
    return `<figure class="g-fig" data-item data-cat="${esc(s.category)}" data-theme="${esc(s.theme || 'light')}">
<a class="g-card" href="${full.url}" data-w="${full.w}" data-h="${full.h}" data-cap="${esc(cap)}" aria-label="${esc(t.gallery.open + ': ' + cap)}"><img src="${thumb.url}" width="${thumb.w}" height="${thumb.h}" alt="${esc(cap)}" loading="lazy" decoding="async"></a>
<figcaption><span class="g-cap">${esc(cap)}</span><span class="g-tag">${esc(catLabel(s.category))}</span></figcaption>
</figure>`;
  }).join('\n');
  write(path.join(pageUrl(l, 'gallery'), 'index.html'), render(tpl.gallery, { ...common('gallery'), catButtons, shots: shotsHtml }, t));

  // apps
  if (HAS_APPS) {
    const present = APP_CATS.filter((c) => apps.some((a) => a.category === c));
    const appCount = (n) => `<span class="n" aria-label="${n} apps">${n}</span>`;
    const catButtons = [`<button type="button" class="g-chip" data-cat="all" aria-pressed="true">${esc(t.apps.all)} ${appCount(apps.length)}</button>`]
      .concat(present.map((c) => `<button type="button" class="g-chip" data-cat="${c}" aria-pressed="false">${esc(t.apps.cats[c])} ${appCount(apps.filter((a) => a.category === c).length)}</button>`)).join('\n');
    const card = (a) => {
      const name = appName(a, l);
      const feats = (a.features && (a.features[l] || a.features.en)) || [];
      const tag = a.tagline && (a.tagline[l] || a.tagline.en);
      const shot = a.shot ? `<a class="g-card" href="${a.shot.full.url}" data-w="${a.shot.full.w}" data-h="${a.shot.full.h}" data-cap="${esc(name)}" aria-label="${esc(t.apps.shotOpen + ': ' + name)}"><img src="${a.shot.thumb.url}" width="${a.shot.thumb.w}" height="${a.shot.thumb.h}" alt="${esc(name)}" loading="lazy" decoding="async"></a>` : '';
      return `<article class="app" id="app-${esc(a.id)}">
<div class="app-head">${a.iconImg ? `<img src="${a.iconImg.url}" width="56" height="56" alt="" loading="lazy">` : ''}<div><h3>${esc(name)}</h3>${tag ? `<p class="app-tag">${esc(tag)}</p>` : ''}</div></div>
${a.isolated ? `<span class="iso" title="${esc(t.apps.isolatedTitle)}"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linejoin="round" aria-hidden="true"><path d="M12 3l8 4v5c0 5-3.5 8-8 9-4.5-1-8-4-8-9V7z"/></svg>${esc(t.apps.isolated)}</span>` : ''}
${feats.length ? `<ul>\n${feats.map((f) => `<li>${esc(f)}</li>`).join('\n')}\n</ul>` : ''}
${shot}
</article>`;
    };
    const sections = present.map((c) => `<section class="apps-sec" data-item data-cat="${c}" aria-labelledby="cat-${c}">
<h2 id="cat-${c}">${esc(t.apps.cats[c])}</h2>
<div class="apps-grid">
${apps.filter((a) => a.category === c).sort((x, y) => appName(x, l).localeCompare(appName(y, l), l)).map(card).join('\n')}
</div>
</section>`).join('\n');
    write(path.join(pageUrl(l, 'apps'), 'index.html'), render(tpl.apps, { ...common('apps'), catButtons, sections }, t));
  }

  // feature pages and paper share these helpers
  const ghLink = (u) => (/^(https?:|mailto:|#)/.test(u) ? u : u.startsWith('/') ? (linkBuilt(u) ? sitePath(u, l) : '') : `${GITHUB}/blob/main/${u.replace(/^\.?\//, '')}`);
  const shotFig = (file, cap, cls = 'shot-fig') => {
    const f = r(file);
    if (!fs.existsSync(f)) { console.warn('[web] missing screenshot', file); return ''; }
    const thumb = webp(f, 720), full = webp(f, 1600);
    return `<figure class="${cls}"><a class="g-card" href="${full.url}" data-w="${full.w}" data-h="${full.h}" data-cap="${esc(cap)}" aria-label="${esc(t.gallery.open + ': ' + cap)}"><img src="${thumb.url}" width="${thumb.w}" height="${thumb.h}" alt="${esc(cap)}" loading="lazy" decoding="async"></a>${cap ? `<figcaption>${esc(cap)}</figcaption>` : ''}</figure>`;
  };
  const diaStrings = { tones: t.pages.tones, legend: t.pages.legend, then: t.pages.then, connections: t.pages.connections };
  const renderMd = (srcText, diagrams, screenshots, usedShots, where = 'content') => { const res = markdown(phIn(srcText, where), {
    anchorLabel: t.pages.anchorLabel,
    link: ghLink,
    image: (url, alt, title) => shotFig(url, title || alt),
    block: (kind, arg) => {
      if (kind === 'diagram') {
        const d = (diagrams || []).find((x) => x.id === arg);
        if (!d) { console.warn('[web] unknown diagram', arg); return ''; }
        return diagram(d, l, diaStrings);
      }
      if (kind === 'shot') {
        if (/^\d+$/.test(arg || '')) {
          const sh = (screenshots || [])[+arg];
          if (!sh) { console.warn('[web] unknown shot', arg); return ''; }
          usedShots && usedShots.add(+arg);
          return shotFig(sh.file, loc(sh.caption, l));
        }
        return arg ? shotFig(arg, '') : '';
      }
      return '';
    },
  }); res.html = phOut(res.html); return res; };

  featureList.forEach((m, i) => {
    const page = 'features/' + m.slug;
    const { body } = frontMatter(fs.readFileSync(path.join(featDir, `${m.slug}.${l}.md`), 'utf8'));
    const used = new Set();
    // the page renders meta.sources as "Read the source"; drop a trailing markdown section that only
    // repeats those GitHub links
    const parts = body.split(/^(?=## )/m);
    const last = parts[parts.length - 1];
    const lines = last.split('\n').slice(1).map((x) => x.trim()).filter(Boolean);
    const mdBody = m.sources && m.sources.length && lines.length && lines.every((x) => /^[-*]\s+\[[^\]]+\]\(https:\/\/github\.com\//.test(x)) ? parts.slice(0, -1).join('') : body;
    const { html: bodyHtml, headings } = renderMd(mdBody, m.diagrams, m.screenshots, used, `features/${m.slug}.${l}.md`);
    const title = loc(m.title, l) || (headings[0] && headings[0].text) || m.slug;
    const rest = (m.screenshots || []).map((sh, k) => (used.has(k) ? '' : shotFig(sh.file, loc(sh.caption, l), 'shot-fig'))).filter(Boolean);
    const shots = rest.length ? `<section class="prose" aria-labelledby="shots-h"><h2 id="shots-h">${esc(t.pages.shotsTitle)}</h2><div class="shots">${rest.join('')}</div></section>` : '';
    const sources = (m.sources || []).map((s) => `<li><a href="${esc(ghLink(s.path || s.url))}" rel="noopener">${esc(s.path || s.url)}</a><span>${esc(loc(s.what, l))}</span></li>`).join('\n');
    const prev = featureList[(i - 1 + featureList.length) % featureList.length], next = featureList[(i + 1) % featureList.length];
    const pn = featureList.length > 1
      ? `<li><a href="${pageUrl(l, 'features/' + prev.slug)}" rel="prev"><small>← ${esc(t.pages.prev)}</small><strong>${esc(loc(prev.title, l))}</strong></a></li>
<li class="next"><a href="${pageUrl(l, 'features/' + next.slug)}" rel="next"><small>${esc(t.pages.next)} →</small><strong>${esc(loc(next.title, l))}</strong></a></li>`
      : '';
    const pose = (m.lumo && m.lumo.pose) || 'front';
    write(path.join(pageUrl(l, page), 'index.html'), render(tpl.feature, {
      ...common(page, [`${title} · LordOS`, loc(m.tagline, l) || t.meta.description], true),
      lumo: LUMO[pose] || LUMO.front || img.lumoWave,
      title: esc(title), tagline: esc(loc(m.tagline, l)), intro: inline(loc(m.lumo && m.lumo.intro, l)),
      body: bodyHtml, shots, sources, prevnext: pn,
    }, t));
  });

  if (HAS_PAPER) {
    const { data: fm, body } = frontMatter(fs.readFileSync(path.join(paperDir, `paper.${l}.md`), 'utf8'));
    const meta = { ...fm, ...paperMeta };
    const { html: bodyHtml, headings } = renderMd(body, paperDiagrams.diagrams || [], paperDiagrams.screenshots || meta.screenshots || []);
    const title = loc(meta.title, l) || fm.title || 'LordOS';
    const subtitle = loc(meta.subtitle, l) || fm.subtitle || '';
    // two-level table of contents from ## and ###
    let toc = '<ol>', open = false;
    for (const h of headings.filter((x) => x.level === 2 || x.level === 3)) {
      if (h.level === 2) { if (open) toc += '</ol></li>'; else if (toc !== '<ol>') toc += '</li>'; toc += `<li><a href="#${h.id}">${esc(h.text)}</a>`; open = false; }
      else { if (!open) { toc += '<ol>'; open = true; } toc += `<li><a href="#${h.id}">${esc(h.text)}</a></li>`; }
    }
    toc += (open ? '</ol></li>' : toc === '<ol>' ? '' : '</li>') + '</ol>';
    const authors = Array.isArray(meta.authors) ? meta.authors.join(', ') : meta.authors || '';
    const metaLine = [
      meta.version ? `<span>${esc(t.pages.paper.version)} <strong>${esc(meta.version)}</strong></span>` : '',
      authors ? `<span>${esc(t.pages.paper.authors)}: <strong>${esc(authors)}${meta.org ? ` · ${esc(loc(meta.org, l))}` : ''}</strong></span>` : '',
      meta.date ? `<span>${esc(t.pages.paper.date)}: <strong><time datetime="${esc(meta.date)}">${esc(new Date(meta.date + 'T12:00:00Z').toLocaleDateString(l === 'en' ? 'en-US' : l === 'es' ? 'es-AR' : 'pt-BR', { year: 'numeric', month: 'long', day: 'numeric', timeZone: 'UTC' }))}</time></strong></span>` : '',
    ].filter(Boolean).join('');
    const pdf = HAS_PDF ? `<a class="btn btn-primary" href="${pageUrl(l, 'paper')}lordos-paper-${l}.pdf" download><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 4v11M7 10l5 5 5-5M5 20h14"/></svg>${esc(t.pages.paper.pdf)}</a>` : '';
    write(path.join(pageUrl(l, 'paper'), 'index.html'), render(tpl.paper, {
      ...common('paper', [`${title} · LordOS`, t.pages.paper.metaDescription], true),
      title: esc(title), subtitle: esc(subtitle), meta: metaLine, pdf, toc, body: bodyHtml,
    }, t));
  }

  // legal pages
  const fmtDate = (iso) => new Date(iso + 'T12:00:00Z').toLocaleDateString(l === 'en' ? 'en-US' : l === 'es' ? 'es-AR' : 'pt-BR', { year: 'numeric', month: 'long', day: 'numeric', timeZone: 'UTC' });
  for (const lp of legalPages) {
    const page = 'legal/' + lp.slug;
    const { body } = frontMatter(fs.readFileSync(path.join(legalDir, `${lp.slug}.${l}.md`), 'utf8'));
    const { html: bodyHtml } = renderMd(body, [], [], null, `legal/${lp.slug}.${l}.md`);
    const title = loc(lp.title, l) || lp.slug;
    const updated = lp.updated || legalIndex.updated || today;
    const nav = LEGAL_GROUPS.map((g) => {
      const ps = legalPages.filter((x) => legalGroup(x) === g);
      return ps.length ? `<h2>${esc(t.pages.legal.groups[g])}</h2>\n<ul>\n${ps.map((x) => `<li><a href="${pageUrl(l, 'legal/' + x.slug)}"${x.slug === lp.slug ? ' aria-current="page"' : ''}>${esc(loc(x.title, l) || x.slug)}</a></li>`).join('\n')}\n</ul>` : '';
    }).join('\n');
    write(path.join(pageUrl(l, page), 'index.html'), render(tpl.legal, {
      ...common(page, [`${title} · LordOS`, loc(lp.description, l) || `${title} — lordos.org`], true),
      title: phOut(esc(phIn(title, 'legal/pages.json'))), updated: esc(fmtDate(updated)), updatedIso: esc(updated), nav, body: bodyHtml, indexUrl: pageUrl(l, 'legal'),
    }, t));
  }

  // legal index and redirects for moved legal pages
  if (legalPages.length) {
    const groups = LEGAL_GROUPS.map((g) => {
      const ps = legalPages.filter((x) => legalGroup(x) === g);
      if (!ps.length) return '';
      return `<section aria-labelledby="lg-${g}"><h2 id="lg-${g}">${esc(t.pages.legal.groups[g])}</h2><ul class="lcards">${ps.map((x) => `<li><a href="${pageUrl(l, 'legal/' + x.slug)}"><strong>${esc(loc(x.title, l) || x.slug)}</strong><span>${esc(legalDesc(x, l))}</span></a></li>`).join('')}</ul></section>`;
    }).join('\n');
    write(path.join(pageUrl(l, 'legal'), 'index.html'), render(tpl.legalIndex, { ...common('legal', [`${t.pages.legal.indexTitle} · LordOS`, t.pages.legal.metaIndex], true), groups }, t));
    for (const [from, to] of LEGAL_MOVED) {
      if (legalPages.some((x) => x.slug === from) || !legalPages.some((x) => x.slug === to)) continue;
      const dest = SITE + pageUrl(l, 'legal/' + to);
      write(path.join(pageUrl(l, 'legal/' + from), 'index.html'), `<!doctype html>
<html lang="${l}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(loc(legalPages.find((x) => x.slug === to).title, l))} · LordOS</title>
<meta name="robots" content="noindex">
<link rel="canonical" href="${dest}">
<meta http-equiv="refresh" content="0; url=${dest}">
<link rel="stylesheet" href="${cssUrl}">
</head>
<body>
<p class="moved">${esc(t.pages.legal.moved)} <a href="${dest}">${dest}</a></p>
</body>
</html>
`);
    }
  }

  // FAQ
  if (HAS_FAQ) {
    const cats = faqJson.categories;
    const chips = cats.map((c) => `<a class="g-chip" href="#${esc(c.id)}">${esc(loc(c.title, l))} <span class="n">${(c.items || []).length}</span></a>`).join('\n');
    const ld = { '@context': 'https://schema.org', '@type': 'FAQPage', inLanguage: l, mainEntity: [] };
    const sections = cats.map((c) => `<section class="faq-sec" id="${esc(c.id)}" aria-labelledby="${esc(c.id)}-h">
<h2 id="${esc(c.id)}-h">${esc(loc(c.title, l))}</h2>
${(c.items || []).map((it) => {
    const q = loc(it.q, l), a = loc(it.a, l);
    const { html } = renderMd(a, [], [], null, `faq.json ${it.id}`);
    ld.mainEntity.push({ '@type': 'Question', name: q, acceptedAnswer: { '@type': 'Answer', text: html.replace(/<[^>]+>/g, ' ').replace(/\s+/g, ' ').replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').trim() } });
    return `<details id="${esc(it.id)}"><summary>${phOut(esc(phIn(q, 'faq.json')))}</summary><div class="prose">${html}</div></details>`;
  }).join('\n')}
</section>`).join('\n');
    const jsonld = `<script type="application/ld+json">${JSON.stringify(ld).replace(/</g, '\\u003c')}</script>`;
    write(path.join(pageUrl(l, 'faq'), 'index.html'), render(tpl.faq, {
      ...common('faq', [`${t.pages.faq.title} · LordOS`, t.pages.faq.lead], true), chips, sections, jsonld,
    }, t));
  }

  // about
  {
    const A = t.pages.about;
    const fill = (s, name) => s.replace('{name}', name);
    const li = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M14 4h6v6M20 4l-9 9M18 14v5a1 1 0 01-1 1H5a1 1 0 01-1-1V7a1 1 0 011-1h5"/></svg>';
    const people = team.people.map((p) => {
      const photo = p.photo && fs.existsSync(r(p.photo))
        ? `<img class="avatar" src="${webp(r(p.photo), 336, 82).url}" alt="${esc(fill(A.photoOf, p.name))}" width="168" height="168" loading="lazy">`
        : `<div class="avatar initials" role="img" aria-label="${esc(fill(A.initialsOf, p.name))}">${esc(p.initials)}</div>`;
      const link = p.linkedin && !/^\[\[/.test(p.linkedin)
        ? `<a class="btn btn-ghost" href="${esc(p.linkedin)}" rel="noopener" aria-label="${esc(fill(A.linkedinOf, p.name))}">${li}LinkedIn</a>` : '';
      return `<article class="creator">${photo}<h3>${esc(p.name)}</h3><p>${esc(A.role)}</p>${link}</article>`;
    }).join('\n');
    const work = A.work.map(([title, text]) => `<article class="feat"><h3>${esc(title)}</h3><p>${esc(text)}</p></article>`).join('\n');
    write(path.join(pageUrl(l, 'about'), 'index.html'), render(tpl.about, {
      ...common('about', [A.metaTitle, A.metaDescription]),
      people, work, liIcon: li, lumoWave: img.lumoWave,
      siteUrl: esc(team.company.site.replace('{lang}', l)), companyLinkedin: esc(team.company.linkedin),
      contactUrl: esc(team.company.contact.replace('{lang}', l)), supportUrl: prefix(l) + '#support',
    }, t));
  }
}

// 404 (English, noindex)
write('404.html', render(tpl.notFound, {
  img,
  head: `<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Page not found · LordOS</title>
<meta name="robots" content="noindex">
<meta name="color-scheme" content="light dark">
<script src="${themeUrl}"></script>
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="${cssUrl}">`,
}, dict.en));

// ---------- root files ----------
write('favicon.svg', read(r('docs/brand/favicon/favicon.svg')));
write('favicon-32.png', read(r('docs/brand/favicon/favicon-32.png')));
write('favicon-512.png', read(r('docs/brand/favicon/favicon-512.png')));
write('apple-touch-icon.png', read(r('docs/brand/favicon/apple-touch-icon-180.png')));
write('social-card.png', read(r('docs/brand/png/social-card-1200x630.png')));
write('lordos-release.pub.pem', read(r('tools/release/lordos-release.pub.pem')));
write('install.sh', read(r('tools/install/install.sh')));
execFileSync('cmp', [r('tools/install/install.sh'), path.join(DIST, 'install.sh')]); // throws if they differ
const secTxt = path.join(CONTENT, 'well-known/security.txt');
if (fs.existsSync(secTxt)) {
  const txt = fs.readFileSync(secTxt, 'utf8');
  if (!hasPh(txt) || ALLOW_PH) write('.well-known/security.txt', notePh(txt, 'well-known/security.txt'));
  else log('security.txt held back: it still has placeholders');
}
write('robots.txt', `User-agent: *\nAllow: /\n\nSitemap: ${SITE}/sitemap.xml\n`);
const allPages = [...PAGES.filter((p) => p !== 'apps' || HAS_APPS), ...featureList.map((m) => 'features/' + m.slug), ...(HAS_PAPER ? ['paper'] : []), ...(HAS_FAQ ? ['faq'] : []), 'about', ...legalPages.map((p) => 'legal/' + p.slug), ...(legalPages.length ? ['legal'] : [])];
const urls = allPages.flatMap((p) => LANGS.map((l) => `  <url>
    <loc>${SITE}${pageUrl(l, p)}</loc>
    <lastmod>${today}</lastmod>
${LANGS.map((a) => `    <xhtml:link rel="alternate" hreflang="${a}" href="${SITE}${pageUrl(a, p)}"/>`).join('\n')}
    <xhtml:link rel="alternate" hreflang="x-default" href="${SITE}${pageUrl('en', p)}"/>
  </url>`));
write('sitemap.xml', `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">
${urls.join('\n')}
</urlset>
`);

// ---------- checks ----------
if (placeholders.size) {
  const list = [...placeholders.keys()];
  if (ALLOW_PH) console.warn(`[web] PREVIEW: ${list.length} placeholders left (highlighted on the pages):\n  ` + list.join('\n  '));
  else throw new Error(`${list.length} content placeholders left; fill them in or build a preview with --allow-placeholders:\n  ` + list.join('\n  '));
}
const html = [];
(function walk(d) { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) walk(p); else if (p.endsWith('.html')) html.push(p); } })(DIST);
for (const f of html) {
  const s = fs.readFileSync(f, 'utf8');
  if (/<script(?![^>]*\ssrc=)(?![^>]*type="application\/ld\+json")[^>]*>/i.test(s)) throw new Error('inline <script> in ' + f);
  if (/\son[a-z]+=/i.test(s)) throw new Error('inline event handler in ' + f);
  if (/https?:\/\/(fonts\.|cdn)/i.test(s)) throw new Error('external font/CDN in ' + f);
  if (/operating system|sistema operativo|sistema operacional|\b(?:our|own|nuestro|propio|próprio|nosso)\s+X11\b|\bX11\s+(?:display\s+)?server\b|from scratch|desde cero|\bdo zero\b/i.test(s)) {
    const msg = 'forbidden wording in ' + path.relative(DIST, f) + ': ' + s.match(/.{0,60}(operating system|sistema operativo|sistema operacional|\b(?:our|own|nuestro|propio|próprio|nosso)\s+X11\b|\bX11\s+(?:display\s+)?server\b|from scratch|desde cero|\bdo zero\b).{0,40}/i)[0];
    if (process.env.WEB_SKIP_WORDING) console.warn('[web] WARNING ' + msg); else throw new Error(msg);
  }
  { const m = s.replace(/<[^>]+>/g, ' ').match(/.{0,70}(\bPi (4|400|500)\b|\bCM[45]\b).{0,50}/i); if (m) throw new Error(`only the Raspberry Pi 5 is supported; other models are not mentioned in website copy (${path.relative(DIST, f)}): …${m[0].trim()}…`); }
}
const jsSize = Buffer.byteLength(jsSrc) + Buffer.byteLength(themeSrc), cssSize = Buffer.byteLength(css);
if (jsSize > 15 * 1024) throw new Error(`JS is ${jsSize} bytes (limit 15 KB)`);
if (cssSize > 30 * 1024) throw new Error(`site.css is ${cssSize} bytes (limit 30 KB)`);
if (docCss.length > 30 * 1024) throw new Error(`doc.css is ${docCss.length} bytes (limit 30 KB)`);
log(`built ${html.length} pages into web/dist (js ${jsSize} B, css ${cssSize} B, ${shots.length} gallery images, ${apps.length} apps); install.sh byte-identical`);

// ---------- paper PDFs (headless Chromium prints the built page) ----------
if (HAS_PDF) {
  const { chromium } = createRequire(path.join(TOOLS, 'package.json'))('playwright');
  const types = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.svg': 'image/svg+xml', '.webp': 'image/webp', '.png': 'image/png', '.woff2': 'font/woff2' };
  const server = http.createServer((req, res) => {
    let p = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    if (p.endsWith('/')) p += 'index.html';
    const f = path.join(DIST, p);
    if (!f.startsWith(DIST) || !fs.existsSync(f)) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'content-type': types[path.extname(f)] || 'application/octet-stream' });
    res.end(fs.readFileSync(f));
  });
  await new Promise((ok) => server.listen(0, '127.0.0.1', ok));
  const port = server.address().port;
  const browser = await chromium.launch();
  for (const l of LANGS) {
    const page = await browser.newPage({ colorScheme: 'light' });
    await page.goto(`http://127.0.0.1:${port}${pageUrl(l, 'paper')}`, { waitUntil: 'networkidle' });
    await page.evaluate(() => document.querySelectorAll('img[loading=lazy]').forEach((i) => { i.loading = 'eager'; }));
    await page.waitForLoadState('networkidle');
    await page.emulateMedia({ media: 'print', colorScheme: 'light' });
    const title = await page.title();
    await page.pdf({
      path: path.join(DIST, pageUrl(l, 'paper'), `lordos-paper-${l}.pdf`), format: 'A4', printBackground: true,
      margin: { top: '18mm', bottom: '18mm', left: '16mm', right: '16mm' }, displayHeaderFooter: true,
      headerTemplate: '<span></span>',
      footerTemplate: `<div style="width:100%;font:9px sans-serif;color:#777;padding:0 16mm;display:flex;justify-content:space-between"><span>${esc(title)}</span><span><span class="pageNumber"></span> / <span class="totalPages"></span></span></div>`,
    });
    await page.close();
  }
  await browser.close();
  server.close();
  log(`paper: ${LANGS.length} PDFs`);
}

// ---------- third-party names outside the allowed list (warning only) ----------
{
  const BRANDS = /\b(GNOME|KDE|Apple|macOS|Mac OS|iPhone|Windows|Microsoft|Google|Android|Outlook|Little Snitch|Ubuntu|Fedora|Chrome OS|ChromeOS|Flatpak|Snap Store)\b/g;
  const found = new Map();
  for (const f of html) { const s = fs.readFileSync(f, 'utf8').replace(/<[^>]+>/g, ' '); for (const m of s.matchAll(BRANDS)) found.set(m[1], (found.get(m[1]) || new Set()).add(path.relative(DIST, f))); }
  for (const [b, fs1] of found) console.warn(`[web] third-party name "${b}" in ${[...fs1].slice(0, 4).join(', ')}${fs1.size > 4 ? ` and ${fs1.size - 4} more` : ''}`);
}

// ---------- internal link check (warns about links to pages that are not built) ----------
{
  const missing = new Map();
  for (const f of html) {
    const s = fs.readFileSync(f, 'utf8');
    for (const m of s.matchAll(/href="(\/[^"#?]*)/g)) {
      let p = m[1];
      const target = path.join(DIST, p.endsWith('/') ? p + 'index.html' : p);
      if (!fs.existsSync(target) && !fs.existsSync(path.join(DIST, p, 'index.html'))) missing.set(p, (missing.get(p) || new Set()).add(path.relative(DIST, f)));
    }
  }
  for (const [p, from] of missing) console.warn(`[web] link to a missing page ${p} from ${[...from].slice(0, 3).join(', ')}${from.size > 3 ? ` and ${from.size - 3} more` : ''}`);
}

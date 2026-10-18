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

const WEB = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(WEB, '..');
const DIST = path.join(WEB, 'dist');
const CACHE = path.join(WEB, '.cache');
const SITE = 'https://lordos.org';
const LANGS = ['en', 'es', 'pt'];
const PAGES = ['home', 'apps', 'gallery'];
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
  .replace(/\/\*[\s\S]*?\*\//g, '').replace(/\n\s*/g, '\n').replace(/\n+/g, '\n');
const cssUrl = hashed(Buffer.from(css), 'site', 'css');
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
const dict = Object.fromEntries(LANGS.map((l) => [l, JSON.parse(fs.readFileSync(path.join(WEB, 'i18n', l + '.json'), 'utf8'))]));
const prefix = (l) => (l === 'en' ? '/' : `/${l}/`);
const pageUrl = (l, page) => prefix(l) + (page === 'home' ? '' : page + '/');
const get = (obj, key) => key.split('.').reduce((o, k) => (o == null ? o : o[k]), obj);

function render(tpl, vars, t) {
  return tpl
    .replace(/\{\{\{([\w.]+)\}\}\}/g, (_, k) => { const v = get(vars, k); if (v == null) throw new Error('missing raw ' + k); return v; })
    .replace(/\{\{t\.([\w.]+)\}\}/g, (_, k) => { const v = get(t, k); if (v == null) throw new Error(`missing ${t.lang}.${k}`); return esc(v); });
}

function head(t, page) {
  const meta = {
    home: [t.meta.title, t.meta.description],
    gallery: [t.meta.galleryTitle, t.meta.galleryDescription],
    apps: [t.apps.metaTitle, t.apps.metaDescription],
  }[page];
  const [title, desc] = meta;
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
<link rel="stylesheet" href="${cssUrl}">
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
    `<a href="${h('#download')}">${esc(t.navDownload)}</a>`,
    `<a href="${h('#support')}">${esc(t.navSupport)}</a>`,
    `<a href="${h('#about')}">${esc(t.navAbout)}</a>`,
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

function footer(t) {
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

const tpl = { home: src('home.html'), gallery: src('gallery.html'), apps: src('apps.html'), notFound: src('404.html'), lightbox: src('lightbox.html') };

for (const l of LANGS) {
  const t = dict[l];
  const common = (page) => ({ img, js: jsUrl, header: header(t, page), footer: footer(t), head: head(t, page), lightbox: render(tpl.lightbox, {}, t) });

  // home
  const features = t.features.map(([title, text], i) => `<article class="feat">
<div class="feat-ico" aria-hidden="true"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="${ICONS[i]}"/></svg></div>
<h3>${esc(title)}</h3>
<p>${esc(text)}</p>
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
    heroImg: picture(r('docs/brand/screenshots/about-light.png'), t.altDesktop, { eager: true }),
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
write('robots.txt', `User-agent: *\nAllow: /\n\nSitemap: ${SITE}/sitemap.xml\n`);
const today = new Date().toISOString().slice(0, 10);
const urls = PAGES.filter((p) => p !== 'apps' || HAS_APPS).flatMap((p) => LANGS.map((l) => `  <url>
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
const html = [];
(function walk(d) { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) walk(p); else if (p.endsWith('.html')) html.push(p); } })(DIST);
for (const f of html) {
  const s = fs.readFileSync(f, 'utf8');
  if (/<script(?![^>]*\ssrc=)[^>]*>/i.test(s)) throw new Error('inline <script> in ' + f);
  if (/\son[a-z]+=/i.test(s)) throw new Error('inline event handler in ' + f);
  if (/https?:\/\/(fonts\.|cdn)/i.test(s)) throw new Error('external font/CDN in ' + f);
  if (/operating system|sistema operativo|sistema operacional|\bX11\b/i.test(s)) throw new Error('forbidden wording in ' + f);
  if (/Raspberry Pi (4|400|500)\b|\bCM4\b/i.test(s)) throw new Error('only Raspberry Pi 5 is supported: ' + f);
}
const jsSize = Buffer.byteLength(jsSrc) + Buffer.byteLength(themeSrc), cssSize = Buffer.byteLength(css);
if (jsSize > 15 * 1024) throw new Error(`JS is ${jsSize} bytes (limit 15 KB)`);
if (cssSize > 30 * 1024) throw new Error(`site.css is ${cssSize} bytes (limit 30 KB)`);
log(`built ${html.length} pages into web/dist (js ${jsSize} B, css ${cssSize} B, ${shots.length} gallery images, ${apps.length} apps); install.sh byte-identical`);

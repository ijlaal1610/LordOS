<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="brand/svg/logo-horizontal-dark.svg">
    <img src="brand/svg/logo-horizontal-light.svg" alt="LordOS" height="96">
  </picture>
</p>

# LordOS brand identity

This document describes the LordOS identity: the logo, its colours, its typefaces and how to use
them. The file index (every SVG, PNG and favicon, and how they are generated) is in
[`brand/README.md`](brand/README.md). All the files come from `tools/gen-brand.py`. Don't edit
the renders by hand.

## 1. What LordOS is

LordOS is a free, fast and secure desktop operating system built from scratch on Linux. It has its
own display server written in C and its own React-based interface engine, an idle session uses
about 120 MB of RAM, and it brings a Dock, an app launcher, a menu bar, instant search and polished
native apps to PCs and to the Raspberry Pi 5, while still running regular Linux applications. The
crown stands for a desktop where the user rules: fast, private and fully under their control.

## 2. The logo

The logo is a soft white crown with three peaks (the middle one taller). It has a violet band near
the base, a rounded rim and a faceted yellow gem on each tip. Next to it is the wordmark **Lord**OS.

### App icon

<img src="brand/svg/app-icon.svg" alt="App icon" width="128"> <img src="brand/png/app-icon-64.png" alt="App icon, 64 px" width="64"> <img src="brand/png/app-icon-32.png" alt="App icon, 32 px" width="32"> <img src="brand/png/app-icon-16.png" alt="App icon, 16 px" width="16">

The crown on a rounded square (22 % corner radius) with a diagonal violet gradient. It is the
system's own icon (About, Settings → General, the Dock, Monitor, Help). Outside the OS, use it for
store listings, avatars and anywhere a square mark is needed. File: `brand/svg/app-icon.svg`, PNGs
from 16 to 1024 px in `brand/png/`.

### Crown symbol

<img src="brand/svg/crown.svg" alt="Crown" width="128">

The crown alone on a transparent background. Use it when the name is already nearby: splash
screens, section headers, stickers. File: `brand/svg/crown.svg`, `brand/png/crown-{256,512}.png`.

### Horizontal logo

| On light | On dark |
|---|---|
| <img src="brand/svg/logo-horizontal-light.svg" alt="Horizontal logo, light" width="320"> | <img src="brand/svg/logo-horizontal-dark.svg" alt="Horizontal logo, dark" width="320"> |

The crown with the wordmark, on a transparent background. It's the main logo for websites, documents,
the README and presentations. Use the light version (navy text) on light backgrounds and the dark
version (white text) on dark ones. On the web, switch between them with `<picture>` and
`prefers-color-scheme`. Files: `brand/svg/logo-horizontal-{light,dark}.svg`,
`brand/png/logo-horizontal-{light,dark}-{400,800}.png` (1x and 2x).

### Monochrome

| Black | White |
|---|---|
| <img src="brand/svg/logo-mono-black.svg" alt="One-colour logo, black" width="320"> | <img src="brand/svg/logo-mono-white.svg" alt="One-colour logo, white" width="320"> |

A flat, single-colour version: the band and the gem outlines are cut out. Use it for print, engraving,
embroidery, small sizes and on photos or busy backgrounds where the full-colour crown would get
lost. Files: `brand/svg/logo-mono-{black,white}.svg`, `brand/svg/crown-mono.svg`.

### Small variants

<img src="brand/svg/app-icon-small.svg" alt="Small app icon" width="32"> <img src="brand/svg/app-icon-small.svg" alt="Small app icon, 16 px" width="16"> &nbsp; <img src="brand/svg/crown-mono-small.svg" alt="Small one-colour crown" width="32"> <img src="brand/svg/crown-mono-small.svg" alt="Small one-colour crown, 16 px" width="16">

The icon and the one-colour crown are redrawn on a pixel grid (32 px and 16 px), with fewer details,
so they stay sharp where the detailed versions would blur. Use `app-icon-small.svg` from 16 to 32 px
and `crown-mono-small.svg` for 12–20 px glyphs. In the OS, the menu bar logo is the small crown in
the menu text colour:

<img src="brand/screenshots/menubar-glyph-3x.png" alt="Menu bar glyph on light and dark" width="480">

### Favicon

<img src="brand/favicon/favicon.svg" alt="Favicon" width="32"> <img src="brand/favicon/apple-touch-icon-180.png" alt="Home screen icon" width="60">

`brand/favicon/favicon.svg` (the pixel-grid icon) for browser tabs, with `favicon-32.png` as a
fallback, `apple-touch-icon-180.png` for phone home screens and `favicon-512.png` for a web app
manifest. The HTML is in [`brand/README.md`](brand/README.md).

### Social card

<img src="brand/png/social-card-1200x630.png" alt="Social card" width="600">

1200 × 630: the dark logo on the icon's violet gradient. Use it for link previews (`og:image`) and
social posts. Files: `brand/svg/social-card.svg`, `brand/png/social-card-1200x630.png`.

### In the system

| About (light) | Login screen |
|---|---|
| <img src="brand/screenshots/about-light.png" alt="About LordOS, light" width="420"> | <img src="brand/screenshots/greeter.png" alt="Login screen" width="420"> |

## 3. Colours

| | Hex | Role |
|---|---|---|
| <img src="brand/swatches/violet-light.svg" width="64" alt=""> | `#B46CFF` | Violet gradient start (icon, social card: top left) |
| <img src="brand/swatches/indigo.svg" width="64" alt=""> | `#4F46E5` | Violet gradient end (bottom right) |
| <img src="brand/swatches/icon-gradient.svg" width="64" alt=""> | `#B46CFF → #4F46E5` | The brand gradient, at 135° |
| <img src="brand/swatches/band-dark.svg" width="64" alt=""> | `#5B3FE0` | Crown band, edges |
| <img src="brand/swatches/band-light.svg" width="64" alt=""> | `#7C5CFF` | Crown band, centre |
| <img src="brand/swatches/gem-yellow.svg" width="64" alt=""> | `#FFD60A` | Gem yellow (main facet) |
| <img src="brand/swatches/gem-highlight.svg" width="64" alt=""> | `#FFF1A8` | Gem highlight (lit facet) |
| <img src="brand/swatches/gem-amber.svg" width="64" alt=""> | `#FFC300` | Gem shade facet |
| <img src="brand/swatches/gem-orange.svg" width="64" alt=""> | `#FFB000` | Gem deep facet |
| <img src="brand/swatches/crown-lavender.svg" width="64" alt=""> | `#BDB1F5` | Crown shading (lavender) |
| <img src="brand/swatches/navy.svg" width="64" alt=""> | `#12122A` | Navy: the wordmark and text on light backgrounds |
| <img src="brand/swatches/white.svg" width="64" alt=""> | `#FFFFFF` | White: the crown body, the wordmark on dark backgrounds |

- **On light backgrounds:** the full-colour crown with the navy wordmark (`logo-horizontal-light`),
  or the black one-colour logo.
- **On dark backgrounds:** the full-colour crown with the white wordmark (`logo-horizontal-dark`),
  or the white one-colour logo.
- **On the brand gradient:** the dark logo (white text), as in the social card.
- In the OS the wordmark follows the theme: navy in light mode, white in dark mode.

## 4. Typography

- **Wordmark: Plus Jakarta Sans.** "Lord" in ExtraBold (800) and "OS" in Medium (500), letter
  spacing −0.01 em, no space in between. SIL Open Font License 1.1 (release 2.7.1,
  github.com/tokotype/PlusJakartaSans). The two weights ship in `assets/fonts/` with their license.
  In the OS the wordmark is live text in this face (`fontFamily: 'brand'`, the `LordOSWordmark`
  component). In the SVGs it is outlined from the same files, so they don't need the font
  installed.
- **Interface: Inter.** All UI text (Regular 400, Medium 500, SemiBold 600, Bold 700). SIL Open Font
  License 1.1. Code and the terminal use JetBrains Mono.

Plus Jakarta Sans is only for the name "LordOS" (and, if needed, short headlines next to it), never
for body text.

## 5. Usage

### Clear space

Keep free space around the logo at least as large as **half the wordmark's cap height**. Around the
app icon, keep 10 % of its size. No text, edges or other marks go inside it. In the lockup the crown is
1.6 times the cap height, centred on the capitals, 0.27 em from the text. Use the files as they are.

### Minimum sizes

| Version | Minimum |
|---|---|
| Horizontal logo | 96 px wide (24 mm in print); below 120 px prefer the one-colour version |
| App icon | 16 px (`app-icon-small.svg` up to 32 px) |
| Crown, full colour | 24 px |
| Crown, one colour | 12 px (`crown-mono-small.svg`) |
| Live wordmark in the UI | 13 px font size |

### Do

- Use the provided files and the generator. Pick light or dark by the background.
- Give the logo its clear space and keep the crown on the left of the name, also in right-to-left
  languages.
- Use the one-colour versions on photos, in print and at small sizes.
- Use the small variants at 32 px and below.

### Don't

- Don't stretch, skew, rotate or re-proportion the logo, or move the crown to another side.
- Don't recolour the crown, the band or the gems, or put the logo on clashing colours.
- Don't set "LordOS" in another font or weight, add a space ("Lord OS") or change the case
  ("LORDOS", "Lordos").
- Don't add outlines, drop shadows, glows, bevels or textures (the app icon's soft shadow is part
  of it).
- Don't use the light logo on dark backgrounds or the dark logo on light ones.
- Don't trace or rebuild the logo from screenshots or raster images. Change `tools/gen-brand.py`
  and regenerate.

## 6. Lumo, the guardian of LordOS

Lumo, the guardian of LordOS: a small celestial fox with white fur and violet highlights, who wears
a crown and a cape as symbols of protection, freedom and control.

**Anatomy:**

- big, pointed fox ears;
- abundant, fluffy white fur, like an arctic fox;
- a short, fluffy, slightly curved tail;
- a round, kitten-like face with big, expressive eyes;
- a small, compact fantasy-creature body.

<!-- Mascot images and the pose table go here (files in brand/mascot/). -->

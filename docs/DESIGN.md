# Design guide for LordOS apps

LordOS apps follow the macOS Human Interface Guidelines: generous whitespace, a small type scale,
consistent control heights and one accent-colored action per view. This guide is the reference
for every polish pass, so all apps share the same spacing, type and controls. The shared building
blocks are in `sdk/controls.jsx` and `sdk/components.jsx`, and the tokens below are exported from
the SDK as `spacing`, `typeScale` and `controlSize`.

The first pass (Calculator, Text Editor, Notes, Photos, PDF Viewer and Fonts) and the second one
(Terminal, Store, Monitor, Player, Screenshots, Camera, Voice Recorder, Scanner, Emoji & Symbols
with the system emoji picker, About and the Installer) are documented with before/after
screenshots in [`docs/polish/`](polish/), as is the third (Settings page by page, Files, Terminal
Preferences and the Store's remaining pages), and the fourth (the system apps: Disks, Backups, Logs,
Clock and Help).

## Spacing: an 8 px grid

| Token | px | Use |
|---|---|---|
| `xs` | 4 | tight spots: between a glyph and its label, between grouped toolbar buttons |
| `s` | 8 | between controls in a row, between rows of a list, toolbar gap |
| `m` | 12 | toolbar side insets, inside a grouped box, between a label and its control |
| `l` | 16 | padding of panels and dialogs that sit inside a window |
| `xl` | 20 | **window content inset** (nothing touches the window edge), dialog padding |
| `xxl` | 24 | between groups and sections |

Rules:

- The content of a window starts 20 px from its edges. Lists in a sidebar start 10 px in, with
  8–10 px of padding inside each row.
- Separate groups by 20–24 px and controls by 8–12 px. Never place two controls touching.
- Use hairlines (1 px, `separator`) between a sidebar and its content, under a toolbar and above a
  status bar. Don't use thick borders.

## Type scale

Inter, weights 400 / 500 / 600 only (700 only for very short emphasis).

| Token | px | Weight | Use |
|---|---|---|---|
| `caption` | 11 | 400–600 | secondary labels, section captions in sidebars, status bars, counts |
| `callout` | 12 | 400 | hints, secondary text in rows |
| `body` | 13 | 400–500 | default text, list rows, buttons, menus |
| `headline` | 15 | 600 | dialog titles, month headers, empty-state titles |
| `title` | 17 | 600 | the title of a pane when space is short |
| `largeTitle` | 22 | 600 | the title of a library or detail view (Photos, Fonts) |

Secondary text uses `textDim`. Avoid uppercase captions: macOS uses sentence case ("Contents",
not "CONTENTS").

## Controls

| Control | Height | Radius | Notes |
|---|---|---|---|
| `Button` | 28 (regular), 22 (`small`) | 7 / 6 | `primary` = the one accent action of the view; `plain` = borderless; `destructive` |
| `TextInput` | 28 (13 px text) | 7 | grows with `size` |
| `SearchField` | 28 | 7 | magnifier at the leading edge, clear button at the trailing edge |
| `Segmented` | 28 (22 `small`) | 8 | `appearance="neutral"` for view switchers (Read/Edit, Sample/Characters/Info); `fill` to share the width |
| `ToolbarButton` | 28 (22 `small`) | 6 | icon-only items **must** have a `title` (tooltip); `active` = toggled on |
| `ToolbarGroup` | 32 | 8 | buttons that belong together: zoom −/value/+, markup tools |
| `Toolbar` | 44 | — | 12 px side insets, 8 px gap, hairline at the bottom |
| `SidebarItem` | 28 | 6 | source list row; string `icon` is drawn in the accent color; optional `detail` |
| `MenuButton` | 28 | 8 | "⋯" or "⊕" button that opens a small menu (checked, disabled and destructive items) |
| `Toggle` | 24 (20 `small`) | — | `small` = the 36 × 20 switch of grouped forms (Settings rows) |
| `Hairline` | 1 | — | `edge="top" \| "bottom" \| "start" \| "end"`: a 1 px separator on that edge of its parent; start/end follow RTL |

Icons are monochrome Unicode glyphs from the fallback fonts (Inter, DejaVu Sans, Noto Sans
Symbols 1/2), not color emoji. Verified on the VM:
`✎ ⧉ ⊕ ⋯ ◫ ⛶ ⤢ ⟲ ⟳ ↻ ⎙ ⌕ ✕ ◐ ▬ ▤ ▦ ▶ ⓘ ☺ ‹ ›`. Color emoji (🗑 📎 🖼 🗨) and some
symbols (🗀) don't render as icons: draw a small shape instead (see `FolderGlyph` in Photos).
The media symbols (⏮ ⏸ ⏭ 🔁 🔊 💬 🎙 📷) are color emoji too: the Player draws its transport
glyphs (`PlayGlyph`, `PauseGlyph`, `SkipGlyph`, `SpeakerGlyph`, `CaptionsGlyph`), the Camera and
the Recorder draw their empty-state icons. `↻ ⤮ ♪ ◫ ▣ ☺ ≡` render monochrome.

## Layout patterns

- **Sidebar (source list):** 200–240 px, `sidebarBg`, a hairline on its trailing edge,
  `SectionTitle` captions (11 px, 600, `textDim`) and `SidebarItem` rows. A search field goes at
  the top of the sidebar when the list is what you search.
- **Toolbar:** icon-only buttons with tooltips where macOS does that. Show a text label only for
  the main mode switch or the back button. Push trailing items to the end with `ToolbarSpacer`. In
  narrow windows, hide items that are also in the app menus (print, invert, rotate), and move
  secondary actions into a `MenuButton` ("⋯"), so the toolbar never wraps to a second row.
- **Grouped form ("inset"):** `GroupBox` with `FormRow` children: a rounded box (radius 10) with
  hairlines between rows. Rows are at least 32–36 px. In a key/value list, put the labels in a
  right-aligned column (`labelWidth`) and the values leading-aligned.
- **Empty state:** `EmptyState` with an icon, a short title (15/600) and one hint line (12,
  `textDim`), centered. At most one primary button.
- **Dialogs and sheets:** 20 px padding, title 15/600, body 13 with `lineHeight: 1.4`, buttons at
  the end of the last row with 12 px above them. The default action is `primary`.
- **Hierarchy:** one primary (accent) action per view. Everything else is plain or neutral.
- **Inspector / detail pane:** a fixed-width pane (200–280 px, `sidebarBg`, `Hairline edge="start"`)
  shows an `EmptyState` until something is selected, never a blank column (Emoji & Symbols).
- **Media bar:** the Player's now-playing bar spans the whole window: cover and title at the
  start, the transport (buttons above a progress row) in a 380 px center column, the volume at
  the end. Under 720 px it keeps only the cover and the transport. Measure the box after layout
  (`lord.getLayout(ref.current.id)[2]`) to adapt columns and widths (Player, Recorder waveform).
- **Viewfinder apps (Camera):** always dark, whatever the theme; mode switch as two words with
  the current one in yellow, a 60 px shutter centered between two `flex: 1` columns.
- **Wizards (Installer):** step list in the sidebar (numbered circles, the current row
  highlighted), `largeTitle` step title, fields with 16 px between them, a footer with a hairline
  and Back / Next (`disabled` instead of a faded look).
- **Utility (Disks):** the actions for the selection are labelled `ToolbarButton`s over the detail
  pane (the ones that don't fit go into "⋯"), the details are a `GroupBox` of `FormRow`s with a
  right-aligned label column, and the selection title is `largeTitle`.
- **Console (Logs):** one 44 px toolbar (live, export, process field, `SearchField`), a 40 px scope bar
  with small neutral `Segmented` filters, column captions over the list, a detail pane and a status
  bar, each separated by a `Hairline`.
- **Settings plus browser (Backups):** a header with the app icon, status and the one primary action;
  a neutral `Segmented` centered in a 44 px bar; settings as `GroupBox`es in a 640 px centered
  column; the browser as a dated source list plus a file list with column captions.
- **Tabbed utility (Clock):** the tabs in the toolbar, centered, and the "add" action as a "+"
  toolbar button at the trailing edge, never a button floating at the bottom.
- **Reader (Help):** a centered reading column up to 680 px; the article keeps its own direction
  (`textDirection(body)`), so an English article in an Arabic interface still reads left to right.
- **Settings (System Settings):** a 220 px sidebar (search field, the user card, groups of
  `SidebarItem` rows 12 px apart with 20 px colored badges: a soft vertical gradient and a white
  glyph, or a drawn shape where the glyph is a color emoji), a 48 px title bar over the page with a
  hairline, and the page in a scroll view (20 px from the top, 24 px side insets, at most 680 px wide,
  centered). Pages are built only from `apps/settings/ui.jsx`: `Stack` (sections 20 px apart),
  `Section` (13/600 header, an inset `GroupBox`, an 11 px footer), `SettingRow` (28 px of content
  with 4 px above and below, so 36 px between hairlines; the control at the trailing edge),
  `ValueRow` (value right-aligned in `textDim`), `FieldRow` (a field of a fixed width at the trailing
  edge), `Actions` (buttons under a group, trailing), `Note`/`Message`. Buttons inside rows are
  `small` (22 px); switches are `<Toggle small>` (36 × 20). Pickers between a few pictures (light/dark,
  Dock side) are thumbnail tiles with a 2 px accent ring.
- **Preferences panes (Terminal):** sections as sidebar rows (not a long tab bar that cannot fit),
  a header with the section and profile names and the `Done` button, and a form with a 170 px
  right-aligned label column. Measure the pane: under 480 px the labels go above their controls.
  Lists of editable items (key mappings, triggers, shortcuts) are inset `GroupBox` rows.

## Robustness checklist

Check every pass on the VM with:

- **light and dark** mode;
- **RTL** (Arabic test pack `docs/packs/lordos-lang-ar.json`). The engine mirrors rows. Content
  that is itself left to right keeps `direction: 'ltr'` (calculator keypad, code, sliders, page
  geometry), and user text follows its own direction with `textDirection(text)` (a Spanish note in
  an Arabic UI still reads left to right);
- the **minimum window size** (`minSize` in the manifest) and a **large/maximized** window;
- no overlapping or clipped controls, and no toolbar that wraps. Text that can be long uses
  `whiteSpace: 'nowrap'` and `overflow: 'hidden'` inside a `flex: 1` box.

## Engine notes

- `left`/`right` and `margin*` on absolute boxes are physical, and `marginInlineStart`/`insetInline*`
  are not supported: use `Hairline` or `[direction() === 'rtl' ? 'right' : 'left']`.
- `flex: 0` means a zero flex basis, so a box with `flex: 0` and a `width` collapses to nothing.
  Pass `flex: 1` or `width`, not both (`style={[base, narrow ? { flex: 1 } : { width: 380 }]}`).
- `getLayout` exported by the SDK is the **keyboard** layout. A box's size after layout is
  `lord.getLayout(ref.current.id)` → `[x, y, w, h]` (Logs measured its list with the wrong one).
- A `flex: 1` box with wrapped word rows inside a row (a Markdown list item next to its bullet) can be
  measured shorter than it is drawn, and the next row overlaps it. Give it an explicit `width`
  (`<Markdown fitWidth width={exact} />`, as Help does).
- A toolbar must not wrap: measure its width and move the actions that don't fit into a `MenuButton`
  (Disks' toolbar, the Backups browser) or narrow its fields (Logs).

## Pending (engine)

- The text engine has no ellipsis (`…`) for clipped text: long names are cut at the edge.
- No per-side borders: hairlines are drawn as 1 px absolute `View`s.
- No vector icon set: icons are font glyphs (see above).

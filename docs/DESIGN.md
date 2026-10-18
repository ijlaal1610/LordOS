# Design guide for LordOS apps

LordOS apps follow the macOS Human Interface Guidelines: generous whitespace, a small type scale,
consistent control heights and one accent-colored action per view. This guide is the reference
for every polish pass, so all apps share the same spacing, type and controls. The shared building
blocks are in `sdk/controls.jsx` and `sdk/components.jsx`, and the tokens below are exported from
the SDK as `spacing`, `typeScale` and `controlSize`.

The first pass (Calculator, Text Editor, Notes, Photos, PDF Viewer and Fonts) is documented with
before/after screenshots in [`docs/polish/`](polish/).

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

Icons are monochrome Unicode glyphs from the fallback fonts (Inter, DejaVu Sans, Noto Sans
Symbols 1/2), not color emoji. Verified on the VM:
`✎ ⧉ ⊕ ⋯ ◫ ⛶ ⤢ ⟲ ⟳ ↻ ⎙ ⌕ ✕ ◐ ▬ ▤ ▦ ▶ ⓘ ☺ ‹ ›`. Color emoji (🗑 📎 🖼 🗨) and some
symbols (🗀) don't render as icons: draw a small shape instead (see `FolderGlyph` in Photos).

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

## Pending (engine)

- The text engine has no ellipsis (`…`) for clipped text: long names are cut at the edge.
- No per-side borders: hairlines are drawn as 1 px absolute `View`s.
- No vector icon set: icons are font glyphs (see above).

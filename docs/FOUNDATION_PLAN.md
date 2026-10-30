# Kantan LogoMaker foundation plan

Status: adopted plan, 27 October 2026; M0–M7 implemented, M8 partly (see IMPLEMENTATION_STATUS.md). Deviations are recorded in the D-L decision records. Based on the Kantan Paint Tool / Kantan NineSlice
baseline (C++20 Qt-free core, Qt 6.8.3 Widgets host, pinned dependencies, compact dark Fluent UI).

## Product direction

A small, fast Windows tool for making logos and app/game icons. Users combine **vector shapes**,
**styled text** and **imported images** on a single artboard, then export crisp PNG/ICO files at
several sizes in one step.

Principles:

- **Light.** Only `qtbase` from Qt (same as NineSlice) plus two small source-only libraries. No QtSvg,
  no QML, no GPU dependency. Target: executable under ~2 MB, cold start under 1 s, and a package about
  the size of NineSlice's (the Qt DLLs make up most of it).
- **Non-destructive by default.** Shape parameters, text, booleans, outlines and effects stay editable
  until the user chooses *Convert to path* or *Flatten*.
- **Resolution-independent.** Everything is re-rendered from vectors and original image pixels at each
  export size. Small sizes are never downscaled from a large export.
- **Same feel as the other Kantan tools.** Same tool rail, contextual top bar, docks, icon toggles,
  disclosure sections, status bar and shortcuts style.

## Primary workflow

1. File → New logo (default artboard 512 × 512 units, transparent background) or pick a starter template.
2. Drop in a preset shape (badge, shield, circle, star…), adjust its parameters, style it with fill,
   outlines and bevel.
3. Add text, pick a system or imported font, add outlines, shadows and bevel, and optionally bend it along an arc.
4. Combine shapes with Union / Subtract / Intersect / Exclude, and place images (mascots, textures) as needed.
5. Watch the live size strip (16 – 512 px) while editing.
6. Export → choose PNG and/or ICO, tick sizes (32, 64, 128, 256, 512, custom) → files written in one action.

## Workspace

```text
┌ File Edit Object Text Path Effects Arrange View Help ───────────────────────────────┐
│ [contextual tool options: e.g. sides 6 · corner 12 px · Apply/Cancel when pending]  │
├──┬─────────────────────────────────────────────────┬────────────────────────────────┤
│V │                                                 │ Objects (tree: groups, booleans│
│A │                                                 │  eye / lock / name / thumbnail)│
│P │              Artboard (zoom/pan,                │────────────────────────────────│
│R │              checkerboard, guides,              │ Inspector                      │
│E │              snapping, gizmos)                  │  ▸ Transform  X Y W H ∠        │
│T │                                                 │  ▸ Fill  solid/linear/radial   │
│I │                                                 │  ▸ Outlines  (+ up to 3)       │
│▣ │                                                 │  ▸ Effects  bevel/shadow/glow  │
│  │                                                 │  ▸ Text / Shape / Image params │
│  │                                                 │────────────────────────────────│
│  │                                                 │ Presets  [Shapes|Styles|Starter│
├──┴─────────────────────────────────────────────────┴────────────────────────────────┤
│ Size strip: 16 · 32 · 48 · 64 · 128 · 256 · 512  (live, bg cycle like NineSlice)    │
│ status: zoom · artboard size · selection · memory                                   │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

- **Tool rail** (Fluent icons, hover shows name + shortcut): Select **V**, Direct select / nodes **A**,
  Pen **P**, Shape **R** (sub-menu of parametric presets; the last used one is remembered), Ellipse **E**, Text **T**,
  Place image **I**, Eyedropper **Alt-hold / K**, Hand **Space**, Zoom **Z**.
- **Contextual top bar** shows only the active tool's options, like NineSlice's ToolOptionsStack.
- **Size strip** works like NineSlice's Resize preview: it is presentation only and never modifies or dirties the document.
- Reuse `CompactControls.h` (IconCheckBox, iconButton, disclosure), the rectangular slider thumbs and the dark palette.
  New Fluent icons needed: `text`, `pen`, `shape_union`, `shape_subtract`, `shape_intersect`,
  `shape_exclude`, `image`, `star`, `align_*`, `distribute_*`, `flip_*`, `font`, `export`.
  Same SVG → 1x/2x/3x pipeline (`scripts/render-icons.cjs`), and record the new icons in `dependencies.json`.

## Document model

One artboard per document: size in artboard units (default 512 × 512, any W × H allowed) and a background
(transparent or a colour, kept as presentation/export metadata like NineSlice's background).

Objects, each with a stable `ObjectId`, name, visibility, lock, opacity, blend mode
(Normal/Multiply/Screen/Overlay/Add, using the existing compositor equations) and a 2D affine transform
(position, scale, rotation, flip; skew deferred):

| Object | Owns | Editable as |
| --- | --- | --- |
| `ShapeObject` | Parametric preset *or* explicit path (cubic Béziers, multiple sub-paths, fill rule) | Parameters until *Convert to path*, then nodes |
| `TextObject` | String, font reference, size, weight/italic, tracking, line height, alignment, arc bend | Text until *Convert to path* |
| `ImageObject` | Reference to an embedded original image, crop rect, adjustments | Image params; never resampled destructively |
| `BooleanGroup` | Operation + ordered children (shapes/text/booleans) | Live; *Flatten* produces one path |
| `Group` | Ordered children | Normal group transform |
| `ClipGroup` | Clip shape + children (images or shapes masked by a vector) | Live |

Shared **Style** on shapes, text, booleans and images:

- **Fill:** none / solid / linear gradient / radial gradient (stops, angle; on-canvas gradient handles).
- **Outlines:** ordered list of up to 3. Each has width, colour (or gradient), position
  *outside / center / inside*, join *miter / round / bevel*, and miter limit. Stacked outlines give the
  common "sticker" logo look (e.g. a white 6 px outline, then a dark 3 px outline).
- **Effects** in a fixed, documented order: Drop shadow → Outer glow → Fill + outlines →
  Inner shadow → Inner glow → **Bevel & emboss** → Overlay texture (deferred).

Document state follows the inherited contracts: immutable snapshots, typed edit requests, one publication
path, and history as immutable before/after state pairs (D8). Vector documents are tiny, so whole-document
snapshots that share unchanged objects through `shared_ptr<const Object>` are enough. There are no tiles or
per-pixel history. Dragging gizmos and sliders uses a private Working state and becomes one undo step on
release; Escape cancels.

## Vector tools

**Selection & transform**: move, scale (Shift = uniform, Alt = from center), rotate (Shift = 15° steps),
flip H/V, numeric X/Y/W/H/angle, arrow-key nudges (1 / Shift 10 units). Reuses the NineSlice snapping
query (pixel grid, artboard center/edges, object bounds/centers, guides), with Alt to bypass.

**Pen & node editing**: click for corner points, drag for smooth Bézier handles, close on the first point;
direct-select to move nodes/handles, toggle corner/smooth, add/delete node, break/join paths, reverse direction.

**Booleans** (Path menu + inspector buttons + Ctrl+Alt shortcuts):

| Command | Result |
| --- | --- |
| Union | Merge all selected |
| Subtract | Bottom minus everything above it |
| Intersect | Area common to all |
| Exclude | Areas covered an odd number of times |
| Divide (later milestone) | Split into separate non-overlapping pieces |

By default these create a live `BooleanGroup`, so the children can still be moved and edited.
*Path → Flatten boolean* bakes the result into a single path.

**Path operations**: Offset path (grow/shrink with join type), Outline stroke (stroke → filled shape),
Simplify (point reduction with tolerance), Convert to path (shapes and text), Align (left/center/right/
top/middle/bottom to selection, key object or artboard), Distribute (equal gaps), Arrange (front/back),
Group/Ungroup, Duplicate, and **Radial repeat** (N rotated copies around a pivot, live, useful for
sunbursts, gears and flower marks; the vector counterpart of NineSlice's mirror axes).

### Geometry engine

- Paths are stored as cubic Béziers in doubles (`core/geometry`).
- Booleans and offsets use **Clipper2** (Boost Software License, source-only, small). Curves are flattened
  at a tolerance tied to the *largest* export size (default 0.05 units at 512), so boolean results stay
  smooth at every export size. After a live boolean, curve fitting turns the polygon back into Béziers
  only when the user *Flattens* (so node editing stays practical); live booleans keep the polygon
  internally.
- This decision is recorded as D-L1. Its revisit trigger is visible faceting at 512 px or users needing
  curve-exact boolean nodes.

## Shape presets

Parametric presets are code and editable after placement:

| Preset | Parameters |
| --- | --- |
| Rectangle | Per-corner radius (linked), corner style round/chamfer/inverted |
| Ellipse / circle | — |
| Polygon | Sides 3–64, corner radius |
| Star | Points 3–64, inner radius %, corner radius |
| Ring | Thickness, start/end angle (arc segments) |
| Gear | Teeth, tooth depth, hole radius |
| Shield, Badge (scalloped circle), Ribbon/Banner, Speech bubble, Arrow, Heart, Burst | 1–3 shape-specific params |

Static presets are SVG files in `app/assets/presets/` (symbols such as bolt, leaf, drop, crown, check,
paw, flame and laurel). They are parsed by **nanosvg** into editable paths, so adding a preset means
dropping in an SVG. Users can save any selection as a preset to their AppData presets folder.

**Style presets** store a Fill + Outlines + Effects bundle (Gold bevel, Chrome, Sticker, Neon glow,
Flat shadow, Embossed stone) and apply to any object with one click.

**Starter templates** are 6–10 complete `.kantanlogo` files (round badge + arc text, shield + initials,
app icon squircle, wordmark) opened as a new untitled document.

**SVG import**: File → Import SVG brings paths, fills and strokes in as editable shapes through the same
nanosvg route. Text is not supported (nanosvg limitation). Unsupported features are reported in the status bar.

## Text

- Font sources: installed system fonts plus **imported fonts** (File → Import font…, or drag-drop
  `.ttf`/`.otf`/`.ttc`). Imported fonts are registered with `QFontDatabase::addApplicationFontFromData`,
  remembered in an app-level font library (AppData), and **embedded in the project archive** so projects
  open on another machine. The font picker separates *Project fonts*, *Imported* and *System*.
- On open, a missing non-embedded font produces a visible warning and a substituted fallback. The project
  keeps the original font name so it re-links when the font becomes available.
- Licensing: embedding is on by default. The import dialog notes that the user is responsible for font
  licences; there is a per-font "do not embed" toggle.
- Shaping uses Qt (`QTextLayout` → `QGlyphRun` → `QRawFont::pathForGlyph`) in `kantan_io`. This gives
  kerning and ligatures through Qt's HarfBuzz at no extra dependency cost. The output is plain core
  `Path` geometry, cached by (text, font, size, tracking, line height, alignment). The core never sees Qt font types.
- Properties: size, weight/italic, tracking, line height, left/center/right alignment, multi-line,
  all-caps toggle, and **arc bend** (−100 % … +100 %; glyphs are laid out along a circular arc, which is the classic
  badge text). Arbitrary text-on-path and variable-font axes are deferred.
- Text takes the full Style: fill/gradient, up to 3 outlines (inside/center/outside), shadows, glow, bevel.
- *Convert to path* produces a ShapeObject per line (or one compound path) for node editing and booleans.
  Live text can also be a `BooleanGroup` child (e.g. letters knocked out of a badge).

## Images

- Import PNG, JPEG, BMP, GIF (first frame) and ICO through the `qtbase` image plugins already deployed.
  WebP/TIFF are deferred because they need the extra qtimageformats module.
- The original pixels are stored once in the archive, converted at import to premultiplied RGBA8 sRGB (the
  existing raster contract). All edits are parameters on the object, so nothing is resampled destructively:
  - transform (move/scale/rotate/flip), crop rectangle, opacity, blend mode;
  - adjustments: brightness, contrast, saturation, hue shift, colorize/tint;
  - **Remove background colour**: colour key with tolerance and edge softness (for logos delivered on white);
  - **Clip to shape**: put the image in a `ClipGroup` with a vector shape or text;
  - Style effects also apply to images (outline, shadow, glow and bevel all work from the image's alpha).
- Rendering resamples from the original at each export size. Downscaling uses a box-filtered mip chain and
  then bilinear sampling, so a 2000 px mascot stays clean at 32 px.

## Edge bevel and effects

Effects are raster operations computed from each object's coverage (alpha) **at the target render
resolution**, in the Qt-free core, on premultiplied RGBA8 buffers. All size parameters are in artboard
units and scaled to the output size, so a 512 px export and a 64 px export look the same proportionally.

- **Bevel & emboss**: styles *Inner bevel / Outer bevel / Emboss / Pillow*; parameters: size, depth,
  softness, profile (*linear / round / cove*), light angle and altitude, highlight colour + opacity +
  blend (Screen), shadow colour + opacity + blend (Multiply).
  Method: exact Euclidean distance transform of the coverage (Felzenszwalb–Huttenlocher, linear time) →
  height via the profile curve clamped to the bevel size → surface normal from the height gradient →
  Lambert lighting against the light vector → highlight/shadow layers composited onto the object only
  (inner) or around it (outer).
- **Drop shadow / Inner shadow**: offset (angle + distance), blur radius, spread, colour, opacity.
- **Outer / Inner glow**: size, spread, colour, opacity.
- Blur is a three-pass box blur (approximately Gaussian), bounded scratch buffers, with the inherited memory budget accounting.
- **Small-size behaviour**: below a configurable threshold (default 0.75 output px), an effect is skipped
  instead of producing a muddy fringe. Per-object **size visibility** (min/max export size) lets the
  user hide fine detail such as a tagline at 16/32 px, since icons often need simplified art at small sizes.

## Rendering

One renderer serves the canvas, size strip, object thumbnails, preset thumbnails and export, as the
inherited "one compositor" rule requires.

```text
Snapshot ─► per object: geometry (core, doubles, transformed)
          ─► flatten (tolerance from output scale)
          ─► coverage rasterizer (core; exact-area AA, non-zero/even-odd)
          ─► fill/gradient shading, outlines (Clipper2 offset → coverage)
          ─► effects (core raster ops)
          ─► composite (existing premultiplied source-over + blend equations)
```

- **Rasterizer: own small scanline coverage rasterizer in core** (signed-area accumulation, ~200 lines,
  the same technique as font-rs/stb_truetype). Chosen over QPainter so that rendering stays Qt-free,
  deterministic and testable headless with exact pixel fixtures, and so that canvas and export cannot
  differ. Recorded as D-L2. Fallback if it turns out to be insufficient: a QPainter adapter in `kantan_io`.
- The canvas renders at view resolution with per-object raster caches keyed by (object revision, scale).
  Interactive drags re-render only the changed object and re-composite. Effects with large blurs may
  render at reduced resolution during a drag and refine on release (presentation only).
- Overlays (selection, nodes, guides, gradient handles) are drawn by Qt in the canvas view, separate from
  artwork, and are never exported.

## Save and export

**Project**: `.kantanlogo`, a ZIP (Miniz, already pinned) containing a UTF-8 JSON manifest
(`kantanlogo-experimental`, schema 1), `fonts/` (embedded font files), `images/` (original PNG-encoded
pixels) and a `thumbnail.png`. The inherited IO contract applies: validation before construction
(duplicate IDs, cycles, missing members, bounded decompression, size limits), a sibling temp file with
safe replacement, and `WriteCoordinator` ordering.

**Export dialog** (File → Export… / Ctrl+Shift+E):

| Option | Behaviour |
| --- | --- |
| Format | PNG, ICO, or both in one action |
| Sizes | Checkboxes 16, 24, 32, 48, 64, 128, 256, 512, plus **Custom** list (W × H entries, add/remove) |
| Defaults | PNG: 32/64/128/256/512 ticked. ICO: 16/32/48/256 ticked (the sizes Windows actually picks) |
| ICO limits | ICO entries are capped at 256 × 256 by the format. 512 and non-square customs are disabled for ICO with a tooltip and stay available for PNG |
| Padding | % of the output size kept empty around the artwork |
| Fit | For non-square outputs: contain (centred) or cover |
| Background | Transparent (default) or a solid colour |
| Source | Whole artboard, or selection bounds |
| Naming | Pattern, default `{name}_{size}.png`; one `.ico` holding all ticked sizes |
| Preview | Thumbnail row of each output at 1:1 before writing |

- Every size is **rendered independently** from the vector snapshot, which keeps small sizes crisp, honours
  per-object size visibility, and lets effects scale correctly.
- PNG output: straight-alpha RGBA via the existing `io/Images` conversion.
- **ICO writer**: small custom writer in `kantan_io` (ICONDIR + entries with embedded PNG data, supported
  by Windows Vista and later). Deterministic and unit-tested; the bundled `qico` plugin is not relied on for writing.
- Export settings are saved in the project manifest and reused next time.
- Each file is written atomically through `WriteCoordinator`. A multi-file export reports per-file results;
  it is not a transaction across files (same honesty as the NineSlice PNG/JSON pair).

## Reuse and architecture

Same three production targets and dependency direction as NineSlice (`app → editor/io → core`), with the
same include-policy check (`cmake/CheckArchitecture.cmake`) and Qt-free core build preset.

```text
core/
  base/        Types, ids, checked math                       (copy from NineSlice)
  geometry/    Path, Bézier, flatten, transform, hit-test, bounds
  shapes/      parametric presets → Path
  boolean/     Clipper2 adapter: booleans, offset, outline stroke, simplify
  document/    objects, styles, snapshot, validation
  history/     state-pair history                             (copy, adapt)
  editing/     Editor, transactions, snapping, alignment      (adapt NineSlice Snapping/Alignment to doubles)
  raster/      Image buffer, coverage rasterizer
  effects/     distance transform, blur, bevel, shadow, glow, image adjustments, colour key
  composite/   blend equations                               (copy)
  render/      snapshot → image at scale (the one renderer)
io/
  ProjectIO (.kantanlogo), Images (copy), WriteCoordinator (copy),
  FontLibrary + TextShaper (Qt → core Path), SvgImport (nanosvg → core Path), IcoWriter, ExportJob
app/
  MainWindow, CanvasView (vector gizmos), ObjectPanel (from LayerPanel), Inspector,
  PresetPanel, SizeStrip (from NineSlicePreview concept), ExportDialog, ArtboardDialog
  (from CanvasSizeDialog), CompactControls/ToolIcons (copy), icons.qrc
```

Copied as-is or nearly as-is: build files, presets, `scripts/` (setup/package/format/run), `.clang-format`,
`dependencies.json` structure, CompactControls, ToolIcons, the icon pipeline, WriteCoordinator, Images,
Composite, History pattern, main.cpp theme setup, and the eyedropper overlay behaviour.

Not carried over: tile `Surface` storage, Brush/StrokeInput, Regions/Enclose, Masks, NineSlice. These are
raster-painting features that a vector logo tool does not need. Images use a plain immutable buffer.

New pinned dependencies (recorded in `dependencies.json` with URL, version/commit, SHA-256, licence, and
licence notices copied into the package by `package.ps1`):

| Dependency | Use | Licence | Footprint |
| --- | --- | --- | --- |
| Clipper2 (C++ source) | Booleans, offsets, outline stroke | BSL-1.0 | Few source files, compiled into core |
| nanosvg (single header) | SVG presets/import | zlib | One header, compiled into io |
| Miniz 3.1.0 | Project archive | MIT | Already used |
| Qt 6.8.3 `qtbase` | Host, fonts/shaping, image codecs | LGPL-3.0 dynamic | Already used |

## Delivery sequence and acceptance gates

Each milestone ends with the MAINTENANCE.md definition-of-done checks (tests, format, boundary check,
package smoke run) before the next begins.

| # | Milestone | Done when |
| --- | --- | --- |
| M0 | Scaffold | Clean clone → setup → desktop + core presets build; dark empty window with artboard zoom/pan; core-only tests and architecture check pass |
| M1 | Vector core | Rectangle/ellipse/polygon/star placed, selected, moved/scaled/rotated with gizmos and numeric fields; solid fill + one outline; object panel; undo/redo; save/open `.kantanlogo`; single PNG export through the core renderer |
| M2 | Export & preview | Export dialog with all sizes + custom, PNG batch + ICO writer, padding/background/fit, per-object size visibility, live size strip |
| M3 | Text & fonts | Text tool, system + imported fonts embedded in projects, tracking/line height/alignment, up to 3 outlines inside/center/outside, arc bend, convert to path |
| M4 | Booleans & paths | Live Union/Subtract/Intersect/Exclude groups + Flatten, pen tool, node editing, offset path, outline stroke, simplify, align/distribute, snapping and guides |
| M5 | Effects | Gradient fills, bevel & emboss (4 styles, 3 profiles), drop/inner shadow, outer/inner glow, style presets |
| M6 | Preset library | Remaining parametric presets, SVG presets + SVG import, save selection as preset, starter templates, radial repeat |
| M7 | Images | Import, crop, adjustments, colour-key background removal, clip to shape, effects from image alpha, mip-chain downscaling |
| M8 | Hardening | Measured render/drag performance, mixed-DPI check, clean-machine package run, user guide, crash-safe autosave |

M1–M2 give a usable shape-and-export tool early. M3 makes it a logo tool. Each later milestone adds
capability without reworking earlier contracts.

## Essential validation cases

- Geometry: parametric presets produce closed, correctly wound paths; transforms round-trip; hit-testing at rotated/flipped objects.
- Booleans: overlapping/touching/nested/disjoint inputs, holes, even-odd vs non-zero, empty results (an empty
  intersect yields an explicit empty path, not a crash or deleted object), live group update after a child edit, Flatten equals live render.
- Outlines: inside/center/outside widths measured on rendered fixtures; sharp corners respect miter limit.
- Rasterizer: exact-area coverage fixtures (half-covered pixels, sub-pixel slivers, self-intersections, both fill rules).
- Effects: bevel symmetry for a circle, zero-size no-op, scale invariance between 64 and 512 renders (downsampled comparison within tolerance), bounded memory on 4096 px custom export.
- Text: imported font embedded → reopens on a machine without it; missing system font warns and substitutes; convert-to-path matches live render.
- Images: colour-key tolerance, crop + rotation, clip-to-shape alpha, large source downscale without aliasing.
- Export: each ticked size produced at exact dimensions; ICO parses in Windows (Explorer/resource-compiler smoke) and in a re-read unit test; 512/non-square excluded from ICO; transparent corners preserved; failure leaves previous files intact.
- IO: malformed/newer manifests, oversized members, duplicate IDs, boolean-group cycles, missing font/image members.
- History: every edit undoable as one step; drags cancel with Escape; size strip / zoom never dirty the document.

## Deferred scope

SVG/PDF export, multiple artboards, skew/perspective, text on arbitrary paths, variable-font axes, texture
overlays, Divide boolean (until M4 stabilises), WebP/TIFF import, macOS/Linux packaging, raster painting
(use Kantan Paint Tool), and any engine integration.

## Decisions (recorded in docs/decisions)

- **D-L1 Geometry engine**: cubic Béziers + Clipper2 polygon booleans with export-tolerance flattening.
- **D-L2 Rasterizer**: Qt-free exact-area coverage rasterizer in core; one renderer for canvas and export.
- **D-L3 Fonts**: Qt shaping in io, embedded font files in project, core sees only Path geometry.
- **D-L4 Effects**: alpha-derived raster effects at target resolution, artboard-unit parameters.
- **D-L5 Code reuse**: copy-and-adapt from NineSlice (owner's decision, 27 October 2026).

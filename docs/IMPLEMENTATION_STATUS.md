# Kantan LogoMaker implementation status

Update, 30 October 2026: the Windows build, packaging, native startup, and all five contract suites now pass.
The interaction improvements and remaining professional-release checks are recorded in
[QUALITY_IMPLEMENTATION.md](QUALITY_IMPLEMENTATION.md). The original milestone account below is historical;
its statement that Windows validation has not run is superseded by this update.

27 October 2026. Experimental build covering milestones M0–M7 of the [foundation plan](FOUNDATION_PLAN.md) and part
of M8. Code was written and validated in a Linux build (GCC 13.3 and Clang 18 with Qt 6.4.2, headless). **The Windows
MSVC/Qt 6.8.3 build, packaging and hands-on checks have not been run yet**; see "Release checks" below.

## Delivered

**M0 Scaffold.** Same targets, presets, scripts, include-policy check and dark Fluent UI as NineSlice. Clipper2 and
nanosvg vendored with checksums; Miniz/Qt acquired by the inherited `setup.ps1`.

**M1 Vector core.** Parametric presets with editable parameters; select, move, scale (parametric size, Shift
uniform, Alt from centre), rotate (Shift 15°), flip, numeric X/Y/W/H/angle, nudges; solid fill and outlines; object
panel with thumbnails, eye/lock, rename and drag-to-reorder/regroup; undo/redo with one step per gesture; save/open
`.kantanlogo`; export through the core renderer.

**M2 Export & preview.** Export dialog: PNG and/or ICO, standard sizes, custom W × H list, padding, contain/cover,
transparent or solid background, whole artboard or selection bounds, naming pattern, 1:1 preview of every output.
Custom ICO writer (PNG entries, ≤ 256 px enforced). Per-object size visibility (hide below/above). Live size strip
with background cycling (16–64 px at true pixels since the usability round). Settings are remembered in the project.

**M3 Text & fonts.** Text tool, inspector text editing, system/imported/project font sections, embedded font
files, per-font embed/link choice, missing-font warning with fallback and re-linking, weight, italic, all caps,
tracking, line height, alignment, multi-line, arc bend, Convert to path.

**M4 Booleans & paths.** Live Union/Subtract/Intersect/Exclude groups (also via Ctrl+Alt+U/S/I/X), Flatten boolean,
pen tool (corners, smooth drags, close, open lines with round-capped strokes), node editing (drag nodes and handles,
smooth handle mirroring, Alt-click to add, Delete to remove, double-click to toggle corner/smooth), offset path,
outline stroke, simplify, reverse, break at node, close open paths, align/distribute to selection or artboard,
snapping (whole units, artboard, other objects, guides), draggable guides.

**M5 Effects.** Linear/radial gradients with up to three stops and on-canvas angle/centre/radius handles, drop and
inner shadow, outer and inner glow, bevel & emboss (4 styles, 3 profiles), six style presets, copy/paste style.

**M6 Presets.** All parametric presets from the plan, eight SVG symbols (original drawings), SVG import with
unsupported-feature report, save selection as preset (AppData/presets), starter templates (ten after the
usability round), radial repeat.

**M7 Images.** PNG/JPEG/BMP/GIF/ICO import and paste, crop, brightness/contrast/saturation/hue/tint, colour-key
background removal, clip to shape, outlines and effects from image alpha, mip-chain downscaling.

**M8 (partial).** Crash-safe autosave with recovery prompt, render benchmark (`kantan_bench`), user guide, clipboard
copy/paste of objects between documents (images included), drag-and-drop of projects, SVGs, fonts and images.

**Usability round (after M8).** Compact transform rows; true-pixel size strip (16–64 px, 1×/2×/3× zoom) and
readable thumbnails; colour fields with hex entry, quick swatches (palette, document, recent) and live picker
preview; named document palette with change-everywhere recolouring; scrubbable labels and mixed-value display;
inline canvas text editing; context menus; Alt-drag duplicate and repeat-spacing duplicate; searchable font box;
export presets (Windows/GameMaker, favicon, Android, iOS, Steam, itch.io, social) with named outputs in subfolders;
Export again; safe-zone overlays; Preview on… mockups; start screen; Open recent; four more templates; preset search.

## Validation (Linux build)

- `core_contracts`: affine/decomposition, flattening tolerance, every preset closed and inside its box, booleans and
  offsets by area, editor lifecycle (preview isolation, cancel, one-step apply, no-op gestures, saved identity, locks,
  group/ungroup preserving world transforms, boolean flatten and undo, duplicate, arrange), document validation
  (duplicate ids, cycles, leaf parents, missing images, outline limit), resize/flip/rotate/snap/align/distribute,
  path operations.
- `render_contracts`: rasterizer fixtures, outline positions and stacking measured on pixels, gradients, size
  visibility, effects (distance field, blur, zero size, bevel symmetry, scale invariance, 4096 px memory), live vs
  flattened booleans, clip groups, radial repeat, open strokes, image mip downscaling and colour key.
- `io_contracts`: full project round trip (every object field, fonts, image bytes and pixels, identical renders),
  atomic save, linked fonts, malformed archives (newer schema, duplicate ids, cycles, missing/unsafe/unreferenced
  members, missing font data, non-archives), ICO structure and Qt re-read, export sizes/naming/padding/background,
  failed export leaves the previous file intact, SVG import, text shaping, fragments.
- `ui_contracts` (Qt offscreen): draw, move with snapping, parametric scale handle, Escape cancel, undo, nudge,
  delete, inspector parameter edit as one undo step, object panel visibility, boolean + flatten through menus, text
  tool, pen + node drag, save/open/export with ICO, presets/styles/copy-paste, eyedropper, inline text editing,
  context menu, Alt-drag and repeat duplicate, label scrubbing, hex entry, palette recolour, export presets and
  Export again, safe-zone geometry, mockup rendering, every template valid, start screen choices, recent files,
  preset and font search.
- Core/IO additions: palette and recolour contracts; every export preset plans without clashes, the Android preset
  creates its subfolders, `../` in output names is rejected, palettes and named outputs round-trip.
- `architecture`: core include policy. Address/UB sanitizer run of all five suites (desktop build) passed. A Clang
  pass with `-Wshadow-all -Wconversion -Wshorten-64-to-32 -Wunreachable-code` is clean; code is formatted with
  clang-format 17.0.3.

Render benchmark (24 styled stars, validation container, Release): 16 px 1.7 ms, 128 px 3.8 ms, 512 px 16.5 ms,
1024 px 49 ms.

## Release checks still open

- First Windows build with MSVC 19.38 `/W4 /WX` and Qt 6.8.3, `ctest --preset desktop`, `format.ps1 -Check`,
  `package.ps1` and a clean-machine run (M8).
- ICO opened in Windows Explorer and a resource compiler smoke test (the unit test re-reads the file and Qt decodes
  it; Windows itself has not been checked).
- Mixed-DPI monitor check and measured canvas drag performance on a dense document (no per-object raster cache yet;
  see D-L2).
- Distribution licence and installer, as for the other Kantan tools.

## Known limitations

- Gradient stop offsets other than the middle stop are edited in the inspector, not on canvas.
- Join for open paths closes each open subpath; joining two separate paths into one is not implemented.
- Divide boolean, skew, text on arbitrary paths, variable-font axes, SVG/PDF export and multiple artboards are
  deferred as planned.
- Steam and other store sizes were checked in October 2026 and change occasionally; the preset table in
  `io/ExportPresets.cpp` is the one place to update.
- The phone mockup uses a superellipse close to, not identical with, Apple's mask.
- Starter templates are built in code rather than shipped as `.kantanlogo` files (same result, testable, no assets
  to keep in sync).

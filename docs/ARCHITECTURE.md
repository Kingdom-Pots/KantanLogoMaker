# Kantan LogoMaker engineering contracts

Status: adopted, 27 October 2026. Inherits the Kantan engineering contracts from Kantan NineSlice (dependency
direction, one edit lifecycle, immutable snapshots, typed outcomes, safe persistence) and adapts them to a vector
document. The [foundation plan](FOUNDATION_PLAN.md) defines product behaviour; [MAINTENANCE.md](MAINTENANCE.md)
defines change checks. Decisions D-L1 to D-L5 are recorded under [decisions/](decisions/).

## 1. Dependency direction

Three production targets: `kantan_core` (Qt-free), `kantan_io` (Qt Core/Gui, Miniz, nanosvg), `kantan_app` (Qt
Widgets). `app -> io -> core`, never back. `cmake/CheckArchitecture.cmake` enforces the core rules on every test run:
no Qt, OS, filesystem, io or app includes in core, and the module order below.

```text
editing  -> render, history, document, boolean, effects ...
render   -> document, effects, boolean, shapes, raster, composite, geometry
history  -> document
effects  -> document (styles), raster, composite
document -> shapes, composite, raster, geometry
boolean  -> geometry            (Clipper2 adapter; the only user of Clipper2)
shapes   -> geometry
composite-> raster
raster   -> geometry
geometry -> base
```

The core builds and tests with Qt discovery disabled (`core`, `core-asan`, `linux-core` presets).

## 2. Document and ownership

`DocumentData` is an immutable snapshot: artboard (size, background, guides), export settings, embedded image
originals, embedded fonts, and a depth-first vector of `shared_ptr<const Object>`. Every object has a stable
`ObjectId`; a container is immediately followed by its subtree (validated). Edits copy the snapshot and replace only
the changed object pointers, so history entries share everything unchanged (D8, inherited).

Objects: Shape (parametric preset or explicit Bézier path), Text, Image, Boolean, Group, Clip. Each has a transform
(position, rotation, signed scale), a Style (fill, up to three outlines, effects), opacity, blend mode, per-size
visibility and an optional radial repeat. Parametric shapes keep their parameters until Convert to path; a gizmo
resize changes their size, not their scale, so corner radii stay true.

Image originals are stored as their encoded file bytes plus the pixels decoded from those bytes, so saving never
re-encodes and reopening reproduces the exact pixels. Fonts are stored as file bytes when embedded, or by name when
linked.

The per-window `Editor` owns the committed snapshot, selection, history and the private Working state. Host-wide
services (memory budget, font library and shaper, image mip cache, write coordinator) are shared between windows
and hold no document state.

## 3. One edit lifecycle

Every mutation goes through `Editor`: typed commands (`add`, `remove`, `duplicate`, `update`, `group`, `ungroup`,
`arrange`, `move`, `replaceSubtrees`, `setArtboard`, `setExportSettings`, fonts) validate and publish once, push one
history entry, and bump the generation. Locked objects accept only lock/visibility/name changes.

Drags and slider scrubs use `begin -> preview* -> apply | cancel`. Previews replace objects in a private copy of the
base snapshot; save, export and undo read the committed snapshot only. Apply publishes one undo step; Escape and
focus loss cancel; a gesture that changed nothing adds no entry. Selection is UI state captured by history entries
but never creates one. The size strip, zoom and view settings never dirty the document.

## 4. Geometry, rendering and effects

- Paths are cubic Béziers in doubles; transforms are 2D affines. Flattening is adaptive with an explicit tolerance.
- Booleans, offsets, outline bands and strokes use Clipper2 in one adapter (D-L1).
- The renderer (D-L2) is the only path from document to pixels: canvas, size strip, thumbnails, presets, project
  thumbnail and export all call `render()`. Objects are flattened in output space, rasterized by the Qt-free
  signed-area coverage rasterizer, shaded (solid/gradient), given outlines and effects in a bounded layer, then
  composited with the inherited blend equations (premultiplied RGBA8 sRGB).
- Effects (D-L4) are computed from layer alpha at the target resolution with sizes in artboard units, so every
  export size is proportionally identical. Effects that would be smaller than 0.75 output px are skipped.
- Text is shaped by the host (D-L3) through the `TextShaper` interface; the core only sees paths.
- All raster allocations reserve against the shared `MemoryBudget`; a render that cannot reserve fails cleanly.

## 5. Persistence

`.kantanlogo` is a ZIP: `manifest.json` (`kantanlogo-experimental`, schema 1), `images/<key>.<ext>`,
`fonts/<key>.<ext>` and a presentation-only `thumbnail.png`. Loading validates the whole archive before a document
exists: member names, sizes and expansion against the budget, duplicate and unreferenced members, every field range,
object ids, hierarchy cycles, depth-first order, missing images/fonts and newer schemas (reported as such). Saving
writes a sibling temporary file and replaces atomically, ordered per destination by `WriteCoordinator`. Exports
write each file the same way; a multi-file export reports per-file results and is not a transaction across files.
Unsaved work is autosaved every minute to the recovery folder and offered on the next start after a crash.

## 6. Decision register

| Decision | Adopted baseline | Revisit trigger |
| --- | --- | --- |
| D1–D8 | Inherited from Kantan NineSlice where applicable (Windows/Qt host, per-document state with one publication path, premultiplied sRGB, CPU presenter, versioned archives, state-pair history) | As recorded there |
| D-L1 Geometry engine | Cubic Béziers; Clipper2 polygon booleans/offsets, recomputed per render in output space | Visible faceting or a boolean-heavy document missing frame budget |
| D-L2 Rasterizer | Qt-free signed-area coverage rasterizer; one renderer for every output | Coverage defects in fixtures, or a measured bottleneck |
| D-L3 Fonts | Qt shaping in io, embedded font files, core sees only paths | Need for variable axes or text-on-path |
| D-L4 Effects | Alpha-derived raster effects at target resolution, artboard-unit parameters | Effects needing vector-exact output (SVG/PDF export) |
| D-L5 Code reuse | Copy and adapt from NineSlice; no shared Kantan library yet | A third tool needing the same changes in lockstep |

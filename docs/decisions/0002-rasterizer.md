# D-L2: rasterizer and one renderer

Adopted 27 October 2026.

A Qt-free scanline coverage rasterizer (signed-area accumulation, the font-rs / stb_truetype technique) lives in
`core/raster`. Edges are split at the raster's left and right bounds and clamped, so geometry beyond the output keeps
correct winding; rows are processed in 64-row bands with bounded scratch. Coverage is exact for edges that do not
cross inside one pixel; where several edges share a pixel the accumulated winding is folded by the fill rule
(non-zero clamps, even-odd folds), which is the documented approximation.

`core/render` is the only path from document to pixels. Canvas, size strip, object and preset thumbnails, project
thumbnail and export all call `render()`, so they cannot disagree.

Alternative: QPainter in `kantan_io`. Rejected to keep rendering Qt-free, deterministic and testable headless with
exact fixtures. The canvas renders the visible view at device resolution on each change (no per-object caches yet);
this is fast enough for logo-sized documents and is a measured M8 item.

Validation: `render_contracts` (aligned squares, half pixels, slivers, triangle integral, orientation, both fill rules,
off-raster geometry, edge splitting, accounting released).

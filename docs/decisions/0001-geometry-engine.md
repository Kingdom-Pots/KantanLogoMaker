# D-L1: geometry engine

Adopted 27 October 2026.

Paths are cubic Béziers in doubles (`core/geometry`). Booleans, offsets, outline bands, open-line strokes and
simplification go through one Clipper2 1.5.4 adapter (`core/boolean`, precision 1e-4). Live booleans are not cached:
every render flattens the operands in output space at 0.1 px tolerance and recomputes the result, so the boolean is
exactly as smooth at 16 px as at 4096 px and there is no separate "export tolerance" to keep in sync. Flatten boolean,
offset path, outline stroke and simplify bake a result in the object's local space at 0.02 units and fit Bézier
segments back (corners stay corners; smooth runs become cubic segments), so node editing stays practical.

Alternative considered: caching boolean polygons at the largest export tolerance (the plan's first draft). Rejected
for now: per-render recomputation was measured fast enough (24 styled objects at 512 px in ~17 ms on the validation
machine) and removes cache invalidation from the edit path.

Consequence: very boolean-heavy documents pay Clipper2 time on every canvas frame. Revisit with a per-object cache
keyed by (object revision, output scale) if a release benchmark misses its budget, or if users need curve-exact
boolean nodes.

Validation: `core_contracts` (union/subtract/intersect/exclude areas, empty intersect, fill rules, offsets, bands),
`render_contracts` (live subtract area, flatten equals live render).

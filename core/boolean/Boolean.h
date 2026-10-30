#pragma once
#include "geometry/Geometry.h"

namespace kantan {
// Clipper2 adapter (D-L1). All inputs/outputs are flattened polygons in a common coordinate space.
struct Operand {
    Polygons polygons;
    FillRule rule{FillRule::NonZero};
};
// Union: all; Subtract: first minus all later; Intersect: common to all; Exclude: odd coverage count.
// Result uses the non-zero rule. An empty result is an explicit empty polygon set.
Polygons applyBoolean(BooleanOp op, const std::vector<Operand>& operands);
// Normalise a self-overlapping set to non-overlapping non-zero polygons.
Polygons normalized(const Operand& operand);
// Grow (delta > 0) or shrink (delta < 0) closed areas. `tolerance` bounds round-join arc error.
Polygons offset(const Polygons& area, double delta, JoinStyle join, double miterLimit, double tolerance);
// Area between offsets `inner` and `outer` (outer > inner; either may be negative).
Polygons band(const Polygons& area, double inner, double outer, JoinStyle join, double miterLimit,
              double tolerance);
// Stroke outline of open or closed polylines as a filled area.
Polygons strokeOutline(const Polygons& lines, bool closed, double width, JoinStyle join, CapStyle cap,
                       double miterLimit, double tolerance);
Polygons simplify(const Polygons& polygons, double tolerance);
} // namespace kantan

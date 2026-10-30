#pragma once
#include "geometry/Geometry.h"
#include "raster/Image.h"

namespace kantan {
// Exact-area scanline coverage rasterizer (signed-area accumulation, as in font-rs / stb_truetype).
// Polygons are closed implicitly and given in output pixel coordinates; pixel (x, y) covers
// [x, x + 1) x [y, y + 1). Coverage is exact for edges that do not cross inside one pixel; where edges
// of the same pixel overlap, the accumulated winding is folded by the fill rule.
Coverage rasterize(const Polygons& polygons, FillRule rule, Coord width, Coord height,
                   const std::shared_ptr<MemoryBudget>& budget);
} // namespace kantan

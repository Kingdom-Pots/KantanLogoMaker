#pragma once
#include "raster/Image.h"

namespace kantan {
enum class Blend { Normal, Multiply, Screen, Overlay, Add };
// The one blend implementation (copied from Kantan NineSlice). Premultiplied source over destination.
Pixel blendPixel(Pixel source, Pixel destination, Blend blend);
// Composite `source` (scaled by opacity) onto `destination` at integer offset.
void compositeImage(Image& destination, const Image& source, Coord offsetX, Coord offsetY, double opacity,
                    Blend blend);
} // namespace kantan

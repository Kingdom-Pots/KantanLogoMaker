#pragma once
#include "document/Style.h"
#include "raster/Image.h"
#include <vector>

namespace kantan {
// Alpha-derived raster effects at target resolution (D-L4). Sizes are in artboard units; `scale` converts
// to output pixels. The layer must already include room for outer effects (see effectExtent).
void applyEffects(Image& layer, const Effects& effects, double scale, double threshold,
                  const std::shared_ptr<MemoryBudget>& budget);
// How far, in output pixels, outer effects reach beyond the body.
double effectExtent(const Effects& effects, double scale);

// Building blocks, exposed for tests.
struct FloatPlane {
    Coord width{}, height{};
    std::shared_ptr<Reservation> memory;
    std::vector<float> values;
    static FloatPlane make(Coord width, Coord height, const std::shared_ptr<MemoryBudget>& budget);
    float at(Coord x, Coord y) const {
        return x >= 0 && y >= 0 && x < width && y < height ? values[index(x, y)] : 0.f;
    }
    std::size_t index(Coord x, Coord y) const {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    }
};
// Exact Euclidean distance (Felzenszwalb–Huttenlocher) from every pixel to the nearest pixel where
// `inside(x, y)` differs from that pixel's own state; returns distance in pixels (0 at the boundary).
FloatPlane distanceToEdge(const FloatPlane& alpha, bool measureInside,
                          const std::shared_ptr<MemoryBudget>& budget);
// Three-pass box blur approximating a Gaussian with the given standard deviation.
void blur(FloatPlane& plane, double sigma, const std::shared_ptr<MemoryBudget>& budget);
} // namespace kantan

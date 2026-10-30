#include "effects/Effects.h"
#include "composite/Composite.h"
#include <array>
#include <numbers>
#include <optional>

namespace kantan {
namespace {
constexpr float Infinite = 1e20f;
// 1D squared distance transform of sampled function f (Felzenszwalb & Huttenlocher 2012).
void edt1d(const float* f, float* d, int n, std::vector<int>& v, std::vector<float>& z) {
    int k = 0;
    v[0] = 0;
    z[0] = -Infinite;
    z[1] = Infinite;
    for (int q = 1; q < n; ++q) {
        float s;
        while (true) {
            const int p = v[static_cast<std::size_t>(k)];
            s = ((f[q] + static_cast<float>(q) * static_cast<float>(q)) -
                 (f[p] + static_cast<float>(p) * static_cast<float>(p))) /
                (2.f * static_cast<float>(q - p));
            if (s > z[static_cast<std::size_t>(k)] || k == 0)
                break;
            --k;
        }
        if (s <= z[static_cast<std::size_t>(k)]) {
            v[0] = q;
            z[0] = -Infinite;
            z[1] = Infinite;
            k = 0;
            continue;
        }
        ++k;
        v[static_cast<std::size_t>(k)] = q;
        z[static_cast<std::size_t>(k)] = s;
        z[static_cast<std::size_t>(k) + 1] = Infinite;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[static_cast<std::size_t>(k) + 1] < static_cast<float>(q))
            ++k;
        const int p = v[static_cast<std::size_t>(k)];
        d[q] = static_cast<float>(q - p) * static_cast<float>(q - p) + f[p];
    }
}
std::array<int, 3> boxSizes(double sigma) {
    // Box widths whose successive application approximates a Gaussian (Kovesi).
    const double ideal = std::sqrt(12 * sigma * sigma / 3 + 1);
    int wl = static_cast<int>(std::floor(ideal));
    if (wl % 2 == 0)
        --wl;
    const int wu = wl + 2;
    const double mIdeal = (12 * sigma * sigma - 3 * wl * wl - 12 * wl - 9) / (-4. * wl - 4);
    const int m = static_cast<int>(std::lround(mIdeal));
    std::array<int, 3> sizes{};
    for (int i = 0; i < 3; ++i)
        sizes[static_cast<std::size_t>(i)] = i < m ? wl : wu;
    return sizes;
}
void boxPass(const std::vector<float>& src, std::vector<float>& dst, Coord w, Coord h, int radius,
             bool horizontal) {
    const Coord lines = horizontal ? h : w, length = horizontal ? w : h;
    const std::size_t step = horizontal ? 1 : static_cast<std::size_t>(w);
    const float inv = 1.f / static_cast<float>(2 * radius + 1);
    for (Coord line = 0; line < lines; ++line) {
        const std::size_t base = horizontal ? static_cast<std::size_t>(line) * static_cast<std::size_t>(w)
                                            : static_cast<std::size_t>(line);
        const auto at = [&](Coord i) {
            // Clamp to edge: the layer margin is already transparent for outer effects, and inner effects
            // (blurring inverted alpha) must see "outside" continuing past the layer bounds.
            i = std::clamp<Coord>(i, 0, length - 1);
            return src[base + static_cast<std::size_t>(i) * step];
        };
        float acc = 0;
        for (Coord i = -radius; i <= radius; ++i)
            acc += at(i);
        for (Coord i = 0; i < length; ++i) {
            dst[base + static_cast<std::size_t>(i) * step] = acc * inv;
            acc += at(i + radius + 1) - at(i - radius);
        }
    }
}
FloatPlane alphaOf(const Image& image, const std::shared_ptr<MemoryBudget>& budget) {
    auto plane = FloatPlane::make(image.width, image.height, budget);
    for (std::size_t i = 0; i < image.pixels.size(); ++i)
        plane.values[i] = image.pixels[i].a / 255.f;
    return plane;
}
FloatPlane shiftedPlane(const FloatPlane& src, double dx, double dy, bool invert,
                        const std::shared_ptr<MemoryBudget>& budget) {
    auto out = FloatPlane::make(src.width, src.height, budget);
    const Coord ix = static_cast<Coord>(std::floor(dx)), iy = static_cast<Coord>(std::floor(dy));
    const float fx = static_cast<float>(dx - static_cast<double>(ix)),
                fy = static_cast<float>(dy - static_cast<double>(iy));
    const auto sample = [&](Coord x, Coord y) {
        const float v = src.at(x, y);
        return invert ? 1.f - v : v;
    };
    for (Coord y = 0; y < src.height; ++y)
        for (Coord x = 0; x < src.width; ++x) {
            const Coord sx = x - ix, sy = y - iy;
            // Bilinear sample at (x - dx, y - dy).
            const float a = sample(sx, sy), b = sample(sx - 1, sy), c = sample(sx, sy - 1),
                        d = sample(sx - 1, sy - 1);
            out.values[out.index(x, y)] = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
        }
    return out;
}
// Spread chokes the soft edge: values reach full strength sooner.
void applySpread(FloatPlane& plane, double spread) {
    if (spread <= 0)
        return;
    const float k = static_cast<float>(1 / std::max(1e-3, 1 - std::clamp(spread, 0., .999)));
    for (auto& v : plane.values)
        v = std::min(1.f, v * k);
}
Pixel tinted(Color c, float alpha) {
    return Pixel::rgba(c.r, c.g, c.b, static_cast<int>(std::lround(std::clamp(alpha, 0.f, 1.f) * 255.f)));
}
void drawBehind(Image& layer, const FloatPlane& strength, Color color, double opacity) {
    const float op = static_cast<float>(opacity) * (color.a / 255.f);
    for (std::size_t i = 0; i < layer.pixels.size(); ++i) {
        const float s = strength.values[i] * op;
        if (s <= 0)
            continue;
        layer.pixels[i] = over(layer.pixels[i], tinted(color, s));
    }
}
void drawAtop(Image& layer, const FloatPlane& strength, Color color, double opacity, Blend blend) {
    const float op = static_cast<float>(opacity) * (color.a / 255.f);
    for (std::size_t i = 0; i < layer.pixels.size(); ++i) {
        auto& d = layer.pixels[i];
        if (!d.a)
            continue;
        const float s = strength.values[i] * op * (d.a / 255.f);
        if (s <= 0)
            continue;
        const auto alpha = d.a;
        d = blendPixel(tinted(color, s), d, blend);
        d.a = alpha; // atop: effects never extend the body's coverage
        d.r = std::min(d.r, alpha);
        d.g = std::min(d.g, alpha);
        d.b = std::min(d.b, alpha);
    }
}
double profileHeight(BevelProfile profile, double t) {
    t = std::clamp(t, 0., 1.);
    switch (profile) {
    case BevelProfile::Linear:
        return t;
    case BevelProfile::Round:
        return std::sqrt(1 - (1 - t) * (1 - t));
    case BevelProfile::Cove:
        return 1 - std::sqrt(1 - t * t);
    }
    return t;
}
void applyBevel(Image& layer, const Bevel& b, double scale, const std::shared_ptr<MemoryBudget>& budget) {
    const double size = b.size * scale;
    auto alpha = alphaOf(layer, budget);
    const auto inner = distanceToEdge(alpha, true, budget);
    const auto outer = distanceToEdge(alpha, false, budget);
    auto height = FloatPlane::make(layer.width, layer.height, budget);
    const bool doInner = b.style != BevelStyle::Outer, doOuter = b.style != BevelStyle::Inner;
    const double innerSize = b.style == BevelStyle::Emboss || b.style == BevelStyle::Pillow ? size / 2 : size;
    const double outerSize = innerSize;
    // Height field: surroundings at 0. Inner: edge 0 rising to 1. Outer: surroundings rise to 1 at the edge.
    // Emboss: 0 -> 0.5 at the edge -> 1 inside. Pillow: surroundings dip to -0.5 at the edge, inside rises to
    // 0.5.
    for (std::size_t i = 0; i < height.values.size(); ++i) {
        const double tin = inner.values[i] / std::max(innerSize, 1e-6);
        const double tout = 1 - outer.values[i] / std::max(outerSize, 1e-6);
        double h = 0;
        const bool in = alpha.values[i] >= .5f;
        switch (b.style) {
        case BevelStyle::Inner:
            h = in ? profileHeight(b.profile, tin) : 0;
            break;
        case BevelStyle::Outer:
            h = in ? 1 : (tout > 0 ? profileHeight(b.profile, tout) : 0);
            break;
        case BevelStyle::Emboss:
            h = in ? .5 + .5 * profileHeight(b.profile, tin)
                   : (tout > 0 ? .5 * profileHeight(b.profile, tout) : 0);
            break;
        case BevelStyle::Pillow:
            h = in ? -.5 + profileHeight(b.profile, tin)
                   : (tout > 0 ? -.5 * profileHeight(b.profile, tout) : 0);
            break;
        }
        height.values[i] = static_cast<float>(h * size * b.depth);
    }
    // The distance field comes from thresholded coverage and is stair-stepped along diagonal edges; a
    // minimum ~1 px smoothing of the height keeps normals (and so the lit rim) free of stipple.
    blur(height, std::max(b.softness * scale / 2, 1.), budget);
    const double azimuth = b.lightAngle * std::numbers::pi / 180,
                 altitude = b.altitude * std::numbers::pi / 180;
    // Light comes from `lightAngle` (0 = from the right, 90 = from the top), as in common editors.
    const double lx = std::cos(azimuth) * std::cos(altitude), ly = -std::sin(azimuth) * std::cos(altitude),
                 lz = std::sin(altitude);
    auto highlight = FloatPlane::make(layer.width, layer.height, budget);
    auto shadow = FloatPlane::make(layer.width, layer.height, budget);
    for (Coord y = 0; y < layer.height; ++y)
        for (Coord x = 0; x < layer.width; ++x) {
            const auto i = height.index(x, y);
            const double gx =
                (height.at(std::min(x + 1, layer.width - 1), y) - height.at(std::max<Coord>(x - 1, 0), y)) /
                2;
            const double gy =
                (height.at(x, std::min(y + 1, layer.height - 1)) - height.at(x, std::max<Coord>(y - 1, 0))) /
                2;
            const double nlen = std::sqrt(gx * gx + gy * gy + 1);
            const double lambert = (-gx * lx - gy * ly + lz) / nlen;
            const double shade = lambert - lz; // flat surface is neutral
            const bool inside = alpha.values[i] >= .5f;
            const bool lit = inside ? doInner : (doOuter && outer.values[i] < outerSize);
            if (!lit)
                continue;
            if (shade > 0)
                highlight.values[i] = static_cast<float>(std::min(1., shade / std::max(1e-6, 1 - lz)));
            else
                shadow.values[i] = static_cast<float>(std::min(1., -shade / std::max(1e-6, lz)));
        }
    if (doOuter) {
        // Outer parts light the transparent surroundings: draw them as regular coloured layers behind.
        auto outerHighlight = highlight, outerShadow = shadow;
        for (std::size_t i = 0; i < alpha.values.size(); ++i)
            if (alpha.values[i] >= .5f)
                outerHighlight.values[i] = outerShadow.values[i] = 0;
        drawBehind(layer, outerShadow, b.shadow, b.shadowOpacity);
        drawBehind(layer, outerHighlight, b.highlight, b.highlightOpacity);
    }
    drawAtop(layer, shadow, b.shadow, b.shadowOpacity, Blend::Multiply);
    drawAtop(layer, highlight, b.highlight, b.highlightOpacity, Blend::Screen);
}
} // namespace
FloatPlane FloatPlane::make(Coord width, Coord height, const std::shared_ptr<MemoryBudget>& budget) {
    const auto count = checkedArea(width, height);
    FloatPlane plane{width, height, std::make_shared<Reservation>(budget, count * sizeof(float)), {}};
    plane.values.assign(count, 0.f);
    return plane;
}
FloatPlane distanceToEdge(const FloatPlane& alpha, bool measureInside,
                          const std::shared_ptr<MemoryBudget>& budget) {
    const Coord w = alpha.width, h = alpha.height;
    auto out = FloatPlane::make(w, h, budget);
    if (out.values.empty())
        return out;
    // Squared distance to the nearest pixel of the opposite state; `measured` pixels start at infinity.
    for (std::size_t i = 0; i < out.values.size(); ++i) {
        const bool in = alpha.values[i] >= .5f;
        out.values[i] = in == measureInside ? Infinite : 0.f;
    }
    const auto n = static_cast<std::size_t>(std::max(w, h));
    Reservation scratch(budget, n * (sizeof(float) * 3 + sizeof(int)) + 64);
    std::vector<float> f(n), d(n), z(n + 1);
    std::vector<int> v(n);
    for (Coord x = 0; x < w; ++x) {
        for (Coord y = 0; y < h; ++y)
            f[static_cast<std::size_t>(y)] = out.values[out.index(x, y)];
        edt1d(f.data(), d.data(), static_cast<int>(h), v, z);
        for (Coord y = 0; y < h; ++y)
            out.values[out.index(x, y)] = d[static_cast<std::size_t>(y)];
    }
    for (Coord y = 0; y < h; ++y) {
        float* row = out.values.data() + out.index(0, y);
        std::copy(row, row + w, f.begin());
        edt1d(f.data(), d.data(), static_cast<int>(w), v, z);
        std::copy(d.begin(), d.begin() + w, row);
    }
    for (std::size_t i = 0; i < out.values.size(); ++i) {
        const bool in = alpha.values[i] >= .5f;
        if (in != measureInside) {
            out.values[i] = 0;
            continue;
        }
        // Distance to the opposite pixel's centre minus half a pixel, pulled in by this pixel's own partial
        // coverage (an edge pixel with alpha 0.75 has its centre about 0.25 px inside the edge).
        const float a = alpha.values[i];
        const float own = measureInside ? 1.f - a : a;
        out.values[i] = std::max(0.f, std::sqrt(out.values[i]) - .5f - own);
    }
    return out;
}
void blur(FloatPlane& plane, double sigma, const std::shared_ptr<MemoryBudget>& budget) {
    if (sigma < .3 || plane.values.empty())
        return;
    sigma = std::min(sigma, 512.);
    Reservation scratch(budget, plane.values.size() * sizeof(float));
    std::vector<float> tmp(plane.values.size());
    for (const int size : boxSizes(sigma)) {
        const int radius = (size - 1) / 2;
        if (radius <= 0)
            continue;
        boxPass(plane.values, tmp, plane.width, plane.height, radius, true);
        boxPass(tmp, plane.values, plane.width, plane.height, radius, false);
    }
}
double effectExtent(const Effects& e, double scale) {
    double extent = 0;
    if (e.dropShadow.enabled)
        extent = std::max(extent, e.dropShadow.distance + e.dropShadow.blur * 1.5 + 2);
    if (e.outerGlow.enabled)
        extent = std::max(extent, e.outerGlow.size * 1.5 + 2);
    if (e.bevel.enabled && e.bevel.style != BevelStyle::Inner)
        extent = std::max(extent, e.bevel.size + e.bevel.softness + 2);
    return extent * scale;
}
void applyEffects(Image& layer, const Effects& e, double scale, double threshold,
                  const std::shared_ptr<MemoryBudget>& budget) {
    if (layer.empty())
        return;
    const auto big = [&](double size) { return size * scale >= threshold; };
    const bool any = (e.dropShadow.enabled && big(std::max(e.dropShadow.distance, e.dropShadow.blur))) ||
                     (e.outerGlow.enabled && big(e.outerGlow.size)) ||
                     (e.innerShadow.enabled && big(std::max(e.innerShadow.distance, e.innerShadow.blur))) ||
                     (e.innerGlow.enabled && big(e.innerGlow.size)) || (e.bevel.enabled && big(e.bevel.size));
    if (!any)
        return;
    const auto body = alphaOf(layer, budget);
    // Outer effects are generated from the untouched body, then drawn behind it.
    std::optional<FloatPlane> glow, drop;
    if (e.outerGlow.enabled && big(e.outerGlow.size)) {
        glow = body;
        blur(*glow, e.outerGlow.size * scale / 2, budget);
        applySpread(*glow, e.outerGlow.spread);
        for (std::size_t i = 0; i < glow->values.size(); ++i)
            glow->values[i] = std::min(1.f, glow->values[i] * 2.f);
    }
    if (e.dropShadow.enabled && big(std::max(e.dropShadow.distance, e.dropShadow.blur))) {
        const double r = e.dropShadow.angle * std::numbers::pi / 180;
        // Angle is the light direction (as in common editors): the shadow falls opposite to it.
        drop = shiftedPlane(body, -std::cos(r) * e.dropShadow.distance * scale,
                            std::sin(r) * e.dropShadow.distance * scale, false, budget);
        blur(*drop, e.dropShadow.blur * scale / 2, budget);
        applySpread(*drop, e.dropShadow.spread);
    }
    if (glow)
        drawBehind(layer, *glow, e.outerGlow.color, e.outerGlow.opacity);
    if (drop)
        drawBehind(layer, *drop, e.dropShadow.color, e.dropShadow.opacity);
    if (e.innerShadow.enabled && big(std::max(e.innerShadow.distance, e.innerShadow.blur))) {
        const double r = e.innerShadow.angle * std::numbers::pi / 180;
        auto shadow = shiftedPlane(body, -std::cos(r) * e.innerShadow.distance * scale,
                                   std::sin(r) * e.innerShadow.distance * scale, true, budget);
        blur(shadow, e.innerShadow.blur * scale / 2, budget);
        applySpread(shadow, e.innerShadow.spread);
        drawAtop(layer, shadow, e.innerShadow.color, e.innerShadow.opacity, Blend::Normal);
    }
    if (e.innerGlow.enabled && big(e.innerGlow.size)) {
        auto edge = body;
        for (auto& v : edge.values)
            v = 1.f - v;
        blur(edge, e.innerGlow.size * scale / 2, budget);
        applySpread(edge, e.innerGlow.spread);
        for (auto& v : edge.values)
            v = std::min(1.f, v * 2.f);
        drawAtop(layer, edge, e.innerGlow.color, e.innerGlow.opacity, Blend::Screen);
    }
    if (e.bevel.enabled && big(e.bevel.size))
        applyBevel(layer, e.bevel, scale, budget);
}
} // namespace kantan

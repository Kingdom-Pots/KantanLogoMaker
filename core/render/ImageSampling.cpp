#include "render/ImageSampling.h"
#include "effects/Effects.h"
#include "raster/Rasterizer.h"
#include "render/Render.h"
#include <numbers>

namespace kantan {
namespace {
Image downsample(const Image& src, const std::shared_ptr<MemoryBudget>& budget) {
    const Coord w = std::max<Coord>(1, (src.width + 1) / 2), h = std::max<Coord>(1, (src.height + 1) / 2);
    auto out = Image::make(w, h, budget);
    for (Coord y = 0; y < h; ++y)
        for (Coord x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0, a = 0, n = 0;
            for (Coord dy = 0; dy < 2; ++dy)
                for (Coord dx = 0; dx < 2; ++dx) {
                    const Coord sx = std::min(src.width - 1, x * 2 + dx),
                                sy = std::min(src.height - 1, y * 2 + dy);
                    const auto p = src.pixels[src.index(sx, sy)];
                    r += p.r;
                    g += p.g;
                    b += p.b;
                    a += p.a;
                    ++n;
                }
            out.ref(x, y) = {
                static_cast<std::uint8_t>((r + n / 2) / n), static_cast<std::uint8_t>((g + n / 2) / n),
                static_cast<std::uint8_t>((b + n / 2) / n), static_cast<std::uint8_t>((a + n / 2) / n)};
        }
    return out;
}
Pixel bilinear(const Image& img, double u, double v) {
    // Sample positions are pixel-centre based: (0.5, 0.5) is the centre of pixel (0, 0). Clamp to edge.
    const double x = std::clamp(u - .5, 0., static_cast<double>(img.width - 1));
    const double y = std::clamp(v - .5, 0., static_cast<double>(img.height - 1));
    const Coord x0 = static_cast<Coord>(std::floor(x)), y0 = static_cast<Coord>(std::floor(y));
    const Coord x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
    const double fx = x - static_cast<double>(x0), fy = y - static_cast<double>(y0);
    const auto p00 = img.pixels[img.index(x0, y0)], p10 = img.pixels[img.index(x1, y0)],
               p01 = img.pixels[img.index(x0, y1)], p11 = img.pixels[img.index(x1, y1)];
    const auto mix = [&](std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
        return static_cast<std::uint8_t>(
            std::lround((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy));
    };
    return {mix(p00.r, p10.r, p01.r, p11.r), mix(p00.g, p10.g, p01.g, p11.g), mix(p00.b, p10.b, p01.b, p11.b),
            mix(p00.a, p10.a, p01.a, p11.a)};
}
double luma(double r, double g, double b) {
    return .2126 * r + .7152 * g + .0722 * b;
}
bool identity(const ImageAdjust& a) {
    return a.brightness == 0 && a.contrast == 0 && a.saturation == 0 && a.hue == 0 && a.tintAmount == 0 &&
           !a.colorKey;
}
} // namespace
std::shared_ptr<const ImageSampler::Chain> ImageSampler::chain(const ImageRef& image) {
    std::lock_guard lock(mutex_);
    if (auto found = entries_.find(image.get()); found != entries_.end()) {
        found->second.used = ++clock_;
        return found->second.chain;
    }
    auto chain = std::make_shared<Chain>();
    chain->push_back(image);
    while (chain->back()->width > 1 || chain->back()->height > 1)
        chain->push_back(std::make_shared<const Image>(downsample(*chain->back(), budget_)));
    if (entries_.size() >= maxEntries_) {
        auto oldest = std::min_element(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
            return a.second.used < b.second.used;
        });
        entries_.erase(oldest);
    }
    entries_[image.get()] = {image, chain, ++clock_};
    return chain;
}
Pixel ImageSampler::sample(const Chain& chain, double u, double v, double footprint) {
    if (chain.empty())
        return {};
    const double level =
        std::clamp(std::log2(std::max(footprint, 1e-9)), 0., static_cast<double>(chain.size() - 1));
    const auto l0 = static_cast<std::size_t>(std::floor(level));
    const auto l1 = std::min(l0 + 1, chain.size() - 1);
    const double f = level - static_cast<double>(l0);
    const auto at = [&](std::size_t l) {
        const auto& img = *chain[l];
        const double sx = static_cast<double>(img.width) / static_cast<double>(chain[0]->width);
        const double sy = static_cast<double>(img.height) / static_cast<double>(chain[0]->height);
        return bilinear(img, u * sx, v * sy);
    };
    const auto a = at(l0);
    if (f <= 1e-6 || l1 == l0)
        return a;
    const auto b = at(l1);
    const auto mix = [f](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(x + (y - x) * f));
    };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}
Pixel adjustPixel(Pixel p, const ImageAdjust& adj) {
    if (!p.a || identity(adj))
        return p;
    const double alpha = p.a / 255.;
    double r = p.r / 255. / alpha, g = p.g / 255. / alpha, b = p.b / 255. / alpha;
    double outAlpha = alpha;
    if (adj.colorKey) {
        const double dr = r - adj.keyColor.r / 255., dg = g - adj.keyColor.g / 255.,
                     db = b - adj.keyColor.b / 255.;
        const double distance = std::sqrt(dr * dr + dg * dg + db * db) / std::sqrt(3.);
        const double lo = adj.keyTolerance, hi = adj.keyTolerance + std::max(1e-6, adj.keySoftness);
        const double t = std::clamp((distance - lo) / (hi - lo), 0., 1.);
        outAlpha *= t * t * (3 - 2 * t);
    }
    r += adj.brightness;
    g += adj.brightness;
    b += adj.brightness;
    const double contrast = 1 + adj.contrast;
    r = (r - .5) * contrast + .5;
    g = (g - .5) * contrast + .5;
    b = (b - .5) * contrast + .5;
    if (adj.hue != 0) {
        // Rotate around the grey axis.
        const double a = adj.hue * 2 * std::numbers::pi, c = std::cos(a), s = std::sin(a);
        const double k = (1 - c) / 3, q = std::sqrt(1. / 3) * s;
        const double nr = r * (c + k) + g * (k - q) + b * (k + q);
        const double ng = r * (k + q) + g * (c + k) + b * (k - q);
        const double nb = r * (k - q) + g * (k + q) + b * (c + k);
        r = nr;
        g = ng;
        b = nb;
    }
    const double y = luma(r, g, b);
    const double sat = 1 + adj.saturation;
    r = y + (r - y) * sat;
    g = y + (g - y) * sat;
    b = y + (b - y) * sat;
    if (adj.tintAmount > 0) {
        const double l = luma(r, g, b), t = std::clamp(adj.tintAmount, 0., 1.);
        r += (l * adj.tint.r / 255. - r) * t;
        g += (l * adj.tint.g / 255. - g) * t;
        b += (l * adj.tint.b / 255. - b) * t;
    }
    const auto byte = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0., 1.) * 255)); };
    return Pixel::rgba(byte(r), byte(g), byte(b), byte(outAlpha));
}
ImageLayer renderImageObject(const ImageRef& source, const ImageData& image, const Style& style,
                             const Affine& localToPixels, Rect clip, const RenderRequest& request,
                             ImageSampler& sampler, const std::shared_ptr<MemoryBudget>& budget) {
    if (!source || source->empty() || image.width <= 0 || image.height <= 0 || !localToPixels.invertible())
        return {};
    const Box local{-image.width / 2, -image.height / 2, image.width / 2, image.height / 2};
    const Box crop = image.crop.empty()
                         ? Box{0, 0, static_cast<double>(source->width), static_cast<double>(source->height)}
                         : image.crop;
    const double scale = std::sqrt(std::abs(localToPixels.determinant()));
    double outlineReach = 0;
    for (const auto& o : style.outlines)
        if (o.enabled && o.position != OutlinePosition::Inside)
            outlineReach += o.position == OutlinePosition::Center ? o.width / 2 : o.width;
    const double margin = (outlineReach + 2) * scale + effectExtent(style.effects, scale) + 2;
    const Polygon quad{localToPixels.map({local.x0, local.y0}), localToPixels.map({local.x1, local.y0}),
                       localToPixels.map({local.x1, local.y1}), localToPixels.map({local.x0, local.y1})};
    const Box box = inflated(bounds(Polygons{quad}), margin);
    Rect r{static_cast<Coord>(std::floor(box.x0)), static_cast<Coord>(std::floor(box.y0)), 0, 0};
    r.w = static_cast<Coord>(std::ceil(box.x1)) - r.x;
    r.h = static_cast<Coord>(std::ceil(box.y1)) - r.y;
    r = intersect(r, clip);
    if (r.empty())
        return {};
    ImageLayer layer{r, Image::make(r.w, r.h, budget)};
    Polygon shiftedQuad = quad;
    for (auto& p : shiftedQuad)
        p = p - Point{static_cast<double>(r.x), static_cast<double>(r.y)};
    const auto edge = rasterize({shiftedQuad}, FillRule::NonZero, r.w, r.h, budget);
    const auto chain = sampler.chain(source);
    const Affine toLocal = localToPixels.inverted();
    // Source pixels per output pixel along the more compressed axis.
    const double footprint = std::max(crop.width() / image.width, crop.height() / image.height) / scale;
    for (Coord y = 0; y < r.h; ++y)
        for (Coord x = 0; x < r.w; ++x) {
            const auto cov = edge.values[static_cast<std::size_t>(y * r.w + x)];
            if (!cov)
                continue;
            const Point l =
                toLocal.map({static_cast<double>(r.x + x) + .5, static_cast<double>(r.y + y) + .5});
            const double u = crop.x0 + (l.x - local.x0) / image.width * crop.width();
            const double v = crop.y0 + (l.y - local.y0) / image.height * crop.height();
            auto p = adjustPixel(ImageSampler::sample(*chain, u, v, footprint), image.adjust);
            if (cov != 255)
                p = scaled(p, cov / 255.);
            layer.pixels.ref(x, y) = p;
        }
    // Outlines from the image's alpha: outside rings behind, inside rings on top, by distance to the edge.
    bool anyOutline = false;
    for (const auto& o : style.outlines)
        anyOutline = anyOutline || (o.enabled && o.width > 0 && o.paint.kind != FillKind::None);
    if (anyOutline) {
        auto alpha = FloatPlane::make(r.w, r.h, budget);
        for (std::size_t i = 0; i < alpha.values.size(); ++i)
            alpha.values[i] = layer.pixels.pixels[i].a / 255.f;
        const auto outside = distanceToEdge(alpha, false, budget);
        const auto inside = distanceToEdge(alpha, true, budget);
        const Affine toLocal01 = Affine::scaling(1 / image.width, 1 / image.height) *
                                 Affine::translation(-local.x0, -local.y0) * toLocal;
        Image behind = Image::make(r.w, r.h, budget), above = Image::make(r.w, r.h, budget);
        const auto ring = [&](const Paint& paint, double from, double to, bool out, Image& target) {
            const auto& dist = out ? outside : inside;
            for (Coord y = 0; y < r.h; ++y)
                for (Coord x = 0; x < r.w; ++x) {
                    const auto i = dist.index(x, y);
                    const bool isIn = alpha.values[i] >= .5f;
                    if (isIn == out)
                        continue;
                    const double d = dist.values[i];
                    const double c = std::clamp(std::min(to - d + .5, d - from + .5), 0., 1.);
                    if (c <= 0)
                        continue;
                    auto p = scaled(shade(paint, toLocal01.map({static_cast<double>(r.x + x) + .5,
                                                                static_cast<double>(r.y + y) + .5})),
                                    c);
                    target.ref(x, y) = out ? over(target.ref(x, y), p) : over(p, target.ref(x, y));
                }
        };
        double outsideDepth = 0, insideDepth = 0;
        for (const auto& o : style.outlines) {
            if (!o.enabled || o.width <= 0 || o.paint.kind == FillKind::None)
                continue;
            const double w = o.width * scale;
            if (o.position == OutlinePosition::Outside) {
                ring(o.paint, outsideDepth - .5, outsideDepth + w, true, behind);
                outsideDepth += w;
            } else if (o.position == OutlinePosition::Inside) {
                ring(o.paint, insideDepth - .5, insideDepth + w, false, above);
                insideDepth += w;
            } else {
                ring(o.paint, -.5, w / 2, true, above);
                ring(o.paint, -.5, w / 2, false, above);
            }
        }
        for (std::size_t i = 0; i < layer.pixels.pixels.size(); ++i) {
            auto& p = layer.pixels.pixels[i];
            p = over(above.pixels[i], over(p, behind.pixels[i]));
        }
    }
    applyEffects(layer.pixels, style.effects, scale, request.effectThreshold, budget);
    return layer;
}
} // namespace kantan

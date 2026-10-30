#include "composite/Composite.h"

namespace kantan {
Pixel blendPixel(Pixel s, Pixel d, Blend blend) {
    if (blend == Blend::Normal)
        return over(s, d);
    const double sa = s.a / 255., da = d.a / 255.;
    const auto channel = [&](int sc, int dc) {
        const double cs = sa > 0 ? sc / (255. * sa) : 0, cd = da > 0 ? dc / (255. * da) : 0;
        double b{};
        switch (blend) {
        case Blend::Multiply:
            b = cs * cd;
            break;
        case Blend::Screen:
            b = cs + cd - cs * cd;
            break;
        case Blend::Overlay:
            b = cd <= .5 ? 2 * cs * cd : 1 - 2 * (1 - cs) * (1 - cd);
            break;
        case Blend::Add:
            b = std::min(1., cs + cd);
            break;
        default:
            b = cs;
            break;
        }
        return static_cast<std::uint8_t>(std::clamp(
            std::lround(255 * ((1 - sa) * dc / 255. + (1 - da) * sc / 255. + sa * da * b)), 0L, 255L));
    };
    return {channel(s.r, d.r), channel(s.g, d.g), channel(s.b, d.b), over(s, d).a};
}
void compositeImage(Image& dst, const Image& src, Coord ox, Coord oy, double opacity, Blend blend) {
    const Rect r = intersect({0, 0, dst.width, dst.height}, {ox, oy, src.width, src.height});
    if (r.empty() || opacity <= 0)
        return;
    const bool full = opacity >= 1;
    for (Coord y = r.y; y < r.y + r.h; ++y)
        for (Coord x = r.x; x < r.x + r.w; ++x) {
            auto s = src.pixels[src.index(x - ox, y - oy)];
            if (!s.a)
                continue;
            if (!full)
                s = scaled(s, opacity);
            auto& d = dst.ref(x, y);
            d = blendPixel(s, d, blend);
        }
}
} // namespace kantan

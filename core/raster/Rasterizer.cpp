#include "raster/Rasterizer.h"

namespace kantan {
namespace {
struct Line {
    Point a, b;
};
constexpr Coord Band = 64;
// Split at x = 0 and x = width, then clamp: winding is preserved for every pixel inside the raster.
void addClipped(std::vector<Line>& lines, Point a, Point b, double width) {
    if (a.y == b.y || !std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) ||
        !std::isfinite(b.y))
        return;
    double ts[4] = {0, 1, 0, 0};
    int count = 2;
    for (double edge : {0., width})
        if ((a.x - edge) * (b.x - edge) < 0)
            ts[count++] = (edge - a.x) / (b.x - a.x);
    // At most two split points: a tiny insertion sort (std::sort trips GCC's bounds analysis here).
    for (int i = 1; i < count; ++i)
        for (int j = i; j > 0 && ts[j] < ts[j - 1]; --j)
            std::swap(ts[j], ts[j - 1]);
    const auto at = [&](double t) { return Point{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; };
    for (int i = 0; i + 1 < count; ++i) {
        auto p = at(ts[i]), q = at(ts[i + 1]);
        p.x = std::clamp(p.x, 0., width);
        q.x = std::clamp(q.x, 0., width);
        if (p.y != q.y)
            lines.push_back({p, q});
    }
}
void accumulate(std::vector<float>& acc, std::size_t stride, Coord bandTop, Coord bandRows,
                const Line& line) {
    double dir = 1;
    Point p0 = line.a, p1 = line.b;
    if (p0.y > p1.y) {
        std::swap(p0, p1);
        dir = -1;
    }
    const double dxdy = (p1.x - p0.x) / (p1.y - p0.y);
    const Coord yStart = std::max(bandTop, static_cast<Coord>(std::floor(p0.y)));
    const Coord yEnd = std::min(bandTop + bandRows, static_cast<Coord>(std::ceil(p1.y)));
    for (Coord y = yStart; y < yEnd; ++y) {
        const double top = std::max(static_cast<double>(y), p0.y);
        const double bottom = std::min(static_cast<double>(y + 1), p1.y);
        if (bottom <= top)
            continue;
        const double x = p0.x + (top - p0.y) * dxdy, xnext = p0.x + (bottom - p0.y) * dxdy;
        const double d = (bottom - top) * dir;
        float* row = acc.data() + static_cast<std::size_t>(y - bandTop) * stride;
        const double x0 = std::min(x, xnext), x1 = std::max(x, xnext);
        const double x0floor = std::floor(x0), x1ceil = std::ceil(x1);
        const auto x0i = static_cast<std::size_t>(x0floor), x1i = static_cast<std::size_t>(x1ceil);
        if (x1i <= x0i + 1) {
            const double xmf = .5 * (x + xnext) - x0floor;
            row[x0i] += static_cast<float>(d - d * xmf);
            row[x0i + 1] += static_cast<float>(d * xmf);
        } else {
            const double s = 1 / (x1 - x0);
            const double x0f = x0 - x0floor;
            const double a0 = .5 * s * (1 - x0f) * (1 - x0f);
            const double x1f = x1 - x1ceil + 1;
            const double am = .5 * s * x1f * x1f;
            row[x0i] += static_cast<float>(d * a0);
            if (x1i == x0i + 2)
                row[x0i + 1] += static_cast<float>(d * (1 - a0 - am));
            else {
                const double a1 = s * (1.5 - x0f);
                row[x0i + 1] += static_cast<float>(d * (a1 - a0));
                for (std::size_t xi = x0i + 2; xi + 1 < x1i; ++xi)
                    row[xi] += static_cast<float>(d * s);
                const double a2 = a1 + static_cast<double>(x1i - x0i - 3) * s;
                row[x1i - 1] += static_cast<float>(d * (1 - a2 - am));
            }
            row[x1i] += static_cast<float>(d * am);
        }
    }
}
} // namespace
Coverage rasterize(const Polygons& polygons, FillRule rule, Coord width, Coord height,
                   const std::shared_ptr<MemoryBudget>& budget) {
    auto out = Coverage::make(width, height, budget);
    if (out.values.empty())
        return out;
    std::vector<Line> lines;
    for (const auto& poly : polygons)
        for (std::size_t i = 0, n = poly.size(); n >= 2 && i < n; ++i)
            addClipped(lines, poly[i], poly[(i + 1) % n], static_cast<double>(width));
    if (lines.empty())
        return out;
    const std::size_t stride = static_cast<std::size_t>(width) + 2;
    Reservation scratch(budget, stride * Band * sizeof(float));
    std::vector<float> acc(stride * Band);
    // Bucket lines by the first band they touch so each band only visits relevant lines.
    for (Coord top = 0; top < height; top += Band) {
        const Coord rows = std::min(Band, height - top);
        std::fill(acc.begin(), acc.end(), 0.f);
        bool any = false;
        for (const auto& line : lines) {
            const double lo = std::min(line.a.y, line.b.y), hi = std::max(line.a.y, line.b.y);
            if (hi <= static_cast<double>(top) || lo >= static_cast<double>(top + rows))
                continue;
            accumulate(acc, stride, top, rows, line);
            any = true;
        }
        if (!any)
            continue;
        for (Coord y = 0; y < rows; ++y) {
            const float* row = acc.data() + static_cast<std::size_t>(y) * stride;
            auto* dst =
                out.values.data() + static_cast<std::size_t>(top + y) * static_cast<std::size_t>(width);
            double sum = 0;
            for (Coord x = 0; x < width; ++x) {
                sum += row[x];
                double w = std::abs(sum);
                if (rule == FillRule::EvenOdd) {
                    w = std::fmod(w, 2.);
                    w = w > 1 ? 2 - w : w;
                } else
                    w = std::min(1., w);
                dst[x] = static_cast<std::uint8_t>(std::lround(w * 255));
            }
        }
    }
    return out;
}
} // namespace kantan

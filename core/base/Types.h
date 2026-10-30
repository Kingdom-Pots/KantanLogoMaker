#pragma once
#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace kantan {
using Coord = std::int64_t;
inline Coord checkedAdd(Coord a, Coord b) {
    if ((b > 0 && a > std::numeric_limits<Coord>::max() - b) ||
        (b < 0 && a < std::numeric_limits<Coord>::min() - b))
        throw std::length_error("Coordinate arithmetic overflow");
    return a + b;
}
inline Coord checkedSubtract(Coord a, Coord b) {
    if ((b > 0 && a < std::numeric_limits<Coord>::min() + b) ||
        (b < 0 && a > std::numeric_limits<Coord>::max() + b))
        throw std::length_error("Coordinate arithmetic overflow");
    return a - b;
}
// Stable object identity. Never an array index; zero is "no object".
struct ObjectId {
    std::uint64_t value{};
    auto operator<=>(const ObjectId&) const = default;
    explicit operator bool() const {
        return value != 0;
    }
};
struct Point {
    double x{}, y{};
    auto operator<=>(const Point&) const = default;
};
inline Point operator+(Point a, Point b) {
    return {a.x + b.x, a.y + b.y};
}
inline Point operator-(Point a, Point b) {
    return {a.x - b.x, a.y - b.y};
}
inline Point operator*(Point a, double s) {
    return {a.x * s, a.y * s};
}
inline double dot(Point a, Point b) {
    return a.x * b.x + a.y * b.y;
}
inline double cross(Point a, Point b) {
    return a.x * b.y - a.y * b.x;
}
inline double length(Point a) {
    return std::hypot(a.x, a.y);
}
inline bool finite(Point p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}
// Continuous, half-open box in artboard units. Empty when either extent is not positive.
struct Box {
    double x0{}, y0{}, x1{}, y1{};
    bool empty() const {
        return !(x1 > x0) || !(y1 > y0);
    }
    double width() const {
        return x1 - x0;
    }
    double height() const {
        return y1 - y0;
    }
    Point center() const {
        return {(x0 + x1) / 2, (y0 + y1) / 2};
    }
    auto operator<=>(const Box&) const = default;
};
inline Box emptyBox() {
    constexpr double inf = std::numeric_limits<double>::infinity();
    return {inf, inf, -inf, -inf};
}
inline Box include(Box b, Point p) {
    return {std::min(b.x0, p.x), std::min(b.y0, p.y), std::max(b.x1, p.x), std::max(b.y1, p.y)};
}
inline Box united(Box a, Box b) {
    if (!(a.x1 >= a.x0 && a.y1 >= a.y0))
        return b;
    if (!(b.x1 >= b.x0 && b.y1 >= b.y0))
        return a;
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}
// Degenerate boxes (a point or a line) still inflate; only a box with no points stays empty.
inline Box inflated(Box b, double d) {
    return !(b.x1 >= b.x0 && b.y1 >= b.y0) ? b : Box{b.x0 - d, b.y0 - d, b.x1 + d, b.y1 + d};
}
// Integer pixel rectangle, half-open: [x, x + w) x [y, y + h).
struct Rect {
    Coord x{}, y{}, w{}, h{};
    bool empty() const {
        return w <= 0 || h <= 0;
    }
    bool contains(Coord px, Coord py) const {
        return !empty() && px >= x && py >= y && px - x < w && py - y < h;
    }
    auto operator<=>(const Rect&) const = default;
};
inline Rect intersect(Rect a, Rect b) {
    const auto x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    const auto endx = std::min(checkedAdd(a.x, a.w), checkedAdd(b.x, b.w)),
               endy = std::min(checkedAdd(a.y, a.h), checkedAdd(b.y, b.h));
    if (endx <= x || endy <= y)
        return {x, y, 0, 0};
    return {x, y, checkedSubtract(endx, x), checkedSubtract(endy, y)};
}
inline constexpr Coord CoordinateLimit = 1LL << 24;
// Largest raster a single render/export may allocate (4096 x 4096 custom exports plus scratch).
inline constexpr std::size_t RasterPixelLimit = 32U * 1024U * 1024U;
inline std::size_t checkedArea(Coord w, Coord h, std::size_t limit = RasterPixelLimit) {
    if (w < 0 || h < 0 || w > CoordinateLimit || h > CoordinateLimit)
        throw std::length_error("Raster exceeds the processing budget");
    if (w == 0 || h == 0)
        return 0;
    if (static_cast<std::uint64_t>(w) > limit / static_cast<std::uint64_t>(h))
        throw std::length_error("Raster exceeds the processing budget");
    return static_cast<std::size_t>(w * h);
}
// Premultiplied RGBA8 in encoded sRGB.
struct Pixel {
    std::uint8_t r{}, g{}, b{}, a{};
    auto operator<=>(const Pixel&) const = default;
    static Pixel rgba(int r, int g, int b, int a = 255) {
        a = std::clamp(a, 0, 255);
        return {static_cast<std::uint8_t>((std::clamp(r, 0, 255) * a + 127) / 255),
                static_cast<std::uint8_t>((std::clamp(g, 0, 255) * a + 127) / 255),
                static_cast<std::uint8_t>((std::clamp(b, 0, 255) * a + 127) / 255),
                static_cast<std::uint8_t>(a)};
    }
};
inline Pixel scaled(Pixel p, double opacity) {
    const auto mul = [opacity](std::uint8_t v) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(v * opacity), 0L, 255L));
    };
    return {mul(p.r), mul(p.g), mul(p.b), mul(p.a)};
}
inline Pixel over(Pixel src, Pixel dst) {
    const auto c = [src](int s, int d) {
        return static_cast<std::uint8_t>(std::min(255, s + (d * (255 - src.a) + 127) / 255));
    };
    return {c(src.r, dst.r), c(src.g, dst.g), c(src.b, dst.b), c(src.a, dst.a)};
}
// Straight (non-premultiplied) document colour. Styles store these; rendering premultiplies once.
struct Color {
    std::uint8_t r{}, g{}, b{}, a{255};
    auto operator<=>(const Color&) const = default;
    Pixel premultiplied() const {
        return Pixel::rgba(r, g, b, a);
    }
};
enum class Status { Ok, NoChange, Cancelled, Stale, InvalidTarget, ResourceLimit, InvalidData, Unsupported };
struct Result {
    Status status{Status::Ok};
    std::string message;
    explicit operator bool() const {
        return status == Status::Ok || status == Status::NoChange;
    }
};
} // namespace kantan

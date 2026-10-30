#include "geometry/Geometry.h"
#include <numbers>

namespace kantan {
Affine Affine::inverted() const {
    const double det = determinant();
    if (!invertible())
        throw std::invalid_argument("Transform is not invertible");
    const double ia = d / det, ib = -b / det, ic = -c / det, id = a / det;
    return {ia, ib, ic, id, -(ia * e + ic * f), -(ib * e + id * f)};
}
double Affine::maxScale() const {
    // Largest singular value of the 2x2 part.
    const double s1 = a * a + b * b + c * c + d * d;
    const double det = determinant();
    const double disc = std::sqrt(std::max(0., s1 * s1 - 4 * det * det));
    return std::sqrt((s1 + disc) / 2);
}
Affine Affine::rotation(double degrees) {
    const double r = degrees * std::numbers::pi / 180;
    const double cs = std::cos(r), sn = std::sin(r);
    return {cs, sn, -sn, cs, 0, 0};
}
Affine operator*(const Affine& m, const Affine& n) {
    return {m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b,       m.a * n.c + m.c * n.d,
            m.b * n.c + m.d * n.d, m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f};
}
bool Path::empty() const {
    for (const auto& s : subpaths)
        if (!s.nodes.empty())
            return false;
    return true;
}
Path transformed(const Path& path, const Affine& m) {
    Path out = path;
    for (auto& s : out.subpaths)
        for (auto& n : s.nodes) {
            n.anchor = m.map(n.anchor);
            n.in = m.map(n.in);
            n.out = m.map(n.out);
        }
    // A mirroring transform keeps the same filled area; winding direction does not affect fill rules.
    return out;
}
Box bounds(const Path& path) {
    Box b = emptyBox();
    for (const auto& s : path.subpaths)
        for (const auto& n : s.nodes) {
            b = include(b, n.anchor);
            b = include(b, n.in);
            b = include(b, n.out);
        }
    return b;
}
Box bounds(const Polygons& polygons) {
    Box b = emptyBox();
    for (const auto& poly : polygons)
        for (auto p : poly)
            b = include(b, p);
    return b;
}
namespace {
void flattenCubic(Polygon& out, Point p0, Point p1, Point p2, Point p3, double tolerance, int depth) {
    // Flatness: max distance of control points from the chord bounds curve deviation.
    const double d1 = distanceToSegment(p1, p0, p3), d2 = distanceToSegment(p2, p0, p3);
    if (depth >= 18 || std::max(d1, d2) <= tolerance) {
        out.push_back(p3);
        return;
    }
    const Point p01 = (p0 + p1) * .5, p12 = (p1 + p2) * .5, p23 = (p2 + p3) * .5;
    const Point p012 = (p01 + p12) * .5, p123 = (p12 + p23) * .5, mid = (p012 + p123) * .5;
    flattenCubic(out, p0, p01, p012, mid, tolerance, depth + 1);
    flattenCubic(out, mid, p123, p23, p3, tolerance, depth + 1);
}
bool straight(const Node& a, const Node& b) {
    return a.out == a.anchor && b.in == b.anchor;
}
Polygon flattenSubPath(const SubPath& s, double tolerance) {
    Polygon out;
    if (s.nodes.empty())
        return out;
    out.push_back(s.nodes[0].anchor);
    const std::size_t count = s.nodes.size();
    const std::size_t segments = s.closed ? count : count - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        const auto& a = s.nodes[i];
        const auto& b = s.nodes[(i + 1) % count];
        if (straight(a, b))
            out.push_back(b.anchor);
        else
            flattenCubic(out, a.anchor, a.out, b.in, b.anchor, tolerance, 0);
    }
    if (s.closed && out.size() > 1 && out.back() == out.front())
        out.pop_back();
    // Remove consecutive duplicates; they add nothing and confuse later offsetting.
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
} // namespace
Polygons flatten(const Path& path, double tolerance) {
    tolerance = std::max(tolerance, 1e-4);
    Polygons out;
    for (const auto& s : path.subpaths) {
        auto poly = flattenSubPath(s, tolerance);
        if (!poly.empty())
            out.push_back(std::move(poly));
    }
    return out;
}
Polygons flattenClosed(const Path& path, double tolerance) {
    tolerance = std::max(tolerance, 1e-4);
    Polygons out;
    for (const auto& s : path.subpaths) {
        if (!s.closed || s.nodes.size() < 2)
            continue;
        auto poly = flattenSubPath(s, tolerance);
        if (poly.size() >= 3)
            out.push_back(std::move(poly));
    }
    return out;
}
double signedArea(const Polygon& poly) {
    double area = 0;
    for (std::size_t i = 0, n = poly.size(); i < n; ++i)
        area += cross(poly[i], poly[(i + 1) % n]);
    return area / 2;
}
int winding(const Polygons& polygons, Point p) {
    int w = 0;
    for (const auto& poly : polygons)
        for (std::size_t i = 0, n = poly.size(); i < n; ++i) {
            const Point a = poly[i], b = poly[(i + 1) % n];
            if (a.y <= p.y) {
                if (b.y > p.y && cross(b - a, p - a) > 0)
                    ++w;
            } else if (b.y <= p.y && cross(b - a, p - a) < 0)
                --w;
        }
    return w;
}
bool inside(const Polygons& polygons, FillRule rule, Point p) {
    const int w = winding(polygons, p);
    return rule == FillRule::NonZero ? w != 0 : (w & 1) != 0;
}
double distanceToSegment(Point p, Point a, Point b) {
    const Point ab = b - a;
    const double len2 = dot(ab, ab);
    if (len2 <= 0)
        return length(p - a);
    const double t = std::clamp(dot(p - a, ab) / len2, 0., 1.);
    return length(p - (a + ab * t));
}
double distanceToOutline(const Polygons& polygons, Point p, bool closed) {
    double best = std::numeric_limits<double>::infinity();
    for (const auto& poly : polygons) {
        if (poly.size() == 1)
            best = std::min(best, length(p - poly[0]));
        const std::size_t n = poly.size(), segments = closed ? n : (n ? n - 1 : 0);
        for (std::size_t i = 0; i < segments; ++i)
            best = std::min(best, distanceToSegment(p, poly[i], poly[(i + 1) % n]));
    }
    return best;
}
Path fromPolygons(const Polygons& polygons, FillRule rule) {
    Path path;
    path.rule = rule;
    for (const auto& poly : polygons) {
        if (poly.size() < 3)
            continue;
        SubPath s;
        for (auto p : poly)
            s.nodes.push_back(Node::corner(p));
        path.subpaths.push_back(std::move(s));
    }
    return path;
}
void reverse(SubPath& s) {
    std::reverse(s.nodes.begin(), s.nodes.end());
    for (auto& n : s.nodes)
        std::swap(n.in, n.out);
}
namespace {
Point normalized(Point v) {
    const double len = length(v);
    return len > 0 ? v * (1 / len) : Point{};
}
void appendNode(SubPath& out, Node node) {
    if (!out.nodes.empty() && length(out.nodes.back().anchor - node.anchor) < 1e-9) {
        out.nodes.back().out = node.out;
        if (node.type == NodeType::Smooth)
            out.nodes.back().type = NodeType::Smooth;
        return;
    }
    out.nodes.push_back(node);
}
} // namespace
void appendRoundedCorner(SubPath& out, Point prev, Point p, Point next, double radius) {
    const Point u = normalized(prev - p), v = normalized(next - p);
    const double cosine = std::clamp(dot(u, v), -1., 1.);
    const double angle = std::acos(cosine); // interior angle
    if (radius <= 0 || angle < 1e-6 || angle > std::numbers::pi - 1e-6) {
        appendNode(out, Node::corner(p));
        return;
    }
    const double half = angle / 2;
    double dist = radius / std::tan(half);
    dist = std::min({dist, length(prev - p) / 2, length(next - p) / 2});
    const double r = dist * std::tan(half);
    const double turn = std::numbers::pi - angle;
    const double handle = 4. / 3. * std::tan(turn / 4) * r;
    const Point t1 = p + u * dist, t2 = p + v * dist;
    appendNode(out, {t1, t1, t1 - u * handle, NodeType::Smooth});
    out.nodes.push_back({t2, t2 - v * handle, t2, NodeType::Smooth});
}
void appendArc(SubPath& out, Point c, double rx, double ry, double start, double sweep) {
    const int parts =
        std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (std::numbers::pi / 2) - 1e-9)));
    const double step = sweep / parts;
    const double k = 4. / 3. * std::tan(step / 4);
    const auto at = [&](double t) { return Point{c.x + rx * std::cos(t), c.y + ry * std::sin(t)}; };
    const auto tangent = [&](double t) { return Point{-rx * std::sin(t), ry * std::cos(t)}; };
    double t = start;
    Point p = at(t);
    appendNode(out, {p, p, p + tangent(t) * k, NodeType::Smooth});
    for (int i = 0; i < parts; ++i) {
        const double t2 = t + step;
        const Point q = at(t2);
        out.nodes.back().out = p + tangent(t) * k;
        out.nodes.push_back({q, q - tangent(t2) * k, q, NodeType::Smooth});
        t = t2;
        p = q;
    }
}
Path ellipsePath(Point c, double rx, double ry) {
    Path path;
    SubPath s;
    appendArc(s, c, rx, ry, 0, 2 * std::numbers::pi);
    // Close: merge the duplicated last node into the first.
    if (s.nodes.size() > 1 && length(s.nodes.back().anchor - s.nodes.front().anchor) < 1e-9) {
        s.nodes.front().in = s.nodes.back().in;
        s.nodes.pop_back();
    }
    path.subpaths.push_back(std::move(s));
    return path;
}
} // namespace kantan

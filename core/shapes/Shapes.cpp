#include "shapes/Shapes.h"
#include <numbers>

namespace kantan {
namespace {
constexpr double Pi = std::numbers::pi;
// Angle 0 is up; positive angles run clockwise on screen (y down).
Point polar(double radiusX, double radiusY, double radians) {
    return {radiusX * std::sin(radians), -radiusY * std::cos(radians)};
}
SubPath roundedPolygon(const std::vector<Point>& points, const std::vector<double>& radii) {
    SubPath s;
    const std::size_t n = points.size();
    for (std::size_t i = 0; i < n; ++i)
        appendRoundedCorner(s, points[(i + n - 1) % n], points[i], points[(i + 1) % n], radii[i]);
    // Rounded corners may produce a first-node duplicate of the last node; merge it.
    if (s.nodes.size() > 1 && length(s.nodes.back().anchor - s.nodes.front().anchor) < 1e-9) {
        s.nodes.front().in = s.nodes.back().in;
        s.nodes.pop_back();
    }
    return s;
}
Path single(SubPath s, FillRule rule = FillRule::NonZero) {
    Path p;
    p.rule = rule;
    p.subpaths.push_back(std::move(s));
    return p;
}
Path rectangle(const RectangleParams& r, double w, double h) {
    const double hw = w / 2, hh = h / 2, limit = std::min(hw, hh);
    const std::array<Point, 4> corners{Point{-hw, -hh}, Point{hw, -hh}, Point{hw, hh}, Point{-hw, hh}};
    SubPath s;
    for (std::size_t i = 0; i < 4; ++i) {
        const double radius = std::clamp(r.linked ? r.radii[0] : r.radii[i], 0., limit);
        const Point p = corners[i], prev = corners[(i + 3) % 4], next = corners[(i + 1) % 4];
        if (radius <= 0) {
            s.nodes.push_back(Node::corner(p));
            continue;
        }
        const Point u = (prev - p) * (1 / length(prev - p)), v = (next - p) * (1 / length(next - p));
        const Point a = p + u * radius, b = p + v * radius;
        if (r.corner == CornerStyle::Chamfer) {
            s.nodes.push_back(Node::corner(a));
            s.nodes.push_back(Node::corner(b));
        } else if (r.corner == CornerStyle::Round) {
            const double k = radius * (4. / 3.) * std::tan(Pi / 8);
            s.nodes.push_back({a, a, a - u * k, NodeType::Smooth});
            s.nodes.push_back({b, b - v * k, b, NodeType::Smooth});
        } else {
            // Inverted: a concave quarter circle centred on the corner itself.
            const double k = radius * (4. / 3.) * std::tan(Pi / 8);
            s.nodes.push_back({a, a, a + v * k, NodeType::Corner});
            s.nodes.push_back({b, b + u * k, b, NodeType::Corner});
        }
    }
    return single(std::move(s));
}
Path regular(int sides, double w, double h, double cornerRadius) {
    std::vector<Point> points;
    for (int i = 0; i < sides; ++i)
        points.push_back(polar(w / 2, h / 2, 2 * Pi * i / sides));
    return single(roundedPolygon(points, std::vector<double>(points.size(), std::max(0., cornerRadius))));
}
Path star(int count, double inner, double w, double h, double cornerRadius) {
    std::vector<Point> points;
    for (int i = 0; i < count * 2; ++i) {
        const double k = i % 2 ? inner : 1.;
        points.push_back(polar(w / 2 * k, h / 2 * k, Pi * i / count));
    }
    return single(roundedPolygon(points, std::vector<double>(points.size(), std::max(0., cornerRadius))));
}
Path ring(const RingParams& r, double w, double h) {
    const double rx = w / 2, ry = h / 2, k = 1 - std::clamp(r.thickness, 0.01, 1.);
    double sweep = r.endAngle - r.startAngle;
    if (std::abs(sweep) >= 360 - 1e-9 || sweep == 0) {
        Path p = ellipsePath({}, rx, ry);
        if (k > 0) {
            auto inner = ellipsePath({}, rx * k, ry * k).subpaths[0];
            reverse(inner);
            p.subpaths.push_back(std::move(inner));
        }
        return p;
    }
    sweep = std::clamp(sweep, -360., 360.);
    // appendArc uses the mathematical angle convention (0 = +x); convert from "0 = up, clockwise".
    const double start = (r.startAngle - 90) * Pi / 180, span = sweep * Pi / 180;
    SubPath s;
    appendArc(s, {}, rx, ry, start, span);
    if (k > 0) {
        SubPath inner;
        appendArc(inner, {}, rx * k, ry * k, start + span, -span);
        for (auto& n : inner.nodes)
            s.nodes.push_back(n);
    } else
        s.nodes.push_back(Node::corner({}));
    return single(std::move(s));
}
Path gear(const GearParams& g, double w, double h) {
    const int teeth = std::clamp(g.teeth, 3, 128);
    const double depth = std::clamp(g.toothDepth, 0.01, .9);
    std::vector<Point> points;
    for (int i = 0; i < teeth; ++i) {
        const double base = 2 * Pi * i / teeth, step = 2 * Pi / teeth;
        // Root, flank up, top, flank down: top land 35% and root land 35% of the pitch.
        for (auto [t, outer] : {std::pair{-.325, false}, std::pair{-.175, true}, std::pair{.175, true},
                                std::pair{.325, false}}) {
            const double k = outer ? 1. : 1 - depth;
            points.push_back(polar(w / 2 * k, h / 2 * k, base + t * step));
        }
    }
    Path p = single(roundedPolygon(points, std::vector<double>(points.size(), 0.)));
    if (g.holeRadius > 0) {
        auto hole = ellipsePath({}, w / 2 * std::min(g.holeRadius, 1 - depth - .02),
                                h / 2 * std::min(g.holeRadius, 1 - depth - .02))
                        .subpaths[0];
        reverse(hole);
        p.subpaths.push_back(std::move(hole));
    }
    return p;
}
Point cubicFromQuadratic(Point from, Point control) {
    return from + (control - from) * (2. / 3.);
}
Path simple(const SimpleParams& p, double w, double h) {
    const double hw = w / 2, hh = h / 2;
    const auto& v = p.values;
    switch (p.kind) {
    case SimpleKind::Shield: {
        const double side = -hh + std::clamp(v[0], .1, .9) * h, curve = std::clamp(v[1], 0., 1.),
                     dip = std::clamp(v[2], 0., .3) * h;
        SubPath s;
        s.nodes.push_back(Node::corner({-hw, -hh}));
        if (dip > 0)
            s.nodes.push_back(
                {{0, -hh + dip}, {-hw * .4, -hh + dip}, {hw * .4, -hh + dip}, NodeType::Smooth});
        s.nodes.push_back(Node::corner({hw, -hh}));
        const double drop = hh - side;
        s.nodes.push_back({{hw, side}, {hw, side}, {hw, side + drop * (.25 + .6 * curve)}, NodeType::Corner});
        s.nodes.push_back({{0, hh},
                           {hw * (.15 + .5 * curve), hh - drop * (.1 + .35 * curve)},
                           {-hw * (.15 + .5 * curve), hh - drop * (.1 + .35 * curve)},
                           NodeType::Corner});
        s.nodes.push_back(
            {{-hw, side}, {-hw, side + drop * (.25 + .6 * curve)}, {-hw, side}, NodeType::Corner});
        return single(std::move(s));
    }
    case SimpleKind::Badge: {
        const int count = std::clamp(static_cast<int>(std::lround(v[0])), 6, 64);
        const double depth = std::clamp(v[1], 0.01, .4);
        SubPath s;
        const double step = 2 * Pi / count;
        const double k = 4. / 3. * std::tan(step / 4);
        for (int i = 0; i < count; ++i) {
            // Valley (corner) then a circular-ish bump peaking mid-way.
            const double a = i * step, m = a + step / 2;
            const Point valley = polar(hw * (1 - depth), hh * (1 - depth), a);
            const Point peak = polar(hw, hh, m);
            const Point tangent = {hw * std::cos(m), hh * std::sin(m)};
            s.nodes.push_back(Node::corner(valley));
            s.nodes.push_back({peak, peak - tangent * k, peak + tangent * k, NodeType::Smooth});
        }
        return single(std::move(s));
    }
    case SimpleKind::Ribbon: {
        const double tail = std::clamp(v[0], 0., .45) * w, fork = std::clamp(v[1], 0., 1.) * tail,
                     bend = std::clamp(v[2], -.5, .5) * h;
        const Point tl{-hw, -hh}, tr{hw, -hh}, br{hw, hh}, bl{-hw, hh};
        const Point qt{0, -hh - bend * 2}, qb{0, hh - bend * 2};
        SubPath s;
        s.nodes.push_back({tl, tl, cubicFromQuadratic(tl, qt), NodeType::Corner});
        s.nodes.push_back({tr, cubicFromQuadratic(tr, qt), tr, NodeType::Corner});
        s.nodes.push_back(Node::corner({hw - fork, 0}));
        s.nodes.push_back({br, br, cubicFromQuadratic(br, qb), NodeType::Corner});
        s.nodes.push_back({bl, cubicFromQuadratic(bl, qb), bl, NodeType::Corner});
        s.nodes.push_back(Node::corner({-hw + fork, 0}));
        return single(std::move(s));
    }
    case SimpleKind::Bubble: {
        const double tailH = std::clamp(v[1], .05, .45) * h, bottom = hh - tailH;
        const double radius = std::clamp(v[0], 0., .5) * std::min(w, h - tailH);
        const double tx = std::clamp(v[2], -.4, .4) * w, base = std::max(4., w * .08);
        std::vector<Point> pts{{-hw, -hh},           {hw, -hh},           {hw, bottom}, {tx + base, bottom},
                               {tx - base * .6, hh}, {tx - base, bottom}, {-hw, bottom}};
        std::vector<double> radii{radius, radius, radius, 0, 0, 0, radius};
        return single(roundedPolygon(pts, radii));
    }
    case SimpleKind::Arrow: {
        const double head = std::clamp(v[0], .1, .9) * w, shaft = std::clamp(v[1], .05, 1.) * hh;
        const double round = std::clamp(v[2], 0., .5) * std::min(head, h);
        std::vector<Point> pts{{-hw, -shaft},   {hw - head, -shaft}, {hw - head, -hh}, {hw, 0},
                               {hw - head, hh}, {hw - head, shaft},  {-hw, shaft}};
        std::vector<double> radii{0, 0, round * .5, round, round * .5, 0, 0};
        return single(roundedPolygon(pts, radii));
    }
    case SimpleKind::Heart: {
        const double cleft = -1 + std::clamp(v[0], 0., 1.) * .6;
        const auto P = [&](double x, double y) { return Point{x * hw, y * hh}; };
        SubPath s;
        s.nodes.push_back({P(0, 1), P(.35, .75), P(-.35, .75), NodeType::Corner});
        s.nodes.push_back({P(-1, -.35), P(-1, .25), P(-1, -.75), NodeType::Smooth});
        s.nodes.push_back({P(-.5, -1), P(-.85, -1), P(-.2, -1), NodeType::Smooth});
        s.nodes.push_back({P(0, cleft), P(0, cleft - .15), P(0, cleft - .15), NodeType::Corner});
        s.nodes.push_back({P(.5, -1), P(.2, -1), P(.85, -1), NodeType::Smooth});
        s.nodes.push_back({P(1, -.35), P(1, -.75), P(1, .25), NodeType::Smooth});
        return single(std::move(s));
    }
    case SimpleKind::Burst:
        return star(std::clamp(static_cast<int>(std::lround(v[0])), 6, 96), 1 - std::clamp(v[1], .02, .6), w,
                    h, 0);
    }
    return {};
}
} // namespace
std::array<ParamRange, 3> simpleParamRanges(SimpleKind kind) {
    switch (kind) {
    case SimpleKind::Shield:
        return {{{"Side length", .1, .9, .45}, {"Curve", 0, 1, .6}, {"Top dip", 0, .3, 0}}};
    case SimpleKind::Badge:
        return {{{"Scallops", 6, 64, 16}, {"Depth", .01, .4, .08}, {nullptr, 0, 0, 0}}};
    case SimpleKind::Ribbon:
        return {{{"Tail length", 0, .45, .15}, {"Fork depth", 0, 1, .6}, {"Bend", -.5, .5, 0}}};
    case SimpleKind::Bubble:
        return {
            {{"Corner radius", 0, .5, .2}, {"Tail size", .05, .45, .22}, {"Tail position", -.4, .4, -.15}}};
    case SimpleKind::Arrow:
        return {{{"Head length", .1, .9, .4}, {"Shaft thickness", .05, 1, .45}, {"Roundness", 0, .5, 0}}};
    case SimpleKind::Heart:
        return {{{"Cleft depth", 0, 1, .5}, {nullptr, 0, 0, 0}, {nullptr, 0, 0, 0}}};
    case SimpleKind::Burst:
        return {{{"Points", 6, 96, 24}, {"Depth", .02, .6, .15}, {nullptr, 0, 0, 0}}};
    }
    return {};
}
const char* simpleKindName(SimpleKind kind) {
    static const char* names[] = {"Shield", "Badge", "Ribbon", "Speech bubble", "Arrow", "Heart", "Burst"};
    return names[static_cast<int>(kind)];
}
Path shapePath(const ShapeGeometry& geometry, double w, double h) {
    return std::visit(
        [&](const auto& g) -> Path {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, RectangleParams>)
                return rectangle(g, w, h);
            else if constexpr (std::is_same_v<T, EllipseParams>)
                return ellipsePath({}, w / 2, h / 2);
            else if constexpr (std::is_same_v<T, PolygonParams>)
                return regular(std::clamp(g.sides, 3, 64), w, h, g.cornerRadius);
            else if constexpr (std::is_same_v<T, StarParams>)
                return star(std::clamp(g.points, 3, 64), std::clamp(g.innerRatio, .01, 1.), w, h,
                            g.cornerRadius);
            else if constexpr (std::is_same_v<T, RingParams>)
                return ring(g, w, h);
            else if constexpr (std::is_same_v<T, GearParams>)
                return gear(g, w, h);
            else if constexpr (std::is_same_v<T, SimpleParams>)
                return simple(g, w, h);
            else
                return g;
        },
        geometry);
}
Result validateShape(const ShapeGeometry& geometry, double w, double h) {
    if (!std::isfinite(w) || !std::isfinite(h) || w < 0 || h < 0 || w > CoordinateLimit ||
        h > CoordinateLimit)
        return {Status::InvalidData, "Shape size is out of range"};
    return std::visit(
        [](const auto& g) -> Result {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, PolygonParams>) {
                if (g.sides < 3 || g.sides > 64 || !std::isfinite(g.cornerRadius) || g.cornerRadius < 0)
                    return {Status::InvalidData, "Polygon needs 3 to 64 sides"};
            } else if constexpr (std::is_same_v<T, StarParams>) {
                if (g.points < 3 || g.points > 64 || !(g.innerRatio > 0 && g.innerRatio <= 1) ||
                    !std::isfinite(g.cornerRadius) || g.cornerRadius < 0)
                    return {Status::InvalidData, "Star needs 3 to 64 points and an inner radius in (0, 1]"};
            } else if constexpr (std::is_same_v<T, RectangleParams>) {
                for (double r : g.radii)
                    if (!std::isfinite(r) || r < 0)
                        return {Status::InvalidData, "Corner radius must be zero or positive"};
            } else if constexpr (std::is_same_v<T, RingParams>) {
                if (!(g.thickness > 0 && g.thickness <= 1) || !std::isfinite(g.startAngle) ||
                    !std::isfinite(g.endAngle))
                    return {Status::InvalidData, "Ring thickness must be in (0, 1]"};
            } else if constexpr (std::is_same_v<T, GearParams>) {
                if (g.teeth < 3 || g.teeth > 128 || !(g.toothDepth > 0 && g.toothDepth < 1) ||
                    !(g.holeRadius >= 0 && g.holeRadius < 1))
                    return {Status::InvalidData, "Gear parameters are out of range"};
            } else if constexpr (std::is_same_v<T, SimpleParams>) {
                for (double v : g.values)
                    if (!std::isfinite(v))
                        return {Status::InvalidData, "Shape parameter is not a number"};
            } else if constexpr (std::is_same_v<T, Path>) {
                for (const auto& s : g.subpaths)
                    for (const auto& n : s.nodes)
                        if (!finite(n.anchor) || !finite(n.in) || !finite(n.out) ||
                            std::abs(n.anchor.x) > CoordinateLimit || std::abs(n.anchor.y) > CoordinateLimit)
                            return {Status::InvalidData, "Path coordinates are out of range"};
            }
            return {};
        },
        geometry);
}
} // namespace kantan

#pragma once
#include "base/Types.h"
#include <vector>

namespace kantan {
// 2D affine map: x' = a x + c y + e, y' = b x + d y + f.
struct Affine {
    double a{1}, b{0}, c{0}, d{1}, e{0}, f{0};
    Point map(Point p) const {
        return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
    }
    Point mapVector(Point v) const {
        return {a * v.x + c * v.y, b * v.x + d * v.y};
    }
    double determinant() const {
        return a * d - b * c;
    }
    bool invertible() const {
        return std::isfinite(determinant()) && std::abs(determinant()) > 1e-12;
    }
    Affine inverted() const;
    // Largest scale factor applied to any direction; used to pick flattening tolerance.
    double maxScale() const;
    static Affine translation(double x, double y) {
        return {1, 0, 0, 1, x, y};
    }
    static Affine scaling(double sx, double sy) {
        return {sx, 0, 0, sy, 0, 0};
    }
    static Affine rotation(double degrees);
    auto operator<=>(const Affine&) const = default;
};
// Apply `second` after `first`.
Affine operator*(const Affine& second, const Affine& first);

enum class FillRule { NonZero, EvenOdd };
enum class JoinStyle { Miter, Round, Bevel };
enum class CapStyle { Butt, Round, Square };
enum class BooleanOp { Union, Subtract, Intersect, Exclude };
enum class NodeType { Corner, Smooth };
// A path anchor with absolute Bézier handles. A handle equal to its anchor means a straight side.
struct Node {
    Point anchor, in, out;
    NodeType type{NodeType::Corner};
    auto operator<=>(const Node&) const = default;
    static Node corner(Point p) {
        return {p, p, p, NodeType::Corner};
    }
};
struct SubPath {
    std::vector<Node> nodes;
    bool closed{true};
    auto operator<=>(const SubPath&) const = default;
};
struct Path {
    std::vector<SubPath> subpaths;
    FillRule rule{FillRule::NonZero};
    bool empty() const;
    auto operator<=>(const Path&) const = default;
};
using Polygon = std::vector<Point>;
using Polygons = std::vector<Polygon>;

Path transformed(const Path& path, const Affine& m);
// Exact control-point hull bounds (a safe superset of the curve).
Box bounds(const Path& path);
Box bounds(const Polygons& polygons);
// Adaptive subdivision; `tolerance` is the maximum distance from the true curve, in path units.
Polygons flatten(const Path& path, double tolerance);
// Open subpaths are flattened as polylines but excluded from fills by callers.
Polygons flattenClosed(const Path& path, double tolerance);
double signedArea(const Polygon& polygon);
int winding(const Polygons& polygons, Point p);
bool inside(const Polygons& polygons, FillRule rule, Point p);
double distanceToSegment(Point p, Point a, Point b);
double distanceToOutline(const Polygons& polygons, Point p, bool closed = true);
// Build a path from polygons (straight-sided closed subpaths).
Path fromPolygons(const Polygons& polygons, FillRule rule = FillRule::NonZero);
void reverse(SubPath& subpath);
// Rounded-corner helpers: append a corner at `p` between `prev` and `next` with radius r (arc as cubics).
void appendRoundedCorner(SubPath& out, Point prev, Point p, Point next, double radius);
// Cubic approximation of an arc on a circle; angles in radians, appended as nodes (first node merged).
void appendArc(SubPath& out, Point center, double rx, double ry, double start, double sweep);
Path ellipsePath(Point center, double rx, double ry);
} // namespace kantan

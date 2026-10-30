#include "boolean/Boolean.h"
#ifdef _MSC_VER
// Clipper2's exception path leaves a fallback assignment unreachable in MSVC.
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
#include <clipper2/clipper.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace kantan {
namespace {
namespace c2 = Clipper2Lib;
constexpr int Precision = 4;
c2::PathsD toClipper(const Polygons& polygons) {
    c2::PathsD out;
    out.reserve(polygons.size());
    for (const auto& poly : polygons) {
        c2::PathD path;
        path.reserve(poly.size());
        for (auto p : poly)
            path.emplace_back(p.x, p.y);
        out.push_back(std::move(path));
    }
    return out;
}
Polygons fromClipper(const c2::PathsD& paths) {
    Polygons out;
    out.reserve(paths.size());
    for (const auto& path : paths) {
        if (path.size() < 3)
            continue;
        Polygon poly;
        poly.reserve(path.size());
        for (const auto& p : path)
            poly.push_back({p.x, p.y});
        out.push_back(std::move(poly));
    }
    return out;
}
c2::FillRule rule(FillRule r) {
    return r == FillRule::EvenOdd ? c2::FillRule::EvenOdd : c2::FillRule::NonZero;
}
c2::JoinType join(JoinStyle j) {
    switch (j) {
    case JoinStyle::Round:
        return c2::JoinType::Round;
    case JoinStyle::Bevel:
        return c2::JoinType::Bevel;
    default:
        return c2::JoinType::Miter;
    }
}
c2::PathsD normalizedPaths(const Operand& operand) {
    return c2::Union(toClipper(operand.polygons), rule(operand.rule), Precision);
}
} // namespace
Polygons normalized(const Operand& operand) {
    return fromClipper(normalizedPaths(operand));
}
Polygons applyBoolean(BooleanOp op, const std::vector<Operand>& operands) {
    if (operands.empty())
        return {};
    c2::PathsD result = normalizedPaths(operands[0]);
    for (std::size_t i = 1; i < operands.size(); ++i) {
        const auto next = normalizedPaths(operands[i]);
        switch (op) {
        case BooleanOp::Union:
            result = c2::Union(result, next, c2::FillRule::NonZero, Precision);
            break;
        case BooleanOp::Subtract:
            result = c2::Difference(result, next, c2::FillRule::NonZero, Precision);
            break;
        case BooleanOp::Intersect:
            result = c2::Intersect(result, next, c2::FillRule::NonZero, Precision);
            break;
        case BooleanOp::Exclude:
            result = c2::Xor(result, next, c2::FillRule::NonZero, Precision);
            break;
        }
    }
    return fromClipper(result);
}
Polygons offset(const Polygons& area, double delta, JoinStyle j, double miterLimit, double tolerance) {
    if (delta == 0)
        return normalized({area, FillRule::NonZero});
    // Normalise first so that holes and overlaps offset consistently.
    const auto base = c2::Union(toClipper(area), c2::FillRule::NonZero, Precision);
    return fromClipper(c2::InflatePaths(base, delta, join(j), c2::EndType::Polygon, std::max(1., miterLimit),
                                        Precision, std::max(1e-3, tolerance)));
}
Polygons band(const Polygons& area, double inner, double outer, JoinStyle j, double miterLimit,
              double tolerance) {
    if (!(outer > inner))
        return {};
    const auto base = c2::Union(toClipper(area), c2::FillRule::NonZero, Precision);
    const auto grow = [&](double delta) {
        return delta == 0 ? base
                          : c2::InflatePaths(base, delta, join(j), c2::EndType::Polygon,
                                             std::max(1., miterLimit), Precision, std::max(1e-3, tolerance));
    };
    return fromClipper(c2::Difference(grow(outer), grow(inner), c2::FillRule::NonZero, Precision));
}
Polygons strokeOutline(const Polygons& lines, bool closed, double width, JoinStyle j, CapStyle cap,
                       double miterLimit, double tolerance) {
    if (!(width > 0))
        return {};
    c2::EndType end = c2::EndType::Joined;
    if (!closed)
        end = cap == CapStyle::Round    ? c2::EndType::Round
              : cap == CapStyle::Square ? c2::EndType::Square
                                        : c2::EndType::Butt;
    const auto stroke = c2::InflatePaths(toClipper(lines), width / 2, join(j), end, std::max(1., miterLimit),
                                         Precision, std::max(1e-3, tolerance));
    return fromClipper(c2::Union(stroke, c2::FillRule::NonZero, Precision));
}
Polygons simplify(const Polygons& polygons, double tolerance) {
    return fromClipper(c2::SimplifyPaths(toClipper(polygons), std::max(1e-6, tolerance), true));
}
} // namespace kantan

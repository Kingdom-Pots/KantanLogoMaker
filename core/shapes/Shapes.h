#pragma once
#include "geometry/Geometry.h"
#include <array>
#include <variant>

namespace kantan {
enum class CornerStyle { Round, Chamfer, Inverted };
struct RectangleParams {
    std::array<double, 4> radii{}; // top-left, top-right, bottom-right, bottom-left
    bool linked{true};
    CornerStyle corner{CornerStyle::Round};
    auto operator<=>(const RectangleParams&) const = default;
};
struct EllipseParams {
    auto operator<=>(const EllipseParams&) const = default;
};
struct PolygonParams {
    int sides{6};
    double cornerRadius{};
    auto operator<=>(const PolygonParams&) const = default;
};
struct StarParams {
    int points{5};
    double innerRatio{.5}; // inner radius as a fraction of the outer radius
    double cornerRadius{};
    auto operator<=>(const StarParams&) const = default;
};
struct RingParams {
    double thickness{.3}; // fraction of the outer radius
    double startAngle{0}, endAngle{360};
    auto operator<=>(const RingParams&) const = default;
};
struct GearParams {
    int teeth{12};
    double toothDepth{.18}; // fraction of the outer radius
    double holeRadius{.3};  // fraction of the outer radius; 0 for none
    auto operator<=>(const GearParams&) const = default;
};
// One- to three-parameter decorative presets; parameter meaning is documented per kind in shapeParamNames.
enum class SimpleKind { Shield, Badge, Ribbon, Bubble, Arrow, Heart, Burst };
struct SimpleParams {
    SimpleKind kind{SimpleKind::Shield};
    std::array<double, 3> values{.5, .5, .5};
    auto operator<=>(const SimpleParams&) const = default;
};
// Parametric presets stay editable; Path is the explicit form after Convert to path / import.
using ShapeGeometry = std::variant<RectangleParams, EllipseParams, PolygonParams, StarParams, RingParams,
                                   GearParams, SimpleParams, Path>;

struct ParamRange {
    const char* name;
    double min, max, defaultValue;
};
std::array<ParamRange, 3> simpleParamRanges(SimpleKind kind);
const char* simpleKindName(SimpleKind kind);
// Local geometry centred on the origin, fitting width x height. Path geometry is returned unchanged.
Path shapePath(const ShapeGeometry& geometry, double width, double height);
Result validateShape(const ShapeGeometry& geometry, double width, double height);
} // namespace kantan

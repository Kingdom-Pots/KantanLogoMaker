#pragma once
#include "composite/Composite.h"
#include "geometry/Geometry.h"
#include <vector>

namespace kantan {
enum class FillKind { None, Solid, Linear, Radial };
inline constexpr Color DefaultShapeColor{64, 81, 100, 255}; // #405164, dark desaturated blue
struct GradientStop {
    double offset{};
    Color color;
    auto operator<=>(const GradientStop&) const = default;
};
// Gradient geometry is relative to the object's local bounds: (0,0) top-left, (1,1) bottom-right.
struct Paint {
    FillKind kind{FillKind::Solid};
    Color color{DefaultShapeColor};
    std::vector<GradientStop> stops{{0, {255, 255, 255, 255}}, {1, {0, 0, 0, 255}}};
    double angle{90};     // linear: degrees, 0 = left to right, 90 = top to bottom
    Point center{.5, .5}; // radial
    double radius{.5};    // radial, fraction of the bounds diagonal-normalised size
    auto operator<=>(const Paint&) const = default;
};
enum class OutlinePosition { Outside, Center, Inside };
inline constexpr std::size_t MaxOutlines = 3;
// Outside outlines stack outward (each starts where the previous outside one ended) and draw behind the fill;
// inside outlines stack inward on top of the fill; centre outlines straddle the edge on top of everything.
struct Outline {
    bool enabled{true};
    double width{4};
    Paint paint{FillKind::Solid, {255, 255, 255, 255}};
    OutlinePosition position{OutlinePosition::Outside};
    JoinStyle join{JoinStyle::Round};
    double miterLimit{4};
    auto operator<=>(const Outline&) const = default;
};
struct Shadow {
    bool enabled{};
    double angle{135}, distance{6}, blur{8}, spread{0};
    Color color{0, 0, 0, 255};
    double opacity{.5};
    auto operator<=>(const Shadow&) const = default;
};
struct Glow {
    bool enabled{};
    double size{10}, spread{0};
    Color color{255, 230, 120, 255};
    double opacity{.75};
    auto operator<=>(const Glow&) const = default;
};
enum class BevelStyle { Inner, Outer, Emboss, Pillow };
enum class BevelProfile { Linear, Round, Cove };
struct Bevel {
    bool enabled{};
    BevelStyle style{BevelStyle::Inner};
    BevelProfile profile{BevelProfile::Round};
    double size{8}, depth{1}, softness{0};
    double lightAngle{120}, altitude{30};
    Color highlight{255, 255, 255, 255};
    double highlightOpacity{.75};
    Color shadow{0, 0, 0, 255};
    double shadowOpacity{.6};
    auto operator<=>(const Bevel&) const = default;
};
// Rendered in a fixed order: drop shadow, outer glow, fill + outlines, inner shadow, inner glow, bevel.
struct Effects {
    Shadow dropShadow, innerShadow{false, 135, 4, 6, 0, {0, 0, 0, 255}, .5};
    Glow outerGlow, innerGlow{false, 8, 0, {255, 255, 255, 255}, .6};
    Bevel bevel;
    auto operator<=>(const Effects&) const = default;
};
struct Style {
    Paint fill;
    std::vector<Outline> outlines;
    Effects effects;
    auto operator<=>(const Style&) const = default;
};
Result validateStyle(const Style& style);
} // namespace kantan

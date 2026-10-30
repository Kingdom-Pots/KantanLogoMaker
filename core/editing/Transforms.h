#pragma once
#include "document/Document.h"
#include "render/Render.h"
#include <array>

namespace kantan {
// Translation/rotation/scale decomposition; nullopt when the matrix contains skew.
std::optional<Transform> decompose(const Affine& m);
// Move objects by an artboard-space delta (converted into each object's parent space).
std::vector<Object> translated(const DocumentData& doc, const std::vector<ObjectId>& ids, Point delta);
// Scale an object's local geometry by (sx, sy) about a local anchor; parametric shapes, images and paths
// change their size/nodes, text and containers change transform scale. Negative factors flip.
Object resizedLocal(const Object& object, double sx, double sy, Point localAnchor);
// Scale a selection by (sx, sy) about an artboard-space pivot (axis-aligned selection box gizmo).
std::vector<Object> scaledAbout(const DocumentData& doc, const std::vector<ObjectId>& ids, double sx,
                                double sy, Point pivot);
// Rotate a selection about an artboard-space pivot.
std::vector<Object> rotatedAbout(const DocumentData& doc, const std::vector<ObjectId>& ids, double degrees,
                                 Point pivot);
std::vector<Object> flipped(const DocumentData& doc, const std::vector<ObjectId>& ids, bool horizontal,
                            const RenderServices& services);
Box selectionBounds(const DocumentData& doc, const std::vector<ObjectId>& ids,
                    const RenderServices& services);

enum class Alignment { Left, CenterX, Right, Top, CenterY, Bottom };
std::vector<Object> aligned(const DocumentData& doc, const std::vector<ObjectId>& ids, Box reference,
                            Alignment alignment, const RenderServices& services);
std::vector<Object> distributed(const DocumentData& doc, const std::vector<ObjectId>& ids, bool horizontal,
                                const RenderServices& services);

enum class SnapKind { None, Pixel, Artboard, Object, Guide };
struct SnapSettings {
    bool enabled{true}, pixels{true}, artboard{true}, objects{true}, guides{true};
    auto operator<=>(const SnapSettings&) const = default;
};
struct SnapResult {
    Point delta;                   // adjusted translation
    std::array<SnapKind, 2> kinds; // x, y
    Point guide{std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()}; // NaN when unused
};
// Snap a moving box's edges/centre to targets, within `tolerance` artboard units.
SnapResult snapTranslation(const DocumentData& doc, Box moving, Point delta,
                           const std::vector<ObjectId>& exclude, const SnapSettings& settings,
                           double tolerance, const RenderServices& services);
Point snapPoint(const DocumentData& doc, Point point, const std::vector<ObjectId>& exclude,
                const SnapSettings& settings, double tolerance, const RenderServices& services,
                std::array<SnapKind, 2>* kinds = nullptr);
struct ResizeSnap {
    double sx{1}, sy{1};
    SnapResult hint{};
};
// Axis-aligned resize in artboard coordinates; opposite (or centre) anchor stays fixed.
ResizeSnap snapResize(const DocumentData& doc, Point start, Point anchor, Point pointer, bool horizontal,
                      bool vertical, bool uniform, const std::vector<ObjectId>& exclude,
                      const SnapSettings& settings, double tolerance, const RenderServices& services,
                      const SnapResult* previous = nullptr);

// Replace parametric shapes / text with explicit paths (Convert to path). Text needs a shaper.
std::vector<Object> convertedToPath(const DocumentData& doc, const std::vector<ObjectId>& ids,
                                    const RenderServices& services);
// Fit Bézier paths to closed polygons: sharp corners stay corners, smooth runs become cubic segments.
Path fitPath(const Polygons& polygons);
// Path operations producing explicit-path replacements (same id; computed in the object's local space).
std::optional<Object> offsetObject(const DocumentData& doc, ObjectId id, double delta, JoinStyle join,
                                   const RenderServices& services);
// Turn the first enabled outline (or an open line's stroke) into a filled shape with that paint.
std::optional<Object> outlineStrokeObject(const DocumentData& doc, ObjectId id,
                                          const RenderServices& services);
std::optional<Object> simplifiedObject(const DocumentData& doc, ObjectId id, double tolerance,
                                       const RenderServices& services);
// Reverse the direction of every subpath of an explicit path.
std::optional<Object> reversedObject(const DocumentData& doc, ObjectId id);
// Open a closed subpath at a node (Break), or close every open subpath (Join ends).
std::optional<Object> breakAtNode(const DocumentData& doc, ObjectId id, std::size_t subpath,
                                  std::size_t node);
std::optional<Object> closedPaths(const DocumentData& doc, ObjectId id);
// Bake a live boolean into one explicit path object (returns the replacement for the boolean root).
std::optional<Object> flattenedBoolean(const DocumentData& doc, ObjectId booleanId,
                                       const RenderServices& services);
} // namespace kantan

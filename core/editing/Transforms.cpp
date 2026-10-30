#include "editing/Transforms.h"
#include "boolean/Boolean.h"
#include <numbers>

namespace kantan {
namespace {
constexpr double Degrees = 180 / std::numbers::pi;
std::vector<ObjectId> roots(const DocumentData& doc, const std::vector<ObjectId>& ids) {
    std::vector<ObjectId> out;
    for (const auto id : ids) {
        if (!doc.find(id) || std::find(out.begin(), out.end(), id) != out.end())
            continue;
        bool nested = false;
        for (const auto other : ids)
            nested = nested || (other != id && doc.isAncestor(other, id));
        if (!nested)
            out.push_back(id);
    }
    return out;
}
double worldAngle(const DocumentData& doc, ObjectId id) {
    const Affine w = doc.worldMatrix(id);
    return std::atan2(w.b, w.a) * Degrees;
}
Point toParent(const DocumentData& doc, ObjectId id, Point artboard) {
    const Affine p = doc.parentMatrix(id);
    return p.invertible() ? p.inverted().map(artboard) : artboard;
}
Point scalePath(Point p, double sx, double sy, Point a) {
    return {a.x + (p.x - a.x) * sx, a.y + (p.y - a.y) * sy};
}
} // namespace
std::optional<Transform> decompose(const Affine& m) {
    const double sx = std::hypot(m.a, m.b);
    if (!(sx > 1e-9) || !m.invertible())
        return std::nullopt;
    const double rotation = std::atan2(m.b, m.a);
    const double sy = m.determinant() / sx;
    const double c = -sy * std::sin(rotation), d = sy * std::cos(rotation);
    const double tolerance = 1e-6 * std::max({1., std::abs(sx), std::abs(sy)});
    if (std::abs(c - m.c) > tolerance || std::abs(d - m.d) > tolerance)
        return std::nullopt;
    return Transform{{m.e, m.f}, rotation * Degrees, sx, sy};
}
std::vector<Object> translated(const DocumentData& doc, const std::vector<ObjectId>& ids, Point delta) {
    std::vector<Object> out;
    for (const auto id : roots(doc, ids)) {
        Object o = *doc.find(id);
        const Affine p = doc.parentMatrix(id);
        o.transform.position =
            o.transform.position + (p.invertible() ? p.inverted().mapVector(delta) : delta);
        out.push_back(std::move(o));
    }
    return out;
}
Object resizedLocal(const Object& object, double sx, double sy, Point a) {
    Object o = object;
    sx = std::abs(sx) < 1e-6 ? (sx < 0 ? -1e-6 : 1e-6) : sx;
    sy = std::abs(sy) < 1e-6 ? (sy < 0 ? -1e-6 : 1e-6) : sy;
    bool geometric = false;
    if (auto* s = o.as<ShapeData>()) {
        if (auto* path = std::get_if<Path>(&s->geometry)) {
            for (auto& sub : path->subpaths) {
                for (auto& n : sub.nodes) {
                    n.anchor = scalePath(n.anchor, sx, sy, {});
                    n.in = scalePath(n.in, sx, sy, {});
                    n.out = scalePath(n.out, sx, sy, {});
                }
                // A single mirror reverses winding; keep orientation consistent for even-odd and offsets.
                if (sx * sy < 0)
                    reverse(sub);
            }
            const Box b = bounds(*path);
            s->width = b.empty() ? 0 : b.width();
            s->height = b.empty() ? 0 : b.height();
            geometric = true;
        } else {
            s->width *= std::abs(sx);
            s->height *= std::abs(sy);
            o.transform.scaleX *= sx < 0 ? -1 : 1;
            o.transform.scaleY *= sy < 0 ? -1 : 1;
            geometric = true;
        }
    } else if (auto* image = o.as<ImageData>()) {
        image->width *= std::abs(sx);
        image->height *= std::abs(sy);
        o.transform.scaleX *= sx < 0 ? -1 : 1;
        o.transform.scaleY *= sy < 0 ? -1 : 1;
        geometric = true;
    } else {
        o.transform.scaleX *= sx;
        o.transform.scaleY *= sy;
    }
    // Keep the anchor fixed in parent space.
    const Point before = object.transform.matrix().map(a);
    const Point anchorAfter = geometric ? Point{a.x * std::abs(sx), a.y * std::abs(sy)} : a;
    const Point after = o.transform.matrix().map(anchorAfter);
    o.transform.position = o.transform.position + (before - after);
    return o;
}
std::vector<Object> scaledAbout(const DocumentData& doc, const std::vector<ObjectId>& ids, double sx,
                                double sy, Point pivot) {
    std::vector<Object> out;
    for (const auto id : roots(doc, ids)) {
        const Object& o = *doc.find(id);
        const double angle = worldAngle(doc, id);
        const double quarter = std::round(angle / 90);
        double fx, fy;
        if (std::abs(angle - quarter * 90) < .01) {
            const bool swapped = static_cast<long long>(quarter) % 2 != 0;
            fx = swapped ? sy : sx;
            fy = swapped ? sx : sy;
        } else
            fx = fy = std::sqrt(std::abs(sx * sy)) * (sx * sy < 0 ? -1 : 1);
        Object scaled = resizedLocal(o, fx, fy, {});
        const Affine parent = doc.parentMatrix(id);
        const Point world = parent.map(o.transform.position);
        const Point moved{pivot.x + (world.x - pivot.x) * sx, pivot.y + (world.y - pivot.y) * sy};
        scaled.transform.position = toParent(doc, id, moved);
        out.push_back(std::move(scaled));
    }
    return out;
}
std::vector<Object> rotatedAbout(const DocumentData& doc, const std::vector<ObjectId>& ids, double degrees,
                                 Point pivot) {
    std::vector<Object> out;
    const Affine r = Affine::translation(pivot.x, pivot.y) * Affine::rotation(degrees) *
                     Affine::translation(-pivot.x, -pivot.y);
    for (const auto id : roots(doc, ids)) {
        Object o = *doc.find(id);
        const Affine parent = doc.parentMatrix(id);
        o.transform.position = toParent(doc, id, r.map(parent.map(o.transform.position)));
        o.transform.rotation += parent.determinant() < 0 ? -degrees : degrees;
        o.transform.rotation = std::remainder(o.transform.rotation, 360.);
        out.push_back(std::move(o));
    }
    return out;
}
std::vector<Object> flipped(const DocumentData& doc, const std::vector<ObjectId>& ids, bool horizontal,
                            const RenderServices& services) {
    const auto list = roots(doc, ids);
    const Point c = selectionBounds(doc, list, services).center();
    std::vector<Object> out;
    for (const auto id : list) {
        const Object& o = *doc.find(id);
        const double world = worldAngle(doc, id);
        Object f = resizedLocal(o, horizontal ? -1 : 1, horizontal ? 1 : -1, {});
        f.transform.rotation = std::remainder(o.transform.rotation - 2 * world, 360.);
        const Point w = doc.parentMatrix(id).map(o.transform.position);
        const Point mirrored = horizontal ? Point{2 * c.x - w.x, w.y} : Point{w.x, 2 * c.y - w.y};
        f.transform.position = toParent(doc, id, mirrored);
        out.push_back(std::move(f));
    }
    return out;
}
Box selectionBounds(const DocumentData& doc, const std::vector<ObjectId>& ids,
                    const RenderServices& services) {
    Box b = emptyBox();
    for (const auto id : roots(doc, ids))
        b = united(b, objectBounds(doc, *doc.find(id), services));
    return b;
}
std::vector<Object> aligned(const DocumentData& doc, const std::vector<ObjectId>& ids, Box ref,
                            Alignment alignment, const RenderServices& services) {
    std::vector<Object> out;
    for (const auto id : roots(doc, ids)) {
        const Box b = objectBounds(doc, *doc.find(id), services);
        Point d{};
        switch (alignment) {
        case Alignment::Left:
            d.x = ref.x0 - b.x0;
            break;
        case Alignment::CenterX:
            d.x = ref.center().x - b.center().x;
            break;
        case Alignment::Right:
            d.x = ref.x1 - b.x1;
            break;
        case Alignment::Top:
            d.y = ref.y0 - b.y0;
            break;
        case Alignment::CenterY:
            d.y = ref.center().y - b.center().y;
            break;
        case Alignment::Bottom:
            d.y = ref.y1 - b.y1;
            break;
        }
        if (d.x != 0 || d.y != 0) {
            auto moved = translated(doc, {id}, d);
            out.insert(out.end(), moved.begin(), moved.end());
        }
    }
    return out;
}
std::vector<Object> distributed(const DocumentData& doc, const std::vector<ObjectId>& ids, bool horizontal,
                                const RenderServices& services) {
    auto list = roots(doc, ids);
    if (list.size() < 3)
        return {};
    std::vector<std::pair<ObjectId, Box>> items;
    for (const auto id : list)
        items.push_back({id, objectBounds(doc, *doc.find(id), services)});
    std::sort(items.begin(), items.end(), [&](const auto& a, const auto& b) {
        return horizontal ? a.second.center().x < b.second.center().x
                          : a.second.center().y < b.second.center().y;
    });
    const auto lo = [&](const Box& b) { return horizontal ? b.x0 : b.y0; };
    const auto size = [&](const Box& b) { return horizontal ? b.width() : b.height(); };
    const double start = lo(items.front().second);
    const double end = lo(items.back().second) + size(items.back().second);
    double total = 0;
    for (const auto& item : items)
        total += size(item.second);
    const double gap = (end - start - total) / static_cast<double>(items.size() - 1);
    std::vector<Object> out;
    double cursor = start;
    for (const auto& [id, box] : items) {
        const double d = cursor - lo(box);
        if (std::abs(d) > 1e-9) {
            auto moved = translated(doc, {id}, horizontal ? Point{d, 0} : Point{0, d});
            out.insert(out.end(), moved.begin(), moved.end());
        }
        cursor += size(box) + gap;
    }
    return out;
}
namespace {
struct Targets {
    std::vector<std::pair<double, SnapKind>> x, y;
};
Targets snapTargets(const DocumentData& doc, const std::vector<ObjectId>& exclude, const SnapSettings& s,
                    const RenderServices& services) {
    Targets t;
    if (s.artboard)
        for (double f : {0., .5, 1.}) {
            t.x.push_back({doc.artboard.width * f, SnapKind::Artboard});
            t.y.push_back({doc.artboard.height * f, SnapKind::Artboard});
        }
    if (s.guides) {
        for (double g : doc.artboard.guidesX)
            t.x.push_back({g, SnapKind::Guide});
        for (double g : doc.artboard.guidesY)
            t.y.push_back({g, SnapKind::Guide});
    }
    if (s.objects)
        for (const auto& ref : doc.objects) {
            const auto& o = *ref;
            if (o.parent || !doc.effectiveVisible(o.id))
                continue; // top-level objects only: groups snap as a whole
            bool skip = false;
            for (const auto id : exclude)
                skip = skip || id == o.id || doc.isAncestor(o.id, id) || doc.isAncestor(id, o.id);
            if (skip)
                continue;
            const Box b = objectBounds(doc, o, services);
            for (double v : {b.x0, b.center().x, b.x1})
                t.x.push_back({v, SnapKind::Object});
            for (double v : {b.y0, b.center().y, b.y1})
                t.y.push_back({v, SnapKind::Object});
        }
    return t;
}
// Best adjustment for any candidate onto any target within tolerance.
std::optional<std::pair<double, std::pair<double, SnapKind>>>
best(const std::vector<double>& candidates, const std::vector<std::pair<double, SnapKind>>& targets,
     double tolerance) {
    std::optional<std::pair<double, std::pair<double, SnapKind>>> found;
    double distance = tolerance;
    for (double c : candidates)
        for (const auto& target : targets) {
            const double d = std::abs(target.first - c);
            if (d <= distance) {
                distance = d;
                found = {{target.first - c, target}};
            }
        }
    return found;
}
} // namespace
SnapResult snapTranslation(const DocumentData& doc, Box moving, Point delta,
                           const std::vector<ObjectId>& exclude, const SnapSettings& settings,
                           double tolerance, const RenderServices& services) {
    SnapResult result{delta, {SnapKind::None, SnapKind::None}};
    if (!settings.enabled || moving.empty())
        return result;
    const auto targets = snapTargets(doc, exclude, settings, services);
    const Box m{moving.x0 + delta.x, moving.y0 + delta.y, moving.x1 + delta.x, moving.y1 + delta.y};
    if (auto x = best({m.x0, m.center().x, m.x1}, targets.x, tolerance)) {
        result.delta.x += x->first;
        result.kinds[0] = x->second.second;
        result.guide.x = x->second.first;
    } else if (settings.pixels) {
        result.delta.x = std::round(moving.x0 + delta.x) - moving.x0;
        result.kinds[0] = SnapKind::Pixel;
    }
    if (auto y = best({m.y0, m.center().y, m.y1}, targets.y, tolerance)) {
        result.delta.y += y->first;
        result.kinds[1] = y->second.second;
        result.guide.y = y->second.first;
    } else if (settings.pixels) {
        result.delta.y = std::round(moving.y0 + delta.y) - moving.y0;
        result.kinds[1] = SnapKind::Pixel;
    }
    return result;
}
ResizeSnap snapResize(const DocumentData& doc, Point start, Point anchor, Point pointer, bool horizontal,
                      bool vertical, bool uniform, const std::vector<ObjectId>& exclude,
                      const SnapSettings& settings, double tolerance, const RenderServices& services,
                      const SnapResult* previous) {
    ResizeSnap result;
    const double dx = start.x - anchor.x, dy = start.y - anchor.y;
    horizontal = horizontal && std::abs(dx) > 1e-9;
    vertical = vertical && std::abs(dy) > 1e-9;
    double sx = horizontal ? (pointer.x - anchor.x) / dx : 1;
    double sy = vertical ? (pointer.y - anchor.y) / dy : 1;
    if (uniform && horizontal && vertical) {
        const double scale = std::max(std::abs(sx), std::abs(sy));
        sx = std::copysign(scale, sx);
        sy = std::copysign(scale, sy);
    }
    const Point proposed{anchor.x + dx * sx, anchor.y + dy * sy};
    std::array<SnapKind, 2> kinds{};
    auto snapped = snapPoint(doc, proposed, exclude, settings, tolerance, services, &kinds);
    if (settings.enabled && previous) {
        if (std::isfinite(previous->guide.x) && std::abs(proposed.x - previous->guide.x) <= tolerance * 1.5) {
            snapped.x = previous->guide.x;
            kinds[0] = previous->kinds[0];
        }
        if (std::isfinite(previous->guide.y) && std::abs(proposed.y - previous->guide.y) <= tolerance * 1.5) {
            snapped.y = previous->guide.y;
            kinds[1] = previous->kinds[1];
        }
    }
    bool snapX = horizontal && kinds[0] != SnapKind::None;
    bool snapY = vertical && kinds[1] != SnapKind::None;
    if (uniform && horizontal && vertical) {
        // One constraint determines the shared magnitude. Never break the aspect lock.
        const double xCorrection = std::abs(snapped.x - proposed.x);
        const double yCorrection = std::abs(snapped.y - proposed.y);
        const bool preferX = kinds[0] != SnapKind::Pixel && kinds[1] == SnapKind::Pixel;
        const bool preferY = kinds[1] != SnapKind::Pixel && kinds[0] == SnapKind::Pixel;
        if (snapX && (!snapY || preferX || (!preferY && xCorrection <= yCorrection))) {
            sx = (snapped.x - anchor.x) / dx;
            sy = std::copysign(std::abs(sx), sy);
            snapY = false;
        } else if (snapY) {
            sy = (snapped.y - anchor.y) / dy;
            sx = std::copysign(std::abs(sy), sx);
            snapX = false;
        }
    } else {
        if (snapX)
            sx = (snapped.x - anchor.x) / dx;
        if (snapY)
            sy = (snapped.y - anchor.y) / dy;
    }
    result.sx = sx;
    result.sy = sy;
    result.hint.kinds = {snapX ? kinds[0] : SnapKind::None, snapY ? kinds[1] : SnapKind::None};
    if (snapX && kinds[0] != SnapKind::Pixel)
        result.hint.guide.x = snapped.x;
    if (snapY && kinds[1] != SnapKind::Pixel)
        result.hint.guide.y = snapped.y;
    return result;
}
Point snapPoint(const DocumentData& doc, Point p, const std::vector<ObjectId>& exclude,
                const SnapSettings& settings, double tolerance, const RenderServices& services,
                std::array<SnapKind, 2>* kinds) {
    std::array<SnapKind, 2> k{SnapKind::None, SnapKind::None};
    if (settings.enabled) {
        const auto targets = snapTargets(doc, exclude, settings, services);
        if (auto x = best({p.x}, targets.x, tolerance)) {
            p.x += x->first;
            k[0] = x->second.second;
        } else if (settings.pixels) {
            p.x = std::round(p.x);
            k[0] = SnapKind::Pixel;
        }
        if (auto y = best({p.y}, targets.y, tolerance)) {
            p.y += y->first;
            k[1] = y->second.second;
        } else if (settings.pixels) {
            p.y = std::round(p.y);
            k[1] = SnapKind::Pixel;
        }
    }
    if (kinds)
        *kinds = k;
    return p;
}
std::vector<Object> convertedToPath(const DocumentData& doc, const std::vector<ObjectId>& ids,
                                    const RenderServices& services) {
    std::vector<Object> out;
    for (const auto id : ids) {
        const auto* o = doc.find(id);
        if (!o)
            continue;
        Object c = *o;
        if (const auto* s = o->as<ShapeData>()) {
            if (std::holds_alternative<Path>(s->geometry))
                continue;
            Path path = shapePath(s->geometry, s->width, s->height);
            c.data = ShapeData{s->width, s->height, std::move(path)};
        } else if (const auto* t = o->as<TextData>()) {
            if (!services.text)
                continue;
            Path path = services.text->shape(*t, doc);
            const Box b = bounds(path);
            c.data = ShapeData{b.empty() ? 0 : b.width(), b.empty() ? 0 : b.height(), std::move(path)};
        } else
            continue;
        out.push_back(std::move(c));
    }
    return out;
}
namespace {
// Fit Bézier nodes through a closed polygon: corners stay sharp, smooth runs become Catmull-Rom cubics.
SubPath fitClosed(const Polygon& poly) {
    SubPath s;
    const std::size_t n = poly.size();
    std::vector<bool> corner(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Point a = poly[(i + n - 1) % n], p = poly[i], b = poly[(i + 1) % n];
        const Point u = p - a, v = b - p;
        const double lu = length(u), lv = length(v);
        corner[i] = lu <= 0 || lv <= 0 || dot(u, v) / (lu * lv) < std::cos(28 * std::numbers::pi / 180);
    }
    for (std::size_t i = 0; i < n; ++i) {
        const Point prev = poly[(i + n - 1) % n], p = poly[i], next = poly[(i + 1) % n];
        Node node = Node::corner(p);
        if (!corner[i]) {
            const Point tangent = (next - prev) * (1. / 6.);
            node = {p, corner[(i + n - 1) % n] ? p : p - tangent, corner[(i + 1) % n] ? p : p + tangent,
                    NodeType::Smooth};
        }
        s.nodes.push_back(node);
    }
    return s;
}
} // namespace
Path fitPath(const Polygons& polygons) {
    Path path;
    path.rule = FillRule::NonZero;
    for (const auto& poly : polygons)
        if (poly.size() >= 3)
            path.subpaths.push_back(fitClosed(poly));
    return path;
}
namespace {
Object withPath(const Object& base, Path path) {
    Object o = base;
    const Box b = bounds(path);
    o.data = ShapeData{b.empty() ? 0 : b.width(), b.empty() ? 0 : b.height(), std::move(path)};
    return o;
}
std::optional<Polygons> localArea(const DocumentData& doc, const Object& o, const RenderServices& services) {
    if (!o.transform.matrix().invertible() || o.kind() == ObjectKind::Image ||
        o.kind() == ObjectKind::Group || o.kind() == ObjectKind::Clip)
        return std::nullopt;
    return normalized(objectArea(doc, o, o.transform.matrix().inverted(), .02, services));
}
const Path* explicitPath(const Object& o) {
    const auto* s = o.as<ShapeData>();
    return s ? std::get_if<Path>(&s->geometry) : nullptr;
}
} // namespace
std::optional<Object> offsetObject(const DocumentData& doc, ObjectId id, double delta, JoinStyle join,
                                   const RenderServices& services) {
    const auto* o = doc.find(id);
    if (!o)
        return std::nullopt;
    const auto area = localArea(doc, *o, services);
    if (!area)
        return std::nullopt;
    auto grown = offset(*area, delta, join, 4, .02);
    if (grown.empty())
        return std::nullopt;
    return withPath(*o, fitPath(simplify(grown, .02)));
}
std::optional<Object> outlineStrokeObject(const DocumentData& doc, ObjectId id,
                                          const RenderServices& services) {
    const auto* o = doc.find(id);
    if (!o || !o->transform.matrix().invertible())
        return std::nullopt;
    const Outline* outline = nullptr;
    for (const auto& candidate : o->style.outlines)
        if (candidate.enabled && candidate.width > 0) {
            outline = &candidate;
            break;
        }
    if (!outline)
        return std::nullopt;
    const Affine toLocal = o->transform.matrix().inverted();
    const auto lines = objectLines(doc, *o, toLocal, services);
    Polygons stroke;
    if (!lines.empty())
        stroke = strokeOutline(lines, false, outline->width, outline->join, CapStyle::Round,
                               outline->miterLimit, .02);
    else if (const auto area = localArea(doc, *o, services)) {
        const double w = outline->width;
        const auto [inner, outer] = outline->position == OutlinePosition::Outside  ? std::pair{0., w}
                                    : outline->position == OutlinePosition::Inside ? std::pair{-w, 0.}
                                                                                   : std::pair{-w / 2, w / 2};
        stroke = band(*area, inner, outer, outline->join, outline->miterLimit, .02);
    }
    if (stroke.empty())
        return std::nullopt;
    Object result = withPath(*o, fitPath(simplify(stroke, .02)));
    result.id = {};
    result.name = (o->name.empty() ? std::string("Object") : o->name) + " outline";
    result.style = {};
    result.style.fill = outline->paint;
    result.repeat = o->repeat;
    return result;
}
std::optional<Object> simplifiedObject(const DocumentData& doc, ObjectId id, double tolerance,
                                       const RenderServices& services) {
    const auto* o = doc.find(id);
    if (!o)
        return std::nullopt;
    const auto area = localArea(doc, *o, services);
    if (!area || area->empty())
        return std::nullopt;
    return withPath(*o, fitPath(simplify(*area, std::max(tolerance, .01))));
}
std::optional<Object> reversedObject(const DocumentData& doc, ObjectId id) {
    const auto* o = doc.find(id);
    if (!o || !explicitPath(*o))
        return std::nullopt;
    Path path = *explicitPath(*o);
    for (auto& s : path.subpaths)
        reverse(s);
    return withPath(*o, std::move(path));
}
std::optional<Object> breakAtNode(const DocumentData& doc, ObjectId id, std::size_t subpath,
                                  std::size_t node) {
    const auto* o = doc.find(id);
    if (!o || !explicitPath(*o))
        return std::nullopt;
    Path path = *explicitPath(*o);
    if (subpath >= path.subpaths.size())
        return std::nullopt;
    auto& s = path.subpaths[subpath];
    if (node >= s.nodes.size())
        return std::nullopt;
    if (s.closed) {
        std::rotate(s.nodes.begin(), s.nodes.begin() + static_cast<std::ptrdiff_t>(node), s.nodes.end());
        Node end = s.nodes.front();
        s.nodes.front().in = s.nodes.front().anchor;
        end.out = end.anchor;
        s.nodes.push_back(end);
        s.closed = false;
    } else {
        if (node == 0 || node + 1 >= s.nodes.size())
            return std::nullopt;
        SubPath tail{{s.nodes.begin() + static_cast<std::ptrdiff_t>(node), s.nodes.end()}, false};
        s.nodes.resize(node + 1);
        s.nodes.back().out = s.nodes.back().anchor;
        tail.nodes.front().in = tail.nodes.front().anchor;
        path.subpaths.insert(path.subpaths.begin() + static_cast<std::ptrdiff_t>(subpath) + 1,
                             std::move(tail));
    }
    return withPath(*o, std::move(path));
}
std::optional<Object> closedPaths(const DocumentData& doc, ObjectId id) {
    const auto* o = doc.find(id);
    if (!o || !explicitPath(*o))
        return std::nullopt;
    Path path = *explicitPath(*o);
    bool changed = false;
    for (auto& s : path.subpaths)
        if (!s.closed && s.nodes.size() >= 3) {
            s.closed = true;
            changed = true;
        }
    if (!changed)
        return std::nullopt;
    return withPath(*o, std::move(path));
}
std::optional<Object> flattenedBoolean(const DocumentData& doc, ObjectId booleanId,
                                       const RenderServices& services) {
    const auto* o = doc.find(booleanId);
    if (!o || o->kind() != ObjectKind::Boolean || !o->transform.matrix().invertible())
        return std::nullopt;
    // Compute in the boolean's own local space.
    const auto area = objectArea(doc, *o, o->transform.matrix().inverted(), .02, services);
    return withPath(*o, fitPath(simplify(area.polygons, .05)));
}
} // namespace kantan

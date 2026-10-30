#include "render/Render.h"
#include "effects/Effects.h"
#include "raster/Rasterizer.h"
#include "render/ImageSampling.h"
#include <numbers>

namespace kantan {
namespace {
constexpr double PixelTolerance = .1; // flattening tolerance in output pixels
Affine repeatMatrix(const Object& o, int copy) {
    if (o.repeat.count <= 1 || copy == 0)
        return {};
    const Point pivot = o.transform.position + o.repeat.pivot;
    return Affine::translation(pivot.x, pivot.y) * Affine::rotation(360. * copy / o.repeat.count) *
           Affine::translation(-pivot.x, -pivot.y);
}
Polygons transformedPolygons(const Polygons& polygons, const Affine& m) {
    Polygons out = polygons;
    for (auto& poly : out)
        for (auto& p : poly)
            p = m.map(p);
    return out;
}
Box imageLocalBox(const ImageData& image) {
    return {-image.width / 2, -image.height / 2, image.width / 2, image.height / 2};
}
// Local (pre-transform) geometry of a leaf object.
std::optional<Path> leafPath(const DocumentData& doc, const Object& o, const RenderServices& services) {
    if (const auto* s = o.as<ShapeData>())
        return shapePath(s->geometry, s->width, s->height);
    if (const auto* t = o.as<TextData>())
        return services.text ? std::optional<Path>(services.text->shape(*t, doc)) : std::nullopt;
    if (const auto* image = o.as<ImageData>()) {
        const Box b = imageLocalBox(*image);
        Path p;
        p.subpaths.push_back({{Node::corner({b.x0, b.y0}), Node::corner({b.x1, b.y0}),
                               Node::corner({b.x1, b.y1}), Node::corner({b.x0, b.y1})},
                              true});
        return p;
    }
    return std::nullopt;
}
Operand containerArea(const DocumentData& doc, const Object& o, const Affine& local, double tolerance,
                      const RenderServices& services) {
    std::vector<Operand> operands;
    for (const auto id : doc.children(o.id)) {
        const auto* child = doc.find(id);
        if (!child || !child->visible)
            continue;
        for (int copy = 0; copy < child->repeat.count; ++copy)
            operands.push_back(
                objectArea(doc, *child, local * repeatMatrix(*child, copy), tolerance, services));
        if (o.kind() == ObjectKind::Clip)
            break; // only the clip shape defines a clip group's area
    }
    if (const auto* b = o.as<BooleanData>())
        return {applyBoolean(b->op, operands), FillRule::NonZero};
    return {applyBoolean(BooleanOp::Union, operands), FillRule::NonZero};
}
double pixelScale(const Affine& m) {
    return std::sqrt(std::abs(m.determinant()));
}
struct Piece {
    Polygons area;
    const Paint* paint;
};
// Layer-local raster of one leaf (or boolean) object with fill, outlines and effects.
struct ObjectLayer {
    Rect bounds; // output pixels
    Image pixels;
};
void paintCoverage(Image& layer, Rect bounds, const Polygons& area, const Paint& paint,
                   const Affine& pixelToLocal01, const std::shared_ptr<MemoryBudget>& budget) {
    if (paint.kind == FillKind::None || area.empty())
        return;
    auto shifted = transformedPolygons(
        area, Affine::translation(static_cast<double>(-bounds.x), static_cast<double>(-bounds.y)));
    const auto cov = rasterize(shifted, FillRule::NonZero, bounds.w, bounds.h, budget);
    const bool solid = paint.kind == FillKind::Solid;
    const Pixel solidColor = paint.color.premultiplied();
    for (Coord y = 0; y < bounds.h; ++y)
        for (Coord x = 0; x < bounds.w; ++x) {
            const auto c = cov.values[static_cast<std::size_t>(y * bounds.w + x)];
            if (!c)
                continue;
            Pixel p = solid ? solidColor
                            : shade(paint, pixelToLocal01.map({static_cast<double>(bounds.x + x) + .5,
                                                               static_cast<double>(bounds.y + y) + .5}));
            if (c != 255)
                p = scaled(p, c / 255.);
            auto& d = layer.ref(x, y);
            d = over(p, d);
        }
}
// Open subpaths (pen lines) of a leaf, flattened in output space; they only take outlines.
Polygons openLines(const Path& path, const Affine& m) {
    Path open;
    for (const auto& s : path.subpaths)
        if (!s.closed && s.nodes.size() >= 2)
            open.subpaths.push_back(s);
    return open.subpaths.empty() ? Polygons{} : flatten(transformed(open, m), PixelTolerance);
}
ObjectLayer renderLeaf(const Operand& area, const Polygons& lines, const Object& o,
                       const Affine& localToPixels, Rect clip, const RenderRequest& request,
                       const std::shared_ptr<MemoryBudget>& budget) {
    const double scale = pixelScale(localToPixels);
    const double tol = PixelTolerance;
    const Polygons fillArea = normalized(area);
    if (fillArea.empty() && lines.empty())
        return {};
    // Outline bands (outside cumulative behind the fill, inside cumulative above it, centre on top).
    std::vector<Piece> behind, above, top;
    double outside = 0, insideDepth = 0, extent = 0;
    std::vector<Polygons> storage;
    storage.reserve(o.style.outlines.size());
    for (const auto& outline : o.style.outlines) {
        if (!outline.enabled || outline.width <= 0 || outline.paint.kind == FillKind::None)
            continue;
        const double w = outline.width * scale;
        if (!lines.empty()) {
            // Open lines have no inside: every outline strokes them centred, on top.
            storage.push_back(
                strokeOutline(lines, false, w, outline.join, CapStyle::Round, outline.miterLimit, tol));
            top.push_back({storage.back(), &outline.paint});
            extent = std::max(extent, w / 2);
            if (fillArea.empty())
                continue;
        }
        switch (outline.position) {
        case OutlinePosition::Outside:
            storage.push_back(band(fillArea, outside, outside + w, outline.join, outline.miterLimit, tol));
            behind.push_back({storage.back(), &outline.paint});
            outside += w;
            break;
        case OutlinePosition::Inside:
            storage.push_back(
                band(fillArea, -(insideDepth + w), -insideDepth, outline.join, outline.miterLimit, tol));
            above.push_back({storage.back(), &outline.paint});
            insideDepth += w;
            break;
        case OutlinePosition::Center:
            storage.push_back(band(fillArea, -w / 2, w / 2, outline.join, outline.miterLimit, tol));
            top.push_back({storage.back(), &outline.paint});
            extent = std::max(extent, w / 2);
            break;
        }
    }
    extent = std::max(extent, outside);
    const double effects = effectExtent(o.style.effects, scale);
    Box box = inflated(united(bounds(fillArea), bounds(lines)), extent + effects + 2);
    const Rect full{static_cast<Coord>(std::floor(box.x0)), static_cast<Coord>(std::floor(box.y0)), 0, 0};
    Rect r{full.x, full.y, static_cast<Coord>(std::ceil(box.x1)) - full.x,
           static_cast<Coord>(std::ceil(box.y1)) - full.y};
    r = intersect(r, clip);
    if (r.empty())
        return {};
    ObjectLayer layer{r, Image::make(r.w, r.h, budget)};
    // Gradient coordinates: normalized object-local bounds.
    const Affine pixelsToLocal = localToPixels.inverted();
    const Box localBox = bounds(transformedPolygons(fillArea.empty() ? lines : fillArea, pixelsToLocal));
    const double lw = std::max(localBox.width(), 1e-9), lh = std::max(localBox.height(), 1e-9);
    const Affine toLocal01 =
        Affine::scaling(1 / lw, 1 / lh) * Affine::translation(-localBox.x0, -localBox.y0) * pixelsToLocal;
    for (auto it = behind.rbegin(); it != behind.rend(); ++it)
        paintCoverage(layer.pixels, r, it->area, *it->paint, toLocal01, budget);
    paintCoverage(layer.pixels, r, fillArea, o.style.fill, toLocal01, budget);
    for (const auto& piece : above)
        paintCoverage(layer.pixels, r, piece.area, *piece.paint, toLocal01, budget);
    for (const auto& piece : top)
        paintCoverage(layer.pixels, r, piece.area, *piece.paint, toLocal01, budget);
    applyEffects(layer.pixels, o.style.effects, scale, request.effectThreshold, budget);
    return layer;
}
bool visibleAtSize(const Object& o, double exportSize) {
    if (exportSize <= 0)
        return true;
    if (o.minExportSize > 0 && exportSize < o.minExportSize)
        return false;
    if (o.maxExportSize > 0 && exportSize > o.maxExportSize)
        return false;
    return true;
}
struct Renderer {
    const DocumentData& doc;
    const RenderRequest& request;
    const RenderServices& services;
    const std::shared_ptr<MemoryBudget>& budget;
    ImageSampler& images;
    Rect full;
    void drawObject(Image& target, Rect targetRect, const Object& o, const Affine& parentToPixels) {
        if (!o.visible || !visibleAtSize(o, request.exportSize))
            return;
        for (int copy = 0; copy < o.repeat.count; ++copy)
            drawCopy(target, targetRect, o, parentToPixels * repeatMatrix(o, copy));
    }
    // Composite a layer rendered in output space into `target` (whose origin is targetRect.x/y).
    void place(Image& target, Rect targetRect, const Image& layer, Rect layerRect, double opacity,
               Blend blend) {
        compositeImage(target, layer, layerRect.x - targetRect.x, layerRect.y - targetRect.y, opacity, blend);
    }
    void drawChildren(Image& target, Rect targetRect, const Object& o, const Affine& toPixels,
                      bool skipFirst) {
        bool first = true;
        for (const auto id : doc.children(o.id)) {
            const auto* child = doc.find(id);
            if (!child)
                continue;
            if (skipFirst && first) {
                first = false;
                continue;
            }
            first = false;
            drawObject(target, targetRect, *child, toPixels);
        }
    }
    void drawCopy(Image& target, Rect targetRect, const Object& o, const Affine& parentToPixels) {
        const Affine local = parentToPixels * o.transform.matrix();
        switch (o.kind()) {
        case ObjectKind::Group: {
            const bool isolated = o.opacity < 1 || o.blend != Blend::Normal;
            if (!isolated) {
                drawChildren(target, targetRect, o, local, false);
                return;
            }
            auto layer = Image::make(targetRect.w, targetRect.h, budget);
            drawChildren(layer, targetRect, o, local, false);
            place(target, targetRect, layer, targetRect, o.opacity, o.blend);
            return;
        }
        case ObjectKind::Clip: {
            const auto kids = doc.children(o.id);
            if (kids.empty())
                return;
            const auto* clipShape = doc.find(kids.front());
            auto layer = Image::make(targetRect.w, targetRect.h, budget);
            drawChildren(layer, targetRect, o, local, true);
            Operand clipArea{};
            if (clipShape && clipShape->visible)
                for (int copy = 0; copy < clipShape->repeat.count; ++copy) {
                    auto a = objectArea(doc, *clipShape, local * repeatMatrix(*clipShape, copy),
                                        PixelTolerance, services);
                    clipArea.polygons.insert(clipArea.polygons.end(), a.polygons.begin(), a.polygons.end());
                }
            auto shifted = transformedPolygons(
                normalized(clipArea),
                Affine::translation(static_cast<double>(-targetRect.x), static_cast<double>(-targetRect.y)));
            const auto mask = rasterize(shifted, FillRule::NonZero, targetRect.w, targetRect.h, budget);
            for (std::size_t i = 0; i < layer.pixels.size(); ++i)
                if (mask.values[i] != 255)
                    layer.pixels[i] = scaled(layer.pixels[i], mask.values[i] / 255.);
            place(target, targetRect, layer, targetRect, o.opacity, o.blend);
            return;
        }
        case ObjectKind::Image: {
            const auto* image = o.as<ImageData>();
            const auto found = doc.images.find(image->imageKey);
            if (found == doc.images.end() || !found->second.pixels)
                return;
            auto layer = renderImageObject(found->second.pixels, *image, o.style, local, targetRect, request,
                                           images, budget);
            if (!layer.pixels.empty())
                place(target, targetRect, layer.pixels, layer.bounds, o.opacity, o.blend);
            return;
        }
        default: {
            Operand area;
            Polygons lines;
            if (o.kind() == ObjectKind::Boolean)
                area = containerArea(doc, o, local, PixelTolerance, services);
            else {
                auto path = leafPath(doc, o, services);
                if (!path)
                    return;
                area = {flattenClosed(transformed(*path, local), PixelTolerance), path->rule};
                lines = openLines(*path, local);
            }
            auto layer = renderLeaf(area, lines, o, local, targetRect, request, budget);
            if (!layer.pixels.empty())
                place(target, targetRect, layer.pixels, layer.bounds, o.opacity, o.blend);
        }
        }
    }
};
} // namespace
Operand objectArea(const DocumentData& doc, const Object& o, const Affine& toTarget, double tolerance,
                   const RenderServices& services) {
    const Affine local = toTarget * o.transform.matrix();
    if (o.container())
        return containerArea(doc, o, local, tolerance, services);
    auto path = leafPath(doc, o, services);
    if (!path)
        return {};
    return {flattenClosed(transformed(*path, local), tolerance), path->rule};
}
Polygons objectLines(const DocumentData& doc, const Object& o, const Affine& toTarget,
                     const RenderServices& services) {
    if (o.container() || !o.as<ShapeData>())
        return {};
    const auto path = leafPath(doc, o, services);
    return path ? openLines(*path, toTarget * o.transform.matrix()) : Polygons{};
}
Box objectBounds(const DocumentData& doc, const Object& o, const RenderServices& services) {
    Box b = emptyBox();
    const Affine parent = doc.parentMatrix(o.id);
    for (int copy = 0; copy < o.repeat.count; ++copy) {
        b = united(b, bounds(objectArea(doc, o, parent * repeatMatrix(o, copy), .05, services).polygons));
        b = united(b, bounds(objectLines(doc, o, parent * repeatMatrix(o, copy), services)));
    }
    // A group whose children are all hidden still needs a selectable box: fall back to its position.
    if (b.empty()) {
        const Point p = parent.map(o.transform.position);
        b = {p.x - .5, p.y - .5, p.x + .5, p.y + .5};
    }
    return b;
}
ObjectId hitTest(const DocumentData& doc, Point p, double tolerance, const RenderServices& services,
                 bool includeLocked) {
    for (auto it = doc.objects.rbegin(); it != doc.objects.rend(); ++it) {
        const auto& o = **it;
        if (o.container() && o.kind() != ObjectKind::Boolean)
            continue; // groups are picked through their leaves
        if (o.parent) {
            const auto* parent = doc.find(o.parent);
            // Leaves of a boolean are covered by the boolean result itself.
            bool insideBoolean = false;
            for (auto* a = parent; a; a = a->parent ? doc.find(a->parent) : nullptr)
                insideBoolean = insideBoolean || a->kind() == ObjectKind::Boolean;
            if (insideBoolean)
                continue;
        }
        if (!doc.effectiveVisible(o.id) || (!includeLocked && doc.effectiveLocked(o.id)))
            continue;
        const Affine parent = doc.parentMatrix(o.id);
        for (int copy = 0; copy < o.repeat.count; ++copy) {
            const auto area = objectArea(doc, o, parent * repeatMatrix(o, copy), .05, services);
            double grow = 0;
            for (const auto& outline : o.style.outlines)
                if (outline.enabled && outline.position == OutlinePosition::Outside)
                    grow += outline.width;
            const auto lines = objectLines(doc, o, parent * repeatMatrix(o, copy), services);
            double half = 0;
            for (const auto& outline : o.style.outlines)
                half = std::max(half, outline.enabled ? outline.width / 2 : 0.);
            if (inside(area.polygons, area.rule, p) ||
                distanceToOutline(area.polygons, p) <= tolerance + grow ||
                (!lines.empty() && distanceToOutline(lines, p, false) <= tolerance + half))
                return o.id;
        }
    }
    return {};
}
ObjectId selectableAncestor(const DocumentData& doc, ObjectId hit, ObjectId scope) {
    const auto* o = doc.find(hit);
    while (o && o->parent && o->parent != scope) {
        o = doc.find(o->parent);
    }
    return o ? o->id : ObjectId{};
}
Affine fitTransform(Box source, Coord width, Coord height, double padding, bool cover) {
    if (source.empty() || width <= 0 || height <= 0)
        return {};
    const double pad = std::clamp(padding, 0., 45.) / 100.;
    const double aw = static_cast<double>(width) * (1 - 2 * pad),
                 ah = static_cast<double>(height) * (1 - 2 * pad);
    const double sx = aw / source.width(), sy = ah / source.height();
    const double s = cover ? std::max(sx, sy) : std::min(sx, sy);
    const Point c = source.center();
    return Affine::translation(static_cast<double>(width) / 2, static_cast<double>(height) / 2) *
           Affine::scaling(s, s) * Affine::translation(-c.x, -c.y);
}
Pixel shade(const Paint& paint, Point local) {
    if (paint.kind == FillKind::None)
        return {};
    if (paint.kind == FillKind::Solid || paint.stops.empty())
        return paint.color.premultiplied();
    double t;
    if (paint.kind == FillKind::Linear) {
        const double r = paint.angle * std::numbers::pi / 180;
        const Point dir{std::cos(r), std::sin(r)};
        // Project onto the direction across the unit box so t spans exactly 0..1 between opposite corners.
        const double extent = std::abs(dir.x) + std::abs(dir.y);
        t = (dot(local - Point{.5, .5}, dir) / extent) + .5;
    } else
        t = length(local - paint.center) / std::max(paint.radius, 1e-6);
    t = std::clamp(t, 0., 1.);
    const auto& stops = paint.stops;
    if (t <= stops.front().offset)
        return stops.front().color.premultiplied();
    if (t >= stops.back().offset)
        return stops.back().color.premultiplied();
    for (std::size_t i = 1; i < stops.size(); ++i)
        if (t <= stops[i].offset) {
            const auto& a = stops[i - 1];
            const auto& b = stops[i];
            const double span = b.offset - a.offset;
            const double k = span > 0 ? (t - a.offset) / span : 1;
            // Interpolate premultiplied so transparent stops do not darken.
            const Pixel pa = a.color.premultiplied(), pb = b.color.premultiplied();
            const auto mix = [k](std::uint8_t u, std::uint8_t v) {
                return static_cast<std::uint8_t>(std::lround(u + (v - u) * k));
            };
            return {mix(pa.r, pb.r), mix(pa.g, pb.g), mix(pa.b, pb.b), mix(pa.a, pb.a)};
        }
    return stops.back().color.premultiplied();
}
Image render(const DocumentData& doc, const RenderRequest& request, const RenderServices& services,
             const std::shared_ptr<MemoryBudget>& budget) {
    auto out = Image::make(request.width, request.height, budget);
    if (out.empty())
        return out;
    if (request.background) {
        const Pixel bg = request.backgroundColor.premultiplied();
        std::fill(out.pixels.begin(), out.pixels.end(), bg);
    }
    ImageSampler fallback(budget);
    Renderer renderer{doc,
                      request,
                      services,
                      budget,
                      services.images ? *services.images : fallback,
                      {0, 0, request.width, request.height}};
    const auto selected = [&](const Object& o) {
        if (request.only.empty())
            return !o.parent;
        return std::find(request.only.begin(), request.only.end(), o.id) != request.only.end();
    };
    for (const auto& ref : doc.objects) {
        const auto& o = *ref;
        if (!selected(o))
            continue;
        // Selected subtrees render with their real ancestor transforms but ignore ancestor styling.
        if (!request.only.empty() && !doc.effectiveVisible(o.id))
            continue;
        renderer.drawObject(out, renderer.full, o, request.toPixels * doc.parentMatrix(o.id));
    }
    return out;
}
} // namespace kantan

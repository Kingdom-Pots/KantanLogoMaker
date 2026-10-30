#include "document/Document.h"
#include <set>
#include <unordered_map>

namespace kantan {
namespace {
bool finiteRange(double v, double lo, double hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
}
Result validatePaint(const Paint& p) {
    if (p.kind == FillKind::Linear || p.kind == FillKind::Radial) {
        if (p.stops.empty() || p.stops.size() > 32)
            return {Status::InvalidData, "Gradients need 1 to 32 stops"};
        for (const auto& s : p.stops)
            if (!finiteRange(s.offset, 0, 1))
                return {Status::InvalidData, "Gradient stop offsets must be between 0 and 1"};
    }
    if (!std::isfinite(p.angle) || !finite(p.center) || !finiteRange(p.radius, 0, 100))
        return {Status::InvalidData, "Gradient geometry is invalid"};
    return {};
}
} // namespace
Result validateStyle(const Style& s) {
    if (auto r = validatePaint(s.fill); !r)
        return r;
    if (s.outlines.size() > MaxOutlines)
        return {Status::InvalidData, "At most three outlines are supported"};
    for (const auto& o : s.outlines) {
        if (!finiteRange(o.width, 0, 4096) || !finiteRange(o.miterLimit, 1, 100))
            return {Status::InvalidData, "Outline width or miter limit is out of range"};
        if (auto r = validatePaint(o.paint); !r)
            return r;
    }
    const auto& e = s.effects;
    for (const auto* sh : {&e.dropShadow, &e.innerShadow})
        if (!std::isfinite(sh->angle) || !finiteRange(sh->distance, 0, 4096) ||
            !finiteRange(sh->blur, 0, 1024) || !finiteRange(sh->spread, 0, 1) ||
            !finiteRange(sh->opacity, 0, 1))
            return {Status::InvalidData, "Shadow settings are out of range"};
    for (const auto* g : {&e.outerGlow, &e.innerGlow})
        if (!finiteRange(g->size, 0, 1024) || !finiteRange(g->spread, 0, 1) || !finiteRange(g->opacity, 0, 1))
            return {Status::InvalidData, "Glow settings are out of range"};
    const auto& b = e.bevel;
    if (!finiteRange(b.size, 0, 1024) || !finiteRange(b.depth, 0, 10) || !finiteRange(b.softness, 0, 1024) ||
        !std::isfinite(b.lightAngle) || !finiteRange(b.altitude, 0, 90) ||
        !finiteRange(b.highlightOpacity, 0, 1) || !finiteRange(b.shadowOpacity, 0, 1))
        return {Status::InvalidData, "Bevel settings are out of range"};
    return {};
}
const Object* DocumentData::find(ObjectId id) const {
    for (const auto& o : objects)
        if (o->id == id)
            return o.get();
    return nullptr;
}
std::size_t DocumentData::indexOf(ObjectId id) const {
    for (std::size_t i = 0; i < objects.size(); ++i)
        if (objects[i]->id == id)
            return i;
    return objects.size();
}
std::vector<ObjectId> DocumentData::children(ObjectId parent) const {
    std::vector<ObjectId> out;
    for (const auto& o : objects)
        if (o->parent == parent)
            out.push_back(o->id);
    return out;
}
std::size_t DocumentData::subtreeEnd(std::size_t index) const {
    if (index >= objects.size())
        return objects.size();
    const auto root = objects[index]->id;
    std::size_t end = index + 1;
    while (end < objects.size() && isAncestor(root, objects[end]->id))
        ++end;
    return end;
}
bool DocumentData::isAncestor(ObjectId ancestor, ObjectId id) const {
    const auto* o = find(id);
    for (std::size_t guard = 0; o && o->parent && guard < objects.size(); ++guard) {
        if (o->parent == ancestor)
            return true;
        o = find(o->parent);
    }
    return false;
}
bool DocumentData::effectiveVisible(ObjectId id) const {
    for (const auto* o = find(id); o; o = o->parent ? find(o->parent) : nullptr)
        if (!o->visible)
            return false;
    return true;
}
bool DocumentData::effectiveLocked(ObjectId id) const {
    for (const auto* o = find(id); o; o = o->parent ? find(o->parent) : nullptr)
        if (o->locked)
            return true;
    return false;
}
Affine DocumentData::parentMatrix(ObjectId id) const {
    const auto* o = find(id);
    return o && o->parent ? worldMatrix(o->parent) : Affine{};
}
Affine DocumentData::worldMatrix(ObjectId id) const {
    const auto* o = find(id);
    return o ? parentMatrix(id) * o->transform.matrix() : Affine{};
}
void normalizeOrder(DocumentData& doc) {
    std::unordered_map<std::uint64_t, std::vector<ObjectRef>> byParent;
    for (const auto& o : doc.objects)
        byParent[o->parent.value].push_back(o);
    std::vector<ObjectRef> ordered;
    ordered.reserve(doc.objects.size());
    const auto visit = [&](auto&& self, std::uint64_t parent) -> void {
        const auto found = byParent.find(parent);
        if (found == byParent.end())
            return;
        for (const auto& o : found->second) {
            ordered.push_back(o);
            self(self, o->id.value);
        }
    };
    visit(visit, 0);
    doc.objects = std::move(ordered);
}
Result validateDocument(const DocumentData& doc) {
    if (!finiteRange(doc.artboard.width, 1, MaxArtboard) || !finiteRange(doc.artboard.height, 1, MaxArtboard))
        return {Status::InvalidData, "Artboard size must be between 1 and 16384 units"};
    if (doc.objects.size() > MaxObjects)
        return {Status::ResourceLimit, "Too many objects"};
    if (doc.palette.size() > MaxPalette)
        return {Status::InvalidData, "Palettes hold at most 64 colours"};
    if (doc.artboard.guidesX.size() > 256 || doc.artboard.guidesY.size() > 256)
        return {Status::InvalidData, "Too many guides"};
    for (const auto g : doc.artboard.guidesX)
        if (!std::isfinite(g))
            return {Status::InvalidData, "Guide position is not a number"};
    for (const auto g : doc.artboard.guidesY)
        if (!std::isfinite(g))
            return {Status::InvalidData, "Guide position is not a number"};
    std::set<std::uint64_t> ids;
    for (const auto& o : doc.objects) {
        if (!o || !o->id)
            return {Status::InvalidData, "Object has no identity"};
        if (!ids.insert(o->id.value).second)
            return {Status::InvalidData, "Duplicate object ID"};
    }
    for (const auto& o : doc.objects) {
        if (o->parent) {
            const auto* p = doc.find(o->parent);
            if (!p)
                return {Status::InvalidData, "Object parent is missing"};
            if (!p->container())
                return {Status::InvalidData, "Object parent is not a group"};
        }
        // Cycle check: walking up must terminate within the object count.
        const Object* walk = o.get();
        std::size_t steps = 0;
        while (walk && walk->parent) {
            if (++steps > doc.objects.size())
                return {Status::InvalidData, "Object hierarchy contains a cycle"};
            walk = doc.find(walk->parent);
        }
        const auto& t = o->transform;
        if (!finite(t.position) || !std::isfinite(t.rotation) || !std::isfinite(t.scaleX) ||
            !std::isfinite(t.scaleY) || std::abs(t.scaleX) < 1e-6 || std::abs(t.scaleY) < 1e-6 ||
            std::abs(t.scaleX) > 1e5 || std::abs(t.scaleY) > 1e5 ||
            std::abs(t.position.x) > CoordinateLimit || std::abs(t.position.y) > CoordinateLimit)
            return {Status::InvalidData, "Object transform is invalid"};
        if (!finiteRange(o->opacity, 0, 1) || !finiteRange(o->minExportSize, 0, 1e6) ||
            !finiteRange(o->maxExportSize, 0, 1e6))
            return {Status::InvalidData, "Object opacity or size visibility is invalid"};
        if (o->repeat.count < 1 || o->repeat.count > 360 || !finite(o->repeat.pivot))
            return {Status::InvalidData, "Radial repeat must have 1 to 360 copies"};
        if (auto r = validateStyle(o->style); !r)
            return r;
        if (const auto* s = o->as<ShapeData>()) {
            if (auto r = validateShape(s->geometry, s->width, s->height); !r)
                return r;
        } else if (const auto* text = o->as<TextData>()) {
            if (text->text.size() > 4096 || !finiteRange(text->size, .5, 4096) || text->weight < 1 ||
                text->weight > 1000 || !finiteRange(text->tracking, -1000, 5000) ||
                !finiteRange(text->lineHeight, .1, 10) || !finiteRange(text->arc, -1, 1))
                return {Status::InvalidData, "Text settings are out of range"};
            if (!text->font.fileKey.empty() && !doc.fonts.contains(text->font.fileKey))
                return {Status::InvalidData, "Text refers to a missing embedded font"};
        } else if (const auto* image = o->as<ImageData>()) {
            const auto found = doc.images.find(image->imageKey);
            if (found == doc.images.end() || !found->second.pixels)
                return {Status::InvalidData, "Image object refers to missing pixels"};
            if (!finiteRange(image->width, 0, CoordinateLimit) ||
                !finiteRange(image->height, 0, CoordinateLimit))
                return {Status::InvalidData, "Image size is out of range"};
        }
    }
    // Depth-first ordering invariant: every object's parent appears before it, and subtrees are contiguous.
    for (std::size_t i = 0; i < doc.objects.size(); ++i) {
        const auto parent = doc.objects[i]->parent;
        if (!parent)
            continue;
        const auto p = doc.indexOf(parent);
        if (p >= i || doc.subtreeEnd(p) <= i)
            return {Status::InvalidData, "Object order is not depth-first"};
    }
    return {};
}
const char* kindName(ObjectKind kind) {
    static const char* names[] = {"Shape", "Text", "Image", "Boolean", "Group", "Clip group"};
    return names[static_cast<int>(kind)];
}
} // namespace kantan

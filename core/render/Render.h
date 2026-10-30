#pragma once
#include "boolean/Boolean.h"
#include "document/TextShaper.h"
#include "raster/Image.h"

namespace kantan {
class ImageSampler; // mip-chain cache, defined in render/ImageSampling.h
struct RenderServices {
    const TextShaper* text{};
    ImageSampler* images{}; // optional; a temporary sampler is used when absent
};
// Closed filled area of an object (non-zero rule) in the space given by `toTarget` (which maps the
// object's parent space). Containers: Boolean -> live result, Group -> union of children, Clip -> clip shape.
Operand objectArea(const DocumentData& doc, const Object& object, const Affine& toTarget, double tolerance,
                   const RenderServices& services);
// Open subpaths of a leaf path (pen lines), flattened in the target space. They take outlines, never fills.
Polygons objectLines(const DocumentData& doc, const Object& object, const Affine& toTarget,
                     const RenderServices& services);
// Union of the areas of a set of objects in artboard space (selection bounds, hit testing).
Box objectBounds(const DocumentData& doc, const Object& object, const RenderServices& services);
// Topmost visible, unlocked object whose area (or outline band) contains the artboard point.
ObjectId hitTest(const DocumentData& doc, Point artboardPoint, double tolerance,
                 const RenderServices& services, bool includeLocked = false);
// Pick the top-level ancestor (child of root or of `scope`) for a hit, as group selection does.
ObjectId selectableAncestor(const DocumentData& doc, ObjectId hit, ObjectId scope = {});

struct RenderRequest {
    Affine toPixels; // artboard units -> output pixels
    Coord width{}, height{};
    double exportSize{}; // output size used for per-object size visibility; 0 ignores visibility
    bool background{};   // paint the artboard background colour
    Color backgroundColor{255, 255, 255, 255};
    std::vector<ObjectId> only;  // render just these subtrees (selection export, thumbnails); empty = all
    double effectThreshold{.75}; // effects whose size scales below this many output pixels are skipped
};
// The one renderer (D-L2) for canvas, size strip, thumbnails and export.
Image render(const DocumentData& doc, const RenderRequest& request, const RenderServices& services,
             const std::shared_ptr<MemoryBudget>& budget);
// Fit the artboard (or a box) into an output, keeping aspect; padding in percent of the output size.
Affine fitTransform(Box source, Coord width, Coord height, double paddingPercent, bool cover = false);
// Premultiplied colour of a paint at a normalized local position (0..1 across the object bounds).
Pixel shade(const Paint& paint, Point local);
} // namespace kantan

#pragma once
#include "document/Document.h"
#include <map>
#include <mutex>

namespace kantan {
// Bounded cache of box-filtered mip chains for embedded originals. Thread-safe; derived data only.
class ImageSampler {
  public:
    explicit ImageSampler(std::shared_ptr<MemoryBudget> budget, std::size_t maxEntries = 16)
        : budget_(std::move(budget)), maxEntries_(maxEntries) {}
    using Chain = std::vector<ImageRef>;
    std::shared_ptr<const Chain> chain(const ImageRef& image);
    // Premultiplied sample at source pixel coordinates (u, v) for a footprint of `footprint` source
    // pixels per output pixel. Trilinear between mip levels, bilinear within a level.
    static Pixel sample(const Chain& chain, double u, double v, double footprint);

  private:
    std::shared_ptr<MemoryBudget> budget_;
    std::size_t maxEntries_;
    std::mutex mutex_;
    struct Entry {
        ImageRef source; // keeps the key alive so the pointer cannot be reused
        std::shared_ptr<const Chain> chain;
        std::uint64_t used{};
    };
    std::map<const Image*, Entry> entries_;
    std::uint64_t clock_{};
};
struct ImageLayer {
    Rect bounds;
    Image pixels;
};
// Apply non-destructive adjustments to a straight-alpha colour (exposed for tests).
Pixel adjustPixel(Pixel premultiplied, const ImageAdjust& adjust);
struct RenderRequest;
ImageLayer renderImageObject(const ImageRef& source, const ImageData& image, const Style& style,
                             const Affine& localToPixels, Rect clip, const RenderRequest& request,
                             ImageSampler& sampler, const std::shared_ptr<MemoryBudget>& budget);
} // namespace kantan

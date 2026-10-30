#pragma once
#include "io/FontLibrary.h"
#include "io/TextShaper.h"
#include "io/WriteCoordinator.h"
#include "render/ImageSampling.h"
#include "render/Render.h"

namespace kantan::app {
// Host-owned services shared by every document window: memory budget, fonts/shaping, image mip cache,
// and file write ordering. No document state lives here.
struct AppServices {
    std::shared_ptr<MemoryBudget> budget;
    std::shared_ptr<io::FontLibrary> fonts;
    std::shared_ptr<io::QtTextShaper> shaper;
    std::shared_ptr<ImageSampler> images;
    std::shared_ptr<io::WriteCoordinator> storage;
    RenderServices render() const {
        return {shaper.get(), images.get()};
    }
    // `fontFolder` empty keeps imported fonts in memory only (tests).
    static std::shared_ptr<AppServices> create(const QString& fontFolder = {}) {
        auto s = std::make_shared<AppServices>();
        s->budget = std::make_shared<MemoryBudget>();
        s->fonts = std::make_shared<io::FontLibrary>(fontFolder);
        s->shaper = std::make_shared<io::QtTextShaper>(s->fonts);
        s->images = std::make_shared<ImageSampler>(s->budget);
        s->storage = std::make_shared<io::WriteCoordinator>();
        return s;
    }
};
} // namespace kantan::app

#pragma once
#include "render/Render.h"
#include <QString>

namespace kantan::io {
struct LoadResult {
    Result result;
    Snapshot document;
    std::vector<std::string> warnings; // e.g. linked fonts that are not installed
};
// `.kantanlogo`: ZIP with manifest.json (format "kantanlogo-experimental", schema 1), fonts/, images/ and
// thumbnail.png. Written to a sibling temporary file and atomically replaced.
Result saveProject(const QString& path, Snapshot document, const RenderServices& services,
                   std::shared_ptr<MemoryBudget> budget);
LoadResult loadProject(const QString& path, std::shared_ptr<MemoryBudget> budget);
inline constexpr int ProjectSchema = 1;
} // namespace kantan::io

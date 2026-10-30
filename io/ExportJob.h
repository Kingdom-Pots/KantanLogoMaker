#pragma once
#include "io/WriteCoordinator.h"
#include "render/Render.h"
#include <QString>

namespace kantan::io {
struct ExportFile {
    QString path;
    bool ico{};
    std::vector<ExportSize> sizes; // one for PNG, all entries for ICO
};
struct ExportPlan {
    Result result;
    std::vector<ExportFile> files;
};
// Resolve settings into concrete files. PNG: every ticked size plus custom W x H entries. ICO: one file with
// every ticked size; sizes above 256 and non-square customs are excluded (the format cannot hold them).
ExportPlan planExport(const ExportSettings& settings, const QString& folder, const QString& baseName);
// `{name}`, `{size}` (e.g. 64 or 128x64), `{w}`, `{h}`; returns an empty string for an invalid pattern.
QString expandName(const std::string& pattern, const QString& baseName, ExportSize size);
// Like expandName, for a relative path of up to four segments separated by '/'.
QString expandPath(const std::string& pattern, const QString& baseName, ExportSize size);
// Render one output independently from the vector snapshot (never downscaled from a larger export).
Image renderExport(const DocumentData& document, ExportSize size, const ExportSettings& settings,
                   const std::vector<ObjectId>& selection, const RenderServices& services,
                   const std::shared_ptr<MemoryBudget>& budget);
struct ExportOutcome {
    QString path;
    Result result;
};
// Write every file atomically through the coordinator. Not a transaction across files: each file
// reports its own result, and a failed file leaves its previous version in place.
std::vector<ExportOutcome> runExport(const DocumentData& document, const ExportPlan& plan,
                                     const ExportSettings& settings, const std::vector<ObjectId>& selection,
                                     const RenderServices& services, WriteCoordinator& storage,
                                     const std::shared_ptr<MemoryBudget>& budget);
} // namespace kantan::io

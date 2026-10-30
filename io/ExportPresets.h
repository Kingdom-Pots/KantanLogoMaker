#pragma once
#include "document/Document.h"
#include <QString>
#include <optional>
#include <vector>

namespace kantan::io {
// Platform export presets. Applying one replaces sizes/outputs/naming but keeps padding, fit, background
// choices that the preset does not care about, and the output folder.
struct ExportPreset {
    QString name, description;
    ExportSettings apply(const ExportSettings& current) const;
    // Fields the preset sets.
    bool png{}, ico{};
    std::vector<int> pngSizes, icoSizes;
    std::vector<NamedOutput> outputs;
    std::string naming{"{name}_{size}"}, icoName{"{name}"};
    std::optional<bool> transparent; // iOS app icons must be opaque
};
const std::vector<ExportPreset>& exportPresets();
} // namespace kantan::io

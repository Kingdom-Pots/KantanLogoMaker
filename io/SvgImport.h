#pragma once
#include "document/Document.h"
#include <QByteArray>
#include <QString>

namespace kantan::io {
struct SvgImport {
    Result result;
    std::vector<Object> objects;          // depth-first; a group root when there is more than one shape
    std::vector<std::string> unsupported; // features in the file that were skipped
};
// Paths, fills (solid/gradient) and strokes become editable shapes through nanosvg. Text, images, filters,
// masks and clip paths are not supported and are reported. Geometry is centred so the result can be placed.
SvgImport importSvg(const QByteArray& bytes, const QString& name, double targetSize = 0);
SvgImport importSvgFile(const QString& path, double targetSize = 0);
} // namespace kantan::io

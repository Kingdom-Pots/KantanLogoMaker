#pragma once
#include "document/Document.h"
#include "render/Render.h"
#include <QByteArray>
#include <QString>

namespace kantan::io {
struct SvgExportOptions {
    bool selectionOnly{false};
    bool transparent{true};
    Color background{255, 255, 255, 255};
};

// Generates valid, self-contained SVG XML for the document (or selected objects).
QByteArray exportSvg(const DocumentData& doc, const SvgExportOptions& options = {},
                     const std::vector<ObjectId>& selection = {}, const RenderServices& services = {});

// Writes SVG to disk atomically.
Result exportSvgFile(const QString& path, const DocumentData& doc, const SvgExportOptions& options = {},
                     const std::vector<ObjectId>& selection = {}, const RenderServices& services = {});
} // namespace kantan::io

#pragma once
#include "document/Document.h"
#include <QString>
#include <vector>

namespace kantan::app {
struct ShapePreset {
    QString name;
    ShapeGeometry geometry;
    double width{200}, height{200};
};
// Parametric presets in the Shape tool menu and the Presets › Shapes tab.
const std::vector<ShapePreset>& shapePresets();
struct StylePreset {
    QString name;
    Style style;
};
// Fill + outlines + effects bundles applied with one click.
const std::vector<StylePreset>& stylePresets();
struct StarterTemplate {
    QString name;
    DocumentData document; // ids already assigned; opened as a new untitled document
};
std::vector<StarterTemplate> starterTemplates(const QString& defaultFamily);
// Default style for newly drawn shapes / text.
Style defaultShapeStyle();
Style defaultTextStyle();
} // namespace kantan::app

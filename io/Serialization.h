#pragma once
#include "document/Document.h"
#include <QJsonArray>
#include <QJsonObject>

namespace kantan::io {
// Versioned data-transfer form shared by project manifests, presets and the clipboard. Readers validate
// every field and throw std::invalid_argument on malformed data; callers convert that to a Result.
QJsonObject styleToJson(const Style& style);
Style styleFromJson(const QJsonObject& json);
QJsonObject objectToJson(const Object& object);
Object objectFromJson(const QJsonObject& json);
QJsonObject artboardToJson(const Artboard& artboard);
Artboard artboardFromJson(const QJsonObject& json);
QJsonArray paletteToJson(const std::vector<PaletteColor>& palette);
std::vector<PaletteColor> paletteFromJson(const QJsonArray& json);
QJsonObject exportToJson(const ExportSettings& settings);
ExportSettings exportFromJson(const QJsonObject& json);
// A self-contained fragment of objects (depth-first, internal parent links) for presets and copy/paste.
struct Fragment {
    std::vector<Object> objects;
    std::map<std::string, EmbeddedImage> images;
};
QJsonObject fragmentToJson(const DocumentData& doc, const std::vector<ObjectId>& roots);
// Image payloads are not part of the JSON fragment; `images` supplies any that the objects reference.
Fragment fragmentFromJson(const QJsonObject& json, const std::map<std::string, EmbeddedImage>& images = {});
QString colorToString(Color color);
Color colorFromString(const QString& text);
} // namespace kantan::io

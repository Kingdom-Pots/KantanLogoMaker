#pragma once
#include "raster/Image.h"
#include <QByteArray>
#include <vector>

namespace kantan::io {
// Windows ICO container with PNG-compressed entries (supported since Windows Vista). Entries are square,
// 1..256 px; a 256 px entry is recorded with the conventional zero width/height bytes.
QByteArray icoBytes(const std::vector<Image>& images);
struct IcoEntry {
    int width{}, height{};
    QByteArray png;
};
// Strict reader used by tests and to verify an export before it replaces the destination.
std::vector<IcoEntry> readIco(const QByteArray& bytes);
} // namespace kantan::io

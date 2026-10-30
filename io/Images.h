#pragma once
#include "document/Document.h"
#include <QByteArray>
#include <QImage>
#include <QString>

namespace kantan::io {
// Originals larger than this are refused at import (decoded pixels plus mip chain must fit the budget).
inline constexpr std::size_t ImportPixelLimit = 40U * 1024U * 1024U;
inline constexpr qint64 ImportFileLimit = 256LL * 1024 * 1024;
struct DecodedImage {
    Result result;
    EmbeddedImage image;
};
// Decode PNG/JPEG/BMP/GIF (first frame)/ICO bytes into premultiplied RGBA8 sRGB, keeping the bytes.
DecodedImage decodeImage(QByteArray bytes, const QString& sourceName,
                         const std::shared_ptr<MemoryBudget>& budget);
DecodedImage importImageFile(const QString& path, const std::shared_ptr<MemoryBudget>& budget);
// Wrap a premultiplied QImage (e.g. clipboard) as an embedded PNG original.
DecodedImage imageFromQImage(const QImage& image, const QString& sourceName,
                             const std::shared_ptr<MemoryBudget>& budget);
QImage toQImage(const Image& source);
// Straight-alpha PNG bytes of a premultiplied image.
QByteArray encodePng(const Image& image);
// Atomic write of bytes (sibling temporary file, then replace).
Result writeFile(const QString& path, const QByteArray& bytes);
Bytes toBytes(const QByteArray& data);
QByteArray fromBytes(const Bytes& data);
} // namespace kantan::io

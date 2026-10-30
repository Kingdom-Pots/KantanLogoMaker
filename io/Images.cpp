#include "io/Images.h"
#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <cstring>

namespace kantan::io {
Bytes toBytes(const QByteArray& data) {
    return std::make_shared<const std::vector<std::uint8_t>>(
        reinterpret_cast<const std::uint8_t*>(data.constData()),
        reinterpret_cast<const std::uint8_t*>(data.constData()) + data.size());
}
QByteArray fromBytes(const Bytes& data) {
    if (!data)
        return {};
    return QByteArray(reinterpret_cast<const char*>(data->data()), static_cast<qsizetype>(data->size()));
}
QImage toQImage(const Image& src) {
    QImage image(static_cast<int>(src.width), static_cast<int>(src.height),
                 QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull() && !src.empty())
        throw std::bad_alloc();
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(image.scanLine(y),
                    src.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(src.width),
                    static_cast<std::size_t>(src.width) * sizeof(Pixel));
    image.setColorSpace(QColorSpace::SRgb);
    return image;
}
QByteArray encodePng(const Image& image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    // Straight alpha is the PNG convention; Qt's conversion divides by alpha with rounding.
    const auto straight = toQImage(image).convertToFormat(QImage::Format_RGBA8888);
    QImageWriter writer(&buffer, "png");
    if (!writer.write(straight))
        throw std::runtime_error(writer.errorString().toStdString());
    return bytes;
}
DecodedImage decodeImage(QByteArray bytes, const QString& sourceName,
                         const std::shared_ptr<MemoryBudget>& budget) {
    try {
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        const QByteArray format = reader.format().toLower();
        static const QList<QByteArray> supported{"png", "jpeg", "jpg", "bmp", "gif", "ico"};
        if (!supported.contains(format))
            return {{Status::Unsupported, "Unsupported image format; use PNG, JPEG, BMP, GIF or ICO"}, {}};
        const auto size = reader.size();
        if (!size.isValid())
            return {{Status::InvalidData, reader.errorString().toStdString()}, {}};
        const auto count = checkedArea(size.width(), size.height(), ImportPixelLimit);
        Reservation scratch(budget, count * sizeof(Pixel) * 2);
        auto image = reader.read();
        if (image.isNull())
            return {{Status::InvalidData, reader.errorString().toStdString()}, {}};
        if (image.colorSpace().isValid() && image.colorSpace() != QColorSpace::SRgb)
            image.convertToColorSpace(QColorSpace::SRgb);
        image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        auto pixels = Image::make(image.width(), image.height(), budget);
        for (int y = 0; y < image.height(); ++y)
            std::memcpy(pixels.pixels.data() +
                            static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width()),
                        image.constScanLine(y), static_cast<std::size_t>(image.width()) * sizeof(Pixel));
        EmbeddedImage out;
        out.pixels = std::make_shared<const Image>(std::move(pixels));
        out.encoded = toBytes(bytes);
        out.format = format == "jpeg" ? "jpg" : format.toStdString();
        out.sourceName = sourceName.toStdString();
        return {{}, std::move(out)};
    } catch (const std::exception& e) {
        return {{Status::ResourceLimit, e.what()}, {}};
    }
}
DecodedImage importImageFile(const QString& path, const std::shared_ptr<MemoryBudget>& budget) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{Status::InvalidData, file.errorString().toStdString()}, {}};
    if (file.size() > ImportFileLimit)
        return {{Status::ResourceLimit, "Image file is too large"}, {}};
    return decodeImage(file.readAll(), QFileInfo(path).fileName(), budget);
}
DecodedImage imageFromQImage(const QImage& source, const QString& sourceName,
                             const std::shared_ptr<MemoryBudget>& budget) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (source.isNull() || !source.save(&buffer, "png"))
        return {{Status::InvalidData, "Could not read the image"}, {}};
    // Decode the bytes we keep, so pixels are always exactly what a reopened project will show.
    return decodeImage(bytes, sourceName, budget);
}
Result writeFile(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return {Status::InvalidData, file.errorString().toStdString()};
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return {Status::InvalidData, file.errorString().toStdString()};
    }
    if (!file.commit())
        return {Status::InvalidData, file.errorString().toStdString()};
    return {};
}
} // namespace kantan::io

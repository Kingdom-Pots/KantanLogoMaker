#include "io/IcoWriter.h"
#include "io/Images.h"

namespace kantan::io {
namespace {
void put16(QByteArray& out, int v) {
    out.append(static_cast<char>(v & 0xff));
    out.append(static_cast<char>((v >> 8) & 0xff));
}
void put32(QByteArray& out, quint32 v) {
    for (int i = 0; i < 4; ++i)
        out.append(static_cast<char>((v >> (8 * i)) & 0xff));
}
quint32 get(const QByteArray& b, qsizetype at, int bytes) {
    if (at < 0 || at + bytes > b.size())
        throw std::invalid_argument("Truncated ICO");
    quint32 v = 0;
    for (int i = 0; i < bytes; ++i)
        v |= static_cast<quint32>(static_cast<unsigned char>(b[at + i])) << (8 * i);
    return v;
}
} // namespace
QByteArray icoBytes(const std::vector<Image>& images) {
    if (images.empty() || images.size() > 64)
        throw std::invalid_argument("ICO needs 1 to 64 images");
    std::vector<QByteArray> pngs;
    for (const auto& image : images) {
        if (image.width != image.height || image.width < 1 || image.width > 256)
            throw std::invalid_argument("ICO entries must be square and at most 256 px");
        pngs.push_back(encodePng(image));
    }
    QByteArray out;
    put16(out, 0); // reserved
    put16(out, 1); // type: icon
    put16(out, static_cast<int>(images.size()));
    quint32 offset = 6 + 16 * static_cast<quint32>(images.size());
    for (std::size_t i = 0; i < images.size(); ++i) {
        const int size = static_cast<int>(images[i].width);
        out.append(static_cast<char>(size == 256 ? 0 : size)); // width
        out.append(static_cast<char>(size == 256 ? 0 : size)); // height
        out.append('\0');                                      // palette colours
        out.append('\0');                                      // reserved
        put16(out, 1);                                         // colour planes
        put16(out, 32);                                        // bits per pixel
        put32(out, static_cast<quint32>(pngs[i].size()));
        put32(out, offset);
        offset += static_cast<quint32>(pngs[i].size());
    }
    for (const auto& png : pngs)
        out.append(png);
    return out;
}
std::vector<IcoEntry> readIco(const QByteArray& b) {
    if (get(b, 0, 2) != 0 || get(b, 2, 2) != 1)
        throw std::invalid_argument("Not an icon file");
    const auto count = get(b, 4, 2);
    if (count == 0 || count > 256)
        throw std::invalid_argument("Invalid ICO entry count");
    std::vector<IcoEntry> out;
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype at = 6 + 16 * static_cast<qsizetype>(i);
        IcoEntry e;
        e.width = static_cast<int>(get(b, at, 1));
        e.height = static_cast<int>(get(b, at + 1, 1));
        e.width = e.width ? e.width : 256;
        e.height = e.height ? e.height : 256;
        const auto length = get(b, at + 8, 4), offset = get(b, at + 12, 4);
        if (static_cast<qint64>(offset) + length > b.size())
            throw std::invalid_argument("ICO entry outside the file");
        e.png = b.mid(static_cast<qsizetype>(offset), static_cast<qsizetype>(length));
        if (!e.png.startsWith("\x89PNG\r\n\x1a\n"))
            throw std::invalid_argument("ICO entry is not PNG");
        out.push_back(std::move(e));
    }
    return out;
}
} // namespace kantan::io

#include "io/FontLibrary.h"
#include "io/Images.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>

namespace kantan::io {
FontLibrary::FontLibrary(QString folder) : folder_(std::move(folder)) {
    if (folder_.isEmpty())
        return;
    QDir dir(folder_);
    for (const auto& info : dir.entryInfoList({"*.ttf", "*.otf", "*.ttc"}, QDir::Files)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64LL * 1024 * 1024)
            continue;
        importData(file.readAll(), info.fileName());
    }
}
QString FontLibrary::keyFor(const QByteArray& data) {
    return QStringLiteral("font-") +
           QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().left(16);
}
QString FontLibrary::fallbackFamily() {
    const auto system = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    if (QFontDatabase::hasFamily(system))
        return system;
    const auto application = QGuiApplication::font().family();
    if (QFontDatabase::hasFamily(application))
        return application;
    const auto available = QFontDatabase::families();
    return available.isEmpty() ? system : available.front();
}
ImportedFont FontLibrary::importFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{Status::InvalidData, file.errorString().toStdString()}, {}, {}};
    if (file.size() > 64LL * 1024 * 1024)
        return {{Status::ResourceLimit, "Font file is too large"}, {}, {}};
    auto imported = importData(file.readAll(), QFileInfo(path).fileName());
    if (imported.result && !folder_.isEmpty()) {
        QDir().mkpath(folder_);
        const auto ext = QFileInfo(path).suffix().toLower();
        const auto target = QDir(folder_).filePath(QString::fromStdString(imported.key) + "." + ext);
        if (!QFileInfo::exists(target))
            writeFile(target, fromBytes(imported.font.data));
    }
    return imported;
}
ImportedFont FontLibrary::importData(const QByteArray& data, const QString& fileName) {
    const auto ext = QFileInfo(fileName).suffix().toLower();
    if (ext != "ttf" && ext != "otf" && ext != "ttc")
        return {{Status::Unsupported, "Fonts must be .ttf, .otf or .ttc files"}, {}, {}};
    const auto key = keyFor(data).toStdString();
    EmbeddedFont font{toBytes(data), {}, fileName.toStdString(), true};
    const auto family = ensureRegistered(key, font);
    if (family.isEmpty())
        return {{Status::InvalidData, "The font file could not be read"}, {}, {}};
    font.family = family.toStdString();
    {
        std::lock_guard lock(mutex_);
        library_[key] = font;
    }
    return {{}, key, font};
}
QStringList FontLibrary::importedFamilies() const {
    std::lock_guard lock(mutex_);
    QStringList out;
    for (const auto& [key, font] : library_) {
        const auto family = QString::fromStdString(font.family);
        if (!out.contains(family))
            out.append(family);
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}
std::vector<ImportedFont> FontLibrary::libraryFonts() const {
    std::lock_guard lock(mutex_);
    std::vector<ImportedFont> out;
    for (const auto& [key, font] : library_)
        out.push_back({{}, key, font});
    return out;
}
QString FontLibrary::ensureRegistered(const std::string& key, const EmbeddedFont& font) {
    std::lock_guard lock(mutex_);
    if (const auto found = registered_.find(key); found != registered_.end())
        return found->second;
    if (!font.data || font.data->empty() || !QGuiApplication::instance())
        return {};
    const int id = QFontDatabase::addApplicationFontFromData(fromBytes(font.data));
    if (id < 0)
        return {};
    const auto families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty())
        return {};
    registered_[key] = families.front();
    return families.front();
}
QString FontLibrary::resolve(const FontRef& ref, const DocumentData& doc) {
    if (!ref.fileKey.empty()) {
        if (const auto found = doc.fonts.find(ref.fileKey); found != doc.fonts.end()) {
            const auto family = ensureRegistered(ref.fileKey, found->second);
            if (!family.isEmpty())
                return family;
        }
        std::lock_guard lock(mutex_);
        if (const auto known = registered_.find(ref.fileKey); known != registered_.end())
            return known->second; // linked font available from the app library
    }
    const auto family = QString::fromStdString(ref.family);
    if (QFontDatabase::hasFamily(family))
        return family;
    return {};
}
std::vector<std::string> FontLibrary::missingFonts(const DocumentData& doc) {
    std::vector<std::string> missing;
    for (const auto& o : doc.objects)
        if (const auto* t = o->as<TextData>())
            if (resolve(t->font, doc).isEmpty() &&
                std::find(missing.begin(), missing.end(), t->font.family) == missing.end())
                missing.push_back(t->font.family);
    return missing;
}
} // namespace kantan::io

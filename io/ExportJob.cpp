#include "io/ExportJob.h"
#include "editing/Transforms.h"
#include "io/IcoWriter.h"
#include "io/Images.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <set>

namespace kantan::io {
namespace {
QString sizeLabel(ExportSize s) {
    return s.width == s.height ? QString::number(s.width)
                               : QStringLiteral("%1x%2").arg(s.width).arg(s.height);
}
} // namespace
QString expandName(const std::string& pattern, const QString& baseName, ExportSize size) {
    QString name = QString::fromStdString(pattern);
    name.replace("{name}", baseName);
    name.replace("{size}", sizeLabel(size));
    name.replace("{w}", QString::number(size.width));
    name.replace("{h}", QString::number(size.height));
    static const QRegularExpression forbidden(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f{}])"));
    if (name.isEmpty() || name.contains(forbidden) || name.endsWith('.') || name.endsWith(' ') ||
        name.size() > 200)
        return {};
    return name;
}
QString expandPath(const std::string& pattern, const QString& baseName, ExportSize size) {
    const auto parts = QString::fromStdString(pattern).split('/');
    if (parts.size() > 4)
        return {};
    QStringList out;
    for (const auto& part : parts) {
        if (part == "." || part == "..")
            return {};
        const auto expanded = expandName(part.toStdString(), baseName, size);
        if (expanded.isEmpty())
            return {};
        out << expanded;
    }
    return out.join('/');
}
ExportPlan planExport(const ExportSettings& s, const QString& folder, const QString& baseName) {
    ExportPlan plan;
    if (!s.png && !s.ico && s.outputs.empty())
        return {{Status::InvalidData, "Choose PNG, ICO or both"}, {}};
    if (folder.isEmpty() || !QDir(folder).exists())
        return {{Status::InvalidData, "Choose an existing output folder"}, {}};
    std::set<QString> used;
    const auto add = [&](ExportFile f) -> bool {
        const auto key = QDir::cleanPath(f.path).toCaseFolded();
        if (!used.insert(key).second)
            return false;
        plan.files.push_back(std::move(f));
        return true;
    };
    if (s.png) {
        std::vector<ExportSize> sizes;
        for (int v : s.pngSizes)
            sizes.push_back({v, v});
        sizes.insert(sizes.end(), s.custom.begin(), s.custom.end());
        std::sort(sizes.begin(), sizes.end());
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
        for (const auto size : sizes) {
            if (size.width < 1 || size.height < 1 || size.width > 8192 || size.height > 8192)
                return {{Status::InvalidData, "Export sizes must be between 1 and 8192 px"}, {}};
            const auto name = expandName(s.naming, baseName, size);
            if (name.isEmpty())
                return {{Status::InvalidData, "The file name pattern produces an invalid file name"}, {}};
            if (!add({QDir(folder).filePath(name + ".png"), false, {size}}))
                return {
                    {Status::InvalidData, "The file name pattern must include {size} to tell sizes apart"},
                    {}};
        }
    }
    for (const auto& named : s.outputs) {
        const ExportSize size{named.width, named.height};
        if (size.width < 1 || size.height < 1 || size.width > 8192 || size.height > 8192)
            return {{Status::InvalidData, "Export sizes must be between 1 and 8192 px"}, {}};
        const auto relative = expandPath(named.path, baseName, size);
        if (relative.isEmpty())
            return {{Status::InvalidData, "A preset file name is invalid"}, {}};
        if (!add({QDir(folder).filePath(relative + ".png"), false, {size}}))
            return {{Status::InvalidData, "Two outputs would share a file name"}, {}};
    }
    if (s.ico) {
        std::vector<ExportSize> sizes;
        for (int v : s.icoSizes)
            if (v >= 1 && v <= 256)
                sizes.push_back({v, v});
        for (const auto c : s.custom)
            if (c.width == c.height && c.width <= 256)
                sizes.push_back(c);
        std::sort(sizes.begin(), sizes.end());
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
        if (sizes.empty())
            return {{Status::InvalidData, "Tick at least one ICO size (256 px or smaller)"}, {}};
        const auto stem = expandName(s.icoName.empty() ? "{name}" : s.icoName, baseName, sizes.back());
        if (stem.isEmpty())
            return {{Status::InvalidData, "The document name is not a valid file name"}, {}};
        if (!add({QDir(folder).filePath(stem + ".ico"), true, sizes}))
            return {{Status::InvalidData, "Two outputs would share a file name"}, {}};
    }
    if (plan.files.empty())
        return {{Status::InvalidData, "Tick at least one size"}, {}};
    return plan;
}
Image renderExport(const DocumentData& doc, ExportSize size, const ExportSettings& s,
                   const std::vector<ObjectId>& selection, const RenderServices& services,
                   const std::shared_ptr<MemoryBudget>& budget) {
    Box source{0, 0, doc.artboard.width, doc.artboard.height};
    RenderRequest request;
    if (s.selectionOnly && !selection.empty()) {
        source = selectionBounds(doc, selection, services);
        request.only = selection;
    }
    request.width = size.width;
    request.height = size.height;
    request.toPixels = fitTransform(source, size.width, size.height, s.padding, s.fit == ExportFit::Cover);
    request.exportSize = std::min(size.width, size.height);
    request.background = !s.transparent;
    request.backgroundColor = s.background;
    return render(doc, request, services, budget);
}
std::vector<ExportOutcome> runExport(const DocumentData& doc, const ExportPlan& plan, const ExportSettings& s,
                                     const std::vector<ObjectId>& selection, const RenderServices& services,
                                     WriteCoordinator& storage, const std::shared_ptr<MemoryBudget>& budget) {
    std::vector<ExportOutcome> out;
    for (const auto& file : plan.files) {
        try {
            QByteArray bytes;
            if (file.ico) {
                std::vector<Image> images;
                for (const auto size : file.sizes)
                    images.push_back(renderExport(doc, size, s, selection, services, budget));
                bytes = icoBytes(images);
                readIco(bytes); // verify the container before it replaces anything
            } else
                bytes = encodePng(renderExport(doc, file.sizes.front(), s, selection, services, budget));
            if (!QDir().mkpath(QFileInfo(file.path).path()))
                throw std::runtime_error("Could not create the output folder");
            auto ticket = storage.reserve(file.path);
            out.push_back({file.path, ticket.run([&] { return writeFile(file.path, bytes); })});
        } catch (const std::exception& e) {
            out.push_back({file.path, {Status::ResourceLimit, e.what()}});
        }
    }
    return out;
}
} // namespace kantan::io

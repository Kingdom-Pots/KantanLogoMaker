#include "app/PresetPanel.h"
#include "editing/Transforms.h"
#include "io/Images.h"
#include "io/Serialization.h"
#include "io/SvgImport.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QPainter>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace kantan::app {
namespace {
QListWidget* grid(QWidget* parent, const QString& name) {
    auto list = new QListWidget(parent);
    list->setObjectName(name);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(48, 48));
    list->setGridSize(QSize(92, 82));
    list->setTextElideMode(Qt::ElideNone);
    QFont small = list->font();
    small.setPointSizeF(small.pointSizeF() * .85);
    list->setFont(small);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWordWrap(true);
    list->setUniformItemSizes(true);
    list->setSpacing(2);
    return list;
}
DocumentData previewDoc(std::vector<Object> objects) {
    DocumentData doc;
    std::uint64_t id = 1;
    std::map<std::uint64_t, std::uint64_t> remap;
    for (auto& o : objects) {
        const auto old = o.id.value;
        o.id = {id++};
        if (old)
            remap[old] = o.id.value;
        o.parent = o.parent ? ObjectId{remap[o.parent.value]} : ObjectId{};
        doc.objects.push_back(std::make_shared<const Object>(std::move(o)));
    }
    return doc;
}
} // namespace
void PresetPanel::filter(const QString& text) {
    const auto needle = text.trimmed();
    for (auto* list : {shapes_, styles_, starters_}) {
        int shown = 0;
        for (int i = 0; i < list->count(); ++i) {
            auto* item = list->item(i);
            const bool match = needle.isEmpty() || item->text().contains(needle, Qt::CaseInsensitive) ||
                               item->toolTip().contains(needle, Qt::CaseInsensitive);
            item->setHidden(!match);
            shown += match ? 1 : 0;
        }
        setTabText(indexOf(list), list->property("baseTitle").toString() +
                                      (needle.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(shown)));
    }
}
PresetPanel::PresetPanel(std::shared_ptr<Editor> editor, std::shared_ptr<AppServices> services,
                         QString userFolder, QWidget* parent)
    : QTabWidget(parent), editor_(std::move(editor)), services_(std::move(services)),
      userFolder_(std::move(userFolder)) {
    setObjectName("presetPanel");
    setDocumentMode(true);
    shapes_ = grid(this, "shapePresets");
    styles_ = grid(this, "stylePresets");
    starters_ = grid(this, "starterTemplates");
    palette_ = new PalettePanel(editor_, this);
    addTab(shapes_, "Shapes");
    addTab(styles_, "Styles");
    addTab(palette_, "Palette");
    addTab(starters_, "Starter");
    shapes_->setProperty("baseTitle", "Shapes");
    styles_->setProperty("baseTitle", "Styles");
    starters_->setProperty("baseTitle", "Starter");
    search_ = new QLineEdit(this);
    search_->setObjectName("presetSearch");
    search_->setPlaceholderText("Search shapes, styles and templates");
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, &PresetPanel::filter);
    connect(palette_, &PalettePanel::documentChanged, this, &PresetPanel::documentChanged);
    connect(palette_, &PalettePanel::feedback, this, &PresetPanel::feedback);
    // Parametric presets.
    const auto& presets = shapePresets();
    for (std::size_t i = 0; i < presets.size(); ++i) {
        Object o;
        o.style = defaultShapeStyle();
        o.data = ShapeData{presets[i].width, presets[i].height, presets[i].geometry};
        const auto doc = previewDoc({o});
        auto item = new QListWidgetItem(render(doc, inflated(objectBounds(doc, *doc.objects[0], {}), 2)),
                                        presets[i].name);
        item->setData(Qt::UserRole, static_cast<int>(i));
        item->setData(Qt::UserRole + 1, QStringLiteral("parametric"));
        item->setToolTip(presets[i].name);
        shapes_->addItem(item);
    }
    // SVG symbols shipped with the app.
    for (const auto& name : QDir(":/presets").entryList({"*.svg"}, QDir::Files, QDir::Name)) {
        QFile file(":/presets/" + name);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const auto svg = io::importSvg(file.readAll(), name, 200);
        if (!svg.result)
            continue;
        const auto doc = previewDoc(svg.objects);
        auto item = new QListWidgetItem(render(doc, selectionBounds(doc, {doc.objects[0]->id}, {})),
                                        QFileInfo(name).completeBaseName());
        item->setData(Qt::UserRole, QString(":/presets/" + name));
        item->setData(Qt::UserRole + 1, QStringLiteral("svg"));
        shapes_->addItem(item);
    }
    builtInShapes_ = shapes_->count();
    reloadUserPresets();
    // Styles, previewed on a rounded rectangle.
    const auto& styles = stylePresets();
    for (std::size_t i = 0; i < styles.size(); ++i) {
        Object o;
        o.style = styles[i].style;
        o.data = ShapeData{160, 160, StarParams{5, .5, 8}};
        const auto doc = previewDoc({o});
        auto item = new QListWidgetItem(render(doc, inflated(objectBounds(doc, *doc.objects[0], {}), 24)),
                                        styles[i].name);
        item->setData(Qt::UserRole, static_cast<int>(i));
        styles_->addItem(item);
    }
    // Starter templates.
    const auto templates = starterTemplates(io::FontLibrary::fallbackFamily());
    for (std::size_t i = 0; i < templates.size(); ++i) {
        const auto& t = templates[i];
        auto item = new QListWidgetItem(
            render(t.document, {0, 0, t.document.artboard.width, t.document.artboard.height}), t.name);
        item->setData(Qt::UserRole, static_cast<int>(i));
        starters_->addItem(item);
    }
    connect(shapes_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const auto kind = item->data(Qt::UserRole + 1).toString();
        if (kind == "parametric") {
            const auto& p = shapePresets()[static_cast<std::size_t>(item->data(Qt::UserRole).toInt())];
            Object o;
            o.name = p.name.toStdString();
            o.style = defaultShapeStyle();
            o.data = ShapeData{p.width, p.height, p.geometry};
            place({o});
        } else if (kind == "svg") {
            auto svg = io::importSvgFile(item->data(Qt::UserRole).toString(), 200);
            if (!svg.result) {
                report(svg.result);
                return;
            }
            place(std::move(svg.objects));
        } else {
            QFile file(item->data(Qt::UserRole).toString());
            if (!file.open(QIODevice::ReadOnly))
                return;
            try {
                const auto fragment = io::fragmentFromJson(QJsonDocument::fromJson(file.readAll()).object());
                place(fragment.objects);
            } catch (const std::exception& e) {
                emit feedback(QString("Preset could not be read: ") + e.what());
            }
        }
    });
    connect(styles_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const auto& style = stylePresets()[static_cast<std::size_t>(item->data(Qt::UserRole).toInt())].style;
        const auto doc = editor_->snapshot();
        std::vector<Object> changed;
        for (const auto id : editor_->selection())
            if (const auto* o = doc->find(id);
                o && o->kind() != ObjectKind::Group && o->kind() != ObjectKind::Clip) {
                Object copy = *o;
                copy.style = style;
                changed.push_back(std::move(copy));
            }
        if (changed.empty()) {
            emit feedback("Select objects to apply a style");
            return;
        }
        report(editor_->update("Apply style", std::move(changed)));
        emit documentChanged();
    });
    connect(starters_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { emit openTemplate(item->data(Qt::UserRole).toInt()); });
    // Single click also activates: presets are buttons, not a selection.
    for (auto* list : {shapes_, styles_, starters_})
        connect(list, &QListWidget::itemClicked, list, &QListWidget::itemActivated);
}
void PresetPanel::report(Result r) {
    if (!r && !r.message.empty())
        emit feedback(QString::fromStdString(r.message));
}
QIcon PresetPanel::render(const DocumentData& doc, Box source) {
    const int size = 104;
    RenderRequest request;
    request.width = request.height = size;
    request.toPixels = fitTransform(source, size, size, 4);
    QImage image;
    try {
        image = io::toQImage(kantan::render(doc, request, services_->render(), services_->budget));
    } catch (const std::exception&) {
        return {};
    }
    image.setDevicePixelRatio(2);
    QPixmap pix(size, size);
    pix.setDevicePixelRatio(2);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.drawImage(0, 0, image);
    p.end();
    return QIcon(pix);
}
void PresetPanel::place(std::vector<Object> objects) {
    if (objects.empty())
        return;
    // Centre the placed roots on the artboard.
    const auto doc = editor_->snapshot();
    const Point center{doc->artboard.width / 2, doc->artboard.height / 2};
    const auto preview = previewDoc(objects);
    std::vector<ObjectId> roots;
    for (const auto& o : preview.objects)
        if (!o->parent)
            roots.push_back(o->id);
    const Point offset = center - selectionBounds(preview, roots, services_->render()).center();
    for (auto& o : objects)
        if (!o.parent)
            o.transform.position = o.transform.position + offset;
    report(editor_->add(std::move(objects)));
    emit documentChanged();
}
void PresetPanel::reloadUserPresets() {
    while (shapes_->count() > builtInShapes_)
        delete shapes_->takeItem(shapes_->count() - 1);
    if (userFolder_.isEmpty())
        return;
    for (const auto& info : QDir(userFolder_).entryInfoList({"*.kantanpreset"}, QDir::Files, QDir::Name)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
            continue;
        try {
            const auto fragment = io::fragmentFromJson(QJsonDocument::fromJson(file.readAll()).object());
            const auto doc = previewDoc(fragment.objects);
            std::vector<ObjectId> roots;
            for (const auto& o : doc.objects)
                if (!o->parent)
                    roots.push_back(o->id);
            auto item = new QListWidgetItem(render(doc, selectionBounds(doc, roots, services_->render())),
                                            info.completeBaseName());
            item->setData(Qt::UserRole, info.absoluteFilePath());
            item->setData(Qt::UserRole + 1, QStringLiteral("user"));
            item->setToolTip("Saved preset");
            shapes_->addItem(item);
        } catch (const std::exception&) {
        }
    }
    if (search_)
        filter(search_->text());
}
void PresetPanel::saveSelection() {
    const auto doc = editor_->snapshot();
    const auto& selection = editor_->selection();
    if (selection.empty()) {
        emit feedback("Select objects to save as a preset");
        return;
    }
    for (const auto id : selection) {
        const auto i = doc->indexOf(id);
        for (auto j = i; j < doc->subtreeEnd(i); ++j)
            if (doc->objects[j]->as<ImageData>()) {
                emit feedback("Presets cannot contain images");
                return;
            }
    }
    bool ok = false;
    auto name = QInputDialog::getText(this, "Save preset", "Preset name", QLineEdit::Normal, "My shape", &ok)
                    .trimmed();
    if (!ok || name.isEmpty())
        return;
    name.replace(QRegularExpression(R"([<>:"/\\|?*\x00-\x1f])"), "_");
    if (userFolder_.isEmpty() || !QDir().mkpath(userFolder_)) {
        emit feedback("The presets folder is not available");
        return;
    }
    const auto json = QJsonDocument(io::fragmentToJson(*doc, selection)).toJson();
    report(io::writeFile(QDir(userFolder_).filePath(name + ".kantanpreset"), json));
    reloadUserPresets();
    emit feedback("Preset saved");
}
} // namespace kantan::app

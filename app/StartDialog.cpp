#include "app/StartDialog.h"
#include "app/Presets.h"
#include "app/RecentFiles.h"
#include "io/ExportJob.h"
#include "io/FontLibrary.h"
#include "io/Images.h"
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace kantan::app {
namespace {
struct SizePreset {
    const char* name;
    int width, height;
};
const SizePreset Sizes[] = {
    {"Square logo · 512 × 512", 512, 512},    {"App icon · 1024 × 1024", 1024, 1024},
    {"Wordmark · 1024 × 384", 1024, 384},     {"Steam capsule · 920 × 430", 920, 430},
    {"itch.io cover · 630 × 500", 630, 500},  {"Social banner · 1500 × 500", 1500, 500},
    {"Avatar / emblem · 800 × 800", 800, 800}};
QIcon thumbnail(const DocumentData& doc, const AppServices& services) {
    constexpr int Side = 96;
    ExportSettings s = doc.exportSettings;
    s.padding = 4;
    s.selectionOnly = false;
    s.fit = ExportFit::Contain;
    QPixmap pix(Side, Side);
    pix.fill(QColor(52, 58, 69));
    try {
        const auto image =
            io::toQImage(io::renderExport(doc, {Side, Side}, s, {}, services.render(), services.budget));
        QPainter p(&pix);
        p.drawImage(0, 0, image);
    } catch (const std::exception&) {
    }
    return QIcon(pix);
}
} // namespace
bool StartDialog::showAtStartup() {
    return QSettings().value("start/show", true).toBool();
}
void StartDialog::setShowAtStartup(bool show) {
    QSettings().setValue("start/show", show);
}
StartDialog::StartDialog(std::shared_ptr<AppServices> services, QWidget* parent) : QDialog(parent) {
    setObjectName("startDialog");
    setWindowTitle("Kantan LogoMaker");
    resize(900, 560);
    auto root = new QVBoxLayout(this);
    auto columns = new QHBoxLayout;
    root->addLayout(columns, 1);
    // Blank artboards.
    auto blank = new QGroupBox("New blank logo");
    auto blankLayout = new QVBoxLayout(blank);
    sizes_ = new QListWidget;
    sizes_->setObjectName("startSizes");
    for (const auto& s : Sizes) {
        auto item = new QListWidgetItem(s.name);
        item->setData(Qt::UserRole, QSize(s.width, s.height));
        sizes_->addItem(item);
    }
    blankLayout->addWidget(sizes_, 1);
    auto custom = new QHBoxLayout;
    width_ = new QSpinBox;
    height_ = new QSpinBox;
    for (auto* spin : {width_, height_}) {
        spin->setRange(1, static_cast<int>(MaxArtboard));
        spin->setValue(512);
    }
    auto createCustom = new QPushButton("Create");
    createCustom->setObjectName("startCustom");
    custom->addWidget(width_);
    custom->addWidget(new QLabel("×"));
    custom->addWidget(height_);
    custom->addWidget(createCustom);
    blankLayout->addWidget(new QLabel("Custom size"));
    blankLayout->addLayout(custom);
    columns->addWidget(blank, 2);
    // Templates.
    auto templateBox = new QGroupBox("Start from a template");
    auto templateLayout = new QVBoxLayout(templateBox);
    templates_ = new QListWidget;
    templates_->setObjectName("startTemplates");
    templates_->setViewMode(QListView::IconMode);
    templates_->setIconSize(QSize(96, 96));
    templates_->setGridSize(QSize(124, 128));
    templates_->setResizeMode(QListView::Adjust);
    templates_->setMovement(QListView::Static);
    templates_->setWordWrap(true);
    const auto list = starterTemplates(io::FontLibrary::fallbackFamily());
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto item = new QListWidgetItem(thumbnail(list[i].document, *services), list[i].name);
        item->setData(Qt::UserRole, static_cast<int>(i));
        templates_->addItem(item);
    }
    templateLayout->addWidget(templates_);
    columns->addWidget(templateBox, 4);
    // Recent files.
    auto recentBox = new QGroupBox("Recent");
    auto recentLayout = new QVBoxLayout(recentBox);
    recent_ = new QListWidget;
    recent_->setObjectName("startRecent");
    for (const auto& path : recentFiles()) {
        auto item = new QListWidgetItem(QFileInfo(path).completeBaseName());
        item->setToolTip(QDir::toNativeSeparators(path));
        item->setData(Qt::UserRole, path);
        recent_->addItem(item);
    }
    if (recent_->count() == 0) {
        auto none = new QListWidgetItem("No recent logos yet");
        none->setFlags(Qt::NoItemFlags);
        recent_->addItem(none);
    }
    recentLayout->addWidget(recent_, 1);
    auto browse = new QPushButton("Open…");
    browse->setObjectName("startBrowse");
    recentLayout->addWidget(browse);
    columns->addWidget(recentBox, 2);
    // Footer.
    auto footer = new QHBoxLayout;
    showAgain_ = new QCheckBox("Show this at startup");
    showAgain_->setObjectName("startShowAgain");
    showAgain_->setChecked(showAtStartup());
    footer->addWidget(showAgain_);
    footer->addStretch();
    auto skip = new QPushButton("Skip");
    footer->addWidget(skip);
    root->addLayout(footer);
    connect(showAgain_, &QCheckBox::toggled, this, &StartDialog::setShowAtStartup);
    connect(skip, &QPushButton::clicked, this, &QDialog::reject);
    const auto blankChoice = [this](int w, int h) {
        Choice c;
        c.kind = Choice::Blank;
        c.artboard.width = w;
        c.artboard.height = h;
        finish(c);
    };
    connect(sizes_, &QListWidget::itemActivated, this, [blankChoice](QListWidgetItem* item) {
        const auto size = item->data(Qt::UserRole).toSize();
        blankChoice(size.width(), size.height());
    });
    connect(createCustom, &QPushButton::clicked, this,
            [this, blankChoice] { blankChoice(width_->value(), height_->value()); });
    connect(templates_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        Choice c;
        c.kind = Choice::Template;
        c.templateIndex = item->data(Qt::UserRole).toInt();
        finish(c);
    });
    connect(recent_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const auto path = item->data(Qt::UserRole).toString();
        if (path.isEmpty())
            return;
        Choice c;
        c.kind = Choice::Recent;
        c.path = path;
        finish(c);
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        Choice c;
        c.kind = Choice::Browse;
        finish(c);
    });
    // Single click is enough in the icon grid and lists; Enter works too.
    for (auto* view : {sizes_, templates_, recent_})
        connect(view, &QListWidget::itemClicked, view, &QListWidget::itemActivated);
}
void StartDialog::finish(Choice choice) {
    choice_ = std::move(choice);
    accept();
}
} // namespace kantan::app

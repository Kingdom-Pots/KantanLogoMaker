#include "app/ExportDialog.h"
#include "io/ExportJob.h"
#include "io/ExportPresets.h"
#include "io/Images.h"
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

namespace kantan::app {
namespace {
const std::vector<int> Standard{16, 24, 32, 48, 64, 128, 256, 512};
}
ExportDialog::ExportDialog(Snapshot document, std::vector<ObjectId> selection,
                           std::shared_ptr<AppServices> services, QString baseName, QWidget* parent)
    : QDialog(parent), document_(std::move(document)), selection_(std::move(selection)),
      services_(std::move(services)) {
    setWindowTitle("Export");
    setObjectName("exportDialog");
    resize(760, 560);
    const auto& s = document_->exportSettings;
    base_ = s;
    background_ = s.background;
    auto root = new QVBoxLayout(this);
    // Platform presets: one click sets sizes, file names and folders for a target.
    auto presetRow = new QHBoxLayout;
    preset_ = new QComboBox;
    preset_->setObjectName("exportPreset");
    preset_->addItem("Custom");
    for (const auto& p : io::exportPresets()) {
        preset_->addItem(p.name);
        preset_->setItemData(preset_->count() - 1, p.description, Qt::ToolTipRole);
    }
    if (const int found = preset_->findText(QString::fromStdString(s.preset)); found > 0)
        preset_->setCurrentIndex(found);
    presetRow->addWidget(new QLabel("Preset"));
    presetRow->addWidget(preset_, 1);
    root->addLayout(presetRow);
    presetInfo_ = new QLabel;
    presetInfo_->setWordWrap(true);
    presetInfo_->setStyleSheet("color:#9aa5b5;");
    root->addWidget(presetInfo_);
    auto columns = new QHBoxLayout;
    root->addLayout(columns);
    auto left = new QVBoxLayout;
    columns->addLayout(left, 1);
    // Formats and sizes.
    auto sizes = new QGroupBox("Formats and sizes");
    auto grid = new QGridLayout(sizes);
    png_ = new QCheckBox("PNG");
    ico_ = new QCheckBox("ICO");
    png_->setChecked(s.png);
    ico_->setChecked(s.ico);
    grid->addWidget(png_, 0, 1);
    grid->addWidget(ico_, 0, 2);
    int row = 1;
    for (int size : Standard) {
        grid->addWidget(new QLabel(QStringLiteral("%1 × %1").arg(size)), row, 0);
        auto p = new QCheckBox;
        p->setChecked(std::find(s.pngSizes.begin(), s.pngSizes.end(), size) != s.pngSizes.end());
        auto i = new QCheckBox;
        i->setChecked(std::find(s.icoSizes.begin(), s.icoSizes.end(), size) != s.icoSizes.end());
        if (size > 256) {
            i->setChecked(false);
            i->setEnabled(false);
            i->setToolTip("ICO entries are limited to 256 × 256 by the format");
        }
        p->setAccessibleName(QStringLiteral("PNG %1").arg(size));
        i->setAccessibleName(QStringLiteral("ICO %1").arg(size));
        pngSizes_[size] = p;
        icoSizes_[size] = i;
        grid->addWidget(p, row, 1);
        grid->addWidget(i, row, 2);
        ++row;
    }
    left->addWidget(sizes);
    auto customBox = new QGroupBox("Custom sizes (PNG; square ones up to 256 also go into the ICO)");
    auto customLayout = new QVBoxLayout(customBox);
    custom_ = new QListWidget;
    custom_->setMaximumHeight(80);
    for (const auto& c : s.custom) {
        auto item = new QListWidgetItem(QStringLiteral("%1 × %2").arg(c.width).arg(c.height));
        item->setData(Qt::UserRole, QSize(c.width, c.height));
        custom_->addItem(item);
    }
    customLayout->addWidget(custom_);
    auto customRow = new QHBoxLayout;
    auto w = new QSpinBox, h = new QSpinBox;
    for (auto* spin : {w, h}) {
        spin->setRange(1, 8192);
        spin->setValue(1024);
    }
    auto add = new QPushButton("Add");
    auto remove = new QPushButton("Remove");
    customRow->addWidget(w);
    customRow->addWidget(new QLabel("×"));
    customRow->addWidget(h);
    customRow->addWidget(add);
    customRow->addWidget(remove);
    customLayout->addLayout(customRow);
    left->addWidget(customBox);
    connect(add, &QPushButton::clicked, this, [this, w, h] {
        if (custom_->count() >= 32)
            return;
        auto item = new QListWidgetItem(QStringLiteral("%1 × %2").arg(w->value()).arg(h->value()));
        item->setData(Qt::UserRole, QSize(w->value(), h->value()));
        custom_->addItem(item);
        previewTimer_.start();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        delete custom_->currentItem();
        previewTimer_.start();
    });
    // Layout options.
    auto right = new QVBoxLayout;
    columns->addLayout(right, 1);
    auto options = new QGroupBox("Options");
    auto form = new QFormLayout(options);
    padding_ = new QDoubleSpinBox;
    padding_->setRange(0, 45);
    padding_->setSuffix(" %");
    padding_->setValue(s.padding);
    form->addRow("Padding", padding_);
    fit_ = new QComboBox;
    fit_->addItems({"Contain (centred)", "Cover"});
    fit_->setCurrentIndex(static_cast<int>(s.fit));
    form->addRow("Non-square fit", fit_);
    transparent_ = new QCheckBox("Transparent");
    transparent_->setChecked(s.transparent);
    auto bgButton = new QToolButton;
    bgButton->setMinimumSize(48, 22);
    const auto swatch = [this, bgButton] {
        bgButton->setStyleSheet(QStringLiteral("background:rgb(%1,%2,%3);border:1px solid #687381;")
                                    .arg(background_.r)
                                    .arg(background_.g)
                                    .arg(background_.b));
    };
    swatch();
    auto bgRow = new QHBoxLayout;
    bgRow->addWidget(transparent_);
    bgRow->addWidget(bgButton);
    form->addRow("Background", bgRow);
    connect(bgButton, &QToolButton::clicked, this, [this, swatch] {
        const auto c = QColorDialog::getColor(QColor(background_.r, background_.g, background_.b), this);
        if (!c.isValid())
            return;
        background_ = {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
                       static_cast<std::uint8_t>(c.blue()), 255};
        transparent_->setChecked(false);
        swatch();
        previewTimer_.start();
    });
    selectionOnly_ = new QCheckBox("Selection bounds only");
    selectionOnly_->setEnabled(!selection_.empty());
    selectionOnly_->setChecked(s.selectionOnly && !selection_.empty());
    form->addRow("Source", selectionOnly_);
    name_ = new QLineEdit(baseName);
    form->addRow("Name", name_);
    naming_ = new QLineEdit(QString::fromStdString(s.naming));
    naming_->setToolTip(
        "{name}, {size}, {w} and {h} are replaced; .png is added. The ICO is saved as {name}.ico.");
    form->addRow("PNG file names", naming_);
    folder_ = new QLineEdit(QString::fromStdString(s.folder));
    if (folder_->text().isEmpty() || !QDir(folder_->text()).exists())
        folder_->setText(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    auto browse = new QPushButton("Browse…");
    auto folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(browse);
    form->addRow("Folder", folderRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto dir = QFileDialog::getExistingDirectory(this, "Export folder", folder_->text());
        if (!dir.isEmpty())
            folder_->setText(dir);
    });
    right->addWidget(options);
    files_ = new QLabel;
    files_->setWordWrap(true);
    files_->setStyleSheet("color:#9aa5b5;");
    right->addWidget(files_);
    right->addStretch();
    // Preview row.
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(150);
    preview_ = new QWidget;
    new QHBoxLayout(preview_);
    scroll->setWidget(preview_);
    root->addWidget(new QLabel("Preview (1:1)"));
    root->addWidget(scroll);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto exportButton = buttons->addButton("Export", QDialogButtonBox::AcceptRole);
    exportButton->setObjectName("exportButton");
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        const auto plan = io::planExport(settings(), folder(), this->baseName());
        if (!plan.result) {
            files_->setText(QString::fromStdString(plan.result.message));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    previewTimer_.setSingleShot(true);
    previewTimer_.setInterval(80);
    connect(&previewTimer_, &QTimer::timeout, this, &ExportDialog::updatePreview);
    const auto changed = [this] { previewTimer_.start(); };
    for (auto* box : {png_, ico_, transparent_, selectionOnly_})
        connect(box, &QCheckBox::toggled, this, changed);
    for (auto& [size, box] : pngSizes_)
        connect(box, &QCheckBox::toggled, this, changed);
    for (auto& [size, box] : icoSizes_)
        connect(box, &QCheckBox::toggled, this, changed);
    connect(padding_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(fit_, &QComboBox::currentIndexChanged, this, changed);
    for (auto* edit : {naming_, folder_, name_})
        connect(edit, &QLineEdit::textChanged, this, changed);
    connect(ico_, &QCheckBox::toggled, this, &ExportDialog::updateIcoAvailability);
    connect(preset_, &QComboBox::activated, this, &ExportDialog::applyPreset);
    updateIcoAvailability();
    load(base_);
    updatePreview();
}
void ExportDialog::applyPreset(int index) {
    auto current = settings();
    if (index <= 0) {
        // Back to custom: drop the preset's named files, keep the size checkboxes as they are.
        current.outputs.clear();
        current.preset.clear();
        current.icoName = "{name}";
    } else {
        current = io::exportPresets()[static_cast<std::size_t>(index - 1)].apply(current);
    }
    base_ = current;
    load(base_);
    previewTimer_.start();
}
void ExportDialog::load(const ExportSettings& s) {
    std::vector<QSignalBlocker> blockers;
    for (auto* box : {png_, ico_, transparent_})
        blockers.emplace_back(box);
    blockers.emplace_back(naming_);
    png_->setChecked(s.png);
    ico_->setChecked(s.ico);
    transparent_->setChecked(s.transparent);
    for (auto& [size, box] : pngSizes_) {
        QSignalBlocker b(box);
        box->setChecked(std::find(s.pngSizes.begin(), s.pngSizes.end(), size) != s.pngSizes.end());
    }
    for (auto& [size, box] : icoSizes_) {
        QSignalBlocker b(box);
        box->setChecked(size <= 256 &&
                        std::find(s.icoSizes.begin(), s.icoSizes.end(), size) != s.icoSizes.end());
    }
    naming_->setText(QString::fromStdString(s.naming));
    custom_->clear();
    for (const auto& size : s.custom) {
        auto item = new QListWidgetItem(QStringLiteral("%1 × %2").arg(size.width).arg(size.height));
        item->setData(Qt::UserRole, QSize(size.width, size.height));
        custom_->addItem(item);
    }
    updateIcoAvailability();
    QString info;
    if (preset_->currentIndex() > 0)
        info = io::exportPresets()[static_cast<std::size_t>(preset_->currentIndex() - 1)].description;
    if (!info.isEmpty() && !info.endsWith('.'))
        info += '.';
    if (!s.outputs.empty())
        info += QStringLiteral("%1Also writes %2 named file(s); they are listed with the others.")
                    .arg(info.isEmpty() ? "" : " ")
                    .arg(s.outputs.size());
    presetInfo_->setText(info);
    presetInfo_->setVisible(!info.isEmpty());
}
void ExportDialog::updateIcoAvailability() {
    for (auto& [size, box] : icoSizes_)
        box->setEnabled(ico_->isChecked() && size <= 256);
}
ExportSettings ExportDialog::settings() const {
    ExportSettings s = base_;
    s.png = png_->isChecked();
    s.ico = ico_->isChecked();
    s.pngSizes.clear();
    s.icoSizes.clear();
    for (const auto& [size, box] : pngSizes_)
        if (box->isChecked())
            s.pngSizes.push_back(size);
    for (const auto& [size, box] : icoSizes_)
        if (box->isChecked() && size <= 256)
            s.icoSizes.push_back(size);
    s.custom.clear();
    for (int i = 0; i < custom_->count(); ++i) {
        const auto size = custom_->item(i)->data(Qt::UserRole).toSize();
        s.custom.push_back({size.width(), size.height()});
    }
    s.padding = padding_->value();
    s.fit = static_cast<ExportFit>(fit_->currentIndex());
    s.transparent = transparent_->isChecked();
    s.background = background_;
    s.selectionOnly = selectionOnly_->isChecked();
    s.naming = naming_->text().toStdString();
    s.folder = folder_->text().toStdString();
    return s;
}
void ExportDialog::updatePreview() {
    auto* layout = preview_->layout();
    while (auto* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto s = settings();
    const auto plan = io::planExport(s, folder(), this->baseName());
    if (!plan.result) {
        files_->setText(QString::fromStdString(plan.result.message));
        return;
    }
    QStringList names;
    for (const auto& f : plan.files)
        names << QDir::toNativeSeparators(f.path);
    files_->setText(QStringLiteral("%1 file(s):\n").arg(plan.files.size()) + names.join('\n'));
    std::vector<ExportSize> sizes;
    for (const auto& f : plan.files)
        for (const auto size : f.sizes)
            if (std::find(sizes.begin(), sizes.end(), size) == sizes.end())
                sizes.push_back(size);
    std::sort(sizes.begin(), sizes.end());
    for (const auto size : sizes) {
        if (size.width > 512 || size.height > 512) {
            auto label =
                new QLabel(QStringLiteral("%1 × %2\n(not previewed)").arg(size.width).arg(size.height));
            label->setAlignment(Qt::AlignCenter);
            layout->addWidget(label);
            continue;
        }
        try {
            auto image = io::toQImage(
                io::renderExport(*document_, size, s, selection_, services_->render(), services_->budget));
            auto label = new QLabel;
            QPixmap pix(image.size());
            pix.fill(QColor(60, 66, 78));
            QPainter p(&pix);
            p.drawImage(0, 0, image);
            p.end();
            label->setPixmap(pix);
            label->setToolTip(QStringLiteral("%1 × %2").arg(size.width).arg(size.height));
            label->setAlignment(Qt::AlignBottom | Qt::AlignHCenter);
            layout->addWidget(label);
        } catch (const std::exception& e) {
            layout->addWidget(new QLabel(QString::fromUtf8(e.what())));
        }
    }
    static_cast<QHBoxLayout*>(layout)->addStretch();
}
} // namespace kantan::app

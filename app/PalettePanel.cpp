#include "app/PalettePanel.h"
#include "app/ColorField.h"
#include "app/CompactControls.h"
#include "editing/Colors.h"
#include <QApplication>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace kantan::app {
namespace {
QIcon chip(Color c) {
    QPixmap pix(40, 40);
    pix.setDevicePixelRatio(2);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(104, 115, 129), 1));
    p.setBrush(QColor(c.r, c.g, c.b, c.a));
    p.drawRoundedRect(QRectF(1, 1, 18, 18), 3, 3);
    p.end();
    return QIcon(pix);
}
} // namespace
PalettePanel::PalettePanel(std::shared_ptr<Editor> editor, QWidget* parent)
    : QWidget(parent), editor_(std::move(editor)) {
    setObjectName("palettePanel");
    auto v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);
    list_ = new QListWidget(this);
    list_->setObjectName("paletteList");
    list_->setIconSize(QSize(20, 20));
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setToolTip(
        "Click: apply to fill · Shift-click: apply to outline · double-click: change colour everywhere");
    v->addWidget(list_);
    auto row = new QHBoxLayout;
    row->setSpacing(4);
    const auto button = [&](const QString& icon, const QString& tip, const std::function<void()>& fn) {
        auto b = new QToolButton(this);
        iconButton(b, icon, tip);
        row->addWidget(b);
        connect(b, &QToolButton::clicked, this, fn);
    };
    button("add", "Add the selection's fill colour (or pick one)", [this] {
        auto palette = editor_->snapshot()->palette;
        if (palette.size() >= MaxPalette)
            return;
        std::optional<Color> color;
        const auto doc = editor_->snapshot();
        if (!editor_->selection().empty())
            if (const auto* o = doc->find(editor_->selection().front());
                o && o->style.fill.kind == FillKind::Solid)
                color = o->style.fill.color;
        if (!color) {
            const auto q =
                QColorDialog::getColor(Qt::white, this, "Add colour", QColorDialog::ShowAlphaChannel);
            if (!q.isValid())
                return;
            color = Color{static_cast<std::uint8_t>(q.red()), static_cast<std::uint8_t>(q.green()),
                          static_cast<std::uint8_t>(q.blue()), static_cast<std::uint8_t>(q.alpha())};
        }
        palette.push_back({"Colour " + std::to_string(palette.size() + 1), *color});
        report(editor_->setPalette(std::move(palette)));
        emit documentChanged();
    });
    button("color", "Add every colour used in this document", [this] {
        auto palette = editor_->snapshot()->palette;
        for (const auto c : documentColors(*editor_->snapshot()))
            if (palette.size() < MaxPalette &&
                std::none_of(palette.begin(), palette.end(),
                             [&](const PaletteColor& p) { return sameRgb(p.color, c); }))
                palette.push_back({"Colour " + std::to_string(palette.size() + 1), c});
        report(editor_->setPalette(std::move(palette)));
        emit documentChanged();
    });
    button("delete", "Remove the selected palette colour (objects keep their colours)", [this] {
        const int current = list_->currentRow();
        auto palette = editor_->snapshot()->palette;
        if (current < 0 || current >= static_cast<int>(palette.size()))
            return;
        palette.erase(palette.begin() + current);
        report(editor_->setPalette(std::move(palette)));
        emit documentChanged();
    });
    row->addStretch();
    v->addLayout(row);
    auto hint = new QLabel("Editing a colour here recolours every object that uses it.");
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#8d97a6;");
    v->addWidget(hint);
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        applyToSelection(static_cast<std::size_t>(list_->row(item)),
                         QApplication::keyboardModifiers() & Qt::ShiftModifier);
    });
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { editColor(static_cast<std::size_t>(list_->row(item))); });
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](QPoint at) {
        auto* item = list_->itemAt(at);
        if (!item)
            return;
        const auto index = static_cast<std::size_t>(list_->row(item));
        QMenu menu(this);
        menu.addAction("Apply to fill", this, [this, index] { applyToSelection(index, false); });
        menu.addAction("Apply to outline", this, [this, index] { applyToSelection(index, true); });
        menu.addAction("Change colour everywhere…", this, [this, index] { editColor(index); });
        menu.addAction("Rename…", this, [this, index] {
            auto palette = editor_->snapshot()->palette;
            bool ok = false;
            const auto name = QInputDialog::getText(this, "Rename colour", "Name", QLineEdit::Normal,
                                                    QString::fromStdString(palette[index].name), &ok)
                                  .trimmed()
                                  .left(128);
            if (!ok || name.isEmpty())
                return;
            palette[index].name = name.toStdString();
            report(editor_->setPalette(std::move(palette)));
            emit documentChanged();
        });
        menu.exec(list_->mapToGlobal(at));
    });
    refresh();
}
void PalettePanel::report(Result r) {
    if (!r && !r.message.empty())
        emit feedback(QString::fromStdString(r.message));
}
void PalettePanel::refresh() {
    const auto& palette = editor_->snapshot()->palette;
    const int current = list_->currentRow();
    list_->clear();
    for (const auto& p : palette) {
        auto item =
            new QListWidgetItem(chip(p.color), QString::fromStdString(p.name) + "  #" + hexName(p.color));
        list_->addItem(item);
    }
    if (current >= 0 && current < list_->count())
        list_->setCurrentRow(current);
}
void PalettePanel::applyToSelection(std::size_t index, bool outline) {
    const auto doc = editor_->snapshot();
    if (index >= doc->palette.size())
        return;
    const Color c = doc->palette[index].color;
    std::vector<Object> changed;
    for (const auto id : editor_->selection())
        if (const auto* o = doc->find(id)) {
            Object copy = *o;
            if (outline) {
                if (copy.style.outlines.empty())
                    copy.style.outlines.push_back({});
                copy.style.outlines[0].paint = {FillKind::Solid, c};
            } else {
                copy.style.fill.kind = FillKind::Solid;
                copy.style.fill.color = c;
            }
            changed.push_back(std::move(copy));
        }
    if (changed.empty()) {
        emit feedback("Select objects to colour");
        return;
    }
    ColorField::remember(c);
    report(editor_->update(outline ? "Outline colour" : "Fill colour", std::move(changed)));
    emit documentChanged();
}
void PalettePanel::editColor(std::size_t index) {
    const auto doc = editor_->snapshot();
    if (index >= doc->palette.size())
        return;
    const Color c = doc->palette[index].color;
    const auto q = QColorDialog::getColor(QColor(c.r, c.g, c.b, c.a), this, "Change colour everywhere",
                                          QColorDialog::ShowAlphaChannel);
    if (!q.isValid())
        return;
    report(editor_->editPaletteColor(
        index, {static_cast<std::uint8_t>(q.red()), static_cast<std::uint8_t>(q.green()),
                static_cast<std::uint8_t>(q.blue()), static_cast<std::uint8_t>(q.alpha())}));
    emit documentChanged();
}
} // namespace kantan::app

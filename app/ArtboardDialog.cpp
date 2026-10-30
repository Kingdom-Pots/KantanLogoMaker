#include "app/ArtboardDialog.h"
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QToolButton>

namespace kantan::app {
ArtboardDialog::ArtboardDialog(const Artboard& current, QWidget* parent)
    : QDialog(parent), initial_(current), background_(current.background) {
    setWindowTitle("Artboard");
    auto form = new QFormLayout(this);
    width_ = new QSpinBox;
    height_ = new QSpinBox;
    for (auto* s : {width_, height_}) {
        s->setRange(1, static_cast<int>(MaxArtboard));
        s->setSuffix(" units");
    }
    width_->setValue(static_cast<int>(std::lround(current.width)));
    height_->setValue(static_cast<int>(std::lround(current.height)));
    form->addRow("Width", width_);
    form->addRow("Height", height_);
    transparent_ = new QCheckBox("Transparent");
    transparent_->setChecked(current.transparent);
    auto swatch = new QToolButton;
    swatch->setMinimumSize(48, 22);
    const auto paint = [this, swatch] {
        swatch->setStyleSheet(QStringLiteral("background:rgb(%1,%2,%3);border:1px solid #687381;")
                                  .arg(background_.r)
                                  .arg(background_.g)
                                  .arg(background_.b));
    };
    paint();
    connect(swatch, &QToolButton::clicked, this, [this, paint] {
        const auto c = QColorDialog::getColor(QColor(background_.r, background_.g, background_.b), this);
        if (!c.isValid())
            return;
        background_ = {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
                       static_cast<std::uint8_t>(c.blue()), 255};
        transparent_->setChecked(false);
        paint();
    });
    auto row = new QHBoxLayout;
    row->addWidget(transparent_);
    row->addWidget(swatch);
    form->addRow("Background", row);
    centred_ = new QCheckBox("Keep artwork centred");
    centred_->setChecked(true);
    form->addRow("", centred_);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
Artboard ArtboardDialog::artboard() const {
    Artboard a = initial_;
    a.width = width_->value();
    a.height = height_->value();
    a.transparent = transparent_->isChecked();
    a.background = background_;
    return a;
}
} // namespace kantan::app

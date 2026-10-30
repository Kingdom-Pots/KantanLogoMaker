#include "app/ColorField.h"
#include <QColorDialog>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>

namespace kantan::app {
namespace {
QString swatchStyle(Color c, bool mixed = false) {
    if (mixed)
        return "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #59616f,stop:0.49 #59616f,stop:0.51 "
               "#2c323e,stop:1 #2c323e);border:1px dashed #8d97a6;border-radius:2px;";
    return QStringLiteral("background:rgba(%1,%2,%3,%4);border:1px solid #687381;border-radius:2px;")
        .arg(c.r)
        .arg(c.g)
        .arg(c.b)
        .arg(c.a / 255., 0, 'f', 3);
}
Color fromQColor(const QColor& q) {
    return {static_cast<std::uint8_t>(q.red()), static_cast<std::uint8_t>(q.green()),
            static_cast<std::uint8_t>(q.blue()), static_cast<std::uint8_t>(q.alpha())};
}
} // namespace
QString hexName(Color c) {
    QString s = QStringLiteral("%1%2%3")
                    .arg(c.r, 2, 16, QLatin1Char('0'))
                    .arg(c.g, 2, 16, QLatin1Char('0'))
                    .arg(c.b, 2, 16, QLatin1Char('0'));
    if (c.a != 255)
        s += QStringLiteral("%1").arg(c.a, 2, 16, QLatin1Char('0'));
    return s.toUpper();
}
std::optional<Color> parseHex(const QString& text) {
    static const QRegularExpression pattern(
        QStringLiteral("^#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    const auto m = pattern.match(text.trimmed());
    if (!m.hasMatch())
        return std::nullopt;
    QString h = m.captured(1);
    if (h.size() == 3)
        h = QString(h[0]) + h[0] + h[1] + h[1] + h[2] + h[2];
    const auto byte = [&](int i) { return static_cast<std::uint8_t>(h.mid(i * 2, 2).toUInt(nullptr, 16)); };
    return Color{byte(0), byte(1), byte(2), h.size() == 8 ? byte(3) : static_cast<std::uint8_t>(255)};
}
std::vector<Color> ColorField::recentColors() {
    std::vector<Color> out;
    for (const auto& v : QSettings().value("colors/recent").toStringList())
        if (const auto c = parseHex(v))
            out.push_back(*c);
    return out;
}
void ColorField::remember(Color color) {
    QStringList list = QSettings().value("colors/recent").toStringList();
    const auto name = hexName(color);
    list.removeAll(name);
    list.prepend(name);
    while (list.size() > 10)
        list.removeLast();
    QSettings().setValue("colors/recent", list);
}
ColorField::ColorField(SwatchSource swatches, QWidget* parent)
    : QWidget(parent), swatches_(std::move(swatches)) {
    auto v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto top = new QHBoxLayout;
    top->setSpacing(4);
    main_ = new QToolButton(this);
    main_->setObjectName("colorSwatch");
    main_->setFixedSize(40, 22);
    main_->setToolTip("Open the colour picker (changes preview live)");
    hex_ = new QLineEdit(this);
    hex_->setObjectName("colorHex");
    hex_->setPlaceholderText("RRGGBB");
    hex_->setMaxLength(9);
    hex_->setFixedWidth(78);
    hex_->setToolTip("Hex colour: RRGGBB or RRGGBBAA, Enter to apply");
    top->addWidget(main_);
    top->addWidget(hex_);
    top->addStretch();
    v->addLayout(top);
    row_ = new QWidget(this);
    auto rowLayout = new QHBoxLayout(row_);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(2);
    v->addWidget(row_);
    connect(main_, &QToolButton::clicked, this, &ColorField::openPicker);
    connect(hex_, &QLineEdit::editingFinished, this, [this] {
        const auto c = parseHex(hex_->text());
        if (!c) {
            hex_->setText(mixed_ ? QString() : hexName(color_));
            return;
        }
        if (*c == color_ && !mixed_)
            return;
        setColor(*c);
        remember(*c);
        emit picked(*c);
    });
    refreshSwatches();
}
void ColorField::setColor(Color color, bool mixed) {
    color_ = color;
    mixed_ = mixed;
    updateSwatch();
}
void ColorField::updateSwatch() {
    main_->setStyleSheet(swatchStyle(color_, mixed_));
    if (!hex_->hasFocus())
        hex_->setText(mixed_ ? QString() : hexName(color_));
    hex_->setPlaceholderText(mixed_ ? "mixed" : "RRGGBB");
}
void ColorField::refreshSwatches() {
    auto* layout = static_cast<QHBoxLayout*>(row_->layout());
    while (auto* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    std::vector<Swatch> list = swatches_ ? swatches_() : std::vector<Swatch>{};
    for (const auto c : recentColors())
        if (std::none_of(list.begin(), list.end(), [&](const Swatch& s) { return s.color == c; }))
            list.push_back({c, "Recent"});
    int shown = 0;
    for (const auto& s : list) {
        if (++shown > 12)
            break;
        auto b = new QToolButton(row_);
        b->setFixedSize(16, 16);
        b->setStyleSheet(swatchStyle(s.color));
        b->setToolTip(s.name.isEmpty() ? "#" + hexName(s.color) : s.name + " · #" + hexName(s.color));
        b->setAccessibleName(b->toolTip());
        connect(b, &QToolButton::clicked, this, [this, c = s.color] {
            setColor(c);
            remember(c);
            emit picked(c);
        });
        layout->addWidget(b);
    }
    layout->addStretch();
    row_->setVisible(shown > 0);
}
void ColorField::openPicker() {
    QColorDialog dialog(QColor(color_.r, color_.g, color_.b, color_.a), this);
    dialog.setOption(QColorDialog::ShowAlphaChannel);
    dialog.setWindowTitle("Choose colour");
    const auto start = color_;
    connect(&dialog, &QColorDialog::currentColorChanged, this, [this](const QColor& q) {
        if (q.isValid())
            emit previewed(fromQColor(q));
    });
    if (dialog.exec() == QDialog::Accepted) {
        const auto c = fromQColor(dialog.selectedColor());
        setColor(c);
        remember(c);
        emit picked(c);
        refreshSwatches();
    } else {
        setColor(start, mixed_);
        emit cancelled();
    }
}
} // namespace kantan::app

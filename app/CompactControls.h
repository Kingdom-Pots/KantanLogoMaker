#pragma once
#include "app/ToolIcons.h"
#include <QCheckBox>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
namespace kantan::app {
class ToolOptionsStack final : public QStackedWidget {
  public:
    explicit ToolOptionsStack(QWidget* parent = nullptr) : QStackedWidget(parent) {
        connect(this, &QStackedWidget::currentChanged, this, [this] { updateGeometry(); });
    }
    QSize sizeHint() const override {
        return currentWidget() ? currentWidget()->sizeHint() : QSize();
    }
    QSize minimumSizeHint() const override {
        return currentWidget() ? currentWidget()->minimumSizeHint() : QSize();
    }
};
class IconCheckBox final : public QCheckBox {
  public:
    using QCheckBox::QCheckBox;

  protected:
    bool hitButton(const QPoint& point) const override {
        return rect().contains(point);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(isChecked() || hasFocus() ? QColor(105, 183, 211)
                            : underMouse()            ? QColor(166, 177, 192)
                                                      : QColor(59, 67, 81),
                            1));
        painter.setBrush(isChecked() ? QColor(56, 87, 107) : QColor(44, 50, 62));
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 2, 2);
        icon().paint(&painter, QRect((width() - 20) / 2, (height() - 20) / 2, 20, 20), Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }
};
inline void iconButton(QToolButton* button, const QString& icon, const QString& name) {
    button->setIcon(uiIcon(icon));
    button->setIconSize(QSize(20, 20));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setText(name);
    button->setToolTip(name);
    button->setAccessibleName(name);
    button->setMinimumSize(28, 28);
    button->setAutoRaise(true);
}
inline void iconToggle(QCheckBox* button, const QIcon& icon, const QString& name) {
    button->setText({});
    button->setIcon(icon);
    button->setIconSize(QSize(20, 20));
    button->setToolTip(name);
    button->setAccessibleName(name);
    button->setFixedSize(30, 28);
}
inline QIcon axisIcon(int axis) {
    QIcon result;
    for (int scale : {1, 2, 3}) {
        QPixmap pix(24 * scale, 24 * scale);
        pix.fill(Qt::transparent);
        pix.setDevicePixelRatio(scale);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(12, 12);
        if (axis == 1)
            p.rotate(90);
        if (axis == 2)
            p.rotate(45);
        if (axis == 3)
            p.rotate(-45);
        p.setPen(QPen(QColor(110, 205, 229), 1.4, Qt::DashLine));
        p.drawLine(QPointF(0, -10), QPointF(0, 10));
        p.setPen(QPen(QColor(225, 230, 238), 1.3));
        p.setBrush(QColor(225, 230, 238, 75));
        p.drawPolygon(QPolygonF{QPointF(-8, -5), QPointF(-3, 0), QPointF(-8, 5)});
        p.drawPolygon(QPolygonF{QPointF(8, -5), QPointF(3, 0), QPointF(8, 5)});
        p.end();
        result.addPixmap(pix);
    }
    return result;
}
inline QIcon alignmentIcon() {
    QPixmap pix(48, 48);
    pix.setDevicePixelRatio(2);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setPen(QPen(QColor(110, 205, 229), 1));
    p.drawLine(3, 3, 3, 21);
    p.setPen(QPen(QColor(225, 230, 238), 2));
    p.drawLine(6, 6, 21, 6);
    p.drawLine(6, 12, 15, 12);
    p.drawLine(6, 18, 18, 18);
    p.end();
    return QIcon(pix);
}
inline QIcon magnetIcon() {
    QPixmap pix(48, 48);
    pix.setDevicePixelRatio(2);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.moveTo(5, 4);
    path.lineTo(5, 13);
    path.cubicTo(5, 22, 19, 22, 19, 13);
    path.lineTo(19, 4);
    p.setPen(QPen(QColor(225, 230, 238), 4, Qt::SolidLine, Qt::FlatCap));
    p.drawPath(path);
    p.fillRect(3, 3, 4, 5, QColor(110, 205, 229));
    p.fillRect(17, 3, 4, 5, QColor(110, 205, 229));
    p.end();
    return QIcon(pix);
}
inline QIcon centerRegionIcon() {
    QPixmap pix(48, 48);
    pix.setDevicePixelRatio(2);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.fillRect(9, 9, 6, 6, QColor(110, 205, 229));
    p.setPen(QPen(QColor(225, 230, 238), 1));
    p.drawRect(3, 3, 18, 18);
    for (int i : {9, 15}) {
        p.drawLine(i, 3, i, 21);
        p.drawLine(3, i, 21, i);
    }
    p.end();
    return QIcon(pix);
}
inline QIcon backgroundIcon(int mode) {
    QPixmap pix(40, 40);
    pix.setDevicePixelRatio(2);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            p.fillRect(x * 5, y * 5, 5, 5,
                       mode == 1     ? QColor(Qt::white)
                       : mode == 2   ? QColor(128, 128, 128)
                       : mode == 3   ? QColor(27, 30, 37)
                       : (x + y) % 2 ? QColor(65, 73, 86)
                                     : QColor(160, 171, 185));
    p.setPen(QColor(169, 186, 204));
    p.drawRect(0, 0, 19, 19);
    p.end();
    return QIcon(pix);
}
inline QToolButton* disclosure(QVBoxLayout* layout, QWidget* contents, const QString& title,
                               bool open = false) {
    auto button = new QToolButton;
    button->setObjectName("sectionHeader");
    button->setText(title);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setCheckable(true);
    button->setChecked(open);
    button->setIcon(uiIcon(open ? "chevron_down" : "chevron_right"));
    button->setAccessibleName(title);
    button->setToolTip("Expand or collapse " + title.toLower());
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(button);
    layout->addWidget(contents);
    contents->setVisible(open);
    QObject::connect(button, &QToolButton::toggled, contents, [contents, button](bool shown) {
        contents->setVisible(shown);
        button->setIcon(uiIcon(shown ? "chevron_down" : "chevron_right"));
    });
    return button;
}
} // namespace kantan::app

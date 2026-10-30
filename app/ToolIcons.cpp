#include "app/ToolIcons.h"
#include "app/CanvasView.h"
#include <QPainter>
namespace kantan::app {
QIcon uiIcon(const QString& name) {
    QIcon icon;
    if (name == "help" || name == "eye_off") {
        for (int scale : {1, 2, 3}) {
            QPixmap pixmap(24 * scale, 24 * scale);
            pixmap.setDevicePixelRatio(scale);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            if (name == "eye_off") {
                uiIcon("eye").paint(&painter, QRect(2, 2, 20, 20));
                painter.setPen(QPen(QColor(37, 41, 50), 4, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(QPointF(4, 20), QPointF(20, 4));
                painter.setPen(QPen(QColor(166, 177, 192), 1.5, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(QPointF(4, 20), QPointF(20, 4));
            } else {
                painter.setPen(QPen(QColor(221, 226, 235), 1.5, Qt::SolidLine, Qt::RoundCap));
                painter.drawEllipse(QRectF(3, 3, 18, 18));
                painter.drawLine(QPointF(12, 11), QPointF(12, 17));
                painter.setBrush(QColor(221, 226, 235));
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(QPointF(12, 7.5), 1, 1);
            }
            painter.end();
            icon.addPixmap(pixmap);
        }
        return icon;
    }
    const QString base = ":/icons/" + name;
    icon.addFile(base + ".png");
    icon.addFile(base + "@2x.png");
    icon.addFile(base + "@3x.png");
    return icon;
}
QIcon toolIcon(Tool tool) {
    static const char* names[] = {"select", "direct_select", "pen",    "shapes", "ellipse",
                                  "text",   "image",         "picker", "hand",   "zoom"};
    return uiIcon(QString::fromLatin1(names[static_cast<int>(tool)]));
}
} // namespace kantan::app

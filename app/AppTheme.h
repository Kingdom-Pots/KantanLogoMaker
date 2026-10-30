#pragma once
#include <QApplication>
#include <QPalette>
namespace kantan::app {
inline void installAppTheme() {
    if (qApp->property("kantanTheme").toBool())
        return;
    qApp->setProperty("kantanTheme", true);
    QApplication::setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(37, 41, 50));
    palette.setColor(QPalette::WindowText, QColor(221, 226, 235));
    palette.setColor(QPalette::Base, QColor(27, 31, 39));
    palette.setColor(QPalette::AlternateBase, QColor(36, 41, 51));
    palette.setColor(QPalette::Text, QColor(221, 226, 235));
    palette.setColor(QPalette::Button, QColor(44, 50, 62));
    palette.setColor(QPalette::ButtonText, QColor(221, 226, 235));
    palette.setColor(QPalette::Light, QColor(80, 90, 105));
    palette.setColor(QPalette::Midlight, QColor(65, 73, 86));
    palette.setColor(QPalette::Mid, QColor(55, 62, 76));
    palette.setColor(QPalette::Dark, QColor(27, 30, 37));
    palette.setColor(QPalette::Shadow, QColor(15, 18, 22));
    palette.setColor(QPalette::Link, QColor(105, 183, 211));
    palette.setColor(QPalette::Highlight, QColor(53, 79, 103));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor(32, 36, 44));
    palette.setColor(QPalette::ToolTipText, QColor(221, 226, 235));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(131, 141, 156));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(131, 141, 156));
    QApplication::setPalette(palette);
}
} // namespace kantan::app

#pragma once
#include "document/Document.h"
#include <QLineEdit>
#include <QToolButton>
#include <QWidget>
#include <functional>

namespace kantan::app {
struct Swatch {
    Color color;
    QString name;
};
// A colour control without modal round-trips: main swatch (opens a live-preview picker), hex entry, and a row
// of one-click swatches (palette, colours already in the document, recent picks).
class ColorField final : public QWidget {
    Q_OBJECT
  public:
    using SwatchSource = std::function<std::vector<Swatch>()>;
    explicit ColorField(SwatchSource swatches, QWidget* parent = nullptr);
    void setColor(Color color, bool mixed = false);
    Color color() const {
        return color_;
    }
    void refreshSwatches();
    static std::vector<Color> recentColors();
    static void remember(Color color);
  signals:
    void previewed(Color color); // live while the picker is open
    void picked(Color color);    // committed
    void cancelled();            // picker closed without a choice

  private:
    void openPicker();
    void updateSwatch();
    SwatchSource swatches_;
    Color color_{0, 0, 0, 255};
    bool mixed_{};
    QToolButton* main_{};
    QLineEdit* hex_{};
    QWidget* row_{};
};
QString hexName(Color c);
std::optional<Color> parseHex(const QString& text);
} // namespace kantan::app

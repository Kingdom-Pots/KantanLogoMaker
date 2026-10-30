#pragma once
#include "app/Services.h"
#include "editing/Editor.h"
#include <QTimer>
#include <QToolButton>
#include <QWidget>

namespace kantan::app {
enum class PreviewBackground { Checker, White, Grey, Dark };
// Live renders at icon sizes, each rendered independently exactly as export would. Presentation only:
// it never modifies or dirties the document.
class SizeStrip final : public QWidget {
    Q_OBJECT
  public:
    SizeStrip(std::shared_ptr<Editor> editor, std::shared_ptr<AppServices> services,
              QWidget* parent = nullptr);
    void schedule();
    void setBackground(PreviewBackground background);
    PreviewBackground background() const {
        return background_;
    }
    const std::vector<QImage>& images() const {
        return images_;
    }
    static const std::vector<int>& sizes();
    int zoom() const {
        return zoom_;
    }
    void setZoom(int zoom);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override {
        return sizeHint();
    }

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

  private:
    void renderAll();
    std::shared_ptr<Editor> editor_;
    std::shared_ptr<AppServices> services_;
    QTimer timer_;
    std::vector<QImage> images_;
    PreviewBackground background_{PreviewBackground::Checker};
    int zoom_{1};
    QToolButton *zoomButton_{}, *backgroundButton_{};
};
} // namespace kantan::app

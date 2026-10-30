#pragma once
#include "app/Services.h"
#include <QDialog>
#include <QImage>
#include <map>

class QComboBox;

namespace kantan::app {
// View › Preview on…: the logo drawn at real pixel sizes in common places — a taskbar, a desktop, a phone
// home screen and a browser tab — on a light or dark theme. Nothing here is exported.
class MockupDialog final : public QDialog {
    Q_OBJECT
  public:
    MockupDialog(Snapshot document, std::shared_ptr<AppServices> services, QString name,
                 QWidget* parent = nullptr);
    // Rendered icon at `size` (cached). Public for tests.
    const QImage& icon(int size);
    bool dark() const;

  private:
    Snapshot document_;
    std::shared_ptr<AppServices> services_;
    QString name_;
    std::map<int, QImage> icons_;
    QComboBox* theme_{};
    QWidget* canvas_{};
};
} // namespace kantan::app

#pragma once
#include "app/Services.h"
#include <QDialog>

class QListWidget;
class QSpinBox;
class QCheckBox;

namespace kantan::app {
// The first thing a new session shows: blank artboard sizes, starter templates and recent files.
class StartDialog final : public QDialog {
    Q_OBJECT
  public:
    struct Choice {
        enum Kind { None, Blank, Template, Recent, Browse } kind{None};
        Artboard artboard;
        int templateIndex{-1};
        QString path;
    };
    explicit StartDialog(std::shared_ptr<AppServices> services, QWidget* parent = nullptr);
    Choice choice() const {
        return choice_;
    }
    static bool showAtStartup();
    static void setShowAtStartup(bool show);

  private:
    void finish(Choice choice);
    Choice choice_;
    QListWidget *sizes_{}, *templates_{}, *recent_{};
    QSpinBox *width_{}, *height_{};
    QCheckBox* showAgain_{};
};
} // namespace kantan::app

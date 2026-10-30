#pragma once
#include "app/Services.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTimer>
#include <map>

namespace kantan::app {
// File › Export: formats, sizes (+ custom W x H), padding, fit, background, source, naming, and a 1:1
// preview of every output before anything is written.
class ExportDialog final : public QDialog {
    Q_OBJECT
  public:
    ExportDialog(Snapshot document, std::vector<ObjectId> selection, std::shared_ptr<AppServices> services,
                 QString baseName, QWidget* parent = nullptr);
    ExportSettings settings() const;
    QString folder() const {
        return folder_->text();
    }
    QString baseName() const {
        return name_->text();
    }

  private:
    void updatePreview();
    void applyPreset(int index);
    void load(const ExportSettings& s); // sets every widget from `s` without triggering previews
    void updateIcoAvailability();
    Snapshot document_;
    ExportSettings base_; // fields the widgets do not show (named outputs, ICO name, preset name)
    QComboBox* preset_{};
    QLabel* presetInfo_{};
    std::vector<ObjectId> selection_;
    std::shared_ptr<AppServices> services_;
    QCheckBox *png_{}, *ico_{}, *transparent_{}, *selectionOnly_{};
    std::map<int, QCheckBox*> pngSizes_, icoSizes_;
    QListWidget* custom_{};
    QDoubleSpinBox* padding_{};
    QComboBox* fit_{};
    QLineEdit *naming_{}, *folder_{}, *name_{};
    QLabel* files_{};
    QWidget* preview_{};
    Color background_{255, 255, 255, 255};
    QTimer previewTimer_;
};
} // namespace kantan::app

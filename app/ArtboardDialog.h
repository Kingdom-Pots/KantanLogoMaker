#pragma once
#include "document/Document.h"
#include <QCheckBox>
#include <QDialog>
#include <QSpinBox>

namespace kantan::app {
// Artboard size and background (from NineSlice's CanvasSizeDialog). "Keep centred" shifts objects so the
// artwork stays in the same place relative to the artboard centre.
class ArtboardDialog final : public QDialog {
  public:
    ArtboardDialog(const Artboard& current, QWidget* parent = nullptr);
    Artboard artboard() const;
    bool keepCentred() const {
        return centred_->isChecked();
    }

  private:
    Artboard initial_;
    QSpinBox *width_{}, *height_{};
    QCheckBox *transparent_{}, *centred_{};
    Color background_;
};
} // namespace kantan::app

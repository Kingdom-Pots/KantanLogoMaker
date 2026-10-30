#pragma once
#include "app/Services.h"
#include "editing/Editor.h"
#include <QListWidget>
#include <QWidget>

namespace kantan::app {
// The project's brand colours. Click applies to the selection's fill (Shift: first outline); editing a colour
// recolours every matching style colour in one undo step.
class PalettePanel final : public QWidget {
    Q_OBJECT
  public:
    PalettePanel(std::shared_ptr<Editor> editor, QWidget* parent = nullptr);
    void refresh();
    QListWidget* list() const {
        return list_;
    }
    void applyToSelection(std::size_t index, bool outline);
    void editColor(std::size_t index);
  signals:
    void documentChanged();
    void feedback(QString);

  private:
    void report(Result r);
    std::shared_ptr<Editor> editor_;
    QListWidget* list_{};
};
} // namespace kantan::app

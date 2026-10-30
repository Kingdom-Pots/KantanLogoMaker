#pragma once
#include "app/PalettePanel.h"
#include "app/Presets.h"
#include "app/Services.h"
#include "editing/Editor.h"
#include <QLineEdit>
#include <QListWidget>
#include <QTabWidget>

namespace kantan::app {
// Presets dock: Shapes (parametric, SVG symbols, saved selections), Styles, Starter templates.
class PresetPanel final : public QTabWidget {
    Q_OBJECT
  public:
    PresetPanel(std::shared_ptr<Editor> editor, std::shared_ptr<AppServices> services, QString userFolder,
                QWidget* parent = nullptr);
    // Save the current selection as a reusable shape preset (asks for a name).
    void saveSelection();
    void reloadUserPresets();
    QListWidget* shapes() const {
        return shapes_;
    }
    QListWidget* styles() const {
        return styles_;
    }
    PalettePanel* palette() const {
        return palette_;
    }
    // Hides presets whose name does not contain `text` (case-insensitive) on every grid tab.
    void filter(const QString& text);
    QLineEdit* search() const {
        return search_;
    }
  signals:
    void documentChanged();
    void feedback(QString);
    void openTemplate(int index);

  private:
    QIcon render(const DocumentData& doc, Box source);
    void place(std::vector<Object> objects);
    void report(Result r);
    std::shared_ptr<Editor> editor_;
    std::shared_ptr<AppServices> services_;
    QString userFolder_;
    QListWidget *shapes_{}, *styles_{}, *starters_{};
    PalettePanel* palette_{};
    QLineEdit* search_{};
    int builtInShapes_{};
};
} // namespace kantan::app

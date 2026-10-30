#pragma once
#include "app/CanvasView.h"
#include "app/Inspector.h"
#include "app/ObjectPanel.h"
#include "app/PresetPanel.h"
#include "app/SizeStrip.h"
#include "app/StartDialog.h"
#include <QAction>
#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QMainWindow>
#include <QStackedWidget>
#include <QTimer>
#include <map>

namespace kantan::app {
struct IOCompletion {
    Result result;
    Snapshot document;
    QString path;
    std::uint64_t state{}, generation{};
    enum Kind { Save, Open, Export, Autosave } kind{Save};
    std::vector<std::string> warnings;
    QStringList written;
};
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(std::shared_ptr<AppServices> services, QWidget* parent = nullptr,
                        Artboard artboard = {});
    ~MainWindow() override;
    CanvasView* canvas() const {
        return canvas_;
    }
    std::shared_ptr<Editor> editor() const {
        return editor_;
    }
    ObjectPanel* objects() const {
        return objects_;
    }
    Inspector* inspector() const {
        return inspector_;
    }
    SizeStrip* sizeStrip() const {
        return strip_;
    }
    PresetPanel* presets() const {
        return presets_;
    }
    QAction* action(const QString& name) const;
    // `recovered`: an autosave from a previous session, opened as unsaved untitled work.
    void load(const QString& path, bool recovered = false);
    void saveTo(const QString& path);
    // Export with the document's saved settings into `folder` (used by tests and "Export again").
    void exportTo(const QString& folder, const QString& baseName);
    void placeImage(const QString& path, QPointF artboardCenter);
    void importSvg(const QString& path);
    void importFont(const QString& path);
    void openTemplate(int index);
    void openFile(const QString& path); // reuses this window if it is an untouched new document
    void showStart();
    void applyStart(const StartDialog::Choice& choice);
    bool untouched() const;
    bool waitForIO(int milliseconds = 10000);
    void demo();
    // Per-user folders; tests point them at temporary directories.
    static QString dataFolder(const QString& sub);

  protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

  private:
    void buildMenus();
    void buildTools();
    void buildOptions();
    void buildDocks();
    void synchronize();
    void updateSelectionActions();
    void status(const QString& message);
    bool report(Result result);
    void chooseTool(Tool tool);
    void save(bool as = false);
    void runIO(IOCompletion::Kind kind, std::function<IOCompletion()> job);
    void booleanOp(BooleanOp op);
    void pathOp(const QString& label, const std::function<std::optional<Object>(ObjectId)>& op);
    void align(Alignment alignment);
    void showContextMenu(QPoint global);
    void copy(bool cut);
    void paste();
    void applyPickedColor(QColor color);
    void autosave();
    QString documentName() const;
    std::shared_ptr<AppServices> services_;
    std::shared_ptr<Editor> editor_;
    CanvasView* canvas_{};
    ObjectPanel* objects_{};
    Inspector* inspector_{};
    PresetPanel* presets_{};
    SizeStrip* strip_{};
    QStackedWidget* options_{};
    QComboBox* shapePicker_{};
    QComboBox* alignTarget_{};
    QToolButton *arrangeOptions_{}, *combineOptions_{};
    QComboBox* pickTarget_{};
    std::map<Tool, QAction*> toolActions_;
    std::map<QString, QAction*> actions_;
    QString lastExportName_; // name used by the last export this session
    QLabel* info_{};
    QString path_;
    QByteArray defaultDockState_;
    QString recoveryPath_;
    QFutureWatcher<IOCompletion> io_;
    QTimer autosaveTimer_;
    std::uint64_t shownGeneration_{}, autosavedState_{};
    bool closeAfterSave_{}, recovering_{};
    Point duplicateOffset_{16, 16};
    std::vector<ObjectId> lastDuplicate_;
};
} // namespace kantan::app

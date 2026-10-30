#include "app/MainWindow.h"
#include "app/AppTheme.h"
#include "app/ArtboardDialog.h"
#include "app/CompactControls.h"
#include "app/ExportDialog.h"
#include "app/MockupDialog.h"
#include "app/Presets.h"
#include "app/PropertyControls.h"
#include "app/RecentFiles.h"
#include "io/ExportJob.h"
#include "io/Images.h"
#include "io/ProjectIO.h"
#include "io/Serialization.h"
#include "io/SvgImport.h"
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QWhatsThis>
#include <QtConcurrent>

namespace kantan::app {
namespace {
constexpr const char* FragmentMime = "application/x-kantanlogo-fragment";
const char* toolName(Tool t) {
    static const char* names[] = {"Select (V)",      "Direct select / nodes (A)",
                                  "Pen (P)",         "Shape (R)",
                                  "Ellipse (E)",     "Text (T)",
                                  "Place image (I)", "Eyedropper (K, or hold Alt)",
                                  "Hand (Space)",    "Zoom (Z)"};
    return names[static_cast<int>(t)];
}
QString imageKey(const Bytes& bytes) {
    return QStringLiteral("img-") +
           QCryptographicHash::hash(io::fromBytes(bytes), QCryptographicHash::Sha256).toHex().left(16);
}
} // namespace
QString MainWindow::dataFolder(const QString& sub) {
    const auto override = qEnvironmentVariable("KANTANLOGO_DATA");
    const QString root =
        override.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : override;
    return QDir(root).filePath(sub);
}
MainWindow::MainWindow(std::shared_ptr<AppServices> services, QWidget* parent, Artboard artboard)
    : QMainWindow(parent), services_(std::move(services)),
      editor_(std::make_shared<Editor>(std::move(artboard), services_->budget)) {
    setObjectName("KantanLogoMainWindow");
    installAppTheme();
    PropertyWheelGuard::install();
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAcceptDrops(true);
    resize(1360, 880);
    setMinimumSize(960, 640);
    canvas_ = new CanvasView(editor_, services_, this);
    canvas_->setObjectName("logoCanvas");
    setCentralWidget(canvas_);
    buildMenus();
    buildTools();
    buildOptions();
    buildDocks();
    defaultDockState_ = saveState();
    connect(inspector_, &Inspector::alignCanvas, this, [this](int alignment) {
        alignTarget_->setCurrentIndex(1);
        align(static_cast<Alignment>(alignment));
    });
    info_ = new QLabel(this);
    statusBar()->addPermanentWidget(info_);
    connect(canvas_, &CanvasView::feedback, this, &MainWindow::status);
    connect(canvas_, &CanvasView::changed, this, &MainWindow::synchronize);
    connect(canvas_, &CanvasView::viewChanged, this, [this] {
        const auto doc = editor_->snapshot();
        info_->setText(QStringLiteral("%1% · %2 × %3 · %4 object(s) · %5 selected")
                           .arg(canvas_->zoom() * 100, 0, 'f', 0)
                           .arg(doc->artboard.width)
                           .arg(doc->artboard.height)
                           .arg(doc->objects.size())
                           .arg(editor_->selection().size()));
    });
    connect(canvas_, &CanvasView::colorPicked, this, &MainWindow::applyPickedColor);
    connect(canvas_, &CanvasView::contextMenuRequested, this, &MainWindow::showContextMenu);
    connect(canvas_, &CanvasView::moved, this,
            [this](std::vector<ObjectId> ids, Point delta, bool afterDuplicate) {
                // Moving the copies made by the last duplicate teaches Ctrl+D the spacing to repeat.
                if (afterDuplicate) {
                    duplicateOffset_ = delta;
                    lastDuplicate_ = ids;
                } else if (!lastDuplicate_.empty() && ids == lastDuplicate_)
                    duplicateOffset_ = duplicateOffset_ + delta;
                else
                    lastDuplicate_.clear();
            });
    connect(canvas_, &CanvasView::requestTextEdit, this, [this] { inspector_->focusText(); });
    connect(canvas_, &CanvasView::requestPlaceImage, this, [this](QPointF at) {
        const auto file = QFileDialog::getOpenFileName(this, "Place image", {},
                                                       "Images (*.png *.jpg *.jpeg *.bmp *.gif *.ico)");
        if (!file.isEmpty())
            placeImage(file, at);
    });
    connect(&io_, &QFutureWatcher<IOCompletion>::finished, this, [this] {
        const auto r = io_.result();
        if (r.kind == IOCompletion::Autosave) {
            if (r.result)
                autosavedState_ = r.state;
            else
                status("Autosave failed: " + QString::fromStdString(r.result.message) + ". It will retry.");
            return;
        }
        if (!r.result) {
            status(QStringLiteral("%1: %2%3")
                       .arg(r.path, QString::fromStdString(r.result.message),
                            r.written.isEmpty() ? QString() : " · Written: " + r.written.join(", ")));
            if (r.kind == IOCompletion::Open)
                QMessageBox::warning(this, "Open", QString::fromStdString(r.result.message));
            closeAfterSave_ = false;
            return;
        }
        if (r.kind == IOCompletion::Open) {
            if (editor_->generation() != r.generation) {
                status("The document changed while reading; open the file again");
                return;
            }
            if (!report(editor_->replace(r.document, !recovering_)))
                return;
            path_ = recovering_ ? QString() : r.path;
            if (recovering_) {
                QFile::remove(r.path); // this window autosaves it again under its own name
                recovering_ = false;
            }
            canvas_->fit();
            rememberRecent(path_);
            const auto missing = services_->fonts->missingFonts(*editor_->snapshot());
            if (!missing.empty()) {
                QStringList names;
                for (const auto& m : missing)
                    names << QString::fromStdString(m);
                QMessageBox::warning(this, "Missing fonts",
                                     "These fonts are not installed and are shown with a fallback font:\n\n" +
                                         names.join("\n") +
                                         "\n\nThe project keeps the original names and re-links them "
                                         "when the fonts become available.");
            }
            status("Document opened");
        } else if (r.kind == IOCompletion::Save) {
            path_ = r.path;
            rememberRecent(path_);
            editor_->markSaved(r.state);
            status("Document saved");
        } else if (r.kind == IOCompletion::Export)
            status(r.written.isEmpty() ? "Nothing exported"
                                       : QStringLiteral("Exported %1 file(s) · %2")
                                             .arg(r.written.size())
                                             .arg(r.written.join(", ")));
        synchronize();
        if (closeAfterSave_) {
            closeAfterSave_ = false;
            close();
        }
    });
    setStyleSheet(
        "QMainWindow,QWidget { background:#252932;color:#dde2eb;font-family:'Segoe UI';font-size:12px; } "
        "QMenuBar,QMenu,QToolBar,QStatusBar {background:#20242c;} QDockWidget::title "
        "{background:#20242c;padding:3px;} "
        "QTreeWidget,QListWidget,QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox,QPlainTextEdit "
        "{background:#1b1f27;border:1px "
        "solid #373e4c;border-radius:2px;padding:2px;alternate-background-color:#242933;} "
        "QTreeWidget::item,QListWidget::item {padding:2px;} "
        "QLabel[scrubbable=\"true\"]:hover {color:#80c9de;} "
        "QLineEdit:focus,QSpinBox:focus,QDoubleSpinBox:focus,QComboBox:focus,QPlainTextEdit:focus "
        "{border:1px solid #69b7d3;} "
        "QTreeWidget::item:selected,QListWidget::item:selected {background:#354f67;} QToolButton,QPushButton "
        "{border:1px solid #3b4351;border-radius:2px;padding:3px;background:#2c323e;} "
        "QToolButton:checked,QPushButton:pressed {background:#38576b;border-color:#69b7d3;} "
        "QToolButton:hover,QPushButton:hover {background:#3c4654;} QCheckBox {spacing:5px;} "
        "QTabBar::tab {background:#2c323e;padding:4px 10px;border:1px solid #3b4351;} "
        "QTabBar::tab:selected {background:#38576b;} QToolBar {border:0;spacing:3px;} "
        "QSlider::groove:horizontal {height:3px;background:#424b59;} "
        "QSlider::handle:horizontal {width:10px;background:#a6b1c0;border:0;border-radius:1px;margin:-6px "
        "0;} "
        "QSlider::handle:horizontal:hover {background:#80c9de;} QGroupBox {border:1px solid "
        "#373e4c;margin-top:8px;padding-top:6px;} "
        "QGroupBox::title {subcontrol-origin:margin;left:6px;} "
        "QScrollBar:vertical {background:#20242c;width:10px;margin:0;} "
        "QScrollBar:horizontal {background:#20242c;height:10px;margin:0;} "
        "QScrollBar::handle {background:#566171;border-radius:4px;} "
        "QScrollBar::handle:vertical {min-height:24px;margin:1px;} "
        "QScrollBar::handle:horizontal {min-width:24px;margin:1px;} "
        "QScrollBar::handle:hover {background:#78889b;} "
        "QScrollBar::add-line,QScrollBar::sub-line {width:0;height:0;} "
        "QScrollBar::add-page,QScrollBar::sub-page {background:transparent;} "
        "QMenu::item {padding:6px 24px 6px 12px;} QMenu::item:selected {background:#354f67;} "
        "QToolTip {background:#20242c;color:#dde2eb;border:1px solid #566171;padding:5px;} "
        "QToolButton:disabled,QPushButton:disabled {color:#838d9c;background:#272c35;border-color:#333a46;} "
        "QToolButton:focus,QPushButton:focus {border-color:#69b7d3;} "
        "QLineEdit:disabled,QSpinBox:disabled,QDoubleSpinBox:disabled,QComboBox:disabled "
        "{color:#838d9c;background:#242933;border-color:#333a46;} "
        "QMenu::item:disabled {color:#838d9c;} "
        "QMenu::separator {height:1px;background:#3b4351;margin:4px 8px;} "
        "QToolButton#sectionHeader {background:transparent;border:0;border-bottom:1px solid #373e4c;"
        "border-radius:0;text-align:left;padding:3px 2px;font-weight:600;} "
        "QToolButton#sectionHeader:checked {background:transparent;color:#dde2eb;border-color:#373e4c;} "
        "QToolButton#sectionHeader:hover {background:#2c323e;} "
        "QToolButton#sectionHeader:focus {border-color:#69b7d3;} "
        "QTabWidget::pane {border:1px solid #373e4c;top:-1px;} "
        "QMainWindow::separator {background:#20242c;width:4px;height:4px;} "
        "QMainWindow::separator:hover {background:#566171;} "
        "QHeaderView::section {background:#20242c;color:#b5c0cf;border:0;border-bottom:1px solid "
        "#373e4c;padding:4px;}");
    QSettings settings;
    if (settings.contains("workspace/geometry"))
        restoreGeometry(settings.value("workspace/geometry").toByteArray());
    if (settings.contains("workspace/docks"))
        restoreState(settings.value("workspace/docks").toByteArray());
    canvas_->setShapePreset(settings.value("tools/shapePreset", 1).toInt());
    shapePicker_->setCurrentIndex(canvas_->shapePreset());
    // Crash-safe autosave of unsaved work.
    recoveryPath_ = QDir(dataFolder("recovery"))
                        .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".kantanlogo");
    autosaveTimer_.setInterval(60000);
    connect(&autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosave);
    autosaveTimer_.start();
    synchronize();
    QTimer::singleShot(0, canvas_, &CanvasView::fit);
}
MainWindow::~MainWindow() {
    io_.waitForFinished();
    QSettings settings;
    settings.setValue("workspace/geometry", saveGeometry());
    settings.setValue("workspace/docks", saveState());
    settings.setValue("tools/shapePreset", canvas_->shapePreset());
    QFile::remove(recoveryPath_);
}
QAction* MainWindow::action(const QString& name) const {
    const auto found = actions_.find(name);
    return found == actions_.end() ? nullptr : found->second;
}
void MainWindow::status(const QString& message) {
    statusBar()->showMessage(message, 10000);
}
bool MainWindow::report(Result r) {
    if (!r || !r.message.empty())
        status(QString::fromStdString(r.message));
    return static_cast<bool>(r);
}
QString MainWindow::documentName() const {
    return path_.isEmpty() ? QStringLiteral("Untitled logo") : QFileInfo(path_).completeBaseName();
}
void MainWindow::synchronize() {
    shownGeneration_ = editor_->generation();
    canvas_->refresh();
    objects_->refresh();
    inspector_->refresh();
    presets_->palette()->refresh();
    strip_->schedule();
    setWindowTitle(documentName() + (editor_->dirty() ? "*" : "") + " — Kantan LogoMaker");
    const auto undo = editor_->undoLabel(), redo = editor_->redoLabel();
    action("undo")->setEnabled(editor_->canUndo());
    action("undo")->setText(undo.empty() ? "Undo" : "Undo " + QString::fromStdString(undo));
    action("redo")->setEnabled(editor_->canRedo());
    action("redo")->setText(redo.empty() ? "Redo" : "Redo " + QString::fromStdString(redo));
    updateSelectionActions();
    emit canvas_->viewChanged();
}
void MainWindow::updateSelectionActions() {
    const bool any = !editor_->selection().empty();
    const auto doc = editor_->snapshot();
    const bool editable = any && !editor_->busy() &&
                          std::none_of(editor_->selection().begin(), editor_->selection().end(),
                                       [&](ObjectId id) { return doc->effectiveLocked(id); });
    if (arrangeOptions_)
        arrangeOptions_->setEnabled(editable);
    if (combineOptions_)
        combineOptions_->setEnabled(editable && editor_->selection().size() >= 2);
    for (const auto* name :
         {"duplicate", "delete", "group", "copy", "cut", "front", "back", "forward", "backward"})
        action(name)->setEnabled(any);
}
void MainWindow::chooseTool(Tool tool) {
    canvas_->setTool(tool);
    toolActions_[tool]->setChecked(true);
    options_->setCurrentIndex(static_cast<int>(tool));
    status(toolName(tool));
    canvas_->setFocus();
}
void MainWindow::buildTools() {
    auto bar = new QToolBar("Tools", this);
    bar->setObjectName("toolRail");
    bar->setOrientation(Qt::Vertical);
    bar->setMovable(false);
    bar->setIconSize(QSize(20, 20));
    addToolBar(Qt::LeftToolBarArea, bar);
    auto group = new QActionGroup(this);
    const std::pair<Tool, QKeySequence> tools[] = {{Tool::Select, Qt::Key_V},  {Tool::Direct, Qt::Key_A},
                                                   {Tool::Pen, Qt::Key_P},     {Tool::Shape, Qt::Key_R},
                                                   {Tool::Ellipse, Qt::Key_E}, {Tool::Text, Qt::Key_T},
                                                   {Tool::Image, Qt::Key_I},   {Tool::Eyedropper, Qt::Key_K},
                                                   {Tool::Hand, Qt::Key_H},    {Tool::Zoom, Qt::Key_Z}};
    for (const auto& [tool, key] : tools) {
        auto a = bar->addAction(toolIcon(tool), toolName(tool));
        a->setCheckable(true);
        a->setShortcut(key);
        a->setShortcutContext(Qt::WindowShortcut);
        a->setToolTip(toolName(tool));
        group->addAction(a);
        toolActions_[tool] = a;
        connect(a, &QAction::triggered, this, [this, chosen = tool] { chooseTool(chosen); });
    }
    toolActions_[Tool::Select]->setChecked(true);
    // Shape tool: preset menu on the rail button; the last used preset is remembered.
    auto menu = new QMenu(this);
    const auto& presets = shapePresets();
    for (std::size_t i = 0; i < presets.size(); ++i)
        menu->addAction(presets[i].name, this, [this, i] {
            canvas_->setShapePreset(static_cast<int>(i));
            shapePicker_->setCurrentIndex(static_cast<int>(i));
            chooseTool(Tool::Shape);
        });
    if (auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(toolActions_[Tool::Shape]))) {
        button->setMenu(menu);
        button->setPopupMode(QToolButton::DelayedPopup);
    }
}
void MainWindow::buildOptions() {
    auto bar = new QToolBar("Tool options", this);
    bar->setObjectName("toolOptions");
    bar->setMovable(false);
    addToolBar(Qt::TopToolBarArea, bar);
    options_ = new ToolOptionsStack(bar);
    bar->addWidget(options_);
    const auto page = [this](const std::function<void(QHBoxLayout*)>& fill) {
        auto w = new QWidget;
        auto h = new QHBoxLayout(w);
        h->setContentsMargins(4, 2, 4, 2);
        h->setSpacing(4);
        fill(h);
        h->addStretch();
        options_->addWidget(w);
    };
    const auto hint = [](const QString& text) {
        auto l = new QToolButton;
        l->setIcon(uiIcon("help"));
        l->setIconSize(QSize(20, 20));
        l->setToolButtonStyle(Qt::ToolButtonIconOnly);
        l->setMinimumSize(28, 28);
        l->setAutoRaise(true);
        l->setAccessibleName("Tool help");
        l->setAccessibleDescription(text);
        l->setToolTip(text);
        QObject::connect(l, &QToolButton::clicked, l, [l, text] {
            QWhatsThis::showText(l->mapToGlobal(QPoint(0, l->height())), text, l);
        });
        return l;
    };
    const auto button = [this](QHBoxLayout* h, const QString& icon, const QString& tip,
                               const std::function<void()>& fn) {
        auto b = new QToolButton;
        iconButton(b, icon, tip);
        h->addWidget(b);
        connect(b, &QToolButton::clicked, this, fn);
    };
    // Select: frequent canvas alignment stays beside Transform; advanced actions live in menus.
    page([&](QHBoxLayout* h) {
        h->parentWidget()->setObjectName("selectionOptions");
        h->addWidget(new QLabel("Align to"));
        alignTarget_ = new QComboBox;
        alignTarget_->addItems({"Selection", "Canvas (as arrangement)", "Canvas (each object)"});
        alignTarget_->setCurrentIndex(1);
        alignTarget_->setAccessibleName("Alignment reference");
        alignTarget_->setToolTip("Reference for the Arrange menu. Inspector Canvas buttons always align the "
                                 "arrangement to the canvas.");
        h->addWidget(alignTarget_);
        auto arrange = new QToolButton;
        arrangeOptions_ = arrange;
        arrange->setObjectName("arrangeOptions");
        arrange->setText("Arrange");
        arrange->setToolButtonStyle(Qt::ToolButtonTextOnly);
        arrange->setMinimumHeight(28);
        arrange->setAccessibleName("Arrange selection");
        arrange->setPopupMode(QToolButton::InstantPopup);
        auto menu = new QMenu(arrange);
        const std::pair<const char*, Alignment> alignments[] = {
            {"Align left", Alignment::Left},           {"Centre horizontally", Alignment::CenterX},
            {"Align right", Alignment::Right},         {"Align top", Alignment::Top},
            {"Centre vertically", Alignment::CenterY}, {"Align bottom", Alignment::Bottom}};
        for (const auto& [label, alignment] : alignments)
            menu->addAction(label, this, [this, alignment] { align(alignment); });
        menu->addSeparator();
        for (const bool horizontal : {true, false}) {
            auto item = menu->addAction(
                horizontal ? "Distribute horizontally" : "Distribute vertically", this, [this, horizontal] {
                    report(
                        editor_->update("Distribute", distributed(*editor_->snapshot(), editor_->selection(),
                                                                  horizontal, services_->render())));
                    synchronize();
                });
            connect(menu, &QMenu::aboutToShow, item,
                    [this, item] { item->setEnabled(editor_->selection().size() >= 3); });
        }
        menu->addSeparator();
        for (const auto* name : {"front", "forward", "backward", "back"})
            menu->addAction(action(name));
        arrange->setMenu(menu);
        h->addWidget(arrange);
        auto combine = new QToolButton;
        combineOptions_ = combine;
        combine->setObjectName("combineOptions");
        combine->setText("Combine");
        combine->setToolButtonStyle(Qt::ToolButtonTextOnly);
        combine->setMinimumHeight(28);
        combine->setAccessibleName("Combine selected shapes");
        combine->setPopupMode(QToolButton::InstantPopup);
        auto operations = new QMenu(combine);
        for (const auto* name : {"union", "subtract", "intersect", "exclude"})
            operations->addAction(action(name));
        combine->setMenu(operations);
        h->addWidget(combine);
        h->addWidget(hint("Shift: uniform / 15 degrees · Alt: from centre · Ctrl-click: inside groups"));
    });
    // Direct select.
    page([&](QHBoxLayout* h) {
        h->addWidget(hint("Drag nodes and handles · Alt-click a segment to add a node · double-click toggles "
                          "corner/smooth"));
        button(h, "delete", "Delete node", [this] {
            report(canvas_->deleteNode());
            synchronize();
        });
    });
    // Pen.
    page([&](QHBoxLayout* h) {
        h->addWidget(hint("Click for corners, drag for curves · click the first point to close · Enter "
                          "finishes an open line"));
        button(h, "checkmark", "Finish line", [this] { canvas_->finishPen(false); });
        button(h, "dismiss", "Cancel", [this] { canvas_->cancelGesture(); });
    });
    // Shape.
    page([&](QHBoxLayout* h) {
        h->addWidget(new QLabel("Preset"));
        shapePicker_ = new QComboBox;
        shapePicker_->setObjectName("shapePicker");
        for (const auto& p : shapePresets())
            shapePicker_->addItem(p.name);
        h->addWidget(shapePicker_);
        connect(shapePicker_, &QComboBox::currentIndexChanged, canvas_, &CanvasView::setShapePreset);
        h->addWidget(
            hint("Drag to draw · Shift: equal sides · Alt: from centre · click for the default size"));
    });
    page([&](QHBoxLayout* h) {
        h->addWidget(hint("Drag to draw an ellipse · Shift: circle · Alt: from centre"));
    });
    page([&](QHBoxLayout* h) {
        h->addWidget(hint("Click to add text and type on the canvas · Enter finishes · Escape cancels"));
    });
    page([&](QHBoxLayout* h) { h->addWidget(hint("Click where the image should go")); });
    page([&](QHBoxLayout* h) {
        h->addWidget(new QLabel("Apply to"));
        pickTarget_ = new QComboBox;
        pickTarget_->addItems({"Fill", "Outline 1", "New shapes"});
        h->addWidget(pickTarget_);
        h->addWidget(hint("Click the artwork to sample a colour"));
    });
    page([&](QHBoxLayout* h) { h->addWidget(hint("Drag to pan · the middle button pans with any tool")); });
    page([&](QHBoxLayout* h) {
        h->addWidget(hint("Click to zoom in · Alt-click to zoom out · Ctrl+0 fits the artboard"));
    });
}
void MainWindow::buildDocks() {
    objects_ = new ObjectPanel(editor_, services_, this);
    objects_->setMinimumHeight(170);
    inspector_ = new Inspector(editor_, services_, this);
    presets_ = new PresetPanel(editor_, services_, dataFolder("presets"), this);
    strip_ = new SizeStrip(editor_, services_, this);
    const auto dock = [this](QWidget* w, const QString& title, const QString& name, Qt::DockWidgetArea area) {
        auto d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable |
                       QDockWidget::DockWidgetFloatable);
        addDockWidget(area, d);
        return d;
    };
    auto o = dock(objects_, "Layers", "objectsDock", Qt::RightDockWidgetArea);
    auto i = dock(inspector_, "Inspector", "inspectorDock", Qt::RightDockWidgetArea);
    // Search sits above the tabs so the tab names keep their room in a narrow dock.
    auto presetBox = new QWidget(this);
    auto presetLayout = new QVBoxLayout(presetBox);
    presetLayout->setContentsMargins(4, 2, 4, 0);
    presetLayout->setSpacing(2);
    presetLayout->addWidget(presets_->search());
    presetLayout->addWidget(presets_, 1);
    auto p = dock(presetBox, "Presets", "presetsDock", Qt::RightDockWidgetArea);
    auto s = dock(strip_, "Size preview", "sizeDock", Qt::BottomDockWidgetArea);
    resizeDocks({o, i, p}, {200, 420, 220}, Qt::Vertical);
    resizeDocks({o}, {300}, Qt::Horizontal);
    s->setMaximumHeight(strip_->sizeHint().height() + 40);
    resizeDocks({s}, {strip_->sizeHint().height()}, Qt::Vertical);
    auto view = menuBar()->findChild<QMenu*>("viewMenu");
    if (view) {
        view->addSeparator();
        for (auto* d : {o, i, p, s})
            view->addAction(d->toggleViewAction());
    }
    connect(objects_, &ObjectPanel::feedback, this, &MainWindow::status);
    connect(inspector_, &Inspector::feedback, this, &MainWindow::status);
    connect(presets_, &PresetPanel::feedback, this, &MainWindow::status);
    connect(objects_, &ObjectPanel::documentChanged, this, &MainWindow::synchronize);
    connect(objects_, &ObjectPanel::contextMenuRequested, this, &MainWindow::showContextMenu);
    connect(objects_, &ObjectPanel::selectionChanged, this, [this] {
        canvas_->update();
        objects_->refresh();
        inspector_->refresh();
        updateSelectionActions();
        emit canvas_->viewChanged();
    });
    connect(inspector_, &Inspector::documentChanged, this, &MainWindow::synchronize);
    connect(presets_, &PresetPanel::documentChanged, this, &MainWindow::synchronize);
    connect(presets_, &PresetPanel::openTemplate, this, &MainWindow::openTemplate);
}
void MainWindow::buildMenus() {
    const auto add = [this](QMenu* menu, const QString& name, const QString& text, QKeySequence shortcut,
                            const std::function<void()>& fn) {
        auto a = menu->addAction(text);
        a->setShortcut(shortcut);
        a->setObjectName(name);
        connect(a, &QAction::triggered, this, fn);
        actions_[name] = a;
        return a;
    };
    auto file = menuBar()->addMenu("File");
    add(file, "new", "New logo…", QKeySequence::New, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle("New logo");
        auto form = new QFormLayout(&dialog);
        auto w = new QSpinBox, h = new QSpinBox;
        for (auto* s : {w, h}) {
            s->setRange(1, static_cast<int>(MaxArtboard));
            s->setValue(512);
        }
        form->addRow("Width", w);
        form->addRow("Height", h);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        Artboard a;
        a.width = w->value();
        a.height = h->value();
        auto window = new MainWindow(services_, nullptr, a);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->show();
    });
    auto templates = file->addMenu("New from template");
    const auto list = starterTemplates(io::FontLibrary::fallbackFamily());
    for (std::size_t i = 0; i < list.size(); ++i)
        templates->addAction(list[i].name, this, [this, i] { openTemplate(static_cast<int>(i)); });
    add(file, "open", "Open…", QKeySequence::Open, [this] {
        const auto p = QFileDialog::getOpenFileName(this, "Open logo", {}, "Kantan LogoMaker (*.kantanlogo)");
        if (!p.isEmpty())
            openFile(p);
    });
    auto recent = file->addMenu("Open recent");
    recent->setObjectName("recentMenu");
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        const auto files = recentFiles();
        for (int i = 0; i < files.size(); ++i) {
            auto a = recent->addAction(
                QStringLiteral("&%1  %2").arg((i + 1) % 10).arg(QFileInfo(files[i]).fileName()));
            a->setToolTip(QDir::toNativeSeparators(files[i]));
            a->setStatusTip(QDir::toNativeSeparators(files[i]));
            connect(a, &QAction::triggered, this, [this, path = files[i]] { openFile(path); });
        }
        if (files.isEmpty())
            recent->addAction("No recent logos")->setEnabled(false);
        recent->addSeparator();
        recent->addAction("Clear list", this, [] { clearRecent(); })->setEnabled(!files.isEmpty());
    });
    add(file, "start", "Start screen…", {}, [this] { showStart(); });
    add(file, "save", "Save", QKeySequence::Save, [this] { save(); });
    add(file, "saveAs", "Save as…", QKeySequence::SaveAs, [this] { save(true); });
    file->addSeparator();
    add(file, "importSvg", "Import SVG…", {}, [this] {
        const auto p = QFileDialog::getOpenFileName(this, "Import SVG", {}, "SVG (*.svg)");
        if (!p.isEmpty())
            importSvg(p);
    });
    add(file, "placeImage", "Place image…", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), [this] {
        const auto p = QFileDialog::getOpenFileName(this, "Place image", {},
                                                    "Images (*.png *.jpg *.jpeg *.bmp *.gif *.ico)");
        const auto& a = editor_->snapshot()->artboard;
        if (!p.isEmpty())
            placeImage(p, QPointF(a.width / 2, a.height / 2));
    });
    add(file, "importFont", "Import font…", {}, [this] {
        const auto p = QFileDialog::getOpenFileName(this, "Import font", {}, "Fonts (*.ttf *.otf *.ttc)");
        if (!p.isEmpty())
            importFont(p);
    });
    file->addSeparator();
    add(file, "export", "Export…", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), [this] {
        if (io_.isRunning()) {
            status("Wait for the current file operation to finish");
            return;
        }
        ExportDialog dialog(editor_->snapshot(), editor_->selection(), services_, documentName(), this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        auto settings = dialog.settings();
        editor_->setExportSettings(settings); // remembered in the project
        lastExportName_ = dialog.baseName();
        exportTo(dialog.folder(), dialog.baseName());
    });
    add(file, "exportAgain", "Export again", QKeySequence(Qt::CTRL | Qt::Key_E), [this] {
        // Same settings, folder and name as last time; falls back to the dialog when there is no usable
        // folder.
        const auto& s = editor_->snapshot()->exportSettings;
        const auto folder = QString::fromStdString(s.folder);
        const auto name = lastExportName_.isEmpty() ? documentName() : lastExportName_;
        if (io_.isRunning()) {
            status("Wait for the current file operation to finish");
            return;
        }
        if (folder.isEmpty() || !QDir(folder).exists() || !io::planExport(s, folder, name).result) {
            actions_["export"]->trigger();
            return;
        }
        exportTo(folder, name);
    });
    actions_["exportAgain"]->setToolTip("Export with the last settings, folder and name (Ctrl+E)");
    file->addSeparator();
    add(file, "close", "Close", QKeySequence::Close, [this] { close(); });
    auto edit = menuBar()->addMenu("Edit");
    add(edit, "undo", "Undo", QKeySequence::Undo, [this] {
        canvas_->cancelGesture();
        report(editor_->undo());
        synchronize();
    });
    add(edit, "redo", "Redo", QKeySequence(Qt::CTRL | Qt::Key_Y), [this] {
        report(editor_->redo());
        synchronize();
    });
    action("redo")->setShortcuts(
        {QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    edit->addSeparator();
    add(edit, "cut", "Cut", QKeySequence::Cut, [this] { copy(true); });
    add(edit, "copy", "Copy", QKeySequence::Copy, [this] { copy(false); });
    add(edit, "paste", "Paste", QKeySequence::Paste, [this] { paste(); });
    add(edit, "duplicate", "Duplicate (repeats the last spacing)", QKeySequence(Qt::CTRL | Qt::Key_D),
        [this] {
            // After moving a fresh copy, Ctrl+D repeats that step: rows of copies in a few keystrokes.
            const auto selection = editor_->selection();
            const bool repeating = !lastDuplicate_.empty() && selection == lastDuplicate_;
            if (!repeating)
                duplicateOffset_ = {16, 16};
            if (report(editor_->duplicate(selection, duplicateOffset_)))
                lastDuplicate_ = editor_->created();
            synchronize();
        });
    add(edit, "delete", "Delete", QKeySequence::Delete, [this] {
        report(editor_->remove(editor_->selection()));
        synchronize();
    });
    action("delete")->setShortcutContext(Qt::WidgetShortcut); // the canvas handles Delete itself
    edit->addSeparator();
    add(edit, "selectAll", "Select all", QKeySequence::SelectAll, [this] {
        std::vector<ObjectId> ids;
        for (const auto& o : editor_->snapshot()->objects)
            if (!o->parent && !o->locked && o->visible)
                ids.push_back(o->id);
        editor_->select(ids);
        synchronize();
    });
    add(edit, "deselect", "Deselect", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), [this] {
        editor_->select({});
        synchronize();
    });
    auto object = menuBar()->addMenu("Object");
    add(object, "group", "Group", QKeySequence(Qt::CTRL | Qt::Key_G), [this] {
        report(editor_->group(editor_->selection(), ObjectKind::Group));
        synchronize();
    });
    add(object, "ungroup", "Ungroup", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), [this] {
        report(editor_->ungroup(editor_->selection()));
        synchronize();
    });
    add(object, "clip", "Clip to bottom shape", QKeySequence(Qt::CTRL | Qt::Key_7), [this] {
        report(editor_->group(editor_->selection(), ObjectKind::Clip));
        synchronize();
    });
    object->addSeparator();
    const auto toggle = [this](const QString& label, const std::function<void(Object&)>& fn) {
        const auto doc = editor_->snapshot();
        std::vector<Object> changed;
        for (const auto id : editor_->selection())
            if (const auto* o = doc->find(id)) {
                Object copy = *o;
                fn(copy);
                changed.push_back(std::move(copy));
            }
        report(editor_->update(label.toStdString(), std::move(changed)));
        synchronize();
    };
    add(object, "lock", "Lock", QKeySequence(Qt::CTRL | Qt::Key_2),
        [toggle] { toggle("Lock", [](Object& o) { o.locked = true; }); });
    add(object, "unlockAll", "Unlock all", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_2), [this] {
        std::vector<Object> changed;
        for (const auto& o : editor_->snapshot()->objects)
            if (o->locked) {
                Object copy = *o;
                copy.locked = false;
                changed.push_back(std::move(copy));
            }
        report(editor_->update("Unlock all", std::move(changed)));
        synchronize();
    });
    add(object, "hide", "Hide", QKeySequence(Qt::CTRL | Qt::Key_3),
        [toggle] { toggle("Hide", [](Object& o) { o.visible = false; }); });
    add(object, "showAll", "Show all", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_3), [this] {
        std::vector<Object> changed;
        for (const auto& o : editor_->snapshot()->objects)
            if (!o->visible) {
                Object copy = *o;
                copy.visible = true;
                changed.push_back(std::move(copy));
            }
        report(editor_->update("Show all", std::move(changed)));
        synchronize();
    });
    object->addSeparator();
    add(object, "radialRepeat", "Radial repeat…", {}, [this] {
        const auto* o =
            editor_->selection().empty() ? nullptr : editor_->snapshot()->find(editor_->selection().front());
        if (!o)
            return;
        bool ok = false;
        const int count = QInputDialog::getInt(this, "Radial repeat", "Copies around the artboard centre",
                                               std::max(6, o->repeat.count), 1, 360, 1, &ok);
        if (!ok)
            return;
        const auto& a = editor_->snapshot()->artboard;
        Object copy = *o;
        const Affine parent = editor_->snapshot()->parentMatrix(o->id);
        const Point pivot = parent.invertible() ? parent.inverted().map({a.width / 2, a.height / 2})
                                                : Point{a.width / 2, a.height / 2};
        copy.repeat = {count, pivot - o->transform.position};
        report(editor_->update("Radial repeat", {copy}));
        synchronize();
    });
    add(object, "savePreset", "Save selection as preset…", {}, [this] { presets_->saveSelection(); });
    add(object, "artboard", "Artboard…", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C), [this] {
        const auto doc = editor_->snapshot();
        ArtboardDialog dialog(doc->artboard, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const auto next = dialog.artboard();
        if (dialog.keepCentred() &&
            (next.width != doc->artboard.width || next.height != doc->artboard.height)) {
            // Artboard and artwork shift together as one undo step: move objects, then resize.
            std::vector<ObjectId> roots;
            for (const auto& o : doc->objects)
                if (!o->parent)
                    roots.push_back(o->id);
            const Point delta{(next.width - doc->artboard.width) / 2,
                              (next.height - doc->artboard.height) / 2};
            if (!report(editor_->update("Move artwork", translated(*doc, roots, delta)))) {
                status("Unlock objects to keep them centred, or untick “Keep artwork centred”");
                return;
            }
        }
        report(editor_->setArtboard(next));
        canvas_->fit();
        synchronize();
    });
    auto textMenu = menuBar()->addMenu("Text");
    add(textMenu, "addText", "Add text", {}, [this] { chooseTool(Tool::Text); });
    textMenu->addAction(action("importFont"));
    add(textMenu, "projectFonts", "Project fonts…", {}, [this] {
        const auto doc = editor_->snapshot();
        if (doc->fonts.empty()) {
            status("This project has no imported fonts; system fonts are referenced by name");
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle("Project fonts");
        auto form = new QFormLayout(&dialog);
        auto note =
            new QLabel("Embedded fonts travel with the project so it opens on other computers. "
                       "You are responsible for each font's licence; untick fonts you may not share.");
        note->setWordWrap(true);
        form->addRow(note);
        std::vector<std::pair<std::string, QCheckBox*>> boxes;
        for (const auto& [key, font] : doc->fonts) {
            auto box = new QCheckBox("Embed");
            box->setChecked(font.embed);
            box->setEnabled(static_cast<bool>(font.data));
            const auto missing = services_->fonts->resolve({font.family, key}, *doc).isEmpty();
            form->addRow(QString::fromStdString(font.family) + (missing ? " (not installed)" : ""), box);
            boxes.push_back({key, box});
        }
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        for (const auto& [key, box] : boxes)
            report(editor_->setFontEmbedding(key, box->isChecked()));
        synchronize();
    });
    auto path = menuBar()->addMenu("Path");
    add(path, "union", "Union", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_U),
        [this] { booleanOp(BooleanOp::Union); });
    add(path, "subtract", "Subtract", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S),
        [this] { booleanOp(BooleanOp::Subtract); });
    add(path, "intersect", "Intersect", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I),
        [this] { booleanOp(BooleanOp::Intersect); });
    add(path, "exclude", "Exclude", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_X),
        [this] { booleanOp(BooleanOp::Exclude); });
    add(path, "flatten", "Flatten boolean", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F), [this] {
        pathOp("Flatten boolean", [this](ObjectId id) {
            return flattenedBoolean(*editor_->snapshot(), id, services_->render());
        });
    });
    path->addSeparator();
    add(path, "convert", "Convert to path", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), [this] {
        report(editor_->replaceSubtrees(
            "Convert to path",
            convertedToPath(*editor_->snapshot(), editor_->selection(), services_->render())));
        synchronize();
    });
    add(path, "offset", "Offset path…", {}, [this] {
        bool ok = false;
        const double d = QInputDialog::getDouble(this, "Offset path", "Distance (negative shrinks)", 8, -1000,
                                                 1000, 1, &ok);
        if (ok)
            pathOp("Offset path", [this, d](ObjectId id) {
                return offsetObject(*editor_->snapshot(), id, d, JoinStyle::Round, services_->render());
            });
    });
    add(path, "outlineStroke", "Outline stroke", {}, [this] {
        const auto doc = editor_->snapshot();
        std::vector<std::pair<Object, Object>> work; // (original without its outline, stroke shape)
        for (const auto id : editor_->selection()) {
            auto s = outlineStrokeObject(*doc, id, services_->render());
            if (!s)
                continue;
            Object original = *doc->find(id);
            auto& outlines = original.style.outlines;
            outlines.erase(std::find_if(outlines.begin(), outlines.end(),
                                        [](const Outline& o) { return o.enabled && o.width > 0; }));
            work.push_back({std::move(original), std::move(*s)});
        }
        if (work.empty()) {
            status("Select objects with an outline");
            return;
        }
        std::vector<Object> stripped;
        for (const auto& [original, stroke] : work)
            stripped.push_back(original);
        report(editor_->update("Outline stroke", std::move(stripped)));
        // The stroke shape goes directly above (outside outlines) or on top of its object.
        for (const auto& [original, stroke] : work)
            report(editor_->add({stroke}, original.parent, original.id));
        synchronize();
    });
    add(path, "simplify", "Simplify…", {}, [this] {
        bool ok = false;
        const double t = QInputDialog::getDouble(this, "Simplify", "Tolerance (units)", .5, .01, 50, 2, &ok);
        if (ok)
            pathOp("Simplify", [this, t](ObjectId id) {
                return simplifiedObject(*editor_->snapshot(), id, t, services_->render());
            });
    });
    add(path, "reverse", "Reverse direction", {}, [this] {
        pathOp("Reverse direction", [this](ObjectId id) { return reversedObject(*editor_->snapshot(), id); });
    });
    add(path, "break", "Break at selected node", {}, [this] {
        const auto node = canvas_->selectedNode();
        if (!node) {
            status("Select a node with the Direct select tool (A)");
            return;
        }
        pathOp("Break path", [this, node](ObjectId id) {
            return breakAtNode(*editor_->snapshot(), id, node->subpath, node->node);
        });
    });
    add(path, "join", "Close open paths", {}, [this] {
        pathOp("Close path", [this](ObjectId id) { return closedPaths(*editor_->snapshot(), id); });
    });
    auto effects = menuBar()->addMenu("Effects");
    for (std::size_t i = 0; i < stylePresets().size(); ++i)
        effects->addAction(stylePresets()[i].name, this, [this, i] {
            const auto doc = editor_->snapshot();
            std::vector<Object> changed;
            for (const auto id : editor_->selection())
                if (const auto* o = doc->find(id)) {
                    Object copy = *o;
                    copy.style = stylePresets()[i].style;
                    changed.push_back(std::move(copy));
                }
            report(editor_->update("Apply style", std::move(changed)));
            synchronize();
        });
    effects->addSeparator();
    static Style copiedStyle;
    add(effects, "copyStyle", "Copy style", QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_C), [this] {
        if (const auto* o = editor_->selection().empty()
                                ? nullptr
                                : editor_->snapshot()->find(editor_->selection().front())) {
            copiedStyle = o->style;
            status("Style copied");
        }
    });
    add(effects, "pasteStyle", "Paste style", QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_V),
        [this] {
            const auto doc = editor_->snapshot();
            std::vector<Object> changed;
            for (const auto id : editor_->selection())
                if (const auto* o = doc->find(id)) {
                    Object copy = *o;
                    copy.style = copiedStyle;
                    changed.push_back(std::move(copy));
                }
            report(editor_->update("Paste style", std::move(changed)));
            synchronize();
        });
    add(effects, "clearEffects", "Clear effects", {}, [this] {
        const auto doc = editor_->snapshot();
        std::vector<Object> changed;
        for (const auto id : editor_->selection())
            if (const auto* o = doc->find(id)) {
                Object copy = *o;
                copy.style.effects = {};
                changed.push_back(std::move(copy));
            }
        report(editor_->update("Clear effects", std::move(changed)));
        synchronize();
    });
    auto arrange = menuBar()->addMenu("Arrange");
    add(arrange, "front", "Bring to front", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight),
        [this] {
            report(editor_->arrange(editor_->selection(), Arrange::BringToFront));
            synchronize();
        });
    add(arrange, "forward", "Bring forward", QKeySequence(Qt::CTRL | Qt::Key_BracketRight), [this] {
        report(editor_->arrange(editor_->selection(), Arrange::BringForward));
        synchronize();
    });
    add(arrange, "backward", "Send backward", QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), [this] {
        report(editor_->arrange(editor_->selection(), Arrange::SendBackward));
        synchronize();
    });
    add(arrange, "back", "Send to back", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft), [this] {
        report(editor_->arrange(editor_->selection(), Arrange::SendToBack));
        synchronize();
    });
    arrange->addSeparator();
    auto alignMenu = arrange->addMenu("Align");
    const std::pair<const char*, Alignment> alignments[] = {
        {"Left", Alignment::Left}, {"Centre", Alignment::CenterX}, {"Right", Alignment::Right},
        {"Top", Alignment::Top},   {"Middle", Alignment::CenterY}, {"Bottom", Alignment::Bottom}};
    for (const auto& [label, a] : alignments)
        alignMenu->addAction(label, this, [this, how = a] { align(how); });
    add(arrange, "flipH", "Flip horizontal", QKeySequence(Qt::SHIFT | Qt::Key_H), [this] {
        report(editor_->update("Flip horizontal", flipped(*editor_->snapshot(), editor_->selection(), true,
                                                          services_->render())));
        synchronize();
    });
    add(arrange, "flipV", "Flip vertical", QKeySequence(Qt::SHIFT | Qt::Key_V), [this] {
        report(editor_->update("Flip vertical", flipped(*editor_->snapshot(), editor_->selection(), false,
                                                        services_->render())));
        synchronize();
    });
    auto view = menuBar()->addMenu("View");
    view->setObjectName("viewMenu");
    add(view, "zoomIn", "Zoom in", QKeySequence::ZoomIn, [this] { canvas_->zoomBy(1.25); });
    add(view, "zoomOut", "Zoom out", QKeySequence::ZoomOut, [this] { canvas_->zoomBy(1 / 1.25); });
    add(view, "fit", "Fit artboard", QKeySequence(Qt::CTRL | Qt::Key_0), [this] { canvas_->fit(); });
    add(view, "actual", "Actual size", QKeySequence(Qt::CTRL | Qt::Key_1),
        [this] { canvas_->zoomAt(1, QPointF(canvas_->width() / 2., canvas_->height() / 2.)); });
    add(view, "fitSelection", "Zoom to selection", QKeySequence(Qt::CTRL | Qt::Key_2),
        [this] { canvas_->fitSelection(); });
    add(view, "resetWorkspace", "Reset workspace layout", {}, [this] {
        restoreState(defaultDockState_);
        resize(1360, 880);
        canvas_->fit();
    });
    view->addSeparator();
    auto snapping = view->addMenu("Snapping");
    const auto snapToggle = [this, snapping](const QString& text, bool SnapSettings::*field,
                                             QKeySequence key = {}) {
        auto a = snapping->addAction(text);
        a->setCheckable(true);
        a->setChecked(canvas_->snapSettings().*field);
        a->setShortcut(key);
        connect(a, &QAction::toggled, this, [this, field](bool on) {
            auto s = canvas_->snapSettings();
            s.*field = on;
            canvas_->setSnapSettings(s);
        });
    };
    snapToggle("Snap", &SnapSettings::enabled, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Semicolon));
    snapToggle("Whole units", &SnapSettings::pixels);
    snapToggle("Artboard edges and centre", &SnapSettings::artboard);
    snapToggle("Other objects", &SnapSettings::objects);
    snapToggle("Guides", &SnapSettings::guides);
    add(view, "mockups", "Preview on…", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P), [this] {
        MockupDialog dialog(editor_->snapshot(), services_, documentName(), this);
        dialog.exec();
    });
    auto zones = view->addMenu("Safe zone");
    zones->setObjectName("safeZoneMenu");
    auto zoneGroup = new QActionGroup(zones);
    for (const auto& [label, zone] : std::vector<std::pair<QString, SafeZone>>{
             {"None", SafeZone::None},
             {"Circle (avatars, round icons)", SafeZone::Circle},
             {"Rounded square (iOS / macOS mask)", SafeZone::Squircle},
             {"Android adaptive (72 dp visible, 66 dp safe)", SafeZone::AndroidAdaptive}}) {
        auto a = zones->addAction(label);
        a->setCheckable(true);
        a->setChecked(zone == SafeZone::None);
        a->setData(static_cast<int>(zone));
        zoneGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, zone] { canvas_->setSafeZone(zone); });
    }
    add(view, "addGuides", "Add centre guides", {}, [this] {
        auto a = editor_->snapshot()->artboard;
        a.guidesX.push_back(a.width / 2);
        a.guidesY.push_back(a.height / 2);
        report(editor_->setArtboard(a));
        synchronize();
    });
    add(view, "addGuide", "Add guide…", {}, [this] {
        bool ok = false;
        const auto text = QInputDialog::getText(this, "Add guide", "Position, e.g. x 128 or y 64",
                                                QLineEdit::Normal, "x 256", &ok)
                              .trimmed();
        if (!ok)
            return;
        const auto parts = text.split(' ', Qt::SkipEmptyParts);
        bool number = false;
        const double v = parts.size() == 2 ? parts[1].toDouble(&number) : 0;
        if (!number || (parts[0] != "x" && parts[0] != "y")) {
            status("Write x or y followed by a position");
            return;
        }
        auto a = editor_->snapshot()->artboard;
        (parts[0] == "x" ? a.guidesX : a.guidesY).push_back(v);
        report(editor_->setArtboard(a));
        synchronize();
    });
    add(view, "clearGuides", "Clear guides", {}, [this] {
        auto a = editor_->snapshot()->artboard;
        a.guidesX.clear();
        a.guidesY.clear();
        report(editor_->setArtboard(a));
        synchronize();
    });
    auto help = menuBar()->addMenu("Help");
    add(help, "shortcuts", "Keyboard shortcuts", QKeySequence::HelpContents, [this] {
        QMessageBox::information(this, "Keyboard shortcuts",
                                 "Tools: V select · A nodes · P pen · R shape · E ellipse · T text · I image "
                                 "· K eyedropper (or hold Alt) · "
                                 "Space pan · Z zoom\n\nArrows nudge 1 unit (Shift 10) · Shift constrains · "
                                 "Alt scales from the centre · "
                                 "Ctrl-click selects inside groups\n\nCtrl+G group · Ctrl+Shift+G ungroup · "
                                 "Ctrl+7 clip · Ctrl+D duplicate · "
                                 "Ctrl+[ ] arrange\nCtrl+Alt+U/S/I/X booleans · Ctrl+Alt+F flatten · "
                                 "Ctrl+Shift+O convert to path\n"
                                 "Ctrl+Shift+E export · Ctrl+E export again · Ctrl+0 fit · Ctrl+1 actual "
                                 "size · Ctrl+2 selection\n\n"
                                 "Properties: click a field before using the wheel. Drag its name to adjust; "
                                 "Ctrl: finer, Shift: faster, Escape: cancel.\n"
                                 "Resize: edges snap to canvas/guides. Hold Ctrl to bypass snapping; Shift "
                                 "keeps proportions; Alt scales from centre.");
    });
    add(help, "about", "About Kantan LogoMaker", {}, [this] {
        QMessageBox::about(
            this, "Kantan LogoMaker",
            "Kantan LogoMaker " + QApplication::applicationVersion() +
                "\nA small, fast logo and icon maker.\n\nQt 6 (LGPL), Clipper2 (BSL-1.0), nanosvg (zlib), "
                "Miniz (MIT), Fluent System Icons (MIT). See the notices folder.");
    });
}
void MainWindow::showContextMenu(QPoint global) {
    QMenu menu(this);
    const auto& sel = editor_->selection();
    const auto doc = editor_->snapshot();
    const auto* first = sel.empty() ? nullptr : doc->find(sel.front());
    const auto addAction = [&](QMenu* m, const char* name) {
        if (auto* a = action(name))
            m->addAction(a);
    };
    if (sel.empty()) {
        addAction(&menu, "paste");
        addAction(&menu, "selectAll");
        menu.addSeparator();
        addAction(&menu, "addGuides");
        addAction(&menu, "artboard");
        menu.exec(global);
        return;
    }
    if (first && first->as<TextData>())
        menu.addAction("Edit text", this, [this, id = first->id] { canvas_->editTextInline(id, false); });
    addAction(&menu, "cut");
    addAction(&menu, "copy");
    addAction(&menu, "paste");
    addAction(&menu, "duplicate");
    addAction(&menu, "delete");
    menu.addSeparator();
    if (sel.size() > 1) {
        addAction(&menu, "group");
        auto* combine = menu.addMenu("Combine");
        for (const char* name : {"union", "subtract", "intersect", "exclude"})
            addAction(combine, name);
        addAction(&menu, "clip");
    }
    if (first && first->container())
        addAction(&menu, "ungroup");
    if (first && first->kind() == ObjectKind::Boolean)
        addAction(&menu, "flatten");
    if (first && (first->as<ShapeData>() || first->as<TextData>()))
        addAction(&menu, "convert");
    auto* arrangeMenu = menu.addMenu("Arrange");
    for (const char* name : {"front", "forward", "backward", "back"})
        addAction(arrangeMenu, name);
    arrangeMenu->addSeparator();
    addAction(arrangeMenu, "flipH");
    addAction(arrangeMenu, "flipV");
    menu.addSeparator();
    addAction(&menu, "copyStyle");
    addAction(&menu, "pasteStyle");
    addAction(&menu, "savePreset");
    menu.addSeparator();
    addAction(&menu, "lock");
    addAction(&menu, "hide");
    menu.exec(global);
    synchronize();
}
void MainWindow::align(Alignment alignment) {
    const auto doc = editor_->snapshot();
    const auto& sel = editor_->selection();
    if (sel.empty())
        return;
    const bool toArtboard = alignTarget_->currentIndex() != 0;
    const Box reference = toArtboard ? Box{0, 0, doc->artboard.width, doc->artboard.height}
                                     : selectionBounds(*doc, sel, services_->render());
    auto replacements = aligned(*doc, sel, reference, alignment, services_->render());
    if (alignTarget_->currentIndex() == 1) {
        // Align the combined bounds, preserving the arrangement within the selection.
        const Box bounds = selectionBounds(*doc, sel, services_->render());
        Point delta{};
        switch (alignment) {
        case Alignment::Left:
            delta.x = -bounds.x0;
            break;
        case Alignment::Right:
            delta.x = reference.x1 - bounds.x1;
            break;
        case Alignment::CenterX:
            delta.x = reference.center().x - bounds.center().x;
            break;
        case Alignment::Top:
            delta.y = -bounds.y0;
            break;
        case Alignment::Bottom:
            delta.y = reference.y1 - bounds.y1;
            break;
        case Alignment::CenterY:
            delta.y = reference.center().y - bounds.center().y;
            break;
        }
        replacements = translated(*doc, sel, delta);
    }
    report(editor_->update("Align", std::move(replacements)));
    synchronize();
}
void MainWindow::booleanOp(BooleanOp op) {
    report(editor_->group(editor_->selection(), ObjectKind::Boolean, op));
    synchronize();
}
void MainWindow::pathOp(const QString& label, const std::function<std::optional<Object>(ObjectId)>& op) {
    std::vector<Object> replacements;
    for (const auto id : editor_->selection())
        if (auto r = op(id))
            replacements.push_back(std::move(*r));
    if (replacements.empty()) {
        status(label + ": nothing to change in the selection");
        return;
    }
    report(editor_->replaceSubtrees(label.toStdString(), std::move(replacements)));
    synchronize();
}
void MainWindow::applyPickedColor(QColor c) {
    const Color color{static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
                      static_cast<std::uint8_t>(c.blue()), static_cast<std::uint8_t>(c.alpha())};
    const int target = pickTarget_->currentIndex();
    if (target == 2 || editor_->selection().empty()) {
        Style shapes = defaultShapeStyle(), text = defaultTextStyle();
        shapes.fill.color = text.fill.color = color;
        canvas_->setDrawStyle(shapes, text);
        status(QStringLiteral("New shapes will use %1").arg(c.name()));
        return;
    }
    const auto doc = editor_->snapshot();
    std::vector<Object> changed;
    for (const auto id : editor_->selection())
        if (const auto* o = doc->find(id)) {
            Object copy = *o;
            if (target == 0) {
                copy.style.fill.kind = FillKind::Solid;
                copy.style.fill.color = color;
            } else {
                if (copy.style.outlines.empty())
                    copy.style.outlines.push_back({});
                copy.style.outlines[0].paint = {FillKind::Solid, color};
            }
            changed.push_back(std::move(copy));
        }
    report(editor_->update("Pick colour", std::move(changed)));
    synchronize();
}
void MainWindow::copy(bool cut) {
    const auto doc = editor_->snapshot();
    const auto& sel = editor_->selection();
    if (sel.empty())
        return;
    QJsonObject payload{{"fragment", io::fragmentToJson(*doc, sel)}};
    QJsonObject images;
    for (const auto v : payload["fragment"].toObject()["images"].toArray()) {
        const auto key = v.toString().toStdString();
        const auto found = doc->images.find(key);
        if (found == doc->images.end())
            continue;
        images[v.toString()] =
            QJsonObject{{"format", QString::fromStdString(found->second.format)},
                        {"source", QString::fromStdString(found->second.sourceName)},
                        {"data", QString::fromLatin1(io::fromBytes(found->second.encoded).toBase64())}};
    }
    payload["images"] = images;
    auto mime = new QMimeData;
    mime->setData(FragmentMime, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QApplication::clipboard()->setMimeData(mime);
    if (cut)
        report(editor_->remove(sel));
    synchronize();
}
void MainWindow::paste() {
    const auto* mime = QApplication::clipboard()->mimeData();
    if (!mime)
        return;
    const auto& board = editor_->snapshot()->artboard;
    if (mime->hasFormat(FragmentMime)) {
        try {
            const auto payload = QJsonDocument::fromJson(mime->data(FragmentMime)).object();
            std::map<std::string, EmbeddedImage> images;
            const auto imageJson = payload["images"].toObject();
            for (auto it = imageJson.begin(); it != imageJson.end(); ++it) {
                const auto imageBytes =
                    QByteArray::fromBase64(it.value().toObject()["data"].toString().toLatin1());
                auto decoded = io::decodeImage(imageBytes, it.value().toObject()["source"].toString(),
                                               services_->budget);
                if (decoded.result)
                    images[it.key().toStdString()] = std::move(decoded.image);
            }
            auto fragment = io::fragmentFromJson(payload["fragment"].toObject(), images);
            // Re-key images by content so pasting across documents never collides.
            std::map<std::string, EmbeddedImage> keyed;
            for (auto& o : fragment.objects)
                if (auto* d = o.as<ImageData>()) {
                    const auto& image = fragment.images[d->imageKey];
                    const auto key = imageKey(image.encoded).toStdString();
                    keyed[key] = image;
                    d->imageKey = key;
                }
            for (auto& o : fragment.objects)
                if (!o.parent)
                    o.transform.position = o.transform.position + Point{16, 16};
            report(editor_->add(std::move(fragment.objects), {}, {}, std::move(keyed)));
        } catch (const std::exception& e) {
            status(QString("Could not paste: ") + e.what());
        }
    } else if (mime->hasImage()) {
        auto decoded =
            io::imageFromQImage(qvariant_cast<QImage>(mime->imageData()), "Pasted image", services_->budget);
        if (!report(decoded.result))
            return;
        const auto key = imageKey(decoded.image.encoded).toStdString();
        Object o;
        o.name = "Pasted image";
        o.transform.position = {board.width / 2, board.height / 2};
        const double iw = static_cast<double>(decoded.image.pixels->width),
                     ih = static_cast<double>(decoded.image.pixels->height);
        const double fit = std::min(1., std::min(board.width * .8 / iw, board.height * .8 / ih));
        o.data = ImageData{key, iw * fit, ih * fit};
        report(editor_->add({o}, {}, {}, {{key, decoded.image}}));
    } else if (mime->hasText()) {
        Object o;
        o.name = "Text";
        o.style = defaultTextStyle();
        o.transform.position = {board.width / 2, board.height / 2};
        TextData t;
        t.text = mime->text().left(4096).toStdString();
        if (!QFontDatabase::hasFamily(QString::fromStdString(t.font.family)))
            t.font.family = io::FontLibrary::fallbackFamily().toStdString();
        o.data = t;
        report(editor_->add({o}));
    }
    synchronize();
}
void MainWindow::placeImage(const QString& file, QPointF at) {
    auto decoded = io::importImageFile(file, services_->budget);
    if (!report(decoded.result))
        return;
    const auto key = imageKey(decoded.image.encoded).toStdString();
    const auto& board = editor_->snapshot()->artboard;
    const double iw = static_cast<double>(decoded.image.pixels->width),
                 ih = static_cast<double>(decoded.image.pixels->height);
    const double fit = std::min(1., std::min(board.width * .8 / iw, board.height * .8 / ih));
    Object o;
    o.name = QFileInfo(file).completeBaseName().toStdString();
    o.transform.position = {at.x(), at.y()};
    o.data = ImageData{key, iw * fit, ih * fit};
    report(editor_->add({o}, {}, {}, {{key, decoded.image}}));
    chooseTool(Tool::Select);
    synchronize();
}
void MainWindow::importSvg(const QString& file) {
    auto svg = io::importSvgFile(file, 0);
    if (!report(svg.result))
        return;
    // Fit large drawings into the artboard, centred.
    const auto& board = editor_->snapshot()->artboard;
    DocumentData preview;
    std::uint64_t id = 1;
    std::map<std::uint64_t, std::uint64_t> remap;
    for (auto o : svg.objects) {
        const auto old = o.id.value;
        o.id = {id++};
        if (old)
            remap[old] = o.id.value;
        o.parent = o.parent ? ObjectId{remap[o.parent.value]} : ObjectId{};
        preview.objects.push_back(std::make_shared<const Object>(o));
    }
    std::vector<ObjectId> roots;
    for (const auto& o : preview.objects)
        if (!o->parent)
            roots.push_back(o->id);
    const Box b = selectionBounds(preview, roots, services_->render());
    const double fit =
        b.empty() ? 1 : std::min(1., std::min(board.width * .9 / b.width(), board.height * .9 / b.height()));
    if (fit < 1)
        svg = io::importSvgFile(file, std::max(b.width(), b.height()) * fit);
    for (auto& o : svg.objects)
        if (!o.parent)
            o.transform.position = o.transform.position + Point{board.width / 2, board.height / 2};
    report(editor_->add(std::move(svg.objects)));
    if (!svg.unsupported.empty()) {
        QStringList items;
        for (const auto& u : svg.unsupported)
            items << QString::fromStdString(u);
        status("SVG imported; skipped: " + items.join(", "));
    }
    synchronize();
}
void MainWindow::importFont(const QString& file) {
    auto imported = services_->fonts->importFile(file);
    if (!report(imported.result))
        return;
    report(editor_->addFont(imported.key, imported.font));
    status(QStringLiteral(
               "Font “%1” imported and embedded in this project. You are responsible for its licence.")
               .arg(QString::fromStdString(imported.font.family)));
    synchronize();
}
bool MainWindow::untouched() const {
    return path_.isEmpty() && !editor_->canUndo() && !editor_->dirty();
}
void MainWindow::openFile(const QString& file) {
    // Reuse this window only while it holds an untouched new document.
    if (untouched()) {
        load(file);
        return;
    }
    auto window = new MainWindow(services_);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    window->load(file);
}
void MainWindow::showStart() {
    StartDialog dialog(services_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    applyStart(dialog.choice());
}
void MainWindow::applyStart(const StartDialog::Choice& c) {
    switch (c.kind) {
    case StartDialog::Choice::None:
        break;
    case StartDialog::Choice::Blank: {
        if (untouched()) {
            auto doc = std::make_shared<DocumentData>();
            doc->artboard = c.artboard;
            report(editor_->replace(doc, false));
            canvas_->fit();
            synchronize();
        } else {
            auto window = new MainWindow(services_, nullptr, c.artboard);
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->show();
        }
        break;
    }
    case StartDialog::Choice::Template:
        openTemplate(c.templateIndex);
        break;
    case StartDialog::Choice::Recent:
        openFile(c.path);
        break;
    case StartDialog::Choice::Browse:
        actions_["open"]->trigger();
        break;
    }
}
void MainWindow::openTemplate(int index) {
    const auto list = starterTemplates(io::FontLibrary::fallbackFamily());
    if (index < 0 || index >= static_cast<int>(list.size()))
        return;
    auto doc = std::make_shared<DocumentData>(list[static_cast<std::size_t>(index)].document);
    MainWindow* target = this;
    if (!untouched()) {
        target = new MainWindow(services_);
        target->setAttribute(Qt::WA_DeleteOnClose);
        target->show();
    }
    target->report(target->editor_->replace(doc, false));
    target->canvas_->fit();
    target->synchronize();
}
void MainWindow::runIO(IOCompletion::Kind kind, std::function<IOCompletion()> job) {
    if (io_.isRunning()) {
        if (kind != IOCompletion::Autosave)
            status("Wait for the current file operation to finish");
        return;
    }
    io_.setFuture(QtConcurrent::run([job, kind] {
        IOCompletion r;
        try {
            r = job();
        } catch (const std::exception& error) {
            r.result = {Status::InvalidData, error.what()};
        }
        r.kind = kind;
        return r;
    }));
}
bool MainWindow::waitForIO(int milliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (io_.isRunning() && timer.elapsed() < milliseconds)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QCoreApplication::processEvents();
    return !io_.isRunning();
}
void MainWindow::load(const QString& file, bool recovered) {
    const auto generation = editor_->generation();
    const auto budget = services_->budget;
    recovering_ = recovered;
    runIO(IOCompletion::Open, [file, generation, budget] {
        auto loaded = io::loadProject(file, budget);
        IOCompletion r;
        r.result = loaded.result;
        r.document = loaded.document;
        r.path = file;
        r.generation = generation;
        r.warnings = loaded.warnings;
        return r;
    });
}
void MainWindow::saveTo(const QString& file) {
    const auto doc = editor_->snapshot();
    const auto services = services_;
    runIO(IOCompletion::Save, [file, doc, services] {
        auto ticket = services->storage->reserve(file);
        IOCompletion r;
        r.result =
            ticket.run([&] { return io::saveProject(file, doc, services->render(), services->budget); });
        r.path = file;
        r.state = doc->state;
        return r;
    });
}
void MainWindow::save(bool as) {
    QString file = path_;
    if (as || file.isEmpty()) {
        file = QFileDialog::getSaveFileName(this, "Save logo",
                                            path_.isEmpty() ? documentName() + ".kantanlogo" : path_,
                                            "Kantan LogoMaker (*.kantanlogo)");
        if (file.isEmpty())
            return;
        if (!file.endsWith(".kantanlogo", Qt::CaseInsensitive))
            file += ".kantanlogo";
    }
    saveTo(file);
}
void MainWindow::exportTo(const QString& folder, const QString& baseName) {
    const auto doc = editor_->snapshot();
    const auto selection = editor_->selection();
    const auto services = services_;
    const auto settings = doc->exportSettings;
    runIO(IOCompletion::Export, [doc, selection, services, settings, folder, baseName] {
        IOCompletion r;
        const auto plan = io::planExport(settings, folder, baseName);
        r.path = folder;
        if (!plan.result) {
            r.result = plan.result;
            return r;
        }
        QStringList failures;
        for (const auto& outcome : io::runExport(*doc, plan, settings, selection, services->render(),
                                                 *services->storage, services->budget)) {
            if (outcome.result)
                r.written << QFileInfo(outcome.path).fileName();
            else
                failures << QFileInfo(outcome.path).fileName() + ": " +
                                QString::fromStdString(outcome.result.message);
        }
        if (!failures.isEmpty())
            r.result = {Status::InvalidData, ("Some files failed: " + failures.join("; ")).toStdString()};
        return r;
    });
}
void MainWindow::autosave() {
    if (!editor_->dirty() || editor_->snapshot()->state == autosavedState_ || io_.isRunning())
        return;
    const auto doc = editor_->snapshot();
    const auto services = services_;
    const auto file = recoveryPath_;
    QDir().mkpath(QFileInfo(file).path());
    runIO(IOCompletion::Autosave, [file, doc, services] {
        IOCompletion r;
        r.state = doc->state;
        r.path = file;
        auto ticket = services->storage->reserve(file);
        r.result =
            ticket.run([&] { return io::saveProject(file, doc, services->render(), services->budget); });
        return r;
    });
}
void MainWindow::closeEvent(QCloseEvent* e) {
    if (io_.isRunning()) {
        status("Wait for the current file operation to finish");
        e->ignore();
        return;
    }
    if (editor_->dirty()) {
        const auto answer =
            QMessageBox::question(this, "Unsaved changes", "Save changes to " + documentName() + "?",
                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) {
            e->ignore();
            return;
        }
        if (answer == QMessageBox::Save) {
            closeAfterSave_ = true;
            save();
            e->ignore();
            return;
        }
    }
    e->accept();
}
void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* e) {
    for (const auto& url : e->mimeData()->urls()) {
        const auto file = url.toLocalFile();
        const auto ext = QFileInfo(file).suffix().toLower();
        const QPointF at = canvas_->toArtboard(QPointF(canvas_->mapFrom(this, e->position().toPoint())));
        if (ext == "kantanlogo")
            load(file);
        else if (ext == "svg")
            importSvg(file);
        else if (ext == "ttf" || ext == "otf" || ext == "ttc")
            importFont(file);
        else
            placeImage(file, at);
    }
}
void MainWindow::demo() {
    openTemplate(0);
}
} // namespace kantan::app

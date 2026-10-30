#include "app/ExportDialog.h"
#include "app/MainWindow.h"
#include "app/MockupDialog.h"
#include "app/PropertyControls.h"
#include "app/RecentFiles.h"
#include "app/StartDialog.h"
#include "app/ToolIcons.h"
#include "io/ExportPresets.h"
#include "io/FontLibrary.h"
#include "io/IcoWriter.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtTest>

using namespace kantan;
class UITests : public QObject {
    Q_OBJECT
    QTemporaryDir data_;
    std::shared_ptr<app::AppServices> services_;
    QString unexpectedDialog_;
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::Show) {
            if (auto* dialog = qobject_cast<QMessageBox*>(object)) {
                unexpectedDialog_ += dialog->windowTitle() + ": " + dialog->text() + "\n";
                QTimer::singleShot(0, dialog, &QMessageBox::reject);
            }
        }
        return false;
    }
    std::unique_ptr<app::MainWindow> window() {
        auto w = std::make_unique<app::MainWindow>(services_);
        w->resize(1300, 860);
        w->show();
        QTest::qWait(30);
        w->canvas()->fit();
        return w;
    }
    static QPoint at(app::CanvasView* canvas, double x, double y) {
        return canvas->toView(QPointF(x, y)).toPoint();
    }
    static void drag(QWidget* w, QPoint from, QPoint to, Qt::KeyboardModifiers mods = {}) {
        QTest::mousePress(w, Qt::LeftButton, mods, from);
        for (int i = 1; i <= 6; ++i)
            QTest::mouseMove(w, from + (to - from) * i / 6);
        QTest::mouseRelease(w, Qt::LeftButton, mods, to);
    }

  private slots:
    void initTestCase() {
        const int font =
            QFontDatabase::addApplicationFont(QString(KANTAN_FIXTURE_DIR) + "/KantanTestSans.ttf");
        QVERIFY(font >= 0);
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front()));
        qApp->installEventFilter(this);
        qputenv("KANTANLOGO_DATA", data_.path().toUtf8());
        // Keep recent files, start-screen and colour settings out of the real user profile.
        QCoreApplication::setOrganizationName("KantanTests");
        QCoreApplication::setApplicationName("LogoMakerUITests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data_.path());
        services_ = app::AppServices::create();
    }
    void cleanup() {
        const auto message = unexpectedDialog_;
        unexpectedDialog_.clear();
        QVERIFY2(message.isEmpty(), qPrintable(message));
    }
    void iconResourcesRenderAtEveryScale() {
        auto w = window();
        QStringList names{"help", "eye_off"};
        for (const auto& file : QDir(":/icons").entryList({"*.png"}, QDir::Files))
            if (!file.contains('@'))
                names.push_back(file.chopped(4));
        QVERIFY(names.size() >= 40);
        for (const auto& name : names) {
            const auto icon = app::uiIcon(name);
            for (const int side : {20, 40, 60}) {
                const auto pixmap = icon.pixmap(side, side);
                QVERIFY2(!pixmap.isNull(), qPrintable(name));
                const auto pixels = pixmap.toImage();
                bool visible = false;
                for (int y = 0; y < pixels.height() && !visible; ++y)
                    for (int x = 0; x < pixels.width(); ++x)
                        if (pixels.pixelColor(x, y).alpha() > 0) {
                            visible = true;
                            break;
                        }
                QVERIFY2(visible, qPrintable(name));
            }
        }
    }
    void propertyWheelRequiresFocus() {
        auto w = window(); // installs the application policy, including dialogs
        QDoubleSpinBox spin(w.get());
        spin.setRange(0, 100);
        spin.setValue(20);
        spin.show();
        spin.clearFocus();
        QWheelEvent idle(QPointF(5, 5), spin.mapToGlobal(QPoint(5, 5)), {}, QPoint(0, 120), Qt::NoButton, {},
                         Qt::NoScrollPhase, false);
        QApplication::sendEvent(&spin, &idle);
        QCOMPARE(spin.value(), 20.);
        QVERIFY(!idle.isAccepted());
        spin.setFocus(Qt::MouseFocusReason);
        QApplication::processEvents();
        QVERIFY(spin.hasFocus());
        QWheelEvent active(QPointF(5, 5), spin.mapToGlobal(QPoint(5, 5)), {}, QPoint(0, 120), Qt::NoButton,
                           {}, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&spin, &active);
        QVERIFY(spin.value() > 20.);
        QScrollArea area(w.get());
        area.resize(200, 100);
        auto* contents = new QWidget;
        contents->setMinimumSize(150, 600);
        auto* layout = new QVBoxLayout(contents);
        auto* child = new QDoubleSpinBox(contents);
        layout->addWidget(child);
        layout->addStretch();
        area.setWidget(contents);
        area.show();
        QApplication::processEvents();
        child->clearFocus();
        QWheelEvent scroll(QPointF(5, 5), child->mapToGlobal(QPoint(5, 5)), {}, QPoint(0, -120), Qt::NoButton,
                           {}, Qt::NoScrollPhase, false);
        QApplication::sendEvent(child, &scroll);
        QCOMPARE(child->value(), 0.);
        QVERIFY(area.verticalScrollBar()->value() > 0);
    }
    void transformScrubUndoAndCancel() {
        auto w = window();
        Object object;
        object.data = ShapeData{100, 100, RectangleParams{}};
        object.transform.position = {200, 200};
        QVERIFY(w->editor()->add({object}));
        w->inspector()->refresh();
        auto* field = w->inspector()->findChild<QDoubleSpinBox*>("transformX");
        QVERIFY(field);
        QLabel* label = nullptr;
        for (auto* candidate : w->inspector()->findChildren<QLabel*>())
            if (candidate->text() == "X")
                label = candidate;
        QVERIFY(label);
        const auto original = w->editor()->snapshot();
        const double initial = field->value();
        const auto scrub = [&] {
            QTest::mousePress(label, Qt::LeftButton, {}, QPoint(3, 3));
            QMouseEvent move(QEvent::MouseMove, QPointF(33, 3), label->mapToGlobal(QPointF(33, 3)),
                             Qt::NoButton, Qt::LeftButton, {});
            QApplication::sendEvent(label, &move);
        };
        scrub();
        QVERIFY(w->editor()->busy());
        QCOMPARE(w->editor()->snapshot(), original);
        QTest::mouseRelease(label, Qt::LeftButton, {}, QPoint(33, 3));
        QVERIFY(!w->editor()->busy());
        QVERIFY(field->value() > initial);
        QVERIFY(w->editor()->undo());
        w->inspector()->refresh();
        QCOMPARE(field->value(), initial);
        scrub();
        QTest::keyClick(label, Qt::Key_Escape);
        QTest::mouseRelease(label, Qt::LeftButton, {}, QPoint(33, 3));
        QVERIFY(!w->editor()->busy());
        QCOMPARE(field->value(), initial);
        auto* scrubLabel = dynamic_cast<app::ScrubLabel*>(label);
        QVERIFY(scrubLabel);
        scrubLabel->resetValue();
        QCOMPARE(field->value(), 0.);
        QVERIFY(w->editor()->undo());
        w->inspector()->refresh();
        QCOMPARE(field->value(), initial);
    }
    void numericResetPreservesOtherProperties() {
        auto w = window();
        Object object;
        RectangleParams rectangle;
        rectangle.radii.fill(12);
        object.data = ShapeData{100, 80, rectangle};
        QVERIFY(w->editor()->add({object}));
        w->inspector()->refresh();
        app::ScrubLabel* radius = nullptr;
        for (auto* label : w->inspector()->findChildren<QLabel*>())
            if (label->text() == "Corner radius" && label->isVisibleTo(w->inspector()))
                radius = dynamic_cast<app::ScrubLabel*>(label);
        QVERIFY(radius);
        radius->resetValue();
        const auto* shape = w->editor()->snapshot()->objects.front()->as<ShapeData>();
        QCOMPARE(std::get<RectangleParams>(shape->geometry).radii[0], 0.);
        QCOMPARE(shape->width, 100.);
        QCOMPARE(shape->height, 80.);
        QVERIFY(w->editor()->undo());
        QCOMPARE(
            std::get<RectangleParams>(w->editor()->snapshot()->objects.front()->as<ShapeData>()->geometry)
                .radii[0],
            12.);
    }
    void scrubReversesImmediatelyAtBounds() {
        auto w = window();
        Object object;
        object.data = ShapeData{100, 100, RectangleParams{}};
        QVERIFY(w->editor()->add({object}));
        w->inspector()->refresh();
        QLabel* angle = nullptr;
        for (auto* label : w->inspector()->findChildren<QLabel*>())
            if (label->text() == "X")
                angle = label;
        QVERIFY(angle);
        auto* field = w->inspector()->findChild<QDoubleSpinBox*>("transformX");
        const double initial = field->value();
        QTest::mousePress(angle, Qt::LeftButton, {}, QPoint(3, 3));
        const auto move = [&](int x) {
            QMouseEvent event(QEvent::MouseMove, QPointF(x, 3), angle->mapToGlobal(QPointF(x, 3)),
                              Qt::NoButton, Qt::LeftButton, {});
            QApplication::sendEvent(angle, &event);
        };
        move(-10000000);
        QCOMPARE(field->value(), -1000000.);
        move(-9999990);
        QCOMPARE(field->value(), -999999.);
        QTest::keyClick(angle, Qt::Key_Escape);
        QTest::mouseRelease(angle, Qt::LeftButton, {}, QPoint(-9999990, 3));
        QCOMPARE(field->value(), initial);
    }
    void focusedWheelIsOneUndoGesture() {
        auto w = window();
        Object object;
        object.data = ShapeData{100, 100, RectangleParams{}};
        object.transform.position = {200, 200};
        QVERIFY(w->editor()->add({object}));
        w->inspector()->refresh();
        auto* field = w->inspector()->findChild<QDoubleSpinBox*>("transformX");
        const double original = field->value();
        field->setFocus();
        QApplication::processEvents();
        QVERIFY(field->hasFocus());
        for (int index = 0; index < 3; ++index) {
            QWheelEvent event(QPointF(5, 5), field->mapToGlobal(QPoint(5, 5)), {}, QPoint(0, 120),
                              Qt::NoButton, {}, Qt::NoScrollPhase, false);
            QApplication::sendEvent(field, &event);
        }
        QVERIFY(w->editor()->busy());
        QTest::qWait(300);
        QVERIFY(!w->editor()->busy());
        QVERIFY(field->value() > original);
        QCOMPARE(QString::fromStdString(w->editor()->undoLabel()), QString("Adjust property"));
        QVERIFY(w->editor()->undo());
        w->inspector()->refresh();
        QCOMPARE(field->value(), original);
    }
    void canvasAlignmentAndResizeSnap() {
        auto w = window();
        Object first;
        first.data = ShapeData{100, 100, RectangleParams{}};
        first.transform.position = {200, 200};
        Object second = first;
        second.transform.position = {350, 300};
        QVERIFY(w->editor()->add({first, second}));
        const auto ids = w->editor()->created();
        emit w->inspector()->alignCanvas(static_cast<int>(Alignment::Right));
        const auto doc = w->editor()->snapshot();
        QCOMPARE(selectionBounds(*doc, ids, services_->render()).x1, 512.);
        QCOMPARE(doc->find(ids[1])->transform.position.x - doc->find(ids[0])->transform.position.x, 150.);
        QVERIFY(w->editor()->undo());
        w->editor()->select({ids[0]});
        w->canvas()->refresh();
        drag(w->canvas(), at(w->canvas(), 250, 200), at(w->canvas(), 510, 200));
        QCOMPARE(selectionBounds(*w->editor()->snapshot(), {ids[0]}, services_->render()).x1, 512.);
        QVERIFY(w->editor()->undo());
        QCOMPARE(selectionBounds(*w->editor()->snapshot(), {ids[0]}, services_->render()).x1, 250.);
    }
    void lockedSelectionDisablesProperties() {
        auto w = window();
        Object object;
        object.data = ShapeData{100, 100, RectangleParams{}};
        object.locked = true;
        QVERIFY(w->editor()->add({object}));
        w->inspector()->refresh();
        QVERIFY(!w->inspector()->findChild<QDoubleSpinBox*>("transformX")->isEnabled());
        QVERIFY(!w->inspector()->findChild<QWidget*>("lockedSelectionNotice")->isHidden());
    }
    void layerReorderingRetainsEveryRow() {
        auto w = window();
        std::vector<Object> objects;
        for (const auto* name : {"A", "B", "C", "D"}) {
            Object object;
            object.name = name;
            object.data = ShapeData{50, 50, RectangleParams{}};
            objects.push_back(object);
        }
        QVERIFY(w->editor()->add(objects));
        const auto ids = w->editor()->created();
        w->objects()->refresh();
        auto* tree = w->objects()->tree();
        QVERIFY(tree->dragDropMode() != QAbstractItemView::InternalMove);
        w->editor()->select({ids[3]});
        emit tree->dropped({ids[0]}, ids[2], app::ObjectTree::Above);
        QCOMPARE(w->editor()->snapshot()->children({}),
                 (std::vector<ObjectId>{ids[1], ids[2], ids[0], ids[3]}));
        QCOMPARE(tree->topLevelItemCount(), 4);
        QCOMPARE(tree->topLevelItem(1)->text(0), QString("A"));
        QVERIFY(w->objects()->moveLayers({ids[3], ids[1]}, ids[2], app::ObjectTree::Below));
        QCOMPARE(w->editor()->snapshot()->children({}),
                 (std::vector<ObjectId>{ids[1], ids[3], ids[2], ids[0]}));
        QVERIFY(w->objects()->moveLayers({ids[0]}, {}, app::ObjectTree::Root));
        QCOMPARE(w->editor()->snapshot()->children({}).front(), ids[0]);
        QCOMPARE(w->editor()->snapshot()->objects.size(), std::size_t(4));
        QVERIFY(w->editor()->undo());
        QVERIFY(w->editor()->redo());
        w->objects()->refresh();
        QCOMPARE(tree->topLevelItemCount(), 4);
        // Reproduce the view-only removal previously performed by Qt's MoveAction cleanup.
        delete tree->takeTopLevelItem(0);
        QCOMPARE(tree->topLevelItemCount(), 3);
        w->objects()->refresh();
        QCOMPARE(tree->topLevelItemCount(), 4);
        QCOMPARE(w->editor()->snapshot()->objects.size(), std::size_t(4));
    }
    void layerGroupingPreservesTransformsAndRejectsCycles() {
        auto w = window();
        Object first;
        first.transform.position = {160, 180};
        first.data = ShapeData{80, 60, RectangleParams{}};
        Object second = first;
        second.transform.position = {320, 300};
        Object group;
        group.name = "Group";
        group.data = GroupData{};
        group.transform.position = {100, 50};
        group.transform.rotation = 20;
        group.transform.scaleX = group.transform.scaleY = 1.5;
        QVERIFY(w->editor()->add({first, second, group}));
        const auto ids = w->editor()->created();
        const auto bounds = selectionBounds(*w->editor()->snapshot(), {ids[0]}, services_->render());
        QVERIFY(w->objects()->moveLayers({ids[0]}, ids[2], app::ObjectTree::Inside));
        QCOMPARE(w->editor()->snapshot()->find(ids[0])->parent, ids[2]);
        auto after = selectionBounds(*w->editor()->snapshot(), {ids[0]}, services_->render());
        QVERIFY(std::abs(after.x0 - bounds.x0) < .01 && std::abs(after.y1 - bounds.y1) < .01);
        Object nested;
        nested.data = GroupData{};
        QVERIFY(w->editor()->add({nested}, ids[2]));
        const auto nestedId = w->editor()->created().front();
        const auto grouped = w->editor()->snapshot();
        QVERIFY(!w->objects()->moveLayers({ids[2]}, nestedId, app::ObjectTree::Inside));
        QCOMPARE(w->editor()->snapshot(), grouped);
        QVERIFY(w->objects()->moveLayers({ids[0]}, ids[2], app::ObjectTree::Above));
        QVERIFY(!w->editor()->snapshot()->find(ids[0])->parent);
        QCOMPARE(w->objects()->tree()->topLevelItemCount(), 3);
        Object locked = *w->editor()->snapshot()->find(ids[2]);
        locked.locked = true;
        QVERIFY(w->editor()->update("Lock", {locked}));
        const auto before = w->editor()->snapshot();
        QVERIFY(!w->objects()->moveLayers({ids[1]}, ids[2], app::ObjectTree::Inside));
        QCOMPARE(w->editor()->snapshot(), before);
        QCOMPARE(before->objects.size(), std::size_t(4));
    }
    void layerVisibilityClickKeepsSelection() {
        auto w = window();
        Object object;
        object.data = ShapeData{80, 80, RectangleParams{}};
        QVERIFY(w->editor()->add({object, object}));
        const auto ids = w->editor()->created();
        w->editor()->select({ids[0]});
        w->objects()->refresh();
        auto* tree = w->objects()->tree();
        const auto rect = tree->visualItemRect(tree->topLevelItem(0));
        const QPoint eye(tree->columnViewportPosition(1) + tree->columnWidth(1) / 2, rect.center().y());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, eye);
        QCOMPARE(w->editor()->selection(), (std::vector<ObjectId>{ids[0]}));
        QVERIFY(!w->editor()->snapshot()->find(ids[1])->visible);
        QCOMPARE(w->editor()->snapshot()->objects.size(), std::size_t(2));
    }
    void transformHandleCursorsFollowDirection() {
        auto w = window();
        Object object;
        object.transform.position = {256, 256};
        object.data = ShapeData{200, 100, RectangleParams{}};
        QVERIFY(w->editor()->add({object}));
        auto* canvas = w->canvas();
        canvas->refresh();
        const auto hover = [&](QPointF point) {
            QMouseEvent event(QEvent::MouseMove, point, canvas->mapToGlobal(point), Qt::NoButton, {}, {});
            QApplication::sendEvent(canvas, &event);
        };
        hover(at(canvas, 356, 256));
        QCOMPARE(canvas->cursor().shape(), Qt::SizeHorCursor);
        hover(at(canvas, 256, 206));
        QCOMPARE(canvas->cursor().shape(), Qt::SizeVerCursor);
        hover(at(canvas, 156, 206));
        QCOMPARE(canvas->cursor().shape(), Qt::SizeFDiagCursor);
        hover(at(canvas, 356, 206));
        QCOMPARE(canvas->cursor().shape(), Qt::SizeBDiagCursor);
        hover(QPointF(at(canvas, 256, 206)) - QPointF(0, 22));
        QCOMPARE(canvas->cursor().shape(), Qt::BitmapCursor);
        auto rotated = *w->editor()->snapshot()->objects.front();
        rotated.transform.rotation = 45;
        QVERIFY(w->editor()->update("Rotate", {rotated}));
        const auto top = rotated.transform.matrix().map({0, -50});
        hover(canvas->toView({top.x, top.y}));
        QCOMPARE(canvas->cursor().shape(), Qt::SizeBDiagCursor);
    }
    void newShapesUseMutedBlue() {
        auto w = window();
        w->canvas()->setTool(app::Tool::Shape);
        drag(w->canvas(), at(w->canvas(), 150, 150), at(w->canvas(), 300, 300));
        QCOMPARE(w->editor()->snapshot()->objects.front()->style.fill.color, (Color{64, 81, 100, 255}));
        QCOMPARE(Paint{}.color, (Color{64, 81, 100, 255}));
    }
    void compactToolMenusRetainActions() {
        auto w = window();
        auto* options = w->findChild<QWidget*>("selectionOptions");
        QVERIFY(options);
        QCOMPARE(options->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly).size(), 3);
        auto* combine = w->findChild<QToolButton*>("combineOptions");
        auto* arrange = w->findChild<QToolButton*>("arrangeOptions");
        QVERIFY(combine && arrange);
        QVERIFY(!combine->isEnabled() && !arrange->isEnabled());
        QVERIFY(combine->menu()->actions().contains(w->action("union")));
        auto* actions = w->objects()->findChild<QToolButton*>("layerActions");
        QVERIFY(actions && actions->menu()->actions().size() == 3);
        Object object;
        object.data = ShapeData{80, 80, RectangleParams{}};
        QVERIFY(w->editor()->add({object, object}));
        w->objects()->refresh();
        emit w->objects()->selectionChanged();
        QVERIFY(combine->isEnabled() && arrange->isEnabled());
        w->editor()->select({w->editor()->snapshot()->objects.front()->id});
        emit w->objects()->selectionChanged();
        QVERIFY(!combine->isEnabled() && arrange->isEnabled());
    }
    void objectSearchPreservesDocument() {
        auto w = window();
        Object first;
        first.name = "Badge base";
        first.data = ShapeData{100, 100, RectangleParams{}};
        Object second = first;
        second.name = "Tagline";
        QVERIFY(w->editor()->add({first, second}));
        w->objects()->refresh();
        const auto doc = w->editor()->snapshot();
        auto* search = w->objects()->findChild<QLineEdit*>("objectSearch");
        QVERIFY(search);
        search->setText("tagline");
        QCOMPARE(w->objects()->tree()->topLevelItem(0)->isHidden(), false);
        QCOMPARE(w->objects()->tree()->topLevelItem(1)->isHidden(), true);
        QVERIFY(!w->objects()->tree()->dragEnabled());
        search->clear();
        QVERIFY(w->objects()->tree()->dragEnabled());
        QCOMPARE(w->editor()->snapshot(), doc);
    }
    void overlayRefreshReusesArtwork() {
        auto w = window();
        Object object;
        object.data = ShapeData{100, 100, RectangleParams{}};
        QVERIFY(w->editor()->add({object}));
        w->canvas()->refresh();
        const auto key = w->canvas()->artwork().cacheKey();
        w->canvas()->refresh();
        QCOMPARE(w->canvas()->artwork().cacheKey(), key);
        auto changed = *w->editor()->snapshot()->objects.front();
        changed.style.fill.color = {200, 0, 0, 255};
        QVERIFY(w->editor()->update("Recolour", {changed}));
        w->canvas()->refresh();
        QVERIFY(w->canvas()->artwork().cacheKey() != key);
    }
    void drawMoveScaleUndo() {
        auto w = window();
        auto* canvas = w->canvas();
        auto editor = w->editor();
        w->action("new"); // menus exist
        canvas->setTool(app::Tool::Shape);
        canvas->setShapePreset(0); // rectangle
        drag(canvas, at(canvas, 100, 100), at(canvas, 300, 200));
        QCOMPARE(editor->snapshot()->objects.size(), std::size_t(1));
        const auto id = editor->snapshot()->objects[0]->id;
        const auto* s = editor->snapshot()->find(id)->as<ShapeData>();
        QVERIFY(std::abs(s->width - 200) < 2 && std::abs(s->height - 100) < 2);
        QVERIFY(editor->selection() == std::vector<ObjectId>{id});
        // The object panel lists it and the size strip renders every size.
        QCOMPARE(w->objects()->tree()->topLevelItemCount(), 1);
        QTest::qWait(150);
        QCOMPARE(w->sizeStrip()->images().size(), app::SizeStrip::sizes().size());
        QCOMPARE(w->sizeStrip()->images().back().width(), 64);
        // Move with the select tool; snapping keeps whole units.
        canvas->setTool(app::Tool::Select);
        drag(canvas, at(canvas, 200, 150), at(canvas, 240.3, 170.6));
        const auto moved = editor->snapshot()->find(id)->transform.position;
        QVERIFY(std::abs(moved.x - 240) < 1.01 && std::abs(moved.y - 170) < 1.01);
        QVERIFY(editor->canUndo());
        // Scale from the right-middle handle doubles the width (parametric size, not scale).
        const auto box = selectionBounds(*editor->snapshot(), {id}, {});
        drag(canvas, canvas->toView(QPointF(box.x1, box.center().y)).toPoint(),
             canvas->toView(QPointF(box.x1 + box.width(), box.center().y)).toPoint());
        const auto* scaled = editor->snapshot()->find(id);
        QVERIFY(std::abs(scaled->as<ShapeData>()->width - 400) < 4);
        QCOMPARE(scaled->transform.scaleX, 1.);
        // Escape during a drag cancels it.
        QTest::mousePress(canvas, Qt::LeftButton, {}, at(canvas, box.center().x, box.center().y));
        QTest::mouseMove(canvas, at(canvas, box.center().x + 50, box.center().y));
        QTest::keyClick(canvas, Qt::Key_Escape);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, at(canvas, box.center().x + 50, box.center().y));
        QCOMPARE(editor->snapshot()->find(id)->transform.position, scaled->transform.position);
        // Undo walks back; the size strip never dirties the document.
        w->action("undo")->trigger();
        w->action("undo")->trigger();
        QVERIFY(std::abs(editor->snapshot()->find(id)->transform.position.x - 200) < 1e-9);
        // Arrow keys nudge.
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Right, Qt::ShiftModifier);
        QVERIFY(std::abs(editor->snapshot()->find(id)->transform.position.x - 210) < 1e-9);
        QTest::keyClick(canvas, Qt::Key_Delete);
        QVERIFY(editor->snapshot()->objects.empty());
    }
    void inspectorEditsAreUndoable() {
        auto w = window();
        auto editor = w->editor();
        Object o;
        o.transform.position = {256, 256};
        o.data = ShapeData{100, 100, StarParams{}};
        QVERIFY(editor->add({o}));
        w->inspector()->refresh();
        // Find the "Points" spin box and change it.
        QDoubleSpinBox* points = nullptr;
        for (auto* spin : w->inspector()->findChildren<QDoubleSpinBox*>())
            if (spin->isVisibleTo(w->inspector()) && spin->maximum() == 64 && spin->minimum() == 3)
                points = spin;
        QVERIFY(points);
        points->setValue(7);
        QCOMPARE(std::get<StarParams>(editor->snapshot()->objects[0]->as<ShapeData>()->geometry).points, 7);
        QCOMPARE(QString::fromStdString(editor->undoLabel()), QString("Star points"));
        w->action("undo")->trigger();
        QCOMPARE(std::get<StarParams>(editor->snapshot()->objects[0]->as<ShapeData>()->geometry).points, 5);
    }
    void objectPanelVisibilityAndBooleans() {
        auto w = window();
        auto editor = w->editor();
        Object a, b;
        a.transform.position = {200, 256};
        a.data = ShapeData{200, 200, EllipseParams{}};
        b = a;
        b.transform.position = {300, 256};
        QVERIFY(editor->add({a, b}));
        w->objects()->refresh();
        auto* tree = w->objects()->tree();
        QCOMPARE(tree->topLevelItemCount(), 2);
        // Clicking the eye column hides the top item (topmost first).
        auto* top = tree->topLevelItem(0);
        emit tree->toggleRequested({top->data(0, Qt::UserRole).toULongLong()}, 1);
        QVERIFY(!editor->snapshot()->objects.back()->visible);
        w->action("undo")->trigger();
        // Booleans via the Path menu create a live group; flatten bakes it.
        editor->select({editor->snapshot()->objects[0]->id, editor->snapshot()->objects[1]->id});
        w->action("subtract")->trigger();
        QCOMPARE(editor->snapshot()->objects.size(), std::size_t(3));
        QCOMPARE(editor->snapshot()->objects[0]->kind(), ObjectKind::Boolean);
        w->action("flatten")->trigger();
        QCOMPARE(editor->snapshot()->objects.size(), std::size_t(1));
        QCOMPARE(editor->snapshot()->objects[0]->kind(), ObjectKind::Shape);
    }
    void textToolAndInspectorText() {
        auto w = window();
        auto* canvas = w->canvas();
        canvas->setTool(app::Tool::Text);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, at(canvas, 256, 256));
        auto editor = w->editor();
        QCOMPARE(editor->snapshot()->objects.size(), std::size_t(1));
        // The text tool opens a box on the canvas: typing updates live, Enter commits one undo step.
        auto* inline_ = canvas->findChild<QPlainTextEdit*>("inlineText");
        QVERIFY(inline_ && inline_->isVisible());
        QTest::keyClicks(inline_, "Hi");
        QCOMPARE(QString::fromStdString(editor->preview()->objects[0]->as<TextData>()->text), QString("Hi"));
        QTest::keyClick(inline_, Qt::Key_Return);
        QTest::qWait(30);
        QVERIFY(!canvas->editingText());
        QCOMPARE(QString::fromStdString(editor->snapshot()->objects[0]->as<TextData>()->text), QString("Hi"));
        QCOMPARE(QString::fromStdString(editor->undoLabel()), QString("Edit text"));
        // The inspector edits the same text.
        w->inspector()->refresh();
        auto* text = w->inspector()->textEdit();
        QVERIFY(text->isVisibleTo(w.get()));
        text->setPlainText("Kantan");
        QTest::qWait(450);
        QCOMPARE(QString::fromStdString(editor->snapshot()->objects[0]->as<TextData>()->text),
                 QString("Kantan"));
        // Text renders through the shaper.
        const auto bounds =
            objectBounds(*editor->snapshot(), *editor->snapshot()->objects[0], services_->render());
        QVERIFY(bounds.width() > 100);
    }
    void penToolClosesPaths() {
        auto w = window();
        auto* canvas = w->canvas();
        canvas->setTool(app::Tool::Pen);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, at(canvas, 100, 100));
        drag(canvas, at(canvas, 300, 100), at(canvas, 340, 160));
        QTest::mouseClick(canvas, Qt::LeftButton, {}, at(canvas, 200, 300));
        QTest::mouseClick(canvas, Qt::LeftButton, {}, at(canvas, 100, 100)); // closes
        auto editor = w->editor();
        QCOMPARE(editor->snapshot()->objects.size(), std::size_t(1));
        const auto* path = std::get_if<Path>(&editor->snapshot()->objects[0]->as<ShapeData>()->geometry);
        QVERIFY(path && path->subpaths[0].closed && path->subpaths[0].nodes.size() == 3);
        QCOMPARE(path->subpaths[0].nodes[1].type, NodeType::Smooth);
        // Direct select: drag a node.
        canvas->setTool(app::Tool::Direct);
        editor->select({editor->snapshot()->objects[0]->id});
        const auto world = editor->snapshot()->worldMatrix(editor->snapshot()->objects[0]->id);
        const auto node = world.map(path->subpaths[0].nodes[2].anchor);
        drag(canvas, at(canvas, node.x, node.y), at(canvas, node.x, node.y + 40));
        const auto* edited = std::get_if<Path>(&editor->snapshot()->objects[0]->as<ShapeData>()->geometry);
        QVERIFY(std::abs(world.map(edited->subpaths[0].nodes[2].anchor).y - (node.y + 40)) < 1);
    }
    void saveOpenAndExport() {
        auto w = window();
        auto editor = w->editor();
        w->openTemplate(0);
        QVERIFY(editor->snapshot()->objects.size() >= 3);
        const auto file = QDir(data_.path()).filePath("badge.kantanlogo");
        w->saveTo(file);
        QVERIFY(w->waitForIO());
        QVERIFY(!editor->dirty());
        QVERIFY(w->windowTitle().startsWith("badge"));
        auto reopened = window();
        reopened->load(file);
        QVERIFY(reopened->waitForIO());
        QCOMPARE(reopened->editor()->snapshot()->objects.size(), editor->snapshot()->objects.size());
        // Export with saved settings, plus ICO.
        auto settings = editor->snapshot()->exportSettings;
        settings.ico = true;
        QVERIFY(editor->setExportSettings(settings));
        const auto folder = QDir(data_.path()).filePath("export");
        QDir().mkpath(folder);
        w->exportTo(folder, "badge");
        QVERIFY(w->waitForIO());
        for (int size : {32, 64, 128, 256, 512})
            QCOMPARE(QImage(QDir(folder).filePath(QStringLiteral("badge_%1.png").arg(size))).width(), size);
        QFile ico(QDir(folder).filePath("badge.ico"));
        QVERIFY(ico.open(QIODevice::ReadOnly));
        QCOMPARE(io::readIco(ico.readAll()).size(), std::size_t(4));
    }
    void copyPasteAndPresets() {
        auto w = window();
        auto editor = w->editor();
        auto* list = w->presets()->shapes();
        QVERIFY(list->count() > 20);             // parametric + SVG symbols
        emit list->itemActivated(list->item(7)); // star preset
        QCOMPARE(editor->snapshot()->objects.size(), std::size_t(1));
        // SVG symbol preset.
        QListWidgetItem* svg = nullptr;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->text() == "bolt")
                svg = list->item(i);
        QVERIFY(svg);
        emit list->itemActivated(svg);
        QCOMPARE(editor->snapshot()->objects.size(), std::size_t(2));
        // Style preset applies to the selection.
        emit w->presets()->styles()->itemActivated(w->presets()->styles()->item(2)); // Sticker
        QCOMPARE(editor->snapshot()->objects.back()->style.outlines.size(), std::size_t(2));
        // Copy / paste duplicates through the clipboard with an offset.
        w->action("copy")->trigger();
        w->action("paste")->trigger();
        QCOMPARE(editor->snapshot()->objects.size(), std::size_t(3));
        const auto& a = *editor->snapshot()->objects[1];
        const auto& b = *editor->snapshot()->objects[2];
        QVERIFY(std::abs(b.transform.position.x - a.transform.position.x - 16) < 1e-6);
        QVERIFY(b.style == a.style);
    }
    void contextMenuAltDragAndRepeatDuplicate() {
        auto w = window();
        auto* canvas = w->canvas();
        auto editor = w->editor();
        Object o;
        o.transform.position = {100, 100};
        o.data = ShapeData{40, 40, RectangleParams{}};
        QVERIFY(editor->add({o}));
        emit w->canvas()->changed(); // the window refreshes action states after edits
        // Ctrl+D after moving a copy repeats the spacing.
        w->action("duplicate")->trigger();
        QCOMPARE(editor->snapshot()->objects.size(), std::size_t(2));
        canvas->setTool(app::Tool::Select);
        const auto copy = editor->snapshot()->objects[1]->transform.position;
        drag(canvas, at(canvas, copy.x, copy.y), at(canvas, copy.x + 44, copy.y - 16));
        w->action("duplicate")->trigger();
        const auto& objs = editor->snapshot()->objects;
        QCOMPARE(objs.size(), std::size_t(3));
        const auto d1 = objs[1]->transform.position - objs[0]->transform.position;
        const auto d2 = objs[2]->transform.position - objs[1]->transform.position;
        QVERIFY(std::abs(d1.x - d2.x) < 1e-6 && std::abs(d1.y - d2.y) < 1e-6 && std::abs(d1.x - 60) < 1.01);
        // Alt-drag duplicates the object under the pointer and moves the copy; the original stays.
        editor->select({objs[0]->id});
        QTest::keyPress(canvas, Qt::Key_Alt);
        drag(canvas, at(canvas, 100, 100), at(canvas, 100, 220), Qt::AltModifier);
        QTest::keyRelease(canvas, Qt::Key_Alt);
        QCOMPARE(editor->snapshot()->objects.size(), std::size_t(4));
        QVERIFY(std::abs(editor->snapshot()->objects[0]->transform.position.y - 100) < 1e-6);
        QVERIFY(std::abs(editor->snapshot()->objects[1]->transform.position.y - 220) < 1.01);
        // Right-click shows a menu with the common commands (checked without opening it modally).
        QVERIFY(w->action("group") && w->action("union") && w->action("convert"));
    }
    void scrubLabelsAndPalette() {
        auto w = window();
        auto editor = w->editor();
        Object o;
        o.transform.position = {256, 256};
        o.data = ShapeData{100, 100, PolygonParams{6, 0}};
        o.style.fill = {FillKind::Solid, {200, 30, 30, 255}};
        QVERIFY(editor->add({o}));
        w->inspector()->refresh();
        // Dragging the "Sides" label scrubs the value as one undo step.
        QLabel* sides = nullptr;
        for (auto* label : w->inspector()->findChildren<QLabel*>())
            if (label->text() == "Sides" && label->isVisibleTo(w->inspector()))
                sides = label;
        QVERIFY(sides);
        QTest::mousePress(sides, Qt::LeftButton, {}, QPoint(5, 5));
        QMouseEvent move(QEvent::MouseMove, QPointF(25, 5), sides->mapToGlobal(QPointF(25, 5)), Qt::NoButton,
                         Qt::LeftButton, {});
        QApplication::sendEvent(sides, &move);
        QTest::mouseRelease(sides, Qt::LeftButton, {}, QPoint(25, 5));
        QCOMPARE(std::get<PolygonParams>(editor->snapshot()->objects[0]->as<ShapeData>()->geometry).sides,
                 16);
        QCOMPARE(QString::fromStdString(editor->undoLabel()), QString("Sides"));
        // Hex entry sets the fill colour.
        QLineEdit* hex = nullptr;
        for (auto* edit : w->inspector()->findChildren<QLineEdit*>("colorHex"))
            if (edit->isVisibleTo(w->inspector()))
                hex = hex ? hex : edit;
        QVERIFY(hex);
        hex->setText("10A0F0");
        emit hex->editingFinished();
        QCOMPARE(editor->snapshot()->objects[0]->style.fill.color, (Color{0x10, 0xa0, 0xf0, 255}));
        // Palette: add the fill colour, then change it everywhere in one step.
        QVERIFY(editor->setPalette({{"Brand", {0x10, 0xa0, 0xf0, 255}}}));
        QVERIFY(editor->editPaletteColor(0, {1, 2, 3, 255}));
        QCOMPARE(editor->snapshot()->objects[0]->style.fill.color, (Color{1, 2, 3, 255}));
        w->presets()->palette()->refresh();
        QCOMPARE(w->presets()->palette()->list()->count(), 1);
    }
    void eyedropperAppliesFill() {
        auto w = window();
        auto editor = w->editor();
        Object o;
        o.transform.position = {256, 256};
        o.data = ShapeData{300, 300, RectangleParams{}};
        o.style.fill = {FillKind::Solid, {10, 200, 30, 255}};
        Object other = o;
        other.transform.position = {60, 60};
        other.data = ShapeData{40, 40, EllipseParams{}};
        other.style.fill.color = {0, 0, 0, 255};
        QVERIFY(editor->add({o, other}));
        editor->select({editor->snapshot()->objects[1]->id});
        w->canvas()->refresh();
        w->canvas()->setTool(app::Tool::Eyedropper);
        QTest::mouseClick(w->canvas(), Qt::LeftButton, {}, at(w->canvas(), 256, 256));
        QCOMPARE(editor->snapshot()->objects[1]->style.fill.color, (Color{10, 200, 30, 255}));
    }
    void exportPresetsAndExportAgain() {
        auto w = window();
        auto editor = w->editor();
        w->openTemplate(0);
        app::ExportDialog dialog(editor->snapshot(), {}, services_, "badge");
        auto* preset = dialog.findChild<QComboBox*>("exportPreset");
        QVERIFY(preset);
        QCOMPARE(preset->count(), static_cast<int>(io::exportPresets().size()) + 1);
        const int android = preset->findText("Android", Qt::MatchStartsWith);
        QVERIFY(android > 0);
        preset->setCurrentIndex(android);
        emit preset->activated(android);
        auto s = dialog.settings();
        QCOMPARE(s.outputs.size(), std::size_t(6));
        QCOMPARE(QString::fromStdString(s.preset), preset->itemText(android));
        preset->setCurrentIndex(0);
        emit preset->activated(0);
        QVERIFY(dialog.settings().outputs.empty());
        // Export again reuses the saved folder and settings without a dialog.
        const auto folder = QDir(data_.path()).filePath("again");
        QDir().mkpath(folder);
        auto settings = editor->snapshot()->exportSettings;
        settings.folder = folder.toStdString();
        settings.pngSizes = {48};
        QVERIFY(editor->setExportSettings(settings));
        w->action("exportAgain")->trigger();
        QVERIFY(w->waitForIO());
        QVERIFY(!QDir(folder).entryList({"*_48.png"}, QDir::Files).isEmpty());
    }
    void safeZonesAndMockups() {
        QPainterPath inner;
        const auto circle = app::safeZonePath(app::SafeZone::Circle, 1024, 512);
        QCOMPARE(circle.boundingRect().toRect(), QRect(256, 0, 512, 512));
        const auto android = app::safeZonePath(app::SafeZone::AndroidAdaptive, 108, 108, &inner);
        QVERIFY(std::abs(android.boundingRect().width() - 72) < 1e-6);
        QVERIFY(std::abs(inner.boundingRect().width() - 66) < 1e-6);
        const auto squircle = app::safeZonePath(app::SafeZone::Squircle, 100, 100);
        QVERIFY(squircle.contains(QPointF(92, 92)) && !squircle.contains(QPointF(99, 99)));
        auto w = window();
        w->openTemplate(2);
        w->canvas()->setSafeZone(app::SafeZone::AndroidAdaptive);
        QVERIFY(!w->canvas()->grab().isNull());
        app::MockupDialog mock(w->editor()->snapshot(), services_, "Squircle");
        QCOMPARE(mock.icon(24).width(), 24);
        QCOMPARE(mock.icon(128).width(), 128);
        mock.show();
        QVERIFY(!mock.grab().isNull());
    }
    void templatesStartScreenAndRecent() {
        const auto list = app::starterTemplates(io::FontLibrary::fallbackFamily());
        QVERIFY(list.size() >= 10);
        for (const auto& t : list)
            QVERIFY2(validateDocument(t.document), qPrintable(t.name));
        app::StartDialog start(services_);
        auto* templates = start.findChild<QListWidget*>("startTemplates");
        QCOMPARE(templates->count(), static_cast<int>(list.size()));
        auto* sizes = start.findChild<QListWidget*>("startSizes");
        QListWidgetItem* steam = nullptr;
        for (int i = 0; i < sizes->count(); ++i)
            if (sizes->item(i)->text().startsWith("Steam"))
                steam = sizes->item(i);
        QVERIFY(steam);
        emit sizes->itemActivated(steam);
        QCOMPARE(start.choice().kind, app::StartDialog::Choice::Blank);
        auto w = window();
        w->applyStart(start.choice());
        QCOMPARE(w->editor()->snapshot()->artboard.width, 920.0);
        QCOMPARE(w->editor()->snapshot()->artboard.height, 430.0);
        app::StartDialog::setShowAtStartup(false);
        QVERIFY(!app::StartDialog::showAtStartup());
        // Saving registers the file as recent, newest first.
        app::clearRecent();
        w->openTemplate(1);
        const auto file = QDir(data_.path()).filePath("recent.kantanlogo");
        w->saveTo(file);
        QVERIFY(w->waitForIO());
        QCOMPARE(app::recentFiles().value(0), QDir::cleanPath(QFileInfo(file).absoluteFilePath()));
        app::StartDialog again(services_);
        QCOMPARE(again.findChild<QListWidget*>("startRecent")->count(), 1);
    }
    void presetAndFontSearch() {
        auto w = window();
        auto* panel = w->presets();
        const int total = panel->shapes()->count();
        panel->search()->setText("heart");
        int visible = 0;
        for (int i = 0; i < total; ++i)
            visible += panel->shapes()->item(i)->isHidden() ? 0 : 1;
        QVERIFY(visible >= 1 && visible < total);
        panel->search()->clear();
        QVERIFY(!panel->shapes()->item(total - 1)->isHidden());
        // Text inspector: the font box is searchable anywhere in the name.
        w->canvas()->setTool(app::Tool::Text);
        QTest::mouseClick(w->canvas(), Qt::LeftButton, {}, at(w->canvas(), 256, 256));
        w->canvas()->finishTextEdit(true);
        w->inspector()->refresh();
        auto* font = w->inspector()->findChild<QComboBox*>("fontFamily");
        QVERIFY(font && font->isEditable());
        QCOMPARE(font->completer()->filterMode(), Qt::MatchContains);
    }
};
QTEST_MAIN(UITests)
#include "ui_tests.moc"

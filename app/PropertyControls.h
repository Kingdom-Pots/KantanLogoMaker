#pragma once
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QSlider>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>

namespace kantan::app {
// Application-wide policy also covers controls constructed by dialogs and toolbars.
class PropertyWheelGuard final : public QObject {
  public:
    explicit PropertyWheelGuard(QObject* parent) : QObject(parent) {}
    static void install() {
        if (!qApp->property("propertyWheelGuard").toBool()) {
            qApp->setProperty("propertyWheelGuard", true);
            qApp->installEventFilter(new PropertyWheelGuard(qApp));
        }
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget)
            return false;
        if (event->type() == QEvent::MouseButtonPress) {
            auto* focused = QApplication::focusWidget();
            if (focused && focused != widget && !focused->isAncestorOf(widget))
                focused->clearFocus();
        }
        const bool property = qobject_cast<QAbstractSpinBox*>(widget) || qobject_cast<QComboBox*>(widget) ||
                              qobject_cast<QSlider*>(widget);
        if (property && event->type() == QEvent::Polish)
            widget->setFocusPolicy(Qt::StrongFocus);
        if (event->type() == QEvent::Wheel) {
            // Spin boxes can receive wheel events through their embedded editor.
            auto* control = widget;
            if (!property && qobject_cast<QAbstractSpinBox*>(widget->parentWidget()))
                control = widget->parentWidget();
            else if (!property)
                return false;
            if (!control->hasFocus()) {
                // Consuming an event in a filter does not propagate it to the scroll area.
                // Forward both pixel and angle deltas, including trackpad phase information.
                const auto* wheel = static_cast<QWheelEvent*>(event);
                for (auto* parent = control->parentWidget(); parent; parent = parent->parentWidget()) {
                    if (auto* area = qobject_cast<QAbstractScrollArea*>(parent)) {
                        QWheelEvent forwarded(area->viewport()->mapFromGlobal(wheel->globalPosition()),
                                              wheel->globalPosition(), wheel->pixelDelta(),
                                              wheel->angleDelta(), wheel->buttons(), wheel->modifiers(),
                                              wheel->phase(), wheel->inverted());
                        forwarded.setAccepted(false);
                        QApplication::sendEvent(area->viewport(), &forwarded);
                        if (forwarded.isAccepted()) {
                            event->accept();
                            return true;
                        }
                    }
                }
                event->ignore();
                return true;
            }
        }
        return false;
    }
};

class ScrubLabel final : public QLabel {
  public:
    ScrubLabel(const QString& text, QWidget* parent, std::function<void()> begin,
               std::function<void(double)> scrub, std::function<void()> end, std::function<void()> cancel)
        : QLabel(text, parent), begin_(std::move(begin)), scrub_(std::move(scrub)), end_(std::move(end)),
          cancel_(std::move(cancel)) {
        setCursor(Qt::SizeHorCursor);
        setProperty("scrubbable", true);
        setAccessibleName(text + " adjustment");
        setToolTip("Drag to adjust · Shift: faster · Ctrl: finer · Escape: cancel");
        qApp->installEventFilter(this);
    }
    void setDeltaRange(std::function<std::pair<double, double>()> range) {
        range_ = std::move(range);
    }
    void setResetAction(std::function<QString()> name, std::function<void()> reset) {
        resetName_ = std::move(name);
        reset_ = std::move(reset);
        setToolTip(toolTip() + " · Right-click: reset");
    }
    void resetValue() {
        if (isEnabled() && reset_ && !active_)
            reset_();
    }

  protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        if (!reset_ || active_)
            return;
        QMenu menu(this);
        menu.addAction(resetName_(), this, [this] { resetValue(); });
        menu.exec(event->globalPos());
    }
    bool eventFilter(QObject*, QEvent* event) override {
        if (pressed_ &&
            ((event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) ||
             event->type() == QEvent::WindowDeactivate)) {
            pressed_ = false;
            if (active_)
                cancel_();
            active_ = false;
            return event->type() == QEvent::KeyPress;
        }
        return false;
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !isEnabled())
            return;
        origin_ = last_ = event->globalPosition().x();
        delta_ = 0;
        pressed_ = true;
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!pressed_)
            return;
        const double position = event->globalPosition().x();
        if (!active_) {
            if (std::abs(position - origin_) < QApplication::startDragDistance())
                return;
            begin_();
            active_ = true;
        }
        const double factor = event->modifiers() & Qt::ControlModifier ? .1
                              : event->modifiers() & Qt::ShiftModifier ? 10.
                                                                       : 1.;
        delta_ += (position - last_) * factor;
        if (range_) {
            const auto [minimum, maximum] = range_();
            delta_ = std::clamp(delta_, minimum, maximum);
        }
        last_ = position;
        scrub_(delta_);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        pressed_ = false;
        if (active_)
            end_();
        active_ = false;
    }

  private:
    std::function<void()> begin_;
    std::function<void(double)> scrub_;
    std::function<void()> end_, cancel_;
    std::function<std::pair<double, double>()> range_;
    std::function<QString()> resetName_;
    std::function<void()> reset_;
    double origin_{}, last_{}, delta_{};
    bool pressed_{}, active_{};
};
} // namespace kantan::app

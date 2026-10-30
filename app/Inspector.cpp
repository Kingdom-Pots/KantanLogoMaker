#include "app/Inspector.h"
#include "app/ColorField.h"
#include "app/CompactControls.h"
#include "app/PropertyControls.h"
#include "editing/Colors.h"
#include "editing/Transforms.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QCompleter>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>
#include <set>

namespace kantan::app {
namespace {
template <class T> bool isKind(const Object& o) {
    return o.as<T>() != nullptr;
}
template <class P> P* shapeParams(Object& o) {
    auto* s = o.as<ShapeData>();
    return s ? std::get_if<P>(&s->geometry) : nullptr;
}
template <class P> const P* shapeParams(const Object& o) {
    const auto* s = o.as<ShapeData>();
    return s ? std::get_if<P>(&s->geometry) : nullptr;
}
bool styled(const Object& o) {
    return o.kind() != ObjectKind::Group && o.kind() != ObjectKind::Clip;
}
Outline& outlineAt(Object& o, std::size_t i) {
    while (o.style.outlines.size() <= i)
        o.style.outlines.push_back({});
    return o.style.outlines[i];
}
const Outline* outlineAt(const Object& o, std::size_t i) {
    return i < o.style.outlines.size() ? &o.style.outlines[i] : nullptr;
}
Object propertyDefaults(const Object& source) {
    Object result;
    result.data = std::visit([](const auto& data) -> ObjectData { return std::decay_t<decltype(data)>{}; },
                             source.data);
    if (const auto* shape = source.as<ShapeData>())
        result.as<ShapeData>()->geometry = std::visit(
            [](const auto& geometry) -> ShapeGeometry { return std::decay_t<decltype(geometry)>{}; },
            shape->geometry);
    result.style.outlines.resize(source.style.outlines.size());
    result.style.fill.stops.resize(std::max<std::size_t>(2, source.style.fill.stops.size()));
    for (std::size_t index = 0; index < result.style.fill.stops.size(); ++index)
        result.style.fill.stops[index].offset =
            static_cast<double>(index) / static_cast<double>(result.style.fill.stops.size() - 1);
    return result;
}
} // namespace
Inspector::Inspector(std::shared_ptr<Editor> editor, std::shared_ptr<AppServices> services, QWidget* parent)
    : QScrollArea(parent), editor_(std::move(editor)), services_(std::move(services)) {
    setObjectName("inspector");
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    content_ = new QWidget;
    layout_ = new QVBoxLayout(content_);
    layout_->setContentsMargins(6, 4, 6, 4);
    layout_->setSpacing(2);
    empty_ = new QLabel("Select an object to edit its properties.\nDraw with the Shape (R), Ellipse (E), Pen "
                        "(P) or Text (T) tools.");
    static_cast<QLabel*>(empty_)->setWordWrap(true);
    empty_->setStyleSheet("color:#8d97a6;padding:8px;");
    layout_->addWidget(empty_);
    auto locked = new QLabel("Unlock the selected objects to edit their properties.", content_);
    locked->setObjectName("lockedSelectionNotice");
    locked->setWordWrap(true);
    lockedNotice_ = locked;
    layout_->addWidget(lockedNotice_);
    // Most-used first: what the object is (text/shape/image settings), then its look, then placement.
    buildTransform();
    buildText();
    buildShape();
    buildImage();
    buildFill();
    buildOutlines();
    buildEffects();
    buildAppearance();
    layout_->addStretch();
    setWidget(content_);
    wheelTimer_.setSingleShot(true);
    wheelTimer_.setInterval(250);
    connect(&wheelTimer_, &QTimer::timeout, this, [this] {
        if (wheelGesture_) {
            wheelGesture_ = false;
            finishPreview();
        }
    });
    for (auto* control : content_->findChildren<QWidget*>())
        if (qobject_cast<QAbstractSpinBox*>(control) || qobject_cast<QSlider*>(control) ||
            qobject_cast<QComboBox*>(control))
            control->installEventFilter(this);
    refresh();
}
bool Inspector::eventFilter(QObject* watched, QEvent* event) {
    auto* control = qobject_cast<QWidget*>(watched);
    if (event->type() == QEvent::Wheel && control && control->hasFocus()) {
        if (!editor_->busy())
            wheelGesture_ = static_cast<bool>(editor_->begin("Adjust property"));
        if (wheelGesture_)
            wheelTimer_.start();
    } else if (wheelGesture_ && (event->type() == QEvent::FocusOut || event->type() == QEvent::KeyPress)) {
        wheelTimer_.stop();
        wheelGesture_ = false;
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            editor_->cancel();
            emit documentChanged();
            return true;
        }
        finishPreview();
    }
    return QScrollArea::eventFilter(watched, event);
}
const Object* Inspector::primary() const {
    const auto& sel = editor_->selection();
    return sel.empty() ? nullptr : editor_->preview()->find(sel.front());
}
void Inspector::edit(const QString& undo, const std::function<void(Object&)>& change,
                     const Applies& applies) {
    if (loading_)
        return;
    if (wheelGesture_) {
        previewEach(undo, change, applies);
        return;
    }
    const auto doc = editor_->snapshot();
    std::vector<Object> replacements;
    for (const auto id : editor_->selection()) {
        const auto* o = doc->find(id);
        if (!o || (applies && !applies(*o)))
            continue;
        Object copy = *o;
        change(copy);
        if (!(copy == *o))
            replacements.push_back(std::move(copy));
    }
    if (replacements.empty())
        return;
    const auto r = editor_->update(undo.toStdString(), std::move(replacements));
    if (!r && !r.message.empty())
        emit feedback(QString::fromStdString(r.message));
    emit documentChanged();
}
QFormLayout* Inspector::section(const QString& title, std::function<bool(const Object&, std::size_t)> visible,
                                bool open) {
    auto box = new QWidget(content_);
    box->setAccessibleName(title);
    auto form = new QFormLayout(box);
    form->setContentsMargins(4, 2, 2, 6);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(3);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto header = disclosure(layout_, box, title, open);
    sections_.push_back({box, header, std::move(visible)});
    return form;
}
namespace {
// Grey italic marks a field whose value differs across the selection; editing it sets every object.
void markMixed(QWidget* w, bool mixed) {
    w->setProperty("mixed", mixed);
    w->setStyleSheet(mixed ? "color:#8d97a6;font-style:italic;" : QString());
    w->setToolTip(mixed ? "The selected objects differ here; a new value applies to all of them" : QString());
}
} // namespace
std::vector<const Object*> Inspector::targets(const Applies& applies) const {
    std::vector<const Object*> out;
    const auto doc = editor_->preview();
    for (const auto id : editor_->selection())
        if (const auto* o = doc->find(id); o && (!applies || applies(*o)))
            out.push_back(o);
    return out;
}
void Inspector::previewEach(const QString& undo, const std::function<void(Object&)>& change,
                            const Applies& applies) {
    if (!editor_->busy() && !editor_->begin(undo.toStdString()))
        return;
    const auto doc = editor_->snapshot();
    std::vector<Object> replacements;
    for (const auto id : editor_->selection())
        if (const auto* o = doc->find(id); o && (!applies || applies(*o))) {
            Object copy = *o;
            change(copy);
            replacements.push_back(std::move(copy));
        }
    editor_->preview(std::move(replacements));
    emit documentChanged();
}
void Inspector::finishPreview() {
    if (editor_->busy()) {
        const auto r = editor_->apply();
        if (!r && !r.message.empty())
            emit feedback(QString::fromStdString(r.message));
    }
    emit documentChanged();
}
QDoubleSpinBox* Inspector::number(QFormLayout* form, const QString& label, double min, double max,
                                  int decimals, Getter get, Setter set, const QString& undo, Applies applies,
                                  const QString& suffix) {
    auto spin = new QDoubleSpinBox(content_);
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(false);
    spin->setSuffix(suffix);
    spin->setAccelerated(true);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setMinimumWidth(70);
    spin->setAccessibleName(label.trimmed());
    auto start = std::make_shared<double>();
    auto owned = std::make_shared<bool>(false);
    const double step = decimals == 0 ? .5 : .1;
    auto scrub = new ScrubLabel(
        label.trimmed(), content_,
        [this, spin, start, owned, undo] {
            *start = spin->value();
            *owned = static_cast<bool>(editor_->begin(undo.toStdString()));
        },
        [this, spin, start, owned, step, get, set, undo, applies](double px) {
            if (!*owned)
                return;
            const double delta =
                std::round(px * step * std::pow(10, spin->decimals())) / std::pow(10, spin->decimals());
            const double v = std::clamp(*start + delta, spin->minimum(), spin->maximum());
            const double rounded =
                std::round(v * std::pow(10, spin->decimals())) / std::pow(10, spin->decimals());
            {
                const QSignalBlocker block(spin);
                spin->setValue(rounded);
            }
            previewEach(
                undo,
                [&](Object& o) {
                    const double value = std::clamp(get(o) + delta, spin->minimum(), spin->maximum());
                    set(o, value);
                },
                applies);
        },
        [this, owned] {
            if (*owned)
                finishPreview();
            *owned = false;
        },
        [this, owned] {
            if (*owned) {
                editor_->cancel();
                emit documentChanged();
            }
            *owned = false;
        });
    if (label.startsWith("  "))
        scrub->setIndent(10);
    scrub->setBuddy(spin);
    scrub->setDeltaRange([this, spin, start, step] {
        if (editor_->selection().size() > 1)
            return std::pair{-std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::infinity()};
        return std::pair{(spin->minimum() - *start) / step, (spin->maximum() - *start) / step};
    });
    scrub->setResetAction(
        [this, label, get, spin, decimals, suffix] {
            const auto* object = primary();
            const double value =
                object ? std::clamp(get(propertyDefaults(*object)), spin->minimum(), spin->maximum()) : 0;
            return "Reset " + label.trimmed() + " to " + QString::number(value, 'f', decimals) + suffix;
        },
        [this, get, set, applies, undo, spin] {
            edit(
                "Reset " + undo,
                [&](Object& object) {
                    set(object, std::clamp(get(propertyDefaults(object)), spin->minimum(), spin->maximum()));
                },
                applies);
        });
    form->addRow(scrub, spin);
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, set, undo, applies](double v) {
        edit(
            undo, [&](Object& o) { set(o, v); }, applies);
    });
    loaders_.push_back([this, spin, get, applies](const Object& o) {
        if (applies && !applies(o))
            return;
        const QSignalBlocker block(spin);
        spin->setValue(get(o));
        bool mixed = false;
        for (const auto* t : targets(applies))
            mixed = mixed || std::abs(get(*t) - get(o)) > 1e-9;
        markMixed(spin, mixed);
    });
    if (applies)
        rows_.push_back({spin, applies});
    return spin;
}
void Inspector::slider(QFormLayout* form, const QString& label, int min, int max, Getter get, Setter set,
                       const QString& undo, Applies applies) {
    auto row = new QWidget(content_);
    auto h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto s = new QSlider(Qt::Horizontal, row);
    s->setRange(min, max);
    s->setAccessibleName(label.trimmed());
    auto value = new QLabel(row);
    value->setMinimumWidth(34);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    h->addWidget(s, 1);
    h->addWidget(value);
    auto initial = std::make_shared<double>();
    auto owned = std::make_shared<bool>(false);
    auto scrub = new ScrubLabel(
        label.trimmed(), row,
        [this, s, initial, owned, undo] {
            *initial = s->value();
            *owned = static_cast<bool>(editor_->begin(undo.toStdString()));
        },
        [this, s, initial, owned, get, set, undo, applies](double pixels) {
            if (!*owned)
                return;
            const double delta = std::round(pixels);
            {
                const QSignalBlocker blocker(s);
                s->setValue(static_cast<int>(std::clamp(*initial + delta, static_cast<double>(s->minimum()),
                                                        static_cast<double>(s->maximum()))));
            }
            previewEach(
                undo,
                [&](Object& object) {
                    set(object, std::clamp(get(object) + delta, static_cast<double>(s->minimum()),
                                           static_cast<double>(s->maximum())));
                },
                applies);
        },
        [this, owned] {
            if (*owned)
                finishPreview();
            *owned = false;
        },
        [this, owned] {
            if (*owned) {
                editor_->cancel();
                emit documentChanged();
            }
            *owned = false;
        });
    form->addRow(scrub, row);
    scrub->setBuddy(s);
    scrub->setDeltaRange([this, s, initial] {
        if (editor_->selection().size() > 1)
            return std::pair{-std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::infinity()};
        return std::pair{static_cast<double>(s->minimum()) - *initial,
                         static_cast<double>(s->maximum()) - *initial};
    });
    scrub->setResetAction(
        [this, label, get, s] {
            const auto* object = primary();
            const double value =
                object ? std::clamp(get(propertyDefaults(*object)), static_cast<double>(s->minimum()),
                                    static_cast<double>(s->maximum()))
                       : 0;
            return "Reset " + label.trimmed() + " to " + QString::number(value);
        },
        [this, get, set, applies, undo, s] {
            edit(
                "Reset " + undo,
                [&](Object& object) {
                    set(object, std::clamp(get(propertyDefaults(object)), static_cast<double>(s->minimum()),
                                           static_cast<double>(s->maximum())));
                },
                applies);
        });
    // A drag is one undo step: begin on press, preview while moving, apply on release.
    connect(s, &QSlider::valueChanged, this, [this, s, value, set, applies, undo](int v) {
        value->setText(QString::number(v));
        if (loading_)
            return;
        if (!s->isSliderDown()) {
            edit(
                undo, [&](Object& o) { set(o, v); }, applies);
            return;
        }
        previewEach(
            undo, [&](Object& o) { set(o, v); }, applies);
    });
    connect(s, &QSlider::sliderReleased, this, [this] { finishPreview(); });
    loaders_.push_back([this, s, value, get, applies](const Object& o) {
        if (applies && !applies(o))
            return;
        const QSignalBlocker block(s);
        s->setValue(static_cast<int>(std::lround(get(o))));
        bool mixed = false;
        for (const auto* t : targets(applies))
            mixed = mixed || std::lround(get(*t)) != std::lround(get(o));
        value->setText(mixed ? QStringLiteral("—") : QString::number(s->value()));
        markMixed(value, mixed);
    });
    if (applies)
        rows_.push_back({row, applies});
}
void Inspector::check(QFormLayout* form, const QString& label, std::function<bool(const Object&)> get,
                      std::function<void(Object&, bool)> set, const QString& undo, Applies applies) {
    auto box = new QCheckBox(content_);
    form->addRow(label, box);
    connect(box, &QCheckBox::toggled, this, [this, set, undo, applies](bool v) {
        edit(
            undo, [&](Object& o) { set(o, v); }, applies);
    });
    loaders_.push_back([this, box, get, applies](const Object& o) {
        if (applies && !applies(o))
            return;
        const QSignalBlocker block(box);
        bool mixed = false;
        for (const auto* t : targets(applies))
            mixed = mixed || get(*t) != get(o);
        box->setTristate(mixed);
        box->setCheckState(mixed ? Qt::PartiallyChecked : get(o) ? Qt::Checked : Qt::Unchecked);
        box->setTristate(false);
        if (mixed)
            box->setCheckState(Qt::PartiallyChecked);
    });
    if (applies)
        rows_.push_back({box, applies});
}
void Inspector::combo(QFormLayout* form, const QString& label, const QStringList& items,
                      std::function<int(const Object&)> get, std::function<void(Object&, int)> set,
                      const QString& undo, Applies applies) {
    auto box = new QComboBox(content_);
    box->addItems(items);
    form->addRow(label, box);
    connect(box, &QComboBox::activated, this, [this, set, undo, applies](int v) {
        edit(
            undo, [&](Object& o) { set(o, v); }, applies);
    });
    loaders_.push_back([this, box, get, applies](const Object& o) {
        if (applies && !applies(o))
            return;
        const QSignalBlocker block(box);
        box->setCurrentIndex(get(o));
        bool mixed = false;
        for (const auto* t : targets(applies))
            mixed = mixed || get(*t) != get(o);
        markMixed(box, mixed);
    });
    if (applies)
        rows_.push_back({box, applies});
}
std::vector<Swatch> Inspector::swatches() const {
    std::vector<Swatch> out;
    const auto doc = editor_->snapshot();
    for (const auto& p : doc->palette)
        out.push_back({p.color, QString::fromStdString(p.name)});
    for (const auto c : documentColors(*doc, 8))
        if (std::none_of(out.begin(), out.end(), [&](const Swatch& s) { return sameRgb(s.color, c); }))
            out.push_back({c, "In this document"});
    return out;
}
void Inspector::color(QFormLayout* form, const QString& label, std::function<Color(const Object&)> get,
                      std::function<void(Object&, Color)> set, const QString& undo, Applies applies) {
    auto field = new ColorField([this] { return swatches(); }, content_);
    form->addRow(label, field);
    connect(field, &ColorField::previewed, this, [this, set, undo, applies](Color c) {
        previewEach(
            undo, [&](Object& o) { set(o, c); }, applies);
    });
    connect(field, &ColorField::picked, this, [this, set, undo, applies](Color c) {
        if (editor_->busy()) {
            previewEach(
                undo, [&](Object& o) { set(o, c); }, applies);
            finishPreview();
        } else
            edit(
                undo, [&](Object& o) { set(o, c); }, applies);
    });
    connect(field, &ColorField::cancelled, this, [this] {
        editor_->cancel();
        emit documentChanged();
    });
    loaders_.push_back([this, field, get, applies](const Object& o) {
        if (applies && !applies(o))
            return;
        bool mixed = false;
        for (const auto* t : targets(applies))
            mixed = mixed || !(get(*t) == get(o));
        field->setColor(get(o), mixed);
        field->refreshSwatches();
    });
    if (applies)
        rows_.push_back({field, applies});
}
void Inspector::buildTransform() {
    auto form = section(
        "Transform", [](const Object&, std::size_t) { return true; }, true);
    const auto mk = [&](const QString& prefix, double min, double max, const QString& suffix = {}) {
        auto s = new QDoubleSpinBox(content_);
        s->setRange(min, max);
        s->setDecimals(1);
        s->setKeyboardTracking(false);
        s->setObjectName("transform" + prefix.trimmed());
        s->setAccessibleName(prefix.trimmed().isEmpty() ? "Angle" : prefix.trimmed());
        s->setSuffix(suffix);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setMinimumWidth(60);
        return s;
    };
    const auto pair = [&](const QString& label, QWidget* a, QWidget* b) {
        auto row = new QWidget(content_);
        auto layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        const auto add = [&](QWidget* widget) {
            auto* spin = qobject_cast<QDoubleSpinBox*>(widget);
            auto initial = std::make_shared<double>();
            auto owned = std::make_shared<bool>(false);
            auto name = spin->accessibleName();
            auto scrub = new ScrubLabel(
                name, row,
                [this, spin, initial, owned] {
                    if (!spin->isEnabled())
                        return;
                    *initial = spin->value();
                    *owned = static_cast<bool>(editor_->begin("Transform"));
                },
                [spin, initial, owned](double delta) {
                    if (*owned)
                        spin->setValue(*initial + delta * .1);
                },
                [this, owned] {
                    if (*owned)
                        finishPreview();
                    *owned = false;
                },
                [this, owned] {
                    if (*owned) {
                        editor_->cancel();
                        emit documentChanged();
                    }
                    *owned = false;
                });
            layout->addWidget(scrub);
            scrub->setBuddy(spin);
            scrub->setDeltaRange([spin, initial] {
                return std::pair{(spin->minimum() - *initial) / .1, (spin->maximum() - *initial) / .1};
            });
            const double reset = name == "W" || name == "H" ? 200. : 0.;
            scrub->setResetAction([name, reset] { return "Reset " + name + " to " + QString::number(reset); },
                                  [spin, reset] {
                                      if (spin->isEnabled())
                                          spin->setValue(reset);
                                  });
            layout->addWidget(spin, 1);
        };
        add(a);
        if (b)
            add(b);
        form->addRow(label == "Angle" ? QString() : label, row);
        return layout;
    };
    x_ = mk("X ", -1e6, 1e6);
    y_ = mk("Y ", -1e6, 1e6);
    w_ = mk("W ", .1, 1e6);
    h_ = mk("H ", .1, 1e6);
    angle_ = mk("Angle", -360, 360, "°");
    pair("Position", x_, y_);
    pair("Size", w_, h_);
    auto* angleRow = pair("Angle", angle_, nullptr);
    auto alignRow = new QWidget(content_);
    auto alignLayout = new QHBoxLayout(alignRow);
    alignLayout->setContentsMargins(0, 0, 0, 0);
    const QStringList icons{"align_left", "align_center_horizontal", "align_right",
                            "align_top",  "align_center_vertical",   "align_bottom"};
    const QStringList names{"left", "horizontal centre", "right", "top", "vertical centre", "bottom"};
    for (int index = 0; index < icons.size(); ++index) {
        auto button = new QToolButton(alignRow);
        iconButton(button, icons[index],
                   "Align selection " + names[index] + " to canvas (preserves spacing)");
        alignLayout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, index] { emit alignCanvas(index); });
    }
    form->addRow("Canvas", alignRow);
    const auto apply =
        [this](const QString& label,
               const std::function<std::vector<Object>(const DocumentData&, kantan::Box)>& fn) {
            if (loading_)
                return;
            const auto doc = editor_->snapshot();
            const auto& sel = editor_->selection();
            if (sel.empty())
                return;
            const kantan::Box b = selectionBounds(*doc, sel, services_->render());
            auto replacements = fn(*doc, b);
            auto r = editor_->busy() ? editor_->preview(std::move(replacements))
                                     : editor_->update(label.toStdString(), std::move(replacements));
            if (!r && !r.message.empty())
                emit feedback(QString::fromStdString(r.message));
            emit documentChanged();
        };
    connect(x_, &QDoubleSpinBox::valueChanged, this, [this, apply](double v) {
        apply("Move", [&](const DocumentData& d, kantan::Box b) {
            return translated(d, editor_->selection(), {v - b.x0, 0});
        });
    });
    connect(y_, &QDoubleSpinBox::valueChanged, this, [this, apply](double v) {
        apply("Move", [&](const DocumentData& d, kantan::Box b) {
            return translated(d, editor_->selection(), {0, v - b.y0});
        });
    });
    connect(w_, &QDoubleSpinBox::valueChanged, this, [this, apply](double v) {
        apply("Resize", [&](const DocumentData& d, kantan::Box b) {
            return scaledAbout(d, editor_->selection(), v / std::max(b.width(), 1e-6), 1, {b.x0, b.y0});
        });
    });
    connect(h_, &QDoubleSpinBox::valueChanged, this, [this, apply](double v) {
        apply("Resize", [&](const DocumentData& d, kantan::Box b) {
            return scaledAbout(d, editor_->selection(), 1, v / std::max(b.height(), 1e-6), {b.x0, b.y0});
        });
    });
    connect(angle_, &QDoubleSpinBox::valueChanged, this, [this, apply](double v) {
        const auto* o =
            editor_->selection().empty() ? nullptr : editor_->snapshot()->find(editor_->selection().front());
        if (!o)
            return;
        const double delta = v - o->transform.rotation;
        apply("Rotate", [&](const DocumentData& d, kantan::Box b) {
            return rotatedAbout(d, editor_->selection(), delta, b.center());
        });
    });
    const auto tool = [&](const QString& icon, const QString& tip, const std::function<void()>& fn) {
        auto b = new QToolButton(content_);
        iconButton(b, icon, tip);
        angleRow->addWidget(b);
        connect(b, &QToolButton::clicked, this, fn);
    };
    tool("flip_horizontal", "Flip horizontal", [this] {
        const auto doc = editor_->snapshot();
        editor_->update("Flip horizontal", flipped(*doc, editor_->selection(), true, services_->render()));
        emit documentChanged();
    });
    tool("flip_vertical", "Flip vertical", [this] {
        const auto doc = editor_->snapshot();
        editor_->update("Flip vertical", flipped(*doc, editor_->selection(), false, services_->render()));
        emit documentChanged();
    });
    loaders_.push_back([this](const Object& o) {
        const auto doc = editor_->preview();
        const kantan::Box b = selectionBounds(*doc, editor_->selection(), services_->render());
        for (auto* s : {x_, y_, w_, h_, angle_})
            s->blockSignals(true);
        x_->setValue(b.x0);
        y_->setValue(b.y0);
        w_->setValue(b.width());
        h_->setValue(b.height());
        angle_->setValue(std::remainder(o.transform.rotation, 360.));
        angle_->setEnabled(editor_->selection().size() == 1);
        for (auto* s : {x_, y_, w_, h_, angle_})
            s->blockSignals(false);
    });
}
void Inspector::buildShape() {
    auto form = section(
        "Shape", [](const Object& o, std::size_t n) { return n == 1 && o.as<ShapeData>(); }, true);
    // Rectangle.
    const auto rect = [](const Object& o) { return shapeParams<RectangleParams>(o) != nullptr; };
    number(
        form, "Corner radius", 0, 1e5, 1,
        [](const Object& o) { return shapeParams<RectangleParams>(o)->radii[0]; },
        [](Object& o, double v) {
            auto* r = shapeParams<RectangleParams>(o);
            r->radii[0] = v;
            if (r->linked)
                r->radii = {v, v, v, v};
        },
        "Corner radius", rect);
    check(
        form, "Link corners", [](const Object& o) { return shapeParams<RectangleParams>(o)->linked; },
        [](Object& o, bool v) {
            auto* r = shapeParams<RectangleParams>(o);
            r->linked = v;
            if (v)
                r->radii = {r->radii[0], r->radii[0], r->radii[0], r->radii[0]};
        },
        "Link corners", rect);
    const auto unlinked = [](const Object& o) {
        const auto* r = shapeParams<RectangleParams>(o);
        return r && !r->linked;
    };
    const char* corners[] = {"Top right", "Bottom right", "Bottom left"};
    for (std::size_t i = 1; i < 4; ++i)
        number(
            form, corners[i - 1], 0, 1e5, 1,
            [i](const Object& o) { return shapeParams<RectangleParams>(o)->radii[i]; },
            [i](Object& o, double v) { shapeParams<RectangleParams>(o)->radii[i] = v; }, "Corner radius",
            unlinked);
    combo(
        form, "Corners", {"Round", "Chamfer", "Inverted"},
        [](const Object& o) { return static_cast<int>(shapeParams<RectangleParams>(o)->corner); },
        [](Object& o, int v) { shapeParams<RectangleParams>(o)->corner = static_cast<CornerStyle>(v); },
        "Corner style", rect);
    // Polygon.
    const auto poly = [](const Object& o) { return shapeParams<PolygonParams>(o) != nullptr; };
    number(
        form, "Sides", 3, 64, 0, [](const Object& o) { return shapeParams<PolygonParams>(o)->sides; },
        [](Object& o, double v) { shapeParams<PolygonParams>(o)->sides = static_cast<int>(v); }, "Sides",
        poly);
    number(
        form, "Corner radius", 0, 1e5, 1,
        [](const Object& o) { return shapeParams<PolygonParams>(o)->cornerRadius; },
        [](Object& o, double v) { shapeParams<PolygonParams>(o)->cornerRadius = v; }, "Corner radius", poly);
    // Star.
    const auto star = [](const Object& o) { return shapeParams<StarParams>(o) != nullptr; };
    number(
        form, "Points", 3, 64, 0, [](const Object& o) { return shapeParams<StarParams>(o)->points; },
        [](Object& o, double v) { shapeParams<StarParams>(o)->points = static_cast<int>(v); }, "Star points",
        star);
    number(
        form, "Inner radius", 1, 100, 0,
        [](const Object& o) { return shapeParams<StarParams>(o)->innerRatio * 100; },
        [](Object& o, double v) { shapeParams<StarParams>(o)->innerRatio = v / 100; }, "Inner radius", star,
        "%");
    number(
        form, "Corner radius", 0, 1e5, 1,
        [](const Object& o) { return shapeParams<StarParams>(o)->cornerRadius; },
        [](Object& o, double v) { shapeParams<StarParams>(o)->cornerRadius = v; }, "Corner radius", star);
    // Ring.
    const auto ring = [](const Object& o) { return shapeParams<RingParams>(o) != nullptr; };
    number(
        form, "Thickness", 1, 100, 0,
        [](const Object& o) { return shapeParams<RingParams>(o)->thickness * 100; },
        [](Object& o, double v) { shapeParams<RingParams>(o)->thickness = v / 100; }, "Ring thickness", ring,
        "%");
    number(
        form, "Start angle", -360, 360, 0,
        [](const Object& o) { return shapeParams<RingParams>(o)->startAngle; },
        [](Object& o, double v) { shapeParams<RingParams>(o)->startAngle = v; }, "Ring angle", ring, "°");
    number(
        form, "End angle", -360, 360, 0, [](const Object& o) { return shapeParams<RingParams>(o)->endAngle; },
        [](Object& o, double v) { shapeParams<RingParams>(o)->endAngle = v; }, "Ring angle", ring, "°");
    // Gear.
    const auto gear = [](const Object& o) { return shapeParams<GearParams>(o) != nullptr; };
    number(
        form, "Teeth", 3, 128, 0, [](const Object& o) { return shapeParams<GearParams>(o)->teeth; },
        [](Object& o, double v) { shapeParams<GearParams>(o)->teeth = static_cast<int>(v); }, "Gear teeth",
        gear);
    number(
        form, "Tooth depth", 1, 90, 0,
        [](const Object& o) { return shapeParams<GearParams>(o)->toothDepth * 100; },
        [](Object& o, double v) { shapeParams<GearParams>(o)->toothDepth = v / 100; }, "Tooth depth", gear,
        "%");
    number(
        form, "Hole", 0, 90, 0, [](const Object& o) { return shapeParams<GearParams>(o)->holeRadius * 100; },
        [](Object& o, double v) { shapeParams<GearParams>(o)->holeRadius = v / 100; }, "Gear hole", gear,
        "%");
    // Decorative presets: up to three named parameters, ranges from the core.
    for (std::size_t i = 0; i < 3; ++i) {
        for (int k = 0; k < 7; ++k) {
            const auto kind = static_cast<SimpleKind>(k);
            const auto range = simpleParamRanges(kind)[i];
            if (!range.name)
                continue;
            const bool whole = std::string(range.name) == "Scallops" || std::string(range.name) == "Points";
            const double scale = whole ? 1 : 100;
            number(
                form, range.name, range.min * scale, range.max * scale, 0,
                [i, scale](const Object& o) { return shapeParams<SimpleParams>(o)->values[i] * scale; },
                [i, scale](Object& o, double v) { shapeParams<SimpleParams>(o)->values[i] = v / scale; },
                range.name,
                [kind](const Object& o) {
                    const auto* p = shapeParams<SimpleParams>(o);
                    return p && p->kind == kind;
                },
                whole ? QString() : QStringLiteral("%"));
        }
    }
    combo(
        form, "Fill rule", {"Non-zero", "Even-odd"},
        [](const Object& o) { return static_cast<int>(shapeParams<Path>(o)->rule); },
        [](Object& o, int v) { shapeParams<Path>(o)->rule = static_cast<FillRule>(v); }, "Fill rule",
        [](const Object& o) { return shapeParams<Path>(o) != nullptr; });
}
void Inspector::buildText() {
    auto form = section(
        "Text", [](const Object& o, std::size_t n) { return n == 1 && o.as<TextData>(); }, true);
    text_ = new QPlainTextEdit(content_);
    text_->setObjectName("textContent");
    text_->setMaximumHeight(64);
    text_->setTabChangesFocus(true);
    form->addRow(text_);
    // Commit text after a short pause so typing does not create one undo step per key.
    auto timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(350);
    connect(text_, &QPlainTextEdit::textChanged, timer, [this, timer] {
        if (!loading_)
            timer->start();
    });
    connect(timer, &QTimer::timeout, this, [this] {
        const auto value = text_->toPlainText().left(4096).toStdString();
        edit(
            "Edit text", [&](Object& o) { o.as<TextData>()->text = value; }, isKind<TextData>);
    });
    font_ = new QComboBox(content_);
    font_->setObjectName("fontFamily");
    font_->setMaxVisibleItems(24);
    // Type to search: matches anywhere in the family name; names are shown in their own typeface.
    font_->setEditable(true);
    font_->setInsertPolicy(QComboBox::NoInsert);
    font_->lineEdit()->setPlaceholderText("Search fonts…");
    auto* completer = new QCompleter(font_->model(), font_);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setMaxVisibleItems(16);
    font_->setCompleter(completer);
    connect(completer, qOverload<const QString&>(&QCompleter::activated), this, [this](const QString& text) {
        const int index = font_->findText(text, Qt::MatchExactly);
        if (index >= 0)
            emit font_->activated(index);
    });
    form->addRow("Font", font_);
    connect(font_, &QComboBox::activated, this, [this](int index) {
        const auto key = font_->itemData(index, Qt::UserRole + 1).toString().toStdString();
        const auto family = font_->itemData(index, Qt::UserRole).toString().toStdString();
        if (family.empty())
            return;
        // Fonts from the app library are embedded into the project on first use.
        if (!key.empty() && !editor_->snapshot()->fonts.contains(key))
            for (const auto& f : services_->fonts->libraryFonts())
                if (f.key == key)
                    editor_->addFont(key, f.font);
        edit(
            "Font",
            [&](Object& o) {
                auto* t = o.as<TextData>();
                t->font.family = family;
                t->font.fileKey = key;
            },
            isKind<TextData>);
    });
    loaders_.push_back([this](const Object& o) {
        const auto* t = o.as<TextData>();
        if (!t)
            return;
        loading_ = true;
        if (text_->toPlainText().toStdString() != t->text)
            text_->setPlainText(QString::fromStdString(t->text));
        refreshFonts();
        for (int i = 0; i < font_->count(); ++i)
            if (font_->itemData(i, Qt::UserRole).toString().toStdString() == t->font.family &&
                font_->itemData(i, Qt::UserRole + 1).toString().toStdString() == t->font.fileKey) {
                font_->setCurrentIndex(i);
                break;
            }
        loading_ = false;
    });
    number(
        form, "Size", .5, 4096, 1, [](const Object& o) { return o.as<TextData>()->size; },
        [](Object& o, double v) { o.as<TextData>()->size = v; }, "Text size", isKind<TextData>);
    combo(
        form, "Weight", {"Thin", "Light", "Regular", "Medium", "Semibold", "Bold", "Extra bold", "Black"},
        [](const Object& o) {
            static const int w[] = {100, 300, 400, 500, 600, 700, 800, 900};
            const int weight = o.as<TextData>()->weight;
            int best = 0;
            for (int i = 0; i < 8; ++i)
                if (std::abs(w[i] - weight) < std::abs(w[best] - weight))
                    best = i;
            return best;
        },
        [](Object& o, int v) {
            static const int w[] = {100, 300, 400, 500, 600, 700, 800, 900};
            o.as<TextData>()->weight = w[v];
        },
        "Font weight", isKind<TextData>);
    check(
        form, "Italic", [](const Object& o) { return o.as<TextData>()->italic; },
        [](Object& o, bool v) { o.as<TextData>()->italic = v; }, "Italic", isKind<TextData>);
    check(
        form, "All caps", [](const Object& o) { return o.as<TextData>()->allCaps; },
        [](Object& o, bool v) { o.as<TextData>()->allCaps = v; }, "All caps", isKind<TextData>);
    number(
        form, "Tracking", -500, 2000, 0, [](const Object& o) { return o.as<TextData>()->tracking; },
        [](Object& o, double v) { o.as<TextData>()->tracking = v; }, "Tracking", isKind<TextData>);
    number(
        form, "Line height", 10, 500, 0, [](const Object& o) { return o.as<TextData>()->lineHeight * 100; },
        [](Object& o, double v) { o.as<TextData>()->lineHeight = v / 100; }, "Line height", isKind<TextData>,
        "%");
    combo(
        form, "Align", {"Left", "Centre", "Right"},
        [](const Object& o) { return static_cast<int>(o.as<TextData>()->align); },
        [](Object& o, int v) { o.as<TextData>()->align = static_cast<TextAlign>(v); }, "Text alignment",
        isKind<TextData>);
    slider(
        form, "Arc bend", -100, 100, [](const Object& o) { return o.as<TextData>()->arc * 100; },
        [](Object& o, double v) { o.as<TextData>()->arc = v / 100; }, "Arc bend", isKind<TextData>);
}
void Inspector::refreshFonts() {
    const auto doc = editor_->snapshot();
    QString signature = QString::number(services_->fonts->libraryFonts().size());
    for (const auto& [key, f] : doc->fonts)
        signature += QString::fromStdString(key) + (f.data ? "+" : "-");
    if (font_->count() > 0 && signature == fontsSignature_)
        return;
    fontsSignature_ = signature;
    const QSignalBlocker block(font_);
    font_->clear();
    auto* model = qobject_cast<QStandardItemModel*>(font_->model());
    const auto header = [&](const QString& title) {
        font_->addItem(title);
        if (model)
            model->item(font_->count() - 1)->setEnabled(false);
    };
    std::set<std::string> added;
    if (!doc->fonts.empty()) {
        header("— Project fonts —");
        for (const auto& [key, f] : doc->fonts) {
            const auto family = services_->fonts->resolve({f.family, key}, *doc);
            font_->addItem(QString::fromStdString(f.family) + (family.isEmpty() ? " (missing)" : ""));
            font_->setItemData(font_->count() - 1, QString::fromStdString(f.family), Qt::UserRole);
            font_->setItemData(font_->count() - 1, QString::fromStdString(key), Qt::UserRole + 1);
            added.insert(key);
        }
    }
    const auto library = services_->fonts->libraryFonts();
    bool libraryHeader = false;
    for (const auto& f : library) {
        if (added.contains(f.key))
            continue;
        if (!libraryHeader) {
            header("— Imported —");
            libraryHeader = true;
        }
        font_->addItem(QString::fromStdString(f.font.family));
        font_->setItemData(font_->count() - 1, QString::fromStdString(f.font.family), Qt::UserRole);
        font_->setItemData(font_->count() - 1, QString::fromStdString(f.key), Qt::UserRole + 1);
    }
    header("— System —");
    for (const auto& family : QFontDatabase::families()) {
        if (QFontDatabase::isPrivateFamily(family))
            continue;
        font_->addItem(family);
        QFont preview(family);
        if (font_->font().pointSizeF() > 0)
            preview.setPointSizeF(font_->font().pointSizeF() * 1.1);
        else
            preview.setPixelSize(std::max(10, font_->font().pixelSize()) + 1);
        font_->setItemData(font_->count() - 1, preview, Qt::FontRole);
        font_->setItemData(font_->count() - 1, family, Qt::UserRole);
        font_->setItemData(font_->count() - 1, QString(), Qt::UserRole + 1);
    }
}
void Inspector::buildImage() {
    auto form = section(
        "Image", [](const Object& o, std::size_t n) { return n == 1 && o.as<ImageData>(); }, true);
    const auto img = isKind<ImageData>;
    const auto adjust = [&](const QString& label, double ImageAdjust::*field) {
        slider(
            form, label, -100, 100,
            [field](const Object& o) { return o.as<ImageData>()->adjust.*field * 100; },
            [field](Object& o, double v) { o.as<ImageData>()->adjust.*field = v / 100; }, label, img);
    };
    adjust("Brightness", &ImageAdjust::brightness);
    adjust("Contrast", &ImageAdjust::contrast);
    adjust("Saturation", &ImageAdjust::saturation);
    adjust("Hue", &ImageAdjust::hue);
    color(
        form, "Tint", [](const Object& o) { return o.as<ImageData>()->adjust.tint; },
        [](Object& o, Color c) { o.as<ImageData>()->adjust.tint = c; }, "Tint", img);
    slider(
        form, "Tint amount", 0, 100,
        [](const Object& o) { return o.as<ImageData>()->adjust.tintAmount * 100; },
        [](Object& o, double v) { o.as<ImageData>()->adjust.tintAmount = v / 100; }, "Tint amount", img);
    check(
        form, "Remove colour", [](const Object& o) { return o.as<ImageData>()->adjust.colorKey; },
        [](Object& o, bool v) { o.as<ImageData>()->adjust.colorKey = v; }, "Remove background colour", img);
    const auto keyed = [](const Object& o) {
        return o.as<ImageData>() && o.as<ImageData>()->adjust.colorKey;
    };
    color(
        form, "Key colour", [](const Object& o) { return o.as<ImageData>()->adjust.keyColor; },
        [](Object& o, Color c) { o.as<ImageData>()->adjust.keyColor = c; }, "Key colour", keyed);
    slider(
        form, "Tolerance", 0, 100,
        [](const Object& o) { return o.as<ImageData>()->adjust.keyTolerance * 100; },
        [](Object& o, double v) { o.as<ImageData>()->adjust.keyTolerance = v / 100; }, "Key tolerance",
        keyed);
    slider(
        form, "Softness", 0, 100, [](const Object& o) { return o.as<ImageData>()->adjust.keySoftness * 100; },
        [](Object& o, double v) { o.as<ImageData>()->adjust.keySoftness = v / 100; }, "Key softness", keyed);
    const auto cropField = [&](const QString& label, int index) {
        number(
            form, label, 0, 1e5, 0,
            [index](const Object& o) {
                const auto& c = o.as<ImageData>()->crop;
                const double v[4] = {c.x0, c.y0, c.width(), c.height()};
                return c.empty() ? 0. : v[index];
            },
            [this, index](Object& o, double v) {
                auto* d = o.as<ImageData>();
                const auto found = editor_->snapshot()->images.find(d->imageKey);
                if (found == editor_->snapshot()->images.end())
                    return;
                const double iw = static_cast<double>(found->second.pixels->width),
                             ih = static_cast<double>(found->second.pixels->height);
                kantan::Box c = d->crop.empty() ? kantan::Box{0, 0, iw, ih} : d->crop;
                double x = c.x0, y = c.y0, w = c.width(), h = c.height();
                (index == 0 ? x : index == 1 ? y : index == 2 ? w : h) = v;
                x = std::clamp(x, 0., iw - 1);
                y = std::clamp(y, 0., ih - 1);
                w = std::clamp(w, 1., iw - x);
                h = std::clamp(h, 1., ih - y);
                // Keep the displayed scale: the visible region grows/shrinks with the crop.
                const double sx = d->width / c.width(), sy = d->height / c.height();
                d->crop = {x, y, x + w, y + h};
                d->width = w * sx;
                d->height = h * sy;
            },
            "Crop", img, " px");
    };
    cropField("Crop X", 0);
    cropField("Crop Y", 1);
    cropField("Crop W", 2);
    cropField("Crop H", 3);
}
void Inspector::buildFill() {
    auto form = section(
        "Fill", [](const Object& o, std::size_t) { return styled(o); }, true);
    combo(
        form, "Type", {"None", "Solid", "Linear gradient", "Radial gradient"},
        [](const Object& o) { return static_cast<int>(o.style.fill.kind); },
        [](Object& o, int v) { o.style.fill.kind = static_cast<FillKind>(v); }, "Fill type", styled);
    const auto solid = [](const Object& o) { return styled(o) && o.style.fill.kind == FillKind::Solid; };
    const auto gradient = [](const Object& o) {
        return styled(o) && (o.style.fill.kind == FillKind::Linear || o.style.fill.kind == FillKind::Radial);
    };
    color(
        form, "Colour", [](const Object& o) { return o.style.fill.color; },
        [](Object& o, Color c) { o.style.fill.color = c; }, "Fill colour", solid);
    color(
        form, "Start", [](const Object& o) { return o.style.fill.stops.front().color; },
        [](Object& o, Color c) { o.style.fill.stops.front().color = c; }, "Gradient colour", gradient);
    const auto threeStops = [](const Object& o) {
        return styled(o) && o.style.fill.kind != FillKind::None && o.style.fill.kind != FillKind::Solid &&
               o.style.fill.stops.size() >= 3;
    };
    color(
        form, "Middle",
        [](const Object& o) { return o.style.fill.stops[o.style.fill.stops.size() / 2].color; },
        [](Object& o, Color c) { o.style.fill.stops[o.style.fill.stops.size() / 2].color = c; },
        "Gradient colour", threeStops);
    color(
        form, "End", [](const Object& o) { return o.style.fill.stops.back().color; },
        [](Object& o, Color c) { o.style.fill.stops.back().color = c; }, "Gradient colour", gradient);
    check(
        form, "Middle stop", [](const Object& o) { return o.style.fill.stops.size() >= 3; },
        [](Object& o, bool v) {
            auto& s = o.style.fill.stops;
            if (v && s.size() < 3) {
                const auto& a = s.front().color;
                const auto& b = s.back().color;
                s.insert(s.begin() + 1, {.5,
                                         {static_cast<std::uint8_t>((a.r + b.r) / 2),
                                          static_cast<std::uint8_t>((a.g + b.g) / 2),
                                          static_cast<std::uint8_t>((a.b + b.b) / 2),
                                          static_cast<std::uint8_t>((a.a + b.a) / 2)}});
            } else if (!v && s.size() >= 3)
                s = {s.front(), s.back()};
        },
        "Gradient stops", gradient);
    slider(
        form, "Middle position", 1, 99,
        [](const Object& o) { return o.style.fill.stops[o.style.fill.stops.size() / 2].offset * 100; },
        [](Object& o, double v) { o.style.fill.stops[o.style.fill.stops.size() / 2].offset = v / 100; },
        "Gradient stop", threeStops);
    slider(
        form, "Angle", -180, 180, [](const Object& o) { return o.style.fill.angle; },
        [](Object& o, double v) { o.style.fill.angle = v; }, "Gradient angle",
        [](const Object& o) { return styled(o) && o.style.fill.kind == FillKind::Linear; });
    const auto radial = [](const Object& o) { return styled(o) && o.style.fill.kind == FillKind::Radial; };
    slider(
        form, "Centre X", 0, 100, [](const Object& o) { return o.style.fill.center.x * 100; },
        [](Object& o, double v) { o.style.fill.center.x = v / 100; }, "Gradient centre", radial);
    slider(
        form, "Centre Y", 0, 100, [](const Object& o) { return o.style.fill.center.y * 100; },
        [](Object& o, double v) { o.style.fill.center.y = v / 100; }, "Gradient centre", radial);
    slider(
        form, "Radius", 5, 150, [](const Object& o) { return o.style.fill.radius * 100; },
        [](Object& o, double v) { o.style.fill.radius = v / 100; }, "Gradient radius", radial);
}
void Inspector::buildOutlines() {
    auto form = section(
        "Outlines", [](const Object& o, std::size_t) { return styled(o); }, true);
    for (std::size_t i = 0; i < MaxOutlines; ++i) {
        const QString n = QString::number(i + 1);
        const auto present = [i](const Object& o) { return styled(o) && outlineAt(o, i) != nullptr; };
        const auto available = [i](const Object& o) { return styled(o) && o.style.outlines.size() >= i; };
        check(
            form, "Outline " + n, present,
            [i](Object& o, bool v) {
                if (v) {
                    Outline next;
                    if (i == 1)
                        next.paint.color = {30, 30, 40, 255};
                    o.style.outlines.resize(std::max(o.style.outlines.size(), i), next);
                    o.style.outlines.insert(o.style.outlines.begin() + static_cast<std::ptrdiff_t>(i), next);
                    o.style.outlines.resize(std::min(o.style.outlines.size(), MaxOutlines));
                } else if (i < o.style.outlines.size())
                    o.style.outlines.erase(o.style.outlines.begin() + static_cast<std::ptrdiff_t>(i));
            },
            "Outline", available);
        number(
            form, "  Width", 0, 1000, 1, [i](const Object& o) { return outlineAt(o, i)->width; },
            [i](Object& o, double v) { outlineAt(o, i).width = v; }, "Outline width", present);
        color(
            form, "  Colour", [i](const Object& o) { return outlineAt(o, i)->paint.color; },
            [i](Object& o, Color c) {
                auto& p = outlineAt(o, i).paint;
                p.kind = FillKind::Solid;
                p.color = c;
            },
            "Outline colour", present);
        combo(
            form, "  Position", {"Outside", "Centre", "Inside"},
            [i](const Object& o) { return static_cast<int>(outlineAt(o, i)->position); },
            [i](Object& o, int v) { outlineAt(o, i).position = static_cast<OutlinePosition>(v); },
            "Outline position", present);
        combo(
            form, "  Join", {"Miter", "Round", "Bevel"},
            [i](const Object& o) { return static_cast<int>(outlineAt(o, i)->join); },
            [i](Object& o, int v) { outlineAt(o, i).join = static_cast<JoinStyle>(v); }, "Outline join",
            present);
    }
}
void Inspector::buildEffects() {
    auto form = section("Effects", [](const Object& o, std::size_t) { return styled(o); });
    const auto shadow = [&](const QString& name, kantan::Shadow Effects::*field) {
        const auto on = [field](const Object& o) { return styled(o) && (o.style.effects.*field).enabled; };
        check(
            form, name, [field](const Object& o) { return (o.style.effects.*field).enabled; },
            [field](Object& o, bool v) { (o.style.effects.*field).enabled = v; }, name, styled);
        slider(
            form, "  Angle", -180, 180, [field](const Object& o) { return (o.style.effects.*field).angle; },
            [field](Object& o, double v) { (o.style.effects.*field).angle = v; }, name, on);
        number(
            form, "  Distance", 0, 500, 1,
            [field](const Object& o) { return (o.style.effects.*field).distance; },
            [field](Object& o, double v) { (o.style.effects.*field).distance = v; }, name, on);
        number(
            form, "  Blur", 0, 300, 1, [field](const Object& o) { return (o.style.effects.*field).blur; },
            [field](Object& o, double v) { (o.style.effects.*field).blur = v; }, name, on);
        slider(
            form, "  Spread", 0, 99,
            [field](const Object& o) { return (o.style.effects.*field).spread * 100; },
            [field](Object& o, double v) { (o.style.effects.*field).spread = v / 100; }, name, on);
        color(
            form, "  Colour", [field](const Object& o) { return (o.style.effects.*field).color; },
            [field](Object& o, Color c) { (o.style.effects.*field).color = c; }, name, on);
        slider(
            form, "  Opacity", 0, 100,
            [field](const Object& o) { return (o.style.effects.*field).opacity * 100; },
            [field](Object& o, double v) { (o.style.effects.*field).opacity = v / 100; }, name, on);
    };
    const auto glow = [&](const QString& name, Glow Effects::*field) {
        const auto on = [field](const Object& o) { return styled(o) && (o.style.effects.*field).enabled; };
        check(
            form, name, [field](const Object& o) { return (o.style.effects.*field).enabled; },
            [field](Object& o, bool v) { (o.style.effects.*field).enabled = v; }, name, styled);
        number(
            form, "  Size", 0, 300, 1, [field](const Object& o) { return (o.style.effects.*field).size; },
            [field](Object& o, double v) { (o.style.effects.*field).size = v; }, name, on);
        slider(
            form, "  Spread", 0, 99,
            [field](const Object& o) { return (o.style.effects.*field).spread * 100; },
            [field](Object& o, double v) { (o.style.effects.*field).spread = v / 100; }, name, on);
        color(
            form, "  Colour", [field](const Object& o) { return (o.style.effects.*field).color; },
            [field](Object& o, Color c) { (o.style.effects.*field).color = c; }, name, on);
        slider(
            form, "  Opacity", 0, 100,
            [field](const Object& o) { return (o.style.effects.*field).opacity * 100; },
            [field](Object& o, double v) { (o.style.effects.*field).opacity = v / 100; }, name, on);
    };
    shadow("Drop shadow", &Effects::dropShadow);
    glow("Outer glow", &Effects::outerGlow);
    shadow("Inner shadow", &Effects::innerShadow);
    glow("Inner glow", &Effects::innerGlow);
    const auto bevelOn = [](const Object& o) { return styled(o) && o.style.effects.bevel.enabled; };
    check(
        form, "Bevel & emboss", [](const Object& o) { return o.style.effects.bevel.enabled; },
        [](Object& o, bool v) { o.style.effects.bevel.enabled = v; }, "Bevel", styled);
    combo(
        form, "  Style", {"Inner bevel", "Outer bevel", "Emboss", "Pillow"},
        [](const Object& o) { return static_cast<int>(o.style.effects.bevel.style); },
        [](Object& o, int v) { o.style.effects.bevel.style = static_cast<BevelStyle>(v); }, "Bevel style",
        bevelOn);
    combo(
        form, "  Profile", {"Linear", "Round", "Cove"},
        [](const Object& o) { return static_cast<int>(o.style.effects.bevel.profile); },
        [](Object& o, int v) { o.style.effects.bevel.profile = static_cast<BevelProfile>(v); },
        "Bevel profile", bevelOn);
    number(
        form, "  Size", 0, 300, 1, [](const Object& o) { return o.style.effects.bevel.size; },
        [](Object& o, double v) { o.style.effects.bevel.size = v; }, "Bevel size", bevelOn);
    slider(
        form, "  Depth", 0, 300, [](const Object& o) { return o.style.effects.bevel.depth * 100; },
        [](Object& o, double v) { o.style.effects.bevel.depth = v / 100; }, "Bevel depth", bevelOn);
    number(
        form, "  Softness", 0, 100, 1, [](const Object& o) { return o.style.effects.bevel.softness; },
        [](Object& o, double v) { o.style.effects.bevel.softness = v; }, "Bevel softness", bevelOn);
    slider(
        form, "  Light angle", -180, 180, [](const Object& o) { return o.style.effects.bevel.lightAngle; },
        [](Object& o, double v) { o.style.effects.bevel.lightAngle = v; }, "Bevel light", bevelOn);
    slider(
        form, "  Altitude", 0, 90, [](const Object& o) { return o.style.effects.bevel.altitude; },
        [](Object& o, double v) { o.style.effects.bevel.altitude = v; }, "Bevel light", bevelOn);
    color(
        form, "  Highlight", [](const Object& o) { return o.style.effects.bevel.highlight; },
        [](Object& o, Color c) { o.style.effects.bevel.highlight = c; }, "Bevel highlight", bevelOn);
    slider(
        form, "  Highlight %", 0, 100,
        [](const Object& o) { return o.style.effects.bevel.highlightOpacity * 100; },
        [](Object& o, double v) { o.style.effects.bevel.highlightOpacity = v / 100; }, "Bevel highlight",
        bevelOn);
    color(
        form, "  Shadow", [](const Object& o) { return o.style.effects.bevel.shadow; },
        [](Object& o, Color c) { o.style.effects.bevel.shadow = c; }, "Bevel shadow", bevelOn);
    slider(
        form, "  Shadow %", 0, 100, [](const Object& o) { return o.style.effects.bevel.shadowOpacity * 100; },
        [](Object& o, double v) { o.style.effects.bevel.shadowOpacity = v / 100; }, "Bevel shadow", bevelOn);
}
void Inspector::buildAppearance() {
    auto form = section("Appearance", [](const Object&, std::size_t) { return true; });
    slider(
        form, "Opacity", 0, 100, [](const Object& o) { return o.opacity * 100; },
        [](Object& o, double v) { o.opacity = v / 100; }, "Opacity");
    combo(
        form, "Blend", {"Normal", "Multiply", "Screen", "Overlay", "Add"},
        [](const Object& o) { return static_cast<int>(o.blend); },
        [](Object& o, int v) { o.blend = static_cast<Blend>(v); }, "Blend mode");
    number(
        form, "Hide below", 0, 8192, 0, [](const Object& o) { return o.minExportSize; },
        [](Object& o, double v) { o.minExportSize = v; }, "Size visibility", {}, " px");
    number(
        form, "Hide above", 0, 8192, 0, [](const Object& o) { return o.maxExportSize; },
        [](Object& o, double v) { o.maxExportSize = v; }, "Size visibility", {}, " px");
    number(
        form, "Radial copies", 1, 360, 0, [](const Object& o) { return o.repeat.count; },
        [](Object& o, double v) { o.repeat.count = static_cast<int>(v); }, "Radial repeat");
    const auto repeated = [](const Object& o) { return o.repeat.count > 1; };
    number(
        form, "  Pivot X", -1e5, 1e5, 1, [](const Object& o) { return o.repeat.pivot.x; },
        [](Object& o, double v) { o.repeat.pivot.x = v; }, "Repeat pivot", repeated);
    number(
        form, "  Pivot Y", -1e5, 1e5, 1, [](const Object& o) { return o.repeat.pivot.y; },
        [](Object& o, double v) { o.repeat.pivot.y = v; }, "Repeat pivot", repeated);
}
void Inspector::focusText() {
    refresh();
    if (text_->isVisible()) {
        text_->setFocus();
        text_->selectAll();
    }
}
void Inspector::refresh() {
    const auto* o = primary();
    const auto count = editor_->selection().size();
    const auto doc = editor_->preview();
    const bool locked = std::any_of(editor_->selection().begin(), editor_->selection().end(),
                                    [&](ObjectId id) { return doc->effectiveLocked(id); });
    lockedNotice_->setVisible(locked);
    empty_->setVisible(!o);
    for (auto& s : sections_) {
        const bool show = o && s.visible(*o, count);
        s.header->setVisible(show);
        s.box->setVisible(show && s.header->isChecked());
        s.box->setEnabled(!locked);
    }
    if (!o)
        return;
    loading_ = true;
    for (const auto& load : loaders_) {
        try {
            load(*o);
        } catch (...) {
        }
    }
    for (const auto& [widget, applies] : rows_) {
        bool show = applies(*o);
        if (auto* form = qobject_cast<QFormLayout*>(widget->parentWidget() ? widget->parentWidget()->layout()
                                                                           : nullptr))
            form->setRowVisible(widget, show);
        else
            widget->setVisible(show);
    }
    loading_ = false;
}
} // namespace kantan::app

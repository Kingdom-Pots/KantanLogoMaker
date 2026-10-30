#include "app/CanvasView.h"
#include "app/Presets.h"
#include "io/Images.h"
#include <QContextMenuEvent>
#include <QCursor>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QTimer>
#include <numbers>

namespace kantan::app {
namespace {
constexpr double HandleRadius = 5;
const QColor Accent(105, 183, 211);
const QColor Snap(236, 90, 190);
QPointF qp(Point p) {
    return {p.x, p.y};
}
Point kp(QPointF p) {
    return {p.x(), p.y()};
}
Point localPoint(const Box& b, int h) {
    const double xs[3] = {b.x0, (b.x0 + b.x1) / 2, b.x1}, ys[3] = {b.y0, (b.y0 + b.y1) / 2, b.y1};
    static const int ix[8] = {0, 1, 2, 2, 2, 1, 0, 0}, iy[8] = {0, 0, 0, 1, 2, 2, 2, 1};
    return {xs[ix[h]], ys[iy[h]]};
}
bool cornerHandle(int h) {
    return h % 2 == 0;
}
QPainterPath painterPath(const Polygons& polygons, const QTransform& t, bool close = true) {
    QPainterPath path;
    for (const auto& poly : polygons) {
        if (poly.empty())
            continue;
        path.moveTo(t.map(qp(poly[0])));
        for (std::size_t i = 1; i < poly.size(); ++i)
            path.lineTo(t.map(qp(poly[i])));
        if (close)
            path.closeSubpath();
    }
    return path;
}
Path* editablePath(Object& o) {
    if (auto* s = o.as<ShapeData>())
        return std::get_if<Path>(&s->geometry);
    return nullptr;
}
const Path* editablePath(const Object& o) {
    if (const auto* s = o.as<ShapeData>())
        return std::get_if<Path>(&s->geometry);
    return nullptr;
}
// Split a cubic at t (de Casteljau), returning the new middle node and updated neighbour handles.
void splitSegment(SubPath& s, std::size_t i, double t) {
    const std::size_t n = s.nodes.size();
    Node& a = s.nodes[i];
    Node& b = s.nodes[(i + 1) % n];
    const Point p0 = a.anchor, p1 = a.out, p2 = b.in, p3 = b.anchor;
    const auto lerp = [t](Point u, Point v) { return u + (v - u) * t; };
    const Point p01 = lerp(p0, p1), p12 = lerp(p1, p2), p23 = lerp(p2, p3);
    const Point p012 = lerp(p01, p12), p123 = lerp(p12, p23), mid = lerp(p012, p123);
    const bool straight = a.out == a.anchor && b.in == b.anchor;
    Node m{mid, straight ? mid : p012, straight ? mid : p123, straight ? NodeType::Corner : NodeType::Smooth};
    if (!straight) {
        a.out = p01;
        b.in = p23;
    }
    s.nodes.insert(s.nodes.begin() + static_cast<std::ptrdiff_t>(i + 1), m);
}
Point cubicAt(Point p0, Point p1, Point p2, Point p3, double t) {
    const double u = 1 - t;
    return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
}
} // namespace
CanvasView::CanvasView(std::shared_ptr<Editor> editor, std::shared_ptr<AppServices> services, QWidget* parent)
    : QWidget(parent), editor_(std::move(editor)), services_(std::move(services)),
      shapeStyle_(defaultShapeStyle()), textStyle_(defaultTextStyle()) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
    updateCursor();
}
void CanvasView::setTool(Tool tool) {
    if (tool == tool_)
        return;
    cancelGesture();
    if (penActive())
        finishPen(false);
    tool_ = tool;
    node_.reset();
    updateCursor();
    update();
}
void CanvasView::setShapePreset(int index) {
    shapePreset_ = std::clamp(index, 0, static_cast<int>(shapePresets().size()) - 1);
}
QTransform CanvasView::viewTransform() const {
    return QTransform::fromTranslate(pan_.x(), pan_.y()).scale(zoom_, zoom_);
}
Affine CanvasView::viewAffine() const {
    return Affine::translation(pan_.x(), pan_.y()) * Affine::scaling(zoom_, zoom_);
}
QPointF CanvasView::toArtboard(QPointF v) const {
    return (v - pan_) / zoom_;
}
QPointF CanvasView::toView(QPointF a) const {
    return a * zoom_ + pan_;
}
double CanvasView::tolerance() const {
    return 6 / zoom_;
}
void CanvasView::report(Result r) {
    if (!r && !r.message.empty())
        emit feedback(QString::fromStdString(r.message));
}
void CanvasView::fit() {
    const auto doc = editor_->snapshot();
    const double w = doc->artboard.width, h = doc->artboard.height;
    zoom_ = std::clamp(std::min((width() - 80.) / w, (height() - 80.) / h), .02, 64.);
    pan_ = QPointF((width() - w * zoom_) / 2, (height() - h * zoom_) / 2);
    refresh();
    emit viewChanged();
}
void CanvasView::zoomAt(double zoom, QPointF anchor) {
    const auto before = toArtboard(anchor);
    zoom_ = std::clamp(zoom, .02, 64.);
    pan_ += anchor - toView(before);
    refresh();
    emit viewChanged();
}
void CanvasView::zoomBy(double factor) {
    zoomAt(zoom_ * factor, QPointF(width() / 2., height() / 2.));
}
void CanvasView::render() {
    const double dpr = devicePixelRatioF();
    const int w = static_cast<int>(std::ceil(width() * dpr)), h = static_cast<int>(std::ceil(height() * dpr));
    const auto document = editor_->preview();
    if (document == renderedDocument_ && renderedTransform_ == viewTransform() &&
        cache_.size() == QSize(w, h) && cache_.devicePixelRatioF() == dpr)
        return;
    try {
        RenderRequest request;
        request.width = w;
        request.height = h;
        request.toPixels = Affine::scaling(dpr, dpr) * viewAffine();
        const auto image = kantan::render(*document, request, services_->render(), services_->budget);
        cache_ = io::toQImage(image);
        cache_.setDevicePixelRatio(dpr);
        renderedDocument_ = document;
        renderedTransform_ = viewTransform();
    } catch (const std::exception& e) {
        cache_ = {};
        renderedDocument_.reset();
        emit feedback(QString::fromUtf8(e.what()));
    }
}
void CanvasView::fitSelection() {
    const auto bounds = selectionBounds(*editor_->preview(), editor_->selection(), services_->render());
    if (bounds.empty())
        return;
    zoom_ =
        std::clamp(std::min((width() - 80.) / bounds.width(), (height() - 80.) / bounds.height()), .02, 32.);
    pan_ = QPointF(width() / 2., height() / 2.) - qp(bounds.center()) * zoom_;
    refresh();
    emit viewChanged();
}
void CanvasView::refresh() {
    render();
    update();
}
void CanvasView::resizeEvent(QResizeEvent*) {
    render();
}
CanvasView::Frame CanvasView::selectionFrame() const {
    const auto doc = editor_->preview();
    const auto& selection = editor_->selection();
    if (selection.empty())
        return {};
    const auto services = services_->render();
    if (selection.size() == 1) {
        const auto* o = doc->find(selection[0]);
        if (!o)
            return {};
        const Affine world = doc->worldMatrix(o->id);
        if (world.invertible() && o->transform.matrix().invertible()) {
            const auto area = objectArea(*doc, *o, o->transform.matrix().inverted(), .05, services);
            Box b = bounds(area.polygons);
            if (b.empty() && o->as<ShapeData>())
                b = bounds(flatten(shapePath(o->as<ShapeData>()->geometry, o->as<ShapeData>()->width,
                                             o->as<ShapeData>()->height),
                                   .05));
            if (!b.empty())
                return {world, b, true};
        }
    }
    const Box b = selectionBounds(*doc, selection, services);
    return {Affine{}, b, !b.empty()};
}
std::array<QPointF, 8> CanvasView::handlePoints(const Frame& f) const {
    std::array<QPointF, 8> out;
    for (int h = 0; h < 8; ++h)
        out[static_cast<std::size_t>(h)] = toView(qp(f.local.map(localPoint(f.box, h))));
    return out;
}
QPointF CanvasView::rotationHandle(const Frame& f) const {
    const QPointF top = toView(qp(f.local.map(localPoint(f.box, 1))));
    const QPointF center = toView(qp(f.local.map(f.box.center())));
    QPointF dir = top - center;
    const double len = std::hypot(dir.x(), dir.y());
    dir = len > 1e-6 ? dir / len : QPointF(0, -1);
    return top + dir * 22;
}
std::optional<int> CanvasView::handleAt(QPointF v, const Frame& f) const {
    if (!f.valid)
        return std::nullopt;
    const auto rot = rotationHandle(f);
    if (QLineF(v, rot).length() <= HandleRadius + 3)
        return 8;
    const auto points = handlePoints(f);
    for (int h = 0; h < 8; ++h)
        if (QLineF(v, points[static_cast<std::size_t>(h)]).length() <= HandleRadius + 2)
            return h;
    return std::nullopt;
}
std::optional<std::pair<bool, std::size_t>> CanvasView::guideAt(QPointF v) const {
    const auto& a = editor_->snapshot()->artboard;
    for (std::size_t i = 0; i < a.guidesX.size(); ++i)
        if (std::abs(toView({a.guidesX[i], 0}).x() - v.x()) <= 4)
            return std::pair{true, i};
    for (std::size_t i = 0; i < a.guidesY.size(); ++i)
        if (std::abs(toView({0, a.guidesY[i]}).y() - v.y()) <= 4)
            return std::pair{false, i};
    return std::nullopt;
}
QPointF CanvasView::fromLocal01(const Frame& f, Point u) const {
    return toView(qp(f.local.map({f.box.x0 + u.x * f.box.width(), f.box.y0 + u.y * f.box.height()})));
}
Point CanvasView::toLocal01(const Frame& f, QPointF view) const {
    if (!f.local.invertible() || f.box.empty())
        return {};
    const Point l = f.local.inverted().map(kp(toArtboard(view)));
    return {(l.x - f.box.x0) / f.box.width(), (l.y - f.box.y0) / f.box.height()};
}
CanvasView::GradientHandles CanvasView::gradientHandles() const {
    const auto& sel = editor_->selection();
    if (sel.size() != 1 || tool_ != Tool::Select)
        return {};
    const auto* o = editor_->preview()->find(sel[0]);
    if (!o || (o->style.fill.kind != FillKind::Linear && o->style.fill.kind != FillKind::Radial))
        return {};
    const auto frame = selectionFrame();
    if (!frame.valid || frame.local == Affine{})
        return {};
    const auto& paint = o->style.fill;
    GradientHandles g{true, paint.kind == FillKind::Radial, frame, {.5, .5}, {}};
    if (g.radial) {
        g.center = paint.center;
        g.end = paint.center + Point{paint.radius, 0};
    } else {
        // The end handle sits half the gradient length along its direction (see shade()).
        const double r = paint.angle * std::numbers::pi / 180;
        const Point dir{std::cos(r), std::sin(r)};
        const double extent = std::abs(dir.x) + std::abs(dir.y);
        g.end = g.center + dir * (.5 * extent);
    }
    return g;
}
const Object* CanvasView::directTarget() const {
    const auto& sel = editor_->selection();
    if (sel.size() != 1)
        return nullptr;
    const auto* o = editor_->preview()->find(sel[0]);
    return o && editablePath(*o) ? o : nullptr;
}
std::optional<std::pair<NodeRef, int>> CanvasView::nodeHandleAt(QPointF v) const {
    const auto* o = directTarget();
    if (!o)
        return std::nullopt;
    const Affine m = viewAffine() * editor_->preview()->worldMatrix(o->id);
    const auto* path = editablePath(*o);
    const auto near = [&](Point p) { return QLineF(v, qp(m.map(p))).length() <= HandleRadius + 2; };
    if (node_ && node_->subpath < path->subpaths.size() &&
        node_->node < path->subpaths[node_->subpath].nodes.size()) {
        const auto& n = path->subpaths[node_->subpath].nodes[node_->node];
        if (n.in != n.anchor && near(n.in))
            return std::pair{*node_, 1};
        if (n.out != n.anchor && near(n.out))
            return std::pair{*node_, 2};
    }
    for (std::size_t s = 0; s < path->subpaths.size(); ++s)
        for (std::size_t i = 0; i < path->subpaths[s].nodes.size(); ++i)
            if (near(path->subpaths[s].nodes[i].anchor))
                return std::pair{NodeRef{s, i}, 0};
    return std::nullopt;
}
void CanvasView::beginNodeDrag(NodeRef node, int part, QPointF) {
    const auto* o = directTarget();
    if (!o || !editor_->begin("Edit path"))
        return;
    nodeObject_ = *editor_->snapshot()->find(o->id);
    node_ = node;
    nodePart_ = part;
    drag_ = Drag::Node;
}
void CanvasView::updateNodeDrag(QPointF v, bool constrain) {
    const auto doc = editor_->snapshot();
    const Affine world = doc->worldMatrix(nodeObject_.id);
    if (!world.invertible())
        return;
    Object o = nodeObject_;
    auto* path = editablePath(o);
    auto& n = path->subpaths[node_->subpath].nodes[node_->node];
    Point p = world.inverted().map(kp(toArtboard(v)));
    if (nodePart_ == 0) {
        const Point d = p - n.anchor;
        n.anchor = p;
        n.in = n.in + d;
        n.out = n.out + d;
    } else {
        Point& moving = nodePart_ == 1 ? n.in : n.out;
        Point& other = nodePart_ == 1 ? n.out : n.in;
        if (constrain) { // Shift: snap the handle angle to 45 degree steps
            const Point d = p - n.anchor;
            const double angle =
                std::round(std::atan2(d.y, d.x) / (std::numbers::pi / 4)) * (std::numbers::pi / 4);
            p = n.anchor + Point{std::cos(angle), std::sin(angle)} * length(d);
        }
        moving = p;
        if (n.type == NodeType::Smooth) {
            const double keep = length(other - n.anchor);
            const Point dir = n.anchor - p;
            const double len = length(dir);
            if (len > 1e-9)
                other = n.anchor + dir * (keep / len);
        }
    }
    const Box b = bounds(*path);
    auto* s = o.as<ShapeData>();
    s->width = b.empty() ? 0 : b.width();
    s->height = b.empty() ? 0 : b.height();
    report(editor_->preview({o}));
    refresh();
}
Result CanvasView::insertNodeAt(QPointF v) {
    const auto* target = directTarget();
    if (!target)
        return {Status::NoChange, {}};
    Object o = *editor_->snapshot()->find(target->id);
    const Affine m = viewAffine() * editor_->snapshot()->worldMatrix(o.id);
    auto* path = editablePath(o);
    double best = 8;
    std::optional<std::tuple<std::size_t, std::size_t, double>> hit;
    for (std::size_t s = 0; s < path->subpaths.size(); ++s) {
        const auto& sub = path->subpaths[s];
        const std::size_t n = sub.nodes.size();
        const std::size_t segments = sub.closed ? n : (n ? n - 1 : 0);
        for (std::size_t i = 0; i < segments; ++i) {
            const auto& a = sub.nodes[i];
            const auto& b = sub.nodes[(i + 1) % n];
            for (int k = 1; k < 32; ++k) {
                const double t = k / 32.;
                const Point p = m.map(cubicAt(a.anchor, a.out, b.in, b.anchor, t));
                const double d = QLineF(v, qp(p)).length();
                if (d < best) {
                    best = d;
                    hit = std::tuple{s, i, t};
                }
            }
        }
    }
    if (!hit)
        return {Status::NoChange, {}};
    auto [s, i, t] = *hit;
    splitSegment(path->subpaths[s], i, t);
    node_ = NodeRef{s, i + 1};
    return editor_->update("Add node", {o});
}
Result CanvasView::deleteNode() {
    const auto* target = directTarget();
    if (!target || !node_)
        return {Status::NoChange, {}};
    Object o = *editor_->snapshot()->find(target->id);
    auto* path = editablePath(o);
    if (node_->subpath >= path->subpaths.size())
        return {Status::NoChange, {}};
    auto& sub = path->subpaths[node_->subpath];
    if (node_->node >= sub.nodes.size())
        return {Status::NoChange, {}};
    sub.nodes.erase(sub.nodes.begin() + static_cast<std::ptrdiff_t>(node_->node));
    if (sub.nodes.size() < 2)
        path->subpaths.erase(path->subpaths.begin() + static_cast<std::ptrdiff_t>(node_->subpath));
    node_.reset();
    if (path->empty())
        return editor_->remove({o.id});
    const Box b = bounds(*path);
    o.as<ShapeData>()->width = b.width();
    o.as<ShapeData>()->height = b.height();
    return editor_->update("Delete node", {o});
}
Result CanvasView::toggleNodeType() {
    const auto* target = directTarget();
    if (!target || !node_)
        return {Status::NoChange, {}};
    Object o = *editor_->snapshot()->find(target->id);
    auto* path = editablePath(o);
    auto& sub = path->subpaths[node_->subpath];
    const std::size_t n = sub.nodes.size();
    auto& node = sub.nodes[node_->node];
    if (node.type == NodeType::Smooth) {
        node.type = NodeType::Corner;
        node.in = node.out = node.anchor;
    } else {
        const Point prev = sub.nodes[(node_->node + n - 1) % n].anchor,
                    next = sub.nodes[(node_->node + 1) % n].anchor;
        const Point tangent = (next - prev) * (1. / 6.);
        node.type = NodeType::Smooth;
        node.in = node.anchor - tangent;
        node.out = node.anchor + tangent;
    }
    return editor_->update("Toggle node", {o});
}
Box CanvasView::drawBox(QPointF from, QPointF to, Qt::KeyboardModifiers mods) const {
    Point a = kp(from), b = kp(to);
    const auto doc = editor_->snapshot();
    a = snapPoint(*doc, a, {}, snap_, tolerance(), services_->render());
    b = snapPoint(*doc, b, {}, snap_, tolerance(), services_->render());
    Point d = b - a;
    if (mods & Qt::ShiftModifier) {
        const double s = std::max(std::abs(d.x), std::abs(d.y));
        d = {d.x < 0 ? -s : s, d.y < 0 ? -s : s};
    }
    if (mods & Qt::AltModifier)
        return {a.x - std::abs(d.x), a.y - std::abs(d.y), a.x + std::abs(d.x), a.y + std::abs(d.y)};
    return {std::min(a.x, a.x + d.x), std::min(a.y, a.y + d.y), std::max(a.x, a.x + d.x),
            std::max(a.y, a.y + d.y)};
}
void CanvasView::commitDraw(QPointF from, QPointF to, Qt::KeyboardModifiers mods) {
    const ShapePreset preset = tool_ == Tool::Ellipse
                                   ? ShapePreset{"Ellipse", EllipseParams{}, 200, 200}
                                   : shapePresets()[static_cast<std::size_t>(shapePreset_)];
    Box box = drawBox(from, to, mods);
    if (QLineF(toView(from), toView(to)).length() < 4) {
        const Point c = kp(from);
        box = {c.x - preset.width / 2, c.y - preset.height / 2, c.x + preset.width / 2,
               c.y + preset.height / 2};
    }
    Object o;
    o.name = preset.name.toStdString();
    o.style = shapeStyle_;
    o.transform.position = box.center();
    o.data = ShapeData{box.width(), box.height(), preset.geometry};
    report(editor_->add({o}));
    emit changed();
}
void CanvasView::finishPen(bool close) {
    if (pen_.nodes.size() >= 2) {
        Path path;
        SubPath s = pen_;
        s.closed = close;
        path.subpaths.push_back(s);
        const Box b = bounds(path);
        const Point c = b.center();
        Object o;
        o.name = close ? "Path" : "Line";
        o.style = shapeStyle_;
        if (!close) {
            o.style.fill.kind = FillKind::None;
            o.style.outlines = {{true, 4,
                                 o.style.fill.kind == FillKind::None ? shapeStyle_.fill : o.style.fill,
                                 OutlinePosition::Center, JoinStyle::Round}};
            o.style.outlines[0].paint.kind = FillKind::Solid;
        }
        o.transform.position = c;
        o.data = ShapeData{b.width(), b.height(), transformed(path, Affine::translation(-c.x, -c.y))};
        report(editor_->add({o}));
        emit changed();
    }
    pen_ = {};
    penDragging_ = false;
    drag_ = Drag::None;
    update();
}
void CanvasView::cancelGesture() {
    if (editor_->busy()) {
        editor_->cancel();
        emit changed();
    }
    drag_ = Drag::None;
    marquee_ = {};
    snapHint_ = {};
    pen_ = {};
    penDragging_ = false;
    refresh();
    updateCursor();
}
QColor CanvasView::sampleColor(QPointF v) const {
    const auto doc = editor_->snapshot();
    const double dpr = cache_.devicePixelRatio();
    const QPoint px(static_cast<int>(v.x() * dpr), static_cast<int>(v.y() * dpr));
    QColor c = cache_.valid(px) ? cache_.pixelColor(px) : QColor(Qt::transparent);
    if (c.alpha() < 255 && !doc->artboard.transparent) {
        const auto bg = doc->artboard.background;
        const double a = c.alphaF();
        c = QColor::fromRgbF(static_cast<float>(c.redF() * a + bg.r / 255. * (1 - a)),
                             static_cast<float>(c.greenF() * a + bg.g / 255. * (1 - a)),
                             static_cast<float>(c.blueF() * a + bg.b / 255. * (1 - a)), 1.f);
    }
    return c;
}
void CanvasView::pickColor(QPointF v) {
    const auto c = sampleColor(v);
    if (c.alpha() == 0) {
        emit feedback("Nothing to sample here");
        return;
    }
    emit colorPicked(c);
}
void CanvasView::updateHover(QPointF v) {
    ObjectId id;
    if (tool_ == Tool::Select || tool_ == Tool::Direct) {
        const auto doc = editor_->snapshot();
        id = hitTest(*doc, kp(toArtboard(v)), tolerance(), services_->render());
        if (id && tool_ == Tool::Select)
            id = selectableAncestor(*doc, id);
    }
    if (id != hover_) {
        hover_ = id;
        update();
    }
}
void CanvasView::updateCursor() {
    if (space_ || tool_ == Tool::Hand)
        setCursor(drag_ == Drag::Pan ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    else if (altPick_ || tool_ == Tool::Eyedropper)
        setCursor(Qt::CrossCursor);
    else if (tool_ == Tool::Pen || tool_ == Tool::Shape || tool_ == Tool::Ellipse)
        setCursor(Qt::CrossCursor);
    else if (tool_ == Tool::Text)
        setCursor(Qt::IBeamCursor);
    else
        setCursor(Qt::ArrowCursor);
}
void CanvasView::updateHandleCursor(int handle, const Frame& frame) {
    if (handle == 8) {
        static const QCursor rotate = [] {
            QPixmap pixmap(64, 64);
            pixmap.setDevicePixelRatio(2);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            const QRectF circle(7, 7, 18, 18);
            path.arcMoveTo(circle, 30);
            path.arcTo(circle, 30, 280);
            const QPointF tip = path.currentPosition();
            const QPointF tangent(.766, -.643), normal(.643, .766);
            path.moveTo(tip - tangent * 6 + normal * 3);
            path.lineTo(tip);
            path.lineTo(tip - tangent * 6 - normal * 3);
            painter.setPen(QPen(Qt::black, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
            painter.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
            return QCursor(pixmap, 16, 16);
        }();
        setCursor(rotate);
        return;
    }
    const Point direction = frame.local.mapVector(localPoint({-1, -1, 1, 1}, handle));
    const int axis =
        (static_cast<int>(std::lround(std::atan2(direction.y, direction.x) * 4 / std::numbers::pi)) % 4 + 4) %
        4;
    static const Qt::CursorShape cursors[]{Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor,
                                           Qt::SizeBDiagCursor};
    setCursor(cursors[axis]);
}
void CanvasView::mousePressEvent(QMouseEvent* e) {
    setFocus();
    const QPointF v = e->position();
    press_ = last_ = v;
    pressArtboard_ = toArtboard(v);
    modifiers_ = e->modifiers();
    if (e->button() == Qt::MiddleButton ||
        (e->button() == Qt::LeftButton && (space_ || tool_ == Tool::Hand))) {
        drag_ = Drag::Pan;
        updateCursor();
        return;
    }
    if (e->button() != Qt::LeftButton || editor_->busy())
        return;
    const auto doc = editor_->snapshot();
    const auto services = services_->render();
    if (altPick_ && tool_ == Tool::Select) {
        // Alt-click samples a colour; Alt-drag on an object duplicates it and moves the copy.
        ObjectId hit = hitTest(*doc, kp(pressArtboard_), tolerance(), services);
        if (hit) {
            hit = selectableAncestor(*doc, hit);
            altPending_ = hit;
            return;
        }
        pickColor(v);
        return;
    }
    if (tool_ == Tool::Eyedropper) {
        pickColor(v);
        return;
    }
    switch (tool_) {
    case Tool::Zoom:
        zoomAt(zoom_ * ((e->modifiers() & Qt::AltModifier) ? 1 / 1.5 : 1.5), v);
        return;
    case Tool::Text: {
        Object o;
        o.name = "Text";
        o.style = textStyle_;
        o.transform.position = kp(pressArtboard_);
        TextData t;
        if (!QFontDatabase::hasFamily(QString::fromStdString(t.font.family)))
            t.font.family = io::FontLibrary::fallbackFamily().toStdString();
        o.data = t;
        report(editor_->add({o}));
        emit changed();
        if (!editor_->created().empty())
            editTextInline(editor_->created().front(), true);
        return;
    }
    case Tool::Image:
        emit requestPlaceImage(pressArtboard_);
        return;
    case Tool::Shape:
    case Tool::Ellipse:
        drag_ = Drag::Draw;
        return;
    case Tool::Pen: {
        Point p = snapPoint(*doc, kp(pressArtboard_), {}, snap_, tolerance(), services);
        if (pen_.nodes.size() >= 2 &&
            QLineF(v, toView(qp(pen_.nodes.front().anchor))).length() <= HandleRadius + 3) {
            finishPen(true);
            return;
        }
        pen_.nodes.push_back(Node::corner(p));
        penDragging_ = true;
        drag_ = Drag::Pen;
        update();
        return;
    }
    case Tool::Direct: {
        if (auto handle = nodeHandleAt(v)) {
            beginNodeDrag(handle->first, handle->second, v);
            update();
            return;
        }
        if (e->modifiers() & Qt::AltModifier) {
            report(insertNodeAt(v));
            emit changed();
            refresh();
            return;
        }
        const auto id = hitTest(*doc, kp(pressArtboard_), tolerance(), services);
        node_.reset();
        report(editor_->select(id ? std::vector<ObjectId>{id} : std::vector<ObjectId>{}));
        if (id) {
            const auto* o = doc->find(id);
            if (o && !editablePath(*o) && (o->as<ShapeData>() || o->as<TextData>()))
                emit feedback("Use Path › Convert to path to edit this object's nodes");
        }
        emit changed();
        update();
        return;
    }
    case Tool::Select:
    default:
        break;
    }
    // Select tool: gradient handles, transform handles, guides, objects, marquee.
    if (const auto g = gradientHandles(); g.valid) {
        const bool onEnd = QLineF(v, fromLocal01(g.frame, g.end)).length() <= HandleRadius + 2;
        const bool onCenter =
            g.radial && QLineF(v, fromLocal01(g.frame, g.center)).length() <= HandleRadius + 2;
        if ((onEnd || onCenter) && editor_->begin("Gradient")) {
            dragFrame_ = g.frame;
            dragTargets_ = editor_->selection();
            gradientPart_ = onEnd ? 1 : 0;
            drag_ = Drag::Handle;
            return;
        }
    }
    const auto frame = selectionFrame();
    if (auto h = handleAt(v, frame); h && !editor_->selection().empty()) {
        dragTargets_ = editor_->selection();
        dragFrame_ = frame;
        handle_ = *h;
        if (editor_->begin(*h == 8 ? "Rotate" : "Scale"))
            drag_ = *h == 8 ? Drag::Rotate : Drag::Scale;
        updateHandleCursor(*h, frame);
        return;
    }
    if (auto g = guideAt(v)) {
        guide_ = *g;
        guideValue_ = g->first ? doc->artboard.guidesX[g->second] : doc->artboard.guidesY[g->second];
        drag_ = Drag::Guide;
        return;
    }
    ObjectId hit = hitTest(*doc, kp(pressArtboard_), tolerance(), services);
    if (hit && !(e->modifiers() & Qt::ControlModifier))
        hit = selectableAncestor(*doc, hit);
    auto selection = editor_->selection();
    const bool selected = std::find(selection.begin(), selection.end(), hit) != selection.end();
    if (hit) {
        if (e->modifiers() & Qt::ShiftModifier) {
            if (selected)
                std::erase(selection, hit);
            else
                selection.push_back(hit);
            editor_->select(selection);
            emit changed();
            update();
            return;
        }
        if (!selected)
            editor_->select({hit});
        dragTargets_ = editor_->selection();
        moveBounds_ = selectionBounds(*doc, dragTargets_, services);
        if (editor_->begin("Move"))
            drag_ = Drag::Move;
        emit changed();
        update();
        return;
    }
    if (!(e->modifiers() & Qt::ShiftModifier))
        editor_->select({});
    drag_ = Drag::Marquee;
    marquee_ = QRectF(v, v);
    emit changed();
    update();
}
void CanvasView::mouseMoveEvent(QMouseEvent* e) {
    const QPointF v = e->position();
    const QPointF a = toArtboard(v);
    const auto doc = editor_->snapshot();
    const auto services = services_->render();
    const auto mods = e->modifiers();
    if (altPending_ && (e->buttons() & Qt::LeftButton) && QLineF(v, press_).length() > 3) {
        const auto id = altPending_;
        altPending_ = {};
        auto selection = editor_->selection();
        if (std::find(selection.begin(), selection.end(), id) == selection.end())
            selection = {id};
        if (editor_->duplicate(selection, {0, 0})) {
            dragTargets_ = editor_->created();
            moveBounds_ = selectionBounds(*editor_->snapshot(), dragTargets_, services);
            duplicatedByDrag_ = true;
            if (editor_->begin("Move copy"))
                drag_ = Drag::Move;
            emit changed();
        }
    }
    switch (drag_) {
    case Drag::None:
        updateHover(v);
        if (tool_ == Tool::Pen && penActive()) {
            penCursor_ = a;
            update();
        }
        if (tool_ == Tool::Select) {
            const auto frame = selectionFrame();
            const auto h = handleAt(v, frame);
            if (h)
                updateHandleCursor(*h, frame);
            else if (auto g = guideAt(v))
                setCursor(g->first ? Qt::SplitHCursor : Qt::SplitVCursor);
            else
                updateCursor();
        }
        break;
    case Drag::Pan:
        pan_ += v - last_;
        refresh();
        emit viewChanged();
        break;
    case Drag::Marquee:
        marquee_ = QRectF(press_, v).normalized();
        update();
        break;
    case Drag::Move: {
        Point delta = kp(a - pressArtboard_);
        if (mods & Qt::ShiftModifier)
            (std::abs(delta.x) > std::abs(delta.y) ? delta.y : delta.x) = 0;
        auto settings = snap_;
        if (mods & Qt::ControlModifier)
            settings.enabled = false;
        snapHint_ = snapTranslation(*doc, moveBounds_, delta, dragTargets_, settings, tolerance(), services);
        if (mods & Qt::ShiftModifier) {
            if (delta.x == 0)
                snapHint_.delta.x = 0;
            if (delta.y == 0)
                snapHint_.delta.y = 0;
        }
        report(editor_->preview(translated(*doc, dragTargets_, snapHint_.delta)));
        refresh();
        break;
    }
    case Drag::Scale: {
        const auto& f = dragFrame_;
        if (!f.local.invertible())
            break;
        const bool center = mods & Qt::AltModifier;
        const Point anchor = center ? f.box.center() : localPoint(f.box, (handle_ + 4) % 8);
        const Point start = localPoint(f.box, handle_);
        const Point m = f.local.inverted().map(kp(a));
        const bool horizontal = handle_ != 1 && handle_ != 5, vertical = handle_ != 3 && handle_ != 7;
        double sx =
            horizontal && std::abs(start.x - anchor.x) > 1e-9 ? (m.x - anchor.x) / (start.x - anchor.x) : 1;
        double sy =
            vertical && std::abs(start.y - anchor.y) > 1e-9 ? (m.y - anchor.y) / (start.y - anchor.y) : 1;
        if ((mods & Qt::ShiftModifier) && cornerHandle(handle_)) {
            const double s = std::max(std::abs(sx), std::abs(sy));
            sx = sx < 0 ? -s : s;
            sy = sy < 0 ? -s : s;
        }
        const auto previousSnap = snapHint_;
        snapHint_ = {};
        // A rotated frame cannot satisfy an axis constraint without a different solver.
        if (std::abs(f.local.b) < 1e-9 && std::abs(f.local.c) < 1e-9) {
            auto settings = snap_;
            if (mods & Qt::ControlModifier)
                settings.enabled = false;
            const auto snapped = snapResize(*doc, f.local.map(start), f.local.map(anchor), kp(a), horizontal,
                                            vertical, (mods & Qt::ShiftModifier) && cornerHandle(handle_),
                                            dragTargets_, settings, tolerance(), services, &previousSnap);
            sx = snapped.sx;
            sy = snapped.sy;
            snapHint_ = snapped.hint;
        }
        const auto clampScale = [](double s) { return std::abs(s) < .01 ? (s < 0 ? -.01 : .01) : s; };
        sx = clampScale(sx);
        sy = clampScale(sy);
        std::vector<Object> replacement;
        if (dragTargets_.size() == 1 && !(f.local == Affine{}))
            replacement = {resizedLocal(*doc->find(dragTargets_[0]), sx, sy, anchor)};
        else if (dragTargets_.size() == 1)
            replacement = {resizedLocal(*doc->find(dragTargets_[0]), sx, sy,
                                        doc->worldMatrix(dragTargets_[0]).inverted().map(anchor))};
        else
            replacement = scaledAbout(*doc, dragTargets_, sx, sy, anchor);
        report(editor_->preview(replacement));
        refresh();
        break;
    }
    case Drag::Rotate: {
        const Point c = dragFrame_.local.map(dragFrame_.box.center());
        const double a0 = std::atan2(pressArtboard_.y() - c.y, pressArtboard_.x() - c.x);
        const double a1 = std::atan2(a.y() - c.y, a.x() - c.x);
        double degrees = (a1 - a0) * 180 / std::numbers::pi;
        if (mods & Qt::ShiftModifier)
            degrees = std::round(degrees / 15) * 15;
        report(editor_->preview(rotatedAbout(*doc, dragTargets_, degrees, c)));
        emit feedback(QStringLiteral("Rotate %1°").arg(degrees, 0, 'f', 1));
        refresh();
        break;
    }
    case Drag::Draw:
    case Drag::Guide:
        if (drag_ == Drag::Guide) {
            Point p = snapPoint(*doc, kp(a), {}, SnapSettings{true, true, true, true, false}, tolerance(),
                                services);
            guideValue_ = guide_.first ? p.x : p.y;
        }
        update();
        break;
    case Drag::Node:
        updateNodeDrag(v, mods & Qt::ShiftModifier);
        break;
    case Drag::Pen:
        if (penDragging_ && !pen_.nodes.empty() && QLineF(v, press_).length() > 2) {
            auto& n = pen_.nodes.back();
            n.out = kp(a);
            n.in = n.anchor * 2 - n.out;
            n.type = NodeType::Smooth;
            update();
        }
        break;
    case Drag::Handle: {
        Object o = *doc->find(dragTargets_[0]);
        auto& paint = o.style.fill;
        const Point u = toLocal01(dragFrame_, v);
        if (paint.kind == FillKind::Radial) {
            if (gradientPart_ == 0)
                paint.center = u;
            else
                paint.radius = std::clamp(length(u - paint.center), .02, 3.);
        } else {
            const Point d = u - Point{.5, .5};
            double degrees = std::atan2(d.y, d.x) * 180 / std::numbers::pi;
            if (mods & Qt::ShiftModifier)
                degrees = std::round(degrees / 15) * 15;
            paint.angle = degrees;
        }
        report(editor_->preview({o}));
        refresh();
        break;
    }
    }
    last_ = v;
}
void CanvasView::mouseReleaseEvent(QMouseEvent* e) {
    const QPointF v = e->position();
    const auto drag = drag_;
    drag_ = Drag::None;
    if (altPending_) {
        altPending_ = {};
        pickColor(v);
        return;
    }
    switch (drag) {
    case Drag::Pan:
        updateCursor();
        return;
    case Drag::Marquee: {
        const auto doc = editor_->snapshot();
        const QRectF r =
            QRectF(toArtboard(marquee_.topLeft()), toArtboard(marquee_.bottomRight())).normalized();
        auto selection =
            (e->modifiers() & Qt::ShiftModifier) ? editor_->selection() : std::vector<ObjectId>{};
        if (r.width() > 0 || r.height() > 0)
            for (const auto& ref : doc->objects) {
                if (ref->parent || !ref->visible || ref->locked)
                    continue;
                const Box b = objectBounds(*doc, *ref, services_->render());
                if (r.intersects(QRectF(b.x0, b.y0, b.width(), b.height())) &&
                    std::find(selection.begin(), selection.end(), ref->id) == selection.end())
                    selection.push_back(ref->id);
            }
        editor_->select(selection);
        marquee_ = {};
        emit changed();
        update();
        return;
    }
    case Drag::Move:
    case Drag::Scale:
    case Drag::Rotate:
    case Drag::Handle:
    case Drag::Node: {
        const auto r = editor_->apply();
        report(r);
        if (drag == Drag::Move && r.status == Status::Ok) {
            emit moved(dragTargets_, snapHint_.delta, duplicatedByDrag_);
        }
        duplicatedByDrag_ = false;
        snapHint_ = {};
        emit changed();
        refresh();
        updateCursor();
        return;
    }
    case Drag::Draw:
        commitDraw(toArtboard(press_), toArtboard(v), e->modifiers());
        return;
    case Drag::Guide: {
        auto artboard = editor_->snapshot()->artboard;
        auto& list = guide_.first ? artboard.guidesX : artboard.guidesY;
        const double extent = guide_.first ? artboard.width : artboard.height;
        if (guide_.second < list.size()) {
            if (guideValue_ < -extent * .05 || guideValue_ > extent * 1.05)
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(guide_.second));
            else
                list[guide_.second] = guideValue_;
            report(editor_->setArtboard(artboard));
        }
        emit changed();
        update();
        return;
    }
    case Drag::Pen:
        if (!pen_.nodes.empty() && pen_.nodes.back().type == NodeType::Smooth &&
            length(pen_.nodes.back().out - pen_.nodes.back().anchor) < 1 / zoom_) {
            auto& n = pen_.nodes.back();
            n.in = n.out = n.anchor;
            n.type = NodeType::Corner;
        }
        penDragging_ = false;
        drag_ = Drag::Pen;
        update();
        return;
    default:
        return;
    }
}
void CanvasView::mouseDoubleClickEvent(QMouseEvent* e) {
    const auto doc = editor_->snapshot();
    const QPointF a = toArtboard(e->position());
    if (tool_ == Tool::Pen && penActive()) {
        finishPen(false);
        return;
    }
    if (tool_ == Tool::Direct) {
        if (auto handle = nodeHandleAt(e->position()); handle && handle->second == 0) {
            node_ = handle->first;
            report(toggleNodeType());
            emit changed();
            refresh();
        }
        return;
    }
    if (tool_ != Tool::Select)
        return;
    const auto hit = hitTest(*doc, kp(a), tolerance(), services_->render());
    if (!hit)
        return;
    const auto* o = doc->find(hit);
    if (o->as<TextData>()) {
        editor_->select({hit});
        emit changed();
        editTextInline(hit);
    } else if (editablePath(*o)) {
        editor_->select({hit});
        setTool(Tool::Direct);
        emit changed();
        emit feedback("Editing nodes · Alt-click a segment to add a node · Del removes the selected node");
    } else {
        // Enter the group: select the leaf directly.
        editor_->select({hit});
        emit changed();
    }
    update();
}
namespace {
// Floating text box for typing straight onto the canvas: Enter commits, Shift+Enter adds a line, Esc cancels.
class InlineTextEdit final : public QPlainTextEdit {
  public:
    std::function<void()> commit, cancel;
    using QPlainTextEdit::QPlainTextEdit;

  protected:
    void keyPressEvent(QKeyEvent* e) override {
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
            !(e->modifiers() & Qt::ShiftModifier)) {
            commit();
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            cancel();
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
    }
    void focusOutEvent(QFocusEvent* e) override {
        QPlainTextEdit::focusOutEvent(e);
        if (commit)
            commit();
    }
};
} // namespace
void CanvasView::editTextInline(ObjectId id, bool selectAll) {
    finishTextEdit(true);
    const auto doc = editor_->snapshot();
    const auto* o = doc->find(id);
    if (!o || !o->as<TextData>() || doc->effectiveLocked(id) || !editor_->begin("Edit text"))
        return;
    textObject_ = *o;
    auto* edit = new InlineTextEdit(this);
    textEdit_ = edit;
    edit->setObjectName("inlineText");
    edit->setPlainText(QString::fromStdString(o->as<TextData>()->text));
    edit->setStyleSheet("QPlainTextEdit{background:rgba(20,23,29,235);color:#f1f4f8;border:1px solid #69b7d3;"
                        "border-radius:3px;padding:2px;font-size:14px;}");
    edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const Box b = objectBounds(*doc, *o, services_->render());
    const QRectF r(toView(QPointF(b.x0, b.y1)), toView(QPointF(b.x1, b.y1)));
    const int lines = std::max(1, static_cast<int>(edit->toPlainText().count('\n')) + 1);
    const int w = std::max(180, static_cast<int>(r.width()));
    const int x = std::clamp(static_cast<int>(r.center().x() - w / 2.), 4, std::max(4, width() - w - 4));
    const int y = std::clamp(static_cast<int>(r.bottom() + 8), 4, std::max(4, height() - 40));
    edit->setGeometry(x, y, w, 12 + 20 * std::min(lines, 4));
    edit->commit = [this] { QTimer::singleShot(0, this, [this] { finishTextEdit(true); }); };
    edit->cancel = [this] { QTimer::singleShot(0, this, [this] { finishTextEdit(false); }); };
    connect(edit, &QPlainTextEdit::textChanged, this, [this] {
        if (!textEdit_)
            return;
        Object copy = textObject_;
        copy.as<TextData>()->text = textEdit_->toPlainText().left(4096).toStdString();
        editor_->preview({copy});
        refresh();
    });
    edit->show();
    edit->setFocus();
    if (selectAll)
        edit->selectAll();
    else
        edit->moveCursor(QTextCursor::End);
    emit feedback("Type the text · Enter to finish · Shift+Enter for a new line · Esc to cancel");
}
void CanvasView::finishTextEdit(bool keep) {
    if (!textEdit_)
        return;
    auto* edit = static_cast<InlineTextEdit*>(textEdit_);
    textEdit_ = nullptr;
    edit->commit = nullptr;
    edit->cancel = nullptr;
    const auto text = edit->toPlainText();
    edit->hide();
    edit->deleteLater();
    if (keep && !text.trimmed().isEmpty())
        report(editor_->apply());
    else
        editor_->cancel();
    setFocus();
    emit changed();
    refresh();
}
void CanvasView::contextMenuEvent(QContextMenuEvent* e) {
    if (tool_ == Tool::Select || tool_ == Tool::Direct) {
        const auto doc = editor_->snapshot();
        auto hit = hitTest(*doc, kp(toArtboard(e->pos())), tolerance(), services_->render());
        if (hit && tool_ == Tool::Select)
            hit = selectableAncestor(*doc, hit);
        const auto& sel = editor_->selection();
        if (hit && std::find(sel.begin(), sel.end(), hit) == sel.end()) {
            editor_->select({hit});
            emit changed();
        }
    }
    emit contextMenuRequested(e->globalPos());
}
void CanvasView::wheelEvent(QWheelEvent* e) {
    const double steps = e->angleDelta().y() / 120.;
    if (steps != 0)
        zoomAt(zoom_ * std::pow(1.15, steps), e->position());
    e->accept();
}
void CanvasView::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        space_ = true;
        updateCursor();
        return;
    }
    if (e->key() == Qt::Key_Alt && !e->isAutoRepeat() && tool_ == Tool::Select && drag_ == Drag::None) {
        altPick_ = true;
        updateCursor();
        return;
    }
    const auto doc = editor_->snapshot();
    switch (e->key()) {
    case Qt::Key_Escape:
        if (drag_ != Drag::None || editor_->busy() || penActive())
            cancelGesture();
        else if (node_)
            node_.reset();
        else
            editor_->select({});
        emit changed();
        update();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (penActive()) {
            finishPen(false);
            return;
        }
        break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (tool_ == Tool::Direct && node_)
            report(deleteNode());
        else if (penActive() && !pen_.nodes.empty()) {
            pen_.nodes.pop_back();
            update();
            return;
        } else
            report(editor_->remove(editor_->selection()));
        emit changed();
        refresh();
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (editor_->selection().empty() || editor_->busy())
            break;
        const double step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        const Point d{e->key() == Qt::Key_Left    ? -step
                      : e->key() == Qt::Key_Right ? step
                                                  : 0,
                      e->key() == Qt::Key_Up     ? -step
                      : e->key() == Qt::Key_Down ? step
                                                 : 0};
        report(editor_->update("Nudge", translated(*doc, editor_->selection(), d)));
        emit changed();
        refresh();
        return;
    }
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}
void CanvasView::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        space_ = false;
        updateCursor();
        return;
    }
    if (e->key() == Qt::Key_Alt && !e->isAutoRepeat()) {
        altPick_ = false;
        updateCursor();
        return;
    }
    QWidget::keyReleaseEvent(e);
}
void CanvasView::focusOutEvent(QFocusEvent* e) {
    space_ = altPick_ = false;
    // Focus loss deterministically cancels an open drag; pen paths stay so the user can continue.
    if (drag_ != Drag::None && drag_ != Drag::Pen)
        cancelGesture();
    updateCursor();
    QWidget::focusOutEvent(e);
}
void CanvasView::leaveEvent(QEvent*) {
    if (hover_) {
        hover_ = {};
        update();
    }
}
QPainterPath safeZonePath(SafeZone zone, double w, double h, QPainterPath* inner) {
    QPainterPath path;
    const double side = std::min(w, h);
    const QPointF c(w / 2, h / 2);
    switch (zone) {
    case SafeZone::None:
        break;
    case SafeZone::Circle:
        path.addEllipse(c, side / 2, side / 2);
        break;
    case SafeZone::Squircle: {
        // Superellipse |x|^5 + |y|^5 = r^5: a close match for the continuous-corner app icon mask.
        const double r = side / 2;
        constexpr int Steps = 160;
        for (int i = 0; i < Steps; ++i) {
            const double a = 2 * std::numbers::pi * i / Steps;
            const double ca = std::cos(a), sa = std::sin(a);
            const QPointF pt(c.x() + r * std::copysign(std::pow(std::abs(ca), 2.0 / 5), ca),
                             c.y() + r * std::copysign(std::pow(std::abs(sa), 2.0 / 5), sa));
            if (i == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        path.closeSubpath();
        break;
    }
    case SafeZone::AndroidAdaptive: {
        // Adaptive icons: 108 dp layers, 72 dp shown inside the launcher mask, 66 dp always safe.
        const double visible = side * 72 / 108;
        path.addRect(QRectF(c.x() - visible / 2, c.y() - visible / 2, visible, visible));
        if (inner) {
            *inner = QPainterPath();
            inner->addEllipse(c, side * 33 / 108, side * 33 / 108);
        }
        break;
    }
    }
    return path;
}
void CanvasView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(27, 30, 37));
    const auto doc = editor_->preview();
    const auto& board = doc->artboard;
    const QTransform t = viewTransform();
    const QRectF paper = t.mapRect(QRectF(0, 0, board.width, board.height));
    // Artboard: checkerboard for transparent, solid colour otherwise.
    if (board.transparent) {
        p.fillRect(paper, QColor(52, 58, 69));
        p.save();
        p.setClipRect(paper);
        const double cell = 10;
        const double top = paper.top() + std::floor(std::max(0., -paper.top()) / cell) * cell;
        const double left = paper.left() + std::floor(std::max(0., -paper.left()) / cell) * cell;
        for (double y = top; y < std::min(paper.bottom(), static_cast<double>(height())); y += cell)
            for (double x = left; x < std::min(paper.right(), static_cast<double>(width())); x += cell)
                if ((static_cast<int>((x - paper.left()) / cell) +
                     static_cast<int>((y - paper.top()) / cell)) %
                        2 ==
                    0)
                    p.fillRect(QRectF(x, y, cell, cell), QColor(70, 77, 90));
        p.restore();
    } else
        p.fillRect(paper, QColor(board.background.r, board.background.g, board.background.b));
    p.drawImage(QPointF(0, 0), cache_);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(90, 100, 116), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(paper.adjusted(-.5, -.5, .5, .5));
    // Safe-zone mask: dims what the platform crops away.
    if (safeZone_ != SafeZone::None) {
        QPainterPath inner;
        const QPainterPath zone = t.map(safeZonePath(safeZone_, board.width, board.height, &inner));
        QPainterPath outside;
        outside.addRect(paper);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.fillPath(outside.subtracted(zone), QColor(15, 17, 22, 150));
        p.setPen(QPen(QColor(255, 196, 80, 220), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPath(zone);
        if (!inner.isEmpty()) {
            p.setPen(QPen(QColor(255, 196, 80, 160), 1, Qt::DotLine));
            p.drawPath(t.map(inner));
        }
        p.restore();
    }
    // Guides.
    p.setPen(QPen(QColor(80, 200, 230, 170), 1, Qt::DashLine));
    for (std::size_t i = 0; i < board.guidesX.size(); ++i) {
        const double x =
            (drag_ == Drag::Guide && guide_.first && guide_.second == i) ? guideValue_ : board.guidesX[i];
        const double vx = toView({x, 0}).x();
        p.drawLine(QPointF(vx, 0), QPointF(vx, height()));
    }
    for (std::size_t i = 0; i < board.guidesY.size(); ++i) {
        const double y =
            (drag_ == Drag::Guide && !guide_.first && guide_.second == i) ? guideValue_ : board.guidesY[i];
        const double vy = toView({0, y}).y();
        p.drawLine(QPointF(0, vy), QPointF(width(), vy));
    }
    const auto services = services_->render();
    // Hover outline.
    if (hover_ && drag_ == Drag::None) {
        if (const auto* o = doc->find(hover_)) {
            const auto area = objectArea(*doc, *o, doc->parentMatrix(o->id), .25 / zoom_, services);
            p.setPen(QPen(Accent, 1.5));
            p.drawPath(painterPath(area.polygons, t));
        }
    }
    // Selection.
    const auto& selection = editor_->selection();
    if (!selection.empty() && tool_ != Tool::Direct) {
        if (selection.size() > 1) {
            p.setPen(QPen(QColor(105, 183, 211, 120), 1));
            for (const auto id : selection)
                if (const auto* o = doc->find(id)) {
                    const Box b = objectBounds(*doc, *o, services);
                    p.drawRect(t.mapRect(QRectF(b.x0, b.y0, b.width(), b.height())));
                }
        }
        const auto frame = selectionFrame();
        if (frame.valid) {
            const auto pts = handlePoints(frame);
            QPolygonF outline{pts[0], pts[2], pts[4], pts[6]};
            p.setPen(QPen(Accent, 1));
            p.drawPolygon(outline);
            if (tool_ == Tool::Select) {
                const auto rot = rotationHandle(frame);
                p.drawLine(pts[1], rot);
                p.setBrush(QColor(37, 42, 51));
                p.drawEllipse(rot, HandleRadius, HandleRadius);
                p.setBrush(Qt::white);
                for (const auto& h : pts)
                    p.drawRect(QRectF(h.x() - HandleRadius + 1, h.y() - HandleRadius + 1,
                                      2 * HandleRadius - 2, 2 * HandleRadius - 2));
                p.setBrush(Qt::NoBrush);
            }
        }
    }
    // Gradient handles.
    if (const auto g = gradientHandles();
        g.valid && drag_ != Drag::Move && drag_ != Drag::Scale && drag_ != Drag::Rotate) {
        const QPointF c = fromLocal01(g.frame, g.center), e = fromLocal01(g.frame, g.end);
        p.setPen(QPen(QColor(255, 255, 255, 200), 1, Qt::DashLine));
        p.drawLine(c, e);
        const auto diamond = [&](QPointF at) {
            p.setPen(QPen(QColor(30, 34, 42), 1));
            p.setBrush(QColor(255, 214, 90));
            p.drawPolygon(
                QPolygonF{at + QPointF(0, -5), at + QPointF(5, 0), at + QPointF(0, 5), at + QPointF(-5, 0)});
        };
        if (g.radial)
            diamond(c);
        diamond(e);
        p.setBrush(Qt::NoBrush);
    }
    // Node editing overlay.
    if (tool_ == Tool::Direct) {
        if (const auto* o = directTarget()) {
            const Affine m = viewAffine() * doc->worldMatrix(o->id);
            const auto* path = editablePath(*o);
            QPainterPath outline;
            for (const auto& s : path->subpaths) {
                if (s.nodes.empty())
                    continue;
                outline.moveTo(qp(m.map(s.nodes[0].anchor)));
                const std::size_t n = s.nodes.size();
                for (std::size_t i = 1; i <= (s.closed ? n : n - 1); ++i) {
                    const auto& a = s.nodes[i - 1];
                    const auto& b = s.nodes[i % n];
                    outline.cubicTo(qp(m.map(a.out)), qp(m.map(b.in)), qp(m.map(b.anchor)));
                }
            }
            p.setPen(QPen(Accent, 1));
            p.drawPath(outline);
            for (std::size_t s = 0; s < path->subpaths.size(); ++s)
                for (std::size_t i = 0; i < path->subpaths[s].nodes.size(); ++i) {
                    const auto& n = path->subpaths[s].nodes[i];
                    const bool chosen = node_ && *node_ == NodeRef{s, i};
                    const QPointF c = qp(m.map(n.anchor));
                    if (chosen) {
                        p.setPen(QPen(QColor(200, 210, 225), 1));
                        for (const auto h : {n.in, n.out})
                            if (h != n.anchor) {
                                p.drawLine(c, qp(m.map(h)));
                                p.setBrush(QColor(37, 42, 51));
                                p.drawEllipse(qp(m.map(h)), 3.5, 3.5);
                            }
                    }
                    p.setPen(QPen(Accent, 1));
                    p.setBrush(chosen ? Accent : QColor(Qt::white));
                    if (n.type == NodeType::Smooth)
                        p.drawEllipse(c, 4, 4);
                    else
                        p.drawRect(QRectF(c.x() - 3.5, c.y() - 3.5, 7, 7));
                }
            p.setBrush(Qt::NoBrush);
        }
    }
    // Snap guides share the same coordinate system for moves and resizes.
    if (drag_ == Drag::Move || drag_ == Drag::Scale) {
        p.setPen(QPen(Snap, 1));
        if (!std::isnan(snapHint_.guide.x)) {
            const double x = toView({snapHint_.guide.x, 0}).x();
            p.drawLine(QPointF(x, 0), QPointF(x, height()));
        }
        if (!std::isnan(snapHint_.guide.y)) {
            const double y = toView({0, snapHint_.guide.y}).y();
            p.drawLine(QPointF(0, y), QPointF(width(), y));
        }
    }
    // Marquee.
    if (drag_ == Drag::Marquee && !marquee_.isEmpty()) {
        p.setPen(QPen(Accent, 1, Qt::DashLine));
        p.setBrush(QColor(105, 183, 211, 30));
        p.drawRect(marquee_);
        p.setBrush(Qt::NoBrush);
    }
    // Draw preview.
    if (drag_ == Drag::Draw) {
        const Box b =
            drawBox(toArtboard(press_), toArtboard(last_), QGuiApplication::queryKeyboardModifiers());
        const ShapeGeometry g = tool_ == Tool::Ellipse
                                    ? ShapeGeometry{EllipseParams{}}
                                    : shapePresets()[static_cast<std::size_t>(shapePreset_)].geometry;
        if (!b.empty()) {
            const auto path = transformed(shapePath(g, b.width(), b.height()),
                                          viewAffine() * Affine::translation(b.center().x, b.center().y));
            p.setPen(QPen(Accent, 1, Qt::DashLine));
            p.drawPath(painterPath(flatten(path, .5), QTransform()));
            p.setPen(QColor(220, 228, 238));
            p.drawText(toView(QPointF(b.x1, b.y1)) + QPointF(8, 16),
                       QStringLiteral("%1 × %2").arg(b.width(), 0, 'f', 0).arg(b.height(), 0, 'f', 0));
        }
    }
    // Pen overlay.
    if (tool_ == Tool::Pen && !pen_.nodes.empty()) {
        const Affine m = viewAffine();
        QPainterPath path;
        path.moveTo(qp(m.map(pen_.nodes[0].anchor)));
        for (std::size_t i = 1; i < pen_.nodes.size(); ++i)
            path.cubicTo(qp(m.map(pen_.nodes[i - 1].out)), qp(m.map(pen_.nodes[i].in)),
                         qp(m.map(pen_.nodes[i].anchor)));
        if (!penDragging_)
            path.lineTo(toView(penCursor_));
        p.setPen(QPen(Accent, 1.5));
        p.drawPath(path);
        const auto& last = pen_.nodes.back();
        if (last.type == NodeType::Smooth) {
            p.setPen(QPen(QColor(200, 210, 225), 1));
            p.drawLine(qp(m.map(last.in)), qp(m.map(last.out)));
        }
        p.setPen(QPen(Accent, 1));
        p.setBrush(Qt::white);
        for (const auto& n : pen_.nodes) {
            const QPointF c = qp(m.map(n.anchor));
            p.drawRect(QRectF(c.x() - 3.5, c.y() - 3.5, 7, 7));
        }
    }
}
} // namespace kantan::app

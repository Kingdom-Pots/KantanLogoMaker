#pragma once
#include "app/Services.h"
#include "editing/Editor.h"
#include "editing/Transforms.h"
#include <QImage>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QTransform>
#include <QWidget>
#include <optional>

namespace kantan::app {
enum class Tool { Select, Direct, Pen, Shape, Ellipse, Text, Image, Eyedropper, Hand, Zoom };
// View-only masks that show what a platform crops away. They are never exported.
enum class SafeZone { None, Circle, Squircle, AndroidAdaptive };
// The visible region for `zone` on a w × h artboard, in artboard units, centred on the largest square.
// AndroidAdaptive returns the 72/108 visible square; `inner` receives the 66/108 keyline circle.
QPainterPath safeZonePath(SafeZone zone, double w, double h, QPainterPath* inner = nullptr);
struct NodeRef {
    std::size_t subpath{}, node{};
    bool operator==(const NodeRef&) const = default;
};
class CanvasView final : public QWidget {
    Q_OBJECT
  public:
    CanvasView(std::shared_ptr<Editor> editor, std::shared_ptr<AppServices> services,
               QWidget* parent = nullptr);
    Tool tool() const {
        return tool_;
    }
    void setTool(Tool tool);
    int shapePreset() const {
        return shapePreset_;
    }
    void setShapePreset(int index);
    void setSafeZone(SafeZone zone) {
        safeZone_ = zone;
        update();
    }
    SafeZone safeZone() const {
        return safeZone_;
    }
    void setSnapSettings(SnapSettings settings) {
        snap_ = settings;
    }
    SnapSettings snapSettings() const {
        return snap_;
    }
    void setDrawStyle(Style shapes, Style text) {
        shapeStyle_ = std::move(shapes);
        textStyle_ = std::move(text);
    }
    void fit();
    void fitSelection();
    void zoomAt(double zoom, QPointF viewAnchor);
    void zoomBy(double factor);
    double zoom() const {
        return zoom_;
    }
    void refresh();
    QPointF toArtboard(QPointF view) const;
    QPointF toView(QPointF artboard) const;
    bool penActive() const {
        return !pen_.nodes.empty();
    }
    void finishPen(bool close);
    void cancelGesture();
    std::optional<NodeRef> selectedNode() const {
        return node_;
    }
    // Path menu operations on the directly selected path node.
    Result deleteNode();
    Result toggleNodeType();
    QColor sampleColor(QPointF view) const;
    QImage artwork() const {
        return cache_;
    }
    // Type straight onto the canvas; the live text updates as you type, one undo step on Enter.
    void editTextInline(ObjectId id, bool selectAll = false);
    void finishTextEdit(bool keep);
    bool editingText() const {
        return textEdit_ != nullptr;
    }
  signals:
    void changed();
    void feedback(QString message);
    void colorPicked(QColor color);
    void viewChanged();
    void requestTextEdit();
    void requestPlaceImage(QPointF artboard);
    void contextMenuRequested(QPoint global);
    // A move finished; `afterDuplicate` when the moved objects were just copied by Alt-drag.
    void moved(std::vector<ObjectId> ids, Point delta, bool afterDuplicate);

  protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void leaveEvent(QEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

  private:
    enum class Drag { None, Pan, Marquee, Move, Scale, Rotate, Draw, Node, Handle, Guide, Pen };
    struct Frame {
        Affine local; // frame-local -> artboard
        Box box;      // in frame-local units
        bool valid{};
    };
    QTransform viewTransform() const;
    Affine viewAffine() const;
    void render();
    void report(Result result);
    Frame selectionFrame() const;
    std::optional<int> handleAt(QPointF view, const Frame& frame) const;
    std::array<QPointF, 8> handlePoints(const Frame& frame) const;
    QPointF rotationHandle(const Frame& frame) const;
    std::optional<std::pair<bool, std::size_t>> guideAt(QPointF view) const;
    struct GradientHandles {
        bool valid{}, radial{};
        Frame frame;
        Point center, end; // normalized object-local (0..1) positions
    };
    GradientHandles gradientHandles() const;
    QPointF fromLocal01(const Frame& frame, Point u) const;
    Point toLocal01(const Frame& frame, QPointF view) const;
    const Object* directTarget() const;
    std::optional<std::pair<NodeRef, int>> nodeHandleAt(QPointF view) const; // part: 0 anchor, 1 in, 2 out
    void beginNodeDrag(NodeRef node, int part, QPointF view);
    void updateNodeDrag(QPointF view, bool constrain);
    Result insertNodeAt(QPointF view);
    void updateHover(QPointF view);
    void commitDraw(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers);
    Box drawBox(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers) const;
    void pickColor(QPointF view);
    void updateCursor();
    void updateHandleCursor(int handle, const Frame& frame);
    double tolerance() const;
    std::shared_ptr<Editor> editor_;
    std::shared_ptr<AppServices> services_;
    Tool tool_{Tool::Select};
    int shapePreset_{0};
    SnapSettings snap_;
    SafeZone safeZone_{SafeZone::None};
    Style shapeStyle_, textStyle_;
    QImage cache_;
    Snapshot renderedDocument_;
    QTransform renderedTransform_;
    double zoom_{1};
    QPointF pan_;
    Drag drag_{Drag::None};
    QPointF press_, last_, pressArtboard_;
    Qt::KeyboardModifiers modifiers_{};
    bool space_{}, altPick_{};
    ObjectId hover_;
    // Gesture state.
    std::vector<ObjectId> dragTargets_;
    Frame dragFrame_;
    int handle_{-1};
    int gradientPart_{};
    Box moveBounds_;
    SnapResult snapHint_{};
    QRectF marquee_;
    std::pair<bool, std::size_t> guide_{};
    double guideValue_{};
    std::optional<NodeRef> node_;
    int nodePart_{};
    Object nodeObject_; // object being node-edited (state at drag start)
    ObjectId altPending_;
    bool duplicatedByDrag_{};
    QPlainTextEdit* textEdit_{};
    Object textObject_;
    SubPath pen_;
    bool penDragging_{};
    QPointF penCursor_;
};
} // namespace kantan::app

#include "app/ObjectPanel.h"
#include "app/CompactControls.h"
#include "io/Images.h"
#include <QDrag>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace kantan::app {
namespace {
constexpr auto LayerMime = "application/x-kantan-layer-order";
constexpr int ContainerRole = Qt::UserRole + 1;
constexpr int LockedRole = Qt::UserRole + 2;
ObjectId idOf(const QTreeWidgetItem* item) {
    return item ? ObjectId{item->data(0, Qt::UserRole).toULongLong()} : ObjectId{};
}
QString kindLabel(const Object& o) {
    if (const auto* b = o.as<BooleanData>()) {
        static const char* names[] = {"Union", "Subtract", "Intersect", "Exclude"};
        return names[static_cast<int>(b->op)];
    }
    return kindName(o.kind());
}
} // namespace
ObjectTree::ObjectTree(QWidget* parent) : QTreeWidget(parent) {
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(false);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setAutoScroll(true);
    setAutoScrollMargin(28);
    setAccessibleName("Layers");
    scrollTimer_.setInterval(60);
    connect(&scrollTimer_, &QTimer::timeout, this, [this] {
        verticalScrollBar()->setValue(verticalScrollBar()->value() + scrollDirection_ * 12);
        const auto [target, position] = dropAt(lastDropPoint_);
        dropPosition_ = position;
        auto* item = itemAt(lastDropPoint_);
        dropRect_ = validTarget(target, position) && item ? visualItemRect(item) : QRect{};
        viewport()->update();
    });
}
void ObjectTree::startDrag(Qt::DropActions) {
    if (!dragEnabled())
        return;
    dragIds_.clear();
    for (auto* item : selectedItems()) {
        if (item->data(0, LockedRole).toBool()) {
            emit feedback("Unlock selected layers before reordering them");
            return;
        }
        dragIds_.push_back(idOf(item));
    }
    if (dragIds_.empty())
        return;
    auto* drag = new QDrag(this);
    const QPointer<QDrag> dragGuard(drag);
    const QPointer<ObjectTree> self(this);
    auto* mime = new QMimeData;
    mime->setData(LayerMime, "layers");
    drag->setMimeData(mime);
    const auto ratio = devicePixelRatioF();
    QPixmap preview(qRound(180 * ratio), qRound(30 * ratio));
    preview.setDevicePixelRatio(ratio);
    preview.fill(QColor(44, 50, 62));
    {
        QPainter painter(&preview);
        painter.setPen(Qt::white);
        painter.drawText(QRect(0, 0, 180, 30), Qt::AlignCenter,
                         dragIds_.size() == 1 ? QString("Move layer")
                                              : QStringLiteral("Move %1 layers").arg(dragIds_.size()));
    }
    drag->setPixmap(preview);
    drag->setHotSpot(QPoint(16, 15));
    // Own the drag completely: QAbstractItemView::startDrag removes source rows after MoveAction.
    // Only the document editor may change layer order; the tree is rebuilt from that document.
    drag->exec(Qt::MoveAction, Qt::MoveAction);
    if (dragGuard)
        dragGuard->deleteLater();
    if (!self)
        return;
    scrollTimer_.stop();
    dragIds_.clear();
    dropRect_ = {};
    viewport()->update();
}
std::pair<ObjectId, int> ObjectTree::dropAt(QPoint point) const {
    auto* item = itemAt(point);
    if (!item)
        return {{}, Root};
    const auto rect = visualItemRect(item);
    const double fraction = static_cast<double>(point.y() - rect.top()) / std::max(1, rect.height());
    // Only plain groups accept nesting. Boolean/clip containers change compositing when children enter.
    if (item->data(0, ContainerRole).toBool() && columnAt(point.x()) == 0 && fraction > .35 && fraction < .65)
        return {idOf(item), Inside};
    return {idOf(item), fraction < .5 ? Above : Below};
}
bool ObjectTree::validTarget(ObjectId target, int position) const {
    if (dragIds_.empty())
        return false;
    QTreeWidgetItem* item = nullptr;
    for (QTreeWidgetItemIterator it(const_cast<ObjectTree*>(this)); *it; ++it)
        if (idOf(*it) == target) {
            item = *it;
            break;
        }
    for (auto* ancestor = item; ancestor; ancestor = ancestor->parent())
        if (std::find(dragIds_.begin(), dragIds_.end(), idOf(ancestor)) != dragIds_.end())
            return false;
    if (position == Inside && item && item->data(0, LockedRole).toBool())
        return false;
    if (position != Inside && item && item->parent() && item->parent()->data(0, LockedRole).toBool())
        return false;
    return true;
}
void ObjectTree::dragEnterEvent(QDragEnterEvent* event) {
    if (event->source() == this && event->mimeData()->hasFormat(LayerMime) && acceptDrops()) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else
        event->ignore();
}
void ObjectTree::dragMoveEvent(QDragMoveEvent* event) {
    const auto point = event->position().toPoint();
    lastDropPoint_ = point;
    scrollDirection_ = point.y() < 28 ? -1 : point.y() > viewport()->height() - 28 ? 1 : 0;
    if (scrollDirection_)
        scrollTimer_.start();
    else
        scrollTimer_.stop();
    const auto [target, position] = dropAt(point);
    if (event->source() != this || !acceptDrops() || !event->mimeData()->hasFormat(LayerMime) ||
        !validTarget(target, position)) {
        scrollTimer_.stop();
        dropRect_ = {};
        event->ignore();
        viewport()->update();
        return;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    dropPosition_ = position;
    auto* item = itemAt(point);
    dropRect_ = item ? visualItemRect(item)
                     : QRect(4, std::min(point.y(), viewport()->height() - 8), viewport()->width() - 8, 3);
    viewport()->update();
    emit feedback(position == Inside ? "Move into group"
                  : position == Root ? "Move to bottom of Layers"
                                     : "Reorder layers");
}
void ObjectTree::dragLeaveEvent(QDragLeaveEvent* event) {
    scrollTimer_.stop();
    dropRect_ = {};
    QTreeWidget::dragLeaveEvent(event);
    viewport()->update();
}
void ObjectTree::dropEvent(QDropEvent* event) {
    scrollTimer_.stop();
    const auto [target, position] = dropAt(event->position().toPoint());
    dropRect_ = {};
    if (event->source() != this || !acceptDrops() || !event->mimeData()->hasFormat(LayerMime) ||
        !validTarget(target, position)) {
        event->ignore();
        return;
    }
    const auto ids = dragIds_; // stable IDs survive a synchronous tree rebuild in the receiver
    event->setDropAction(Qt::MoveAction);
    event->accept();
    emit dropped(ids, target, position);
    viewport()->update();
}
void ObjectTree::paintEvent(QPaintEvent* event) {
    QTreeWidget::paintEvent(event);
    if (dropRect_.isEmpty())
        return;
    QPainter painter(viewport());
    painter.setPen(QPen(QColor(105, 183, 211), 2));
    if (dropPosition_ == Inside) {
        painter.setBrush(QColor(105, 183, 211, 35));
        painter.drawRect(dropRect_.adjusted(1, 1, -2, -2));
    } else {
        const int y = dropPosition_ == Below ? dropRect_.bottom() : dropRect_.top();
        painter.drawLine(4, y, viewport()->width() - 4, y);
    }
}
void ObjectTree::mousePressEvent(QMouseEvent* event) {
    const int column = columnAt(event->position().toPoint().x());
    auto* item = itemAt(event->position().toPoint());
    iconPress_ = event->button() == Qt::LeftButton && item && (column == 1 || column == 2);
    if (iconPress_) {
        emit toggleRequested(idOf(item), column);
        event->accept();
        return;
    }
    QTreeWidget::mousePressEvent(event);
}
void ObjectTree::mouseReleaseEvent(QMouseEvent* event) {
    if (iconPress_) {
        iconPress_ = false;
        event->accept();
        return;
    }
    QTreeWidget::mouseReleaseEvent(event);
}
void ObjectTree::mouseDoubleClickEvent(QMouseEvent* event) {
    if (columnAt(event->position().toPoint().x()) != 0) {
        event->accept();
        return;
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}
ObjectPanel::ObjectPanel(std::shared_ptr<Editor> editor, std::shared_ptr<AppServices> services,
                         QWidget* parent)
    : QWidget(parent), editor_(std::move(editor)), services_(std::move(services)) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);
    count_ = new QLabel("Layers", this);
    count_->setAccessibleName("Layer count");
    layout->addWidget(count_);
    search_ = new QLineEdit(this);
    search_->setObjectName("objectSearch");
    search_->setPlaceholderText("Find objects by name or type");
    search_->setAccessibleName("Find objects");
    search_->setClearButtonEnabled(true);
    layout->addWidget(search_);
    tree_ = new ObjectTree(this);
    tree_->setObjectName("objectTree");
    tree_->setColumnCount(3);
    tree_->setHeaderHidden(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    tree_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    tree_->setColumnWidth(1, 26);
    tree_->setColumnWidth(2, 26);
    tree_->setIndentation(14);
    tree_->setIconSize(QSize(28, 28));
    tree_->setAlternatingRowColors(true);
    tree_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QWidget::customContextMenuRequested, this, [this](QPoint at) {
        if (auto* item = tree_->itemAt(at); item && !item->isSelected()) {
            tree_->clearSelection();
            item->setSelected(true);
        }
        emit contextMenuRequested(tree_->viewport()->mapToGlobal(at));
    });
    layout->addWidget(tree_);
    connect(search_, &QLineEdit::textChanged, this, [this] { filter(); });
    auto row = new QHBoxLayout;
    row->setSpacing(4);
    const auto button = [&](QString icon, QString tip, const std::function<void()>& action) {
        auto b = new QToolButton(this);
        iconButton(b, icon, tip);
        row->addWidget(b);
        connect(b, &QToolButton::clicked, this, action);
        return b;
    };
    auto actions = new QToolButton(this);
    actions->setObjectName("layerActions");
    actions->setText("Actions");
    actions->setToolButtonStyle(Qt::ToolButtonTextOnly);
    actions->setMinimumHeight(28);
    actions->setAccessibleName("Layer actions");
    actions->setPopupMode(QToolButton::InstantPopup);
    auto menu = new QMenu(actions);
    auto group = menu->addAction("Group", this, [this] {
        report(editor_->group(editor_->selection(), ObjectKind::Group));
        emit documentChanged();
    });
    auto duplicate = menu->addAction("Duplicate", this, [this] {
        report(editor_->duplicate(editor_->selection(), {16, 16}));
        emit documentChanged();
    });
    auto remove = menu->addAction("Delete", this, [this] {
        report(editor_->remove(editor_->selection()));
        emit documentChanged();
    });
    connect(menu, &QMenu::aboutToShow, this, [this, group, duplicate, remove] {
        const auto doc = editor_->snapshot();
        const bool editable = !editor_->busy() && !editor_->selection().empty() &&
                              std::none_of(editor_->selection().begin(), editor_->selection().end(),
                                           [&](ObjectId id) { return doc->effectiveLocked(id); });
        group->setEnabled(editable && editor_->selection().size() >= 2);
        duplicate->setEnabled(editable);
        remove->setEnabled(editable);
    });
    actions->setMenu(menu);
    row->addWidget(actions);
    raise_ = button("chevron_down", "Move layers up", [this] {
        report(editor_->arrange(selected(), Arrange::BringForward));
        emit documentChanged();
    });
    lower_ = button("chevron_down", "Move layers down", [this] {
        report(editor_->arrange(selected(), Arrange::SendBackward));
        emit documentChanged();
    });
    raise_->setArrowType(Qt::UpArrow);
    lower_->setArrowType(Qt::DownArrow);
    row->addStretch();
    layout->addLayout(row);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (refreshing_)
            return;
        editor_->select(selected());
        emit selectionChanged();
    });
    connect(tree_, &ObjectTree::toggleRequested, this, [this](ObjectId id, int column) {
        if (column == 0)
            return;
        const auto* o = editor_->snapshot()->find(id);
        if (!o)
            return;
        Object changed = *o;
        if (column == 1)
            changed.visible = !changed.visible;
        else
            changed.locked = !changed.locked;
        report(editor_->update(column == 1 ? (changed.visible ? "Show" : "Hide")
                                           : (changed.locked ? "Lock" : "Unlock"),
                               {changed}));
        emit documentChanged();
    });
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (refreshing_ || column != 0)
            return;
        const auto* o = editor_->snapshot()->find(idOf(item));
        const auto name = item->text(0).trimmed().left(256).toStdString();
        if (!o || name.empty() || name == o->name) {
            shownDocument_.reset();
            refresh();
            return;
        }
        Object renamed = *o;
        renamed.name = name;
        report(editor_->update("Rename", {renamed}));
        emit documentChanged();
    });
    connect(tree_, &ObjectTree::dropped, this,
            [this](std::vector<ObjectId> ids, ObjectId target, int position) {
                report(moveLayers(ids, target, position));
            });
    connect(tree_, &ObjectTree::feedback, this, &ObjectPanel::feedback);
}
Result ObjectPanel::moveLayers(const std::vector<ObjectId>& ids, ObjectId target, int position) {
    if (!search_->text().trimmed().isEmpty())
        return {Status::InvalidTarget, "Clear layer search before reordering"};
    if (ids.empty())
        return {Status::NoChange, {}};
    const auto doc = editor_->snapshot();
    for (const auto id : ids)
        if (!doc->find(id))
            return {Status::Stale, "A dragged layer no longer exists"};
    if (std::find(ids.begin(), ids.end(), target) != ids.end())
        return {Status::NoChange, {}};
    ObjectId parent, before;
    if (position == ObjectTree::Root) {
        // The blank area is below the visible stack: put the selection at the back.
        for (const auto id : doc->children({}))
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
                before = id;
                break;
            }
    } else {
        const auto* object = doc->find(target);
        if (!object)
            return {Status::Stale, "The drop target no longer exists"};
        if (position == ObjectTree::Inside) {
            if (object->kind() != ObjectKind::Group)
                return {Status::InvalidTarget,
                        "Only plain groups accept layer drops; drop between rows to reorder"};
            parent = target;
        } else if (position == ObjectTree::Below) {
            parent = object->parent;
            before = target;
        } else if (position == ObjectTree::Above) {
            parent = object->parent;
            const auto siblings = doc->children(parent);
            auto next = std::find(siblings.begin(), siblings.end(), target);
            for (++next; next != siblings.end(); ++next)
                if (std::find(ids.begin(), ids.end(), *next) == ids.end()) {
                    before = *next;
                    break;
                }
        } else
            return {Status::InvalidTarget, "Invalid layer drop position"};
    }
    const auto result = editor_->move(ids, parent, before);
    if (!result)
        return result;
    editor_->select(ids);
    shownDocument_.reset();
    emit documentChanged();
    refresh();
    if (position == ObjectTree::Inside)
        for (QTreeWidgetItemIterator it(tree_); *it; ++it)
            if (idOf(*it) == target)
                (*it)->setExpanded(true);
    return result;
}
void ObjectPanel::report(Result r) {
    if (!r && !r.message.empty())
        emit feedback(QString::fromStdString(r.message));
}
std::vector<ObjectId> ObjectPanel::selected() const {
    std::vector<ObjectId> out;
    for (auto* item : tree_->selectedItems())
        out.push_back(idOf(item));
    return out;
}
QIcon ObjectPanel::thumbnail(const DocumentData& doc, const Object& o) {
    // Key on the immutable refs of the whole subtree: any edit inside a group refreshes its icon.
    QString key;
    const auto start = doc.indexOf(o.id);
    for (auto i = start; i < doc.subtreeEnd(start); ++i)
        key += QString::number(reinterpret_cast<quintptr>(doc.objects[i].get()), 16) + ':';
    if (const auto found = thumbnails_.find(o.id.value); found != thumbnails_.end() && found->first == key)
        return found->second;
    QIcon icon;
    try {
        const auto services = services_->render();
        const Box b = objectBounds(doc, o, services);
        const int size = 56;
        RenderRequest request;
        request.width = request.height = size;
        request.toPixels = fitTransform(b, size, size, 6);
        request.only = {o.id};
        auto image = io::toQImage(kantan::render(doc, request, services, services_->budget));
        image.setDevicePixelRatio(2);
        // Pick a backdrop that contrasts with the artwork (dark text needs a light tile).
        double luminance = 0, weight = 0;
        for (int y = 0; y < image.height(); y += 2)
            for (int x = 0; x < image.width(); x += 2) {
                const QColor c = image.pixelColor(x, y);
                luminance += (0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF()) * c.alphaF();
                weight += c.alphaF();
            }
        const bool dark = weight > 0 && luminance / weight < .35;
        QPixmap pix(size, size);
        pix.setDevicePixelRatio(2);
        pix.fill(dark ? QColor(196, 202, 212) : QColor(44, 50, 62));
        QPainter p(&pix);
        p.drawImage(0, 0, image);
        p.end();
        icon = QIcon(pix);
    } catch (const std::exception&) {
    }
    thumbnails_[o.id.value] = {key, icon};
    return icon;
}
void ObjectPanel::refresh() {
    refreshing_ = true;
    const auto doc = editor_->snapshot();
    const bool selectionChanged = shownSelection_ != editor_->selection();
    std::size_t rowCount = 0;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it)
        ++rowCount;
    count_->setText(QStringLiteral("Layers: %1 · Selected: %2")
                        .arg(doc->objects.size())
                        .arg(editor_->selection().size()));
    const bool movable = !editor_->selection().empty() && search_->text().trimmed().isEmpty() &&
                         std::none_of(editor_->selection().begin(), editor_->selection().end(),
                                      [&](ObjectId id) { return doc->effectiveLocked(id); });
    raise_->setEnabled(movable);
    lower_->setEnabled(movable);
    if (shownDocument_ == doc && rowCount == doc->objects.size()) {
        const QSignalBlocker blocker(tree_);
        for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
            const auto id = idOf(*it);
            const bool selected = std::find(editor_->selection().begin(), editor_->selection().end(), id) !=
                                  editor_->selection().end();
            (*it)->setSelected(selected);
            if (selected && selectionChanged)
                tree_->scrollToItem(*it);
        }
        shownSelection_ = editor_->selection();
        refreshing_ = false;
        return;
    }
    const int scroll = tree_->verticalScrollBar()->value();
    // Remember expansion state by id.
    QSet<quint64> collapsed;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it)
        if ((*it)->childCount() && !(*it)->isExpanded())
            collapsed.insert(idOf(*it).value);
    tree_->clear();
    std::map<std::uint64_t, QTreeWidgetItem*> items;
    QSet<quint64> live;
    // Topmost first: walk the depth-first order backwards per sibling list.
    const auto build = [&](auto&& self, ObjectId parent, QTreeWidgetItem* parentItem) -> void {
        auto kids = doc->children(parent);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
            const auto* o = doc->find(*it);
            auto* item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(tree_);
            item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(o->id.value));
            item->setData(0, ContainerRole, o->kind() == ObjectKind::Group);
            item->setData(0, LockedRole, doc->effectiveLocked(o->id));
            item->setText(0, o->name.empty() ? kindLabel(*o) : QString::fromStdString(o->name));
            item->setToolTip(0, kindLabel(*o));
            item->setIcon(0, thumbnail(*doc, *o));
            item->setIcon(1, uiIcon(o->visible ? "eye" : "eye_off"));
            item->setIcon(2, uiIcon(o->locked ? "lock_closed" : "lock_open"));
            item->setToolTip(1, o->visible ? "Hide" : "Show");
            item->setToolTip(2, o->locked ? "Unlock" : "Lock");
            item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled |
                           (o->container() ? Qt::ItemIsDropEnabled : Qt::ItemFlags{}));
            if (!doc->effectiveVisible(o->id))
                item->setForeground(0, QColor(120, 128, 140));
            items[o->id.value] = item;
            live.insert(o->id.value);
            self(self, o->id, item);
            item->setExpanded(!collapsed.contains(o->id.value));
        }
    };
    build(build, {}, nullptr);
    for (auto it = thumbnails_.begin(); it != thumbnails_.end();)
        it = live.contains(it.key()) ? std::next(it) : thumbnails_.erase(it);
    for (const auto id : editor_->selection())
        if (const auto found = items.find(id.value); found != items.end())
            found->second->setSelected(true);
    if (selectionChanged && !editor_->selection().empty())
        if (const auto found = items.find(editor_->selection().back().value); found != items.end())
            tree_->scrollToItem(found->second);
    if (!selectionChanged)
        tree_->verticalScrollBar()->setValue(scroll);
    shownDocument_ = doc;
    shownSelection_ = editor_->selection();
    filter();
    refreshing_ = false;
}
void ObjectPanel::filter() {
    const QSignalBlocker blocker(tree_);
    const auto query = search_->text().trimmed();
    if (!query.isEmpty() && !filtering_) {
        collapsedBeforeFilter_.clear();
        for (QTreeWidgetItemIterator it(tree_); *it; ++it)
            if ((*it)->childCount() && !(*it)->isExpanded())
                collapsedBeforeFilter_.insert(idOf(*it).value);
    } else if (query.isEmpty() && filtering_) {
        for (QTreeWidgetItemIterator it(tree_); *it; ++it)
            if ((*it)->childCount())
                (*it)->setExpanded(!collapsedBeforeFilter_.contains(idOf(*it).value));
    }
    filtering_ = !query.isEmpty();
    const auto visit = [&](auto&& self, QTreeWidgetItem* item, bool parentMatches) -> bool {
        const bool matches = query.isEmpty() || parentMatches ||
                             item->text(0).contains(query, Qt::CaseInsensitive) ||
                             item->toolTip(0).contains(query, Qt::CaseInsensitive);
        bool childMatches = false;
        for (int index = 0; index < item->childCount(); ++index)
            childMatches = self(self, item->child(index), matches) || childMatches;
        item->setHidden(!matches && !childMatches);
        if (!query.isEmpty() && childMatches)
            item->setExpanded(true);
        return matches || childMatches;
    };
    for (int index = 0; index < tree_->topLevelItemCount(); ++index)
        visit(visit, tree_->topLevelItem(index), false);
    // Reordering a filtered tree would hide the true insertion position.
    tree_->setDragEnabled(query.isEmpty());
    tree_->setAcceptDrops(query.isEmpty());
    if (!query.isEmpty()) {
        raise_->setEnabled(false);
        lower_->setEnabled(false);
    } else {
        const auto doc = editor_->snapshot();
        const bool enabled = !editor_->selection().empty() &&
                             std::none_of(editor_->selection().begin(), editor_->selection().end(),
                                          [&](ObjectId id) { return doc->effectiveLocked(id); });
        raise_->setEnabled(enabled);
        lower_->setEnabled(enabled);
    }
}
} // namespace kantan::app

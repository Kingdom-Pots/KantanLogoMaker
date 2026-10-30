#pragma once
#include "app/Services.h"
#include "editing/Editor.h"
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QWidget>

namespace kantan::app {
class ObjectTree final : public QTreeWidget {
    Q_OBJECT
  public:
    explicit ObjectTree(QWidget* parent = nullptr);
    enum DropPosition { Above, Below, Inside, Root };
  signals:
    void dropped(std::vector<ObjectId> ids, ObjectId target, int position);
    void toggleRequested(ObjectId id, int column);
    void feedback(QString message);

  protected:
    void startDrag(Qt::DropActions) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

  private:
    std::pair<ObjectId, int> dropAt(QPoint position) const;
    bool validTarget(ObjectId target, int position) const;
    std::vector<ObjectId> dragIds_;
    QRect dropRect_;
    int dropPosition_{Root};
    bool iconPress_{};
    QTimer scrollTimer_;
    int scrollDirection_{};
    QPoint lastDropPoint_;
};
// Objects tree: topmost first, groups/booleans/clips expandable, eye / lock columns, thumbnails.
class ObjectPanel final : public QWidget {
    Q_OBJECT
  public:
    ObjectPanel(std::shared_ptr<Editor> editor, std::shared_ptr<AppServices> services,
                QWidget* parent = nullptr);
    void refresh();
    Result moveLayers(const std::vector<ObjectId>& ids, ObjectId target, int position);
    ObjectTree* tree() const {
        return tree_;
    }
  signals:
    void documentChanged();
    void selectionChanged();
    void feedback(QString);
    void contextMenuRequested(QPoint global);

  private:
    void filter();
    QIcon thumbnail(const DocumentData& doc, const Object& object);
    void report(Result result);
    std::vector<ObjectId> selected() const;
    std::shared_ptr<Editor> editor_;
    std::shared_ptr<AppServices> services_;
    ObjectTree* tree_{};
    QLineEdit* search_{};
    QLabel* count_{};
    QToolButton *raise_{}, *lower_{};
    Snapshot shownDocument_;
    std::vector<ObjectId> shownSelection_;
    QSet<quint64> collapsedBeforeFilter_;
    bool filtering_{};
    bool refreshing_{};
    QHash<quint64, std::pair<QString, QIcon>> thumbnails_; // id -> (subtree key, icon)
};
} // namespace kantan::app

#include "app/MockupDialog.h"
#include "app/CanvasView.h"
#include "io/ExportJob.h"
#include "io/Images.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QVBoxLayout>

namespace kantan::app {
namespace {
// Neutral stand-ins for other apps' icons, so the logo is judged among neighbours.
const QColor Others[] = {{86, 150, 220}, {230, 120, 80}, {90, 185, 120}, {170, 110, 210},
                         {235, 190, 70}, {70, 175, 185}, {210, 90, 120}, {120, 130, 150}};

class Scenes final : public QWidget {
  public:
    explicit Scenes(MockupDialog* owner) : QWidget(owner), owner_(owner) {
        setMinimumSize(760, 720);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const bool dark = owner_->dark();
        const QColor page = dark ? QColor(30, 33, 40) : QColor(236, 238, 242);
        const QColor text = dark ? QColor(225, 230, 238) : QColor(30, 34, 42);
        p.fillRect(rect(), page);
        p.setRenderHint(QPainter::Antialiasing);
        const auto title = [&](int y, const QString& s) {
            p.setPen(text);
            QFont f = font();
            f.setBold(true);
            p.setFont(f);
            p.drawText(16, y, s);
            p.setFont(font());
        };
        // Taskbar (Windows-style, 24 px icons on a 48 px bar).
        title(24, "Taskbar · 24 px");
        const QRect bar(16, 36, 728, 48);
        p.fillRect(bar, dark ? QColor(32, 32, 32) : QColor(243, 243, 243));
        int x = bar.left() + 300;
        for (int i = 0; i < 6; ++i, x += 44) {
            const QRect slot(x, bar.top() + 4, 40, 40);
            if (i == 2) {
                p.fillRect(slot, dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 14));
                p.drawImage(slot.left() + 8, slot.top() + 8, owner_->icon(24));
                p.fillRect(QRect(slot.center().x() - 8, slot.bottom() - 2, 16, 3), QColor(96, 205, 255));
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(Others[i]);
                p.drawRoundedRect(QRectF(slot.left() + 8, slot.top() + 8, 24, 24), 5, 5);
            }
        }
        // Desktop (48 px icon with a label on wallpaper).
        title(112, "Desktop · 48 px");
        const QRect wall(16, 124, 360, 170);
        QLinearGradient g(wall.topLeft(), wall.bottomRight());
        g.setColorAt(0, dark ? QColor(20, 40, 70) : QColor(120, 170, 220));
        g.setColorAt(1, dark ? QColor(60, 30, 80) : QColor(200, 160, 220));
        p.fillRect(wall, g);
        for (int i = 0; i < 3; ++i) {
            const int ix = wall.left() + 24 + i * 100, iy = wall.top() + 24;
            if (i == 0)
                p.drawImage(ix + 16, iy, owner_->icon(48));
            else {
                p.setPen(Qt::NoPen);
                p.setBrush(Others[i + 3]);
                p.drawRoundedRect(QRectF(ix + 16, iy, 48, 48), 8, 8);
            }
            p.setPen(Qt::white);
            p.drawText(QRect(ix - 4, iy + 52, 88, 32), Qt::AlignHCenter | Qt::TextWordWrap,
                       i == 0 ? name_() : (i == 1 ? "Documents" : "Recycle Bin"));
        }
        // Browser tab (16 px favicon).
        p.setPen(text);
        p.drawText(392, 112, "Browser tab · 16 px");
        const QRect strip(392, 124, 352, 40);
        p.fillRect(strip, dark ? QColor(41, 42, 45) : QColor(222, 225, 230));
        const QRect tab(strip.left() + 8, strip.top() + 6, 200, 34);
        QPainterPath tp;
        tp.addRoundedRect(tab, 8, 8);
        p.fillPath(tp, dark ? QColor(53, 54, 58) : Qt::white);
        p.drawImage(tab.left() + 10, tab.top() + 9, owner_->icon(16));
        p.setPen(text);
        p.drawText(tab.adjusted(34, 0, -8, 0), Qt::AlignVCenter, name_());
        // Store / social avatar (circle crop, 64 px).
        p.drawText(392, 196, "Avatar · 64 px circle");
        {
            const QRect a(392, 208, 64, 64);
            QPainterPath circle;
            circle.addEllipse(a);
            p.save();
            p.setClipPath(circle);
            p.fillRect(a, dark ? QColor(60, 64, 72) : Qt::white);
            p.drawImage(a.topLeft(), owner_->icon(64));
            p.restore();
            p.setPen(text);
            p.drawText(468, 236, name_());
            p.setPen(dark ? QColor(150, 158, 170) : QColor(110, 116, 126));
            p.drawText(468, 256, "@" + name_().toLower().remove(' '));
        }
        // Phone home screen (60 px icons, squircle mask).
        title(322, "Phone home screen · 60 px, rounded mask");
        const QRect phone(16, 334, 300, 370);
        QPainterPath body;
        body.addRoundedRect(phone, 36, 36);
        QLinearGradient pg(phone.topLeft(), phone.bottomLeft());
        pg.setColorAt(0, dark ? QColor(25, 30, 50) : QColor(150, 200, 235));
        pg.setColorAt(1, dark ? QColor(10, 12, 20) : QColor(240, 200, 170));
        p.fillPath(body, pg);
        p.setPen(QPen(dark ? QColor(80, 84, 92) : QColor(40, 40, 44), 6));
        p.setBrush(Qt::NoBrush);
        p.drawPath(body);
        for (int i = 0; i < 12; ++i) {
            const int col = i % 4, row = i / 4;
            const QRectF cell(phone.left() + 18 + col * 68, phone.top() + 40 + row * 92, 60, 60);
            const QPainterPath mask = safeZonePath(SafeZone::Squircle, 60, 60).translated(cell.topLeft());
            if (i == 5) {
                p.save();
                p.setClipPath(mask);
                p.fillRect(cell, Qt::white);
                p.drawImage(cell.topLeft(), owner_->icon(60));
                p.restore();
            } else
                p.fillPath(mask, Others[i % 8]);
            p.setPen(Qt::white);
            QFont small = font();
            small.setPointSizeF(small.pointSizeF() * 0.8);
            p.setFont(small);
            p.drawText(QRectF(cell.left() - 6, cell.bottom() + 2, 72, 18), Qt::AlignHCenter,
                       i == 5 ? p.fontMetrics().elidedText(name_(), Qt::ElideRight, 70) : QString());
            p.setFont(font());
        }
        // Big tile (store listing, 128 px) on both themes' card.
        p.setPen(text);
        p.drawText(340, 322, "Store listing · 128 px");
        const QRect card(340, 334, 404, 160);
        QPainterPath cp;
        cp.addRoundedRect(card, 12, 12);
        p.fillPath(cp, dark ? QColor(44, 48, 58) : Qt::white);
        p.drawImage(card.left() + 16, card.top() + 16, owner_->icon(128));
        QFont big = font();
        big.setPointSizeF(big.pointSizeF() * 1.4);
        big.setBold(true);
        p.setFont(big);
        p.setPen(text);
        p.drawText(card.left() + 160, card.top() + 48, name_());
        p.setFont(font());
        p.setPen(dark ? QColor(150, 158, 170) : QColor(110, 116, 126));
        p.drawText(card.left() + 160, card.top() + 74, "Games · Free");
        p.fillRect(QRect(card.left() + 160, card.top() + 100, 90, 30), QColor(56, 120, 220));
        p.setPen(Qt::white);
        p.drawText(QRect(card.left() + 160, card.top() + 100, 90, 30), Qt::AlignCenter, "Install");
    }

  private:
    QString name_() const {
        return owner_->windowTitle().section(" — ", 1);
    }
    MockupDialog* owner_;
};
} // namespace

MockupDialog::MockupDialog(Snapshot document, std::shared_ptr<AppServices> services, QString name,
                           QWidget* parent)
    : QDialog(parent), document_(std::move(document)), services_(std::move(services)),
      name_(std::move(name)) {
    setObjectName("mockupDialog");
    setWindowTitle("Preview on… — " + name_);
    resize(800, 760);
    auto root = new QVBoxLayout(this);
    auto top = new QHBoxLayout;
    theme_ = new QComboBox;
    theme_->setObjectName("mockupTheme");
    theme_->addItems({"Dark theme", "Light theme"});
    top->addWidget(new QLabel("Shown at true pixel sizes, using the export padding and background."));
    top->addStretch();
    top->addWidget(theme_);
    root->addLayout(top);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    canvas_ = new Scenes(this);
    scroll->setWidget(canvas_);
    root->addWidget(scroll, 1);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(theme_, &QComboBox::currentIndexChanged, canvas_, qOverload<>(&QWidget::update));
}
bool MockupDialog::dark() const {
    return theme_->currentIndex() == 0;
}
const QImage& MockupDialog::icon(int size) {
    auto it = icons_.find(size);
    if (it != icons_.end())
        return it->second;
    auto settings = document_->exportSettings;
    settings.selectionOnly = false;
    QImage image;
    try {
        image = io::toQImage(
            io::renderExport(*document_, {size, size}, settings, {}, services_->render(), services_->budget));
    } catch (const std::exception&) {
        image = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }
    return icons_.emplace(size, std::move(image)).first->second;
}
} // namespace kantan::app

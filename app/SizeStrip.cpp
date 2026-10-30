#include "app/SizeStrip.h"
#include "app/CompactControls.h"
#include "io/ExportJob.h"
#include "io/Images.h"
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

namespace kantan::app {
const std::vector<int>& SizeStrip::sizes() {
    // Icon sizes shown at true pixels; larger exports look like the canvas and are checked in the export
    // preview.
    static const std::vector<int> s{16, 24, 32, 48, 64};
    return s;
}
SizeStrip::SizeStrip(std::shared_ptr<Editor> editor, std::shared_ptr<AppServices> services, QWidget* parent)
    : QWidget(parent), editor_(std::move(editor)), services_(std::move(services)) {
    setObjectName("sizeStrip");
    setToolTip("Live export preview at true pixels · click to change the background");
    zoomButton_ = new QToolButton(this);
    zoomButton_->setObjectName("sizeZoom");
    zoomButton_->setText("1×");
    zoomButton_->setToolTip("Magnify the previews (each output pixel drawn as a block)");
    connect(zoomButton_, &QToolButton::clicked, this, [this] { setZoom(zoom_ % 3 + 1); });
    backgroundButton_ = new QToolButton(this);
    backgroundButton_->setIcon(backgroundIcon(0));
    backgroundButton_->setIconSize(QSize(20, 20));
    backgroundButton_->setAccessibleName("Preview background");
    zoomButton_->setAccessibleName("Preview magnification");
    backgroundButton_->setToolTip("Preview background: checkerboard, white, grey, dark");
    connect(backgroundButton_, &QToolButton::clicked, this, [this] {
        setBackground(static_cast<PreviewBackground>((static_cast<int>(background_) + 1) % 4));
    });
    timer_.setSingleShot(true);
    timer_.setInterval(60);
    connect(&timer_, &QTimer::timeout, this, &SizeStrip::renderAll);
    schedule();
}
void SizeStrip::schedule() {
    timer_.start();
}
QSize SizeStrip::sizeHint() const {
    int width = 20 + 60;
    for (int s : sizes())
        width += s * zoom_ + 14;
    return {width, sizes().back() * zoom_ + 34};
}
void SizeStrip::setZoom(int zoom) {
    zoom_ = std::clamp(zoom, 1, 3);
    zoomButton_->setText(QStringLiteral("%1×").arg(zoom_));
    updateGeometry();
    if (auto* p = parentWidget())
        p->setMaximumHeight(sizeHint().height() + 40);
    update();
}
void SizeStrip::resizeEvent(QResizeEvent*) {
    zoomButton_->setGeometry(width() - 74, 6, 32, 24);
    backgroundButton_->setGeometry(width() - 38, 6, 32, 24);
}
void SizeStrip::setBackground(PreviewBackground b) {
    background_ = b;
    backgroundButton_->setIcon(backgroundIcon(static_cast<int>(b)));
    const QStringList names{"Transparent checkerboard", "White", "Grey", "Dark"};
    backgroundButton_->setToolTip("Preview background: " + names[static_cast<int>(b)] + " · click to cycle");
    update();
}
void SizeStrip::renderAll() {
    images_.clear();
    const auto doc = editor_->preview();
    ExportSettings settings = doc->exportSettings;
    settings.selectionOnly = false;
    try {
        for (const int size : sizes())
            images_.push_back(io::toQImage(
                io::renderExport(*doc, {size, size}, settings, {}, services_->render(), services_->budget)));
    } catch (const std::exception&) {
        images_.clear();
    }
    update();
}
void SizeStrip::mousePressEvent(QMouseEvent*) {
    setBackground(static_cast<PreviewBackground>((static_cast<int>(background_) + 1) % 4));
}
void SizeStrip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(32, 36, 44));
    const int labelHeight = 16;
    const int base = 6 + sizes().back() * zoom_; // bottom-aligned tiles
    int x = 10;
    p.setFont(QFont(font().family(), 8));
    for (std::size_t i = 0; i < images_.size(); ++i) {
        const int shown = sizes()[i] * zoom_;
        const QRect target(x, base - shown, shown, shown);
        switch (background_) {
        case PreviewBackground::Checker: {
            const int cell = 4 * zoom_;
            for (int yy = 0; yy < shown; yy += cell)
                for (int xx = 0; xx < shown; xx += cell)
                    p.fillRect(QRect(target.x() + xx, target.y() + yy, std::min(cell, shown - xx),
                                     std::min(cell, shown - yy)),
                               ((xx + yy) / cell) % 2 ? QColor(70, 77, 90) : QColor(52, 58, 69));
            break;
        }
        case PreviewBackground::White:
            p.fillRect(target, Qt::white);
            break;
        case PreviewBackground::Grey:
            p.fillRect(target, QColor(128, 128, 128));
            break;
        case PreviewBackground::Dark:
            p.fillRect(target, QColor(16, 18, 22));
            break;
        }
        // True pixels: nearest-neighbour when magnified.
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawImage(target, images_[i]);
        p.setPen(QColor(150, 160, 175));
        p.drawText(QRect(x - 6, base + 2, shown + 12, labelHeight), Qt::AlignHCenter | Qt::AlignVCenter,
                   QString::number(sizes()[i]));
        x += shown + 14;
    }
}
} // namespace kantan::app

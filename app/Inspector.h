#pragma once
#include "app/ColorField.h"
#include "app/Services.h"
#include "editing/Editor.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <functional>

namespace kantan::app {
// Properties of the selection. Style edits apply to every selected object; parameter sections show for a
// single object of the matching kind. Spin boxes commit on Enter/focus-out, sliders as one scrub step.
class Inspector final : public QScrollArea {
    Q_OBJECT
  public:
    Inspector(std::shared_ptr<Editor> editor, std::shared_ptr<AppServices> services,
              QWidget* parent = nullptr);
    void refresh();
    void focusText();
    QPlainTextEdit* textEdit() const {
        return text_;
    }
  signals:
    void documentChanged();
    void feedback(QString);
    void alignCanvas(int alignment);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    using Getter = std::function<double(const Object&)>;
    using Setter = std::function<void(Object&, double)>;
    using Applies = std::function<bool(const Object&)>;
    struct Section {
        QWidget* box{};
        QToolButton* header{};
        std::function<bool(const Object&, std::size_t count)> visible;
    };
    QFormLayout* section(const QString& title, std::function<bool(const Object&, std::size_t)> visible,
                         bool open = false);
    QDoubleSpinBox* number(QFormLayout* form, const QString& label, double min, double max, int decimals,
                           Getter get, Setter set, const QString& undo, Applies applies = {},
                           const QString& suffix = {});
    void slider(QFormLayout* form, const QString& label, int min, int max, Getter get, Setter set,
                const QString& undo, Applies applies = {});
    void check(QFormLayout* form, const QString& label, std::function<bool(const Object&)> get,
               std::function<void(Object&, bool)> set, const QString& undo, Applies applies = {});
    void combo(QFormLayout* form, const QString& label, const QStringList& items,
               std::function<int(const Object&)> get, std::function<void(Object&, int)> set,
               const QString& undo, Applies applies = {});
    void color(QFormLayout* form, const QString& label, std::function<Color(const Object&)> get,
               std::function<void(Object&, Color)> set, const QString& undo, Applies applies = {});
    void edit(const QString& undo, const std::function<void(Object&)>& change, const Applies& applies = {});
    // Live (scrub/picker) edits: one gesture -> one undo step, finished by finishPreview().
    void previewEach(const QString& undo, const std::function<void(Object&)>& change, const Applies& applies);
    void finishPreview();
    std::vector<const Object*> targets(const Applies& applies) const;
    std::vector<Swatch> swatches() const;
    void buildTransform();
    void buildShape();
    void buildText();
    void buildImage();
    void buildFill();
    void buildOutlines();
    void buildEffects();
    void buildAppearance();
    void refreshFonts();
    const Object* primary() const;
    std::shared_ptr<Editor> editor_;
    std::shared_ptr<AppServices> services_;
    QWidget* content_{};
    QVBoxLayout* layout_{};
    std::vector<Section> sections_;
    std::vector<std::function<void(const Object&)>> loaders_;
    std::vector<std::pair<QWidget*, std::function<bool(const Object&)>>> rows_; // per-row visibility
    bool loading_{};
    bool wheelGesture_{};
    QTimer wheelTimer_;
    QPlainTextEdit* text_{};
    QComboBox* font_{};
    QDoubleSpinBox *x_{}, *y_{}, *w_{}, *h_{}, *angle_{};
    QWidget* empty_{};
    QWidget* lockedNotice_{};
    QString fontsSignature_;
};
} // namespace kantan::app

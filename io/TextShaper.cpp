#include "io/TextShaper.h"
#include <QFont>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QPainterPath>
#include <QRawFont>
#include <QTextLayout>
#include <numbers>

namespace kantan::io {
namespace {
// Shape at a fixed reference size, then scale: geometry is independent of hinting and integer pixel sizes.
constexpr double Reference = 256;
constexpr std::size_t CacheLimit = 256;
struct Glyph {
    Path path;
    double center{}; // horizontal centre on the baseline, for arc placement
};
void appendPainterPath(Path& out, const QPainterPath& p, const Affine& m) {
    SubPath current;
    const auto flush = [&] {
        if (current.nodes.size() > 1) {
            // Drop a duplicated closing node; the subpath closes implicitly.
            if (length(current.nodes.back().anchor - current.nodes.front().anchor) < 1e-9) {
                current.nodes.front().in = current.nodes.back().in;
                current.nodes.pop_back();
            }
            out.subpaths.push_back(std::move(current));
        }
        current = {};
    };
    for (int i = 0; i < p.elementCount(); ++i) {
        const auto e = p.elementAt(i);
        const Point pt = m.map({e.x, e.y});
        switch (e.type) {
        case QPainterPath::MoveToElement:
            flush();
            current.nodes.push_back(Node::corner(pt));
            break;
        case QPainterPath::LineToElement:
            current.nodes.push_back(Node::corner(pt));
            break;
        case QPainterPath::CurveToElement: {
            if (i + 2 >= p.elementCount() || current.nodes.empty())
                return;
            const auto c2 = p.elementAt(i + 1), end = p.elementAt(i + 2);
            current.nodes.back().out = pt;
            const Point endPoint = m.map({end.x, end.y});
            current.nodes.push_back({endPoint, m.map({c2.x, c2.y}), endPoint, NodeType::Corner});
            i += 2;
            break;
        }
        default:
            break;
        }
    }
    flush();
}
} // namespace
Path QtTextShaper::shape(const TextData& text, const DocumentData& doc) const {
    QString family = fonts_->resolve(text.font, doc);
    if (family.isEmpty())
        family = FontLibrary::fallbackFamily();
    std::lock_guard lock(mutex_);
    const Key key{text, family};
    for (auto it = cache_.begin(); it != cache_.end(); ++it)
        if (it->first == key) {
            cache_.splice(cache_.begin(), cache_, it);
            return it->second;
        }
    QFont font(family);
    font.setPixelSize(static_cast<int>(Reference));
    font.setWeight(static_cast<QFont::Weight>(std::clamp(text.weight, 1, 1000)));
    font.setItalic(text.italic);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setKerning(true);
    font.setStyleStrategy(QFont::PreferOutline);
    if (text.allCaps)
        font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, text.tracking / 1000. * Reference);
    const double scale = text.size / Reference;
    const QStringList lines = QString::fromStdString(text.text).split('\n');
    const QFontMetricsF metrics(font);
    const double lineStep = text.lineHeight * Reference;
    struct Line {
        std::vector<Glyph> glyphs;
        double width{};
    };
    std::vector<Line> laidOut;
    for (const auto& line : lines) {
        Line out;
        QTextLayout layout(line, font);
        layout.beginLayout();
        auto textLine = layout.createLine();
        if (textLine.isValid())
            textLine.setLineWidth(1e7);
        layout.endLayout();
        if (textLine.isValid()) {
            out.width = textLine.naturalTextWidth();
            for (const auto& run : layout.glyphRuns()) {
                const auto raw = run.rawFont();
                const auto indexes = run.glyphIndexes();
                const auto positions = run.positions();
                const auto advances = raw.advancesForGlyphIndexes(indexes);
                for (qsizetype i = 0; i < indexes.size(); ++i) {
                    Glyph g;
                    const auto pos = positions[i];
                    // Glyph coordinates are relative to the layout's top-left; move the baseline to y = 0.
                    const Affine place = Affine::translation(pos.x(), pos.y() - textLine.ascent());
                    appendPainterPath(g.path, raw.pathForGlyph(indexes[i]), place);
                    g.center = pos.x() + (i < advances.size() ? advances[i].x() / 2 : 0);
                    if (!g.path.subpaths.empty())
                        out.glyphs.push_back(std::move(g));
                }
            }
        }
        laidOut.push_back(std::move(out));
    }
    // Vertical layout: baselines lineStep apart, block centred on the origin using ascent/descent.
    const double blockHeight =
        static_cast<double>(laidOut.size() - 1) * lineStep + metrics.ascent() + metrics.descent();
    const double firstBaseline = -blockHeight / 2 + metrics.ascent();
    Path result;
    result.rule = FillRule::NonZero;
    const double arc = std::clamp(text.arc, -1., 1.);
    for (std::size_t li = 0; li < laidOut.size(); ++li) {
        const auto& line = laidOut[li];
        const double x0 = text.align == TextAlign::Left     ? 0
                          : text.align == TextAlign::Center ? -line.width / 2
                                                            : -line.width;
        const double baseline = firstBaseline + static_cast<double>(li) * lineStep;
        for (const auto& g : line.glyphs) {
            Affine m;
            if (std::abs(arc) < 1e-4 || line.width <= 0)
                m = Affine::translation(x0, baseline);
            else {
                // Glyphs ride on a circle; full bend (|arc| = 1) spans a half circle.
                const double total = std::abs(arc) * std::numbers::pi;
                const double radius = line.width / total;
                const double phi = (g.center + x0) / radius; // signed angle from the top/bottom
                const double sign = arc > 0 ? 1 : -1;
                const Point center{0, baseline + sign * radius};
                const Point onCircle{center.x + radius * std::sin(phi),
                                     center.y - sign * radius * std::cos(phi)};
                m = Affine::translation(onCircle.x, onCircle.y) *
                    Affine::rotation(sign * phi * 180 / std::numbers::pi) * Affine::translation(-g.center, 0);
            }
            for (const auto& s : transformed(g.path, Affine::scaling(scale, scale) * m).subpaths)
                result.subpaths.push_back(s);
        }
    }
    // Arc bend pushes glyphs off-centre vertically; recentre the block's bounds on the origin.
    if (std::abs(arc) >= 1e-4) {
        const Box b = bounds(result);
        if (!b.empty())
            result = transformed(result, Affine::translation(0, -b.center().y));
    }
    cache_.emplace_front(key, result);
    if (cache_.size() > CacheLimit)
        cache_.pop_back();
    return result;
}
} // namespace kantan::io

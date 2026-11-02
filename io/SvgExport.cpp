#include "io/SvgExport.h"
#include "editing/Transforms.h"
#include "geometry/Geometry.h"
#include "shapes/Shapes.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <cmath>
#include <numbers>
#include <set>

namespace kantan::io {
namespace {
QString xmlEscape(const QString& str) {
    QString out;
    out.reserve(str.size());
    for (const QChar c : str) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '"')
            out += "&quot;";
        else if (c == '\'')
            out += "&apos;";
        else
            out += c;
    }
    return out;
}

QString colorHex(Color c) {
    return QStringLiteral("#%1%2%3")
        .arg(c.r, 2, 16, QChar('0'))
        .arg(c.g, 2, 16, QChar('0'))
        .arg(c.b, 2, 16, QChar('0'));
}

double colorAlpha(Color c) {
    return c.a / 255.0;
}

QString num(double v, int maxDecimals = 2) {
    QString s = QString::number(v, 'f', maxDecimals);
    if (s.contains('.')) {
        while (s.endsWith('0'))
            s.chop(1);
        if (s.endsWith('.'))
            s.chop(1);
    }
    return s;
}

QString matrixStr(const Affine& m) {
    return QStringLiteral("matrix(%1 %2 %3 %4 %5 %6)")
        .arg(m.a, 0, 'f', 5)
        .arg(m.b, 0, 'f', 5)
        .arg(m.c, 0, 'f', 5)
        .arg(m.d, 0, 'f', 5)
        .arg(m.e, 0, 'f', 3)
        .arg(m.f, 0, 'f', 3);
}

Affine repeatMatrix(const Object& o, int copy) {
    if (o.repeat.count <= 1 || copy == 0)
        return {};
    const Point pivot = o.transform.position + o.repeat.pivot;
    return Affine::translation(pivot.x, pivot.y) * Affine::rotation(360. * copy / o.repeat.count) *
           Affine::translation(-pivot.x, -pivot.y);
}

QString pathToSvgD(const Path& path) {
    QString d;
    for (const auto& sub : path.subpaths) {
        if (sub.nodes.empty())
            continue;
        const auto& n0 = sub.nodes.front();
        d += QStringLiteral("M %1 %2 ").arg(num(n0.anchor.x)).arg(num(n0.anchor.y));
        for (std::size_t i = 1; i < sub.nodes.size(); ++i) {
            const auto& prev = sub.nodes[i - 1];
            const auto& cur = sub.nodes[i];
            if (prev.out == prev.anchor && cur.in == cur.anchor) {
                d += QStringLiteral("L %1 %2 ").arg(num(cur.anchor.x)).arg(num(cur.anchor.y));
            } else {
                d += QStringLiteral("C %1 %2 %3 %4 %5 %6 ")
                         .arg(num(prev.out.x))
                         .arg(num(prev.out.y))
                         .arg(num(cur.in.x))
                         .arg(num(cur.in.y))
                         .arg(num(cur.anchor.x))
                         .arg(num(cur.anchor.y));
            }
        }
        if (sub.closed && sub.nodes.size() > 1) {
            const auto& prev = sub.nodes.back();
            const auto& cur = sub.nodes.front();
            if (prev.out != prev.anchor || cur.in != cur.anchor) {
                d += QStringLiteral("C %1 %2 %3 %4 %5 %6 ")
                         .arg(num(prev.out.x))
                         .arg(num(prev.out.y))
                         .arg(num(cur.in.x))
                         .arg(num(cur.in.y))
                         .arg(num(cur.anchor.x))
                         .arg(num(cur.anchor.y));
            }
            d += "Z ";
        }
    }
    return d.trimmed();
}

struct SvgContext {
    int nextId{1};
    QString defs;
    std::set<ObjectId> included;
};

QString definePaint(SvgContext& ctx, const Paint& paint) {
    if (paint.kind == FillKind::None)
        return "none";
    if (paint.kind == FillKind::Solid) {
        if (colorAlpha(paint.color) < 1.0)
            return colorHex(paint.color); // opacity handled separately or rgba
        return colorHex(paint.color);
    }
    const QString id = QStringLiteral("kantan_grad_%1").arg(ctx.nextId++);
    if (paint.kind == FillKind::Linear) {
        const double rad = paint.angle * std::numbers::pi / 180.0;
        const double dx = std::cos(rad) * 0.5;
        const double dy = std::sin(rad) * 0.5;
        ctx.defs += QStringLiteral("    <linearGradient id=\"%1\" x1=\"%2\" y1=\"%3\" x2=\"%4\" y2=\"%5\">\n")
                        .arg(id)
                        .arg(0.5 - dx, 0, 'f', 4)
                        .arg(0.5 - dy, 0, 'f', 4)
                        .arg(0.5 + dx, 0, 'f', 4)
                        .arg(0.5 + dy, 0, 'f', 4);
        for (const auto& s : paint.stops) {
            ctx.defs += QStringLiteral("      <stop offset=\"%1%\" stop-color=\"%2\" stop-opacity=\"%3\"/>\n")
                            .arg(s.offset * 100.0, 0, 'f', 2)
                            .arg(colorHex(s.color))
                            .arg(colorAlpha(s.color), 0, 'f', 3);
        }
        ctx.defs += "    </linearGradient>\n";
    } else if (paint.kind == FillKind::Radial) {
        ctx.defs += QStringLiteral(
                        "    <radialGradient id=\"%1\" cx=\"%2\" cy=\"%3\" r=\"%4\" fx=\"%2\" fy=\"%3\">\n")
                        .arg(id)
                        .arg(paint.center.x, 0, 'f', 4)
                        .arg(paint.center.y, 0, 'f', 4)
                        .arg(paint.radius, 0, 'f', 4);
        for (const auto& s : paint.stops) {
            ctx.defs += QStringLiteral("      <stop offset=\"%1%\" stop-color=\"%2\" stop-opacity=\"%3\"/>\n")
                            .arg(s.offset * 100.0, 0, 'f', 2)
                            .arg(colorHex(s.color))
                            .arg(colorAlpha(s.color), 0, 'f', 3);
        }
        ctx.defs += "    </radialGradient>\n";
    }
    return QStringLiteral("url(#%1)").arg(id);
}

QString defineShadowFilter(SvgContext& ctx, const Shadow& shadow) {
    if (!shadow.enabled)
        return {};
    const QString id = QStringLiteral("kantan_shadow_%1").arg(ctx.nextId++);
    const double rad = shadow.angle * std::numbers::pi / 180.0;
    const double dx = shadow.distance * std::cos(rad);
    const double dy = shadow.distance * std::sin(rad);
    ctx.defs +=
        QStringLiteral("    <filter id=\"%1\" x=\"-30%\" y=\"-30%\" width=\"160%\" height=\"160%\">\n")
            .arg(id);
    ctx.defs += QStringLiteral("      <feDropShadow dx=\"%1\" dy=\"%2\" stdDeviation=\"%3\" "
                               "flood-color=\"%4\" flood-opacity=\"%5\"/>\n")
                    .arg(dx, 0, 'f', 2)
                    .arg(dy, 0, 'f', 2)
                    .arg(shadow.blur / 2.0, 0, 'f', 2)
                    .arg(colorHex(shadow.color))
                    .arg(shadow.opacity * colorAlpha(shadow.color), 0, 'f', 3);
    ctx.defs += "    </filter>\n";
    return QStringLiteral("url(#%1)").arg(id);
}

void emitObjectSvg(QString& out, SvgContext& ctx, const DocumentData& doc, const Object& o,
                   const RenderServices& services) {
    if (!o.visible || o.opacity <= 0.0)
        return;
    if (!ctx.included.empty() && ctx.included.find(o.id) == ctx.included.end())
        return;

    const Affine world = doc.worldMatrix(o.id);
    const QString filterAttr = defineShadowFilter(ctx, o.style.effects.dropShadow);

    for (int copy = 0; copy < std::max(1, o.repeat.count); ++copy) {
        const Affine m = world * repeatMatrix(o, copy);
        const QString tr =
            (m == Affine{}) ? QString{} : QStringLiteral(" transform=\"%1\"").arg(matrixStr(m));
        const QString op =
            (o.opacity < 1.0) ? QStringLiteral(" opacity=\"%1\"").arg(o.opacity, 0, 'f', 3) : QString{};
        const QString fl =
            filterAttr.isEmpty() ? QString{} : QStringLiteral(" filter=\"%1\"").arg(filterAttr);

        if (const auto* grp = o.as<GroupData>()) {
            Q_UNUSED(grp);
            out += QStringLiteral("  <g id=\"%1\"%2%3%4>\n")
                       .arg(xmlEscape(QString::fromStdString(o.name)))
                       .arg(tr)
                       .arg(op)
                       .arg(fl);
            for (const auto childId : doc.children(o.id)) {
                if (const auto* child = doc.find(childId))
                    emitObjectSvg(out, ctx, doc, *child, services);
            }
            out += "  </g>\n";
            continue;
        }

        if (const auto* clip = o.as<ClipData>()) {
            Q_UNUSED(clip);
            const auto children = doc.children(o.id);
            if (!children.empty()) {
                const auto* clipShape = doc.find(children.front());
                if (clipShape && clipShape->as<ShapeData>()) {
                    const auto* s = clipShape->as<ShapeData>();
                    const Path localP = resolvedCorners(shapePath(s->geometry, s->width, s->height));
                    const QString clipId = QStringLiteral("kantan_clip_%1").arg(ctx.nextId++);
                    const Affine clipM = doc.worldMatrix(clipShape->id);
                    ctx.defs += QStringLiteral("    <clipPath id=\"%1\">\n").arg(clipId);
                    ctx.defs += QStringLiteral("      <path d=\"%1\" transform=\"%2\"/>\n")
                                    .arg(pathToSvgD(localP))
                                    .arg(matrixStr(clipM));
                    ctx.defs += "    </clipPath>\n";
                    out += QStringLiteral("  <g clip-path=\"url(#%1)\"%2%3>\n").arg(clipId).arg(op).arg(fl);
                    for (std::size_t ci = 1; ci < children.size(); ++ci) {
                        if (const auto* child = doc.find(children[ci]))
                            emitObjectSvg(out, ctx, doc, *child, services);
                    }
                    out += "  </g>\n";
                    continue;
                }
            }
        }

        if (const auto* image = o.as<ImageData>()) {
            auto it = doc.images.find(image->imageKey);
            if (it != doc.images.end() && it->second.encoded) {
                const QString fmt =
                    it->second.format.empty() ? "png" : QString::fromStdString(it->second.format);
                const QString b64 =
                    QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(it->second.encoded->data()),
                                                   static_cast<qsizetype>(it->second.encoded->size()))
                                            .toBase64());
                out += QStringLiteral("  <image%1%2%3 x=\"%4\" y=\"%5\" width=\"%6\" height=\"%7\" "
                                      "href=\"data:image/%8;base64,%9\"/>\n")
                           .arg(tr)
                           .arg(op)
                           .arg(fl)
                           .arg(-image->width / 2.0, 0, 'f', 2)
                           .arg(-image->height / 2.0, 0, 'f', 2)
                           .arg(image->width, 0, 'f', 2)
                           .arg(image->height, 0, 'f', 2)
                           .arg(fmt)
                           .arg(b64);
                continue;
            }
        }

        // Geometry paths for Shape and Text
        Path localPath;
        bool isText = false;
        QString textContent;
        if (const auto* s = o.as<ShapeData>()) {
            localPath = resolvedCorners(shapePath(s->geometry, s->width, s->height));
        } else if (const auto* t = o.as<TextData>()) {
            isText = true;
            textContent = QString::fromStdString(t->text);
            if (services.text)
                localPath = services.text->shape(*t, doc);
        }

        const QString fillRef = definePaint(ctx, o.style.fill);
        QString fillOpacityAttr;
        if (o.style.fill.kind == FillKind::Solid && colorAlpha(o.style.fill.color) < 1.0)
            fillOpacityAttr =
                QStringLiteral(" fill-opacity=\"%1\"").arg(colorAlpha(o.style.fill.color), 0, 'f', 3);

        const QString fillRuleAttr = (localPath.rule == FillRule::EvenOdd) ? " fill-rule=\"evenodd\"" : "";

        // Outside outlines draw behind fill; center/inside draw above
        QStringList outsideStrokes, aboveStrokes;
        for (const auto& outline : o.style.outlines) {
            if (!outline.enabled || outline.width <= 0 || outline.paint.kind == FillKind::None)
                continue;
            const QString strokePaint = definePaint(ctx, outline.paint);
            const QString joinStr = outline.join == JoinStyle::Miter
                                        ? "miter"
                                        : (outline.join == JoinStyle::Bevel ? "bevel" : "round");
            QString strokeOp;
            if (outline.paint.kind == FillKind::Solid && colorAlpha(outline.paint.color) < 1.0)
                strokeOp =
                    QStringLiteral(" stroke-opacity=\"%1\"").arg(colorAlpha(outline.paint.color), 0, 'f', 3);
            const QString strokeAttrs =
                QStringLiteral(" fill=\"none\" stroke=\"%1\" stroke-width=\"%2\" stroke-linejoin=\"%3\"%4")
                    .arg(strokePaint)
                    .arg(outline.width, 0, 'f', 2)
                    .arg(joinStr)
                    .arg(strokeOp);
            if (outline.position == OutlinePosition::Outside)
                outsideStrokes.append(strokeAttrs);
            else
                aboveStrokes.append(strokeAttrs);
        }

        if (isText && localPath.empty() && o.as<TextData>()) {
            // Text fallback if text shaper produced no vector glyphs
            const auto* t = o.as<TextData>();
            const QString alignStr =
                t->align == TextAlign::Left ? "start" : (t->align == TextAlign::Right ? "end" : "middle");
            const QString italicStr = t->italic ? " font-style=\"italic\"" : "";
            out += QStringLiteral("  <text%1%2%3 x=\"0\" y=\"0\" font-family=\"%4\" font-size=\"%5\" "
                                  "font-weight=\"%6\" text-anchor=\"%7\" fill=\"%8\"%9%10>%11</text>\n")
                       .arg(tr)
                       .arg(op)
                       .arg(fl)
                       .arg(xmlEscape(QString::fromStdString(t->font.family)))
                       .arg(t->size, 0, 'f', 2)
                       .arg(t->weight)
                       .arg(alignStr)
                       .arg(fillRef)
                       .arg(fillOpacityAttr)
                       .arg(italicStr)
                       .arg(xmlEscape(textContent));
            continue;
        }

        if (localPath.empty())
            continue;

        const QString d = pathToSvgD(localPath);
        if (d.isEmpty())
            continue;

        // Emit outside stroke paths
        for (const auto& sAttrs : outsideStrokes) {
            out += QStringLiteral("  <path%1%2%3 d=\"%4\"%5/>\n").arg(tr).arg(op).arg(fl).arg(d).arg(sAttrs);
        }

        // Emit main fill path
        out += QStringLiteral("  <path%1%2%3 d=\"%4\" fill=\"%5\"%6%7")
                   .arg(tr)
                   .arg(op)
                   .arg(outsideStrokes.isEmpty() ? fl : QString{})
                   .arg(d)
                   .arg(fillRef)
                   .arg(fillOpacityAttr)
                   .arg(fillRuleAttr);

        // If single outline, can attach stroke directly
        if (aboveStrokes.size() == 1 && outsideStrokes.isEmpty()) {
            out += aboveStrokes.front();
            aboveStrokes.clear();
        }
        out += "/>\n";

        // Emit inside/center strokes
        for (const auto& sAttrs : aboveStrokes) {
            out += QStringLiteral("  <path%1%2 d=\"%3\"%4/>\n").arg(tr).arg(op).arg(d).arg(sAttrs);
        }
    }
}
} // namespace

QByteArray exportSvg(const DocumentData& doc, const SvgExportOptions& options,
                     const std::vector<ObjectId>& selection, const RenderServices& services) {
    SvgContext ctx;
    if (options.selectionOnly && !selection.empty()) {
        for (const auto id : selection) {
            ctx.included.insert(id);
            for (const auto childId : doc.children(id))
                ctx.included.insert(childId);
        }
    }

    double vx = 0, vy = 0, vw = doc.artboard.width, vh = doc.artboard.height;
    if (options.selectionOnly && !selection.empty()) {
        const Box sb = selectionBounds(doc, selection, services);
        if (sb.width() >= 1.0 && sb.height() >= 1.0) {
            vx = sb.x0;
            vy = sb.y0;
            vw = sb.width();
            vh = sb.height();
        }
    }

    QString body;
    // Emit top-level objects in document order
    for (const auto& obj : doc.objects) {
        if (!obj->parent)
            emitObjectSvg(body, ctx, doc, *obj, services);
    }

    QString xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += QStringLiteral(
               "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
               "viewBox=\"%1 %2 %3 %4\" width=\"%3\" height=\"%4\">\n")
               .arg(num(vx))
               .arg(num(vy))
               .arg(num(vw))
               .arg(num(vh));

    if (!ctx.defs.isEmpty()) {
        xml += "  <defs>\n";
        xml += ctx.defs;
        xml += "  </defs>\n";
    }

    if (!options.transparent) {
        xml += QStringLiteral(
                   "  <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" fill=\"%5\" fill-opacity=\"%6\"/>\n")
                   .arg(num(vx))
                   .arg(num(vy))
                   .arg(num(vw))
                   .arg(num(vh))
                   .arg(colorHex(options.background))
                   .arg(num(colorAlpha(options.background), 3));
    }

    xml += body;
    xml += "</svg>\n";
    return xml.toUtf8();
}

Result exportSvgFile(const QString& path, const DocumentData& doc, const SvgExportOptions& options,
                     const std::vector<ObjectId>& selection, const RenderServices& services) {
    if (path.isEmpty())
        return {Status::InvalidData, "Invalid file path"};
    const QByteArray bytes = exportSvg(doc, options, selection, services);
    if (!QDir().mkpath(QFileInfo(path).path()))
        return {Status::InvalidData, "Could not create target directory"};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {Status::InvalidData, "Could not open file for writing"};
    if (file.write(bytes) != bytes.size() || !file.commit())
        return {Status::InvalidData, "Failed to write SVG data"};
    return {};
}
} // namespace kantan::io

#include "io/SvgImport.h"
#include <QFile>
#include <QFileInfo>
#include <cstdio>
#include <cstring>
#include <nanosvg.h>
#include <numbers>

namespace kantan::io {
namespace {
Color fromAbgr(unsigned int c, double opacity) {
    return {static_cast<std::uint8_t>(c & 0xff), static_cast<std::uint8_t>((c >> 8) & 0xff),
            static_cast<std::uint8_t>((c >> 16) & 0xff),
            static_cast<std::uint8_t>(std::lround(((c >> 24) & 0xff) * std::clamp(opacity, 0., 1.)))};
}
Paint paintFrom(const NSVGpaint& p, double opacity) {
    Paint out;
    if (p.type == NSVG_PAINT_COLOR) {
        out.kind = FillKind::Solid;
        out.color = fromAbgr(p.color, opacity);
    } else if ((p.type == NSVG_PAINT_LINEAR_GRADIENT || p.type == NSVG_PAINT_RADIAL_GRADIENT) && p.gradient &&
               p.gradient->nstops > 0) {
        out.kind = p.type == NSVG_PAINT_LINEAR_GRADIENT ? FillKind::Linear : FillKind::Radial;
        out.stops.clear();
        for (int i = 0; i < p.gradient->nstops && i < 32; ++i)
            out.stops.push_back({std::clamp(static_cast<double>(p.gradient->stops[i].offset), 0., 1.),
                                 fromAbgr(p.gradient->stops[i].color, opacity)});
        out.color = out.stops.front().color;
        // nanosvg stores the inverse gradient transform; its y axis gives the gradient direction.
        const float* x = p.gradient->xform;
        const double det = static_cast<double>(x[0]) * x[3] - static_cast<double>(x[1]) * x[2];
        if (out.kind == FillKind::Linear && std::abs(det) > 1e-12) {
            const double ix = -x[1] / det, iy = x[0] / det;
            out.angle = std::atan2(iy, ix) * 180 / std::numbers::pi;
        }
    } else
        out.kind = FillKind::None;
    return out;
}
JoinStyle joinFrom(char join) {
    return join == NSVG_JOIN_ROUND   ? JoinStyle::Round
           : join == NSVG_JOIN_BEVEL ? JoinStyle::Bevel
                                     : JoinStyle::Miter;
}
std::vector<std::string> scanUnsupported(const QByteArray& bytes) {
    std::vector<std::string> out;
    for (const auto& [tag, label] :
         std::initializer_list<std::pair<const char*, const char*>>{{"<text", "text"},
                                                                    {"<image", "embedded images"},
                                                                    {"<filter", "filters"},
                                                                    {"<mask", "masks"},
                                                                    {"<clipPath", "clip paths"},
                                                                    {"<pattern", "patterns"},
                                                                    {"<use", "symbol references"}})
        if (bytes.contains(tag))
            out.push_back(label);
    return out;
}
} // namespace
SvgImport importSvg(const QByteArray& bytes, const QString& name, double targetSize) {
    if (bytes.size() > 16 * 1024 * 1024)
        return {{Status::ResourceLimit, "SVG file is too large"}, {}, {}};
    std::vector<char> buffer(static_cast<std::size_t>(bytes.size()) + 1);
    std::memcpy(buffer.data(), bytes.constData(), static_cast<std::size_t>(bytes.size()));
    buffer.back() = '\0';
    NSVGimage* image = nsvgParse(buffer.data(), "px", 96.f);
    if (!image)
        return {{Status::InvalidData, "Not a readable SVG file"}, {}, {}};
    struct Guard {
        NSVGimage* image;
        ~Guard() {
            nsvgDelete(image);
        }
    } guard{image};
    SvgImport out;
    out.unsupported = scanUnsupported(bytes);
    Box all = emptyBox();
    std::vector<std::pair<Path, Object>> shapes;
    std::size_t nodes = 0;
    for (NSVGshape* s = image->shapes; s; s = s->next) {
        if (!(s->flags & NSVG_FLAGS_VISIBLE))
            continue;
        Path path;
        path.rule = s->fillRule == NSVG_FILLRULE_EVENODD ? FillRule::EvenOdd : FillRule::NonZero;
        for (NSVGpath* p = s->paths; p; p = p->next) {
            if (p->npts < 1)
                continue;
            SubPath sub;
            sub.closed = p->closed != 0;
            const float* pt = p->pts;
            sub.nodes.push_back(Node::corner({pt[0], pt[1]}));
            for (int i = 0; i + 3 < p->npts; i += 3) {
                const float* c = &pt[i * 2];
                sub.nodes.back().out = {c[2], c[3]};
                const Point end{c[6], c[7]};
                sub.nodes.push_back({end, {c[4], c[5]}, end, NodeType::Corner});
            }
            if (sub.closed && sub.nodes.size() > 1 &&
                length(sub.nodes.back().anchor - sub.nodes.front().anchor) < 1e-6) {
                sub.nodes.front().in = sub.nodes.back().in;
                sub.nodes.pop_back();
            }
            nodes += sub.nodes.size();
            if (nodes > 200000)
                return {{Status::ResourceLimit, "SVG has too many path points"}, {}, {}};
            path.subpaths.push_back(std::move(sub));
        }
        if (path.empty())
            continue;
        Object o;
        o.name = s->id[0] ? s->id : "Path";
        o.opacity = 1;
        o.style.fill = paintFrom(s->fill, s->opacity);
        bool openOnly = true;
        for (const auto& sub : path.subpaths)
            openOnly = openOnly && !sub.closed;
        if (openOnly)
            o.style.fill.kind = FillKind::None;
        if (s->stroke.type != NSVG_PAINT_NONE && s->strokeWidth > 0)
            o.style.outlines.push_back({true, s->strokeWidth, paintFrom(s->stroke, s->opacity),
                                        OutlinePosition::Center, joinFrom(s->strokeLineJoin),
                                        std::max(1., static_cast<double>(s->miterLimit))});
        if (o.style.fill.kind == FillKind::None && o.style.outlines.empty())
            continue;
        if (openOnly && !o.style.outlines.empty())
            out.unsupported.push_back("open strokes (shown as their closed outline)");
        all = united(all, bounds(path));
        shapes.push_back({std::move(path), std::move(o)});
    }
    if (shapes.empty())
        return {
            {Status::InvalidData, "The SVG contains no supported shapes"}, {}, std::move(out.unsupported)};
    const double scale = targetSize > 0 ? targetSize / std::max(all.width(), all.height()) : 1;
    const Point center = all.center();
    // Each shape is centred on its own bounds; positions place it relative to the group centre.
    std::vector<Object> objects;
    for (auto& [path, o] : shapes) {
        auto scaled =
            transformed(path, Affine::scaling(scale, scale) * Affine::translation(-center.x, -center.y));
        const Box b = bounds(scaled);
        const Point c = b.center();
        scaled = transformed(scaled, Affine::translation(-c.x, -c.y));
        for (auto& outline : o.style.outlines)
            outline.width *= scale;
        o.transform.position = c;
        o.data = ShapeData{b.width(), b.height(), std::move(scaled)};
        objects.push_back(std::move(o));
    }
    if (objects.size() == 1) {
        objects[0].name = QFileInfo(name).completeBaseName().toStdString();
        out.objects = std::move(objects);
    } else {
        Object group;
        group.id = {1};
        group.name = QFileInfo(name).completeBaseName().toStdString();
        group.data = GroupData{};
        out.objects.push_back(group);
        std::uint64_t id = 2;
        for (auto& o : objects) {
            o.id = {id++};
            o.parent = {1};
            out.objects.push_back(std::move(o));
        }
    }
    std::sort(out.unsupported.begin(), out.unsupported.end());
    out.unsupported.erase(std::unique(out.unsupported.begin(), out.unsupported.end()), out.unsupported.end());
    return out;
}
SvgImport importSvgFile(const QString& path, double targetSize) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{Status::InvalidData, file.errorString().toStdString()}, {}, {}};
    if (file.size() > 16 * 1024 * 1024)
        return {{Status::ResourceLimit, "SVG file is too large"}, {}, {}};
    return importSvg(file.readAll(), QFileInfo(path).fileName(), targetSize);
}
} // namespace kantan::io

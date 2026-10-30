#include "io/Serialization.h"
#include "editing/Transforms.h"
#include <QRegularExpression>
#include <set>

namespace kantan::io {
namespace {
[[noreturn]] void fail(const char* what) {
    throw std::invalid_argument(what);
}
double number(const QJsonObject& o, const char* key, double min, double max) {
    const auto v = o.value(QLatin1String(key));
    if (!v.isDouble())
        fail("Missing numeric field");
    const double n = v.toDouble();
    if (!std::isfinite(n) || n < min || n > max)
        fail("Numeric field out of range");
    return n;
}
double optionalNumber(const QJsonObject& o, const char* key, double min, double max, double fallback) {
    return o.contains(QLatin1String(key)) ? number(o, key, min, max) : fallback;
}
int integer(const QJsonObject& o, const char* key, int min, int max) {
    const double n = number(o, key, min, max);
    if (n != std::floor(n))
        fail("Integer field is fractional");
    return static_cast<int>(n);
}
bool boolean(const QJsonObject& o, const char* key) {
    const auto v = o.value(QLatin1String(key));
    if (!v.isBool())
        fail("Missing boolean field");
    return v.toBool();
}
bool optionalBoolean(const QJsonObject& o, const char* key, bool fallback) {
    return o.contains(QLatin1String(key)) ? boolean(o, key) : fallback;
}
QString string(const QJsonObject& o, const char* key, int maxLength = 4096) {
    const auto v = o.value(QLatin1String(key));
    if (!v.isString() || v.toString().size() > maxLength)
        fail("Missing or oversized text field");
    return v.toString();
}
QJsonObject object(const QJsonObject& o, const char* key) {
    const auto v = o.value(QLatin1String(key));
    if (!v.isObject())
        fail("Missing object field");
    return v.toObject();
}
QJsonArray array(const QJsonObject& o, const char* key, int maxSize) {
    const auto v = o.value(QLatin1String(key));
    if (!v.isArray() || v.toArray().size() > maxSize)
        fail("Missing or oversized array field");
    return v.toArray();
}
template <class E> E enumeration(const QJsonObject& o, const char* key, int count) {
    return static_cast<E>(integer(o, key, 0, count - 1));
}
std::uint64_t identity(const QJsonObject& o, const char* key, bool zero = false) {
    const auto v = o.value(QLatin1String(key));
    bool valid = false;
    const auto n = v.toString().toULongLong(&valid);
    if (!v.isString() || !valid || (!n && !zero) || n > 1000000000000000ULL)
        fail("Invalid identity");
    return n;
}
QJsonArray point(Point p) {
    return {p.x, p.y};
}
Point pointFrom(const QJsonValue& v) {
    const auto a = v.toArray();
    if (!v.isArray() || a.size() != 2 || !a[0].isDouble() || !a[1].isDouble())
        fail("Invalid point");
    const Point p{a[0].toDouble(), a[1].toDouble()};
    if (!finite(p) || std::abs(p.x) > CoordinateLimit || std::abs(p.y) > CoordinateLimit)
        fail("Point out of range");
    return p;
}
QJsonObject paintToJson(const Paint& p) {
    QJsonArray stops;
    for (const auto& s : p.stops)
        stops.append(QJsonObject{{"offset", s.offset}, {"color", colorToString(s.color)}});
    return {{"kind", static_cast<int>(p.kind)},
            {"color", colorToString(p.color)},
            {"stops", stops},
            {"angle", p.angle},
            {"center", point(p.center)},
            {"radius", p.radius}};
}
Paint paintFromJson(const QJsonObject& o) {
    Paint p;
    p.kind = enumeration<FillKind>(o, "kind", 4);
    p.color = colorFromString(string(o, "color", 16));
    p.stops.clear();
    for (const auto v : array(o, "stops", 32)) {
        if (!v.isObject())
            fail("Invalid gradient stop");
        const auto s = v.toObject();
        p.stops.push_back({number(s, "offset", 0, 1), colorFromString(string(s, "color", 16))});
    }
    std::stable_sort(p.stops.begin(), p.stops.end(),
                     [](const auto& a, const auto& b) { return a.offset < b.offset; });
    p.angle = number(o, "angle", -36000, 36000);
    p.center = pointFrom(o.value("center"));
    p.radius = number(o, "radius", 0, 100);
    return p;
}
QJsonObject shadowToJson(const Shadow& s) {
    return {{"enabled", s.enabled}, {"angle", s.angle},   {"distance", s.distance},
            {"blur", s.blur},       {"spread", s.spread}, {"color", colorToString(s.color)},
            {"opacity", s.opacity}};
}
Shadow shadowFromJson(const QJsonObject& o) {
    return {boolean(o, "enabled"),          number(o, "angle", -36000, 36000),
            number(o, "distance", 0, 4096), number(o, "blur", 0, 1024),
            number(o, "spread", 0, 1),      colorFromString(string(o, "color", 16)),
            number(o, "opacity", 0, 1)};
}
QJsonObject glowToJson(const Glow& g) {
    return {{"enabled", g.enabled},
            {"size", g.size},
            {"spread", g.spread},
            {"color", colorToString(g.color)},
            {"opacity", g.opacity}};
}
Glow glowFromJson(const QJsonObject& o) {
    return {boolean(o, "enabled"), number(o, "size", 0, 1024), number(o, "spread", 0, 1),
            colorFromString(string(o, "color", 16)), number(o, "opacity", 0, 1)};
}
QJsonObject bevelToJson(const Bevel& b) {
    return {{"enabled", b.enabled},
            {"style", static_cast<int>(b.style)},
            {"profile", static_cast<int>(b.profile)},
            {"size", b.size},
            {"depth", b.depth},
            {"softness", b.softness},
            {"lightAngle", b.lightAngle},
            {"altitude", b.altitude},
            {"highlight", colorToString(b.highlight)},
            {"highlightOpacity", b.highlightOpacity},
            {"shadow", colorToString(b.shadow)},
            {"shadowOpacity", b.shadowOpacity}};
}
Bevel bevelFromJson(const QJsonObject& o) {
    Bevel b;
    b.enabled = boolean(o, "enabled");
    b.style = enumeration<BevelStyle>(o, "style", 4);
    b.profile = enumeration<BevelProfile>(o, "profile", 3);
    b.size = number(o, "size", 0, 1024);
    b.depth = number(o, "depth", 0, 10);
    b.softness = number(o, "softness", 0, 1024);
    b.lightAngle = number(o, "lightAngle", -36000, 36000);
    b.altitude = number(o, "altitude", 0, 90);
    b.highlight = colorFromString(string(o, "highlight", 16));
    b.highlightOpacity = number(o, "highlightOpacity", 0, 1);
    b.shadow = colorFromString(string(o, "shadow", 16));
    b.shadowOpacity = number(o, "shadowOpacity", 0, 1);
    return b;
}
QJsonObject pathToJson(const Path& path) {
    QJsonArray subpaths;
    for (const auto& s : path.subpaths) {
        QJsonArray nodes;
        for (const auto& n : s.nodes)
            nodes.append(QJsonArray{n.anchor.x, n.anchor.y, n.in.x, n.in.y, n.out.x, n.out.y,
                                    n.type == NodeType::Smooth ? 1 : 0});
        subpaths.append(QJsonObject{{"closed", s.closed}, {"nodes", nodes}});
    }
    return {{"rule", static_cast<int>(path.rule)}, {"subpaths", subpaths}};
}
Path pathFromJson(const QJsonObject& o) {
    Path path;
    path.rule = enumeration<FillRule>(o, "rule", 2);
    std::size_t total = 0;
    for (const auto sv : array(o, "subpaths", 4096)) {
        if (!sv.isObject())
            fail("Invalid subpath");
        const auto so = sv.toObject();
        SubPath s;
        s.closed = boolean(so, "closed");
        for (const auto nv : array(so, "nodes", 100000)) {
            const auto a = nv.toArray();
            if (!nv.isArray() || a.size() != 7)
                fail("Invalid path node");
            double v[7];
            for (qsizetype i = 0; i < 7; ++i) {
                if (!a[i].isDouble() || !std::isfinite(a[i].toDouble()) ||
                    std::abs(a[i].toDouble()) > CoordinateLimit)
                    fail("Path node out of range");
                v[i] = a[i].toDouble();
            }
            s.nodes.push_back(
                {{v[0], v[1]}, {v[2], v[3]}, {v[4], v[5]}, v[6] != 0 ? NodeType::Smooth : NodeType::Corner});
            if (++total > 200000)
                fail("Path has too many nodes");
        }
        path.subpaths.push_back(std::move(s));
    }
    return path;
}
QJsonObject geometryToJson(const ShapeGeometry& g) {
    return std::visit(
        [](const auto& v) -> QJsonObject {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, RectangleParams>)
                return {{"type", "rectangle"},
                        {"radii", QJsonArray{v.radii[0], v.radii[1], v.radii[2], v.radii[3]}},
                        {"linked", v.linked},
                        {"corner", static_cast<int>(v.corner)}};
            else if constexpr (std::is_same_v<T, EllipseParams>)
                return {{"type", "ellipse"}};
            else if constexpr (std::is_same_v<T, PolygonParams>)
                return {{"type", "polygon"}, {"sides", v.sides}, {"cornerRadius", v.cornerRadius}};
            else if constexpr (std::is_same_v<T, StarParams>)
                return {{"type", "star"},
                        {"points", v.points},
                        {"innerRatio", v.innerRatio},
                        {"cornerRadius", v.cornerRadius}};
            else if constexpr (std::is_same_v<T, RingParams>)
                return {{"type", "ring"},
                        {"thickness", v.thickness},
                        {"startAngle", v.startAngle},
                        {"endAngle", v.endAngle}};
            else if constexpr (std::is_same_v<T, GearParams>)
                return {{"type", "gear"},
                        {"teeth", v.teeth},
                        {"toothDepth", v.toothDepth},
                        {"holeRadius", v.holeRadius}};
            else if constexpr (std::is_same_v<T, SimpleParams>)
                return {{"type", "simple"},
                        {"kind", static_cast<int>(v.kind)},
                        {"values", QJsonArray{v.values[0], v.values[1], v.values[2]}}};
            else
                return {{"type", "path"}, {"path", pathToJson(v)}};
        },
        g);
}
ShapeGeometry geometryFromJson(const QJsonObject& o) {
    const auto type = string(o, "type", 32);
    if (type == "rectangle") {
        RectangleParams r;
        const auto radii = array(o, "radii", 4);
        if (radii.size() != 4)
            fail("Rectangle needs four radii");
        for (qsizetype i = 0; i < 4; ++i) {
            if (!radii[i].isDouble() || !std::isfinite(radii[i].toDouble()) || radii[i].toDouble() < 0 ||
                radii[i].toDouble() > CoordinateLimit)
                fail("Invalid corner radius");
            r.radii[static_cast<std::size_t>(i)] = radii[i].toDouble();
        }
        r.linked = boolean(o, "linked");
        r.corner = enumeration<CornerStyle>(o, "corner", 3);
        return r;
    }
    if (type == "ellipse")
        return EllipseParams{};
    if (type == "polygon")
        return PolygonParams{integer(o, "sides", 3, 64), number(o, "cornerRadius", 0, CoordinateLimit)};
    if (type == "star")
        return StarParams{integer(o, "points", 3, 64), number(o, "innerRatio", .001, 1),
                          number(o, "cornerRadius", 0, CoordinateLimit)};
    if (type == "ring")
        return RingParams{number(o, "thickness", .001, 1), number(o, "startAngle", -36000, 36000),
                          number(o, "endAngle", -36000, 36000)};
    if (type == "gear")
        return GearParams{integer(o, "teeth", 3, 128), number(o, "toothDepth", .001, .999),
                          number(o, "holeRadius", 0, .999)};
    if (type == "simple") {
        SimpleParams s;
        s.kind = enumeration<SimpleKind>(o, "kind", 7);
        const auto values = array(o, "values", 3);
        if (values.size() != 3)
            fail("Shape needs three parameters");
        for (qsizetype i = 0; i < 3; ++i) {
            if (!values[i].isDouble() || !std::isfinite(values[i].toDouble()) ||
                std::abs(values[i].toDouble()) > 1e6)
                fail("Invalid shape parameter");
            s.values[static_cast<std::size_t>(i)] = values[i].toDouble();
        }
        return s;
    }
    if (type == "path")
        return pathFromJson(object(o, "path"));
    fail("Unknown shape type");
}
QJsonObject dataToJson(const ObjectData& data) {
    return std::visit(
        [](const auto& v) -> QJsonObject {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ShapeData>)
                return {{"width", v.width}, {"height", v.height}, {"geometry", geometryToJson(v.geometry)}};
            else if constexpr (std::is_same_v<T, TextData>)
                return {{"text", QString::fromStdString(v.text)},
                        {"family", QString::fromStdString(v.font.family)},
                        {"fontKey", QString::fromStdString(v.font.fileKey)},
                        {"size", v.size},
                        {"weight", v.weight},
                        {"italic", v.italic},
                        {"allCaps", v.allCaps},
                        {"tracking", v.tracking},
                        {"lineHeight", v.lineHeight},
                        {"align", static_cast<int>(v.align)},
                        {"arc", v.arc}};
            else if constexpr (std::is_same_v<T, ImageData>) {
                const auto& a = v.adjust;
                return {{"image", QString::fromStdString(v.imageKey)},
                        {"width", v.width},
                        {"height", v.height},
                        {"crop", QJsonArray{v.crop.x0, v.crop.y0, v.crop.x1, v.crop.y1}},
                        {"adjust", QJsonObject{{"brightness", a.brightness},
                                               {"contrast", a.contrast},
                                               {"saturation", a.saturation},
                                               {"hue", a.hue},
                                               {"tint", colorToString(a.tint)},
                                               {"tintAmount", a.tintAmount},
                                               {"colorKey", a.colorKey},
                                               {"keyColor", colorToString(a.keyColor)},
                                               {"keyTolerance", a.keyTolerance},
                                               {"keySoftness", a.keySoftness}}}};
            } else if constexpr (std::is_same_v<T, BooleanData>)
                return {{"op", static_cast<int>(v.op)}};
            else
                return {};
        },
        data);
}
ObjectData dataFromJson(ObjectKind kind, const QJsonObject& o) {
    switch (kind) {
    case ObjectKind::Shape:
        return ShapeData{number(o, "width", 0, CoordinateLimit), number(o, "height", 0, CoordinateLimit),
                         geometryFromJson(object(o, "geometry"))};
    case ObjectKind::Text: {
        TextData t;
        t.text = string(o, "text").toStdString();
        t.font.family = string(o, "family", 256).toStdString();
        t.font.fileKey = string(o, "fontKey", 128).toStdString();
        t.size = number(o, "size", .5, 4096);
        t.weight = integer(o, "weight", 1, 1000);
        t.italic = boolean(o, "italic");
        t.allCaps = boolean(o, "allCaps");
        t.tracking = number(o, "tracking", -1000, 5000);
        t.lineHeight = number(o, "lineHeight", .1, 10);
        t.align = enumeration<TextAlign>(o, "align", 3);
        t.arc = number(o, "arc", -1, 1);
        return t;
    }
    case ObjectKind::Image: {
        ImageData d;
        d.imageKey = string(o, "image", 128).toStdString();
        d.width = number(o, "width", 0, CoordinateLimit);
        d.height = number(o, "height", 0, CoordinateLimit);
        const auto crop = array(o, "crop", 4);
        if (crop.size() != 4)
            fail("Invalid crop");
        double c[4];
        for (qsizetype i = 0; i < 4; ++i) {
            if (!crop[i].isDouble() || !std::isfinite(crop[i].toDouble()))
                fail("Invalid crop");
            c[i] = crop[i].toDouble();
        }
        d.crop = {c[0], c[1], c[2], c[3]};
        const auto a = object(o, "adjust");
        d.adjust.brightness = number(a, "brightness", -1, 1);
        d.adjust.contrast = number(a, "contrast", -1, 1);
        d.adjust.saturation = number(a, "saturation", -1, 1);
        d.adjust.hue = number(a, "hue", -1, 1);
        d.adjust.tint = colorFromString(string(a, "tint", 16));
        d.adjust.tintAmount = number(a, "tintAmount", 0, 1);
        d.adjust.colorKey = boolean(a, "colorKey");
        d.adjust.keyColor = colorFromString(string(a, "keyColor", 16));
        d.adjust.keyTolerance = number(a, "keyTolerance", 0, 1);
        d.adjust.keySoftness = number(a, "keySoftness", 0, 1);
        return d;
    }
    case ObjectKind::Boolean:
        return BooleanData{enumeration<BooleanOp>(o, "op", 4)};
    case ObjectKind::Group:
        return GroupData{};
    case ObjectKind::Clip:
        return ClipData{};
    }
    fail("Unknown object kind");
}
QJsonArray doubles(const std::vector<double>& values) {
    QJsonArray a;
    for (double v : values)
        a.append(v);
    return a;
}
std::vector<double> doublesFrom(const QJsonArray& a, double min, double max) {
    std::vector<double> out;
    for (const auto v : a) {
        if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < min || v.toDouble() > max)
            fail("Invalid number in list");
        out.push_back(v.toDouble());
    }
    return out;
}
} // namespace
QString colorToString(Color c) {
    return QStringLiteral("#%1%2%3%4")
        .arg(c.r, 2, 16, QLatin1Char('0'))
        .arg(c.g, 2, 16, QLatin1Char('0'))
        .arg(c.b, 2, 16, QLatin1Char('0'))
        .arg(c.a, 2, 16, QLatin1Char('0'));
}
Color colorFromString(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^#[0-9a-fA-F]{8}$"));
    if (!pattern.match(text).hasMatch())
        fail("Invalid colour");
    const auto byte = [&](int i) {
        return static_cast<std::uint8_t>(text.mid(1 + i * 2, 2).toUInt(nullptr, 16));
    };
    return {byte(0), byte(1), byte(2), byte(3)};
}
QJsonObject styleToJson(const Style& s) {
    QJsonArray outlines;
    for (const auto& o : s.outlines)
        outlines.append(QJsonObject{{"enabled", o.enabled},
                                    {"width", o.width},
                                    {"paint", paintToJson(o.paint)},
                                    {"position", static_cast<int>(o.position)},
                                    {"join", static_cast<int>(o.join)},
                                    {"miterLimit", o.miterLimit}});
    const auto& e = s.effects;
    return {{"fill", paintToJson(s.fill)},
            {"outlines", outlines},
            {"effects", QJsonObject{{"dropShadow", shadowToJson(e.dropShadow)},
                                    {"innerShadow", shadowToJson(e.innerShadow)},
                                    {"outerGlow", glowToJson(e.outerGlow)},
                                    {"innerGlow", glowToJson(e.innerGlow)},
                                    {"bevel", bevelToJson(e.bevel)}}}};
}
Style styleFromJson(const QJsonObject& o) {
    Style s;
    s.fill = paintFromJson(object(o, "fill"));
    for (const auto v : array(o, "outlines", static_cast<int>(MaxOutlines))) {
        if (!v.isObject())
            fail("Invalid outline");
        const auto ol = v.toObject();
        s.outlines.push_back({boolean(ol, "enabled"), number(ol, "width", 0, 4096),
                              paintFromJson(object(ol, "paint")),
                              enumeration<OutlinePosition>(ol, "position", 3),
                              enumeration<JoinStyle>(ol, "join", 3), number(ol, "miterLimit", 1, 100)});
    }
    const auto e = object(o, "effects");
    s.effects.dropShadow = shadowFromJson(object(e, "dropShadow"));
    s.effects.innerShadow = shadowFromJson(object(e, "innerShadow"));
    s.effects.outerGlow = glowFromJson(object(e, "outerGlow"));
    s.effects.innerGlow = glowFromJson(object(e, "innerGlow"));
    s.effects.bevel = bevelFromJson(object(e, "bevel"));
    return s;
}
QJsonObject objectToJson(const Object& o) {
    const auto& t = o.transform;
    return {{"id", QString::number(o.id.value)},
            {"parent", QString::number(o.parent.value)},
            {"kind", static_cast<int>(o.kind())},
            {"name", QString::fromStdString(o.name)},
            {"visible", o.visible},
            {"locked", o.locked},
            {"opacity", o.opacity},
            {"blend", static_cast<int>(o.blend)},
            {"transform", QJsonObject{{"position", point(t.position)},
                                      {"rotation", t.rotation},
                                      {"scaleX", t.scaleX},
                                      {"scaleY", t.scaleY}}},
            {"style", styleToJson(o.style)},
            {"minExportSize", o.minExportSize},
            {"maxExportSize", o.maxExportSize},
            {"repeat", QJsonObject{{"count", o.repeat.count}, {"pivot", point(o.repeat.pivot)}}},
            {"data", dataToJson(o.data)}};
}
Object objectFromJson(const QJsonObject& j) {
    Object o;
    o.id = {identity(j, "id")};
    o.parent = {identity(j, "parent", true)};
    const auto kind = enumeration<ObjectKind>(j, "kind", 6);
    o.name = string(j, "name", 256).toStdString();
    o.visible = boolean(j, "visible");
    o.locked = boolean(j, "locked");
    o.opacity = number(j, "opacity", 0, 1);
    o.blend = enumeration<Blend>(j, "blend", 5);
    const auto t = object(j, "transform");
    o.transform = {pointFrom(t.value("position")), number(t, "rotation", -36000, 36000),
                   number(t, "scaleX", -1e5, 1e5), number(t, "scaleY", -1e5, 1e5)};
    o.style = styleFromJson(object(j, "style"));
    o.minExportSize = optionalNumber(j, "minExportSize", 0, 1e6, 0);
    o.maxExportSize = optionalNumber(j, "maxExportSize", 0, 1e6, 0);
    if (j.contains("repeat")) {
        const auto r = object(j, "repeat");
        o.repeat = {integer(r, "count", 1, 360), pointFrom(r.value("pivot"))};
    }
    o.data = dataFromJson(kind, object(j, "data"));
    return o;
}
QJsonObject artboardToJson(const Artboard& a) {
    return {{"width", a.width},
            {"height", a.height},
            {"transparent", a.transparent},
            {"background", colorToString(a.background)},
            {"guidesX", doubles(a.guidesX)},
            {"guidesY", doubles(a.guidesY)}};
}
Artboard artboardFromJson(const QJsonObject& o) {
    Artboard a;
    a.width = number(o, "width", 1, MaxArtboard);
    a.height = number(o, "height", 1, MaxArtboard);
    a.transparent = boolean(o, "transparent");
    a.background = colorFromString(string(o, "background", 16));
    if (o.contains("guidesX"))
        a.guidesX = doublesFrom(array(o, "guidesX", 256), -MaxArtboard, 2 * MaxArtboard);
    if (o.contains("guidesY"))
        a.guidesY = doublesFrom(array(o, "guidesY", 256), -MaxArtboard, 2 * MaxArtboard);
    return a;
}
QJsonObject exportToJson(const ExportSettings& s) {
    QJsonArray png, ico, custom;
    for (int v : s.pngSizes)
        png.append(v);
    for (int v : s.icoSizes)
        ico.append(v);
    for (const auto& c : s.custom)
        custom.append(QJsonArray{c.width, c.height});
    QJsonArray outputs;
    for (const auto& o : s.outputs)
        outputs.append(
            QJsonObject{{"width", o.width}, {"height", o.height}, {"path", QString::fromStdString(o.path)}});
    return {{"png", s.png},
            {"ico", s.ico},
            {"pngSizes", png},
            {"icoSizes", ico},
            {"custom", custom},
            {"padding", s.padding},
            {"fit", static_cast<int>(s.fit)},
            {"transparent", s.transparent},
            {"background", colorToString(s.background)},
            {"selectionOnly", s.selectionOnly},
            {"naming", QString::fromStdString(s.naming)},
            {"icoName", QString::fromStdString(s.icoName)},
            {"outputs", outputs},
            {"preset", QString::fromStdString(s.preset)},
            {"folder", QString::fromStdString(s.folder)}};
}
ExportSettings exportFromJson(const QJsonObject& o) {
    ExportSettings s;
    s.png = boolean(o, "png");
    s.ico = boolean(o, "ico");
    const auto sizes = [](const QJsonArray& a, int max) {
        std::vector<int> out;
        for (const auto v : a) {
            if (!v.isDouble() || v.toDouble() != std::floor(v.toDouble()) || v.toDouble() < 1 ||
                v.toDouble() > max)
                fail("Invalid export size");
            out.push_back(v.toInt());
        }
        return out;
    };
    s.pngSizes = sizes(array(o, "pngSizes", 64), 8192);
    s.icoSizes = sizes(array(o, "icoSizes", 64), 256);
    s.custom.clear();
    for (const auto v : array(o, "custom", 64)) {
        const auto pair = sizes(v.toArray(), 8192);
        if (!v.isArray() || pair.size() != 2)
            fail("Invalid custom size");
        s.custom.push_back({pair[0], pair[1]});
    }
    s.padding = number(o, "padding", 0, 45);
    s.fit = enumeration<ExportFit>(o, "fit", 2);
    s.transparent = boolean(o, "transparent");
    s.background = colorFromString(string(o, "background", 16));
    s.selectionOnly = optionalBoolean(o, "selectionOnly", false);
    s.naming = string(o, "naming", 256).toStdString();
    s.folder = string(o, "folder", 4096).toStdString();
    if (o.contains("icoName"))
        s.icoName = string(o, "icoName", 256).toStdString();
    if (o.contains("preset"))
        s.preset = string(o, "preset", 128).toStdString();
    s.outputs.clear();
    if (o.contains("outputs"))
        for (const auto v : array(o, "outputs", 64)) {
            if (!v.isObject())
                fail("Invalid named output");
            const auto n = v.toObject();
            s.outputs.push_back({integer(n, "width", 1, 8192), integer(n, "height", 1, 8192),
                                 string(n, "path", 512).toStdString()});
        }
    return s;
}
QJsonArray paletteToJson(const std::vector<PaletteColor>& palette) {
    QJsonArray out;
    for (const auto& p : palette)
        out.append(QJsonObject{{"name", QString::fromStdString(p.name)}, {"color", colorToString(p.color)}});
    return out;
}
std::vector<PaletteColor> paletteFromJson(const QJsonArray& json) {
    if (json.size() > static_cast<qsizetype>(MaxPalette))
        fail("Palette is too large");
    std::vector<PaletteColor> out;
    for (const auto v : json) {
        if (!v.isObject())
            fail("Invalid palette colour");
        const auto o = v.toObject();
        out.push_back({string(o, "name", 128).toStdString(), colorFromString(string(o, "color", 16))});
    }
    return out;
}
QJsonObject fragmentToJson(const DocumentData& doc, const std::vector<ObjectId>& roots) {
    QJsonArray objects;
    QJsonArray images;
    std::set<std::string> keys;
    for (std::size_t i = 0; i < doc.objects.size(); ++i) {
        const auto& o = *doc.objects[i];
        if (std::find(roots.begin(), roots.end(), o.id) == roots.end())
            continue;
        const auto end = doc.subtreeEnd(i);
        for (auto j = i; j < end; ++j) {
            Object copy = *doc.objects[j];
            if (j == i) {
                copy.parent = {};
                // Bake ancestor transforms so the fragment looks the same when placed at the root.
                if (const auto world = decompose(doc.worldMatrix(copy.id)))
                    copy.transform = *world;
            }
            if (const auto* image = copy.as<ImageData>())
                keys.insert(image->imageKey);
            objects.append(objectToJson(copy));
        }
    }
    for (const auto& k : keys)
        images.append(QString::fromStdString(k));
    return {{"format", "kantanlogo-fragment"}, {"version", 1}, {"objects", objects}, {"images", images}};
}
Fragment fragmentFromJson(const QJsonObject& json, const std::map<std::string, EmbeddedImage>& images) {
    if (json.value("format") != "kantanlogo-fragment" || integer(json, "version", 1, 1) != 1)
        fail("Not a Kantan LogoMaker fragment");
    Fragment f;
    std::set<std::uint64_t> seen;
    for (const auto v : array(json, "objects", static_cast<int>(MaxObjects))) {
        if (!v.isObject())
            fail("Invalid object");
        auto o = objectFromJson(v.toObject());
        if (!seen.insert(o.id.value).second)
            fail("Duplicate object in fragment");
        if (o.parent && !seen.contains(o.parent.value))
            fail("Fragment order is not depth-first");
        if (const auto* image = o.as<ImageData>()) {
            const auto found = images.find(image->imageKey);
            if (found == images.end())
                fail("Fragment refers to an unavailable image");
            f.images[image->imageKey] = found->second;
        }
        f.objects.push_back(std::move(o));
    }
    return f;
}
} // namespace kantan::io

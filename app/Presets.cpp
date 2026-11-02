#include "app/Presets.h"
#include "io/ExportPresets.h"

namespace kantan::app {
namespace {
SimpleParams simple(SimpleKind kind) {
    SimpleParams p{kind};
    const auto ranges = simpleParamRanges(kind);
    for (std::size_t i = 0; i < 3; ++i)
        p.values[i] = ranges[i].defaultValue;
    return p;
}
Paint solid(Color c) {
    return {FillKind::Solid, c};
}
Paint linear(Color a, Color b, double angle = 90) {
    Paint p{FillKind::Linear, a};
    p.stops = {{0, a}, {1, b}};
    p.angle = angle;
    return p;
}
Outline outline(double width, Color c, OutlinePosition position = OutlinePosition::Outside) {
    return {true, width, solid(c), position, JoinStyle::Round, 4};
}
} // namespace
const std::vector<ShapePreset>& shapePresets() {
    static const std::vector<ShapePreset> presets{
        {"Rectangle", RectangleParams{}, 220, 160},
        {"Rounded rectangle", RectangleParams{{32, 32, 32, 32}}, 220, 160},
        {"App icon squircle", RectangleParams{{90, 90, 90, 90}}, 400, 400},
        {"Ellipse", EllipseParams{}, 200, 200},
        {"Triangle", PolygonParams{3, 0}, 220, 200},
        {"Hexagon", PolygonParams{6, 0}, 220, 220},
        {"Octagon", PolygonParams{8, 0}, 220, 220},
        {"Star", StarParams{5, .45, 0}, 220, 220},
        {"Rounded star", StarParams{8, .7, 6}, 220, 220},
        {"Ring", RingParams{.28, 0, 360}, 220, 220},
        {"Arc", RingParams{.2, -120, 120}, 220, 220},
        {"Gear", GearParams{12, .18, .3}, 220, 220},
        {"Shield", simple(SimpleKind::Shield), 200, 240},
        {"Badge", simple(SimpleKind::Badge), 240, 240},
        {"Ribbon", simple(SimpleKind::Ribbon), 320, 80},
        {"Speech bubble", simple(SimpleKind::Bubble), 240, 180},
        {"Arrow", simple(SimpleKind::Arrow), 240, 140},
        {"Heart", simple(SimpleKind::Heart), 220, 200},
        {"Burst", simple(SimpleKind::Burst), 240, 240},
    };
    return presets;
}
Style defaultShapeStyle() {
    Style s;
    s.fill = solid(DefaultShapeColor);
    return s;
}
Style defaultTextStyle() {
    Style s;
    s.fill = solid({34, 38, 48, 255});
    return s;
}
const std::vector<StylePreset>& stylePresets() {
    static const std::vector<StylePreset> presets = [] {
        std::vector<StylePreset> out;
        {
            Style s;
            s.fill = linear({255, 226, 122, 255}, {196, 128, 24, 255});
            s.outlines = {outline(3, {120, 70, 10, 255})};
            s.effects.bevel.enabled = true;
            s.effects.bevel.size = 8;
            s.effects.bevel.highlight = {255, 250, 220, 255};
            s.effects.bevel.shadow = {90, 50, 0, 255};
            out.push_back({"Gold bevel", s});
        }
        {
            Style s;
            s.fill = linear({245, 248, 252, 255}, {120, 130, 145, 255});
            s.fill.stops = {{0, {250, 252, 255, 255}},
                            {.48, {160, 170, 185, 255}},
                            {.52, {90, 98, 110, 255}},
                            {1, {200, 208, 220, 255}}};
            s.outlines = {outline(2, {70, 76, 88, 255})};
            s.effects.bevel.enabled = true;
            s.effects.bevel.size = 5;
            s.effects.bevel.profile = BevelProfile::Linear;
            out.push_back({"Chrome", s});
        }
        {
            Style s;
            s.fill = solid({255, 92, 120, 255});
            s.outlines = {outline(8, {255, 255, 255, 255}), outline(3, {40, 30, 50, 255})};
            s.effects.dropShadow = {true, 120, 4, 6, 0, {0, 0, 0, 255}, .35};
            out.push_back({"Sticker", s});
        }
        {
            Style s;
            s.fill = solid({80, 255, 230, 255});
            s.outlines = {outline(2, {220, 255, 250, 255}, OutlinePosition::Inside)};
            s.effects.outerGlow = {true, 14, .1, {40, 240, 220, 255}, .9};
            s.effects.innerGlow = {true, 6, 0, {255, 255, 255, 255}, .6};
            out.push_back({"Neon glow", s});
        }
        {
            Style s;
            s.fill = solid({42, 110, 220, 255});
            s.effects.dropShadow = {true, 135, 10, 0, 0, {20, 30, 60, 255}, .45};
            out.push_back({"Flat shadow", s});
        }
        {
            Style s;
            s.fill = linear({168, 162, 150, 255}, {120, 114, 104, 255});
            s.effects.bevel.enabled = true;
            s.effects.bevel.style = BevelStyle::Emboss;
            s.effects.bevel.size = 10;
            s.effects.bevel.softness = 2;
            s.effects.innerShadow = {true, 120, 2, 3, 0, {40, 34, 28, 255}, .5};
            out.push_back({"Embossed stone", s});
        }
        return out;
    }();
    return presets;
}
namespace {
struct Builder {
    DocumentData doc;
    std::uint64_t next{1};
    ObjectId add(Object o, ObjectId parent = {}) {
        o.id = {next++};
        o.parent = parent;
        doc.objects.push_back(std::make_shared<const Object>(std::move(o)));
        return doc.objects.back()->id;
    }
};
Object shapeObject(const char* name, ShapeGeometry g, double w, double h, Point at, Style style) {
    Object o;
    o.name = name;
    o.transform.position = at;
    o.style = std::move(style);
    o.data = ShapeData{w, h, std::move(g)};
    return o;
}
Object textObject(const char* name, const char* text, const QString& family, double size, Point at,
                  Style style, double arc = 0, double tracking = 40, int weight = 700, bool allCaps = true) {
    Object o;
    o.name = name;
    o.transform.position = at;
    o.style = std::move(style);
    TextData t;
    t.text = text;
    t.font.family = family.toStdString();
    t.size = size;
    t.weight = weight;
    t.arc = arc;
    t.tracking = tracking;
    t.allCaps = allCaps;
    o.data = t;
    return o;
}
Path polygonPath(std::initializer_list<Point> pts, double cornerRadius = 0,
                 CornerStyle cornerStyle = CornerStyle::Round) {
    Path p;
    SubPath s;
    for (const auto& pt : pts) {
        Node n = Node::corner(pt);
        n.radius = cornerRadius;
        n.cornerStyle = cornerStyle;
        s.nodes.push_back(n);
    }
    p.subpaths.push_back(std::move(s));
    return p;
}
Object pathObject(const char* name, Path p, Point at, Style style) {
    const Box b = bounds(resolvedCorners(p));
    return shapeObject(name, std::move(p), b.empty() ? 0 : b.width(), b.empty() ? 0 : b.height(), at,
                       std::move(style));
}
} // namespace
std::vector<StarterTemplate> starterTemplates(const QString& family) {
    std::vector<StarterTemplate> out;
    {
        // 1. Bastion crest: clean two-tone geometric fortress shield with bold chevron mark.
        Builder b;
        Style tile;
        tile.fill = solid({24, 34, 48, 255});
        b.add(shapeObject("Plaque", RectangleParams{{88, 88, 88, 88}}, 440, 440, {256, 256}, tile));
        Style crest;
        crest.fill = solid({244, 246, 250, 255});
        b.add(pathObject("Bastion outer",
                         polygonPath({{0, -116}, {104, -44}, {82, 82}, {0, 120}, {-82, 82}, {-104, -44}}, 10),
                         {256, 210}, crest));
        Style inner;
        inner.fill = solid({24, 34, 48, 255});
        b.add(pathObject("Bastion cutout",
                         polygonPath({{0, -68}, {60, -24}, {46, 52}, {0, 22}, {-46, 52}, {-60, -24}}, 4),
                         {256, 212}, inner));
        Style text;
        text.fill = solid({244, 246, 250, 255});
        b.add(textObject("Name", "BASTION", family, 48, {256, 404}, text, 0, 140, 700));
        b.doc.palette = {{"Obsidian", {24, 34, 48, 255}}, {"Ivory", {244, 246, 250, 255}}};
        out.push_back({"Bastion", std::move(b.doc)});
    }
    {
        // 2. Tower mark: monochrome architectural rook silhouette on a clean rounded plaque.
        Builder b;
        Style badge;
        badge.fill = solid({30, 41, 59, 255});
        b.add(shapeObject("Plaque", RectangleParams{{88, 88, 88, 88}}, 440, 440, {256, 256}, badge));
        Style stone;
        stone.fill = solid({244, 241, 234, 255});
        b.add(pathObject("Tower crown",
                         polygonPath({{-76, -126},
                                      {-40, -126},
                                      {-40, -96},
                                      {-18, -96},
                                      {-18, -126},
                                      {18, -126},
                                      {18, -96},
                                      {40, -96},
                                      {40, -126},
                                      {76, -126},
                                      {76, -68},
                                      {56, -48},
                                      {72, 92},
                                      {-72, 92},
                                      {-56, -48},
                                      {-76, -68}},
                                     3),
                         {256, 212}, stone));
        Style cutout;
        cutout.fill = solid({30, 41, 59, 255});
        b.add(shapeObject("Gate", RectangleParams{{22, 22, 0, 0}, false}, 44, 76, {256, 268}, cutout));
        Style text;
        text.fill = solid({244, 241, 234, 255});
        b.add(textObject("Name", "CITADEL", family, 48, {256, 404}, text, 0, 140, 700));
        b.doc.palette = {{"Slate", {30, 41, 59, 255}}, {"Stone", {244, 241, 234, 255}}};
        out.push_back({"Tower", std::move(b.doc)});
    }
    {
        // 3. Dairy & creamery: clean milk bottle silhouette with negative-space drop.
        Builder b;
        Style tile;
        tile.fill = solid({27, 77, 114, 255});
        b.add(shapeObject("Tile", RectangleParams{{88, 88, 88, 88}}, 440, 440, {256, 256}, tile));
        Style cream;
        cream.fill = solid({247, 245, 240, 255});
        b.add(pathObject("Bottle",
                         polygonPath({{-32, -122},
                                      {32, -122},
                                      {36, -76},
                                      {74, -24},
                                      {74, 114},
                                      {-74, 114},
                                      {-74, -24},
                                      {-36, -76}},
                                     18),
                         {256, 228}, cream));
        b.add(shapeObject("Bottle rim", RectangleParams{{9, 9, 9, 9}}, 86, 18, {256, 96}, cream));
        Style drop;
        drop.fill = solid({27, 77, 114, 255});
        SubPath dropSub;
        dropSub.nodes = {
            Node::corner({0, -44}),
            {{32, 12}, {18, -14}, {32, 30}, NodeType::Smooth},
            {{0, 42}, {18, 42}, {-18, 42}, NodeType::Smooth},
            {{-32, 12}, {-32, 30}, {-18, -14}, NodeType::Smooth},
        };
        Path dropPath;
        dropPath.subpaths.push_back(std::move(dropSub));
        b.add(pathObject("Drop", std::move(dropPath), {256, 244}, drop));
        Style text;
        text.fill = solid({247, 245, 240, 255});
        b.add(textObject("Brand", "DAIRY CO", family, 46, {256, 406}, text, 0, 120, 700));
        b.doc.palette = {{"Deep Blue", {27, 77, 114, 255}}, {"Cream", {247, 245, 240, 255}}};
        out.push_back({"Dairy", std::move(b.doc)});
    }
    {
        // 4. Fresh fruits / orchard: minimalist geometric citrus mark on deep botanical green.
        Builder b;
        Style bg;
        bg.fill = solid({29, 71, 52, 255});
        b.add(shapeObject("Tile", RectangleParams{{88, 88, 88, 88}}, 440, 440, {256, 256}, bg));
        Style fruit;
        fruit.fill = solid({238, 123, 68, 255});
        b.add(shapeObject("Fruit", EllipseParams{}, 184, 184, {256, 238}, fruit));
        Style leaf;
        leaf.fill = solid({246, 243, 234, 255});
        SubPath leafSub;
        leafSub.nodes = {
            Node::corner({-28, 16}),
            {{0, -14}, {-20, -6}, {12, -20}, NodeType::Smooth},
            Node::corner({34, -18}),
            {{6, 12}, {26, 4}, {-6, 18}, NodeType::Smooth},
        };
        Path leafPath;
        leafPath.subpaths.push_back(std::move(leafSub));
        b.add(pathObject("Leaf", std::move(leafPath), {274, 122}, leaf));
        Style text;
        text.fill = solid({246, 243, 234, 255});
        b.add(textObject("Brand", "ORCHARD", family, 48, {256, 404}, text, 0, 130, 700));
        b.doc.palette = {{"Forest", {29, 71, 52, 255}},
                         {"Tangerine", {238, 123, 68, 255}},
                         {"Cream", {246, 243, 234, 255}}};
        out.push_back({"Fruits", std::move(b.doc)});
    }
    {
        // 5. Books & publishing: clean open book pages with central spine gap.
        Builder b;
        Style crest;
        crest.fill = solid({28, 39, 54, 255});
        b.add(shapeObject("Emblem", RectangleParams{{88, 88, 88, 88}}, 440, 440, {256, 256}, crest));
        Style page;
        page.fill = solid({245, 242, 235, 255});
        b.add(pathObject("Left page", polygonPath({{-108, -72}, {-12, -52}, {-12, 76}, {-108, 56}}, 8),
                         {256, 214}, page));
        b.add(pathObject("Right page", polygonPath({{12, -52}, {108, -72}, {108, 56}, {12, 76}}, 8),
                         {256, 214}, page));
        Style text;
        text.fill = solid({245, 242, 235, 255});
        b.add(textObject("Brand", "ATELIER", family, 48, {256, 402}, text, 0, 140, 700));
        b.doc.palette = {{"Ink Navy", {28, 39, 54, 255}}, {"Paper", {245, 242, 235, 255}}};
        out.push_back({"Books", std::move(b.doc)});
    }
    {
        // 6. Coffee & roastery: clean geometric cup and steam mark.
        Builder b;
        Style bg;
        bg.fill = solid({56, 38, 30, 255});
        b.add(shapeObject("Plaque", PolygonParams{8, 24}, 440, 440, {256, 256}, bg));
        Style cup;
        cup.fill = solid({242, 233, 220, 255});
        b.add(pathObject("Cup", polygonPath({{-82, -48}, {82, -48}, {58, 72}, {-58, 72}}, 18), {244, 242},
                         cup));
        Style handle;
        handle.fill = {FillKind::None, {}};
        handle.outlines = {outline(16, {242, 233, 220, 255}, OutlinePosition::Center)};
        b.add(shapeObject("Handle", RectangleParams{{24, 24, 24, 24}}, 56, 68, {326, 232}, handle));
        Style saucer;
        saucer.fill = solid({214, 152, 92, 255});
        b.add(shapeObject("Saucer", RectangleParams{{8, 8, 8, 8}}, 190, 16, {256, 332}, saucer));
        b.add(shapeObject("Steam left", RectangleParams{{6, 6, 6, 6}}, 12, 40, {226, 148}, saucer));
        b.add(shapeObject("Steam right", RectangleParams{{6, 6, 6, 6}}, 12, 52, {262, 138}, saucer));
        Style text;
        text.fill = solid({242, 233, 220, 255});
        b.add(textObject("Brand", "ROASTERY", family, 46, {256, 402}, text, 0, 110, 700));
        b.doc.palette = {
            {"Espresso", {56, 38, 30, 255}}, {"Caramel", {214, 152, 92, 255}}, {"Oat", {242, 233, 220, 255}}};
        out.push_back({"Coffee", std::move(b.doc)});
    }
    {
        // 7. Bakery & grain: warm artisan wheat ear and diamond stamp.
        Builder b;
        Style stamp;
        stamp.fill = solid({194, 109, 58, 255});
        b.add(shapeObject("Stamp", RectangleParams{{56, 56, 56, 56}}, 430, 430, {256, 256}, stamp));
        Style wheat;
        wheat.fill = solid({248, 239, 222, 255});
        b.add(shapeObject("Stem", RectangleParams{{5, 5, 5, 5}}, 10, 210, {256, 226}, wheat));
        for (int i = 0; i < 3; ++i) {
            const double y = 148.0 + i * 52.0;
            b.add(pathObject("Grain left", polygonPath({{-12, 12}, {-54, -8}, {-44, -36}, {-8, -16}}, 10),
                             {256, y}, wheat));
            b.add(pathObject("Grain right", polygonPath({{12, 12}, {54, -8}, {44, -36}, {8, -16}}, 10),
                             {256, y}, wheat));
        }
        b.add(pathObject("Grain tip", polygonPath({{0, -48}, {18, -16}, {0, 12}, {-18, -16}}, 10), {256, 102},
                         wheat));
        Style text;
        text.fill = solid({248, 239, 222, 255});
        b.add(textObject("Brand", "MILL & HEARTH", family, 40, {256, 404}, text, 0, 90, 700));
        b.doc.palette = {{"Terracotta Crust", {194, 109, 58, 255}}, {"Flour", {248, 239, 222, 255}}};
        out.push_back({"Bakery", std::move(b.doc)});
    }
    {
        // 8. Cloud & SaaS app icon: crisp solid squircle with geometric lightning/prism mark.
        Builder b;
        Style tile;
        tile.fill = solid({23, 37, 84, 255});
        b.add(shapeObject("App tile", RectangleParams{{108, 108, 108, 108}}, 460, 460, {256, 256}, tile));
        Style accent;
        accent.fill = solid({56, 189, 248, 255});
        b.add(pathObject("Upper wing", polygonPath({{-90, 18}, {24, -110}, {24, -22}, {96, -22}}, 10),
                         {246, 236}, accent));
        Style primary;
        primary.fill = solid({248, 250, 252, 255});
        b.add(pathObject("Lower wing", polygonPath({{90, -18}, {-24, 110}, {-24, 22}, {-96, 22}}, 10),
                         {266, 276}, primary));
        b.doc.palette = {
            {"Midnight", {23, 37, 84, 255}}, {"Cyan", {56, 189, 248, 255}}, {"Ice", {248, 250, 252, 255}}};
        out.push_back({"Tech app icon", std::move(b.doc)});
    }
    {
        // 9. Modern wordmark: horizontal studio lockup with geometric hexagon monogram.
        Builder b;
        b.doc.artboard.width = 1024;
        b.doc.artboard.height = 384;
        Style card;
        card.fill = solid({247, 245, 240, 255});
        b.add(shapeObject("Card", RectangleParams{{48, 48, 48, 48}}, 960, 320, {512, 192}, card));
        Style hex;
        hex.fill = solid({24, 34, 48, 255});
        b.add(shapeObject("Hexagon", PolygonParams{6, 18}, 196, 196, {184, 192}, hex));
        Style chevron;
        chevron.fill = solid({45, 164, 132, 255});
        b.add(pathObject("Mark",
                         polygonPath({{-44, 18}, {0, -44}, {44, 18}, {22, 40}, {0, -6}, {-22, 40}}, 4),
                         {184, 192}, chevron));
        Style text;
        text.fill = solid({24, 34, 48, 255});
        b.add(textObject("Wordmark", "VERDANT", family, 112, {610, 192}, text, 0, 90, 800));
        b.doc.palette = {
            {"Ivory", {247, 245, 240, 255}}, {"Charcoal", {24, 34, 48, 255}}, {"Mint", {45, 164, 132, 255}}};
        out.push_back({"Studio wordmark", std::move(b.doc)});
    }
    {
        // 10. Wide store banner: clean solid architectural framing with Steam export preset.
        Builder b;
        b.doc.artboard.width = 920;
        b.doc.artboard.height = 430;
        b.doc.artboard.transparent = false;
        b.doc.artboard.background = {20, 26, 36, 255};
        Style frame;
        frame.fill = solid({28, 37, 50, 255});
        frame.outlines = {outline(4, {218, 165, 74, 255}, OutlinePosition::Inside)};
        b.add(shapeObject("Frame", RectangleParams{{20, 20, 20, 20}, true, CornerStyle::Chamfer}, 860, 370,
                          {460, 215}, frame));
        Style emblem;
        emblem.fill = solid({218, 165, 74, 255});
        b.add(pathObject("Crest", polygonPath({{0, -44}, {42, -12}, {26, 40}, {-26, 40}, {-42, -12}}, 6),
                         {460, 108}, emblem));
        Style title;
        title.fill = solid({244, 246, 250, 255});
        b.add(textObject("Title", "IRONHOLD", family, 106, {460, 232}, title, 0, 110, 800));
        Style tag;
        tag.fill = solid({218, 165, 74, 255});
        b.add(textObject("Subtitle", "TACTICAL STRATEGY", family, 30, {460, 326}, tag, 0, 180, 600));
        b.doc.palette = {{"Obsidian", {20, 26, 36, 255}},
                         {"Slate", {28, 37, 50, 255}},
                         {"Gold", {218, 165, 74, 255}},
                         {"Ivory", {244, 246, 250, 255}}};
        for (const auto& p : io::exportPresets())
            if (p.name.startsWith("Steam"))
                b.doc.exportSettings = p.apply(b.doc.exportSettings);
        b.doc.exportSettings.fit = ExportFit::Contain;
        b.doc.exportSettings.transparent = false;
        b.doc.exportSettings.background = b.doc.artboard.background;
        out.push_back({"Store banner", std::move(b.doc)});
    }
    return out;
}
} // namespace kantan::app

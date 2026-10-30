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
                  Style style, double arc = 0, double tracking = 0) {
    Object o;
    o.name = name;
    o.transform.position = at;
    o.style = std::move(style);
    TextData t;
    t.text = text;
    t.font.family = family.toStdString();
    t.size = size;
    t.weight = 800;
    t.arc = arc;
    t.tracking = tracking;
    t.allCaps = true;
    o.data = t;
    return o;
}
} // namespace
std::vector<StarterTemplate> starterTemplates(const QString& family) {
    std::vector<StarterTemplate> out;
    {
        Builder b;
        Style ring = defaultShapeStyle();
        ring.fill = solid({28, 52, 92, 255});
        ring.outlines = {outline(6, {242, 200, 90, 255})};
        b.add(shapeObject("Badge", simple(SimpleKind::Badge), 440, 440, {256, 256}, ring));
        Style inner;
        inner.fill = solid({242, 200, 90, 255});
        b.add(shapeObject("Inner circle", EllipseParams{}, 300, 300, {256, 256}, inner));
        Style text;
        text.fill = solid({28, 52, 92, 255});
        b.add(textObject("Name", "Kantan", family, 64, {256, 256}, text));
        Style arcText;
        arcText.fill = solid({242, 200, 90, 255});
        b.add(textObject("Arc text", "Est. 2026", family, 36, {256, 108}, arcText, .55, 80));
        out.push_back({"Round badge", std::move(b.doc)});
    }
    {
        Builder b;
        Style shield;
        shield.fill = linear({214, 54, 60, 255}, {140, 20, 30, 255});
        shield.outlines = {outline(8, {255, 255, 255, 255}), outline(4, {30, 30, 40, 255})};
        b.add(shapeObject("Shield", simple(SimpleKind::Shield), 340, 410, {256, 256}, shield));
        Style text;
        text.fill = solid({255, 255, 255, 255});
        text.effects.dropShadow = {true, 120, 4, 2, 0, {0, 0, 0, 255}, .4};
        b.add(textObject("Initials", "KS", family, 150, {256, 236}, text));
        out.push_back({"Shield + initials", std::move(b.doc)});
    }
    {
        Builder b;
        Style tile;
        tile.fill = linear({90, 120, 255, 255}, {150, 70, 230, 255}, 135);
        b.add(shapeObject("Icon tile", RectangleParams{{112, 112, 112, 112}}, 512, 512, {256, 256}, tile));
        Style mark;
        mark.fill = solid({255, 255, 255, 255});
        mark.effects.dropShadow = {true, 120, 8, 12, 0, {20, 0, 60, 255}, .35};
        b.add(shapeObject("Mark", StarParams{4, .38, 10}, 260, 260, {256, 256}, mark));
        out.push_back({"App icon squircle", std::move(b.doc)});
    }
    {
        Builder b;
        b.doc.artboard.width = 1024;
        b.doc.artboard.height = 384;
        Style dot;
        dot.fill = solid({255, 120, 60, 255});
        b.add(shapeObject("Symbol", EllipseParams{}, 180, 180, {170, 192}, dot));
        Style text;
        text.fill = solid({30, 34, 44, 255});
        auto word = textObject("Wordmark", "Wordmark", family, 140, {610, 192}, text);
        word.as<TextData>()->allCaps = false;
        b.add(std::move(word));
        out.push_back({"Wordmark", std::move(b.doc)});
    }
    {
        Builder b;
        Style ring;
        ring.fill = solid({20, 24, 32, 255});
        b.add(shapeObject("Ring", RingParams{.16, 0, 360}, 460, 460, {256, 256}, ring));
        Style star;
        star.fill = linear({255, 214, 90, 255}, {230, 140, 30, 255});
        star.effects.bevel.enabled = true;
        star.effects.bevel.size = 10;
        b.add(shapeObject("Star", StarParams{5, .45, 4}, 300, 300, {256, 262}, star));
        out.push_back({"Star emblem", std::move(b.doc)});
    }
    {
        Builder b;
        Style burst;
        burst.fill = solid({255, 214, 0, 255});
        burst.outlines = {outline(6, {30, 30, 30, 255})};
        b.add(shapeObject("Burst", simple(SimpleKind::Burst), 470, 470, {256, 256}, burst));
        Style text;
        text.fill = solid({30, 30, 30, 255});
        b.add(textObject("Label", "New!", family, 120, {256, 256}, text));
        out.push_back({"Burst label", std::move(b.doc)});
    }
    {
        Builder b;
        Style hex;
        hex.fill = linear({40, 44, 60, 255}, {16, 18, 26, 255});
        hex.outlines = {outline(10, {0, 210, 255, 255}), outline(4, {10, 12, 18, 255})};
        b.add(shapeObject("Crest", PolygonParams{6, 18}, 440, 460, {256, 256}, hex));
        Style star;
        star.fill = linear({120, 240, 255, 255}, {0, 150, 220, 255});
        star.effects.outerGlow = {true, 12, .1, {0, 210, 255, 255}, .6};
        b.add(shapeObject("Star", StarParams{5, .45, 3}, 150, 150, {256, 170}, star));
        Style text;
        text.fill = solid({255, 255, 255, 255});
        text.outlines = {outline(4, {10, 12, 18, 255})};
        b.add(textObject("Team name", "Team", family, 96, {256, 320}, text, 0, 40));
        b.doc.palette = {
            {"Night", {16, 18, 26, 255}}, {"Cyan", {0, 210, 255, 255}}, {"White", {255, 255, 255, 255}}};
        out.push_back({"Esports crest", std::move(b.doc)});
    }
    {
        Builder b;
        Style ring;
        ring.fill = solid({246, 241, 230, 255});
        ring.outlines = {outline(6, {30, 64, 60, 255}, OutlinePosition::Inside)};
        b.add(shapeObject("Disc", EllipseParams{}, 440, 440, {256, 256}, ring));
        Style thin;
        thin.fill = solid({30, 64, 60, 255});
        b.add(shapeObject("Inner line", RingParams{.012, 0, 360}, 380, 380, {256, 256}, thin));
        Style text;
        text.fill = solid({30, 64, 60, 255});
        auto letters = textObject("Monogram", "AK", family, 170, {256, 262}, text, 0, -20);
        letters.as<TextData>()->weight = 400;
        b.add(std::move(letters));
        b.doc.palette = {{"Cream", {246, 241, 230, 255}}, {"Forest", {30, 64, 60, 255}}};
        out.push_back({"Monogram", std::move(b.doc)});
    }
    {
        Builder b;
        b.doc.artboard.background = {32, 18, 54, 255};
        b.doc.artboard.transparent = false;
        Style sun;
        sun.fill = linear({255, 214, 90, 255}, {255, 70, 140, 255}, 90);
        b.add(shapeObject("Sun", EllipseParams{}, 340, 340, {256, 230}, sun));
        Style stripe;
        stripe.fill = solid({32, 18, 54, 255});
        for (int i = 0; i < 4; ++i)
            b.add(shapeObject("Stripe", RectangleParams{}, 360, 6.0 + i * 5, {256, 260.0 + i * 34}, stripe));
        Style text;
        text.fill = solid({90, 230, 255, 255});
        text.effects.outerGlow = {true, 10, .1, {90, 230, 255, 255}, .7};
        b.add(textObject("Title", "Retro", family, 92, {256, 440}, text, 0, 30));
        b.doc.palette = {{"Night", {32, 18, 54, 255}},
                         {"Gold", {255, 214, 90, 255}},
                         {"Pink", {255, 70, 140, 255}},
                         {"Cyan", {90, 230, 255, 255}}};
        out.push_back({"Retro sunset", std::move(b.doc)});
    }
    {
        // Wide store art: opens with the Steam export preset already chosen.
        Builder b;
        b.doc.artboard.width = 920;
        b.doc.artboard.height = 430;
        b.doc.artboard.transparent = false;
        b.doc.artboard.background = {18, 26, 40, 255};
        Style glow;
        glow.fill = linear({44, 96, 170, 255}, {14, 20, 34, 255}, 90);
        b.add(shapeObject("Backdrop", RectangleParams{}, 920, 430, {460, 215}, glow));
        Style title;
        title.fill = linear({255, 255, 255, 255}, {170, 210, 255, 255});
        title.effects.dropShadow = {true, 90, 6, 10, 0, {0, 0, 0, 255}, .55};
        b.add(textObject("Game title", "Game Title", family, 120, {460, 200}, title, 0, 10));
        Style tag;
        tag.fill = solid({255, 190, 70, 255});
        b.add(textObject("Tagline", "Coming soon", family, 36, {460, 300}, tag, 0, 120));
        b.doc.palette = {
            {"Deep", {18, 26, 40, 255}}, {"Blue", {40, 90, 160, 255}}, {"Amber", {255, 190, 70, 255}}};
        for (const auto& p : io::exportPresets())
            if (p.name.startsWith("Steam"))
                b.doc.exportSettings = p.apply(b.doc.exportSettings);
        b.doc.exportSettings.fit = ExportFit::Cover;
        b.doc.exportSettings.transparent = false;
        b.doc.exportSettings.background = b.doc.artboard.background;
        out.push_back({"Steam capsule", std::move(b.doc)});
    }
    return out;
}
} // namespace kantan::app

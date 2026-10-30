#include "editing/Editor.h"
#include "editing/Transforms.h"
#include "effects/Effects.h"
#include "raster/Rasterizer.h"
#include "render/ImageSampling.h"
#include "render/Render.h"
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace kantan;
namespace {
void check(bool yes, const char* message) {
    if (!yes)
        throw std::runtime_error(message);
}
void ok(Result r) {
    if (!r)
        throw std::runtime_error(r.message);
}
auto budget() {
    return std::make_shared<MemoryBudget>();
}
Polygons rect(double x0, double y0, double x1, double y1) {
    return {{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}};
}
void rasterizerContracts() {
    const auto b = budget();
    {
        auto c = rasterize(rect(1, 1, 3, 3), FillRule::NonZero, 4, 4, b);
        check(c.at(1, 1) == 255 && c.at(2, 2) == 255 && c.at(0, 0) == 0 && c.at(3, 3) == 0,
              "Aligned square is exact");
        c = rasterize(rect(0, 0, .5, 1), FillRule::NonZero, 2, 1, b);
        check(c.at(0, 0) == 128 && c.at(1, 0) == 0, "Half-covered pixel has half coverage");
        c = rasterize(rect(.25, 0, .5, 1), FillRule::NonZero, 1, 1, b);
        check(c.at(0, 0) == 64, "Sub-pixel sliver coverage is its area");
        c = rasterize({{{0, 0}, {4, 0}, {0, 4}}}, FillRule::NonZero, 4, 4, b);
        int total = 0;
        for (auto v : c.values)
            total += v;
        check(std::abs(total - 8 * 255) <= 4, "Triangle coverage integrates to its area");
        // Orientation does not matter; overlapping same-direction squares stay opaque under non-zero.
        Polygons reversed{{{3, 1}, {1, 1}, {1, 3}, {3, 3}}};
        check(rasterize(reversed, FillRule::NonZero, 4, 4, b).at(2, 2) == 255,
              "Clockwise and anticlockwise fill");
        Polygons twice{rect(1, 1, 3, 3)[0], rect(1, 1, 3, 3)[0]};
        check(rasterize(twice, FillRule::NonZero, 4, 4, b).at(2, 2) == 255,
              "Non-zero clamps overlapping cover");
        check(rasterize(twice, FillRule::EvenOdd, 4, 4, b).at(2, 2) == 0, "Even-odd cancels double cover");
        // Geometry beyond the raster on every side still fills interior pixels correctly.
        c = rasterize(rect(-10, -10, 20, 20), FillRule::NonZero, 4, 4, b);
        check(c.at(0, 0) == 255 && c.at(3, 3) == 255,
              "Off-raster geometry is clipped without losing winding");
        c = rasterize({{{-5, 0}, {2, 0}, {2, 4}}}, FillRule::NonZero, 4, 4, b);
        check(c.at(3, 3) == 0 && c.at(1, 0) > 0, "Edges crossing x = 0 are split, not clamped wholesale");
    }
    check(b->used() == 0, "Rasterizer releases its accounting");
}
Object square(double size, Point at) {
    Object o;
    o.transform.position = at;
    o.data = ShapeData{size, size, RectangleParams{}};
    o.style.fill = {FillKind::Solid, {200, 30, 30, 255}};
    return o;
}
DocumentData single(Object o, double board = 64) {
    DocumentData doc;
    doc.artboard.width = doc.artboard.height = board;
    o.id = {1};
    doc.objects.push_back(std::make_shared<const Object>(std::move(o)));
    return doc;
}
Image draw(const DocumentData& doc, Coord size, double exportSize = 0) {
    RenderRequest request;
    request.width = request.height = size;
    request.toPixels = fitTransform({0, 0, doc.artboard.width, doc.artboard.height}, size, size, 0);
    request.exportSize = exportSize;
    return render(doc, request, {}, std::make_shared<MemoryBudget>());
}
std::size_t covered(const Image& image) {
    std::size_t total = 0;
    for (auto p : image.pixels)
        total += p.a;
    return (total + 127) / 255;
}
void outlineContracts() {
    auto o = square(20, {32, 32});
    const auto plain = covered(draw(single(o), 64));
    check(plain == 400, "Filled square covers its area");
    o.style.outlines = {
        {true, 4, {FillKind::Solid, {255, 255, 255, 255}}, OutlinePosition::Outside, JoinStyle::Miter}};
    check(covered(draw(single(o), 64)) == 28 * 28, "Outside outline adds its width outside");
    o.style.outlines.push_back(
        {true, 2, {FillKind::Solid, {0, 0, 0, 255}}, OutlinePosition::Outside, JoinStyle::Miter});
    const auto stacked = draw(single(o), 64);
    check(covered(stacked) == 32 * 32, "Stacked outside outlines accumulate");
    check(stacked.at(17, 32) == Pixel::rgba(0, 0, 0) && stacked.at(19, 32) == Pixel::rgba(255, 255, 255),
          "Later outside outlines sit outside earlier ones");
    o.style.outlines = {
        {true, 3, {FillKind::Solid, {0, 0, 255, 255}}, OutlinePosition::Inside, JoinStyle::Miter}};
    const auto inside = draw(single(o), 64);
    check(covered(inside) == 400 && inside.at(23, 32) == Pixel::rgba(0, 0, 255) &&
              inside.at(32, 32) == Pixel::rgba(200, 30, 30),
          "Inside outline stays within the shape");
    o.style.outlines = {
        {true, 4, {FillKind::Solid, {0, 255, 0, 255}}, OutlinePosition::Center, JoinStyle::Miter}};
    check(covered(draw(single(o), 64)) == 24 * 24, "Centre outline straddles the edge");
}
void gradientAndVisibility() {
    Paint p{FillKind::Linear};
    p.stops = {{0, {0, 0, 0, 255}}, {1, {255, 255, 255, 255}}};
    p.angle = 0;
    check(shade(p, {0, .5}) == Pixel::rgba(0, 0, 0) && shade(p, {1, .5}) == Pixel::rgba(255, 255, 255),
          "Linear gradient spans the box");
    p.stops = {{0, {255, 0, 0, 0}}, {1, {255, 0, 0, 255}}};
    const auto mid = shade(p, {.5, .5});
    check(mid.r == mid.a, "Transparent stops interpolate premultiplied without darkening");
    auto o = square(20, {32, 32});
    o.maxExportSize = 32;
    check(covered(draw(single(o), 64, 64)) == 0, "Objects above their max export size are hidden");
    check(covered(draw(single(o), 64, 32)) > 0, "Objects within their size range are drawn");
    check(covered(draw(single(o), 64, 0)) > 0, "Canvas renders ignore size visibility");
}
double meanDifference(const Image& a, const Image& b) {
    double total = 0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i)
        total += std::abs(a.pixels[i].a - b.pixels[i].a) + std::abs(a.pixels[i].r - b.pixels[i].r);
    return total / static_cast<double>(a.pixels.size()) / 2;
}
Image boxDown(const Image& src, Coord factor) {
    auto out = Image::make(src.width / factor, src.height / factor, budget());
    for (Coord y = 0; y < out.height; ++y)
        for (Coord x = 0; x < out.width; ++x) {
            double r = 0, g = 0, b = 0, a = 0;
            for (Coord dy = 0; dy < factor; ++dy)
                for (Coord dx = 0; dx < factor; ++dx) {
                    const auto p = src.at(x * factor + dx, y * factor + dy);
                    r += p.r;
                    g += p.g;
                    b += p.b;
                    a += p.a;
                }
            const double n = static_cast<double>(factor * factor);
            out.ref(x, y) = {
                static_cast<std::uint8_t>(std::lround(r / n)), static_cast<std::uint8_t>(std::lround(g / n)),
                static_cast<std::uint8_t>(std::lround(b / n)), static_cast<std::uint8_t>(std::lround(a / n))};
        }
    return out;
}
void effectContracts() {
    const auto b = budget();
    // Distance transform: exact for an axis-aligned edge.
    auto alpha = FloatPlane::make(16, 1, b);
    for (Coord x = 0; x < 8; ++x)
        alpha.values[static_cast<std::size_t>(x)] = 1;
    const auto inner = distanceToEdge(alpha, true, b);
    check(std::abs(inner.at(7, 0) - .5f) < 1e-4 && std::abs(inner.at(4, 0) - 3.5f) < 1e-4,
          "Inside distance measures to the edge");
    // Blur preserves total mass.
    auto plane = FloatPlane::make(64, 64, b);
    plane.values[plane.index(32, 32)] = 1;
    blur(plane, 4, b);
    double mass = 0;
    for (auto v : plane.values)
        mass += v;
    check(std::abs(mass - 1) < 1e-3, "Blur conserves mass");
    // Zero-size effects are a no-op.
    auto o = square(20, {32, 32});
    const auto plain = draw(single(o), 64);
    o.style.effects.bevel.enabled = true;
    o.style.effects.bevel.size = 0;
    o.style.effects.dropShadow.enabled = true;
    o.style.effects.dropShadow.distance = 0;
    o.style.effects.dropShadow.blur = 0;
    check(draw(single(o), 64).pixels == plain.pixels, "Zero-size effects change nothing");
    // Bevel on a circle lit from the top is left-right symmetric and brighter at the top.
    Object circle;
    circle.transform.position = {32, 32};
    circle.data = ShapeData{40, 40, EllipseParams{}};
    circle.style.fill = {FillKind::Solid, {120, 120, 120, 255}};
    circle.style.effects.bevel.enabled = true;
    circle.style.effects.bevel.size = 8;
    circle.style.effects.bevel.lightAngle = 90;
    circle.style.effects.bevel.altitude = 30;
    const auto bevel = draw(single(circle), 64);
    for (Coord y = 14; y < 50; y += 3)
        for (Coord d = 2; d < 18; d += 3) {
            const auto l = bevel.at(31 - d, y), r = bevel.at(32 + d, y);
            check(std::abs(l.r - r.r) <= 2 && std::abs(l.a - r.a) <= 2,
                  "Bevel is symmetric about the light axis");
        }
    check(bevel.at(32, 15).r > 140 && bevel.at(32, 49).r < 100, "Bevel lights the top and shades the bottom");
    // Scale invariance: a 512 render box-downsampled to 64 matches a direct 64 render.
    auto styled = square(24, {32, 32});
    styled.style.outlines = {{true, 3, {FillKind::Solid, {255, 255, 255, 255}}, OutlinePosition::Outside}};
    styled.style.effects.dropShadow.enabled = true;
    styled.style.effects.dropShadow.distance = 3;
    styled.style.effects.dropShadow.blur = 4;
    const auto large = boxDown(draw(single(styled), 512), 8);
    const auto small = draw(single(styled), 64);
    check(meanDifference(large, small) < 3, "Effects scale with output size");
    // Bounded memory on a 4096 px export with effects; everything is released afterwards.
    auto limited = std::make_shared<MemoryBudget>(768U * 1024U * 1024U);
    {
        RenderRequest request;
        request.width = request.height = 4096;
        request.toPixels = fitTransform({0, 0, 64, 64}, 4096, 4096, 0);
        const auto big = render(single(styled), request, {}, limited);
        check(big.at(2048, 2048).a == 255, "Large export renders");
    }
    check(limited->used() == 0, "Large export releases all scratch memory");
}
void booleanRenderContracts() {
    Editor e(Artboard{64, 64});
    auto a = square(30, {26, 32});
    auto c = square(30, {38, 32});
    ok(e.add({a, c}));
    const auto ids = e.created();
    ok(e.group(ids, ObjectKind::Boolean, BooleanOp::Subtract));
    const auto boolean = e.created()[0];
    const auto live = draw(*e.snapshot(), 64);
    check(covered(live) == 12 * 30, "Live subtract renders the difference");
    const auto flat = flattenedBoolean(*e.snapshot(), boolean, {});
    ok(e.replaceSubtrees("Flatten", {*flat}));
    check(meanDifference(draw(*e.snapshot(), 64), live) < .5, "Flatten equals the live render");
    // Clip groups mask content by the bottom shape and do not draw the clip shape itself.
    Editor clip(Artboard{64, 64});
    auto mask = square(20, {32, 32});
    auto content = square(60, {32, 32});
    content.style.fill.color = {0, 200, 0, 255};
    ok(clip.add({mask, content}));
    ok(clip.group(clip.created(), ObjectKind::Clip));
    const auto clipped = draw(*clip.snapshot(), 64);
    check(covered(clipped) == 400 && clipped.at(32, 32) == Pixel::rgba(0, 200, 0),
          "Clip masks by the bottom shape");
    // Radial repeat draws rotated copies.
    auto petal = square(8, {32, 12});
    petal.repeat = {4, {0, 20}};
    const auto repeated = draw(single(petal), 64);
    check(covered(repeated) == 4 * 64 && repeated.at(52, 32).a == 255 && repeated.at(12, 32).a == 255,
          "Radial repeat places copies around the pivot");
}
void openPathContracts() {
    Object line;
    Path p;
    p.subpaths.push_back({{Node::corner({-20, 0}), Node::corner({20, 0})}, false});
    line.transform.position = {32, 32};
    line.data = ShapeData{40, 0, p};
    line.style.fill.kind = FillKind::Solid; // ignored for open paths
    line.style.outlines = {
        {true, 4, {FillKind::Solid, {0, 0, 0, 255}}, OutlinePosition::Center, JoinStyle::Round}};
    const auto doc = single(line);
    const auto image = draw(doc, 64);
    check(image.at(32, 32).a == 255 && image.at(32, 36).a == 0, "Open paths render as centred strokes");
    check(std::abs(static_cast<double>(covered(image)) - (40 * 4 + std::numbers::pi * 4)) < 6,
          "Stroke has round caps");
    check(hitTest(doc, {30, 33}, 0, {}) == ObjectId{1}, "Open strokes are hit-testable");
    check(!objectBounds(doc, *doc.objects[0], {}).empty(), "Open strokes have bounds");
}
void imageContracts() {
    const auto b = budget();
    auto src = Image::make(200, 200, b);
    for (Coord y = 0; y < 200; ++y)
        for (Coord x = 0; x < 200; ++x)
            src.ref(x, y) = ((x / 2 + y / 2) % 2) ? Pixel::rgba(255, 255, 255) : Pixel::rgba(0, 0, 0);
    auto ref = std::make_shared<const Image>(std::move(src));
    DocumentData doc;
    doc.artboard = {32, 32};
    doc.images["img"] = {ref, {}, "png", "checker"};
    Object o;
    o.id = {1};
    o.transform.position = {16, 16};
    o.data = ImageData{"img", 32, 32};
    doc.objects.push_back(std::make_shared<const Object>(o));
    ok(validateDocument(doc));
    // A fine checker downscaled ~6x must average to grey rather than alias.
    const auto out = draw(doc, 32);
    for (Coord y = 4; y < 28; y += 5)
        for (Coord x = 4; x < 28; x += 5)
            check(std::abs(out.at(x, y).r - 128) < 24, "Mip-mapped downscaling does not alias");
    // Colour key removes the key colour with tolerance.
    ImageAdjust adjust;
    adjust.colorKey = true;
    adjust.keyColor = {255, 255, 255, 255};
    check(adjustPixel(Pixel::rgba(255, 255, 255), adjust).a == 0, "Colour key removes the key colour");
    check(adjustPixel(Pixel::rgba(250, 250, 250), adjust).a == 0, "Colour key honours tolerance");
    check(adjustPixel(Pixel::rgba(20, 30, 40), adjust).a == 255, "Other colours stay opaque");
    ImageAdjust none;
    check(adjustPixel(Pixel::rgba(20, 30, 40, 128), none) == Pixel::rgba(20, 30, 40, 128),
          "No adjustment is identity");
}
} // namespace
int main() {
    try {
        rasterizerContracts();
        outlineContracts();
        gradientAndVisibility();
        effectContracts();
        booleanRenderContracts();
        openPathContracts();
        imageContracts();
        std::cout << "All render contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

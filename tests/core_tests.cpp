#include "boolean/Boolean.h"
#include "editing/Colors.h"
#include "editing/Editor.h"
#include "editing/Transforms.h"
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
bool near(double a, double b, double tolerance = 1e-6) {
    return std::abs(a - b) <= tolerance;
}
Object shape(ShapeGeometry geometry, double w, double h, Point at) {
    Object o;
    o.name = "Shape";
    o.transform.position = at;
    o.data = ShapeData{w, h, std::move(geometry)};
    return o;
}
double polygonsArea(const Polygons& polygons) {
    double a = 0;
    for (const auto& p : polygons)
        a += signedArea(p);
    return std::abs(a);
}
void geometryContracts() {
    // Affine composition and inversion round-trip.
    const Affine m = Affine::translation(10, -4) * Affine::rotation(30) * Affine::scaling(2, -3);
    const Point p{3.5, -1.25};
    const Point back = m.inverted().map(m.map(p));
    check(near(back.x, p.x) && near(back.y, p.y), "Affine inverse round-trips");
    check(decompose(m).has_value(), "Rotation/scale matrices decompose");
    const auto rebuilt = decompose(m)->matrix();
    check(near(rebuilt.a, m.a) && near(rebuilt.b, m.b) && near(rebuilt.c, m.c) && near(rebuilt.d, m.d),
          "Decomposition rebuilds the same linear map");
    check(!decompose(Affine{1, 0, .5, 1, 0, 0}), "Skewed matrices are rejected");
    // Ellipse flattening stays within tolerance of the true circle.
    const auto circle = flatten(ellipsePath({}, 100, 100), .05);
    double worst = 0;
    for (auto q : circle[0])
        worst = std::max(worst, std::abs(length(q) - 100));
    check(worst < .06, "Cubic circle flattening honours the tolerance");
    check(near(polygonsArea(circle), std::numbers::pi * 100 * 100, 40), "Circle area is accurate");
}
void presetContracts() {
    // Every preset produces a closed, non-empty path that fits its box.
    std::vector<ShapeGeometry> presets{RectangleParams{{20, 20, 20, 20}},
                                       EllipseParams{},
                                       PolygonParams{6, 8},
                                       StarParams{5, .45, 4},
                                       RingParams{},
                                       RingParams{.3, 0, 270},
                                       GearParams{}};
    for (const auto& g : presets) {
        if (std::holds_alternative<Path>(g))
            continue;
        const auto path = shapePath(g, 200, 120);
        check(!path.empty(), "Preset produces geometry");
        for (const auto& s : path.subpaths)
            check(s.closed, "Preset subpaths are closed");
        const auto polys = flattenClosed(path, .05);
        const Box b = bounds(polys);
        check(b.x0 >= -100.01 && b.x1 <= 100.01 && b.y0 >= -60.01 && b.y1 <= 60.01, "Preset fits its box");
        check(polygonsArea(normalized({polys, path.rule})) > 100, "Preset encloses area");
    }
    for (int k = 0; k < 7; ++k) {
        SimpleParams s{static_cast<SimpleKind>(k)};
        const auto ranges = simpleParamRanges(s.kind);
        for (std::size_t i = 0; i < 3; ++i)
            s.values[i] = ranges[i].defaultValue;
        const auto polys = normalized({flattenClosed(shapePath(s, 200, 200), .05), FillRule::NonZero});
        check(polygonsArea(polys) > 1000, "Decorative presets enclose area");
    }
    // Rounded rectangle area: w*h - (4 - pi) r^2.
    const auto rr = flattenClosed(shapePath(RectangleParams{{20, 20, 20, 20}}, 200, 100), .01);
    check(near(polygonsArea(rr), 200 * 100 - (4 - std::numbers::pi) * 400, 2), "Rounded rectangle area");
    // Ring hole: outer minus inner area.
    const auto ring =
        normalized({flattenClosed(shapePath(RingParams{.5, 0, 360}, 200, 200), .01), FillRule::NonZero});
    check(near(polygonsArea(ring), std::numbers::pi * (100 * 100 - 50 * 50), 30), "Ring has a hole");
    check(!validateShape(PolygonParams{2, 0}, 10, 10), "Polygon side count is validated");
    check(!validateShape(StarParams{5, 0, 0}, 10, 10), "Star inner radius is validated");
}
void booleanContracts() {
    const Polygons a{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    const Polygons b{{{5, 0}, {15, 0}, {15, 10}, {5, 10}}};
    const Polygons far{{{100, 100}, {110, 100}, {110, 110}, {100, 110}}};
    check(near(polygonsArea(applyBoolean(BooleanOp::Union, {{a}, {b}})), 150, 1e-3),
          "Union of overlapping boxes");
    check(near(polygonsArea(applyBoolean(BooleanOp::Subtract, {{a}, {b}})), 50, 1e-3),
          "Subtract keeps the rest");
    check(near(polygonsArea(applyBoolean(BooleanOp::Intersect, {{a}, {b}})), 50, 1e-3),
          "Intersect keeps overlap");
    check(near(polygonsArea(applyBoolean(BooleanOp::Exclude, {{a}, {b}})), 100, 1e-3),
          "Exclude drops overlap");
    // Empty intersections are explicit empty results, not failures.
    check(applyBoolean(BooleanOp::Intersect, {{a}, {far}}).empty(), "Disjoint intersect is empty");
    // Even-odd self-overlap differs from non-zero.
    const Polygons twice{a[0], a[0]};
    check(near(polygonsArea(normalized({twice, FillRule::NonZero})), 100, 1e-3),
          "Non-zero overlap counts once");
    check(normalized({twice, FillRule::EvenOdd}).empty(), "Even-odd double cover cancels");
    // Offsets and bands.
    check(near(polygonsArea(offset(a, 2, JoinStyle::Miter, 4, .01)), 14 * 14, .01),
          "Mitered offset grows square");
    check(near(polygonsArea(band(a, 0, 2, JoinStyle::Miter, 4, .01)), 14 * 14 - 100, .01),
          "Outside band area");
    check(near(polygonsArea(band(a, -2, 0, JoinStyle::Miter, 4, .01)), 100 - 36, .01), "Inside band area");
    check(offset(a, -6, JoinStyle::Miter, 4, .01).empty(), "Over-shrink yields empty area");
}
void editorContracts() {
    Editor e;
    ok(e.add({shape(RectangleParams{}, 100, 100, {100, 100})}));
    const auto a = e.created().at(0);
    ok(e.add({shape(EllipseParams{}, 50, 50, {300, 300})}));
    const auto b = e.created().at(0);
    check(e.snapshot()->objects.size() == 2 && e.canUndo(), "Objects are added with undo");
    check(e.selection() == std::vector<ObjectId>{b}, "New objects become the selection");
    const auto saved = e.snapshot()->state;
    e.markSaved(saved);
    check(!e.dirty(), "Saved state is clean");
    // Gesture lifecycle: previews do not touch the committed snapshot; cancel discards.
    ok(e.begin("Move"));
    ok(e.preview(translated(*e.snapshot(), {a}, {10, 0})));
    check(e.preview()->find(a)->transform.position.x == 110, "Preview shows the drag");
    check(e.snapshot()->find(a)->transform.position.x == 100,
          "Committed snapshot is untouched during a drag");
    check(!e.add({shape(EllipseParams{}, 5, 5, {})}), "Commands are refused while a gesture is open");
    e.cancel();
    check(e.preview()->find(a)->transform.position.x == 100 && !e.dirty(),
          "Cancel restores the committed state");
    ok(e.begin("Move"));
    ok(e.preview(translated(*e.snapshot(), {a}, {10, 0})));
    ok(e.preview(translated(*e.snapshot(), {a}, {25, 5})));
    ok(e.apply());
    check(e.snapshot()->find(a)->transform.position.x == 125 && e.dirty(), "Apply publishes one step");
    ok(e.undo());
    check(e.snapshot()->find(a)->transform.position.x == 100 && !e.dirty(),
          "Undo returns to the saved identity");
    ok(e.redo());
    // A no-op gesture creates no history.
    const auto steps = e.undoLabel();
    ok(e.begin("Nothing"));
    check(e.apply().status == Status::NoChange && e.undoLabel() == steps, "No-op gestures add no undo entry");
    // Locked objects refuse geometry edits but accept unlocking.
    Object locked = *e.snapshot()->find(a);
    locked.locked = true;
    ok(e.update("Lock", {locked}));
    auto moved = translated(*e.snapshot(), {a}, {5, 5});
    check(!e.update("Move", moved), "Locked objects cannot move");
    check(!e.remove({a}), "Locked objects cannot be deleted");
    locked.locked = false;
    ok(e.update("Unlock", {locked}));
    // Grouping keeps world positions and depth-first order; ungroup restores.
    ok(e.group({a, b}, ObjectKind::Group));
    const auto g = e.created().at(0);
    check(e.snapshot()->find(a)->parent == g && e.snapshot()->indexOf(g) == 0, "Group contains its members");
    ok(validateDocument(*e.snapshot()));
    Object group = *e.snapshot()->find(g);
    group.transform.position = {50, 0};
    group.transform.rotation = 90;
    ok(e.update("Rotate group", {group}));
    const Point world = e.snapshot()->worldMatrix(a).map({});
    ok(e.ungroup({g}));
    const Point after = e.snapshot()->worldMatrix(a).map({});
    check(near(world.x, after.x) && near(world.y, after.y) && !e.snapshot()->find(g),
          "Ungroup composes transforms without moving children");
    check(near(e.snapshot()->find(a)->transform.rotation, 90), "Ungroup keeps the inherited rotation");
    // Booleans need two operands; images are not allowed.
    check(!e.group({a}, ObjectKind::Boolean), "Booleans need at least two objects");
    ok(e.group({a, b}, ObjectKind::Boolean, BooleanOp::Subtract));
    const auto boolean = e.created().at(0);
    const auto flat = flattenedBoolean(*e.snapshot(), boolean, {});
    check(flat && flat->kind() == ObjectKind::Shape, "Flatten boolean produces a shape");
    ok(e.replaceSubtrees("Flatten", {*flat}));
    check(e.snapshot()->objects.size() == 1, "Flatten replaces the boolean subtree");
    ok(e.undo());
    check(e.snapshot()->objects.size() == 3, "Undo restores the live boolean");
    // Duplicates get fresh ids, stay above originals, and offset.
    ok(e.duplicate({boolean}, {10, 10}));
    check(e.snapshot()->objects.size() == 6, "Duplicating a boolean duplicates its subtree");
    ok(validateDocument(*e.snapshot()));
    // Arrange.
    const auto copy = e.created().at(0);
    ok(e.arrange({copy}, Arrange::SendToBack));
    check(e.snapshot()->objects.front()->id == copy, "Send to back moves the subtree first");
    check(e.arrange({copy}, Arrange::SendToBack).status == Status::NoChange, "Repeated arrange is a no-op");
}
void documentValidation() {
    DocumentData doc;
    Object a = shape(EllipseParams{}, 10, 10, {});
    a.id = {1};
    Object b = a;
    doc.objects = {std::make_shared<const Object>(a), std::make_shared<const Object>(b)};
    check(!validateDocument(doc), "Duplicate ids are rejected");
    Object g;
    g.id = {2};
    g.parent = {3};
    g.data = GroupData{};
    Object h = g;
    h.id = {3};
    h.parent = {2};
    doc.objects = {std::make_shared<const Object>(g), std::make_shared<const Object>(h)};
    check(!validateDocument(doc), "Parent cycles are rejected");
    Object child = a;
    child.parent = {1};
    child.id = {4};
    doc.objects = {std::make_shared<const Object>(a), std::make_shared<const Object>(child)};
    check(!validateDocument(doc), "Leaves cannot have children");
    Object img;
    img.id = {5};
    img.data = ImageData{"missing", 10, 10};
    doc.objects = {std::make_shared<const Object>(img)};
    check(!validateDocument(doc), "Missing image members are rejected");
    Object bad = a;
    bad.style.outlines.resize(4);
    doc.objects = {std::make_shared<const Object>(bad)};
    check(!validateDocument(doc), "More than three outlines are rejected");
}
void resizeSnapContracts() {
    DocumentData doc;
    SnapSettings settings;
    auto result = snapResize(doc, {200, 200}, {100, 100}, {510, 509}, true, true, false, {}, settings, 6, {});
    check(near(100 + result.sx * 100, 512) && near(100 + result.sy * 100, 512),
          "Resize reaches canvas edges exactly");
    check(result.hint.kinds[0] == SnapKind::Artboard && result.hint.kinds[1] == SnapKind::Artboard,
          "Resize reports canvas targets");
    const auto previous = result.hint;
    result = snapResize(doc, {200, 200}, {100, 100}, {504, 200}, true, false, false, {}, settings, 6, {},
                        &previous);
    check(near(100 + result.sx * 100, 512), "Resize holds a captured edge within the release tolerance");
    result = snapResize(doc, {200, 200}, {100, 100}, {501, 200}, true, false, false, {}, settings, 6, {},
                        &previous);
    check(near(100 + result.sx * 100, 501), "Resize releases an edge beyond the release tolerance");
    result = snapResize(doc, {200, 150}, {100, 100}, {510, 249}, true, true, true, {}, settings, 6, {});
    check(near(std::abs(result.sx), std::abs(result.sy)), "Resize snapping preserves aspect lock");
    result = snapResize(doc, {200, 200}, {150, 150}, {511, 200}, true, false, false, {}, settings, 6, {});
    check(near(150 + result.sx * 50, 512) && near(result.sy, 1),
          "Centre resize preserves anchor and inactive axis");
    settings.enabled = false;
    result = snapResize(doc, {200, 200}, {100, 100}, {510.2, 509.3}, true, true, false, {}, settings, 6, {});
    check(near(result.sx, 4.102) && near(result.sy, 4.093), "Snap bypass preserves pointer position");
    settings.enabled = true;
    settings.pixels = false;
    doc.artboard.guidesX = {310};
    result = snapResize(doc, {200, 200}, {100, 100}, {308, 200}, true, false, false, {}, settings, 6, {});
    check(near(100 + result.sx * 100, 310) && result.hint.kinds[0] == SnapKind::Guide,
          "Resize snaps to guides");
    result = snapResize(doc, {200, 200}, {100, 100}, {-1, 200}, true, false, false, {}, settings, 6, {});
    check(near(result.sx, -1), "Resize can flip across the anchor and snap to zero");
}
void transformContracts() {
    Editor e;
    ok(e.add({shape(RectangleParams{}, 100, 50, {100, 100})}));
    const auto id = e.created()[0];
    auto doc = e.snapshot();
    // Resizing a parametric shape changes its size, not its scale, and keeps the anchor fixed.
    const Object r = resizedLocal(*doc->find(id), 2, 1, {-50, 0});
    const auto* s = r.as<ShapeData>();
    check(near(s->width, 200) && near(r.transform.scaleX, 1), "Parametric resize edits the size");
    check(near(r.transform.matrix().map({-100, 0}).x, 50), "Resize keeps its anchor fixed");
    // Flip mirrors about the selection centre and keeps bounds.
    const auto before = selectionBounds(*doc, {id}, {});
    ok(e.update("Flip", flipped(*doc, {id}, true, {})));
    const auto afterFlip = selectionBounds(*e.snapshot(), {id}, {});
    check(near(before.x0, afterFlip.x0, 1e-3) && near(before.x1, afterFlip.x1, 1e-3), "Flip keeps bounds");
    check(e.snapshot()->find(id)->transform.scaleX < 0, "Flip mirrors the object");
    // Rotation about a pivot.
    ok(e.update("Rotate", rotatedAbout(*e.snapshot(), {id}, 90, {0, 0})));
    check(near(e.snapshot()->find(id)->transform.position.x, -100, 1e-9), "Rotation moves the position");
    // Snapping prefers artboard centre over pixel rounding.
    SnapSettings snap;
    const auto result = snapTranslation(*e.snapshot(), {0, 0, 10, 10}, {250.6, 3.3}, {}, snap, 2, {});
    check(near(result.delta.x, 251) && result.kinds[0] == SnapKind::Artboard,
          "Centre snap wins within tolerance");
    check(near(result.delta.y, 3) && result.kinds[1] == SnapKind::Pixel, "Pixel snap applies otherwise");
    // Alignment and distribution.
    Editor f;
    ok(f.add({shape(RectangleParams{}, 10, 10, {10, 10}), shape(RectangleParams{}, 10, 10, {35, 30}),
              shape(RectangleParams{}, 10, 10, {100, 90})}));
    const auto ids = f.created();
    ok(f.update("Align",
                aligned(*f.snapshot(), ids, selectionBounds(*f.snapshot(), ids, {}), Alignment::Left, {})));
    for (const auto i : ids)
        check(near(objectBounds(*f.snapshot(), *f.snapshot()->find(i), {}).x0, 5, 1e-3), "Align left");
    ok(f.update("Distribute", distributed(*f.snapshot(), ids, false, {})));
    const auto mid = objectBounds(*f.snapshot(), *f.snapshot()->find(ids[1]), {});
    check(near(mid.center().y, 50, 1e-3), "Distribute equalises gaps");
}
void pathOperationContracts() {
    Editor e;
    ok(e.add({shape(RectangleParams{}, 100, 100, {100, 100})}));
    const auto id = e.created()[0];
    auto doc = e.snapshot();
    const auto grown = offsetObject(*doc, id, 10, JoinStyle::Miter, {});
    check(grown && near(grown->as<ShapeData>()->width, 120, 1e-2), "Offset path grows the shape");
    check(!offsetObject(*doc, id, -60, JoinStyle::Miter, {}), "Over-shrinking offset is refused");
    Object outlined = *doc->find(id);
    outlined.style.outlines = {
        {true, 6, {FillKind::Solid, {0, 0, 0, 255}}, OutlinePosition::Outside, JoinStyle::Miter}};
    ok(e.update("Outline", {outlined}));
    const auto stroke = outlineStrokeObject(*e.snapshot(), id, {});
    check(stroke && near(stroke->as<ShapeData>()->width, 112, 1e-2) && stroke->style.outlines.empty() &&
              stroke->style.fill.color == Color{0, 0, 0, 255},
          "Outline stroke turns the outline into a filled ring");
    const auto ring = objectArea(*e.snapshot(), *stroke, {}, .01, {});
    check(!inside(ring.polygons, ring.rule, {100, 100}) && inside(ring.polygons, ring.rule, {153, 100}),
          "The stroke ring has a hole");
    ok(e.replaceSubtrees("Convert", convertedToPath(*e.snapshot(), {id}, {})));
    const auto broken = breakAtNode(*e.snapshot(), id, 0, 2);
    check(broken && !std::get<Path>(broken->as<ShapeData>()->geometry).subpaths[0].closed,
          "Break opens a closed path");
    ok(e.replaceSubtrees("Break", {*broken}));
    const auto closed = closedPaths(*e.snapshot(), id);
    check(closed && std::get<Path>(closed->as<ShapeData>()->geometry).subpaths[0].closed,
          "Join ends closes it again");
    const auto reversed = reversedObject(*e.snapshot(), id);
    check(reversed.has_value(), "Explicit paths can be reversed");
    // Simplify keeps a polygonal shape's area.
    Editor s;
    ok(s.add({shape(EllipseParams{}, 200, 200, {200, 200})}));
    const auto simple = simplifiedObject(*s.snapshot(), s.created()[0], .5, {});
    const auto area = normalized(objectArea(*s.snapshot(), *simple, {}, .05, {}));
    check(simple && near(polygonsArea(area), std::numbers::pi * 100 * 100, 400), "Simplify preserves area");
}
void paletteContracts() {
    Editor e;
    Object a = shape(EllipseParams{}, 10, 10, {10, 10});
    a.style.fill = {FillKind::Solid, {200, 30, 30, 255}};
    a.style.outlines = {{true, 2, {FillKind::Solid, {200, 30, 30, 128}}}};
    Object b = shape(EllipseParams{}, 10, 10, {30, 10});
    b.style.fill = {FillKind::Linear, {0, 0, 0, 255}};
    b.style.fill.stops = {{0, {200, 30, 30, 255}}, {1, {0, 0, 255, 255}}};
    Object c = shape(EllipseParams{}, 10, 10, {50, 10});
    c.style.fill = {FillKind::Solid, {200, 30, 30, 255}};
    c.locked = true;
    ok(e.add({a, b, c}));
    const auto colors = documentColors(*e.snapshot());
    check(colors.front() == Color{200, 30, 30, 255}, "Most-used colour comes first, alpha ignored");
    ok(e.setPalette({{"Brand red", {200, 30, 30, 255}}}));
    ok(e.editPaletteColor(0, {10, 120, 240, 255}));
    const auto& objs = e.snapshot()->objects;
    check(objs[0]->style.fill.color == Color{10, 120, 240, 255}, "Palette edit recolours fills");
    check(objs[0]->style.outlines[0].paint.color == Color{10, 120, 240, 128},
          "Recolouring keeps each use's alpha");
    check(objs[1]->style.fill.stops[0].color == Color{10, 120, 240, 255} &&
              objs[1]->style.fill.stops[1].color == Color{0, 0, 255, 255},
          "Gradient stops recolour, other stops stay");
    check(objs[2]->style.fill.color == Color{200, 30, 30, 255}, "Locked objects are not recoloured");
    check(e.snapshot()->palette[0].color == Color{10, 120, 240, 255}, "Palette entry updates");
    ok(e.undo());
    check(e.snapshot()->objects[0]->style.fill.color == Color{200, 30, 30, 255} &&
              e.snapshot()->palette[0].color == Color{200, 30, 30, 255},
          "Recolour is one undo step");
    DocumentData big;
    big.palette.resize(MaxPalette + 1);
    check(!validateDocument(big), "Palette size is limited");
}
} // namespace
int main() {
    try {
        geometryContracts();
        presetContracts();
        booleanContracts();
        editorContracts();
        documentValidation();
        transformContracts();
        resizeSnapContracts();
        pathOperationContracts();
        paletteContracts();
        std::cout << "All core contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

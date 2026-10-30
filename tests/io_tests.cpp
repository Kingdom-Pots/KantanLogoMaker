#include "editing/Editor.h"
#include "editing/Transforms.h"
#include "io/ExportJob.h"
#include "io/ExportPresets.h"
#include "io/FontLibrary.h"
#include "io/IcoWriter.h"
#include "io/Images.h"
#include "io/ProjectIO.h"
#include "io/Serialization.h"
#include "io/SvgImport.h"
#include "io/TextShaper.h"
#include "render/ImageSampling.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <miniz.h>

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
QString fixture(const char* name) {
    return QDir(KANTAN_FIXTURE_DIR).filePath(name);
}
Object shape(ShapeGeometry g, double w, double h, Point at) {
    Object o;
    o.name = "Shape";
    o.transform.position = at;
    o.data = ShapeData{w, h, std::move(g)};
    return o;
}
// Write a ZIP with the given members (for malformed-project fixtures).
void writeZip(const QString& path, const std::vector<std::pair<QByteArray, QByteArray>>& members) {
    mz_zip_archive zip{};
    QByteArray name = path.toLocal8Bit();
    check(mz_zip_writer_init_heap(&zip, 0, 0), "zip init");
    for (const auto& [n, data] : members)
        check(
            mz_zip_writer_add_mem(&zip, n.constData(), data.constData(), static_cast<size_t>(data.size()), 6),
            "zip add");
    void* buffer = nullptr;
    size_t size = 0;
    check(mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size), "zip finalize");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "zip open");
    file.write(static_cast<const char*>(buffer), static_cast<qint64>(size));
    file.close();
    mz_zip_writer_end(&zip);
}
DocumentData sampleDocument(const std::shared_ptr<MemoryBudget>& budget, io::FontLibrary& fonts) {
    Editor e;
    Object star = shape(StarParams{5, .45, 3}, 200, 200, {256, 256});
    star.style.fill = {FillKind::Linear, {255, 200, 0, 255}};
    star.style.fill.stops = {{0, {255, 220, 80, 255}}, {.5, {255, 160, 0, 255}}, {1, {200, 90, 0, 255}}};
    star.style.outlines = {
        {true, 6, {FillKind::Solid, {255, 255, 255, 255}}, OutlinePosition::Outside, JoinStyle::Round, 4},
        {true, 3, {FillKind::Solid, {20, 20, 30, 255}}, OutlinePosition::Outside, JoinStyle::Miter, 6}};
    star.style.effects.bevel.enabled = true;
    star.style.effects.dropShadow.enabled = true;
    star.minExportSize = 16;
    star.repeat = {1, {}};
    ok(e.add(
        {star, shape(RectangleParams{{12, 4, 12, 4}, false, CornerStyle::Chamfer}, 120, 60, {100, 100})}));
    ok(e.group(e.created(), ObjectKind::Boolean, BooleanOp::Exclude));
    // Embedded font + text.
    const auto imported = fonts.importFile(fixture("KantanTestSans.ttf"));
    ok(imported.result);
    ok(e.addFont(imported.key, imported.font));
    Object text;
    text.name = "Name";
    text.transform.position = {256, 400};
    TextData t;
    t.text = "Kantan\nLogo";
    t.font = {imported.font.family, imported.key};
    t.arc = .4;
    text.data = t;
    ok(e.add({text}));
    // Embedded image.
    QImage pixels(40, 30, QImage::Format_RGBA8888);
    pixels.fill(QColor(10, 200, 120, 128));
    auto decoded = io::imageFromQImage(pixels, "dot.png", budget);
    ok(decoded.result);
    Object image;
    image.name = "Image";
    image.transform.position = {400, 120};
    image.data = ImageData{"img-test", 80, 60, {2, 2, 30, 20}};
    ok(e.add({image}, {}, {}, {{"img-test", decoded.image}}));
    auto artboard = e.snapshot()->artboard;
    artboard.guidesX = {128};
    ok(e.setArtboard(artboard));
    auto exportSettings = e.snapshot()->exportSettings;
    exportSettings.ico = true;
    exportSettings.custom = {{300, 120}};
    exportSettings.padding = 8;
    exportSettings.outputs = {{64, 32, "sub/{name}_wide"}};
    exportSettings.icoName = "app";
    ok(e.setPalette({{"Primary", {1, 2, 3, 255}}}));
    ok(e.setExportSettings(exportSettings));
    return *e.snapshot();
}
void projectRoundTrip(const QString& dir, const std::shared_ptr<MemoryBudget>& budget) {
    auto fonts = std::make_shared<io::FontLibrary>();
    io::QtTextShaper shaper(fonts);
    ImageSampler sampler(budget);
    const RenderServices services{&shaper, &sampler};
    const auto doc = std::make_shared<const DocumentData>(sampleDocument(budget, *fonts));
    const auto path = QDir(dir).filePath("sample.kantanlogo");
    ok(io::saveProject(path, doc, services, budget));
    const auto loaded = io::loadProject(path, budget);
    ok(loaded.result);
    check(loaded.document->objects.size() == doc->objects.size(), "Objects round-trip");
    for (std::size_t i = 0; i < doc->objects.size(); ++i)
        check(*loaded.document->objects[i] == *doc->objects[i], "Every object field round-trips exactly");
    check(loaded.document->artboard == doc->artboard &&
              loaded.document->exportSettings == doc->exportSettings,
          "Artboard and export settings round-trip");
    check(loaded.document->fonts.size() == 1 &&
              *loaded.document->fonts.begin()->second.data == *doc->fonts.begin()->second.data,
          "Embedded font bytes round-trip");
    const auto& a = doc->images.at("img-test");
    const auto& b = loaded.document->images.at("img-test");
    check(*a.encoded == *b.encoded && a.pixels->pixels == b.pixels->pixels,
          "Embedded image originals round-trip exactly");
    // Rendering the reopened document matches the original.
    RenderRequest request;
    request.width = request.height = 128;
    request.toPixels = fitTransform({0, 0, 512, 512}, 128, 128, 0);
    check(render(*doc, request, services, budget).pixels ==
              render(*loaded.document, request, services, budget).pixels,
          "Reopened document renders identically");
    // A second save replaces the first atomically and leaves no temporary files behind.
    ok(io::saveProject(path, doc, services, budget));
    check(QDir(dir).entryList({"sample*"}, QDir::Files).size() == 1, "Atomic save leaves no temporary file");
    // Linked (non-embedded) fonts: metadata only; reopening reports nothing embedded.
    auto linked = std::make_shared<DocumentData>(*doc);
    linked->fonts.begin()->second.embed = false;
    ok(io::saveProject(QDir(dir).filePath("linked.kantanlogo"), linked, services, budget));
    const auto relinked = io::loadProject(QDir(dir).filePath("linked.kantanlogo"), budget);
    ok(relinked.result);
    check(!relinked.document->fonts.begin()->second.data, "Linked fonts are not stored in the project");
    // The shaper still resolves the font through the app library (it was imported there).
    check(
        !fonts->resolve(FontRef{"x", relinked.document->fonts.begin()->first}, *relinked.document).isEmpty(),
        "Linked fonts re-link from the app library");
}
void malformedProjects(const QString& dir, const std::shared_ptr<MemoryBudget>& budget) {
    const QByteArray artboard =
        R"("artboard":{"width":512,"height":512,"transparent":true,"background":"#ffffffff"},)";
    const QByteArray exportJson =
        R"("export":{"png":true,"ico":false,"pngSizes":[32],"icoSizes":[16],"custom":[],"padding":0,"fit":0,"transparent":true,"background":"#ffffffff","naming":"{name}_{size}","folder":""},)";
    const auto manifest = [&](const QByteArray& extra, int version = 1) {
        return R"({"format":"kantanlogo-experimental","version":)" + QByteArray::number(version) +
               R"(,"pixelFormat":"premultiplied-rgba8-srgb","state":"1",)" + artboard + exportJson + extra +
               "}";
    };
    const QByteArray object =
        R"({"id":"5","parent":"0","kind":4,"name":"G","visible":true,"locked":false,"opacity":1,"blend":0,"transform":{"position":[0,0],"rotation":0,"scaleX":1,"scaleY":1},"style":{"fill":{"kind":0,"color":"#000000ff","stops":[],"angle":0,"center":[0.5,0.5],"radius":0.5},"outlines":[],"effects":{"dropShadow":{"enabled":false,"angle":0,"distance":0,"blur":0,"spread":0,"color":"#000000ff","opacity":0},"innerShadow":{"enabled":false,"angle":0,"distance":0,"blur":0,"spread":0,"color":"#000000ff","opacity":0},"outerGlow":{"enabled":false,"size":0,"spread":0,"color":"#000000ff","opacity":0},"innerGlow":{"enabled":false,"size":0,"spread":0,"color":"#000000ff","opacity":0},"bevel":{"enabled":false,"style":0,"profile":0,"size":0,"depth":0,"softness":0,"lightAngle":0,"altitude":0,"highlight":"#000000ff","highlightOpacity":0,"shadow":"#000000ff","shadowOpacity":0}}},"data":{}})";
    const auto load = [&](const char* name, std::vector<std::pair<QByteArray, QByteArray>> members) {
        const auto path = QDir(dir).filePath(name);
        writeZip(path, members);
        return io::loadProject(path, budget);
    };
    ok(load("valid.kantanlogo",
            {{"manifest.json", manifest(R"("objects":[)" + object + R"(],"images":[],"fonts":[])")}})
           .result);
    check(
        !load("newer.kantanlogo", {{"manifest.json", manifest(R"("objects":[],"images":[],"fonts":[])", 2)}})
             .result,
        "Newer schema is refused");
    check(load("newer.kantanlogo", {{"manifest.json", manifest(R"("objects":[],"images":[],"fonts":[])", 2)}})
                  .result.message.find("newer") != std::string::npos,
          "Newer schema says why");
    QByteArray dup = object;
    check(!load("dup.kantanlogo", {{"manifest.json", manifest(R"("objects":[)" + object + "," + dup +
                                                              R"(],"images":[],"fonts":[])")}})
               .result,
          "Duplicate object ids are refused");
    QByteArray cycleA = object, cycleB = object;
    cycleA.replace(R"("parent":"0")", R"("parent":"6")");
    cycleB.replace(R"("id":"5")", R"("id":"6")").replace(R"("parent":"0")", R"("parent":"5")");
    check(!load("cycle.kantanlogo", {{"manifest.json", manifest(R"("objects":[)" + cycleA + "," + cycleB +
                                                                R"(],"images":[],"fonts":[])")}})
               .result,
          "Group cycles are refused");
    check(
        !load(
             "missingimage.kantanlogo",
             {{"manifest.json",
               manifest(
                   R"("objects":[],"images":[{"key":"a","entry":"images/a.png","width":1,"height":1}],"fonts":[])")}})
             .result,
        "Missing image members are refused");
    check(!load("extra.kantanlogo",
                {{"manifest.json", manifest(R"("objects":[],"images":[],"fonts":[])")}, {"stray.bin", "x"}})
               .result,
          "Unreferenced archive members are refused");
    check(
        !load(
             "escape.kantanlogo",
             {{"manifest.json",
               manifest(
                   R"("objects":[],"images":[{"key":"../x","entry":"images/../x.png","width":1,"height":1}],"fonts":[])")},
              {"images/../x.png", "x"}})
             .result,
        "Unsafe member names are refused");
    check(
        !load(
             "embedfont.kantanlogo",
             {{"manifest.json",
               manifest(
                   R"("objects":[],"images":[],"fonts":[{"key":"f","family":"F","file":"f.ttf","embed":true}])")}})
             .result,
        "Embedded fonts without data are refused");
    QFile junk(QDir(dir).filePath("junk.kantanlogo"));
    check(junk.open(QIODevice::WriteOnly), "junk");
    junk.write(QByteArray(100, 'x'));
    junk.close();
    check(!io::loadProject(junk.fileName(), budget).result, "Non-archives are refused");
    check(budget->used() == 0, "Failed loads release their memory");
}
void icoContracts(const std::shared_ptr<MemoryBudget>& budget) {
    std::vector<Image> images;
    for (int size : {16, 32, 48, 256}) {
        auto image = Image::make(size, size, budget);
        image.ref(0, 0) = Pixel::rgba(255, 0, 0);
        images.push_back(std::move(image));
    }
    const auto bytes = io::icoBytes(images);
    const auto entries = io::readIco(bytes);
    check(entries.size() == 4 && entries[3].width == 256 && entries[0].width == 16,
          "ICO directory lists every size");
    for (std::size_t i = 0; i < entries.size(); ++i) {
        QImage decoded = QImage::fromData(entries[i].png, "PNG");
        check(decoded.width() == entries[i].width && decoded.pixelColor(0, 0) == QColor(255, 0, 0) &&
                  decoded.pixelColor(1, 1).alpha() == 0,
              "ICO entries decode with exact pixels and transparency");
    }
    bool threw = false;
    try {
        io::icoBytes({Image::make(512, 512, budget)});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "ICO refuses entries above 256 px");
    // Qt's own ICO reader agrees (the bundled qico plugin is not relied on for writing).
    QImage viaQt = QImage::fromData(bytes, "ICO");
    check(!viaQt.isNull(), "Qt reads the written ICO");
}
void exportContracts(const QString& dir, const std::shared_ptr<MemoryBudget>& budget) {
    io::WriteCoordinator storage;
    Editor e;
    Object o = shape(EllipseParams{}, 512, 512, {256, 256});
    o.style.fill = {FillKind::Solid, {200, 0, 0, 255}};
    ok(e.add({o}));
    ExportSettings s;
    s.png = s.ico = true;
    s.pngSizes = {32, 64, 512};
    s.icoSizes = {16, 32, 256};
    s.custom = {{300, 100}, {128, 128}};
    const auto plan = io::planExport(s, dir, "logo");
    ok(plan.result);
    check(plan.files.size() == 6, "PNG sizes, customs and one ICO are planned");
    const auto& ico = plan.files.back();
    check(ico.ico && ico.sizes.size() == 4 && ico.sizes.back().width == 256,
          "ICO takes ticked sizes and square customs <= 256");
    const auto results = io::runExport(*e.snapshot(), plan, s, {}, {}, storage, budget);
    for (const auto& r : results)
        ok(r.result);
    for (const auto& f : plan.files) {
        if (f.ico)
            continue;
        QImage image(f.path);
        check(image.width() == f.sizes[0].width && image.height() == f.sizes[0].height,
              "Each PNG has its exact size");
        check(image.pixelColor(0, 0).alpha() == 0, "Transparent corners are preserved");
    }
    QImage wide(QDir(dir).filePath("logo_300x100.png"));
    check(wide.pixelColor(150, 50).red() == 200 && wide.pixelColor(10, 50).alpha() == 0,
          "Non-square outputs contain the artwork centred");
    QFile icoFile(QDir(dir).filePath("logo.ico"));
    check(icoFile.open(QIODevice::ReadOnly) && io::readIco(icoFile.readAll()).size() == 4, "ICO file parses");
    // Naming validation and duplicate protection.
    s.naming = "{name}";
    check(!io::planExport(s, dir, "logo").result, "A pattern without {size} cannot hold several sizes");
    s.naming = "bad/{size}";
    check(!io::planExport(s, dir, "logo").result, "Path separators are refused in names");
    check(!io::planExport(s, QDir(dir).filePath("missing"), "logo").result, "Missing folders are refused");
    // Platform presets with named outputs in subfolders.
    for (const auto& preset : io::exportPresets()) {
        const auto applied = preset.apply(s);
        ok(io::planExport(applied, dir, "logo").result);
    }
    const auto android = std::find_if(io::exportPresets().begin(), io::exportPresets().end(),
                                      [](const auto& p) { return p.name.startsWith("Android"); });
    auto androidSettings = android->apply(ExportSettings{});
    const auto androidPlan = io::planExport(androidSettings, dir, "logo");
    for (const auto& r : io::runExport(*e.snapshot(), androidPlan, androidSettings, {}, {}, storage, budget))
        ok(r.result);
    check(QImage(QDir(dir).filePath("mipmap-xxxhdpi/ic_launcher.png")).width() == 192,
          "Named outputs create subfolders");
    const auto favicon = std::find_if(io::exportPresets().begin(), io::exportPresets().end(),
                                      [](const auto& p) { return p.name.contains("favicon"); });
    const auto favPlan = io::planExport(favicon->apply(ExportSettings{}), dir, "logo");
    check(favPlan.files.back().ico && favPlan.files.back().path.endsWith("favicon.ico"),
          "ICO name follows the preset");
    ExportSettings escape;
    escape.png = false;
    escape.outputs = {{16, 16, "../outside"}};
    check(!io::planExport(escape, dir, "logo").result, "Named outputs cannot leave the export folder");
    // Padding and background.
    s.naming = "{name}_{size}";
    s.padding = 25;
    s.transparent = false;
    s.background = {0, 0, 255, 255};
    const auto padded = io::renderExport(*e.snapshot(), {64, 64}, s, {}, {}, budget);
    check(padded.at(2, 2) == Pixel::rgba(0, 0, 255) && padded.at(32, 32) == Pixel::rgba(200, 0, 0) &&
              padded.at(10, 32) == Pixel::rgba(0, 0, 255),
          "Padding keeps a margin and the background fills the output");
    // A failed write leaves the previous file intact.
    const auto target = QDir(dir).filePath("logo_32.png");
    QFile before(target);
    check(before.open(QIODevice::ReadOnly), "previous export");
    const auto previous = before.readAll();
    before.close();
    QFile blocker(QDir(dir).filePath("blocker"));
    check(blocker.open(QIODevice::WriteOnly), "blocker file");
    blocker.close();
    io::ExportPlan broken{{}, {{QDir(dir).filePath("blocker/logo_32.png"), false, {{32, 32}}}}};
    check(!io::runExport(*e.snapshot(), broken, s, {}, {}, storage, budget)[0].result,
          "Unwritable destinations fail");
    QFile after(target);
    check(after.open(QIODevice::ReadOnly) && after.readAll() == previous,
          "Failed exports do not touch existing files");
}
void svgContracts() {
    const QByteArray svg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
        <rect x="10" y="10" width="40" height="40" fill="#ff0000"/>
        <circle cx="70" cy="70" r="20" fill="none" stroke="#0000ff" stroke-width="4"/>
        <text x="0" y="0">Hi</text></svg>)";
    const auto result = io::importSvg(svg, "test.svg", 200);
    ok(result.result);
    check(result.objects.size() == 3 && result.objects[0].kind() == ObjectKind::Group,
          "Several shapes import as a group");
    check(result.objects[1].style.fill.color == Color{255, 0, 0, 255}, "SVG fills become solid paints");
    check(result.objects[2].style.fill.kind == FillKind::None &&
              result.objects[2].style.outlines.size() == 1 &&
              std::abs(result.objects[2].style.outlines[0].width - 10) < 1e-3,
          "SVG strokes become centre outlines scaled with the drawing");
    check(std::find(result.unsupported.begin(), result.unsupported.end(), "text") != result.unsupported.end(),
          "Unsupported SVG text is reported");
    check(!io::importSvg("not svg", "x.svg").result, "Garbage is refused");
}
void textContracts() {
    auto fonts = std::make_shared<io::FontLibrary>();
    const auto imported = fonts->importFile(fixture("KantanTestSans.ttf"));
    ok(imported.result);
    check(imported.font.family == "Kantan Test Sans", "Imported font family is read from the file");
    io::QtTextShaper shaper(fonts);
    DocumentData doc;
    doc.fonts[imported.key] = imported.font;
    TextData t;
    t.text = "AV";
    t.font = {imported.font.family, imported.key};
    t.size = 100;
    const auto path = shaper.shape(t, doc);
    check(!path.empty(), "Text shapes into paths");
    const Box straight = bounds(path);
    check(std::abs(straight.center().x) < 2 && std::abs(straight.center().y) < 40,
          "Centre-aligned text sits on the origin");
    t.tracking = 500;
    check(bounds(shaper.shape(t, doc)).width() > straight.width() + 30, "Tracking widens text");
    t.tracking = 0;
    t.text = "ABCDEFGH";
    t.arc = .8;
    const Box arc = bounds(shaper.shape(t, doc));
    t.arc = 0;
    const Box flat = bounds(shaper.shape(t, doc));
    check(arc.height() > flat.height() * 1.3 && arc.width() < flat.width(), "Arc bend curves the line");
    // Missing fonts fall back but are reported.
    TextData missing = t;
    missing.font = {"No Such Font Family 123", ""};
    DocumentData doc2;
    Object o;
    o.id = {1};
    o.data = missing;
    doc2.objects.push_back(std::make_shared<const Object>(o));
    check(fonts->missingFonts(doc2).size() == 1, "Missing fonts are reported");
    check(!shaper.shape(missing, doc2).empty(), "Missing fonts render with the fallback family");
    // Convert to path equals the live render.
    DocumentData live;
    Object text;
    text.id = {1};
    text.transform.position = {128, 128};
    text.data = t;
    text.style.fill = {FillKind::Solid, {0, 0, 0, 255}};
    live.fonts = doc.fonts;
    live.artboard = {256, 256};
    live.objects.push_back(std::make_shared<const Object>(text));
    RenderServices services{&shaper, nullptr};
    auto converted = convertedToPath(live, {ObjectId{1}}, services);
    DocumentData asPath = live;
    asPath.objects = {std::make_shared<const Object>(converted.at(0))};
    RenderRequest request;
    request.width = request.height = 256;
    auto budget = std::make_shared<MemoryBudget>();
    check(render(live, request, services, budget).pixels == render(asPath, request, services, budget).pixels,
          "Convert to path matches the live text render");
}
void fragmentContracts() {
    DocumentData doc;
    Object g;
    g.id = {1};
    g.transform.position = {100, 0};
    g.data = GroupData{};
    Object child = shape(EllipseParams{}, 10, 10, {5, 5});
    child.id = {2};
    child.parent = {1};
    doc.objects = {std::make_shared<const Object>(g), std::make_shared<const Object>(child)};
    const auto json = io::fragmentToJson(doc, {ObjectId{2}});
    const auto fragment = io::fragmentFromJson(json);
    check(fragment.objects.size() == 1 && !fragment.objects[0].parent, "Fragment roots are detached");
    check(std::abs(fragment.objects[0].transform.position.x - 105) < 1e-9,
          "Fragment roots keep their world position");
    bool threw = false;
    try {
        QJsonObject bad = json;
        bad["format"] = "other";
        io::fragmentFromJson(bad);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "Foreign fragments are refused");
}
} // namespace
int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        check(dir.isValid(), "temporary folder");
        auto budget = std::make_shared<MemoryBudget>();
        projectRoundTrip(dir.path(), budget);
        malformedProjects(dir.path(), budget);
        icoContracts(budget);
        exportContracts(dir.path(), budget);
        svgContracts();
        textContracts();
        fragmentContracts();
        std::cout << "All IO contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

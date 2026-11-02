#include "app/MainWindow.h"
#include "app/Presets.h"
#include "io/ExportJob.h"
#include "io/ExportPresets.h"
#include "io/Images.h"
#include "io/ProjectIO.h"
#include "io/SvgExport.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTimer>
#include <QtPlugin>
#include <cstdio>

namespace {
QString argValue(const QStringList& args, const QString& flag, const QString& fallback = {}) {
    const int i = args.indexOf(flag);
    return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
}
int runCli(const QStringList& args, const std::shared_ptr<kantan::app::AppServices>& services) {
    const QString cmd = args.size() > 2 ? args[2] : QStringLiteral("help");
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        std::fprintf(
            stdout,
            "Kantan Logo Maker CLI bridge\n"
            "  --cli presets                              List templates, export presets and styles (JSON)\n"
            "  --cli inspect <file.kantanlogo>            Inspect document objects, fonts and settings "
            "(JSON)\n"
            "  --cli new --template <i|name> -o <file>    Create a project from a starter template\n"
            "  --cli render <in> -o <out.png|out.svg> [--size WxH] [--padding P] [--bg #RRGGBB|transparent]\n"
            "  --cli export <in> -o <folder> [--name N] [--preset P] [--sizes 16,32,64,256] [--ico] [--svg] "
            "[--no-png] [--no-svg]\n");
        return 0;
    }
    if (cmd == "presets") {
        QJsonArray templates, exports, styles, shapes;
        const auto tlist = kantan::app::starterTemplates(kantan::io::FontLibrary::fallbackFamily());
        for (std::size_t i = 0; i < tlist.size(); ++i)
            templates.append(QJsonObject{{"index", static_cast<int>(i)},
                                         {"name", tlist[i].name},
                                         {"width", tlist[i].document.artboard.width},
                                         {"height", tlist[i].document.artboard.height}});
        for (const auto& p : kantan::io::exportPresets())
            exports.append(QJsonObject{{"name", p.name}, {"description", p.description}});
        for (const auto& s : kantan::app::stylePresets())
            styles.append(s.name);
        for (const auto& s : kantan::app::shapePresets())
            shapes.append(s.name);
        std::fprintf(stdout, "%s",
                     QJsonDocument(QJsonObject{{"templates", templates},
                                               {"export_presets", exports},
                                               {"styles", styles},
                                               {"shapes", shapes}})
                         .toJson()
                         .constData());
        return 0;
    }
    if (cmd == "new") {
        const auto out = argValue(args, "-o", argValue(args, "--output"));
        if (out.isEmpty()) {
            std::fprintf(stderr, "Missing -o <file.kantanlogo>\n");
            return 2;
        }
        const auto tlist = kantan::app::starterTemplates(kantan::io::FontLibrary::fallbackFamily());
        const auto tname = argValue(args, "--template", "0");
        bool ok = false;
        int idx = tname.toInt(&ok);
        if (!ok) {
            idx = -1;
            for (std::size_t i = 0; i < tlist.size(); ++i)
                if (tlist[i].name.compare(tname, Qt::CaseInsensitive) == 0)
                    idx = static_cast<int>(i);
        }
        if (idx < 0 || idx >= static_cast<int>(tlist.size())) {
            std::fprintf(stderr, "Unknown template: %s\n", tname.toUtf8().constData());
            return 2;
        }
        auto doc = std::make_shared<kantan::DocumentData>(tlist[static_cast<std::size_t>(idx)].document);
        if (const auto text = argValue(args, "--text"); !text.isEmpty())
            for (auto& ref : doc->objects)
                if (ref->as<kantan::TextData>()) {
                    kantan::Object copy = *ref;
                    copy.as<kantan::TextData>()->text = text.toStdString();
                    ref = std::make_shared<const kantan::Object>(std::move(copy));
                    break;
                }
        const auto r = kantan::io::saveProject(out, doc, services->render(), services->budget);
        if (!r) {
            std::fprintf(stderr, "%s\n", r.message.c_str());
            return 1;
        }
        return 0;
    }
    if (args.size() < 4) {
        std::fprintf(stderr, "Missing input file\n");
        return 2;
    }
    const QString input = args[3];
    kantan::Snapshot doc;
    if (input.startsWith("template:", Qt::CaseInsensitive)) {
        const auto tlist = kantan::app::starterTemplates(kantan::io::FontLibrary::fallbackFamily());
        const int idx = input.mid(9).toInt();
        if (idx < 0 || idx >= static_cast<int>(tlist.size())) {
            std::fprintf(stderr, "Unknown template index\n");
            return 2;
        }
        doc = std::make_shared<const kantan::DocumentData>(tlist[static_cast<std::size_t>(idx)].document);
    } else {
        const auto loaded = kantan::io::loadProject(input, services->budget);
        if (!loaded.result) {
            std::fprintf(stderr, "%s\n", loaded.result.message.c_str());
            return 1;
        }
        doc = loaded.document;
    }
    if (cmd == "inspect") {
        QJsonArray objs;
        for (const auto& o : doc->objects)
            objs.append(QJsonObject{{"id", static_cast<qint64>(o->id.value)},
                                    {"parent", static_cast<qint64>(o->parent.value)},
                                    {"name", QString::fromStdString(o->name)},
                                    {"kind", kantan::kindName(o->kind())},
                                    {"visible", o->visible},
                                    {"locked", o->locked}});
        QJsonArray missing;
        for (const auto& f : services->fonts->missingFonts(*doc))
            missing.append(QString::fromStdString(f));
        std::fprintf(stdout, "%s",
                     QJsonDocument(QJsonObject{{"width", doc->artboard.width},
                                               {"height", doc->artboard.height},
                                               {"transparent", doc->artboard.transparent},
                                               {"objects", objs},
                                               {"missing_fonts", missing}})
                         .toJson()
                         .constData());
        return 0;
    }
    if (cmd == "render") {
        const auto out = argValue(args, "-o", argValue(args, "--output"));
        if (out.isEmpty()) {
            std::fprintf(stderr, "Missing -o <file.png>\n");
            return 2;
        }
        kantan::ExportSize size{static_cast<int>(doc->artboard.width),
                                static_cast<int>(doc->artboard.height)};
        if (const auto s = argValue(args, "--size"); !s.isEmpty()) {
            const auto parts = s.split('x', Qt::SkipEmptyParts, Qt::CaseInsensitive);
            const int w = parts[0].toInt();
            const int h = parts.size() > 1 ? parts[1].toInt() : w;
            if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
                std::fprintf(stderr, "Invalid size\n");
                return 2;
            }
            size = {w, h};
        }
        kantan::ExportSettings settings = doc->exportSettings;
        settings.selectionOnly = false;
        if (const auto p = argValue(args, "--padding"); !p.isEmpty())
            settings.padding = std::clamp(p.toDouble(), 0., 45.);
        if (const auto bg = argValue(args, "--bg"); !bg.isEmpty()) {
            if (bg.compare("transparent", Qt::CaseInsensitive) == 0)
                settings.transparent = true;
            else if (const auto c = kantan::app::parseHex(bg)) {
                settings.transparent = false;
                settings.background = *c;
            }
        }
        if (out.endsWith(".svg", Qt::CaseInsensitive)) {
            try {
                kantan::io::SvgExportOptions svgOpt;
                svgOpt.selectionOnly = settings.selectionOnly;
                svgOpt.transparent = settings.transparent;
                svgOpt.background = settings.background;
                const auto bytes = kantan::io::exportSvg(*doc, svgOpt, {}, services->render());
                QDir().mkpath(QFileInfo(out).path());
                auto ticket = services->storage->reserve(out);
                const auto r = ticket.run([&] { return kantan::io::writeFile(out, bytes); });
                if (!r) {
                    std::fprintf(stderr, "%s\n", r.message.c_str());
                    return 1;
                }
                return 0;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s\n", e.what());
                return 1;
            }
        }
        try {
            const auto bytes = kantan::io::encodePng(
                kantan::io::renderExport(*doc, size, settings, {}, services->render(), services->budget));
            QDir().mkpath(QFileInfo(out).path());
            auto ticket = services->storage->reserve(out);
            const auto r = ticket.run([&] { return kantan::io::writeFile(out, bytes); });
            if (!r) {
                std::fprintf(stderr, "%s\n", r.message.c_str());
                return 1;
            }
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s\n", e.what());
            return 1;
        }
    }
    if (cmd == "export") {
        const auto folder = argValue(
            args, "-o", argValue(args, "--output", QString::fromStdString(doc->exportSettings.folder)));
        if (folder.isEmpty()) {
            std::fprintf(stderr, "Missing -o <folder>\n");
            return 2;
        }
        kantan::ExportSettings s = doc->exportSettings;
        s.selectionOnly = false;
        if (const auto preset = argValue(args, "--preset"); !preset.isEmpty()) {
            bool found = false;
            for (const auto& p : kantan::io::exportPresets())
                if (p.name.compare(preset, Qt::CaseInsensitive) == 0) {
                    s = p.apply(s);
                    found = true;
                    break;
                }
            if (!found) {
                std::fprintf(stderr, "Unknown export preset: %s\n", preset.toUtf8().constData());
                return 2;
            }
        }
        if (const auto sizes = argValue(args, "--sizes"); !sizes.isEmpty()) {
            s.pngSizes.clear();
            s.icoSizes.clear();
            s.custom.clear();
            s.outputs.clear();
            for (const auto& tok : sizes.split(',', Qt::SkipEmptyParts)) {
                const auto wh = tok.trimmed().split('x', Qt::SkipEmptyParts, Qt::CaseInsensitive);
                const int w = wh[0].toInt(), h = wh.size() > 1 ? wh[1].toInt() : w;
                if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
                    std::fprintf(stderr, "Invalid size: %s\n", tok.toUtf8().constData());
                    return 2;
                }
                if (w == h) {
                    s.pngSizes.push_back(w);
                    if (w <= 256)
                        s.icoSizes.push_back(w);
                } else
                    s.custom.push_back({w, h});
            }
        }
        if (args.contains("--ico"))
            s.ico = true;
        if (args.contains("--no-ico"))
            s.ico = false;
        if (args.contains("--no-png"))
            s.png = false;
        if (args.contains("--png"))
            s.png = true;
        if (args.contains("--svg"))
            s.svg = true;
        if (args.contains("--no-svg"))
            s.svg = false;
        if (const auto p = argValue(args, "--padding"); !p.isEmpty())
            s.padding = std::clamp(p.toDouble(), 0., 45.);
        const QString base = argValue(args, "--name",
                                      input.startsWith("template:", Qt::CaseInsensitive)
                                          ? QStringLiteral("logo")
                                          : QFileInfo(input).completeBaseName());
        QDir().mkpath(folder);
        const auto plan = kantan::io::planExport(s, folder, base);
        if (!plan.result) {
            std::fprintf(stderr, "%s\n", plan.result.message.c_str());
            return 1;
        }
        QJsonArray written;
        for (const auto& outcome : kantan::io::runExport(*doc, plan, s, {}, services->render(),
                                                         *services->storage, services->budget)) {
            if (!outcome.result) {
                std::fprintf(stderr, "%s: %s\n", outcome.path.toUtf8().constData(),
                             outcome.result.message.c_str());
                return 1;
            }
            written.append(outcome.path);
        }
        std::fprintf(stdout, "%s", QJsonDocument(QJsonObject{{"files", written}}).toJson().constData());
        return 0;
    }
    std::fprintf(stderr, "Unknown CLI command: %s\n", cmd.toUtf8().constData());
    return 2;
}
} // namespace

#ifndef KANTAN_VERSION
#define KANTAN_VERSION "0.1.1"
#endif

int main(int argc, char** argv) {
    Q_INIT_RESOURCE(icons);
    Q_INIT_RESOURCE(presets);
    QElapsedTimer launch;
    launch.start();
    bool cliMode = false;
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--cli")
            cliMode = true;
    if (cliMode) {
        QGuiApplication app(argc, argv);
        QGuiApplication::setApplicationName("Kantan LogoMaker");
        QGuiApplication::setApplicationVersion(QStringLiteral(KANTAN_VERSION));
        QGuiApplication::setOrganizationName("Kantan");
        auto services = kantan::app::AppServices::create(kantan::app::MainWindow::dataFolder("fonts"));
        return runCli(app.arguments(), services);
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName("Kantan Logo Maker");
    QApplication::setApplicationVersion(QStringLiteral(KANTAN_VERSION));
    QApplication::setOrganizationName("Kantan");
    QIcon appIcon(":/icons/app.ico");
    if (appIcon.isNull())
        appIcon = QIcon(":/icons/app_256.png");
    QApplication::setWindowIcon(appIcon);
    auto services = kantan::app::AppServices::create(kantan::app::MainWindow::dataFolder("fonts"));
    const auto args = app.arguments();
    auto* window = new kantan::app::MainWindow(services);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    if (args.contains("--demo"))
        window->demo();
    if (args.contains("--select")) { // screenshot aid: select the object at this index (depth-first)
        const auto i = args.indexOf("--select");
        const auto index = i + 1 < args.size() ? args[i + 1].toInt() : 0;
        const auto doc = window->editor()->snapshot();
        if (index >= 0 && index < static_cast<int>(doc->objects.size()))
            window->editor()->select({doc->objects[static_cast<std::size_t>(index)]->id});
        emit window->canvas()->changed();
    }
    if (args.contains("--ready-file")) {
        const auto i = args.indexOf("--ready-file");
        if (i + 1 >= args.size())
            return 2;
        const auto path = args[i + 1];
        QTimer::singleShot(0, window, [window, &launch, path] {
            window->canvas()->fit();
            window->repaint();
            QFile file(path);
            if (file.open(QIODevice::WriteOnly))
                file.write(QJsonDocument(QJsonObject{{"launch_ms", launch.nsecsElapsed() / 1000000.},
                                                     {"device_pixel_ratio", window->devicePixelRatioF()},
                                                     {"logical_width", window->width()},
                                                     {"logical_height", window->height()}})
                               .toJson());
        });
    }
    if (args.contains("--quit-after")) {
        const auto i = args.indexOf("--quit-after");
        bool ok = false;
        const auto delay = i + 1 < args.size() ? args[i + 1].toInt(&ok) : -1;
        if (!ok || delay < 0 || delay > 600000)
            return 2;
        QTimer::singleShot(delay, &app, [] { QApplication::exit(0); });
    }
    if (args.contains("--screenshot")) {
        const auto i = args.indexOf("--screenshot");
        if (i + 1 >= args.size())
            return 2;
        const auto path = args[i + 1];
        QTimer::singleShot(900, window, [window, path] {
            window->canvas()->fit();
            QApplication::processEvents();
            QApplication::exit(window->grab().save(path) ? 0 : 3);
        });
    } else if (args.size() > 1 && !args[1].startsWith("--") && QFileInfo::exists(args[1]))
        window->load(args[1]);
    else if (!args.contains("--quit-after")) {
        // Offer autosaves left behind by a session that did not close cleanly.
        const QDir recovery(kantan::app::MainWindow::dataFolder("recovery"));
        const auto files = recovery.entryInfoList({"*.kantanlogo"}, QDir::Files, QDir::Time);
        bool recovered = false;
        if (!files.isEmpty()) {
            if (QMessageBox::question(
                    window, "Recover unsaved work",
                    QStringLiteral("Kantan Logo Maker did not close normally. Open %1 recovered document(s)?")
                        .arg(files.size())) == QMessageBox::Yes) {
                for (int k = 0; k < files.size(); ++k) {
                    auto* target = window;
                    if (k > 0) {
                        target = new kantan::app::MainWindow(services);
                        target->setAttribute(Qt::WA_DeleteOnClose);
                        target->show();
                    }
                    target->load(files[k].absoluteFilePath(), true);
                }
                recovered = true;
            } else {
                for (const auto& f : files)
                    QFile::remove(f.absoluteFilePath());
            }
        }
        if (!recovered && !args.contains("--demo") && kantan::app::StartDialog::showAtStartup())
            QTimer::singleShot(0, window, [window] { window->showStart(); });
    }
    return app.exec();
}

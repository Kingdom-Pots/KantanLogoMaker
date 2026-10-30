#include "app/MainWindow.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTimer>

int main(int argc, char** argv) {
    QElapsedTimer launch;
    launch.start();
    QApplication app(argc, argv);
    QApplication::setApplicationName("Kantan LogoMaker");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("Kantan");
    auto services = kantan::app::AppServices::create(kantan::app::MainWindow::dataFolder("fonts"));
    auto* window = new kantan::app::MainWindow(services);
    window->setAttribute(Qt::WA_DeleteOnClose);
    const auto args = app.arguments();
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
        if (!files.isEmpty() &&
            QMessageBox::question(
                window, "Recover unsaved work",
                QStringLiteral("Kantan LogoMaker did not close normally. Open %1 recovered document(s)?")
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
        }
        if (!recovered && !args.contains("--demo") && kantan::app::StartDialog::showAtStartup())
            QTimer::singleShot(0, window, [window] { window->showStart(); });
    }
    return app.exec();
}

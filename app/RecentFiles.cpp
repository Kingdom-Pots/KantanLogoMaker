#include "app/RecentFiles.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace kantan::app {
namespace {
constexpr int MaxRecent = 10;
const char* Key = "files/recent";
} // namespace
QStringList recentFiles() {
    QStringList out;
    for (const auto& path : QSettings().value(Key).toStringList())
        if (QFileInfo::exists(path) && !out.contains(path))
            out << path;
    return out;
}
void rememberRecent(const QString& path) {
    if (path.isEmpty())
        return;
    const auto clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    auto list = QSettings().value(Key).toStringList();
    list.removeAll(clean);
    list.prepend(clean);
    while (list.size() > MaxRecent)
        list.removeLast();
    QSettings().setValue(Key, list);
}
void clearRecent() {
    QSettings().remove(Key);
}
} // namespace kantan::app

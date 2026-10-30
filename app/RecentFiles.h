#pragma once
#include <QStringList>

namespace kantan::app {
// File › Open recent and the start screen. Stored in QSettings ("files/recent"), newest first, max 10.
QStringList recentFiles(); // only files that still exist
void rememberRecent(const QString& path);
void clearRecent();
} // namespace kantan::app

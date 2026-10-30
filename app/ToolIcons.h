#pragma once
#include <QIcon>
namespace kantan::app {
enum class Tool;
QIcon toolIcon(Tool tool);
QIcon uiIcon(const QString& name);
} // namespace kantan::app

#pragma once
#include <QString>
#include <string_view>
#include <vector>

class QWidget;

namespace kantan::app {
enum class Language {
    English,
    ChineseTraditional,
    ChineseSimplified,
    Japanese,
    Spanish,
};

struct LanguageInfo {
    Language lang;
    const char* code;
    const char* nativeName;
};

const std::vector<LanguageInfo>& supportedLanguages();
Language currentLanguage();
QString currentLanguageCode();
void setCurrentLanguage(Language lang);
void setCurrentLanguageByCode(const QString& code);
void loadLanguageFromSettings();

// Localized brand / software name ("Kantan Logo Maker" in EN/ES, "簡單 Logo Maker" in zh-TW, "简单 Logo
// Maker" in zh-CN, "簡単 Logo Maker" in ja).
QString brandAppName();
// Translates an English UI literal into the currently active language.
QString trUi(const char* english);
QString trUi(std::string_view english);
QString trUi(const QString& english);
// Localized "About Kantan Logo Maker" title and body text.
QString aboutDialogText(const QString& version);
// Recursively retranslates all labels, buttons, menus, actions, and dock titles in a widget subtree.
void retranslateWidgetTree(QWidget* root);
} // namespace kantan::app

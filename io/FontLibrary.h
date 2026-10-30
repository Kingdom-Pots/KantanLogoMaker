#pragma once
#include "document/Document.h"
#include <QString>
#include <QStringList>
#include <map>
#include <mutex>

namespace kantan::io {
struct ImportedFont {
    Result result;
    std::string key;
    EmbeddedFont font;
};
// App-level library of imported fonts (stored under AppData/fonts) plus registration of project-embedded
// fonts with the host font database. Registration needs a QGuiApplication.
class FontLibrary {
  public:
    // `folder` empty: an in-memory library (tests); otherwise imported files are copied there.
    explicit FontLibrary(QString folder = {});
    // Copy + register a .ttf/.otf/.ttc file; the returned font can be embedded into a document.
    ImportedFont importFile(const QString& path);
    ImportedFont importData(const QByteArray& data, const QString& fileName);
    // Families from fonts in the app library (shown under "Imported").
    QStringList importedFamilies() const;
    // Fonts in the app library, ready to embed into a document.
    std::vector<ImportedFont> libraryFonts() const;
    // Register a project font from its bytes (idempotent). Returns the family Qt knows it by.
    QString ensureRegistered(const std::string& key, const EmbeddedFont& font);
    // Resolve the family used for shaping: embedded > imported library > installed; empty when missing.
    QString resolve(const FontRef& ref, const DocumentData& document);
    // Fonts the document refers to that cannot be resolved (they render with the fallback family).
    std::vector<std::string> missingFonts(const DocumentData& document);
    static QString keyFor(const QByteArray& data);
    static QString fallbackFamily();

  private:
    QString folder_;
    mutable std::mutex mutex_;
    std::map<std::string, QString> registered_;   // key -> family
    std::map<std::string, EmbeddedFont> library_; // key -> font (app library)
};
} // namespace kantan::io

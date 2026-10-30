#pragma once
#include "document/TextShaper.h"
#include "io/FontLibrary.h"
#include <list>

namespace kantan::io {
// Qt shaping (QTextLayout + HarfBuzz kerning/ligatures) into plain core paths. Results are cached by
// every input that affects geometry. Thread-safe: shaping is serialized.
class QtTextShaper final : public TextShaper {
  public:
    explicit QtTextShaper(std::shared_ptr<FontLibrary> fonts) : fonts_(std::move(fonts)) {}
    Path shape(const TextData& text, const DocumentData& document) const override;
    FontLibrary& fonts() const {
        return *fonts_;
    }

  private:
    struct Key {
        TextData text;
        QString family;
        bool operator==(const Key& other) const {
            return text == other.text && family == other.family;
        }
    };
    std::shared_ptr<FontLibrary> fonts_;
    mutable std::mutex mutex_;
    mutable std::list<std::pair<Key, Path>> cache_; // most recent first
};
} // namespace kantan::io

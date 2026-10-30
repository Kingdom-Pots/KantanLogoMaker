#pragma once
#include "document/Document.h"

namespace kantan {
// Replaceable boundary (D-L3): the host shapes text with its font engine and returns plain geometry.
// The core never sees font engine types. Returned paths are in the text object's local space, laid out
// around the origin according to alignment, with arc bend already applied.
class TextShaper {
  public:
    virtual ~TextShaper() = default;
    virtual Path shape(const TextData& text, const DocumentData& document) const = 0;
};
} // namespace kantan

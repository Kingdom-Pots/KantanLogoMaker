#pragma once
#include "document/Document.h"

namespace kantan {
// D8: history stores immutable before/after document states that share unchanged objects.
struct HistoryEntry {
    std::string label;
    Snapshot before, after;
    std::vector<ObjectId> selectionBefore, selectionAfter;
};
inline constexpr std::size_t HistoryLimit = 200;
} // namespace kantan

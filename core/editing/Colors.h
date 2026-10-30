#pragma once
#include "document/Document.h"

namespace kantan {
// Every distinct style colour in use (fills, gradient stops, outlines, effects), most used first, ignoring
// alpha so that a translucent use of a brand colour still counts as that colour.
std::vector<Color> documentColors(const DocumentData& doc, std::size_t limit = 24);
// Objects whose style uses `from` (RGB match; each use keeps its own alpha), recoloured to `to`.
std::vector<Object> recolored(const DocumentData& doc, Color from, Color to);
bool sameRgb(Color a, Color b);
} // namespace kantan

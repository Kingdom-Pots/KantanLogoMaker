#include "editing/Colors.h"
#include <functional>

namespace kantan {
namespace {
// Visit every colour of a style; the visitor may change it.
void visitColors(Style& s, const std::function<void(Color&)>& visit) {
    const auto paint = [&](Paint& p) {
        if (p.kind == FillKind::Solid)
            visit(p.color);
        else if (p.kind == FillKind::Linear || p.kind == FillKind::Radial)
            for (auto& stop : p.stops)
                visit(stop.color);
    };
    paint(s.fill);
    for (auto& o : s.outlines)
        if (o.enabled)
            paint(o.paint);
    auto& e = s.effects;
    for (auto* shadow : {&e.dropShadow, &e.innerShadow})
        if (shadow->enabled)
            visit(shadow->color);
    for (auto* glow : {&e.outerGlow, &e.innerGlow})
        if (glow->enabled)
            visit(glow->color);
    if (e.bevel.enabled) {
        visit(e.bevel.highlight);
        visit(e.bevel.shadow);
    }
}
bool styled(const Object& o) {
    return o.kind() != ObjectKind::Group && o.kind() != ObjectKind::Clip;
}
} // namespace
bool sameRgb(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}
std::vector<Color> documentColors(const DocumentData& doc, std::size_t limit) {
    std::vector<std::pair<Color, int>> counts;
    for (const auto& ref : doc.objects) {
        if (!styled(*ref))
            continue;
        Style style = ref->style;
        visitColors(style, [&](Color& c) {
            const Color opaque{c.r, c.g, c.b, 255};
            const auto found =
                std::find_if(counts.begin(), counts.end(), [&](const auto& p) { return p.first == opaque; });
            if (found == counts.end())
                counts.push_back({opaque, 1});
            else
                ++found->second;
        });
    }
    std::stable_sort(counts.begin(), counts.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<Color> out;
    for (const auto& [c, n] : counts)
        if (out.size() < limit)
            out.push_back(c);
    return out;
}
std::vector<Object> recolored(const DocumentData& doc, Color from, Color to) {
    std::vector<Object> out;
    for (const auto& ref : doc.objects) {
        if (!styled(*ref))
            continue;
        Object o = *ref;
        bool changed = false;
        visitColors(o.style, [&](Color& c) {
            if (sameRgb(c, from) && !sameRgb(c, to)) {
                c = {to.r, to.g, to.b, c.a};
                changed = true;
            }
        });
        if (changed)
            out.push_back(std::move(o));
    }
    return out;
}
} // namespace kantan

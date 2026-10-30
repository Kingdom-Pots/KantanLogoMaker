// Release render timings for the size strip / export path. Prints CSV: case,size,milliseconds.
#include "render/Render.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

using namespace kantan;
int main() {
    std::cout << "case,size,milliseconds,p95_ms,max_ms,samples\n";
    for (const int count : {1, 24, 96}) {
        DocumentData doc;
        for (int i = 0; i < count; ++i) {
            Object o;
            o.id = {static_cast<std::uint64_t>(i + 1)};
            const int columns = count == 96 ? 12 : 6;
            o.transform.position = count == 96 ? Point{30. + (i % columns) * 40, 30. + (i / columns) * 60}
                                               : Point{64. + (i % columns) * 76, 64. + (i / columns) * 110};
            o.transform.rotation = i * 15;
            const double side = count == 96 ? 30. : 70.;
            o.data = ShapeData{side, side, StarParams{5 + i % 4, .5, 3}};
            o.style.outlines = {{true, 4, {FillKind::Solid, {255, 255, 255, 255}}, OutlinePosition::Outside}};
            o.style.effects.dropShadow.enabled = i % 2 == 0;
            o.style.effects.bevel.enabled = i % 3 == 0;
            doc.objects.push_back(std::make_shared<const Object>(o));
        }
        auto budget = std::make_shared<MemoryBudget>();
        for (Coord size : {16, 32, 64, 128, 256, 512, 1024}) {
            RenderRequest request;
            request.width = request.height = size;
            request.toPixels = fitTransform({0, 0, 512, 512}, size, size, 0);
            { const auto warmup = render(doc, request, {}, budget); }
            std::vector<double> samples;
            for (int sample = 0; sample < 21; ++sample) {
                const auto start = std::chrono::steady_clock::now();
                { const auto image = render(doc, request, {}, budget); }
                samples.push_back(
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count());
                if (budget->used() != 0) {
                    std::cerr << "Render retained budgeted memory after releasing its output\n";
                    return 1;
                }
            }
            std::sort(samples.begin(), samples.end());
            std::cout << "stars" << count << ',' << size << ',' << samples[10] << ',' << samples[19] << ','
                      << samples.back() << ',' << samples.size() << '\n';
        }
    }
}

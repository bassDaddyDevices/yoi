// Prints YOI's factory drawings (DSP/YoiFactoryShapes.hpp) as JSON for the browser stand-in.
// Used by Tools/update-standin.sh.

#include <cstdio>

#include "YoiFactoryShapes.hpp"

int main() {
    std::printf("[");
    for (size_t s = 0; s < yoi::kFactoryShapes.size(); ++s) {
        const auto& shape = yoi::kFactoryShapes[s];
        std::printf("%s{\"name\":\"%s\",\"points\":[", s > 0 ? "," : "", shape.name);
        for (int i = 0; i < shape.count; ++i) {
            const auto& point = shape.points[size_t(i)];
            std::printf("%s[%g,%g,%g]", i > 0 ? "," : "", double(point.x), double(point.y), double(point.bend));
        }
        std::printf("]}");
    }
    std::printf("]\n");
    return 0;
}

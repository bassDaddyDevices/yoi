//
//  yoi_state.cpp
//  YOI VST3
//

#include "yoi_state.h"

#include <array>
#include <cmath>

namespace yoi {

std::vector<bdd::CurvePoint> cleanDrawing(const bdd::CurvePoint* points, int count) {
    std::array<bdd::CurvePoint, bdd::kMaxCurvePoints> cleaned{};
    const int kept = bdd::sanitizeCurve(points, std::clamp(count, 0, bdd::kMaxCurvePoints), cleaned.data());
    return std::vector<bdd::CurvePoint>(cleaned.begin(), cleaned.begin() + kept);
}

std::vector<bdd::CurvePoint> factoryDrawing(int index) {
    const int count = YoiExtensionDSPKernel::factoryShapeCount();
    const auto& shape = kFactoryShapes[size_t(std::clamp(index, 0, count - 1))];
    return cleanDrawing(shape.points.data(), shape.count);
}

State defaultState() {
    State state;
    for (const auto& spec : params().specs) {
        state.plain.push_back(spec.defaultValue);
    }
    state.drawing = factoryDrawing(0);
    return state;
}

State stateFromPreset(const bdd::vst3::PresetFile& preset) {
    State state = defaultState();
    const auto& specs = params().specs;
    for (size_t index = 0; index < specs.size(); ++index) {
        const auto found = preset.parameters.find(specs[index].identifier);
        if (found != preset.parameters.end() && std::isfinite(found->second)) {
            state.plain[index] = specs[index].clamp(float(found->second));
        }
    }
    if (const auto bypass = preset.extras.find("bypass"); bypass != preset.extras.end()) {
        state.bypass = bypass->second >= 0.5;
    }
    if (preset.drawing) {
        std::vector<bdd::CurvePoint> points;
        for (const auto& point : *preset.drawing) {
            points.push_back({ point[0], point[1], point[2] });
        }
        state.drawing = cleanDrawing(points.data(), int(points.size()));
    }
    return state;
}

bdd::vst3::PresetFile presetFromState(const State& state, const std::string& name, bool includeBypass) {
    bdd::vst3::PresetFile preset;
    preset.synth = kSynthName;
    preset.name = name;
    const auto& specs = params().specs;
    for (size_t index = 0; index < specs.size() && index < state.plain.size(); ++index) {
        preset.parameters[specs[index].identifier] = state.plain[index];
    }
    std::vector<std::array<float, 3>> drawing;
    for (const auto& point : state.drawing) {
        drawing.push_back({ point.x, point.y, point.bend });
    }
    preset.drawing = std::move(drawing);
    if (includeBypass) {
        preset.extras["bypass"] = state.bypass ? 1.0 : 0.0;
    }
    return preset;
}

} // namespace yoi

//
//  yoi_vst3_tests.cpp
//  YOI VST3
//
//  Tests for what the VST3 adds around the kernel: its parameter table against the Audio Unit's,
//  the bdd-preset format's reading and writing rules, the factory presets, saved state, and the
//  drawing library. The kernel itself is covered by Tests/yoi_dsp_tests.cpp.
//

#include "yoi_params.h"
#include "yoi_state.h"

#include "shared/bdd_drawing_library.h"
#include "shared/bdd_preset_file.h"
#include "shared/bdd_preset_library.h"
#include "shared/bdd_text_files.h"

#include "choc/text/choc_JSON.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace {

int failures = 0;

#define CHECK(condition, ...)                                                                 \
    do {                                                                                      \
        if (!(condition)) {                                                                   \
            ++failures;                                                                       \
            std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                         \
            std::fprintf(stderr, __VA_ARGS__);                                                \
            std::fprintf(stderr, "\n");                                                       \
        }                                                                                     \
    } while (false)

using namespace yoi;
using bdd::vst3::PresetFile;

// MARK: - The parameter table against the Audio Unit's

/// The AU's own descriptor, as Tools/update-standin.sh captured it from the built Audio Unit.
choc::value::Value auDescriptor() {
    std::string text;
    if (!bdd::vst3::readTextFile(YOI_STANDIN_JS, text)) {
        return {};
    }
    // A Windows checkout has CRLF line endings (.gitattributes: text=auto); the JSON has no raw CRs.
    std::erase(text, '\r');
    const std::string marker = "const SNAPSHOT = ";
    const auto start = text.find(marker);
    const auto end = text.find(";\n", start);
    if (start == std::string::npos || end == std::string::npos) {
        return {};
    }
    return choc::json::parse(text.substr(start + marker.size(), end - start - marker.size()));
}

void testParametersMatchTheAudioUnit() {
    const auto snapshot = auDescriptor();
    CHECK(snapshot.isObject() && snapshot.hasObjectMember("parameters"), "couldn't read the AU's descriptor from yoi-standin.js");
    if (!snapshot.isObject()) {
        return;
    }
    const auto parameters = snapshot["parameters"];
    const auto& specs = params().specs;
    CHECK(parameters.size() == specs.size(), "the AU has %u parameters, the VST3 %zu", parameters.size(), specs.size());

    for (uint32_t index = 0; index < parameters.size() && index < specs.size(); ++index) {
        const auto au = parameters[index];
        const ParamSpec& spec = specs[index];
        const std::string identifier(au["identifier"].getString());
        CHECK(spec.identifier == identifier, "parameter %u: AU %s, VST3 %s", index, identifier.c_str(), spec.identifier.c_str());
        CHECK(int64_t(spec.id) == au["id"].getWithDefault<int64_t>(-1), "%s: ID differs", identifier.c_str());
        CHECK(spec.name == au["name"].getString(), "%s: name differs", identifier.c_str());
        CHECK(spec.group == au["group"].getString(), "%s: group differs", identifier.c_str());
        CHECK(std::string(bdd::vst3::unitName(spec.unit)) == au["unit"].getString(), "%s: unit differs", identifier.c_str());
        CHECK(spec.logarithmic == au["log"].getWithDefault<bool>(false), "%s: logarithmic differs", identifier.c_str());
        CHECK(std::fabs(spec.minimum - au["min"].getWithDefault<double>(0)) < 1e-4, "%s: minimum differs", identifier.c_str());
        CHECK(std::fabs(spec.maximum - au["max"].getWithDefault<double>(0)) < 1e-4, "%s: maximum differs", identifier.c_str());
        CHECK(std::fabs(spec.defaultValue - au["default"].getWithDefault<double>(0)) < 1e-4, "%s: default differs",
              identifier.c_str());

        const bool auHasOptions = au.hasObjectMember("options");
        CHECK(auHasOptions == !spec.options.empty(), "%s: options differ", identifier.c_str());
        if (auHasOptions) {
            const auto options = au["options"];
            CHECK(options.size() == spec.options.size(), "%s: option count differs", identifier.c_str());
            for (uint32_t option = 0; option < options.size() && option < spec.options.size(); ++option) {
                CHECK(spec.options[option] == options[option].getString(), "%s: option %u differs", identifier.c_str(), option);
            }
        }

        // Readings the plug-in owns must read the same as the AU's.
        if (au.hasObjectMember("valueDisplays")) {
            const auto readings = au["valueDisplays"];
            for (uint32_t step = 0; step < readings.size(); ++step) {
                const float value = spec.minimum + (spec.maximum - spec.minimum) * float(step) / float(readings.size() - 1);
                CHECK(derivedDisplay(spec.identifier, value) == readings[step].getString(), "%s: reading %u differs",
                      identifier.c_str(), step);
            }
        }
    }
}

void testNormalisedRoundTrip() {
    for (const auto& spec : params().specs) {
        for (const float value : { spec.minimum, spec.defaultValue, spec.maximum }) {
            const float back = bdd::vst3::toPlain(spec, bdd::vst3::toNormalised(spec, value));
            CHECK(std::fabs(back - value) <= 1e-3f * std::max(1.0f, std::fabs(value)), "%s: %g came back as %g",
                  spec.identifier.c_str(), double(value), double(back));
        }
    }
}

// MARK: - The format

void testShortestFloat() {
    for (const float value : { 0.4967497f, 408.7088f, 100.0f, 0.25f, -1.0f, 1e-7f, 29.999998f }) {
        const std::string text = bdd::vst3::shortestFloat(value);
        CHECK(std::strtof(text.c_str(), nullptr) == value, "%s doesn't read back as %.9g", text.c_str(), double(value));
    }
    CHECK(bdd::vst3::shortestFloat(100.0f) == "100", "100 should be written without a fraction");
    CHECK(bdd::vst3::shortestFloat(-0.0f) == "0", "-0 should be written as 0");
}

void testReadingRules() {
    const char* document = R"({
        "format": "bdd-preset", "version": 1, "synth": "YOI", "number": 4, "name": "Test",
        "parameters": { "cutoff": 99999, "resonance": 12.5, "notAParameter": 3, "subLevel": "loud" },
        "drawing": [[0, 0, 0], [0.5, 1, 0.25], [1, 0, 0]]
    })";
    const auto read = bdd::vst3::readPreset(document, "YOI");
    CHECK(read.preset.has_value(), "a valid document should read: %s", read.error.c_str());
    if (!read.preset) {
        return;
    }
    CHECK(read.preset->number == 4 && read.preset->name == "Test", "number and name should read");

    const State state = stateFromPreset(*read.preset);
    const State defaults = defaultState();
    const auto& specs = params().specs;
    for (size_t index = 0; index < specs.size(); ++index) {
        const auto& identifier = specs[index].identifier;
        if (identifier == "cutoff") {
            CHECK(state.plain[index] == 2500.0f, "cutoff should clamp to 2500, got %g", double(state.plain[index]));
        } else if (identifier == "resonance") {
            CHECK(state.plain[index] == 12.5f, "resonance should be 12.5");
        } else {
            CHECK(state.plain[index] == defaults.plain[index], "%s isn't in the file, so should be at its default",
                  identifier.c_str());
        }
    }
    CHECK(state.drawing.size() == 3, "the drawing should have 3 points, got %zu", state.drawing.size());

    CHECK(!bdd::vst3::readPreset(R"({"format":"bdd-preset","version":2,"synth":"YOI"})", "YOI").preset,
          "a newer version should be refused");
    CHECK(!bdd::vst3::readPreset(R"({"format":"bdd-preset","version":1,"synth":"OTHER"})", "YOI").preset,
          "another synth's preset should be refused");
    CHECK(!bdd::vst3::readPreset(R"({"format":"something-else","version":1})", "YOI").preset, "another format should be refused");
    CHECK(!bdd::vst3::readPreset("{ not json", "YOI").preset, "broken JSON should be refused, not thrown");

    const auto noDrawing = bdd::vst3::readPreset(R"({"format":"bdd-preset","version":1,"synth":"YOI","parameters":{}})", "YOI");
    CHECK(noDrawing.preset && stateFromPreset(*noDrawing.preset).drawing.size() == defaults.drawing.size(),
          "no drawing should mean Init's");
}

void testStateRoundTrip() {
    State state = defaultState();
    for (size_t index = 0; index < state.plain.size(); ++index) {
        const auto& spec = params().specs[index];
        state.plain[index] = spec.minimum + (spec.maximum - spec.minimum) * 0.37f;
        if (spec.isDiscrete()) {
            state.plain[index] = spec.maximum;
        }
    }
    state.bypass = true;
    state.drawing = factoryDrawing(5);

    const std::string text = bdd::vst3::writePreset(presetFromState(state, "Session", true));
    const auto read = bdd::vst3::readPreset(text, kSynthName);
    CHECK(read.preset.has_value(), "the written state should read back: %s", read.error.c_str());
    if (!read.preset) {
        return;
    }
    const State back = stateFromPreset(*read.preset);
    CHECK(back.bypass, "bypass should survive");
    for (size_t index = 0; index < state.plain.size(); ++index) {
        CHECK(back.plain[index] == state.plain[index], "%s: %g came back as %g", params().specs[index].identifier.c_str(),
              double(state.plain[index]), double(back.plain[index]));
    }
    CHECK(back.drawing.size() == state.drawing.size(), "the drawing should survive");
    for (size_t index = 0; index < back.drawing.size() && index < state.drawing.size(); ++index) {
        CHECK(back.drawing[index].x == state.drawing[index].x && back.drawing[index].y == state.drawing[index].y
                  && back.drawing[index].bend == state.drawing[index].bend,
              "drawing point %zu differs", index);
    }
    CHECK(read.preset->parameters.size() == params().specs.size(), "every parameter should be written");
}

// MARK: - Factory presets

void testFactoryPresets() {
    const auto presets = bdd::vst3::PresetLibrary::factoryPresets(kSynthName);

    size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(YOI_FACTORY_DIR)) {
        files += entry.path().extension() == ".json" ? 1 : 0;
    }
    CHECK(presets.size() == files, "%zu factory files but %zu presets compiled in", files, presets.size());
    CHECK(presets.size() == 16, "the V1 list has 16 factory presets, got %zu", presets.size());

    int expected = 1;
    for (const auto& preset : presets) {
        CHECK(preset.number == expected, "factory numbers should run 1, 2, ...: got %d for %s", preset.number, preset.name.c_str());
        ++expected;
        CHECK(preset.drawing.has_value(), "%s has no drawing", preset.name.c_str());
        for (const auto& spec : params().specs) {
            CHECK(preset.parameters.count(spec.identifier) == 1, "%s doesn't set %s", preset.name.c_str(), spec.identifier.c_str());
        }
        for (const auto& [identifier, value] : preset.parameters) {
            const int index = params().indexOf(identifier);
            CHECK(index >= 0, "%s sets unknown parameter %s", preset.name.c_str(), identifier.c_str());
            if (index >= 0) {
                const auto& spec = params().specs[size_t(index)];
                CHECK(value >= spec.minimum && value <= spec.maximum, "%s: %s = %g is out of range", preset.name.c_str(),
                      identifier.c_str(), value);
            }
        }
    }
    if (!presets.empty()) {
        CHECK(presets.front().name == "Basic Cyber Yuy", "preset 1 should be Basic Cyber Yuy, got %s", presets.front().name.c_str());
    }
}

void testFactoryPresetsPlay() {
    // Every factory preset, played through the kernel as the processor would set it up.
    for (const auto& preset : bdd::vst3::PresetLibrary::factoryPresets(kSynthName)) {
        const State state = stateFromPreset(preset);
        auto kernel = std::make_unique<YoiExtensionDSPKernel>();
        kernel->initialize(2, 48000.0);
        for (size_t index = 0; index < state.plain.size(); ++index) {
            kernel->setParameter(AUParameterAddress(params().specs[index].id), state.plain[index]);
        }
        std::vector<float> xs, ys, bends;
        for (const auto& point : state.drawing) {
            xs.push_back(point.x);
            ys.push_back(point.y);
            bends.push_back(point.bend);
        }
        kernel->setEnvelopeCurve(xs.data(), ys.data(), bends.data(), int(xs.size()));
        kernel->setHostTiming(140.0, 0.0, 0, true, 4.0, 4.0);
        kernel->noteOn(33, 100);

        std::vector<float> left(512), right(512);
        float peak = 0.0f;
        bool finite = true;
        for (int block = 0; block < 94; ++block) {   // about one second
            std::array<float*, 2> buffers { left.data(), right.data() };
            kernel->process(std::span<float*>(buffers.data(), 2), int64_t(block) * 512, 512);
            for (size_t i = 0; i < left.size(); ++i) {
                finite = finite && std::isfinite(left[i]) && std::isfinite(right[i]);
                peak = std::max({ peak, std::fabs(left[i]), std::fabs(right[i]) });
            }
        }
        CHECK(finite, "%s produced a non-finite sample", preset.name.c_str());
        CHECK(peak > 0.01f, "%s is silent (peak %g)", preset.name.c_str(), double(peak));
        CHECK(peak <= 1.0f, "%s goes past full scale (peak %g)", preset.name.c_str(), double(peak));
    }
}

// MARK: - User presets and drawings

std::filesystem::path scratchFolder(const char* name) {
    auto folder = std::filesystem::temp_directory_path() / ("yoi-vst3-tests-" + std::string(name));
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder;
}

void testUserPresets() {
    const auto folder = scratchFolder("presets");
    bdd::vst3::PresetLibrary library(kSynthName, folder);

    int number = 0;
    CHECK(library.saveUser("  ", presetFromState(defaultState(), "", false), number) == "Enter a preset name.",
          "an empty name should be refused");
    CHECK(library.saveUser("My Growl", presetFromState(defaultState(), "", false), number).empty(), "saving should work");
    CHECK(number == -1, "the first user preset should be -1, got %d", number);
    CHECK(library.saveUser("my growl", presetFromState(defaultState(), "", false), number) == "A user preset already has that name.",
          "names should be unique regardless of case");
    CHECK(library.saveUser("A/B: test?", presetFromState(defaultState(), "", false), number).empty(),
          "a name with path characters should still save");

    const auto entries = library.list();
    CHECK(entries.size() == 1 + 16 + 2, "Init, 16 factory and 2 user presets expected, got %zu", entries.size());
    CHECK(entries.front().number == 0 && entries.front().name == "Init", "Init should come first");
    CHECK(entries.back().user && entries.back().name == "My Growl", "user presets should be in name order");

    const auto loaded = library.load(-2);
    CHECK(loaded && loaded->name == "My Growl", "a user preset should load by its number");
    CHECK(library.removeUser(-2).empty(), "deleting should work");
    CHECK(library.list().size() == 1 + 16 + 1, "one user preset should be left");
    std::filesystem::remove_all(folder);
}

void testDrawingLibrary() {
    const auto folder = scratchFolder("drawings");
    const auto file = folder / "Drawings.json";
    {
        bdd::vst3::DrawingLibrary library(file);
        CHECK(library.drawings().empty(), "no file should be an empty library");
        CHECK(library.save("Wobble", { { 0, 0, 0 }, { 0.5f, 1, 0.2f }, { 1, 0, 0 } }).empty(), "saving should work");
        CHECK(library.save("wobble", { { 0, 0, 0 } }) == "A drawing with that name already exists.", "duplicates should be refused");
        CHECK(!library.save("", {}).empty(), "an empty name should be refused");
    }
    {
        bdd::vst3::DrawingLibrary library(file);
        const auto* found = library.find("WOBBLE");
        CHECK(found != nullptr && found->points.size() == 3 && found->points[1][2] == 0.2f,
              "a saved drawing should read back, found case-insensitively");
        CHECK(library.remove("Wobble").empty(), "deleting should work");
        CHECK(library.drawings().empty(), "the library should be empty again");
    }
    // A file from a newer version is read, but never rewritten.
    bdd::vst3::writeTextFileAtomically(file, R"({"version": 2, "drawings": [{"name": "Future", "points": [[0, 0, 0]]}]})");
    {
        bdd::vst3::DrawingLibrary library(file);
        CHECK(library.find("Future") != nullptr, "a newer file's drawings should still load");
        CHECK(!library.save("Mine", { { 0, 0, 0 } }).empty(), "a newer file should not be rewritten");
    }
    std::filesystem::remove_all(folder);
}

} // namespace

int main() {
    testParametersMatchTheAudioUnit();
    testNormalisedRoundTrip();
    testShortestFloat();
    testReadingRules();
    testStateRoundTrip();
    testFactoryPresets();
    testFactoryPresetsPlay();
    testUserPresets();
    testDrawingLibrary();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("All YOI VST3 tests passed\n");
    return 0;
}

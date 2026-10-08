//
//  yoi_controller.cpp
//  YOI VST3
//

#include "yoi_controller.h"

#include "yoi_ids.h"
#include "yoi_processor.h"

#if YOI_WITH_WEBVIEW
#include "yoi_editor.h"
#endif

#include "shared/bdd_platform.h"
#include "shared/bdd_text_files.h"

#include "BDDLicenseStore.hpp"
#include "shared/bdd_spec_parameter.h"

#include "choc/text/choc_JSON.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace yoi {

namespace {

/// A hidden parameter: read-only for the display values, writable for the pitch wheel.
Parameter* makeHiddenParameter(ParamID id, const char* name, int32 flags) {
    ParameterInfo info{};
    info.id = id;
    UString(info.title, str16BufferSize(String128)).fromAscii(name);
    info.flags = flags | ParameterInfo::kIsHidden;
    info.unitId = kRootUnitId;
    return new Parameter(info);
}

} // namespace

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    const tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    for (const ParamSpec& spec : params().specs) {
        std::function<std::string(float)> formatter;
        if (!derivedDisplay(spec.identifier, spec.defaultValue).empty()) {
            formatter = [identifier = spec.identifier](float value) { return derivedDisplay(identifier, value); };
        }
        parameters.addParameter(new bdd::vst3::SpecParameter(spec, ParameterInfo::kCanAutomate, std::move(formatter)));
    }

    static const ParamSpec bypass = { kBypassId, "bypass", "Bypass", "", Unit::boolean, 0, 1, 0 };
    parameters.addParameter(new bdd::vst3::SpecParameter(bypass, ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass));

    const char* displayNames[kDisplayCount] = { "Envelope Position", "Envelope Value", "Filter Cutoff", "Filter Q",
                                                "Cutoff Top", "Cutoff Bottom", "Meter Left", "Meter Right" };
    for (int index = 0; index < kDisplayCount; ++index) {
        parameters.addParameter(makeHiddenParameter(kDisplayPositionId + uint32_t(index), displayNames[index],
                                                    ParameterInfo::kIsReadOnly));
    }

    Parameter* bend = makeHiddenParameter(kPitchBendId, "Pitch Bend", ParameterInfo::kCanAutomate);
    bend->getInfo().defaultNormalizedValue = 0.5;
    bend->setNormalized(0.5);
    parameters.addParameter(bend);

    mLicense = bdd::license::loadFromFolder(bdd::vst3::utf8FromPath(bdd::vst3::platform::licensesDirectory()),
                                            kSynthName, kProductMajor);
    return kResultOk;
}

tresult PLUGIN_API Controller::terminate() {
    mEditor = nullptr;
    return EditControllerEx1::terminate();
}

tresult PLUGIN_API Controller::getMidiControllerAssignment(int32 busIndex, int16 channel, CtrlNumber midiControllerNumber,
                                                           ParamID& id) {
    (void)channel;
    if (busIndex == 0 && midiControllerNumber == kPitchBend) {
        id = kPitchBendId;
        return kResultTrue;
    }
    return kResultFalse;
}

// MARK: - State

tresult PLUGIN_API Controller::setComponentState(IBStream* stream) {
    State state;
    if (!Processor::readStateStream(stream, state)) {
        return kResultFalse;
    }
    const auto& specs = params().specs;
    for (size_t index = 0; index < specs.size(); ++index) {
        setParamNormalized(specs[index].id, bdd::vst3::toNormalised(specs[index], state.plain[index]));
    }
    setParamNormalized(kBypassId, state.bypass ? 1.0 : 0.0);
    mDrawing = state.drawing;
#if YOI_WITH_WEBVIEW
    if (mEditor != nullptr) {
        mEditor->drawingChanged();
    }
#endif
    return kResultOk;
}

/// The controller's own state: which preset the menu shows. JSON, like everything else.
tresult PLUGIN_API Controller::setState(IBStream* stream) {
    if (stream == nullptr) {
        return kResultFalse;
    }
    std::string text;
    char buffer[1024];
    int32 read = 0;
    do {
        read = 0;
        if (stream->read(buffer, sizeof(buffer), &read) != kResultOk) {
            break;
        }
        text.append(buffer, size_t(std::max<int32>(read, 0)));
    } while (read == int32(sizeof(buffer)) && text.size() < 64 * 1024);

    try {
        const auto root = choc::json::parse(text);
        CurrentPreset preset;
        if (root.isObject() && root.hasObjectMember("currentPreset") && root["currentPreset"].isObject()) {
            const auto current = root["currentPreset"];
            preset.set = true;
            preset.number = int(current["number"].getWithDefault<int64_t>(0));
            preset.name = std::string(current["name"].getWithDefault<std::string_view>(""));
            preset.user = current["kind"].getWithDefault<std::string_view>("") == "user";
        }
        mCurrentPreset = preset;
    } catch (...) {
        return kResultFalse;
    }
#if YOI_WITH_WEBVIEW
    if (mEditor != nullptr) {
        mEditor->presetsChanged();
    }
#endif
    return kResultOk;
}

tresult PLUGIN_API Controller::getState(IBStream* stream) {
    if (stream == nullptr) {
        return kResultFalse;
    }
    auto root = choc::value::createObject("");
    if (mCurrentPreset.set) {
        auto current = choc::value::createObject("");
        current.setMember("number", int64_t(mCurrentPreset.number));
        current.setMember("name", mCurrentPreset.name);
        current.setMember("kind", std::string(mCurrentPreset.user ? "user" : "factory"));
        root.setMember("currentPreset", current);
    }
    const std::string text = choc::json::toString(root);
    int32 written = 0;
    return stream->write(const_cast<char*>(text.data()), int32(text.size()), &written) == kResultOk ? kResultOk : kResultFalse;
}

// MARK: - Parameters

tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value) {
    if (tag >= kDisplayPositionId && tag < kDisplayEnd) {
        mDisplay[size_t(tag - kDisplayPositionId)] = displayRange(tag).denormalise(value);
        return EditControllerEx1::setParamNormalized(tag, value);
    }
    const tresult result = EditControllerEx1::setParamNormalized(tag, value);
#if YOI_WITH_WEBVIEW
    if (result == kResultOk && mEditor != nullptr && tag != mWritingParameter) {
        mEditor->parameterChanged(tag);
    }
#endif
    return result;
}

void Controller::setParameterFromEditor(ParamID id, double plain) {
    const ParamValue normalised = plainParamToNormalized(id, plain);
    mWritingParameter = id;
    setParamNormalized(id, normalised);
    mWritingParameter = 0xFFFFFFFF;
    performEdit(id, normalised);
}

double Controller::display(uint32_t id) const {
    return (id >= kDisplayPositionId && id < kDisplayEnd) ? mDisplay[size_t(id - kDisplayPositionId)] : 0.0;
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
#if YOI_WITH_WEBVIEW
    if (FIDStringsEqual(name, ViewType::kEditor)) {
        return new Editor(this);
    }
#else
    (void)name;
#endif
    return nullptr;
}

// MARK: - The sound

State Controller::currentState() {
    State state;
    for (const auto& spec : params().specs) {
        state.plain.push_back(float(normalizedParamToPlain(spec.id, getParamNormalized(spec.id))));
    }
    state.bypass = getParamNormalized(kBypassId) >= 0.5;
    state.drawing = mDrawing;
    return state;
}

void Controller::applyState(const State& state) {
    const auto& specs = params().specs;
    for (size_t index = 0; index < specs.size() && index < state.plain.size(); ++index) {
        const ParamID id = specs[index].id;
        const ParamValue normalised = bdd::vst3::toNormalised(specs[index], state.plain[index]);
        beginEdit(id);
        setParamNormalized(id, normalised);
        performEdit(id, normalised);
        endEdit(id);
    }
    setDrawing(state.drawing);
}

// MARK: - Drawing

std::vector<float> Controller::drawingTable(int count) const {
    std::vector<float> table(static_cast<size_t>(bdd::kCurveTableSize));
    bdd::renderCurveTable(mDrawing.data(), int(mDrawing.size()), table.data(), bdd::kCurveTableSize);
    std::vector<float> samples(static_cast<size_t>(std::max(2, count)));
    for (size_t index = 0; index < samples.size(); ++index) {
        const double x = double(index) / double(samples.size() - 1);
        samples[index] = bdd::lookup(table.data(), bdd::kCurveTableSize, x);
    }
    return samples;
}

void Controller::setDrawing(const std::vector<bdd::CurvePoint>& points) {
    mDrawing = cleanDrawing(points.data(), int(points.size()));
    sendDrawingToProcessor();
#if YOI_WITH_WEBVIEW
    if (mEditor != nullptr) {
        mEditor->drawingChanged();
    }
#endif
}

void Controller::sendDrawingToProcessor() {
    IPtr<IMessage> message = owned(allocateMessage());
    if (!message) {
        return;
    }
    std::vector<float> values;
    for (const auto& point : mDrawing) {
        values.push_back(point.x);
        values.push_back(point.y);
        values.push_back(point.bend);
    }
    message->setMessageID(kDrawingMessage);
    message->getAttributes()->setBinary(kDrawingAttribute, values.data(), uint32(values.size() * sizeof(float)));
    sendMessage(message);
}

// MARK: - Licensing

bdd::license::License Controller::installLicense(const std::string& token) {
    const auto result = bdd::license::installInFolder(bdd::vst3::utf8FromPath(bdd::vst3::platform::licensesDirectory()),
                                                      token, kSynthName, kProductMajor);
    if (result.isValid()) {
        mLicense = result;
        if (IPtr<IMessage> message = owned(allocateMessage())) {
            message->setMessageID(kLicenseChangedMessage);
            sendMessage(message);
        }
    }
    return result;
}

// MARK: - Libraries

bdd::vst3::PresetLibrary& Controller::presets() {
    if (!mPresets) {
        mPresets = std::make_unique<bdd::vst3::PresetLibrary>(kSynthName, bdd::vst3::platform::productDirectory(kSynthName) / "Presets");
    }
    return *mPresets;
}

bdd::vst3::DrawingLibrary& Controller::drawings() {
    if (!mDrawings) {
        mDrawings = std::make_unique<bdd::vst3::DrawingLibrary>(bdd::vst3::platform::productDirectory(kSynthName) / "Drawings.json");
    }
    return *mDrawings;
}

} // namespace yoi

//
//  yoi_editor.cpp
//  YOI VST3
//

#include "yoi_editor.h"

#include "yoi_controller.h"
#include "yoi_params.h"

#include "choc/containers/choc_Value.h"

#include <cstdio>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace yoi {

namespace {

/// Points the page draws the curve with, as the AU sends.
constexpr int kTableSize = 256;

/// How many readings a plug-in-owned display is sampled at across a parameter's range.
constexpr int kDerivedDisplaySteps = 101;

choc::value::Value pointsValue(const std::vector<bdd::CurvePoint>& points) {
    auto list = choc::value::createEmptyArray();
    for (const auto& point : points) {
        auto entry = choc::value::createEmptyArray();
        entry.addArrayElement(double(point.x));
        entry.addArrayElement(double(point.y));
        entry.addArrayElement(double(point.bend));
        list.addArrayElement(entry);
    }
    return list;
}

int64_t integerMember(const choc::value::ValueView& message, const char* name, int64_t fallback) {
    return message.hasObjectMember(name) ? message[name].getWithDefault<int64_t>(fallback) : fallback;
}

std::string stringMember(const choc::value::ValueView& message, const char* name) {
    return (message.hasObjectMember(name) && message[name].isString()) ? std::string(message[name].getString()) : std::string();
}

choc::value::Value status(const std::string& message, bool error) {
    auto value = choc::value::createObject("");
    value.setMember("message", message);
    value.setMember("error", error);
    return value;
}

} // namespace

Editor::Editor(Controller* controller) : WebEditor(controller, kSize, YOI_UI_DEBUG != 0) {}

Editor::~Editor() {
    if (getController() != nullptr) {
        controller().setEditor(nullptr);
    }
}

Controller& Editor::controller() const {
    return *static_cast<Controller*>(getController());
}

void Editor::opened() {
    controller().setEditor(this);
}

void Editor::closed() {
    controller().setEditor(nullptr);
}

// MARK: - Page -> plug-in

void Editor::handleMessage(const std::string& type, const choc::value::ValueView& message) {
    Controller& yoi = controller();

    if (type == "hello") {
        setPageReady(true);
        mDirtyParameters.clear();
        mDrawingDirty = false;
        mPresetsDirty = false;
        sendFullState();

    } else if (type == "beginEdit" || type == "edit" || type == "endEdit") {
        const auto id = ParamID(integerMember(message, "id", -1));
        if (params().find(id) == nullptr) {
            return;
        }
        if (type == "beginEdit") {
            yoi.beginEdit(id);
        } else if (type == "endEdit") {
            yoi.endEdit(id);
        } else if (message.hasObjectMember("value")) {
            yoi.setParameterFromEditor(id, message["value"].getWithDefault<double>(0.0));
        }

    } else if (type == "setCurve") {
        if (!message.hasObjectMember("points") || !message["points"].isArray()) {
            return;
        }
        const auto list = message["points"];
        std::vector<bdd::CurvePoint> points;
        for (uint32_t index = 0; index < list.size(); ++index) {
            const auto point = list[index];
            if (!point.isArray() || point.size() < 2) {
                continue;
            }
            points.push_back({ float(point[0].getWithDefault<double>(0.0)), float(point[1].getWithDefault<double>(0.5)),
                               point.size() > 2 ? float(point[2].getWithDefault<double>(0.0)) : 0.0f });
        }
        yoi.setDrawing(points);
        sendCurve();

    } else if (type == "loadShape") {
        yoi.setDrawing(factoryDrawing(int(integerMember(message, "index", 0))));
        sendCurve();

    } else if (type == "drawingList") {
        yoi.drawings().reload();
        sendDrawingState();

    } else if (type == "drawingLoad") {
        auto& library = yoi.drawings();
        library.reload();
        const auto* drawing = library.find(stringMember(message, "name"));
        if (drawing == nullptr) {
            sendDrawingState("That drawing is no longer available.", true);
            return;
        }
        std::vector<bdd::CurvePoint> points;
        for (const auto& point : drawing->points) {
            points.push_back({ point[0], point[1], point[2] });
        }
        yoi.setDrawing(points);
        sendCurve();

    } else if (type == "drawingSave") {
        std::vector<std::array<float, 3>> points;
        for (const auto& point : yoi.drawing()) {
            points.push_back({ point.x, point.y, point.bend });
        }
        const std::string name = stringMember(message, "name");
        const std::string error = yoi.drawings().save(name, points);
        if (error.empty()) {
            const auto* saved = yoi.drawings().find(name);
            sendDrawingState("Saved drawing \xE2\x80\x9C" + (saved ? saved->name : name) + "\xE2\x80\x9D.");
        } else {
            sendDrawingState(error, true);
        }

    } else if (type == "drawingDelete") {
        const std::string error = yoi.drawings().remove(stringMember(message, "name"));
        sendDrawingState(error.empty() ? std::string("Drawing deleted.") : error, !error.empty());

    } else if (type == "presetList") {
        sendPresetState();

    } else if (type == "presetSelect") {
        const int number = int(integerMember(message, "number", 0));
        auto& library = yoi.presets();
        Controller::CurrentPreset selected;
        for (const auto& entry : library.list()) {
            if (entry.number == number) {
                selected = { true, entry.number, entry.name, entry.user };
            }
        }
        if (!selected.set) {
            sendPresetState("That preset is no longer available.", true);
            return;
        }
        // Init is every default and Init's drawing; anything else is a file, on top of defaults.
        // Choosing a preset always reloads it, so it doubles as "revert".
        if (number == 0) {
            yoi.applyState(defaultState());
        } else if (const auto preset = library.load(number)) {
            yoi.applyState(stateFromPreset(*preset));
        } else {
            sendPresetState("That preset is no longer available.", true);
            return;
        }
        yoi.setCurrentPreset(selected);
        sendFullState();
        sendPresetState("Loaded preset.");

    } else if (type == "presetSave") {
        int number = 0;
        const std::string name = stringMember(message, "name");
        const std::string error = yoi.presets().saveUser(name, presetFromState(yoi.currentState(), name, false), number);
        if (!error.empty()) {
            sendPresetState(error, true);
            return;
        }
        for (const auto& entry : yoi.presets().list()) {
            if (entry.number == number) {
                yoi.setCurrentPreset({ true, entry.number, entry.name, true });
            }
        }
        sendFullState();
        sendPresetState("Preset saved.");

    } else if (type == "presetDelete") {
        const int number = int(integerMember(message, "number", 0));
        if (number >= 0) {
            return;
        }
        const auto current = yoi.currentPreset();
        auto& library = yoi.presets();
        library.list();   // number the user presets as the page saw them
        const std::string error = library.removeUser(number);
        if (!error.empty()) {
            sendPresetState("Could not delete that preset.", true);
            return;
        }
        if (current.set && current.user && current.number == number) {
            yoi.setCurrentPreset({});
        }
        sendFullState();
        sendPresetState("Preset deleted.");

    } else if (type == "error") {
        std::fprintf(stderr, "YOI editor script error: %s (%s:%lld)\n", stringMember(message, "message").c_str(),
                     stringMember(message, "source").c_str(), (long long)integerMember(message, "line", 0));
    }
    // "pong" needs nothing: the VST3 window is rebuilt each time it opens, so it never pings.
}

// MARK: - Plug-in -> page

void Editor::sendFullState() {
    Controller& yoi = controller();

    auto parameters = choc::value::createEmptyArray();
    auto values = choc::value::createObject("");
    for (const auto& spec : params().specs) {
        const double value = yoi.normalizedParamToPlain(spec.id, yoi.getParamNormalized(spec.id));
        values.setMember(std::to_string(spec.id), value);

        auto entry = choc::value::createObject("");
        entry.setMember("id", int64_t(spec.id));
        entry.setMember("identifier", spec.identifier);
        entry.setMember("name", spec.name);
        entry.setMember("group", spec.group);
        entry.setMember("min", double(spec.minimum));
        entry.setMember("max", double(spec.maximum));
        entry.setMember("unit", std::string(bdd::vst3::unitName(spec.unit)));
        entry.setMember("log", spec.logarithmic);
        entry.setMember("default", double(spec.defaultValue));
        if (!spec.options.empty()) {
            auto options = choc::value::createEmptyArray();
            for (const auto& option : spec.options) {
                options.addArrayElement(option);
            }
            entry.setMember("options", options);
        }
        // Readings the plug-in owns, sampled across the range so the page can look one up
        // instead of knowing the mapping. Evenly spaced from min to max, ends included.
        if (!derivedDisplay(spec.identifier, spec.defaultValue).empty()) {
            auto readings = choc::value::createEmptyArray();
            for (int step = 0; step < kDerivedDisplaySteps; ++step) {
                const float fraction = float(step) / float(kDerivedDisplaySteps - 1);
                readings.addArrayElement(derivedDisplay(spec.identifier, spec.minimum + (spec.maximum - spec.minimum) * fraction));
            }
            entry.setMember("valueDisplays", readings);
        }
        parameters.addArrayElement(entry);
    }

    auto shapes = choc::value::createEmptyArray();
    for (int index = 0; index < YoiExtensionDSPKernel::factoryShapeCount(); ++index) {
        shapes.addArrayElement(std::string(YoiExtensionDSPKernel::factoryShapeName(index)));
    }

    auto descriptor = choc::value::createObject("");
    descriptor.setMember("parameters", parameters);
    descriptor.setMember("shapes", shapes);

    // One message, as the AU sends it.
    auto state = choc::value::createObject("");
    state.setMember("descriptor", descriptor);
    state.setMember("params", values);
    state.setMember("curve", curveValue());
    state.setMember("presetState", presetStateValue());
    state.setMember("drawingState", drawingStateValue());
    // Included so the filter graph has something to draw on the page's first frame.
    state.setMember("display", displayValue());
    send(state);

    mDirtyParameters.clear();
    mDrawingDirty = false;
    mPresetsDirty = false;
}

choc::value::Value Editor::curveValue() {
    Controller& yoi = controller();
    auto table = choc::value::createEmptyArray();
    for (float value : yoi.drawingTable(kTableSize)) {
        table.addArrayElement(double(value));
    }
    auto curve = choc::value::createObject("");
    curve.setMember("points", pointsValue(yoi.drawing()));
    curve.setMember("table", table);
    return curve;
}

choc::value::Value Editor::presetStateValue() {
    Controller& yoi = controller();
    auto factory = choc::value::createEmptyArray();
    auto user = choc::value::createEmptyArray();
    for (const auto& entry : yoi.presets().list()) {
        auto item = choc::value::createObject("");
        item.setMember("number", int64_t(entry.number));
        item.setMember("name", entry.name);
        item.setMember("kind", std::string(entry.user ? "user" : "factory"));
        (entry.user ? user : factory).addArrayElement(item);
    }
    auto current = choc::value::createObject("");
    if (const auto& selected = yoi.currentPreset(); selected.set) {
        current.setMember("number", int64_t(selected.number));
        current.setMember("name", selected.name);
        current.setMember("kind", std::string(selected.user ? "user" : "factory"));
    }
    auto presetState = choc::value::createObject("");
    presetState.setMember("factoryPresets", factory);
    presetState.setMember("userPresets", user);
    presetState.setMember("currentPreset", current);
    return presetState;
}

choc::value::Value Editor::drawingStateValue() {
    auto names = choc::value::createEmptyArray();
    for (const auto& drawing : controller().drawings().drawings()) {
        names.addArrayElement(drawing.name);
    }
    auto drawingState = choc::value::createObject("");
    drawingState.setMember("drawings", names);
    return drawingState;
}

choc::value::Value Editor::displayValue() {
    Controller& yoi = controller();
    auto display = choc::value::createObject("");
    display.setMember("position", yoi.display(kDisplayPositionId));
    display.setMember("value", yoi.display(kDisplayValueId));
    display.setMember("cutoffHz", yoi.display(kDisplayCutoffId));
    display.setMember("filterQ", yoi.display(kDisplayQId));
    display.setMember("cutoffTopHz", yoi.display(kDisplayCutoffTopId));
    display.setMember("cutoffBottomHz", yoi.display(kDisplayCutoffBottomId));
    display.setMember("meterLeft", yoi.display(kDisplayMeterLeftId));
    display.setMember("meterRight", yoi.display(kDisplayMeterRightId));
    return display;
}

void Editor::sendCurve() {
    auto state = choc::value::createObject("");
    state.setMember("curve", curveValue());
    send(state);
    mDrawingDirty = false;
}

void Editor::sendPresetState(const std::string& message, bool error) {
    auto state = choc::value::createObject("");
    state.setMember("presetState", presetStateValue());
    if (!message.empty()) {
        state.setMember("status", status(message, error));
    }
    send(state);
    mPresetsDirty = false;
}

void Editor::sendDrawingState(const std::string& message, bool error) {
    auto state = choc::value::createObject("");
    state.setMember("drawingState", drawingStateValue());
    if (!message.empty()) {
        state.setMember("status", status(message, error));
    }
    send(state);
}

void Editor::tick() {
    Controller& yoi = controller();
    auto state = choc::value::createObject("");

    if (!mDirtyParameters.empty()) {
        auto values = choc::value::createObject("");
        for (ParamID id : mDirtyParameters) {
            if (params().find(id) != nullptr) {
                values.setMember(std::to_string(id), yoi.normalizedParamToPlain(id, yoi.getParamNormalized(id)));
            }
        }
        mDirtyParameters.clear();
        state.setMember("params", values);
    }
    if (mDrawingDirty) {
        state.setMember("curve", curveValue());
        mDrawingDirty = false;
    }
    if (mPresetsDirty) {
        state.setMember("presetState", presetStateValue());
        mPresetsDirty = false;
    }
    state.setMember("display", displayValue());
    send(state);
}

} // namespace yoi

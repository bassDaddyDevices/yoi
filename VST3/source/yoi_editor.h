//
//  yoi_editor.h
//  YOI VST3
//
//  YOI's window: the same page the Audio Unit shows (YoiExtension/WebUI), speaking the same
//  protocol as UI/WebEditor.swift. Page -> plug-in: hello, beginEdit/edit/endEdit, setCurve,
//  loadShape, drawingList/Load/Save/Delete, presetList/Select/Save/Delete, pong, error.
//  Plug-in -> page: window.bdd.receive({ descriptor, params, curve, display, presetState,
//  drawingState, status }).
//

#pragma once

#include "shared/bdd_web_editor.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <set>
#include <string>

namespace yoi {

class Controller;

class Editor : public bdd::vst3::WebEditor {
public:
    explicit Editor(Controller* controller);
    ~Editor() override;

    /// The AU's editorSize; the page scales to fit whatever the window is.
    static constexpr Size kSize { 1040, 640, 780, 480 };

    // Called by the controller when something the page shows changes.
    void parameterChanged(Steinberg::Vst::ParamID id) { mDirtyParameters.insert(id); }
    void drawingChanged() { mDrawingDirty = true; }
    void presetsChanged() { mPresetsDirty = true; }

protected:
    void handleMessage(const std::string& type, const choc::value::ValueView& message) override;
    void tick() override;
    void opened() override;
    void closed() override;

private:
    Controller& controller() const;

    void sendFullState();
    void sendCurve();
    choc::value::Value curveValue();
    choc::value::Value presetStateValue();
    choc::value::Value drawingStateValue();
    choc::value::Value displayValue();
    void sendPresetState(const std::string& message = {}, bool error = false);
    void sendDrawingState(const std::string& message = {}, bool error = false);

    std::set<Steinberg::Vst::ParamID> mDirtyParameters;
    bool mDrawingDirty = false;
    bool mPresetsDirty = false;
};

} // namespace yoi

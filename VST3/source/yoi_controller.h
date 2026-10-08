//
//  yoi_controller.h
//  YOI VST3
//
//  The editing half of the plug-in: publishes the parameters to the host, maps the pitch wheel,
//  keeps the controller's copy of the drawing (cleaned and rendered by the kernel's own curve
//  code, never reimplemented), sends redrawn curves to the processor, and runs the preset and
//  drawing libraries for the editor.
//

#pragma once

#include "yoi_params.h"
#include "yoi_state.h"

#include "BDDLicense.hpp"

#include "shared/bdd_drawing_library.h"
#include "shared/bdd_preset_library.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace yoi {

class Editor;

class Controller : public Steinberg::Vst::EditControllerEx1, public Steinberg::Vst::IMidiMapping {
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller);
    }

    OBJ_METHODS(Controller, EditControllerEx1)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
    END_DEFINE_INTERFACES(EditControllerEx1)
    REFCOUNT_METHODS(EditControllerEx1)

    // EditController
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

    // IMidiMapping: the pitch wheel arrives as kPitchBendId.
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                              Steinberg::Vst::ParamID& id) SMTG_OVERRIDE;

    // MARK: Editor support

    /// The drawing as the processor plays it.
    const std::vector<bdd::CurvePoint>& drawing() const { return mDrawing; }

    /// The drawing as the envelope plays it, sampled at `count` points across 0...1, from the
    /// kernel's own renderer, so the editor draws exactly what is heard.
    std::vector<float> drawingTable(int count) const;

    /// Takes a drawing from the editor (or a preset), cleans it and sends it to the processor.
    void setDrawing(const std::vector<bdd::CurvePoint>& points);

    /// The current sound: every parameter as the host has it, and the drawing.
    State currentState();

    /// Applies a sound as edits the host sees (so they reach the processor and can be recorded),
    /// and sends its drawing.
    void applyState(const State& state);

    /// The latest display value the processor sent, as plain (Hz, linear, 0...1).
    double display(uint32_t id) const;

    /// What the preset menu shows as selected. `number` 0 is Init; negative numbers are user
    /// presets. Kept with the controller's state.
    struct CurrentPreset {
        bool set = false;
        int number = 0;
        std::string name;
        bool user = false;
    };
    const CurrentPreset& currentPreset() const { return mCurrentPreset; }
    void setCurrentPreset(CurrentPreset preset) { mCurrentPreset = std::move(preset); }

    /// The license this copy runs under, as the processor also reads it.
    const bdd::license::License& license() const { return mLicense; }

    /// Checks a pasted license and, if it's valid, saves it and tells the processor to play in full.
    /// Returns the check, so the editor can say what was wrong.
    bdd::license::License installLicense(const std::string& token);

    bdd::vst3::PresetLibrary& presets();
    bdd::vst3::DrawingLibrary& drawings();

    /// The editor registers itself while it is open so it can be told about changes.
    void setEditor(Editor* editor) { mEditor = editor; }

    /// While the editor itself is writing a parameter, the change it causes isn't echoed back.
    void setParameterFromEditor(Steinberg::Vst::ParamID id, double plain);

private:
    void sendDrawingToProcessor();

    std::vector<bdd::CurvePoint> mDrawing = factoryDrawing(0);
    std::array<double, kDisplayCount> mDisplay{};
    CurrentPreset mCurrentPreset;
    std::unique_ptr<bdd::vst3::PresetLibrary> mPresets;
    std::unique_ptr<bdd::vst3::DrawingLibrary> mDrawings;
    bdd::license::License mLicense;
    Editor* mEditor = nullptr;
    Steinberg::Vst::ParamID mWritingParameter = 0xFFFFFFFF;
};

} // namespace yoi

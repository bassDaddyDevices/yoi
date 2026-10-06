//
//  yoi_processor.h
//  YOI VST3
//
//  The audio half of the plug-in: a thin VST3 wrapper around the same kernel the Audio Unit runs
//  (YoiExtensionDSPKernel.hpp, compiled with YOI_PORTABLE). Notes go in through noteOn/noteOff,
//  the wheel through setPitchBend, the host's tempo and position through setHostTiming, exactly as
//  the AU's MIDI handling and the DSP tests do.
//

#pragma once

#include "yoi_params.h"
#include "yoi_state.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace yoi {

class Processor : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() override;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

    // IConnectionPoint: the controller sends the drawing.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    /// The saved state as text: a bdd-preset document. Shared with the controller, which reads the
    /// same stream in `setComponentState`.
    static bool readStateStream(Steinberg::IBStream* stream, State& out);
    static bool writeStateStream(Steinberg::IBStream* stream, const State& state);

private:
    /// Hands the drawing to the kernel. Callers hold `mDrawingMutex`: the kernel takes one writer.
    void publishDrawingLocked();

    /// Pushes every parameter's value to the kernel. Render thread, or while inactive.
    void pushAllParameters();

    void applyParameter(Steinberg::Vst::ParamID id, double normalised);
    void readHostTiming(const Steinberg::Vst::ProcessContext* context);
    void publishDisplay(Steinberg::Vst::IParameterChanges* outputs);

    std::unique_ptr<YoiExtensionDSPKernel> mKernel;

    /// Plain value of each parameter, in `params()` order. Written by the render thread from host
    /// automation and by `setState`; read by `getState`.
    std::vector<std::atomic<float>> mPlain;
    std::atomic<bool> mBypass { false };
    /// Set by `setState` so the render thread picks the restored values up at its next block.
    std::atomic<bool> mParametersNeedResync { true };

    std::mutex mDrawingMutex;
    std::vector<bdd::CurvePoint> mDrawing;

    double mSampleRate = 44100.0;
    /// A running sample count, the kernel's time base for placing blocks against the song position.
    int64_t mSampleTime = 0;
    Steinberg::int32 mDisplayInterval = 1470;
    Steinberg::int32 mSamplesSinceDisplay = 0;

    /// Notes and automation gathered from the host and ordered by sample offset, so a block can be
    /// split wherever something happens. Preallocated: this runs on the render thread.
    struct PendingEvent {
        enum Kind : uint8_t { parameter, noteOn, noteOff };
        Steinberg::int32 offset;
        Steinberg::int32 order;
        Kind kind;
        Steinberg::Vst::ParamID id;   ///< parameter
        double value;                 ///< parameter value (normalised), or the note number
        int velocity;                 ///< note on
    };
    static constexpr int kMaxPendingEvents = 4096;
    std::array<PendingEvent, kMaxPendingEvents> mPending{};
};

} // namespace yoi

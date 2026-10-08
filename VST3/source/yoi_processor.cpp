//
//  yoi_processor.cpp
//  YOI VST3
//

#include "yoi_processor.h"

#include "yoi_ids.h"

#include "shared/bdd_platform.h"
#include "shared/bdd_text_files.h"

#include "BDDLicenseStore.hpp"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace yoi {

namespace {

/// Display values go to the controller about this often; the editor redraws at the same rate.
constexpr double kDisplayRate = 30.0;

/// Largest state accepted. A real one is a couple of kilobytes.
constexpr int64 kMaxStateSize = 256 * 1024;

} // namespace

Processor::Processor() : mKernel(std::make_unique<YoiExtensionDSPKernel>()), mPlain(params().specs.size()) {
    setControllerClass(kControllerUID);

    const State defaults = defaultState();
    for (size_t index = 0; index < mPlain.size(); ++index) {
        mPlain[index].store(defaults.plain[index], std::memory_order_relaxed);
    }
    std::lock_guard<std::mutex> lock(mDrawingMutex);
    mDrawing = defaults.drawing;
    publishDrawingLocked();
}

Processor::~Processor() = default;

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) {
        return result;
    }
    addEventInput(STR16("MIDI In"), 1);
    addAudioOutput(STR16("Output"), SpeakerArr::kStereo);
    reloadLicense();
    return kResultOk;
}

void Processor::reloadLicense() {
    const auto license = bdd::license::loadFromFolder(bdd::vst3::utf8FromPath(bdd::vst3::platform::licensesDirectory()),
                                                      kSynthName, kProductMajor);
    mKernel->setLicensed(bdd::license::unlocks(license));
}

/// No audio in; mono or stereo out. The voice is mono-safe: left plus right is the dry sound.
tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                 SpeakerArrangement* outputs, int32 numOuts) {
    (void)inputs;
    if (numIns != 0 || numOuts != 1) {
        return kResultFalse;
    }
    const int32 channels = SpeakerArr::getChannelCount(outputs[0]);
    if (channels == 1 || channels == 2) {
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }
    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize) {
    // The kernel is single precision throughout.
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    mSampleRate = setup.sampleRate;
    mDisplayInterval = std::max<int32>(1, int32(setup.sampleRate / kDisplayRate));
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state) {
        int32 channels = 2;
        if (auto* bus = FCast<AudioBus>(audioOutputs.at(0))) {
            channels = SpeakerArr::getChannelCount(bus->getArrangement());
        }
        mKernel->setMaximumFramesToRender(uint32_t(std::max<int32>(processSetup.maxSamplesPerBlock, 1)));
        mKernel->initialize(channels, mSampleRate);
        pushAllParameters();
        mParametersNeedResync.store(false, std::memory_order_relaxed);
        mSamplesSinceDisplay = 0;
    } else {
        mKernel->deInitialize();
    }
    return AudioEffect::setActive(state);
}

uint32 PLUGIN_API Processor::getTailSamples() {
    // The release can be automated, so report the longest it can be, as the AU does (10.5 s).
    const auto* release = params().find(YoiExtensionParameterAddress::ampRelease);
    const double seconds = (release != nullptr ? release->maximum * 0.001 : 10.0) + 0.5;
    return uint32(std::ceil(seconds * mSampleRate));
}

// MARK: - Parameters

void Processor::applyParameter(ParamID id, double normalised) {
    if (id == kBypassId) {
        const bool bypass = normalised >= 0.5;
        mBypass.store(bypass, std::memory_order_relaxed);
        mKernel->setBypass(bypass);
        return;
    }
    if (id == kPitchBendId) {
        mKernel->setPitchBend(std::clamp(normalised * 2.0 - 1.0, -1.0, 1.0));
        return;
    }
    const int index = params().indexOf(id);
    if (index < 0) {
        return;
    }
    const float plain = bdd::vst3::toPlain(params().specs[size_t(index)], normalised);
    mPlain[size_t(index)].store(plain, std::memory_order_relaxed);
    mKernel->setParameter(AUParameterAddress(id), plain);
}

void Processor::pushAllParameters() {
    // In table order, which is the AU tree's: the macros first, then the stages they don't own.
    const auto& specs = params().specs;
    for (size_t index = 0; index < specs.size(); ++index) {
        mKernel->setParameter(AUParameterAddress(specs[index].id), mPlain[index].load(std::memory_order_relaxed));
    }
    mKernel->setBypass(mBypass.load(std::memory_order_relaxed));
}

// MARK: - Processing

void Processor::readHostTiming(const ProcessContext* context) {
    if (context == nullptr) {
        // No transport information this block: keep the tempo, treat the song as stopped.
        mKernel->setHostTiming(0.0, 0.0, mSampleTime, false, 0.0, 0.0);
        return;
    }
    const bool hasTempo = (context->state & ProcessContext::kTempoValid) != 0;
    const bool hasPosition = (context->state & ProcessContext::kProjectTimeMusicValid) != 0;
    const bool hasSignature = (context->state & ProcessContext::kTimeSigValid) != 0;
    const bool playing = (context->state & ProcessContext::kPlaying) != 0 && hasPosition;
    mKernel->setHostTiming(hasTempo ? context->tempo : 0.0,             // 0 keeps the last tempo
                           hasPosition ? context->projectTimeMusic : 0.0,
                           mSampleTime,
                           playing,
                           hasSignature ? double(context->timeSigNumerator) : 0.0,
                           hasSignature ? double(context->timeSigDenominator) : 0.0);
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    if (mParametersNeedResync.exchange(false, std::memory_order_acq_rel)) {
        pushAllParameters();
    }

    // Gather every note and automation point, in time order, so the block can be split wherever
    // one lands. The kernel smooths its own parameters, so this only has to get each change to it
    // at the right sample.
    int pendingCount = 0;
    auto push = [&](const PendingEvent& event) {
        if (pendingCount < kMaxPendingEvents) {
            mPending[size_t(pendingCount++)] = event;
        }
    };

    if (IParameterChanges* changes = data.inputParameterChanges) {
        const int32 queueCount = changes->getParameterCount();
        for (int32 queueIndex = 0; queueIndex < queueCount; ++queueIndex) {
            IParamValueQueue* queue = changes->getParameterData(queueIndex);
            if (queue == nullptr) {
                continue;
            }
            const ParamID id = queue->getParameterId();
            const int32 pointCount = queue->getPointCount();
            for (int32 point = 0; point < pointCount; ++point) {
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint(point, offset, value) != kResultTrue) {
                    continue;
                }
                if (pendingCount == kMaxPendingEvents - 1 && point != pointCount - 1) {
                    continue;   // nearly out of room: keep the last slot for a final value
                }
                push({ offset, pendingCount, PendingEvent::parameter, id, value, 0 });
            }
        }
    }

    if (IEventList* events = data.inputEvents) {
        const int32 eventCount = events->getEventCount();
        for (int32 index = 0; index < eventCount; ++index) {
            Event event{};
            if (events->getEvent(index, event) != kResultOk) {
                continue;
            }
            if (event.type == Event::kNoteOnEvent) {
                // Velocity 0 is a release, as in MIDI.
                const int velocity = int(std::lround(std::clamp(event.noteOn.velocity, 0.0f, 1.0f) * 127.0f));
                push({ event.sampleOffset, pendingCount, velocity > 0 ? PendingEvent::noteOn : PendingEvent::noteOff, 0,
                       double(event.noteOn.pitch), velocity });
            } else if (event.type == Event::kNoteOffEvent) {
                push({ event.sampleOffset, pendingCount, PendingEvent::noteOff, 0, double(event.noteOff.pitch), 0 });
            }
        }
    }

    // By time, then by arrival, so two events at one offset keep their order. `std::sort` rather
    // than `std::stable_sort`, which may allocate.
    std::sort(mPending.begin(), mPending.begin() + pendingCount, [](const PendingEvent& a, const PendingEvent& b) {
        return a.offset != b.offset ? a.offset < b.offset : a.order < b.order;
    });

    auto apply = [this](const PendingEvent& event) {
        switch (event.kind) {
            case PendingEvent::parameter: applyParameter(event.id, event.value); break;
            case PendingEvent::noteOn: mKernel->noteOn(int(event.value), event.velocity); break;
            case PendingEvent::noteOff: mKernel->noteOff(int(event.value)); break;
        }
    };

    const bool hasOutput = data.numOutputs > 0 && data.numSamples > 0 && data.outputs[0].numChannels > 0;
    if (!hasOutput) {
        // A parameter flush, or nothing connected: take the values and go.
        for (int index = 0; index < pendingCount; ++index) {
            apply(mPending[size_t(index)]);
        }
        return kResultOk;
    }

    readHostTiming(data.processContext);

    AudioBusBuffers& output = data.outputs[0];
    const int channelCount = std::min(int(output.numChannels), 2);
    std::array<float*, 2> buffers{};

    int32 position = 0;
    int next = 0;
    while (position < data.numSamples) {
        while (next < pendingCount && mPending[size_t(next)].offset <= position) {
            apply(mPending[size_t(next)]);
            ++next;
        }
        const int32 end = (next < pendingCount)
            ? std::min(data.numSamples, std::max(mPending[size_t(next)].offset, position + 1))
            : data.numSamples;

        for (int channel = 0; channel < channelCount; ++channel) {
            buffers[size_t(channel)] = output.channelBuffers32[channel] + position;
        }
        mKernel->process(std::span<float*>(buffers.data(), size_t(channelCount)), mSampleTime + position,
                         uint32_t(end - position));
        position = end;
    }
    // Anything stamped past the end of the block, which a well-behaved host never sends.
    while (next < pendingCount) {
        apply(mPending[size_t(next)]);
        ++next;
    }

    for (int channel = channelCount; channel < output.numChannels; ++channel) {
        std::fill(output.channelBuffers32[channel], output.channelBuffers32[channel] + data.numSamples, 0.0f);
    }
    output.silenceFlags = 0;
    mSampleTime += data.numSamples;

    mSamplesSinceDisplay += data.numSamples;
    if (mSamplesSinceDisplay >= mDisplayInterval) {
        mSamplesSinceDisplay = 0;
        publishDisplay(data.outputParameterChanges);
    }
    return kResultOk;
}

void Processor::publishDisplay(IParameterChanges* outputs) {
    if (outputs == nullptr) {
        return;
    }
    auto send = [outputs](uint32_t id, double value) {
        int32 queueIndex = 0;
        if (IParamValueQueue* queue = outputs->addParameterData(id, queueIndex)) {
            int32 pointIndex = 0;
            queue->addPoint(0, displayRange(id).normalise(std::isfinite(value) ? value : 0.0), pointIndex);
        }
    };
    send(kDisplayPositionId, mKernel->envelopeDisplayPosition());
    send(kDisplayValueId, mKernel->envelopeDisplayValue());
    send(kDisplayCutoffId, mKernel->filterDisplayCutoffHertz());
    send(kDisplayQId, mKernel->filterDisplayQ());
    send(kDisplayCutoffTopId, mKernel->filterDisplayTopHertz());
    send(kDisplayCutoffBottomId, mKernel->filterDisplayBottomHertz());
    send(kDisplayMeterLeftId, mKernel->outputMeterLeft());
    send(kDisplayMeterRightId, mKernel->outputMeterRight());
}

// MARK: - Drawing

void Processor::publishDrawingLocked() {
    std::vector<float> xs, ys, bends;
    for (const auto& point : mDrawing) {
        xs.push_back(point.x);
        ys.push_back(point.y);
        bends.push_back(point.bend);
    }
    mKernel->setEnvelopeCurve(xs.data(), ys.data(), bends.data(), int(mDrawing.size()));
}

tresult PLUGIN_API Processor::notify(IMessage* message) {
    if (message == nullptr) {
        return kInvalidArgument;
    }
    if (FIDStringsEqual(message->getMessageID(), kLicenseChangedMessage)) {
        reloadLicense();
        return kResultOk;
    }
    if (FIDStringsEqual(message->getMessageID(), kDrawingMessage)) {
        const void* data = nullptr;
        uint32 size = 0;
        if (message->getAttributes()->getBinary(kDrawingAttribute, data, size) == kResultTrue && data != nullptr
            && size % (3 * sizeof(float)) == 0) {
            const auto* values = static_cast<const float*>(data);
            std::vector<bdd::CurvePoint> points;
            for (uint32 index = 0; index < size / (3 * sizeof(float)); ++index) {
                points.push_back({ values[index * 3], values[index * 3 + 1], values[index * 3 + 2] });
            }
            std::lock_guard<std::mutex> lock(mDrawingMutex);
            mDrawing = cleanDrawing(points.data(), int(points.size()));
            publishDrawingLocked();
        }
        return kResultOk;
    }
    return AudioEffect::notify(message);
}

// MARK: - State

bool Processor::readStateStream(IBStream* stream, State& out) {
    if (stream == nullptr) {
        return false;
    }
    std::string text;
    char buffer[4096];
    int32 read = 0;
    do {
        read = 0;
        if (stream->read(buffer, sizeof(buffer), &read) != kResultOk) {
            break;
        }
        text.append(buffer, size_t(std::max<int32>(read, 0)));
        if (int64(text.size()) > kMaxStateSize) {
            return false;
        }
    } while (read == int32(sizeof(buffer)));

    const auto result = bdd::vst3::readPreset(text, kSynthName);
    if (!result.preset) {
        return false;
    }
    out = stateFromPreset(*result.preset);
    return true;
}

bool Processor::writeStateStream(IBStream* stream, const State& state) {
    if (stream == nullptr) {
        return false;
    }
    const std::string text = bdd::vst3::writePreset(presetFromState(state, "", true));
    int32 written = 0;
    return stream->write(const_cast<char*>(text.data()), int32(text.size()), &written) == kResultOk
        && written == int32(text.size());
}

tresult PLUGIN_API Processor::setState(IBStream* stream) {
    State state;
    if (!readStateStream(stream, state)) {
        return kResultFalse;
    }
    for (size_t index = 0; index < mPlain.size(); ++index) {
        mPlain[index].store(state.plain[index], std::memory_order_relaxed);
    }
    mBypass.store(state.bypass, std::memory_order_relaxed);
    mParametersNeedResync.store(true, std::memory_order_release);

    std::lock_guard<std::mutex> lock(mDrawingMutex);
    mDrawing = state.drawing;
    publishDrawingLocked();
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* stream) {
    State state;
    for (const auto& value : mPlain) {
        state.plain.push_back(value.load(std::memory_order_relaxed));
    }
    state.bypass = mBypass.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mDrawingMutex);
        state.drawing = mDrawing;
    }
    return writeStateStream(stream, state) ? kResultOk : kResultFalse;
}

} // namespace yoi

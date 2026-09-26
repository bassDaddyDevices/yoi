//
//  YoiExtensionDSPKernel.hpp
//  YoiExtension
//
//  Created by Chris Connelly on 2026-09-26.
//

#pragma once

// The kernel is shared with the VST3 build and the DSP tests. Everything Audio Unit specific
// (render events, MIDI event lists and the musical context block) is only compiled where
// AudioToolbox is in use; those other builds define YOI_PORTABLE so they never depend on it.
#if defined(__APPLE__) && !defined(YOI_PORTABLE)
#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>
#define YOI_AUDIO_UNIT 1
#else
#define YOI_AUDIO_UNIT 0
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

#include "YoiExtensionParameterAddresses.h"
#include "Shared/BDDEnvelopes.hpp"
#include "Shared/BDDFilters.hpp"
#include "Shared/BDDMath.hpp"
#include "Shared/BDDMonoVoice.hpp"
#include "Shared/BDDOscillators.hpp"

/*
 YoiExtensionDSPKernel

 YOI's single voice. Signal flow per sample:

     held keys -> newest key -> glide + pitch bend -> pitch
     pitch -> main oscillator (saw <-> square) ----------------------------+
           -> sub oscillator (sine <-> triangle, one or two octaves down) --+-> mix
     mix -> state-variable filter (low-pass or band-pass) -> amp envelope
         -> output level -> soft limiter -> every output channel

 Later stages add the drawn envelope on the filter cutoff (stage 2), the wavefolder, downsampler
 and clean-up filter (stage 3), and the vowel filter (stage 4).

 Parameters only ever store their new target here, from whichever thread sets them. Anything
 derived from them (envelope coefficients, smoothed values) is recalculated on the render thread,
 so the audio never reads half-updated state.

 As a non-ObjC class, this is safe to use from the render thread.
 */
class YoiExtensionDSPKernel {
public:
    void initialize(int channelCount, double inSampleRate) {
        (void)channelCount;
        mSampleRate = inSampleRate;

        mAmpEnvelope.setSampleRate(inSampleRate);
        mAmpEnvelope.reset();
        mAppliedAttack = mAppliedDecay = mAppliedSustain = mAppliedRelease = -1.0f;
        applyEnvelopeSettings();

        for (auto* smoother : { &mOscShapeSmoother, &mSubLevelSmoother, &mSubShapeSmoother,
                                &mCutoffSmoother, &mResonanceSmoother, &mFilterModeSmoother,
                                &mOutputSmoother }) {
            smoother->setTimeConstant(kSmoothingSeconds, inSampleRate);
        }
        mBendSmoother.setTimeConstant(kBendSmoothingSeconds, inSampleRate);
        snapSmoothers();

        mHeldNotes.clear();
        mGlide.jump(60.0);
        mHasPlayed = false;
        mMainOscillator.reset();
        mSubOscillator.reset();
        mFilter.reset();
    }

    void deInitialize() {
    }

    // MARK: - Bypass
    bool isBypassed() {
        return mBypassed;
    }

    void setBypass(bool shouldBypass) {
        mBypassed = shouldBypass;
    }

    // MARK: - Parameter Getter / Setter
    // Defaults below must match Parameters.swift, which is what hosts see.
    void setParameter(AUParameterAddress address, AUValue value) {
        switch (address) {
            case YoiExtensionParameterAddress::outputLevel:
                mOutputDecibels = value;
                break;
            case YoiExtensionParameterAddress::glideTime:
                mGlideMilliseconds = std::max(0.0f, value);
                break;
            case YoiExtensionParameterAddress::glideMode:
                mGlideMode = int(std::lround(value));
                break;
            case YoiExtensionParameterAddress::bendRange:
                mBendRange = std::clamp(value, 0.0f, 24.0f);
                break;
            case YoiExtensionParameterAddress::oscShape:
                mOscShape = std::clamp(value * 0.01f, 0.0f, 1.0f);
                break;
            case YoiExtensionParameterAddress::subLevel:
                mSubLevel = std::clamp(value * 0.01f, 0.0f, 1.0f);
                break;
            case YoiExtensionParameterAddress::subShape:
                mSubShape = std::clamp(value * 0.01f, 0.0f, 1.0f);
                break;
            case YoiExtensionParameterAddress::subOctave:
                mSubOctave = std::clamp(int(std::lround(value)), 0, 1);
                break;
            case YoiExtensionParameterAddress::filterMode:
                mFilterMode = std::clamp(int(std::lround(value)), 0, 1);
                break;
            case YoiExtensionParameterAddress::cutoff:
                mCutoffHertz = std::clamp(value, 20.0f, 20000.0f);
                break;
            case YoiExtensionParameterAddress::resonance:
                mResonance = std::clamp(value * 0.01f, 0.0f, 1.0f);
                break;
            case YoiExtensionParameterAddress::ampAttack:
                mAttackMilliseconds = std::max(0.0f, value);
                break;
            case YoiExtensionParameterAddress::ampDecay:
                mDecayMilliseconds = std::max(0.0f, value);
                break;
            case YoiExtensionParameterAddress::ampSustain:
                mSustain = std::clamp(value * 0.01f, 0.0f, 1.0f);
                break;
            case YoiExtensionParameterAddress::ampRelease:
                mReleaseMilliseconds = std::max(0.0f, value);
                break;
        }
    }

    AUValue getParameter(AUParameterAddress address) {
        // Return the goal. It is not thread safe to return the ramping value.
        switch (address) {
            case YoiExtensionParameterAddress::outputLevel: return mOutputDecibels;
            case YoiExtensionParameterAddress::glideTime: return mGlideMilliseconds;
            case YoiExtensionParameterAddress::glideMode: return AUValue(mGlideMode);
            case YoiExtensionParameterAddress::bendRange: return mBendRange;
            case YoiExtensionParameterAddress::oscShape: return mOscShape * 100.0f;
            case YoiExtensionParameterAddress::subLevel: return mSubLevel * 100.0f;
            case YoiExtensionParameterAddress::subShape: return mSubShape * 100.0f;
            case YoiExtensionParameterAddress::subOctave: return AUValue(mSubOctave);
            case YoiExtensionParameterAddress::filterMode: return AUValue(mFilterMode);
            case YoiExtensionParameterAddress::cutoff: return mCutoffHertz;
            case YoiExtensionParameterAddress::resonance: return mResonance * 100.0f;
            case YoiExtensionParameterAddress::ampAttack: return mAttackMilliseconds;
            case YoiExtensionParameterAddress::ampDecay: return mDecayMilliseconds;
            case YoiExtensionParameterAddress::ampSustain: return mSustain * 100.0f;
            case YoiExtensionParameterAddress::ampRelease: return mReleaseMilliseconds;
            default: return 0.f;
        }
    }

    // MARK: - Max Frames
    uint32_t maximumFramesToRender() const {
        return mMaxFramesToRender;
    }

    void setMaximumFramesToRender(const uint32_t &maxFrames) {
        mMaxFramesToRender = maxFrames;
    }

    // MARK: - Notes
    // The format-neutral way in: the Audio Unit's MIDI handling below, the VST3 build and the
    // tests all come through these.

    /// Glide modes, as stored in the `glideMode` parameter.
    enum GlideMode : int { glideLegato = 0, glideAlways = 1 };

    /// A key went down. Velocity is accepted for the future but does not shape the sound yet.
    void noteOn(int note, int velocity) {
        (void)velocity;
        const bool legato = !mHeldNotes.empty();
        mHeldNotes.push(note);
        startNote(note, legato);
    }

    /// A key came up. If it was the sounding key and others are still held, the voice slides back
    /// to the most recent of those without restarting its envelope.
    void noteOff(int note) {
        const bool wasSounding = (mHeldNotes.newest() == note);
        mHeldNotes.remove(note);
        if (mHeldNotes.empty()) {
            mAmpEnvelope.gateOff();
        } else if (wasSounding) {
            startNote(mHeldNotes.newest(), true);
        }
    }

    /// Releases every key, letting the sound fade out (MIDI "all notes off").
    void allNotesOff() {
        mHeldNotes.clear();
        mAmpEnvelope.gateOff();
    }

    /// Silences the voice at once (MIDI "all sound off").
    void allSoundOff() {
        mHeldNotes.clear();
        mAmpEnvelope.reset();
    }

    /// Pitch bend wheel position, -1...1. How far that bends is the `bendRange` parameter.
    void setPitchBend(double bipolar) {
        mBendPosition = float(std::clamp(bipolar, -1.0, 1.0));
    }

    /// True while the voice is making sound, including its release tail.
    bool isSounding() const {
        return mAmpEnvelope.isActive();
    }

    /// The pitch the voice is playing or gliding through, as a MIDI note number, before pitch
    /// bend. Read by the tests, and later by the editor.
    double currentPitch() const {
        return mGlide.current;
    }

#if YOI_AUDIO_UNIT
    // MARK: - Musical Context
    void setMusicalContextBlock(AUHostMusicalContextBlock contextBlock) {
        mMusicalContextBlock = contextBlock;
    }

    // MARK: - MIDI Protocol
    MIDIProtocolID AudioUnitMIDIProtocol() const {
        return kMIDIProtocol_2_0;
    }
#endif

    /**
     MARK: - Internal Process

     Renders `frameCount` samples of the voice into every output buffer.
     */
    void process(std::span<float *> outputBuffers, int64_t bufferStartTime, uint32_t frameCount) {
        (void)bufferStartTime;

        applyEnvelopeSettings();

        if (mBypassed || !mAmpEnvelope.isActive()) {
            // Nothing is sounding, so there is nothing to smooth towards either: settle every
            // control where it was left, so the next note starts exactly on its settings.
            snapSmoothers();
            for (auto* buffer : outputBuffers) {
                std::fill_n(buffer, frameCount, 0.f);
            }
            return;
        }

        const double subRatio = (mSubOctave == 0) ? 0.5 : 0.25;
        const float cutoffTarget = std::log2(mCutoffHertz);
        const float outputTarget = bdd::decibelsToGain(mOutputDecibels);
        const float filterModeTarget = float(mFilterMode);

        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            const float mainMorph = mOscShapeSmoother.next(mOscShape);
            const float subGain = mSubLevelSmoother.next(mSubLevel);
            const float subMorph = mSubShapeSmoother.next(mSubShape);
            const float cutoffOctaves = mCutoffSmoother.next(cutoffTarget);
            const float resonanceAmount = mResonanceSmoother.next(mResonance);
            const float bandPassAmount = mFilterModeSmoother.next(filterModeTarget);
            const float outputGain = mOutputSmoother.next(outputTarget);
            const float bend = mBendSmoother.next(mBendPosition);

            const double pitch = mGlide.next() + double(bend * mBendRange);
            const double increment = bdd::noteToHertz(pitch) / mSampleRate;

            const float main = mMainOscillator.next(increment, mainMorph);
            const float sub = mSubOscillator.next(increment * subRatio, subMorph);
            const double mix = kOscillatorHeadroom * double(main + subGain * sub);

            mFilter.setCoefficients(std::exp2(double(cutoffOctaves)),
                                    bdd::StateVariableFilter::qForResonance(resonanceAmount),
                                    mSampleRate);
            const auto filtered = mFilter.process(mix);
            const double voice = filtered.lowPass + (filtered.bandPass - filtered.lowPass) * double(bandPassAmount);

            const double amp = mAmpEnvelope.next();
            const float sample = bdd::softLimit(float(voice * amp) * outputGain);

            for (auto* buffer : outputBuffers) {
                buffer[frameIndex] = sample;
            }
        }
    }

#if YOI_AUDIO_UNIT
    void handleOneEvent(AUEventSampleTime now, AURenderEvent const *event) {
        switch (event->head.eventType) {
            case AURenderEventParameter: {
                handleParameterEvent(now, event->parameter);
                break;
            }

            case AURenderEventMIDI: {
                // Hosts that don't use MIDI event lists still send plain MIDI 1.0 bytes.
                const auto& midi = event->MIDI;
                handleMIDI1Bytes(midi.data[0], midi.length > 1 ? midi.data[1] : 0, midi.length > 2 ? midi.data[2] : 0);
                break;
            }

            case AURenderEventMIDIEventList: {
                handleMIDIEventList(now, &event->MIDIEventsList);
                break;
            }

            default:
                break;
        }
    }

    void handleParameterEvent(AUEventSampleTime now, AUParameterEvent const& parameterEvent) {
        (void)now;
        setParameter(parameterEvent.parameterAddress, parameterEvent.value);
    }

    void handleMIDIEventList(AUEventSampleTime now, AUMIDIEventList const* midiEvent) {
        (void)now;
        auto visitor = [] (void* context, MIDITimeStamp timeStamp, MIDIUniversalMessage message) {
            (void)timeStamp;
            auto thisObject = static_cast<YoiExtensionDSPKernel *>(context);

            switch (message.type) {
                case kMIDIMessageTypeChannelVoice2:
                    thisObject->handleMIDI2VoiceMessage(message);
                    break;

                case kMIDIMessageTypeChannelVoice1:
                    thisObject->handleMIDI1VoiceMessage(message);
                    break;

                default:
                    break;
            }
        };

        MIDIEventListForEachEvent(&midiEvent->eventList, visitor, this);
    }

    void handleMIDI2VoiceMessage(const struct MIDIUniversalMessage& message) {
        const auto& voice = message.channelVoice2;

        switch (voice.status) {
            case kMIDICVStatusNoteOff:
                noteOff(voice.note.number);
                break;

            case kMIDICVStatusNoteOn:
                // In MIDI 2.0 a velocity of zero is still a note-on.
                noteOn(voice.note.number, voice.note.velocity >> 9);
                break;

            case kMIDICVStatusPitchBend:
                setPitchBend((double(voice.pitchBend.data) - 2147483648.0) / 2147483648.0);
                break;

            case kMIDICVStatusControlChange:
                handleControlChange(voice.controlChange.index);
                break;

            default:
                break;
        }
    }

    void handleMIDI1VoiceMessage(const struct MIDIUniversalMessage& message) {
        const auto& voice = message.channelVoice1;

        switch (voice.status) {
            case kMIDICVStatusNoteOff:
                noteOff(voice.note.number);
                break;

            case kMIDICVStatusNoteOn:
                if (voice.note.velocity == 0) {
                    noteOff(voice.note.number);
                } else {
                    noteOn(voice.note.number, voice.note.velocity);
                }
                break;

            case kMIDICVStatusPitchBend:
                setPitchBend((double(voice.pitchBend) - 8192.0) / 8192.0);
                break;

            case kMIDICVStatusControlChange:
                handleControlChange(voice.controlChange.index);
                break;

            default:
                break;
        }
    }

    void handleMIDI1Bytes(uint8_t status, uint8_t data1, uint8_t data2) {
        switch (status & 0xF0) {
            case 0x80:
                noteOff(data1);
                break;
            case 0x90:
                if (data2 == 0) {
                    noteOff(data1);
                } else {
                    noteOn(data1, data2);
                }
                break;
            case 0xB0:
                handleControlChange(data1);
                break;
            case 0xE0:
                setPitchBend((double(int(data2) << 7 | int(data1)) - 8192.0) / 8192.0);
                break;
            default:
                break;
        }
    }
#endif

    void handleControlChange(int controller) {
        switch (controller) {
            case 120: allSoundOff(); break;
            case 123: allNotesOff(); break;
            default: break;
        }
    }

private:
    /// How quickly knob moves settle, so automation and dragging never zipper.
    static constexpr double kSmoothingSeconds = 0.015;
    /// Pitch bend arrives in steps; this is just enough to hide them without feeling late.
    static constexpr double kBendSmoothingSeconds = 0.005;
    /// Scales the oscillator mix so that the default sound peaks around -7 dBFS (about -15 dBFS
    /// RMS). That leaves room for resonance, which can add 10 dB or more at a harmonic, before
    /// the limiter's knee at -4.4 dBFS, so the limiter only catches genuinely extreme settings.
    static constexpr double kOscillatorHeadroom = 0.25;

    void startNote(int note, bool legato) {
        const bool glides = mHasPlayed
                         && mGlideMilliseconds > 0.0f
                         && (legato || mGlideMode == glideAlways);
        if (glides) {
            mGlide.glideTo(double(note), double(mGlideMilliseconds) * 0.001, mSampleRate);
        } else {
            mGlide.jump(double(note));
        }
        mHasPlayed = true;

        if (!legato) {
            // Restart the waveforms only from silence. Resetting them under a release tail would
            // cut the waveform mid-cycle and click.
            if (!mAmpEnvelope.isActive()) {
                mMainOscillator.reset();
                mSubOscillator.reset();
            }
            mAmpEnvelope.gateOn();
        }
    }

    /// Recalculates envelope coefficients when their settings have changed. Runs on the render
    /// thread, once per block, so the envelope never sees a half-written update.
    void applyEnvelopeSettings() {
        if (mAttackMilliseconds != mAppliedAttack) {
            mAppliedAttack = mAttackMilliseconds;
            mAmpEnvelope.setAttack(double(mAttackMilliseconds) * 0.001);
        }
        if (mSustain != mAppliedSustain) {
            mAppliedSustain = mSustain;
            mAmpEnvelope.setSustain(double(mSustain));
        }
        if (mDecayMilliseconds != mAppliedDecay) {
            mAppliedDecay = mDecayMilliseconds;
            mAmpEnvelope.setDecay(double(mDecayMilliseconds) * 0.001);
        }
        if (mReleaseMilliseconds != mAppliedRelease) {
            mAppliedRelease = mReleaseMilliseconds;
            mAmpEnvelope.setRelease(double(mReleaseMilliseconds) * 0.001);
        }
    }

    void snapSmoothers() {
        mOscShapeSmoother.snap(mOscShape);
        mSubLevelSmoother.snap(mSubLevel);
        mSubShapeSmoother.snap(mSubShape);
        mCutoffSmoother.snap(std::log2(mCutoffHertz));
        mResonanceSmoother.snap(mResonance);
        mFilterModeSmoother.snap(float(mFilterMode));
        mOutputSmoother.snap(bdd::decibelsToGain(mOutputDecibels));
        mBendSmoother.snap(mBendPosition);
    }

    // MARK: - Member Variables
#if YOI_AUDIO_UNIT
    AUHostMusicalContextBlock mMusicalContextBlock = nullptr;
#endif

    double mSampleRate = 44100.0;
    bool mBypassed = false;
    uint32_t mMaxFramesToRender = 1024;

    // Parameter targets, in the kernel's own units (fractions rather than percent).
    float mOutputDecibels = 0.0f;
    float mGlideMilliseconds = 60.0f;
    int mGlideMode = glideLegato;
    float mBendRange = 2.0f;
    float mOscShape = 0.0f;
    float mSubLevel = 0.5f;
    float mSubShape = 0.0f;
    int mSubOctave = 0;
    int mFilterMode = 0;
    float mCutoffHertz = 800.0f;
    float mResonance = 0.3f;
    float mAttackMilliseconds = 3.0f;
    float mDecayMilliseconds = 300.0f;
    float mSustain = 1.0f;
    float mReleaseMilliseconds = 150.0f;

    float mBendPosition = 0.0f;

    // The envelope settings last handed to the envelope, to spot changes.
    float mAppliedAttack = -1.0f;
    float mAppliedDecay = -1.0f;
    float mAppliedSustain = -1.0f;
    float mAppliedRelease = -1.0f;

    bdd::Smoother mOscShapeSmoother;
    bdd::Smoother mSubLevelSmoother;
    bdd::Smoother mSubShapeSmoother;
    bdd::Smoother mCutoffSmoother;
    bdd::Smoother mResonanceSmoother;
    bdd::Smoother mFilterModeSmoother;
    bdd::Smoother mOutputSmoother;
    bdd::Smoother mBendSmoother;

    bdd::NoteStack mHeldNotes;
    bdd::Glide mGlide;
    bool mHasPlayed = false;

    bdd::MorphOscillator mMainOscillator;
    bdd::SubOscillator mSubOscillator;
    bdd::StateVariableFilter mFilter;
    bdd::ADSREnvelope mAmpEnvelope;
};

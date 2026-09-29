//
//  YoiExtensionDSPKernel.hpp
//  YoiExtension
//
//  Created by Chris Connelly on 2026-09-26.
//

#pragma once

// The kernel is shared with the VST3 build and the DSP tests. Everything Audio Unit specific
// (render events, MIDI event lists and the host's musical context and transport blocks) is only
// compiled where AudioToolbox is in use; those other builds define YOI_PORTABLE so they never
// depend on it.
#if defined(__APPLE__) && !defined(YOI_PORTABLE)
#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>
#include "Shared/BDDRetainedBlock.hpp"
#define YOI_AUDIO_UNIT 1
#else
#define YOI_AUDIO_UNIT 0
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>

#include "YoiExtensionParameterAddresses.h"
#include "YoiFactoryShapes.hpp"
#include "Shared/BDDAtomics.hpp"
#include "Shared/BDDDrawnCurve.hpp"
#include "Shared/BDDDownsamplers.hpp"
#include "Shared/BDDDrawnEnvelope.hpp"
#include "Shared/BDDDynamics.hpp"
#include "Shared/BDDEnvelopes.hpp"
#include "Shared/BDDFilters.hpp"
#include "Shared/BDDMath.hpp"
#include "Shared/BDDMonoVoice.hpp"
#include "Shared/BDDOscillators.hpp"
#include "Shared/BDDShapers.hpp"
#include "Shared/BDDSpatial.hpp"

/*
 YoiExtensionDSPKernel

 YOI's single voice. Signal flow per sample:

     held keys -> newest key -> glide + pitch bend -> pitch
     pitch -> main oscillator (saw <-> square) -> high-pass at X-OVER
         -> [wavefolder, pre-filter] -> state-variable filter (low-pass or band-pass)
         -> [wavefolder, pre-downsample] -> downsampler (S&H or Downsample)
         -> [wavefolder, post-downsample, uneven] -> clean-up low-pass (a multiple of the moving
            cutoff) -> harmonic booster -> amp envelope -> OTT-style compressor
         -> dimension expander (mono in, stereo out) --------------------------------+
     pitch -> sub oscillator (sine <-> triangle, one or two octaves down)            |
         -> low-pass at X-OVER -> amp envelope (the sub skips everything above) -----+-> merge
     merge -> output level -> soft limiter -> left and right

     drawn envelope: clock (Sync to the host, or Free) -> direction -> read the drawing (0...1)
         -> filter cutoff = CUTOFF lowered by Amount x (1 - drawing), in octaves

 The downsampler sits straight after the resonant filter on purpose: it folds the filter's
 resonant peak back down into throaty, vocal tones, which is where YOI gets its sound. The
 wavefolder adds harmonics before the filter, before the downsampler or (the default) after it:
 the more harmonics going in, the more bite comes out.

 Parameters only ever store their new target here, from whichever thread sets them. Anything
 derived from them (envelope coefficients, smoothed values) is recalculated on the render thread,
 so the audio never reads half-updated state. The drawing arrives the same way, through a
 lock-free exchange.

 As a non-ObjC class, this is safe to use from the render thread.
 */
class YoiExtensionDSPKernel {
public:
    YoiExtensionDSPKernel() {
        // A new instance starts on the first factory drawing, already in place for the first block.
        loadFactoryShape(0);
        mCurrentTable = mCurveExchange.shared;
        mPreviousTable = mCurrentTable;
        mObservedCurveSequence = mCurveExchange.sequence;
    }

    void initialize(int channelCount, double inSampleRate) {
        (void)channelCount;
        mSampleRate = inSampleRate;

        mAmpEnvelope.setSampleRate(inSampleRate);
        mAmpEnvelope.reset();
        mAppliedAttack = mAppliedDecay = mAppliedSustain = mAppliedRelease = -1.0f;
        applyAmpEnvelopeSettings();

        for (auto* smoother : { &mOscShapeSmoother, &mSubLevelSmoother, &mSubShapeSmoother,
                                &mCutoffSmoother, &mResonanceSmoother, &mFilterModeSmoother,
                                &mOutputSmoother, &mEnvAmountSmoother, &mAccelStartSmoother,
                                &mAccelEndSmoother, &mAccelCurveSmoother, &mFoldAmountSmoother,
                                &mCleanupMultipleSmoother, &mSubCrossoverSmoother, &mBoostSmoother,
                                &mOttDepthSmoother, &mWidthSmoother }) {
            smoother->setTimeConstant(kSmoothingSeconds, inSampleRate);
        }
        mFoldLevel.setTimeConstant(kFoldLevelSeconds, inSampleRate);
        mBoostLevel.setTimeConstant(kFoldLevelSeconds, inSampleRate);
        mFoldDC.setSampleRate(inSampleRate);
        mOtt.setSampleRate(inSampleRate);
        mOttWeight.setDuration(kModeCrossfadeSeconds, inSampleRate);
        mDimension.setSampleRate(inSampleRate);
        mFoldPositionBlend.setDuration(kModeCrossfadeSeconds, inSampleRate);
        mCleanupWeight.setDuration(kModeCrossfadeSeconds, inSampleRate);
        mSampleHoldWeight.setDuration(kModeCrossfadeSeconds, inSampleRate);
        mDownsampleWeight.setDuration(kModeCrossfadeSeconds, inSampleRate);
        mBendSmoother.setTimeConstant(kBendSmoothingSeconds, inSampleRate);
        snapSmoothers();

        mHeldNotes.clear();
        mGlide.jump(60.0);
        mHasPlayed = false;
        mMainOscillator.reset();
        mSubOscillator.reset();
        mFilter.reset();
        mSampleAndHold.reset();
        mSampleCountDownsampler.reset();
        mWavefolder.reset();
        mFoldLevel.reset();
        mCleanupFilter.reset();
        mMainHighPass.reset();
        mSubLowPass.reset();
        resetFinish();
        mAppliedCrossoverOctaves = -1.0f;
        mActiveFoldPosition = mFoldPosition;

        mEnvelopeClock = bdd::EnvelopeClock();
        mCurveFade = 1.0f;
        mCurveFadeStep = float(1.0 / (kCurveCrossfadeSeconds * inSampleRate));
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
            case YoiExtensionParameterAddress::subCrossover:
                mSubCrossoverHertz = std::clamp(value, 50.0f, 700.0f);
                break;
            case YoiExtensionParameterAddress::filterMode:
                mFilterMode = std::clamp(int(std::lround(value)), 0, 1);
                break;
            case YoiExtensionParameterAddress::cutoff:
                mCutoffHertz = std::clamp(value, 20.0f, 2500.0f);
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
            case YoiExtensionParameterAddress::envAmount:
                mEnvAmount = std::clamp(value, 0.0f, 8.0f);
                break;
            case YoiExtensionParameterAddress::envTimeMode:
                mEnvTimeMode = std::clamp(int(std::lround(value)), 0, 1);
                break;
            case YoiExtensionParameterAddress::envSyncLength:
                mEnvSyncLength = std::clamp(int(std::lround(value)), 0, int(bdd::kSyncLengths.size()) - 1);
                break;
            case YoiExtensionParameterAddress::envFreeTime:
                mEnvFreeMilliseconds = std::clamp(value, 10.0f, 30000.0f);
                break;
            case YoiExtensionParameterAddress::envDirection:
                mEnvDirection = std::clamp(int(std::lround(value)), 0, bdd::kEnvelopeDirectionCount - 1);
                break;
            case YoiExtensionParameterAddress::envRetrigger:
                mEnvRetrigger = std::clamp(int(std::lround(value)), 0, 1);
                break;
            case YoiExtensionParameterAddress::accelStart:
                mAccelStart = std::clamp(value, 0.1f, 4.0f);
                break;
            case YoiExtensionParameterAddress::accelEnd:
                mAccelEnd = std::clamp(value, 0.1f, 4.0f);
                break;
            case YoiExtensionParameterAddress::accelCurve:
                mAccelCurve = std::clamp(value, -1.0f, 1.0f);
                break;
            case YoiExtensionParameterAddress::dsMode:
                mDownsampleMode = std::clamp(int(std::lround(value)), 0, 2);
                break;
            case YoiExtensionParameterAddress::dsRate:
                mSampleHoldRate = std::clamp(value, 1300.0f, 6000.0f);
                break;
            case YoiExtensionParameterAddress::dsAmount:
                mDownsampleAmount = std::clamp(value, 0.0f, 100.0f);
                break;
            case YoiExtensionParameterAddress::foldAmount:
                mFoldAmount = std::clamp(value * 0.01f, 0.0f, 1.0f);
                break;
            case YoiExtensionParameterAddress::foldPosition:
                mFoldPosition = std::clamp(int(std::lround(value)), 0, 2);
                break;
            case YoiExtensionParameterAddress::boostAmount:
                mBoostAmount = std::clamp(value * 0.01f, 0.0f, 1.0f);
                break;
            case YoiExtensionParameterAddress::ottDepth:
                mOttDepth = std::clamp(value * 0.01f, 0.0f, 1.0f);
                break;
            case YoiExtensionParameterAddress::widthAmount:
                mWidthAmount = std::clamp(value * 0.01f, 0.0f, 1.0f);
                break;
            case YoiExtensionParameterAddress::cleanupMode:
                mCleanupMode = std::clamp(int(std::lround(value)), 0, 1);
                break;
            case YoiExtensionParameterAddress::cleanupMultiple:
                mCleanupMultiple = std::clamp(value, 1.0f, 16.0f);
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
            case YoiExtensionParameterAddress::subCrossover: return mSubCrossoverHertz;
            case YoiExtensionParameterAddress::filterMode: return AUValue(mFilterMode);
            case YoiExtensionParameterAddress::cutoff: return mCutoffHertz;
            case YoiExtensionParameterAddress::resonance: return mResonance * 100.0f;
            case YoiExtensionParameterAddress::ampAttack: return mAttackMilliseconds;
            case YoiExtensionParameterAddress::ampDecay: return mDecayMilliseconds;
            case YoiExtensionParameterAddress::ampSustain: return mSustain * 100.0f;
            case YoiExtensionParameterAddress::ampRelease: return mReleaseMilliseconds;
            case YoiExtensionParameterAddress::envAmount: return mEnvAmount;
            case YoiExtensionParameterAddress::envTimeMode: return AUValue(mEnvTimeMode);
            case YoiExtensionParameterAddress::envSyncLength: return AUValue(mEnvSyncLength);
            case YoiExtensionParameterAddress::envFreeTime: return mEnvFreeMilliseconds;
            case YoiExtensionParameterAddress::envDirection: return AUValue(mEnvDirection);
            case YoiExtensionParameterAddress::envRetrigger: return AUValue(mEnvRetrigger);
            case YoiExtensionParameterAddress::accelStart: return mAccelStart;
            case YoiExtensionParameterAddress::accelEnd: return mAccelEnd;
            case YoiExtensionParameterAddress::accelCurve: return mAccelCurve;
            case YoiExtensionParameterAddress::dsMode: return AUValue(mDownsampleMode);
            case YoiExtensionParameterAddress::dsRate: return mSampleHoldRate;
            case YoiExtensionParameterAddress::dsAmount: return mDownsampleAmount;
            case YoiExtensionParameterAddress::foldAmount: return mFoldAmount * 100.0f;
            case YoiExtensionParameterAddress::foldPosition: return AUValue(mFoldPosition);
            case YoiExtensionParameterAddress::boostAmount: return mBoostAmount * 100.0f;
            case YoiExtensionParameterAddress::ottDepth: return mOttDepth * 100.0f;
            case YoiExtensionParameterAddress::widthAmount: return mWidthAmount * 100.0f;
            case YoiExtensionParameterAddress::cleanupMode: return AUValue(mCleanupMode);
            case YoiExtensionParameterAddress::cleanupMultiple: return mCleanupMultiple;
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

    /// Envelope time modes, as stored in the `envTimeMode` parameter.
    enum EnvelopeTimeMode : int { envelopeSync = 0, envelopeFree = 1 };

    /// Downsampler modes, as stored in the `dsMode` parameter.
    enum DownsampleMode : int { downsampleOff = 0, downsampleSampleHold = 1, downsampleCount = 2 };

    /// Where the wavefolder sits, as stored in the `foldPosition` parameter.
    enum FoldPosition : int { foldPreFilter = 0, foldPreDownsample = 1, foldPostDownsample = 2 };

    /// A key went down. Velocity is accepted for the future but does not shape the sound yet.
    void noteOn(int note, int velocity) {
        (void)velocity;
        const bool legato = !mHeldNotes.empty();
        mHeldNotes.push(note);
        startNote(note, legato);
    }

    /// A key came up. If it was the sounding key and others are still held, the voice slides back
    /// to the most recent of those without restarting its envelopes.
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

    // MARK: - Host timing

    /// Tells the kernel where the host's song position was at `sampleTime`, in quarter notes, and
    /// whether its transport is running. The Audio Unit calls this once per render cycle from the
    /// host's blocks; the VST3 build will call it from its process context. Until it is called the
    /// kernel assumes 120 BPM in 4/4 with the transport stopped.
    void setHostTiming(double tempo, double beatPosition, int64_t sampleTime, bool playing,
                       double numerator, double denominator) {
        if (std::isfinite(tempo) && tempo > 0.0) {
            mHostTempo = std::clamp(tempo, 10.0, 999.0);
        }
        mHostBeat = std::isfinite(beatPosition) ? beatPosition : 0.0;
        mHostSampleTime = sampleTime;
        mHostPlaying = playing;
        if (numerator > 0.0 && denominator > 0.0) {
            mHostNumerator = numerator;
            mHostDenominator = denominator;
        }
    }

    // MARK: - Drawn envelope curve
    // Called from the thread that edits the drawing (the UI, or state restore). Rendering happens
    // on that thread; the render thread only ever picks up the finished table.

    /// Publishes a new drawing: `count` points given as separate x, y and bend arrays (`bends` may
    /// be null). The points are cleaned up first, so any input is safe.
    void setEnvelopeCurve(const float* xs, const float* ys, const float* bends, int count) {
        std::array<bdd::CurvePoint, bdd::kMaxCurvePoints> points{};
        const int usable = (xs != nullptr && ys != nullptr) ? std::clamp(count, 0, bdd::kMaxCurvePoints) : 0;
        for (int i = 0; i < usable; ++i) {
            points[size_t(i)] = { xs[i], ys[i], (bends != nullptr) ? bends[i] : 0.0f };
        }
        publishEnvelopeCurve(points.data(), usable);
    }

    /// Option names for the sync-length and direction parameters, so the parameter tree reads them
    /// from the one list the DSP uses.
    static int syncLengthCount() { return int(bdd::kSyncLengths.size()); }
    static const char* syncLengthName(int index) {
        return bdd::kSyncLengths[size_t(std::clamp(index, 0, syncLengthCount() - 1))].name;
    }
    static int directionCount() { return bdd::kEnvelopeDirectionCount; }
    static const char* directionName(int index) {
        static constexpr const char* names[bdd::kEnvelopeDirectionCount] = {
            "Forward", "Backward", "Pingpong", "Sine", "Random", "Accelerate"
        };
        return names[std::clamp(index, 0, bdd::kEnvelopeDirectionCount - 1)];
    }

    static int factoryShapeCount() {
        return int(yoi::kFactoryShapes.size());
    }

    static const char* factoryShapeName(int index) {
        return yoi::kFactoryShapes[size_t(std::clamp(index, 0, factoryShapeCount() - 1))].name;
    }

    void loadFactoryShape(int index) {
        const auto& shape = yoi::kFactoryShapes[size_t(std::clamp(index, 0, factoryShapeCount() - 1))];
        publishEnvelopeCurve(shape.points.data(), shape.count);
    }

    /// The drawing as last published, after clean-up: what to save, and what an editor shows.
    int envelopePointCount() const { return mPublishedCount; }
    float envelopePointX(int index) const { return publishedPoint(index).x; }
    float envelopePointY(int index) const { return publishedPoint(index).y; }
    float envelopePointBend(int index) const { return publishedPoint(index).bend; }

    /// Samples the rendered drawing into `destination` (`count` values across 0...1), so an editor
    /// can draw exactly what the envelope plays without repeating its maths.
    void copyEnvelopeTable(float* destination, int count) const {
        for (int i = 0; destination != nullptr && i < count; ++i) {
            const double x = (count > 1) ? double(i) / double(count - 1) : 0.0;
            destination[i] = bdd::lookup(mPublishScratch.data(), bdd::kCurveTableSize, x);
        }
    }

    /// Where the envelope was reading in the drawing (0...1) and the value it read, at the end of
    /// the last block. For the editor's playhead; safe to call from any thread.
    float envelopeDisplayPosition() const {
        return std::bit_cast<float>(bdd::atomics::loadRelaxed(const_cast<uint32_t&>(mDisplayPositionBits)));
    }

    float envelopeDisplayValue() const {
        return std::bit_cast<float>(bdd::atomics::loadRelaxed(const_cast<uint32_t&>(mDisplayValueBits)));
    }

#if YOI_AUDIO_UNIT
    // MARK: - Musical Context

    /// Takes the host's musical context and transport state blocks from the audio unit, holding
    /// its own references to them. Call from allocateRenderResources, after the host has set them.
    /// Reading them here, rather than having Swift pass them in, matters: Swift would hand over a
    /// temporary wrapper that is freed as soon as the call returns (see BDDRetainedBlock.hpp).
    void captureHostBlocks(AUAudioUnit* audioUnit) {
        mMusicalContextBlock.reset(audioUnit != nil ? BDD_BLOCK_AS_POINTER(audioUnit.musicalContextBlock) : nullptr);
        mTransportStateBlock.reset(audioUnit != nil ? BDD_BLOCK_AS_POINTER(audioUnit.transportStateBlock) : nullptr);
    }

    /// Drops the host blocks. Call from deallocateRenderResources, when rendering has stopped.
    void releaseHostBlocks() {
        mMusicalContextBlock.reset(nullptr);
        mTransportStateBlock.reset(nullptr);
    }

    /// Asks the host for tempo, song position and transport state. Called once at the start of
    /// every render cycle, before any events or audio in it.
    void beginRenderCycle(int64_t sampleTime) {
        double tempo = mHostTempo;
        double numerator = mHostNumerator;
        NSInteger denominator = NSInteger(mHostDenominator);
        double beat = 0.0;
        const bool haveContext = mMusicalContextBlock.isSet()
            && BDD_POINTER_AS_BLOCK(AUHostMusicalContextBlock, mMusicalContextBlock.pointer())(
                   &tempo, &numerator, &denominator, &beat, nullptr, nullptr);

        AUHostTransportStateFlags flags = 0;
        const bool haveTransport = mTransportStateBlock.isSet()
            && BDD_POINTER_AS_BLOCK(AUHostTransportStateBlock, mTransportStateBlock.pointer())(
                   &flags, nullptr, nullptr, nullptr);
        const bool playing = haveContext && haveTransport && (flags & AUHostTransportStateMoving) != 0;

        if (haveContext) {
            setHostTiming(tempo, beat, sampleTime, playing, numerator, double(denominator));
        } else {
            mHostPlaying = false;
        }
    }

    // MARK: - MIDI Protocol
    MIDIProtocolID AudioUnitMIDIProtocol() const {
        return kMIDIProtocol_2_0;
    }
#endif

    /**
     MARK: - Internal Process

     Renders `frameCount` samples of the voice into every output buffer. `bufferStartTime` is the
     sample time of the first frame, which places it against the host's song position.
     */
    void process(std::span<float *> outputBuffers, int64_t bufferStartTime, uint32_t frameCount) {
        applyAmpEnvelopeSettings();
        adoptPublishedCurve();

        // Envelope timing for this block.
        const double beatsPerSample = mHostTempo / 60.0 / mSampleRate;
        const double syncLength = bdd::syncLengthInQuarterNotes(mEnvSyncLength, mHostNumerator, mHostDenominator);
        const bool synced = (mEnvTimeMode == envelopeSync);
        const bool locked = synced && mHostPlaying && mEnvRetrigger == 0;
        const double cyclesPerSample = synced ? beatsPerSample / syncLength
                                              : 1000.0 / (double(mEnvFreeMilliseconds) * mSampleRate);
        double beat = mHostBeat + double(bufferStartTime - mHostSampleTime) * beatsPerSample;

        if (mBypassed || !mAmpEnvelope.isActive()) {
            // Nothing is sounding, so there is nothing to smooth towards either: settle every
            // control where it was left, so the next note starts exactly on its settings. A free
            // envelope keeps running, so it is wherever it should be when the next note comes.
            snapSmoothers();
            mCurveFade = 1.0f;
            if (locked) {
                mEnvelopeClock.lockTo((beat + double(frameCount) * beatsPerSample) / syncLength);
            } else {
                mEnvelopeClock.advance(double(frameCount) * cyclesPerSample);
            }
            for (auto* buffer : outputBuffers) {
                std::fill_n(buffer, frameCount, 0.f);
            }
            return;
        }

        const double subRatio = (mSubOctave == 0) ? 0.5 : 0.25;
        const float crossoverTarget = std::log2(mSubCrossoverHertz);
        const float cutoffTarget = std::log2(mCutoffHertz);
        const float outputTarget = bdd::decibelsToGain(mOutputDecibels);
        const float filterModeTarget = float(mFilterMode);
        const auto direction = bdd::EnvelopeDirection(mEnvDirection);
        const float sampleHoldTarget = (mDownsampleMode == downsampleSampleHold) ? 1.0f : 0.0f;
        const float downsampleTarget = (mDownsampleMode == downsampleCount) ? 1.0f : 0.0f;
        const double sampleHoldRate = double(mSampleHoldRate);
        // Amount 0-100 is a hold of 1-60 samples at 48 kHz, as in the Max device, scaled so
        // the effective rate is the same at any sample rate.
        const double downsampleFactor = std::max(1.0, double(mDownsampleAmount) * 0.6)
                                      * (mSampleRate / kDownsampleReferenceRate);
        const float cleanupTarget = (mCleanupMode != 0) ? 1.0f : 0.0f;
        const float cleanupMultipleTarget = std::log2(mCleanupMultiple);

        double drawingPosition = 0.0;
        float drawing = 0.0f;

        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            const float mainMorph = mOscShapeSmoother.next(mOscShape);
            const float subGain = mSubLevelSmoother.next(mSubLevel);
            const float subMorph = mSubShapeSmoother.next(mSubShape);
            const float cutoffOctaves = mCutoffSmoother.next(cutoffTarget);
            const float resonanceAmount = mResonanceSmoother.next(mResonance);
            const float bandPassAmount = mFilterModeSmoother.next(filterModeTarget);
            const float outputGain = mOutputSmoother.next(outputTarget);
            const float bend = mBendSmoother.next(mBendPosition);
            const float envAmount = mEnvAmountSmoother.next(mEnvAmount);
            const bdd::AccelerateSettings accelerate {
                double(mAccelStartSmoother.next(mAccelStart)),
                double(mAccelEndSmoother.next(mAccelEnd)),
                double(mAccelCurveSmoother.next(mAccelCurve)),
            };

            // Drawn envelope: where in the drawing, and what it says there.
            if (locked) {
                mEnvelopeClock.lockTo(beat / syncLength);
                beat += beatsPerSample;
            }
            drawingPosition = bdd::readPosition(direction, mEnvelopeClock.index(), mEnvelopeClock.phase(),
                                             mEnvelopeClock.seed, accelerate);
            drawing = readDrawing(drawingPosition);
            if (!locked) {
                mEnvelopeClock.advance(cyclesPerSample);
            }

            const double pitch = mGlide.next() + double(bend * mBendRange);
            const double increment = bdd::noteToHertz(pitch) / mSampleRate;

            const float main = mMainOscillator.next(increment, mainMorph);
            const float sub = mSubOscillator.next(increment * subRatio, subMorph);

            // Crossover, as in the Max device: the sub keeps the low end below X-OVER and the
            // oscillator gives it up, so the two never stack there. That holds at every Sub Level:
            // with the sub at 0 the low end is simply gone. (It used to fade the split out below 10 %,
            // which brought 30 dB of fundamental back through the filter and downsampler: YOI-001.)
            const float crossoverOctaves = mSubCrossoverSmoother.next(crossoverTarget);
            if (crossoverOctaves != mAppliedCrossoverOctaves) {
                mAppliedCrossoverOctaves = crossoverOctaves;
                const double crossoverHertz = std::exp2(double(crossoverOctaves));
                mMainHighPass.setCutoff(crossoverHertz, mSampleRate);
                mSubLowPass.setCutoff(crossoverHertz, mSampleRate);
            }
            const double mainAboveSub = mMainHighPass.process(double(main));
            const double subBelow = mSubLowPass.process(double(sub));

            // Wavefolder settings. Moving it between positions fades it out, swaps, and fades it
            // back in, so the switch never clicks.
            if (mActiveFoldPosition != mFoldPosition && mFoldPositionBlend.current <= 0.0f) {
                mActiveFoldPosition = mFoldPosition;
                mWavefolder.reset();
                mFoldLevel.reset();
                mFoldDC.reset();
            }
            const double positionBlend = double(mFoldPositionBlend.next(mActiveFoldPosition == mFoldPosition ? 1.0f : 0.0f));
            const double foldDepth = double(mFoldAmountSmoother.next(mFoldAmount));
            // Only the main oscillator goes through the filter and grit; the sub takes its own clean
            // path and joins at the end. The oscillator is at full scale, where the fold starts.
            double oscillator = mainAboveSub;
            if (mActiveFoldPosition == foldPreFilter) {
                oscillator = applyFold(oscillator, foldDepth, positionBlend);
            }
            const double mix = kOscillatorHeadroom * oscillator;

            // The top of the drawing is the CUTOFF setting; the bottom is Amount octaves below it.
            const double modulatedOctaves = double(cutoffOctaves) - double(envAmount) * (1.0 - double(drawing));
            mFilter.setCoefficients(std::exp2(modulatedOctaves),
                                    bdd::StateVariableFilter::qForResonance(resonanceRange(resonanceAmount)),
                                    mSampleRate);
            const auto filtered = mFilter.process(mix);
            double filteredVoice = filtered.lowPass + (filtered.bandPass - filtered.lowPass) * double(bandPassAmount);
            if (mActiveFoldPosition == foldPreDownsample) {
                filteredVoice = kOscillatorHeadroom * applyFold(filteredVoice / kOscillatorHeadroom, foldDepth, positionBlend);
            }
            const float voice = float(filteredVoice);

            // Downsampler, right after the filter. Both kinds always run, so switching between
            // them (or off) crossfades instead of clicking.
            const float sampleHoldAmount = mSampleHoldWeight.next(sampleHoldTarget);
            const float downsampleAmount = mDownsampleWeight.next(downsampleTarget);
            const float sampledAndHeld = mSampleAndHold.next(voice, sampleHoldRate, mSampleRate);
            const float downsampled = mSampleCountDownsampler.next(voice, downsampleFactor);
            // Weights that sum to 1, so a fully selected mode passes its output exactly (a held
            // step stays perfectly flat) rather than rebuilding it from the unheld signal.
            const float dryAmount = 1.0f - sampleHoldAmount - downsampleAmount;
            double gritty = double(dryAmount * voice + sampleHoldAmount * sampledAndHeld + downsampleAmount * downsampled);

            // Folding after the downsampler shapes the filtered note and its downsampled grit
            // together, unevenly, which bonds the grit onto the note.
            if (mActiveFoldPosition == foldPostDownsample) {
                gritty = kOscillatorHeadroom * applyUnevenFold(gritty / kOscillatorHeadroom, foldDepth, positionBlend);
            }

            // Clean-up low-pass: a multiple of wherever the filter cutoff is right now, so it
            // follows the drawn envelope, taming the harshest of the downsampler's images.
            const double cleanupMultiplier = std::exp2(double(mCleanupMultipleSmoother.next(cleanupMultipleTarget)));
            mCleanupFilter.setCutoff(std::min(std::exp2(modulatedOctaves) * cleanupMultiplier, 0.45 * mSampleRate),
                                     mSampleRate);
            const double cleaned = mCleanupFilter.process(gritty);
            const double cleanupAmount = double(mCleanupWeight.next(cleanupTarget));
            const double tamed = (1.0 - cleanupAmount) * gritty + cleanupAmount * cleaned;

            // Harmonic booster: peaks at 3, 5 and 7 x the note, level-matched, so it brings the
            // pitch forward through the grit without making it louder.
            const double boost = double(mBoostSmoother.next(mBoostAmount));
            mHarmonicBooster.setFundamental(bdd::noteToHertz(pitch), mSampleRate);
            const double boosted = mHarmonicBooster.process(tamed, boost);
            const double brightened = (boost > 0.0) ? mBoostLevel.process(tamed, boosted) : tamed;

            const double amp = mAmpEnvelope.next();
            const double top = brightened * amp;

            // OTT-style compression on everything but the sub. Its crossover is in the path only
            // while it's in use, faded in and out so turning it on or off never clicks.
            const double ottDepth = double(mOttDepthSmoother.next(mOttDepth));
            const double ottWeight = double(mOttWeight.next(mOttDepth > 0.0f ? 1.0f : 0.0f));
            double compressed = top;
            if (ottWeight > 0.0) {
                const double makeup = std::exp2(ottDepth * kOttMakeupDecibels / 6.020599913);
                compressed = top + ottWeight * (mOtt.process(top, ottDepth) * makeup - top);
            }

            // Width, then the sub back in, in the middle.
            const auto stereo = mDimension.process(compressed, double(mWidthSmoother.next(mWidthAmount)));
            const double subVoice = kOscillatorHeadroom * double(subGain) * subBelow * amp;
            const float left = bdd::softLimit(float((stereo.left + subVoice) * double(outputGain)));
            const float right = bdd::softLimit(float((stereo.right + subVoice) * double(outputGain)));

            if (outputBuffers.size() == 1) {
                outputBuffers[0][frameIndex] = bdd::softLimit(float((compressed + subVoice) * double(outputGain)));
            } else {
                outputBuffers[0][frameIndex] = left;
                outputBuffers[1][frameIndex] = right;
                for (size_t channel = 2; channel < outputBuffers.size(); ++channel) {
                    outputBuffers[channel][frameIndex] = 0.5f * (left + right);
                }
            }
        }

        bdd::atomics::storeRelaxed(mDisplayPositionBits, std::bit_cast<uint32_t>(float(drawingPosition)));
        bdd::atomics::storeRelaxed(mDisplayValueBits, std::bit_cast<uint32_t>(drawing));
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
    /// Switching downsampler modes crossfades over this long.
    static constexpr double kModeCrossfadeSeconds = 0.015;
    /// A redrawn envelope fades in over this long, so editing it while it plays never clicks.
    static constexpr double kCurveCrossfadeSeconds = 0.02;
    /// Scales the oscillator mix so that the default sound peaks around -7 dBFS (about -15 dBFS
    /// RMS). That leaves room for resonance, which can add 10 dB or more at a harmonic, before
    /// the limiter's knee at -4.4 dBFS, so the limiter only catches genuinely extreme settings.
    static constexpr double kOscillatorHeadroom = 0.25;
    /// The sample rate the Downsample amount is counted at: the rate the owner's Live projects
    /// (and so the Max device it was tuned in) run at. At any other rate the hold is scaled to
    /// keep the same effective rate, so a Set sounds the same whatever its sample rate.
    static constexpr double kDownsampleReferenceRate = 48000.0;
    /// How long the fold's level matching listens before adjusting: long next to a bass cycle,
    /// short next to a phrase.
    static constexpr double kFoldLevelSeconds = 0.08;
    /// The part of the filter's resonance range the RES knob covers, from the owner's ears
    /// (ADJUST-001): below 7 % the peak did nothing, above 85 % it was too much. The knob still
    /// reads 0-100 %: 0 % is now Q 0.89 (the old 7 %) and 100 % is Q 12.1 (the old 85 %). The
    /// shared filter's own mapping is left alone, because the OTT's crossover relies on its
    /// minimum Q.
    static constexpr double kResonanceFloor = 0.07;
    static constexpr double kResonanceCeiling = 0.85;

    /// RES (0...1) as a position in the filter's full resonance range.
    static double resonanceRange(float amount) {
        return kResonanceFloor + (kResonanceCeiling - kResonanceFloor) * double(amount);
    }
    /// The offset the post-downsample fold adds before folding and takes away after, which folds
    /// the two halves of the wave differently (the owner's Max experiment used 0.15).
    static constexpr double kFoldAsymmetry = 0.15;
    /// Make-up gain for the OTT-style compressor at full depth. Its downward stage pulls the loud
    /// low band down (5 to 7 dB overall at full depth); this puts the level back, so turning OTT
    /// up adds density and bite rather than a change in level. Measured on the default sound,
    /// where full depth then stays within 0.3 dB and peaks just under the limiter's knee.
    static constexpr double kOttMakeupDecibels = 5.0;

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
            // cut the waveform mid-cycle and click. From silence, every control also jumps straight
            // to its setting, so a change made just before the note is heard exactly, not glided.
            if (!mAmpEnvelope.isActive()) {
                snapSmoothers();
                mMainOscillator.reset();
                mSubOscillator.reset();
                mSampleAndHold.reset();
                mSampleCountDownsampler.reset();
                resetFinish();
            }
            mAmpEnvelope.gateOn();

            if (mEnvRetrigger != 0) {
                mEnvelopeClock.restart();
            }
        }
    }

    /// Folds `x` (full scale at +-1) by `amount` 0...1, which drives it 1-10x into the fold, as the
    /// Max device's fold amount did. At 0 it returns `x` exactly; over the first 2 % it fades in, so
    /// leaving zero never jumps. `blend` fades the whole fold in and out when it changes position.
    ///
    /// The folded signal is kept at the loudness of what went in: the fold adds harmonics, not
    /// volume. Without that, driving a quiet signal (a band-pass, a low cutoff) 10x into the fold
    /// made it up to 9 dB louder.
    double applyFold(double x, double amount, double blend) {
        const double folded = mFoldLevel.process(x, mWavefolder.process(x * (1.0 + 9.0 * amount)));
        const double wet = std::min(1.0, amount * 50.0) * blend;
        return (1.0 - wet) * x + wet * folded;
    }

    /// The post-downsample fold: as `applyFold`, but offset by `kFoldAsymmetry` before folding
    /// and back after, so the top and bottom of the wave fold differently and add even harmonics.
    /// The DC the uneven fold leaves is filtered out.
    double applyUnevenFold(double x, double amount, double blend) {
        const double driven = x * (1.0 + 9.0 * amount) + kFoldAsymmetry;
        const double folded = mFoldDC.process(mWavefolder.process(driven) - kFoldAsymmetry);
        const double matched = mFoldLevel.process(x, folded);
        const double wet = std::min(1.0, amount * 50.0) * blend;
        return (1.0 - wet) * x + wet * matched;
    }

    /// Clears everything after the amp envelope and the stages that remember past audio, so a note
    /// from silence doesn't start with the tail of the last one.
    void resetFinish() {
        mHarmonicBooster.reset();
        mBoostLevel.reset();
        mFoldDC.reset();
        mOtt.reset();
        mDimension.reset();
    }

    /// Recalculates amp envelope coefficients when their settings have changed. Runs on the
    /// render thread, once per block, so the envelope never sees a half-written update.
    void applyAmpEnvelopeSettings() {
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

    /// Cleans up, renders and publishes a drawing. Runs on the editing thread.
    void publishEnvelopeCurve(const bdd::CurvePoint* points, int count) {
        mPublishedCount = bdd::sanitizeCurve(points, count, mPublishedPoints.data());
        bdd::renderCurveTable(mPublishedPoints.data(), mPublishedCount, mPublishScratch.data(), bdd::kCurveTableSize);
        mCurveExchange.publish(mPublishScratch.data());
    }

    const bdd::CurvePoint& publishedPoint(int index) const {
        return mPublishedPoints[size_t(std::clamp(index, 0, std::max(0, mPublishedCount - 1)))];
    }

    /// Picks up a newly published drawing and starts fading towards it. Runs on the render thread.
    void adoptPublishedCurve() {
        if (!mCurveExchange.fetch(mIncomingTable, mObservedCurveSequence)) {
            return;
        }
        // Bake whatever is audible now into the table being faded from, so a second edit in the
        // middle of a fade continues from what was heard rather than jumping.
        for (size_t i = 0; i < mPreviousTable.size(); ++i) {
            mPreviousTable[i] += mCurveFade * (mCurrentTable[i] - mPreviousTable[i]);
        }
        mCurrentTable = mIncomingTable;
        mCurveFade = 0.0f;
    }

    /// The drawing's value at `position`, part way through a crossfade if the drawing just changed.
    float readDrawing(double position) {
        const float value = bdd::lookup(mCurrentTable.data(), bdd::kCurveTableSize, position);
        if (mCurveFade >= 1.0f) {
            return value;
        }
        const float previous = bdd::lookup(mPreviousTable.data(), bdd::kCurveTableSize, position);
        const float faded = previous + mCurveFade * (value - previous);
        mCurveFade = std::min(1.0f, mCurveFade + mCurveFadeStep);
        return faded;
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
        mEnvAmountSmoother.snap(mEnvAmount);
        mAccelStartSmoother.snap(mAccelStart);
        mAccelEndSmoother.snap(mAccelEnd);
        mAccelCurveSmoother.snap(mAccelCurve);
        mSampleHoldWeight.snap(mDownsampleMode == downsampleSampleHold ? 1.0f : 0.0f);
        mDownsampleWeight.snap(mDownsampleMode == downsampleCount ? 1.0f : 0.0f);
        mFoldAmountSmoother.snap(mFoldAmount);
        mCleanupMultipleSmoother.snap(std::log2(mCleanupMultiple));
        mSubCrossoverSmoother.snap(std::log2(mSubCrossoverHertz));
        mBoostSmoother.snap(mBoostAmount);
        mOttDepthSmoother.snap(mOttDepth);
        mOttWeight.snap(mOttDepth > 0.0f ? 1.0f : 0.0f);
        mWidthSmoother.snap(mWidthAmount);
        mCleanupWeight.snap(mCleanupMode != 0 ? 1.0f : 0.0f);
        mActiveFoldPosition = mFoldPosition;
        mFoldPositionBlend.snap(1.0f);
    }

    // MARK: - Member Variables
#if YOI_AUDIO_UNIT
    bdd::RetainedBlock mMusicalContextBlock;
    bdd::RetainedBlock mTransportStateBlock;
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
    float mSubLevel = 0.75f;   // with the crossover the sub carries the low end alone; 75 % keeps the defaults at -7 dBFS
    float mSubShape = 0.0f;
    int mSubOctave = 0;
    float mSubCrossoverHertz = 130.0f;
    int mFilterMode = 0;
    float mCutoffHertz = 800.0f;
    float mResonance = 0.3f;
    float mAttackMilliseconds = 3.0f;
    float mDecayMilliseconds = 300.0f;
    float mSustain = 1.0f;
    float mReleaseMilliseconds = 150.0f;
    float mEnvAmount = 3.0f;
    int mEnvTimeMode = envelopeSync;
    int mEnvSyncLength = 6;   // 1/8
    float mEnvFreeMilliseconds = 500.0f;
    int mEnvDirection = 0;    // Forward
    int mEnvRetrigger = 0;
    float mAccelStart = 0.25f;
    float mAccelEnd = 2.0f;
    float mAccelCurve = 0.0f;
    int mDownsampleMode = downsampleSampleHold;
    float mSampleHoldRate = 1400.0f;
    float mDownsampleAmount = 45.0f;
    float mFoldAmount = 0.0f;
    int mFoldPosition = foldPostDownsample;
    int mActiveFoldPosition = foldPostDownsample;
    int mCleanupMode = 1;
    float mCleanupMultiple = 5.0f;
    float mBoostAmount = 0.0f;
    float mOttDepth = 0.0f;
    float mWidthAmount = 0.0f;

    float mBendPosition = 0.0f;

    // The amp envelope settings last handed to the envelope, to spot changes.
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
    bdd::Smoother mEnvAmountSmoother;
    bdd::Smoother mAccelStartSmoother;
    bdd::Smoother mAccelEndSmoother;
    bdd::Smoother mAccelCurveSmoother;
    bdd::LinearRamp mSampleHoldWeight;
    bdd::LinearRamp mDownsampleWeight;
    bdd::Smoother mFoldAmountSmoother;
    bdd::LinearRamp mFoldPositionBlend;
    bdd::Smoother mCleanupMultipleSmoother;
    bdd::LinearRamp mCleanupWeight;
    bdd::Smoother mSubCrossoverSmoother;
    bdd::Smoother mBoostSmoother;
    bdd::Smoother mOttDepthSmoother;
    bdd::LinearRamp mOttWeight;
    bdd::Smoother mWidthSmoother;
    float mAppliedCrossoverOctaves = -1.0f;

    bdd::NoteStack mHeldNotes;
    bdd::Glide mGlide;
    bool mHasPlayed = false;

    bdd::MorphOscillator mMainOscillator;
    bdd::SubOscillator mSubOscillator;
    bdd::StateVariableFilter mFilter;
    bdd::ADSREnvelope mAmpEnvelope;
    bdd::SampleAndHold mSampleAndHold;
    bdd::SampleCountDownsampler mSampleCountDownsampler;
    bdd::Wavefolder mWavefolder;
    bdd::LevelMatch mFoldLevel;
    bdd::ButterworthLowPass4 mCleanupFilter;
    bdd::ButterworthHighPass4 mMainHighPass;
    bdd::ButterworthLowPass4 mSubLowPass;
    bdd::DCBlocker mFoldDC;
    bdd::HarmonicBooster mHarmonicBooster;
    bdd::LevelMatch mBoostLevel;
    bdd::MultibandCompressor mOtt;
    bdd::DimensionExpander mDimension;

    // Host timing, as last reported.
    double mHostTempo = 120.0;
    double mHostBeat = 0.0;
    int64_t mHostSampleTime = 0;
    bool mHostPlaying = false;
    double mHostNumerator = 4.0;
    double mHostDenominator = 4.0;

    // Drawn envelope. The published points and scratch table belong to the editing thread; the
    // exchange passes finished tables across; the rest belongs to the render thread.
    std::array<bdd::CurvePoint, bdd::kMaxCurvePoints> mPublishedPoints{};
    int mPublishedCount = 0;
    std::array<float, bdd::kCurveTableSize> mPublishScratch{};
    bdd::CurveExchange mCurveExchange;
    uint32_t mObservedCurveSequence = 0;
    std::array<float, bdd::kCurveTableSize> mIncomingTable{};
    std::array<float, bdd::kCurveTableSize> mCurrentTable{};
    std::array<float, bdd::kCurveTableSize> mPreviousTable{};
    float mCurveFade = 1.0f;
    float mCurveFadeStep = 0.001f;
    bdd::EnvelopeClock mEnvelopeClock;
    uint32_t mDisplayPositionBits = 0;
    uint32_t mDisplayValueBits = 0;
};

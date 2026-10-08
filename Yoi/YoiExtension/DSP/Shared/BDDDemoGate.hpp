//
//  BDDDemoGate.hpp
//  Bass Daddy Devices
//
//  What an unlicensed synth does (YOI_DOCS/decisions/licensing.md): it plays normally, except that
//  for `kSilentSeconds` at the end of every `kPeriodSeconds` the output fades to silence, over
//  `kFadeSeconds` each way so it never clicks. The clock counts rendered audio, not wall time.
//  Licensed, it is an exact bypass (gain 1). Shared by every synth; no knowledge of licenses.
//

#pragma once

#include "BDDAtomics.hpp"

#include <algorithm>
#include <cstdint>

namespace bdd {

struct DemoGate {
    static constexpr double kPeriodSeconds = 60.0;
    static constexpr double kSilentSeconds = 3.0;
    static constexpr double kFadeSeconds = 0.02;

    void setSampleRate(double sampleRate) {
        mPeriod = std::max<int64_t>(1, int64_t(kPeriodSeconds * sampleRate));
        mSilenceStart = mPeriod - std::max<int64_t>(1, int64_t(kSilentSeconds * sampleRate));
        mStep = float(1.0 / std::max(1.0, kFadeSeconds * sampleRate));
        mPosition = 0;
    }

    /// Any thread. Takes effect within the fade time, so licensing mid-silence fades back in.
    void setLicensed(bool licensed) {
        atomics::storeRelaxed(mLicensed, licensed ? 1u : 0u);
    }

    bool isLicensed() const {
        return atomics::loadRelaxed(const_cast<uint32_t&>(mLicensed)) != 0;
    }

    /// The gain for the next sample. Render thread.
    inline float next() {
        const bool silent = atomics::loadRelaxed(mLicensed) == 0 && mPosition >= mSilenceStart;
        mPosition = (mPosition + 1 < mPeriod) ? mPosition + 1 : 0;
        const float target = silent ? 0.0f : 1.0f;
        mGain = (mGain < target) ? std::min(target, mGain + mStep) : std::max(target, mGain - mStep);
        return mGain;
    }

    /// Moves the clock on by `frames` without producing gains, for blocks with nothing sounding.
    void advance(uint32_t frames) {
        mPosition = (mPosition + int64_t(frames)) % mPeriod;
        const bool silent = atomics::loadRelaxed(mLicensed) == 0 && mPosition >= mSilenceStart;
        mGain = silent ? 0.0f : 1.0f;
    }

private:
    uint32_t mLicensed = 1;
    int64_t mPeriod = 48000 * 60;
    int64_t mSilenceStart = 48000 * 57;
    int64_t mPosition = 0;
    float mStep = 1.0f / 960.0f;
    float mGain = 1.0f;
};

} // namespace bdd

//
//  BDDDynamics.hpp
//  Bass Daddy Devices shared DSP
//
//  Dynamics: a three-band upward and downward compressor in the style producers call "OTT".
//  Each band pushes loud material down and pulls quiet material up, so detail hiding in the
//  quiet parts of a sound comes forward and the peaks are held in.
//
//  Header-only, allocation-free and safe to use from the render thread.
//

#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "BDDFilters.hpp"
#include "BDDMath.hpp"

namespace bdd {

/// One band's compressor, working in decibels on a smoothed peak level.
struct UpwardDownwardCompressor {
    /// Above this the band is pushed down, hard.
    static constexpr double kDownThreshold = -24.0;
    static constexpr double kDownRatio = 8.0;
    /// Below this the band is pulled up, gently, by no more than `kMaximumLift`.
    static constexpr double kUpThreshold = -36.0;
    static constexpr double kUpRatio = 3.0;
    static constexpr double kMaximumLift = 18.0;
    /// Below this the band counts as silence and isn't lifted at all, so noise and tails
    /// don't swell up; the lift fades in over the 12 dB above it.
    static constexpr double kFloor = -72.0;

    double envelope = 0.0;
    double attack = 0.01;
    double release = 0.001;

    void reset() { envelope = 0.0; }

    void setTimes(double attackSeconds, double releaseSeconds, double sampleRate) {
        attack = 1.0 - std::exp(-1.0 / std::max(1.0, attackSeconds * sampleRate));
        release = 1.0 - std::exp(-1.0 / std::max(1.0, releaseSeconds * sampleRate));
    }

    /// Gain in decibels at full depth, from the band's current level. `upward` scales the upward
    /// half only (lift and its ceiling together): 1 is the curve above, 0 leaves quiet material
    /// alone, 2 lifts it twice as far.
    static double gainFor(double levelDecibels, double upward = 1.0) {
        if (levelDecibels > kDownThreshold) {
            return (levelDecibels - kDownThreshold) * (1.0 / kDownRatio - 1.0);
        }
        if (levelDecibels < kUpThreshold) {
            const double lift = std::min(kMaximumLift, (kUpThreshold - levelDecibels) * (1.0 - 1.0 / kUpRatio));
            const double fadeIn = std::clamp((levelDecibels - kFloor) / 12.0, 0.0, 1.0);
            return lift * fadeIn * upward;
        }
        return 0.0;
    }

    inline double process(double input, double depth, double upward = 1.0) {
        const double level = std::fabs(input);
        envelope = flushDenormal(envelope + (level - envelope) * (level > envelope ? attack : release));
        const double levelDecibels = 20.0 * std::log10(envelope + 1.0e-9);
        return input * std::exp2(depth * gainFor(levelDecibels, upward) * (1.0 / 6.020599913));
    }
};

/// Three bands (split with Linkwitz-Riley crossovers), each through its own upward and downward
/// compressor, summed again. `depth` 0...1 scales how much the compressors act, like OTT's depth;
/// the crossover is always in the path, so at 0 the signal comes back through an all-pass.
/// `setTimeScale` stretches or shrinks attack and release together, like OTT's Time; `upward`
/// scales the upward half of every band, like OTT's Upward.
struct MultibandCompressor {
    static constexpr double kLowSplitHertz = 150.0;
    static constexpr double kHighSplitHertz = 2500.0;
    static constexpr double kAttackSeconds = 0.005;
    static constexpr double kReleaseSeconds = 0.1;

    LinkwitzRileySplit lowSplit;
    LinkwitzRileySplit highSplit;
    std::array<UpwardDownwardCompressor, 3> bands{};
    double timeScale = 1.0;

    void reset() {
        lowSplit.reset();
        highSplit.reset();
        for (auto& band : bands) {
            band.reset();
        }
    }

    void setSampleRate(double sampleRate) {
        lowSplit.setFrequency(kLowSplitHertz, sampleRate);
        highSplit.setFrequency(std::min(kHighSplitHertz, 0.45 * sampleRate), sampleRate);
        setTimeScale(timeScale, sampleRate);
    }

    /// 1 is the times above; 2 doubles both attack and release, 0.5 halves them.
    void setTimeScale(double scale, double sampleRate) {
        timeScale = std::max(0.01, scale);
        for (auto& band : bands) {
            band.setTimes(kAttackSeconds * timeScale, kReleaseSeconds * timeScale, sampleRate);
        }
    }

    inline double process(double input, double depth, double upward = 1.0) {
        const auto first = lowSplit.process(input);
        const auto second = highSplit.process(first.high);
        return bands[0].process(first.low, depth, upward)
             + bands[1].process(second.low, depth, upward)
             + bands[2].process(second.high, depth, upward);
    }
};

} // namespace bdd

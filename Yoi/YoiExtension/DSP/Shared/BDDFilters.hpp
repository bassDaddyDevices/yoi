//
//  BDDFilters.hpp
//  Bass Daddy Devices shared DSP
//
//  Header-only, allocation-free and safe to use from the render thread.
//

#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "BDDMath.hpp"

namespace bdd {

/// Resonant state-variable filter in the topology-preserving (trapezoidal) form described by
/// Vadim Zavalishin and Andrew Simper.
///
/// Unlike a biquad it stays stable and well-behaved while its cutoff and resonance are swept
/// every sample, which is exactly what the drawn envelope will do to it. One `process` call
/// yields low-pass, band-pass and high-pass together.
struct StateVariableFilter {
    struct Outputs {
        double lowPass;
        /// Normalised so that the peak at the cutoff sits at unity gain whatever the resonance.
        double bandPass;
        double highPass;
    };

    /// Q with the resonance control at zero: a Butterworth response, flat with no peak.
    static constexpr double kMinimumQ = 0.7071;
    /// Q with the resonance control at full: strongly vocal, but short of self-oscillation.
    static constexpr double kMaximumQ = 20.0;

    double ic1eq = 0.0;
    double ic2eq = 0.0;
    double k = 1.0 / kMinimumQ;
    double a1 = 1.0;
    double a2 = 0.0;
    double a3 = 0.0;

    void reset() {
        ic1eq = 0.0;
        ic2eq = 0.0;
    }

    /// Maps a 0...1 resonance control onto Q exponentially, so equal turns of the control sound
    /// like equal steps.
    static double qForResonance(double amount) {
        return kMinimumQ * std::pow(kMaximumQ / kMinimumQ, std::clamp(amount, 0.0, 1.0));
    }

    void setCoefficients(double cutoffHertz, double q, double sampleRate) {
        const double clampedHertz = std::clamp(cutoffHertz, 10.0, 0.49 * sampleRate);
        const double g = std::tan(kPi * clampedHertz / sampleRate);
        k = 1.0 / std::max(q, 0.01);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline Outputs process(double input) {
        const double v3 = input - ic2eq;
        const double v1 = a1 * ic1eq + a2 * v3;
        const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = flushDenormal(2.0 * v1 - ic1eq);
        ic2eq = flushDenormal(2.0 * v2 - ic2eq);
        return { v2, v1 * k, input - k * v1 - v2 };
    }

    /// As process(), with the band-pass state (the resonance loop) soft-clipped at `level`, so a
    /// strong peak saturates and compresses instead of only growing with Q. A level far above the
    /// signal behaves like process(). Outputs are unnormalised the same way.
    inline Outputs processDriven(double input, double level) {
        const double v3 = input - ic2eq;
        const double v1 = a1 * ic1eq + a2 * v3;
        const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = flushDenormal(level * std::tanh((2.0 * v1 - ic1eq) / level));
        ic2eq = flushDenormal(2.0 * v2 - ic2eq);
        return { v2, v1 * k, input - k * v1 - v2 };
    }
};

/// Four-pole (24 dB per octave) Butterworth low-pass: two state-variable sections with the Qs
/// that make the pair maximally flat. Flat up to the cutoff with no resonant bump, then steep, as
/// a clean-up filter should be.
struct ButterworthLowPass4 {
    StateVariableFilter first;
    StateVariableFilter second;

    void reset() {
        first.reset();
        second.reset();
    }

    void setCutoff(double cutoffHertz, double sampleRate) {
        first.setCoefficients(cutoffHertz, 0.5411961, sampleRate);
        second.setCoefficients(cutoffHertz, 1.3065630, sampleRate);
    }

    inline double process(double input) {
        return second.process(first.process(input).lowPass).lowPass;
    }
};

/// Removes DC with a one-pole high-pass at about 10 Hz: far below any note, fast enough to catch
/// the offset an uneven waveshaper leaves behind.
struct DCBlocker {
    double previousInput = 0.0;
    double previousOutput = 0.0;
    double pole = 0.9987;

    void reset() {
        previousInput = 0.0;
        previousOutput = 0.0;
    }

    void setSampleRate(double sampleRate, double cornerHertz = 10.0) {
        pole = std::exp(-kTwoPi * cornerHertz / sampleRate);
    }

    inline double process(double input) {
        const double output = input - previousInput + pole * previousOutput;
        previousInput = input;
        previousOutput = flushDenormal(output);
        return previousOutput;
    }
};

/// One band edge of a Linkwitz-Riley crossover: 4-pole low- and high-pass outputs at the same
/// frequency (each two Butterworth 2-pole sections), which sum back to an all-pass response.
struct LinkwitzRileySplit {
    StateVariableFilter low1, low2, high1, high2;

    void reset() {
        low1.reset();
        low2.reset();
        high1.reset();
        high2.reset();
    }

    void setFrequency(double hertz, double sampleRate) {
        for (auto* section : { &low1, &low2, &high1, &high2 }) {
            section->setCoefficients(hertz, StateVariableFilter::kMinimumQ, sampleRate);
        }
    }

    struct Bands {
        double low;
        double high;
    };

    inline Bands process(double input) {
        return { low2.process(low1.process(input).lowPass).lowPass,
                 high2.process(high1.process(input).highPass).highPass };
    }
};

/// Boosts the odd harmonics of the note that's playing: bell-shaped peaks at 3, 5 and 7 times
/// its frequency, which move with the pitch (glide, bend). After a downsampler, most of what's
/// added is off-pitch; lifting the note's real harmonics brings the pitch back through the grit.
struct HarmonicBooster {
    static constexpr int kCount = 3;
    static constexpr double kMultiples[kCount] = { 3.0, 5.0, 7.0 };
    /// Wide enough that a glide doesn't whistle, narrow enough to pick out one harmonic.
    static constexpr double kQ = 4.0;
    /// Peak gain at full amount: +12 dB.
    static constexpr double kMaximumGain = 3.0;   // added to the dry signal, so 1 + 3 = 4x

    std::array<StateVariableFilter, kCount> bells{};
    double appliedFrequency = -1.0;

    void reset() {
        for (auto& bell : bells) {
            bell.reset();
        }
        appliedFrequency = -1.0;
    }

    /// `fundamentalHertz` is the note's frequency; peaks above 0.45 x the sample rate are pinned
    /// just below it.
    void setFundamental(double fundamentalHertz, double sampleRate) {
        if (fundamentalHertz == appliedFrequency) {
            return;
        }
        appliedFrequency = fundamentalHertz;
        for (int i = 0; i < kCount; ++i) {
            const double hertz = std::min(fundamentalHertz * kMultiples[i], 0.45 * sampleRate);
            bells[size_t(i)].setCoefficients(hertz, kQ, sampleRate);
        }
    }

    /// `amount` 0...1. At 0 the input comes back exactly.
    inline double process(double input, double amount) {
        double boosted = input;
        for (auto& bell : bells) {
            boosted += amount * kMaximumGain * bell.process(input).bandPass;
        }
        return boosted;
    }
};

/// Four-pole (24 dB per octave) Butterworth high-pass, the mirror of `ButterworthLowPass4`.
/// With the low-pass at the same frequency it makes a crossover, as between a sub and the
/// oscillator above it.
struct ButterworthHighPass4 {
    StateVariableFilter first;
    StateVariableFilter second;

    void reset() {
        first.reset();
        second.reset();
    }

    void setCutoff(double cutoffHertz, double sampleRate) {
        first.setCoefficients(cutoffHertz, 0.5411961, sampleRate);
        second.setCoefficients(cutoffHertz, 1.3065630, sampleRate);
    }

    inline double process(double input) {
        return second.process(first.process(input).highPass).highPass;
    }
};

} // namespace bdd

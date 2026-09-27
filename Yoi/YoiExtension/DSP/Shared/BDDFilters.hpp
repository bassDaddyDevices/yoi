//
//  BDDFilters.hpp
//  Bass Daddy Devices shared DSP
//
//  Header-only, allocation-free and safe to use from the render thread.
//

#pragma once

#include <algorithm>
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

//
//  BDDOscillators.hpp
//  Bass Daddy Devices shared DSP
//
//  Band-limited oscillators for bass voices.
//
//  Header-only, allocation-free and safe to use from the render thread.
//

#pragma once

#include <algorithm>
#include <cmath>

#include "BDDMath.hpp"

namespace bdd {

/// Two-sample polynomial correction (PolyBLEP) for a unit step at phase 0.
///
/// `t` is the oscillator phase in 0...1 and `dt` the phase increment per sample. Subtracting it
/// from a naive waveform at each discontinuity removes most of the aliasing a hard edge would
/// otherwise fold back into the audible range.
inline double polyBlep(double t, double dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

/// Highest phase increment the oscillators accept: just under Nyquist.
inline constexpr double kMaxPhaseIncrement = 0.45;

/// The main oscillator: a band-limited saw that morphs into a band-limited square.
///
/// Both shapes run from the same phase and are crossfaded, so the morph never changes pitch
/// or phase. The square is scaled by `kSquareLevel` so the two sit at a similar loudness; at
/// full scale a square carries about 4.8 dB more energy than a saw.
///
/// The saw falls rather than rises, so its odd harmonics start in step with the square's and the
/// crossfade adds them. A rising saw has them upside down relative to the square: halfway through
/// the morph they cancel, and the fundamental all but vanishes (measured: 10 dB quieter at 40-50 %).
struct MorphOscillator {
    static constexpr double kSquareLevel = 0.7;

    double phase = 0.0;

    void reset() { phase = 0.0; }

    /// `increment` is frequency / sample rate. `morph` is 0 for saw, 1 for square.
    inline float next(double increment, float morph) {
        const double dt = std::clamp(increment, 0.0, kMaxPhaseIncrement);
        const double t = phase;

        const double saw = 1.0 - 2.0 * t + polyBlep(t, dt);

        double halfway = t + 0.5;
        if (halfway >= 1.0) {
            halfway -= 1.0;
        }
        const double square = kSquareLevel * ((t < 0.5 ? 1.0 : -1.0) + polyBlep(t, dt) - polyBlep(halfway, dt));

        phase += dt;
        if (phase >= 1.0) {
            phase -= 1.0;
        }
        return float(saw + (square - saw) * double(morph));
    }
};

/// The sub oscillator: a sine that morphs into a triangle.
///
/// The triangle is aligned with the sine (zero at phase 0, peak at a quarter cycle) so the
/// morph is a pure change of tone. It is left naive: at sub-bass pitches its harmonics fall
/// away at 12 dB per octave and nothing audible aliases.
struct SubOscillator {
    double phase = 0.0;

    void reset() { phase = 0.0; }

    /// `increment` is frequency / sample rate. `morph` is 0 for sine, 1 for triangle.
    inline float next(double increment, float morph) {
        const double t = phase;
        const double sine = std::sin(kTwoPi * t);
        const double triangle = (t < 0.25) ? 4.0 * t
                              : (t < 0.75) ? 2.0 - 4.0 * t
                                           : 4.0 * t - 4.0;

        phase += std::clamp(increment, 0.0, kMaxPhaseIncrement);
        if (phase >= 1.0) {
            phase -= 1.0;
        }
        return float(sine + (triangle - sine) * double(morph));
    }
};

} // namespace bdd

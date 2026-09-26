//
//  BDDMath.hpp
//  Bass Daddy Devices shared DSP
//
//  Small numeric helpers used by every block in `DSP/Shared`: constants, denormal flushing,
//  pitch and level conversions, parameter smoothing and the output limiter.
//
//  Nothing in `DSP/Shared` knows about YOI. It is written to be lifted out into its own module
//  when the next synth starts, so keep YOI-specific behaviour in the kernel instead.
//
//  Header-only, allocation-free and safe to use from the render thread.
//

#pragma once

#include <algorithm>
#include <cmath>

namespace bdd {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

/// Stops a recursive filter's state from decaying into denormal numbers, which are ruinously
/// slow to compute with. The threshold is around -400 dB, so nothing audible is lost.
inline float flushDenormal(float x) {
    return (std::fabs(x) < 1e-20f) ? 0.0f : x;
}

/// As above for double-precision state.
inline double flushDenormal(double x) {
    return (std::fabs(x) < 1e-30) ? 0.0 : x;
}

/// Frequency of a MIDI note number, which may be fractional (glide, pitch bend). A4 = 440 Hz.
inline double noteToHertz(double note) {
    return 440.0 * std::exp2((note - 69.0) / 12.0);
}

inline float decibelsToGain(float decibels) {
    return std::pow(10.0f, decibels * 0.05f);
}

/// One-pole low pass used to de-zipper parameter changes.
struct Smoother {
    float current = 0.0f;
    float coefficient = 0.01f;

    void setTimeConstant(double seconds, double sampleRate) {
        coefficient = float(1.0 - std::exp(-1.0 / std::max(1.0, seconds * sampleRate)));
    }

    void snap(float value) { current = value; }

    /// Lands exactly on the target once within a hair of it. Without that, a value easing towards
    /// zero would eventually decay into denormal numbers.
    inline float next(float target) {
        const float distance = target - current;
        current = (std::fabs(distance) < 1.0e-7f) ? target : current + distance * coefficient;
        return current;
    }
};

/// Output safety stage. Leaves the signal untouched up to the knee (about -4.4 dBFS), then
/// bends it smoothly towards ±1 so that nothing, not even a screaming resonance, can leave the
/// plug-in above full scale. The curve's slope is continuous at the knee, so crossing it adds
/// no hard edge.
inline float softLimit(float x) {
    constexpr float knee = 0.6f;
    const float magnitude = std::fabs(x);
    if (magnitude <= knee) {
        return x;
    }
    const float over = (magnitude - knee) / (1.0f - knee);
    return std::copysign(knee + (1.0f - knee) * std::tanh(over), x);
}

} // namespace bdd

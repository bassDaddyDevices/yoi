//
//  BDDSpatial.hpp
//  Bass Daddy Devices shared DSP
//
//  Width: a dimension expander that turns a mono sound into a wide stereo one without changing
//  its tone, and that disappears completely when the stereo is summed back to mono.
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

/// Two very short delays of the input, each swept slowly by its own LFO (the two in opposite
/// phase), are subtracted from each other to make a "side" signal. It is added to the left channel
/// and taken from the right, like a mid/side widener:
///
///     left = input + side,  right = input - side
///
/// so left + right is exactly twice the input, and a mono system (a club's sub stack, a phone)
/// hears the dry sound untouched. The side is high-passed, so the low mids stay in the middle.
struct DimensionExpander {
    static constexpr int kBufferSize = 8192;   // over 40 ms at 192 kHz
    static constexpr double kFirstDelaySeconds = 0.011;
    static constexpr double kSecondDelaySeconds = 0.017;
    static constexpr double kSweepSeconds = 0.0012;
    static constexpr double kSweepHertz = 0.35;
    static constexpr double kSideHighPassHertz = 200.0;
    /// Side level at full width.
    static constexpr double kMaximumSide = 0.7;

    std::array<float, kBufferSize> buffer{};
    int writeIndex = 0;
    double lfoPhase = 0.0;
    double lfoStep = 0.0;
    double sampleRate = 48000.0;
    StateVariableFilter sideHighPass;

    void reset() {
        buffer.fill(0.0f);
        writeIndex = 0;
        lfoPhase = 0.0;
        sideHighPass.reset();
    }

    void setSampleRate(double rate) {
        sampleRate = rate;
        lfoStep = kSweepHertz / rate;
        sideHighPass.setCoefficients(kSideHighPassHertz, StateVariableFilter::kMinimumQ, rate);
    }

    struct Stereo {
        double left;
        double right;
    };

    /// `width` 0...1. At 0 both channels are exactly the input.
    inline Stereo process(double input, double width) {
        buffer[size_t(writeIndex)] = float(input);
        const double sweep = std::sin(kTwoPi * lfoPhase);
        const double first = read((kFirstDelaySeconds + kSweepSeconds * sweep) * sampleRate);
        const double second = read((kSecondDelaySeconds - kSweepSeconds * sweep) * sampleRate);
        writeIndex = (writeIndex + 1) & (kBufferSize - 1);
        lfoPhase += lfoStep;
        if (lfoPhase >= 1.0) {
            lfoPhase -= 1.0;
        }
        const double side = width * kMaximumSide * sideHighPass.process(first - second).highPass;
        return { input + side, input - side };
    }

private:
    /// The input from `delaySamples` ago, linearly interpolated.
    double read(double delaySamples) const {
        const double clamped = std::clamp(delaySamples, 1.0, double(kBufferSize - 2));
        const double position = double(writeIndex) - clamped;
        const double wrapped = position < 0.0 ? position + double(kBufferSize) : position;
        const int index = int(wrapped);
        const double fraction = wrapped - double(index);
        const double a = double(buffer[size_t(index & (kBufferSize - 1))]);
        const double b = double(buffer[size_t((index + 1) & (kBufferSize - 1))]);
        return a + (b - a) * fraction;
    }
};

} // namespace bdd

//
//  BDDShapers.hpp
//  Bass Daddy Devices shared DSP
//
//  Waveshapers that add harmonics.
//
//  Header-only, allocation-free and safe to use from the render thread.
//

#pragma once

#include <algorithm>
#include <cmath>

namespace bdd {

/// A triangle wavefolder: anything beyond ±1 is reflected back into range, again and again, like
/// Max's `fold~` between -1 and 1. Unchanged inside ±1.
///
/// Folding makes a lot of new harmonics, and the highest of them would alias. So the curve is
/// applied with first-order antiderivative anti-aliasing (ADAA): the output is the curve's average
/// between the last input and this one, worked out from its integral. The real harmonics are kept;
/// the aliased ones, which fold back as inharmonic fizz, are largely removed. It adds half a sample
/// of delay.
struct Wavefolder {
    double previousInput = 0.0;
    double previousIntegral = 0.0;

    void reset() {
        previousInput = 0.0;
        previousIntegral = 0.0;
    }

    /// The fold curve itself: a triangle wave in x with period 4, equal to x across -1...1.
    static double fold(double x) {
        const double wrapped = (x + 1.0) - 4.0 * std::floor((x + 1.0) * 0.25);   // 0...4
        return (wrapped < 2.0) ? wrapped - 1.0 : 3.0 - wrapped;
    }

    /// An antiderivative of `fold`. It repeats every 4 like the curve, because the curve averages
    /// zero over each period, which keeps it exact for any input size.
    static double integral(double x) {
        const double wrapped = (x + 1.0) - 4.0 * std::floor((x + 1.0) * 0.25);
        if (wrapped < 2.0) {
            const double t = wrapped - 1.0;
            return 0.5 * (t * t - 1.0);
        }
        const double t = 3.0 - wrapped;
        return 0.5 * (1.0 - t * t);
    }

    inline double process(double input) {
        const double currentIntegral = integral(input);
        const double step = input - previousInput;
        const double output = (std::fabs(step) > 1.0e-6)
            ? (currentIntegral - previousIntegral) / step
            : fold(0.5 * (input + previousInput));
        previousInput = input;
        previousIntegral = currentIntegral;
        return output;
    }
};

/// A soft saturator: tanh, offset by `bias` so the top and bottom of the wave round off
/// differently. The even harmonics that adds are what make a saturated sound fuller rather than
/// just brighter. The curve is shifted so 0 still gives 0, but a biased curve leaves some DC on a
/// loud signal, so follow it with a DC blocker.
///
/// Anti-aliased the same way as the wavefolder (first-order ADAA), with half a sample of delay.
struct Saturator {
    double bias = 0.0;
    double previousInput = 0.0;
    double previousIntegral = 0.0;

    void reset() {
        previousInput = 0.0;
        previousIntegral = integral(0.0);
    }

    void setBias(double newBias) {
        if (newBias != bias) {
            bias = newBias;
            previousIntegral = integral(previousInput);
        }
    }

    double curve(double x) const {
        return std::tanh(x + bias) - std::tanh(bias);
    }

    /// An antiderivative of `curve`: log(cosh(x + bias)) - tanh(bias) x, written so large inputs
    /// neither overflow nor lose precision.
    double integral(double x) const {
        const double shifted = std::fabs(x + bias);
        const double logCosh = shifted + std::log1p(std::exp(-2.0 * shifted)) - 0.69314718055994531;
        return logCosh - std::tanh(bias) * x;
    }

    inline double process(double input) {
        const double currentIntegral = integral(input);
        const double step = input - previousInput;
        const double output = (std::fabs(step) > 1.0e-6)
            ? (currentIntegral - previousIntegral) / step
            : curve(0.5 * (input + previousInput));
        previousInput = input;
        previousIntegral = currentIntegral;
        return output;
    }
};

/// Keeps a shaped signal at the loudness of the signal it was made from, so a shaper changes the
/// tone and not the volume. It follows both signals' power over `seconds` and scales the shaped
/// one by the ratio, within `kMinimumGain`...`kMaximumGain`.
///
/// The follow time is long next to a bass cycle, so the gain doesn't wobble with the waveform, and
/// short next to a phrase, so it keeps up with the filter moving.
struct LevelMatch {
    static constexpr double kMinimumGain = 0.125;   // -18 dB
    static constexpr double kMaximumGain = 2.0;     // +6 dB
    /// Below this power (about -90 dBFS) both signals count as silence and nothing is changed.
    static constexpr double kSilence = 1.0e-9;

    double sourcePower = 0.0;
    double shapedPower = 0.0;
    double coefficient = 0.001;

    void setTimeConstant(double seconds, double sampleRate) {
        coefficient = 1.0 - std::exp(-1.0 / std::max(1.0, seconds * sampleRate));
    }

    void reset() {
        sourcePower = 0.0;
        shapedPower = 0.0;
    }

    /// `shaped`, scaled to the loudness `source` has had lately.
    inline double process(double source, double shaped) {
        sourcePower = flushTiny(sourcePower + (source * source - sourcePower) * coefficient);
        shapedPower = flushTiny(shapedPower + (shaped * shaped - shapedPower) * coefficient);
        const double gain = std::sqrt((sourcePower + kSilence) / (shapedPower + kSilence));
        return shaped * std::clamp(gain, kMinimumGain, kMaximumGain);
    }

private:
    static double flushTiny(double value) {
        return (value < 1.0e-30) ? 0.0 : value;
    }
};

} // namespace bdd

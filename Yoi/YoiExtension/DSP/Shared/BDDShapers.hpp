//
//  BDDShapers.hpp
//  Bass Daddy Devices shared DSP
//
//  Waveshapers that add harmonics.
//
//  Header-only, allocation-free and safe to use from the render thread.
//

#pragma once

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

} // namespace bdd

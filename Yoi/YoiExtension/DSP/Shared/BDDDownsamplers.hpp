//
//  BDDDownsamplers.hpp
//  Bass Daddy Devices shared DSP
//
//  Two deliberately naive downsamplers, reproducing the Max objects the YOI sound was found
//  with. Neither is band-limited: the aliasing they make is the effect, so "improving" them would
//  change the sound.
//
//  Header-only, allocation-free and safe to use from the render thread.
//

#pragma once

#include <algorithm>
#include <cmath>

namespace bdd {

/// Sample-and-hold at a rate in hertz, like a gen `phasor` driving a `sah`: once per cycle of
/// the rate it grabs the current input sample and holds it until the next cycle.
///
/// Grabs happen on the host's sample grid, so at 1400 Hz and 48 kHz the holds alternate between
/// 34 and 35 samples. That uneven stepping is part of the sound it was tuned by ear with.
struct SampleAndHold {
    double phase = 1.0;   // starts due, so the first sample is grabbed straight away
    float held = 0.0f;

    void reset() {
        phase = 1.0;
        held = 0.0f;
    }

    inline float next(float input, double rateHertz, double sampleRate) {
        if (phase >= 1.0) {
            phase -= std::floor(phase);
            held = input;
        }
        phase += std::max(0.0, rateHertz) / sampleRate;
        return held;
    }
};

/// Keeps every Nth sample and holds it in between, like Max's `downsamp~`. N may be fractional:
/// the count carries its remainder, so hold lengths alternate to average exactly N.
struct SampleCountDownsampler {
    double count = 0.0;
    double factor = 1.0;
    float held = 0.0f;

    void reset() {
        count = 0.0;
        held = 0.0f;
    }

    inline float next(float input, double newFactor) {
        const double clamped = std::max(1.0, std::fabs(newFactor));
        if (clamped != factor) {
            factor = clamped;
            // A shorter hold takes effect now rather than finishing the longer one.
            count = std::min(count, factor);
        }
        count += 1.0;
        if (count >= factor) {
            count -= factor;
            held = input;
        }
        return held;
    }
};

} // namespace bdd

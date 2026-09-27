//
//  BDDDrawnEnvelope.hpp
//  Bass Daddy Devices shared DSP
//
//  Playback of a drawn curve as a looping envelope: how long one pass takes (synced to the host
//  or free), and how the curve is read on each pass (the direction modes).
//
//  Everything that decides *where in the drawing* the envelope is reading is a pure function of
//  the cycle number and the position within that cycle. That keeps a synced envelope locked to
//  the host's bar position through loops and jumps, and makes even the Random mode repeat exactly
//  on every playback of a song.
//
//  Header-only, allocation-free and safe to use from the render thread.
//

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "BDDDrawnCurve.hpp"
#include "BDDMath.hpp"

namespace bdd {

// MARK: - Timing

/// A tempo-synced length. Lengths of a bar or more are written for 4/4 and scale with the time
/// signature, so "1 bar" in 3/4 lasts three beats; shorter note values never scale.
struct SyncLength {
    double quarterNotes;
    bool isBars;
    const char* name;
};

/// The 24 sync lengths, shortest first, including triplet and dotted values.
inline constexpr std::array<SyncLength, 24> kSyncLengths = {{
    { 1.0 / 16.0, false, "1/64" },  { 1.0 / 12.0, false, "1/48" },  { 1.0 / 8.0, false, "1/32" },
    { 1.0 / 6.0, false, "1/24" },   { 1.0 / 4.0, false, "1/16" },   { 1.0 / 3.0, false, "1/12" },
    { 1.0 / 2.0, false, "1/8" },    { 2.0 / 3.0, false, "1/6" },    { 3.0 / 4.0, false, "3/16" },
    { 1.0, false, "1/4" },          { 5.0 / 4.0, false, "5/16" },   { 4.0 / 3.0, false, "1/3" },
    { 3.0 / 2.0, false, "3/8" },    { 2.0, false, "1/2" },          { 3.0, false, "3/4" },
    { 4.0, true, "1 bar" },         { 6.0, true, "1.5 bars" },      { 8.0, true, "2 bars" },
    { 12.0, true, "3 bars" },       { 16.0, true, "4 bars" },       { 24.0, true, "6 bars" },
    { 32.0, true, "8 bars" },       { 64.0, true, "16 bars" },      { 128.0, true, "32 bars" },
}};

/// Length of sync option `index` in quarter notes, under the given time signature.
inline double syncLengthInQuarterNotes(int index, double numerator, double denominator) {
    const SyncLength& length = kSyncLengths[size_t(std::clamp(index, 0, int(kSyncLengths.size()) - 1))];
    if (!length.isBars || numerator <= 0.0 || denominator <= 0.0) {
        return length.quarterNotes;
    }
    return length.quarterNotes * (numerator / denominator);
}

/// Where the envelope is, counted in whole passes through the drawing: the integer part is the
/// pass number and the fraction is how far through that pass it is.
struct EnvelopeClock {
    double cycles = 0.0;
    uint64_t seed = 0x9E3779B97F4A7C15ull;

    /// Back to the start of the drawing, with a new random pattern for the Random direction.
    void restart() {
        cycles = 0.0;
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    }

    /// Runs freely by `amount` passes.
    void advance(double amount) {
        cycles += amount;
        // Keep the count small enough that the fraction stays precise over very long sessions.
        if (cycles >= 1073741824.0) {
            cycles -= 1073741824.0;
        }
    }

    /// Follows the host: `cyclesFromHost` is the song position divided by the sync length.
    void lockTo(double cyclesFromHost) { cycles = cyclesFromHost; }

    int64_t index() const { return int64_t(std::floor(cycles)); }
    double phase() const { return cycles - std::floor(cycles); }
};

// MARK: - Directions

enum class EnvelopeDirection : int {
    forward = 0,
    backward = 1,
    pingpong = 2,
    sine = 3,
    random = 4,
    accelerate = 5,
};

inline constexpr int kEnvelopeDirectionCount = 6;

/// Settings for the Accelerate direction: playback speed glides from `start` to `end` over each
/// pass, along `curve` (-1...1, shaped like a curve segment). Speeds are multiples of the TIME
/// setting, so 1 plays the drawing once per pass.
struct AccelerateSettings {
    double start = 0.25;
    double end = 2.0;
    double curve = 0.0;
};

/// A repeatable random value in 0...1 for one pass of the Random direction.
inline double randomPoint(uint64_t seed, int64_t cycle) {
    uint64_t z = seed + uint64_t(cycle) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) * (1.0 / 9007199254740992.0);
}

/// How far through the drawing Accelerate has travelled after `phase` of one pass: the integral
/// of its speed, so it can be worked out directly for any point in the pass.
inline double accelerateTravel(double phase, const AccelerateSettings& settings) {
    const double steepness = std::clamp(settings.curve, -1.0, 1.0) * kBendSteepness;
    double shapedArea; // integral of bendShape(u, curve) from 0 to phase
    if (std::fabs(steepness) < 1.0e-4) {
        shapedArea = 0.5 * phase * phase;
    } else {
        shapedArea = (std::expm1(steepness * phase) / steepness - phase) / std::expm1(steepness);
    }
    return settings.start * phase + (settings.end - settings.start) * shapedArea;
}

/// The position (0...1) in the drawing to read for this point in the envelope's cycle.
inline double readPosition(EnvelopeDirection direction, int64_t cycle, double phase, uint64_t seed,
                           const AccelerateSettings& accelerate) {
    switch (direction) {
        case EnvelopeDirection::forward:
            return phase;
        case EnvelopeDirection::backward:
            return 1.0 - phase;
        case EnvelopeDirection::pingpong:
            return (phase < 0.5) ? 2.0 * phase : 2.0 - 2.0 * phase;
        case EnvelopeDirection::sine:
            return 0.5 - 0.5 * std::cos(kTwoPi * phase);
        case EnvelopeDirection::random: {
            // Glides from the last pass's random point to this pass's over the whole pass.
            const double from = randomPoint(seed, cycle - 1);
            const double to = randomPoint(seed, cycle);
            return from + (to - from) * phase;
        }
        case EnvelopeDirection::accelerate: {
            // Starts each pass from the beginning of the drawing and wraps round it as it speeds up.
            const double travelled = accelerateTravel(phase, accelerate);
            return travelled - std::floor(travelled);
        }
    }
    return phase;
}

} // namespace bdd

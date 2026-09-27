//
//  BDDDrawnCurve.hpp
//  Bass Daddy Devices shared DSP
//
//  A curve the user draws: points joined by segments that can bend, rendered into a lookup
//  table the audio thread reads. This is the only place the curve's shape is calculated. Editors
//  move points and show the rendered table; they never repeat this maths.
//
//  Header-only. Rendering and sanitising run on whichever thread edits the curve; `lookup` and
//  `CurveExchange::fetch` are safe on the render thread.
//

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "BDDAtomics.hpp"

namespace bdd {

/// One point of a drawn curve. `x` and `y` are 0...1. `bend` (-1...1) shapes the segment that
/// runs from this point to the next: 0 is a straight line, positive starts slowly and finishes
/// fast, negative starts fast and eases in. The last point's bend is unused.
///
/// Two points at the same `x` make a vertical jump.
struct CurvePoint {
    float x = 0.0f;
    float y = 0.0f;
    float bend = 0.0f;
};

inline constexpr int kMaxCurvePoints = 64;

/// Entries in a rendered curve table: 1024 steps across 0...1, plus the end point.
inline constexpr int kCurveTableSize = 1025;

/// How strongly a bend of ±1 curves a segment. At 6, a fully bent segment reaches only about 5 %
/// of its rise by the halfway point.
inline constexpr double kBendSteepness = 6.0;

/// Progress through a bent segment: `t` is 0...1 along it, and so is the result.
inline double bendShape(double t, double bend) {
    const double steepness = std::clamp(bend, -1.0, 1.0) * kBendSteepness;
    if (std::fabs(steepness) < 1.0e-4) {
        return t;
    }
    return std::expm1(steepness * t) / std::expm1(steepness);
}

/// Turns any list of points into a valid curve: finite values clamped to 0...1, sorted by `x`
/// (points sharing an `x` keep their order, so jumps survive), the first point pinned to x = 0
/// and the last to x = 1, and no more than `kMaxCurvePoints`. With fewer than two usable points
/// the result is a flat line at the height of the one given, or at half height.
///
/// Returns the number of points written to `out`, which must hold `kMaxCurvePoints`.
inline int sanitizeCurve(const CurvePoint* in, int count, CurvePoint* out) {
    auto unit = [](float value, float fallback) {
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
    };

    int written = 0;
    for (int i = 0; in != nullptr && i < count && written < kMaxCurvePoints; ++i) {
        if (!std::isfinite(in[i].x) || !std::isfinite(in[i].y)) {
            continue;
        }
        CurvePoint point;
        point.x = unit(in[i].x, 0.0f);
        point.y = unit(in[i].y, 0.5f);
        point.bend = std::isfinite(in[i].bend) ? std::clamp(in[i].bend, -1.0f, 1.0f) : 0.0f;
        out[written++] = point;
    }

    if (written < 2) {
        const float level = (written == 1) ? out[0].y : 0.5f;
        out[0] = { 0.0f, level, 0.0f };
        out[1] = { 1.0f, level, 0.0f };
        return 2;
    }

    std::stable_sort(out, out + written, [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
    out[0].x = 0.0f;
    out[written - 1].x = 1.0f;
    return written;
}

/// Renders sanitised points into a table of `size` entries spanning x = 0...1.
inline void renderCurveTable(const CurvePoint* points, int count, float* table, int size) {
    int segment = 0;
    for (int i = 0; i < size; ++i) {
        const double x = (size > 1) ? double(i) / double(size - 1) : 0.0;

        // Move to the segment containing x. At a vertical jump the later segment wins, so the
        // table already holds the new level at the jump itself.
        while (segment + 2 < count && double(points[segment + 1].x) <= x) {
            ++segment;
        }

        const CurvePoint& from = points[segment];
        const CurvePoint& to = points[std::min(segment + 1, count - 1)];
        const double width = double(to.x) - double(from.x);
        const double t = (width > 0.0) ? std::clamp((x - double(from.x)) / width, 0.0, 1.0) : 1.0;
        table[i] = float(double(from.y) + (double(to.y) - double(from.y)) * bendShape(t, double(from.bend)));
    }
}

/// Reads a rendered table at `x` (0...1), interpolating between entries.
inline float lookup(const float* table, int size, double x) {
    const double position = std::clamp(x, 0.0, 1.0) * double(size - 1);
    const int index = std::min(int(position), size - 2);
    const float fraction = float(position - double(index));
    return table[index] + fraction * (table[index + 1] - table[index]);
}

/// Hands rendered tables from the thread that edits the curve to the render thread without a
/// lock, as a seqlock: the sequence number is odd while a write is in progress, so a reader that
/// catches a half-written table can tell and keep the one it has.
///
/// Only one thread may publish at a time.
struct CurveExchange {
    std::array<float, kCurveTableSize> shared{};
    uint32_t sequence = 0;

    void publish(const float* table) {
        atomics::incrementAcquireRelease(sequence);
        std::memcpy(shared.data(), table, sizeof(float) * size_t(kCurveTableSize));
        atomics::incrementAcquireRelease(sequence);
    }

    /// Copies a newly published table into `destination`. Returns false if there is nothing new,
    /// or if every attempt collided with a write; the caller then keeps its current table and
    /// asks again next block. `observed` remembers the last table taken.
    bool fetch(std::array<float, kCurveTableSize>& destination, uint32_t& observed) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            const uint32_t before = atomics::loadAcquire(sequence);
            if (before == observed) {
                return false;
            }
            if ((before & 1u) != 0u) {
                continue;
            }
            std::memcpy(destination.data(), shared.data(), sizeof(float) * size_t(kCurveTableSize));
            if (atomics::loadAcquire(sequence) == before) {
                observed = before;
                return true;
            }
        }
        return false;
    }
};

} // namespace bdd

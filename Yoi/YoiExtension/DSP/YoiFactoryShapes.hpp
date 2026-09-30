//
//  YoiFactoryShapes.hpp
//  YoiExtension
//
//  Built-in drawings for YOI's envelope. The first is the default for a new instance; they are
//  also what the temporary development panel offers until the HTML editor lets you draw.
//

#pragma once

#include <array>

#include "Shared/BDDDrawnCurve.hpp"

namespace yoi {

struct FactoryShape {
    const char* name;
    int count;
    std::array<bdd::CurvePoint, 8> points;
};

inline constexpr std::array<FactoryShape, 8> kFactoryShapes = {{
    // Traced from the concept mock-up: a slow swell, a plateau, a peak and a sharp drop.
    { "Mock-up", 5, {{ { 0.0f, 0.33f, 0.6f }, { 0.57f, 0.62f, 0.0f }, { 0.76f, 0.62f, 0.0f },
                       { 0.89f, 0.88f, 0.0f }, { 1.0f, 0.33f, 0.0f } }} },
    // One smooth hump per pass: the classic wobble.
    { "Wub", 3, {{ { 0.0f, 0.05f, -0.5f }, { 0.5f, 1.0f, 0.5f }, { 1.0f, 0.05f, 0.0f } }} },
    // Opens slowly, then faster and faster.
    { "Rise", 2, {{ { 0.0f, 0.0f, 0.5f }, { 1.0f, 1.0f, 0.0f } }} },
    // Snaps open and dies away.
    { "Pluck", 2, {{ { 0.0f, 1.0f, -0.7f }, { 1.0f, 0.0f, 0.0f } }} },
    // Open for the first half, shut for the second.
    { "Gate", 4, {{ { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }} },
    // Three rounded bumps, for a talking, vowel-like movement.
    { "Talk", 7, {{ { 0.0f, 0.2f, -0.4f }, { 0.18f, 0.85f, 0.4f }, { 0.33f, 0.35f, -0.4f }, { 0.5f, 0.95f, 0.4f },
                    { 0.68f, 0.3f, -0.4f }, { 0.84f, 0.75f, 0.4f }, { 1.0f, 0.2f, 0.0f } }} },
    // Four steps up.
    { "Steps", 8, {{ { 0.0f, 0.1f, 0.0f }, { 0.25f, 0.1f, 0.0f }, { 0.25f, 0.4f, 0.0f }, { 0.5f, 0.4f, 0.0f },
                     { 0.5f, 0.7f, 0.0f }, { 0.75f, 0.7f, 0.0f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }} },
    // A flat line at the top: the envelope does nothing, so the cutoff sits exactly on CUTOFF.
    // A clean start for drawing. Kept last so the first drawing stays the default.
    { "Init", 2, {{ { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }} },
}};

} // namespace yoi

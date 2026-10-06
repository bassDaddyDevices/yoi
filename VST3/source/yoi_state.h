//
//  yoi_state.h
//  YOI VST3
//
//  Everything that makes up a YOI sound, and its round trip through a bdd-preset document. The
//  processor's saved state, the controller's copy of it, factory presets and user presets all go
//  through here, so a session and a preset file are the same format
//  (YOI_DOCS/specs/preset-format.md).
//

#pragma once

#include "yoi_params.h"

#include "shared/bdd_preset_file.h"

#include <string>
#include <vector>

namespace yoi {

inline constexpr const char* kSynthName = "YOI";

struct State {
    /// Plain value of every host parameter, in `params()` order.
    std::vector<float> plain;
    bool bypass = false;
    /// The drawing, already cleaned up by the kernel's rules (bdd::sanitizeCurve).
    std::vector<bdd::CurvePoint> drawing;
};

/// Every parameter at its default and Init's drawing (factory drawing 0, as the AU's Init uses).
State defaultState();

/// `points` cleaned up exactly as the kernel will clean them.
std::vector<bdd::CurvePoint> cleanDrawing(const bdd::CurvePoint* points, int count);

/// Factory drawing `index`, cleaned.
std::vector<bdd::CurvePoint> factoryDrawing(int index);

/// The preset's sound: defaults first, so anything it doesn't mention (a parameter newer than the
/// preset) is at its default; then its values, clamped to each range. Unknown identifiers are
/// ignored. No drawing means Init's.
State stateFromPreset(const bdd::vst3::PresetFile& preset);

/// A complete document: every parameter, the drawing, and (when `includeBypass`) the bypass
/// switch, which belongs to a session rather than a sound.
bdd::vst3::PresetFile presetFromState(const State& state, const std::string& name, bool includeBypass);

} // namespace yoi

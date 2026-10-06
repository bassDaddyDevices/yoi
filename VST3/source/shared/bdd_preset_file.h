//
//  bdd_preset_file.h
//  Bass Daddy Devices VST3 glue
//
//  The portable bdd-preset format (YOI_DOCS/specs/preset-format.md), read and written in C++.
//  The Audio Unit reads the same files with PresetFile.swift. Shared by every synth in the family.
//
//      { "format": "bdd-preset", "version": 1, "synth": "YOI", "number": 2, "name": "Basic Growl",
//        "parameters": { "cutoff": 408.7088, ... }, "drawing": [[x, y, bend], ...] }
//
//  The VST3's saved state is a document in this format too, so a session and a preset file are
//  the same thing.
//

#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bdd::vst3 {

struct PresetFile {
    static constexpr const char* kFormat = "bdd-preset";
    static constexpr int kVersion = 1;

    int version = kVersion;
    std::string synth;
    /// A factory preset's permanent number (1 and up). 0 when the document has none: user
    /// presets and saved state don't need one.
    int number = 0;
    std::string name;
    /// Identifier -> plain value. Only finite values are kept.
    std::map<std::string, double> parameters;
    /// [x, y, bend] points, absent when the document has no drawing.
    std::optional<std::vector<std::array<float, 3>>> drawing;
    /// Keys this format doesn't define but the writer wants kept alongside (the VST3's bypass
    /// switch in its state, for example). Readers that don't know them ignore them.
    std::map<std::string, double> extras;
};

struct PresetReadResult {
    std::optional<PresetFile> preset;
    std::string error;   ///< Why it wasn't read; empty when `preset` is set.
};

/// Reads a document. Refuses another format, a newer version or another synth (if `synth` is
/// not empty); never throws.
PresetReadResult readPreset(std::string_view json, std::string_view synth);

/// Writes a document: parameters sorted by identifier, one per line, each number as the shortest
/// text that reads back as the same 32-bit float, so diffs stay readable.
std::string writePreset(const PresetFile& preset);

/// The shortest text that reads back as `value` as a 32-bit float, without a trailing ".0".
std::string shortestFloat(float value);

} // namespace bdd::vst3

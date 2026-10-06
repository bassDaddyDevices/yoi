//
//  yoi_params.h
//  YOI VST3
//
//  YOI's host parameters: the same 36 the Audio Unit's Parameters.swift publishes, with the same
//  IDs (the AU addresses), identifiers, names, groups, ranges, defaults, units and options, in the
//  same order. tests/yoi_vst3_tests.cpp checks this table against the AU's own descriptor (the
//  stand-in snapshot Tools/update-standin.sh takes from the built AU), so the two can't drift.
//
//  Plus the parameters only the VST3 needs: bypass, the hidden read-only display values the
//  processor sends the editor, and the pitch bend the host's MIDI mapping arrives on.
//

#pragma once

#include "shared/bdd_param_spec.h"

#include "YoiExtensionDSPKernel.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace yoi {

using bdd::vst3::ParamSpec;
using bdd::vst3::Unit;

// MARK: - Parameters only the VST3 has

/// The host's bypass switch. The AU gets this from the host for free; VST3 wants a parameter.
inline constexpr uint32_t kBypassId = 1000;

/// Hidden, read-only parameters the processor writes the editor's display into: VST3 keeps the
/// two halves of a plug-in apart, and this is the sanctioned way across. Each is mapped onto 0...1
/// by its `DisplayRange`.
enum DisplayId : uint32_t {
    kDisplayPositionId = 2000,     ///< where the envelope reads the drawing, 0...1
    kDisplayValueId,               ///< what it read there, 0...1
    kDisplayCutoffId,              ///< the filter's cutoff as the kernel set it, Hz
    kDisplayQId,                   ///< the filter's Q after the RES remap
    kDisplayCutoffTopId,           ///< the cutoff at the top of the drawing, Hz
    kDisplayCutoffBottomId,        ///< ... and at the bottom, Hz
    kDisplayMeterLeftId,           ///< output peaks after the limiter, linear (1 = 0 dBFS)
    kDisplayMeterRightId,
    kDisplayEnd,
};
inline constexpr int kDisplayCount = int(kDisplayEnd - kDisplayPositionId);

struct DisplayRange {
    double minimum;
    double maximum;
    bool logarithmic;

    double normalise(double value) const {
        const double clamped = std::clamp(value, minimum, maximum);
        return logarithmic ? std::log(clamped / minimum) / std::log(maximum / minimum)
                           : (clamped - minimum) / (maximum - minimum);
    }
    double denormalise(double normalised) const {
        const double clamped = std::clamp(normalised, 0.0, 1.0);
        return logarithmic ? minimum * std::pow(maximum / minimum, clamped) : minimum + clamped * (maximum - minimum);
    }
};

inline DisplayRange displayRange(uint32_t id) {
    switch (id) {
        case kDisplayCutoffId:
        case kDisplayCutoffTopId:
        case kDisplayCutoffBottomId: return { 1.0, 30000.0, true };
        case kDisplayQId: return { 0.01, 1000.0, true };
        // Peaks above full scale survive the trip: the meter's clip light needs to see past 1.
        case kDisplayMeterLeftId:
        case kDisplayMeterRightId: return { 0.0, 4.0, false };
        default: return { 0.0, 1.0, false };
    }
}

/// Pitch bend. Hosts deliver the wheel as a parameter, through the controller's MIDI mapping.
/// 0...1, with 0.5 at rest.
inline constexpr uint32_t kPitchBendId = 3000;

// MARK: - The host parameters

class ParamTable {
public:
    std::vector<ParamSpec> specs;

    const ParamSpec* find(uint32_t id) const {
        for (const auto& spec : specs) {
            if (spec.id == id) {
                return &spec;
            }
        }
        return nullptr;
    }

    int indexOf(uint32_t id) const {
        for (size_t index = 0; index < specs.size(); ++index) {
            if (specs[index].id == id) {
                return int(index);
            }
        }
        return -1;
    }

    int indexOf(const std::string& identifier) const {
        for (size_t index = 0; index < specs.size(); ++index) {
            if (specs[index].identifier == identifier) {
                return int(index);
            }
        }
        return -1;
    }

    ParamTable() {
        using A = YoiExtensionParameterAddress;
        auto add = [this](uint32_t id, const char* identifier, const char* name, const char* group, Unit unit,
                          float minimum, float maximum, float defaultValue, bool logarithmic = false,
                          std::vector<std::string> options = {}) {
            specs.push_back({ id, identifier, name, group, unit, minimum, maximum, defaultValue, logarithmic,
                              std::move(options) });
        };

        // The option lists the kernel owns, as Parameters.swift reads them.
        std::vector<std::string> syncLengths;
        for (int index = 0; index < YoiExtensionDSPKernel::syncLengthCount(); ++index) {
            syncLengths.emplace_back(YoiExtensionDSPKernel::syncLengthName(index));
        }
        std::vector<std::string> directions;
        for (int index = 0; index < YoiExtensionDSPKernel::directionCount(); ++index) {
            directions.emplace_back(YoiExtensionDSPKernel::directionName(index));
        }

        // In Parameters.swift's order. Logarithmic as the AU reports it: Glide Time is declared
        // logarithmic there, but its range starts at 0, so the AU publishes it as linear.
        add(A::macroVoice, "macroVoice", "Voice", "macros", Unit::percent, 0, 100, 0);
        add(A::macroThroat, "macroThroat", "Throat", "macros", Unit::percent, 0, 100, 100);
        add(A::macroPower, "macroPower", "Power", "macros", Unit::percent, 0, 100, 0);
        add(A::macroControl, "macroControl", "Control", "macros", Unit::percent, 0, 100, 0);
        add(A::macroWidth, "macroWidth", "Width", "macros", Unit::percent, 0, 100, 0);

        add(A::outputLevel, "outputLevel", "Output Level", "output", Unit::decibels, -48, 6, 0);

        add(A::glideTime, "glideTime", "Glide Time", "voice", Unit::milliseconds, 0, 2000, 60);
        add(A::glideMode, "glideMode", "Glide Mode", "voice", Unit::indexed, 0, 1, 0, false, { "Legato", "Always" });
        add(A::bendRange, "bendRange", "Bend Range", "voice", Unit::semitones, 0, 24, 2);

        add(A::subOctave, "subOctave", "Sub Octave", "oscillators", Unit::indexed, 0, 1, 0, false, { "-1 Oct", "-2 Oct" });
        add(A::subCrossover, "subCrossover", "Sub Crossover", "oscillators", Unit::hertz, 50, 700, 130, true);
        add(A::subFollow, "subFollow", "Sub Follow", "oscillators", Unit::percent, 0, 100, 0);

        add(A::filterMode, "filterMode", "Filter Mode", "filter", Unit::indexed, 0, 1, 0, false, { "LP", "BP" });
        add(A::cutoff, "cutoff", "Cutoff", "filter", Unit::hertz, 20, 2500, 800, true);
        add(A::resonance, "resonance", "Resonance", "filter", Unit::percent, 0, 100, 30);
        add(A::filterMirror, "filterMirror", "Mirror", "filter", Unit::percent, 0, 100, 0);

        add(A::ampAttack, "ampAttack", "Attack", "amp", Unit::milliseconds, 0.1f, 5000, 3, true);
        add(A::ampDecay, "ampDecay", "Decay", "amp", Unit::milliseconds, 1, 5000, 300, true);
        add(A::ampSustain, "ampSustain", "Sustain", "amp", Unit::percent, 0, 100, 100);
        add(A::ampRelease, "ampRelease", "Release", "amp", Unit::milliseconds, 1, 10000, 150, true);

        add(A::envAmount, "envAmount", "Env Amount", "envelope", Unit::octaves, 0, 8, 3);
        add(A::envTimeMode, "envTimeMode", "Env Time Mode", "envelope", Unit::indexed, 0, 1, 0, false, { "Sync", "Free" });
        add(A::envSyncLength, "envSyncLength", "Env Sync", "envelope", Unit::indexed, 0, float(syncLengths.size() - 1), 16,
            false, syncLengths);
        add(A::envFreeTime, "envFreeTime", "Env Free Time", "envelope", Unit::milliseconds, 10, 30000, 500, true);
        add(A::envDirection, "envDirection", "Env Direction", "envelope", Unit::indexed, 0, float(directions.size() - 1), 0,
            false, directions);
        add(A::envAccelerate, "envAccelerate", "Env Accelerate", "envelope", Unit::indexed, 0, 1, 0, false, { "Off", "On" });
        add(A::envRetrigger, "envRetrigger", "Env Re-Trigger", "envelope", Unit::boolean, 0, 1, 0);
        add(A::accelStart, "accelStart", "Accel Start", "envelope", Unit::rate, 0.1f, 4, 0.25f, true);
        add(A::accelEnd, "accelEnd", "Accel End", "envelope", Unit::rate, 0.1f, 4, 2, true);
        add(A::accelCurve, "accelCurve", "Accel Curve", "envelope", Unit::generic, -1, 1, 0);

        add(A::dsMode, "dsMode", "Downsampler", "grit", Unit::indexed, 0, 2, 1, false, { "Off", "S&H", "Downsample" });
        add(A::foldAmount, "foldAmount", "Fold", "grit", Unit::percent, 0, 5, 0);
        add(A::foldPosition, "foldPosition", "Fold Position", "grit", Unit::indexed, 0, 2, 2, false,
            { "Pre-filter", "Pre-downsample", "Post-downsample" });
        add(A::cleanupMode, "cleanupMode", "Clean-up", "grit", Unit::indexed, 0, 1, 1, false, { "Off", "On" });
        add(A::dsLock, "dsLock", "S&H Lock", "grit", Unit::indexed, 0, 1, 0, false, { "Free", "Lock" });

        add(A::ottTime, "ottTime", "OTT Time", "finish", Unit::percent, 0, 100, 50);
    }

    ParamTable(const ParamTable&) = delete;
    ParamTable& operator=(const ParamTable&) = delete;
};

/// The shared, immutable table, built on first use.
inline const ParamTable& params() {
    static const ParamTable table;
    return table;
}

/// Parameters whose reading the plug-in owns, as the AU's `derivedDisplayIdentifiers`: OTT TIME
/// is stored as a percent, but means the compressor's release in milliseconds, which only the
/// kernel knows. Returns an empty string for every other parameter.
inline std::string derivedDisplay(const std::string& identifier, float value) {
    if (identifier == "ottTime") {
        return std::to_string(std::lround(YoiExtensionDSPKernel::ottReleaseMilliseconds(value))) + " ms";
    }
    return {};
}

} // namespace yoi

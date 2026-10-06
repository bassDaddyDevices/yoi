//
//  bdd_param_spec.h
//  Bass Daddy Devices VST3 glue
//
//  How one host parameter is described, and the conversions between the host's 0...1 normalised
//  values and the plain values the kernels take. Shared by every synth in the family: nothing
//  here knows which synth it is describing.
//
//  The text a value is shown as matches the Audio Unit's `implementorStringFromValueCallback`,
//  so a parameter reads the same in both formats.
//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bdd::vst3 {

/// The AU's parameter units, by the names the editor's descriptor uses.
enum class Unit {
    generic,
    percent,
    decibels,
    hertz,
    milliseconds,
    octaves,
    semitones,
    rate,
    ratio,
    indexed,
    boolean,
};

/// The descriptor's name for a unit, as the AU's editor bridge sends it.
inline const char* unitName(Unit unit) {
    switch (unit) {
        case Unit::percent: return "percent";
        case Unit::decibels: return "db";
        case Unit::hertz: return "hz";
        case Unit::milliseconds: return "ms";
        case Unit::octaves: return "octaves";
        case Unit::semitones: return "semitones";
        case Unit::rate: return "rate";
        case Unit::ratio: return "ratio";
        case Unit::indexed: return "indexed";
        case Unit::boolean: return "boolean";
        case Unit::generic: return "generic";
    }
    return "generic";
}

/// What the host shows after the number. ASCII only: VST3 unit strings go through `fromAscii`.
inline const char* unitLabel(Unit unit) {
    switch (unit) {
        case Unit::percent: return "%";
        case Unit::decibels: return "dB";
        case Unit::hertz: return "Hz";
        case Unit::milliseconds: return "ms";
        case Unit::octaves: return "oct";
        case Unit::semitones: return "st";
        case Unit::rate: return "x";
        default: return "";
    }
}

struct ParamSpec {
    uint32_t id;               ///< The AU's parameter address; the VST3 parameter ID.
    std::string identifier;    ///< Persistent: preset files and state are keyed by it.
    std::string name;
    std::string group;         ///< The AU parameter group's identifier, for the editor.
    Unit unit = Unit::generic;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    /// Equal ratios across the host's 0...1 range rather than equal steps (frequencies, times).
    /// As the AU reports it: only ever with a minimum above 0.
    bool logarithmic = false;
    /// Names of an indexed parameter's values, from `minimum` up.
    std::vector<std::string> options;

    bool isDiscrete() const { return unit == Unit::indexed || unit == Unit::boolean; }

    /// 0 for continuous, otherwise the number of steps as VST3 counts them (values - 1).
    int stepCount() const { return isDiscrete() ? int(std::lround(maximum - minimum)) : 0; }

    float clamp(float plain) const { return std::clamp(plain, minimum, maximum); }
};

inline float toPlain(const ParamSpec& spec, double normalised) {
    const double clamped = std::clamp(normalised, 0.0, 1.0);
    if (spec.logarithmic && spec.minimum > 0.0f) {
        return float(double(spec.minimum) * std::pow(double(spec.maximum) / double(spec.minimum), clamped));
    }
    if (const int steps = spec.stepCount(); steps > 0) {
        return spec.minimum + float(std::min(double(steps), std::floor(clamped * double(steps + 1))));
    }
    return float(spec.minimum + clamped * (double(spec.maximum) - double(spec.minimum)));
}

inline double toNormalised(const ParamSpec& spec, float plain) {
    const double span = double(spec.maximum) - double(spec.minimum);
    if (span <= 0.0) {
        return 0.0;
    }
    const double clamped = std::clamp(double(plain), double(spec.minimum), double(spec.maximum));
    if (spec.logarithmic && spec.minimum > 0.0f) {
        return std::log(clamped / double(spec.minimum)) / std::log(double(spec.maximum) / double(spec.minimum));
    }
    if (const int steps = spec.stepCount(); steps > 0) {
        return std::round(clamped - double(spec.minimum)) / double(steps);
    }
    return (clamped - double(spec.minimum)) / span;
}

/// A value as the AU shows it: "800 Hz", "1.20 kHz", "60 ms", "2.00 s", "75%", "Sync".
inline std::string formatValue(const ParamSpec& spec, float value) {
    char text[64] = {};
    switch (spec.unit) {
        case Unit::decibels: std::snprintf(text, sizeof(text), "%.1f dB", double(value)); break;
        case Unit::percent: std::snprintf(text, sizeof(text), "%.0f%%", double(value)); break;
        case Unit::hertz:
            if (value >= 1000.0f) {
                std::snprintf(text, sizeof(text), "%.2f kHz", double(value) / 1000.0);
            } else {
                std::snprintf(text, sizeof(text), "%.0f Hz", double(value));
            }
            break;
        case Unit::milliseconds:
            if (value >= 1000.0f) {
                std::snprintf(text, sizeof(text), "%.2f s", double(value) / 1000.0);
            } else {
                std::snprintf(text, sizeof(text), value < 10.0f ? "%.1f ms" : "%.0f ms", double(value));
            }
            break;
        case Unit::semitones: std::snprintf(text, sizeof(text), "%.0f st", double(value)); break;
        case Unit::octaves: std::snprintf(text, sizeof(text), "%.1f oct", double(value)); break;
        case Unit::rate: std::snprintf(text, sizeof(text), "%.2fx", double(value)); break;
        case Unit::ratio: std::snprintf(text, sizeof(text), "x%.1f", double(value)); break;
        case Unit::boolean: return value >= 0.5f ? "On" : "Off";
        case Unit::indexed: {
            const long index = std::lround(value - spec.minimum);
            if (index >= 0 && size_t(index) < spec.options.size()) {
                return spec.options[size_t(index)];
            }
            std::snprintf(text, sizeof(text), "%ld", index);
            break;
        }
        case Unit::generic: std::snprintf(text, sizeof(text), "%.2f", double(value)); break;
    }
    return text;
}

} // namespace bdd::vst3

//
//  bdd_preset_file.cpp
//  Bass Daddy Devices VST3 glue
//

#include "bdd_preset_file.h"

#include "choc/text/choc_JSON.h"

#include <charconv>
#include <cmath>

namespace bdd::vst3 {

namespace {

constexpr const char* kKnownKeys[] = { "format", "version", "synth", "number", "name", "parameters", "drawing" };

bool isKnownKey(std::string_view key) {
    for (const char* known : kKnownKeys) {
        if (key == known) {
            return true;
        }
    }
    return false;
}

bool isNumber(const choc::value::ValueView& value) {
    return value.isFloat() || value.isInt();
}

} // namespace

std::string shortestFloat(float value) {
    if (!std::isfinite(value)) {
        return "0";
    }
    if (value == 0.0f) {
        return "0";   // never "-0"
    }
    char buffer[32] = {};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

PresetReadResult readPreset(std::string_view json, std::string_view synth) {
    PresetReadResult result;
    choc::value::Value root;
    try {
        root = choc::json::parse(json);
    } catch (const std::exception& error) {
        result.error = std::string("not valid JSON: ") + error.what();
        return result;
    }
    if (!root.isObject() || !root.hasObjectMember("format") || !root["format"].isString()
        || root["format"].getString() != PresetFile::kFormat) {
        result.error = "not a bdd-preset document";
        return result;
    }

    PresetFile preset;
    preset.version = root.hasObjectMember("version") ? int(root["version"].getWithDefault<int64_t>(0)) : 0;
    if (preset.version < 1) {
        result.error = "missing or invalid version";
        return result;
    }
    if (preset.version > PresetFile::kVersion) {
        result.error = "written by a newer version (format version " + std::to_string(preset.version) + ")";
        return result;
    }
    if (root.hasObjectMember("synth") && root["synth"].isString()) {
        preset.synth = std::string(root["synth"].getString());
    }
    if (!synth.empty() && preset.synth != synth) {
        result.error = "for another synth (" + preset.synth + ")";
        return result;
    }
    if (root.hasObjectMember("number") && isNumber(root["number"])) {
        preset.number = int(root["number"].getWithDefault<int64_t>(0));
    }
    if (root.hasObjectMember("name") && root["name"].isString()) {
        preset.name = std::string(root["name"].getString());
    }

    if (root.hasObjectMember("parameters")) {
        const auto parameters = root["parameters"];
        if (!parameters.isObject()) {
            result.error = "\"parameters\" is not an object";
            return result;
        }
        for (uint32_t index = 0; index < parameters.size(); ++index) {
            const auto member = parameters.getObjectMemberAt(index);
            if (!isNumber(member.value)) {
                continue;
            }
            const double value = member.value.getWithDefault<double>(0.0);
            if (std::isfinite(value)) {
                preset.parameters[std::string(member.name)] = value;
            }
        }
    }

    if (root.hasObjectMember("drawing")) {
        const auto drawing = root["drawing"];
        if (!drawing.isArray()) {
            result.error = "\"drawing\" is not an array";
            return result;
        }
        std::vector<std::array<float, 3>> points;
        for (uint32_t index = 0; index < drawing.size(); ++index) {
            const auto point = drawing[index];
            if (!point.isArray() || point.size() != 3 || !isNumber(point[0]) || !isNumber(point[1]) || !isNumber(point[2])) {
                result.error = "a drawing point is not [x, y, bend]";
                return result;
            }
            points.push_back({ float(point[0].getWithDefault<double>(0.0)), float(point[1].getWithDefault<double>(0.0)),
                               float(point[2].getWithDefault<double>(0.0)) });
        }
        preset.drawing = std::move(points);
    }

    for (uint32_t index = 0; index < root.size(); ++index) {
        const auto member = root.getObjectMemberAt(index);
        if (!isKnownKey(member.name) && isNumber(member.value)) {
            preset.extras[std::string(member.name)] = member.value.getWithDefault<double>(0.0);
        } else if (!isKnownKey(member.name) && member.value.isBool()) {
            preset.extras[std::string(member.name)] = member.value.getBool() ? 1.0 : 0.0;
        }
    }

    result.preset = std::move(preset);
    return result;
}

std::string writePreset(const PresetFile& preset) {
    using choc::json::getEscapedQuotedString;
    std::string text = "{\n";
    text += "  \"format\": \"" + std::string(PresetFile::kFormat) + "\",\n";
    text += "  \"version\": " + std::to_string(PresetFile::kVersion) + ",\n";
    text += "  \"synth\": " + getEscapedQuotedString(preset.synth) + ",\n";
    if (preset.number > 0) {
        text += "  \"number\": " + std::to_string(preset.number) + ",\n";
    }
    text += "  \"name\": " + getEscapedQuotedString(preset.name) + ",\n";
    for (const auto& [key, value] : preset.extras) {
        text += "  " + getEscapedQuotedString(key) + ": " + shortestFloat(float(value)) + ",\n";
    }

    text += "  \"parameters\": {\n";
    size_t written = 0;
    for (const auto& [identifier, value] : preset.parameters) {   // std::map: sorted
        text += "    " + getEscapedQuotedString(identifier) + ": " + shortestFloat(float(value));
        text += (++written < preset.parameters.size()) ? ",\n" : "\n";
    }
    text += "  }";

    if (preset.drawing) {
        text += ",\n  \"drawing\": [\n";
        for (size_t index = 0; index < preset.drawing->size(); ++index) {
            const auto& point = (*preset.drawing)[index];
            text += "    [" + shortestFloat(point[0]) + ", " + shortestFloat(point[1]) + ", " + shortestFloat(point[2]) + "]";
            text += (index + 1 < preset.drawing->size()) ? ",\n" : "\n";
        }
        text += "  ]";
    }
    text += "\n}\n";
    return text;
}

} // namespace bdd::vst3

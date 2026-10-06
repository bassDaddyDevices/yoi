//
//  bdd_preset_library.cpp
//  Bass Daddy Devices VST3 glue
//

#include "bdd_preset_library.h"

#include "bdd_resources.h"
#include "bdd_text_files.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <system_error>

namespace bdd::vst3 {

namespace {

constexpr std::string_view kFactoryPrefix = "/factory/";

bool sameNameIgnoringCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

bool lessIgnoringCase(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

} // namespace

std::vector<PresetFile> PresetLibrary::factoryPresets(const std::string& synth) {
    std::map<int, PresetFile> byNumber;
    for (const auto* resource = resourcesBegin(); resource != resourcesEnd(); ++resource) {
        const std::string_view path(resource->path);
        if (path.substr(0, kFactoryPrefix.size()) != kFactoryPrefix) {
            continue;
        }
        auto read = readPreset(resource->text(), synth);
        if (!read.preset || read.preset->number < 1 || byNumber.count(read.preset->number) != 0) {
            continue;
        }
        byNumber.emplace(read.preset->number, std::move(*read.preset));
    }
    std::vector<PresetFile> presets;
    for (auto& [number, preset] : byNumber) {
        presets.push_back(std::move(preset));
    }
    return presets;
}

PresetLibrary::PresetLibrary(std::string synth, std::filesystem::path userFolder)
    : mSynth(std::move(synth)), mUserFolder(std::move(userFolder)), mFactory(factoryPresets(mSynth)) {
    reloadUser();
}

void PresetLibrary::reloadUser() {
    mUser.clear();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(mUserFolder, error)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        std::string text;
        if (!readTextFile(entry.path(), text)) {
            continue;
        }
        const auto read = readPreset(text, mSynth);
        if (!read.preset) {
            continue;
        }
        // The name inside the file is the preset's name; the file's name only has to be unique.
        const std::string name = read.preset->name.empty() ? utf8FromPath(entry.path().stem()) : read.preset->name;
        mUser.push_back({ name, entry.path() });
    }
    std::sort(mUser.begin(), mUser.end(), [](const UserPreset& a, const UserPreset& b) { return lessIgnoringCase(a.name, b.name); });
}

std::vector<PresetLibrary::Entry> PresetLibrary::list() {
    reloadUser();
    std::vector<Entry> entries;
    entries.push_back({ 0, "Init", false });
    for (const auto& preset : mFactory) {
        entries.push_back({ preset.number, preset.name, false });
    }
    for (size_t index = 0; index < mUser.size(); ++index) {
        entries.push_back({ -int(index) - 1, mUser[index].name, true });
    }
    return entries;
}

std::optional<PresetFile> PresetLibrary::load(int number) const {
    if (number > 0) {
        for (const auto& preset : mFactory) {
            if (preset.number == number) {
                return preset;
            }
        }
        return std::nullopt;
    }
    if (number < 0 && size_t(-number - 1) < mUser.size()) {
        std::string text;
        if (readTextFile(mUser[size_t(-number - 1)].file, text)) {
            if (auto read = readPreset(text, mSynth); read.preset) {
                read.preset->name = mUser[size_t(-number - 1)].name;
                return read.preset;
            }
        }
    }
    return std::nullopt;
}

std::string PresetLibrary::saveUser(const std::string& name, PresetFile preset, int& savedNumber) {
    // The Audio Unit's messages, so the panel reads the same in both formats.
    const std::string cleanName = trimmed(name);
    if (cleanName.empty()) {
        return "Enter a preset name.";
    }
    reloadUser();
    for (const auto& existing : mUser) {
        if (sameNameIgnoringCase(existing.name, cleanName)) {
            return "A user preset already has that name.";
        }
    }

    preset.name = cleanName;
    preset.number = 0;   // user presets have no permanent number
    preset.synth = mSynth;

    // A file name of its own, even if two names clean up the same way.
    const std::string base = fileNameFor(cleanName);
    std::filesystem::path file = mUserFolder / pathFromUTF8(base + ".json");
    std::error_code error;
    for (int suffix = 2; std::filesystem::exists(file, error); ++suffix) {
        file = mUserFolder / pathFromUTF8(base + " " + std::to_string(suffix) + ".json");
    }
    if (!writeTextFileAtomically(file, writePreset(preset))) {
        return "Could not save that preset.";
    }

    reloadUser();
    savedNumber = 0;
    for (size_t index = 0; index < mUser.size(); ++index) {
        if (mUser[index].file == file) {
            savedNumber = -int(index) - 1;
        }
    }
    return {};
}

std::string PresetLibrary::removeUser(int number) {
    if (number >= 0 || size_t(-number - 1) >= mUser.size()) {
        return "That preset is no longer available.";
    }
    std::error_code error;
    if (!std::filesystem::remove(mUser[size_t(-number - 1)].file, error)) {
        return "Could not delete that preset.";
    }
    reloadUser();
    return {};
}

} // namespace bdd::vst3

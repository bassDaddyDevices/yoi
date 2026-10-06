//
//  bdd_preset_library.h
//  Bass Daddy Devices VST3 glue
//
//  Factory presets (compiled in from Presets/Factory, the same files the Audio Unit bundles) and
//  the user's presets (bdd-preset files in the synth's Presets folder), listed the way the
//  editor's preset menu expects: factory presets by their permanent number, 0 being the built-in
//  Init; user presets numbered -1, -2, ... in name order. Shared by every synth.
//

#pragma once

#include "bdd_preset_file.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bdd::vst3 {

class PresetLibrary {
public:
    struct Entry {
        int number = 0;
        std::string name;
        bool user = false;
    };

    PresetLibrary(std::string synth, std::filesystem::path userFolder);

    /// Init, the factory presets, then the user's. Reads the user folder again first, since
    /// another instance may have saved.
    std::vector<Entry> list();

    /// The preset with this number, as listed by the last `list()`. Init (0) is not a file: the
    /// caller resets to defaults for it, so it returns nothing.
    std::optional<PresetFile> load(int number) const;

    /// Saves `preset` as a new user preset called `name`. Returns an empty string, or the message
    /// to show; on success `savedNumber` is its number in the new list.
    std::string saveUser(const std::string& name, PresetFile preset, int& savedNumber);

    /// Returns an empty string, or the message to show.
    std::string removeUser(int number);

    const std::filesystem::path& userFolder() const { return mUserFolder; }

    /// The factory presets compiled into the plug-in for `synth`, ordered by number. Damaged files,
    /// newer versions, numbers below 1 and duplicate numbers are skipped.
    static std::vector<PresetFile> factoryPresets(const std::string& synth);

private:
    struct UserPreset {
        std::string name;
        std::filesystem::path file;
    };

    void reloadUser();

    std::string mSynth;
    std::filesystem::path mUserFolder;
    std::vector<PresetFile> mFactory;
    std::vector<UserPreset> mUser;
};

} // namespace bdd::vst3

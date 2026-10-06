//
//  bdd_drawing_library.h
//  Bass Daddy Devices VST3 glue
//
//  The user's own drawings, kept apart from presets: the C++ twin of the Audio Unit's
//  DrawingLibrary.swift, with the same file, the same format and the same rules. Shared by every
//  synth in the family.
//
//      { "version": 1, "drawings": [ { "name": "Wobble", "points": [[x, y, bend], ...] } ] }
//
//  A persistent format: whatever changes later, keep reading version 1.
//

#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace bdd::vst3 {

class DrawingLibrary {
public:
    struct Drawing {
        std::string name;
        std::vector<std::array<float, 3>> points;
    };

    static constexpr int kFormatVersion = 1;
    static constexpr size_t kMaximumNameLength = 80;

    explicit DrawingLibrary(std::filesystem::path file);

    /// Reads the file again. Called before every change, because other instances (in this host or
    /// another) may have saved since.
    void reload();

    const std::vector<Drawing>& drawings() const { return mDrawings; }

    /// Case-insensitive, as names are unique regardless of case.
    const Drawing* find(const std::string& name) const;

    /// Adds a drawing under a new name. Returns an empty string, or the message to show.
    std::string save(const std::string& name, const std::vector<std::array<float, 3>>& points);

    /// Returns an empty string, or the message to show.
    std::string remove(const std::string& name);

private:
    bool write();

    std::filesystem::path mFile;
    std::vector<Drawing> mDrawings;
    /// Set when the file on disk can't safely be rewritten (newer, or damaged), so saving never
    /// destroys drawings this version doesn't understand. The message to show, or empty.
    std::string mWriteBlocked;
};

} // namespace bdd::vst3

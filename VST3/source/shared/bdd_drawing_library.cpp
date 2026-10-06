//
//  bdd_drawing_library.cpp
//  Bass Daddy Devices VST3 glue
//

#include "bdd_drawing_library.h"

#include "bdd_preset_file.h"
#include "bdd_text_files.h"

#include "choc/text/choc_JSON.h"

#include <algorithm>
#include <cctype>

namespace bdd::vst3 {

namespace {

// The Audio Unit's messages (DrawingLibrary.LibraryError), word for word.
constexpr const char* kEmptyName = "Enter a name for the drawing.";
constexpr const char* kDuplicateName = "A drawing with that name already exists.";
constexpr const char* kNotFound = "That drawing is no longer available.";
constexpr const char* kNewerFormat = "Your drawings were saved by a newer version, so they can't be changed here.";
constexpr const char* kUnreadable = "Your drawings file couldn't be read, so it wasn't changed.";
constexpr const char* kWriteFailed = "Your drawings couldn't be saved.";

bool sameName(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
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

/// At most `limit` characters, never splitting a UTF-8 sequence.
std::string prefixCharacters(const std::string& text, size_t limit) {
    size_t characters = 0;
    for (size_t index = 0; index < text.size(); ++index) {
        if ((static_cast<unsigned char>(text[index]) & 0xC0) != 0x80) {
            if (characters == limit) {
                return text.substr(0, index);
            }
            ++characters;
        }
    }
    return text;
}

} // namespace

DrawingLibrary::DrawingLibrary(std::filesystem::path file) : mFile(std::move(file)) {
    reload();
}

void DrawingLibrary::reload() {
    mWriteBlocked.clear();
    mDrawings.clear();
    std::string text;
    if (!readTextFile(mFile, text)) {
        return;   // no file yet: an empty library
    }
    try {
        const auto root = choc::json::parse(text);
        const int version = int(root["version"].getWithDefault<int64_t>(0));
        const auto drawings = root["drawings"];
        if (version < 1 || !drawings.isArray()) {
            throw std::runtime_error("not a drawings file");
        }
        for (uint32_t index = 0; index < drawings.size(); ++index) {
            const auto entry = drawings[index];
            Drawing drawing;
            drawing.name = std::string(entry["name"].getString());
            const auto points = entry["points"];
            for (uint32_t p = 0; p < points.size(); ++p) {
                const auto point = points[p];
                drawing.points.push_back({ float(point[0].getWithDefault<double>(0.0)),
                                           float(point[1].getWithDefault<double>(0.0)),
                                           point.size() > 2 ? float(point[2].getWithDefault<double>(0.0)) : 0.0f });
            }
            mDrawings.push_back(std::move(drawing));
        }
        if (version > kFormatVersion) {
            mWriteBlocked = kNewerFormat;
        }
    } catch (...) {
        mDrawings.clear();
        mWriteBlocked = kUnreadable;
    }
}

const DrawingLibrary::Drawing* DrawingLibrary::find(const std::string& name) const {
    for (const auto& drawing : mDrawings) {
        if (sameName(drawing.name, name)) {
            return &drawing;
        }
    }
    return nullptr;
}

std::string DrawingLibrary::save(const std::string& name, const std::vector<std::array<float, 3>>& points) {
    reload();
    if (!mWriteBlocked.empty()) {
        return mWriteBlocked;
    }
    const std::string cleanName = prefixCharacters(trimmed(name), kMaximumNameLength);
    if (cleanName.empty()) {
        return kEmptyName;
    }
    if (find(cleanName) != nullptr) {
        return kDuplicateName;
    }
    mDrawings.push_back({ cleanName, points });
    if (!write()) {
        reload();
        return kWriteFailed;
    }
    return {};
}

std::string DrawingLibrary::remove(const std::string& name) {
    reload();
    if (!mWriteBlocked.empty()) {
        return mWriteBlocked;
    }
    const auto found = std::find_if(mDrawings.begin(), mDrawings.end(),
                                    [&](const Drawing& drawing) { return sameName(drawing.name, name); });
    if (found == mDrawings.end()) {
        return kNotFound;
    }
    mDrawings.erase(found);
    if (!write()) {
        reload();
        return kWriteFailed;
    }
    return {};
}

bool DrawingLibrary::write() {
    // The same layout the Audio Unit's JSONEncoder writes (sorted keys), so the files diff cleanly.
    using choc::json::getEscapedQuotedString;
    std::string text = "{\n  \"drawings\" : [\n";
    for (size_t index = 0; index < mDrawings.size(); ++index) {
        const auto& drawing = mDrawings[index];
        text += "    {\n      \"name\" : " + getEscapedQuotedString(drawing.name) + ",\n      \"points\" : [\n";
        for (size_t p = 0; p < drawing.points.size(); ++p) {
            const auto& point = drawing.points[p];
            text += "        [\n          " + shortestFloat(point[0]) + ",\n          " + shortestFloat(point[1])
                  + ",\n          " + shortestFloat(point[2]) + "\n        ]";
            text += (p + 1 < drawing.points.size()) ? ",\n" : "\n";
        }
        text += "      ]\n    }";
        text += (index + 1 < mDrawings.size()) ? ",\n" : "\n";
    }
    text += "  ],\n  \"version\" : " + std::to_string(kFormatVersion) + "\n}";
    return writeTextFileAtomically(mFile, text);
}

} // namespace bdd::vst3

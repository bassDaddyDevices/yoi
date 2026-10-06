//
//  bdd_text_files.h
//  Bass Daddy Devices VST3 glue
//
//  Small, careful file helpers for the synth's own JSON files. Shared by every synth.
//

#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace bdd::vst3 {

/// Largest file read. Presets and drawing libraries are a few kilobytes.
inline constexpr std::uintmax_t kMaxTextFileSize = 1024 * 1024;

/// UTF-8 text to a path and back. `std::filesystem` would otherwise use the system code page on
/// Windows, which mangles anything outside ASCII.
inline std::filesystem::path pathFromUTF8(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

inline std::string utf8FromPath(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

inline bool readTextFile(const std::filesystem::path& file, std::string& out) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error) || std::filesystem::file_size(file, error) > kMaxTextFileSize || error) {
        return false;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::stringstream text;
    text << stream.rdbuf();
    out = text.str();
    return true;
}

/// Writes beside the destination and swaps it in, so a failed write never leaves half a file.
inline bool writeTextFileAtomically(const std::filesystem::path& file, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    const auto temporary = std::filesystem::path(file).concat(".tmp");
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << text;
        if (!stream) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

/// A user-supplied name as a file name: separators, control characters and characters Windows
/// refuses become "-", leading dots and spaces go, and it is never empty.
inline std::string fileNameFor(const std::string& name) {
    std::string out;
    for (char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        const bool refused = byte < 0x20 || byte == 0x7F || c == '/' || c == '\\' || c == ':' || c == '*'
                          || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
        out += refused ? '-' : c;
    }
    const auto first = out.find_first_not_of(". ");
    out = (first == std::string::npos) ? std::string() : out.substr(first);
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
    }
    return out.empty() ? std::string("Untitled") : out;
}

} // namespace bdd::vst3

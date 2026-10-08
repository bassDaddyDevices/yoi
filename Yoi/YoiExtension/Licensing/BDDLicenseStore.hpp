//
//  BDDLicenseStore.hpp
//  Bass Daddy Devices
//
//  Where licenses are kept: one `.bddlicense` file each (the license's single line) in
//  `Application Support/Bass Daddy Devices/Licenses`. The VST3 passes the real folder; the
//  sandboxed AU passes the same path inside its container. Shared by every synth and both formats,
//  so they keep licenses by the same rules. Main thread only.
//

#pragma once

#include "BDDLicense.hpp"
#include "BDDLicenseKey.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace bdd::license {

/// The license this copy runs under: the first valid one in `folder` for `product`, or, if none is
/// valid, the most useful reason (an other-product or older-version license says more than none).
inline License loadFromFolder(const std::string& folder, const std::string& product, int productMajor,
                              const std::array<uint8_t, 32>& publicKey) {
    License best;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(folder), error)) {
        if (entry.path().extension() != ".bddlicense" || entry.file_size(error) > kMaximumLength * 2) {
            continue;
        }
        std::ifstream file(entry.path(), std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        const License license = check(text.str(), publicKey, product, productMajor);
        if (license.isValid()) {
            return license;
        }
        if (license.status == Status::otherProduct || license.status == Status::needsUpgrade) {
            best = license;
        }
    }
    if (best.status == Status::empty && publicKey == std::array<uint8_t, 32>{}) {
        best.status = Status::notConfigured;
    }
    return best;
}

/// Checks `token` and, if it's valid for `product`, saves it in `folder`. Returns the check either
/// way, so the editor can say what was wrong. Invalid licenses are never saved.
inline License installInFolder(const std::string& folder, const std::string& token, const std::string& product,
                               int productMajor, const std::array<uint8_t, 32>& publicKey) {
    License license = check(token, publicKey, product, productMajor);
    if (!license.isValid()) {
        return license;
    }
    // Named after the license's id, so installing the same license twice overwrites it.
    std::string name;
    for (char c : license.id.empty() ? std::string("license") : license.id) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        name += plain ? c : '_';
    }
    std::error_code error;
    const std::filesystem::path directory(folder);
    std::filesystem::create_directories(directory, error);
    const auto file = directory / (name + ".bddlicense");
    const auto temporary = directory / (name + ".bddlicense.tmp");
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << detail::trimmed(token) << '\n';
        if (!stream) {
            license.status = Status::couldNotSave;
            return license;
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        license.status = Status::couldNotSave;
    }
    return license;
}

// With Bass Daddy Devices' own key, as the plug-ins call them. Overloads rather than default
// arguments, which Swift's C++ interop doesn't take.
inline License loadFromFolder(const std::string& folder, const std::string& product, int productMajor) {
    return loadFromFolder(folder, product, productMajor, kPublicKey);
}

inline License installInFolder(const std::string& folder, const std::string& token, const std::string& product,
                               int productMajor) {
    return installInFolder(folder, token, product, productMajor, kPublicKey);
}

/// Whether a copy with this license should play without the demo silence.
inline bool unlocks(const License& license) {
    return !kLicensingEnforced || license.isValid();
}

} // namespace bdd::license

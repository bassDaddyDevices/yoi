//
//  BDDLicense.hpp
//  Bass Daddy Devices
//
//  Checks a Bass Daddy Devices license offline. Shared by every synth in the family and by both
//  plug-in formats; nothing here knows about YOI. See YOI_DOCS/decisions/licensing.md.
//
//  A license is one line, issued by auth.bass-daddy.com after a Gumroad purchase:
//
//      BDD1.<payload, base64url>.<signature, base64url>
//
//  The signature is Ed25519 (RFC 8032, via Monocypher) over the payload's exact bytes. The payload
//  is "key=value" lines; unknown keys are ignored:
//
//      format=bdd-license-1
//      products=YOI            (comma-separated: one license can cover several synths)
//      licensee=Chris Connelly
//      email=me@example.com
//      id=gumroad:<sale id>
//      issued=2026-10-08
//      major=1                 (the highest major version it unlocks)
//
//  Main thread only (load, install); the audio thread only ever sees a "licensed" switch.
//

#pragma once

#include "ThirdParty/monocypher/monocypher-ed25519.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace bdd::license {

enum class Status {
    valid,
    empty,           ///< nothing entered or stored
    malformed,       ///< not a BDD1 license at all
    badSignature,    ///< not signed by Bass Daddy Devices (or altered)
    otherProduct,    ///< a real license, for other synths
    needsUpgrade,    ///< a real license for an older major version
    notConfigured,   ///< this build has no public key yet
    couldNotSave,    ///< valid, but writing it to disk failed
};

struct License {
    Status status = Status::empty;
    std::string licensee;
    std::string email;
    std::string id;
    std::string issued;
    std::string products;
    int major = 0;

    bool isValid() const { return status == Status::valid; }
};

/// What to tell the user, short enough for the panel.
inline const char* describe(Status status) {
    switch (status) {
        case Status::valid: return "Licensed.";
        case Status::empty: return "Not licensed yet.";
        case Status::malformed: return "That isn't a Bass Daddy Devices license or key.";
        case Status::badSignature: return "That license isn't valid. Paste it again exactly as it was sent.";
        case Status::otherProduct: return "That license is for another Bass Daddy Devices synth.";
        case Status::needsUpgrade: return "That license is for an earlier version.";
        case Status::notConfigured: return "Licensing isn't set up in this build.";
        case Status::couldNotSave: return "That license is valid, but it couldn't be saved.";
    }
    return "";
}

inline constexpr std::string_view kPrefix = "BDD1.";
inline constexpr size_t kMaximumLength = 4096;

namespace detail {

inline int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-' || c == '+') return 62;
    if (c == '_' || c == '/') return 63;
    return -1;
}

/// base64url (or plain base64), padding optional. False on any other character.
inline bool decodeBase64(std::string_view text, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') {
            break;
        }
        const int value = base64Value(c);
        if (value < 0) {
            return false;
        }
        buffer = (buffer << 6) | uint32_t(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((buffer >> bits) & 0xFF));
        }
    }
    return true;
}

inline std::string_view trimmed(std::string_view text) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    return text;
}

inline bool listContains(std::string_view list, std::string_view item) {
    while (!list.empty()) {
        const size_t comma = list.find(',');
        if (trimmed(list.substr(0, comma)) == item) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        list.remove_prefix(comma + 1);
    }
    return false;
}

} // namespace detail

/// True when `text` looks like a signed license rather than a store's license key, so an editor
/// can install it directly instead of activating it online.
inline bool looksLikeLicense(std::string_view text) {
    return detail::trimmed(text).substr(0, kPrefix.size()) == kPrefix;
}

/// Checks `token` against Bass Daddy Devices' public key, for `product` at `productMajor`.
inline License check(std::string_view token, const std::array<uint8_t, 32>& publicKey,
                     std::string_view product, int productMajor) {
    License license;
    token = detail::trimmed(token);
    if (token.empty()) {
        return license;
    }

    bool keyConfigured = false;
    for (uint8_t byte : publicKey) {
        keyConfigured = keyConfigured || byte != 0;
    }
    if (!keyConfigured) {
        license.status = Status::notConfigured;
        return license;
    }

    license.status = Status::malformed;
    if (token.size() > kMaximumLength || !looksLikeLicense(token)) {
        return license;
    }
    token.remove_prefix(kPrefix.size());
    const size_t dot = token.find('.');
    if (dot == std::string_view::npos || token.find('.', dot + 1) != std::string_view::npos) {
        return license;
    }
    std::vector<uint8_t> payload;
    std::vector<uint8_t> signature;
    if (!detail::decodeBase64(token.substr(0, dot), payload) || !detail::decodeBase64(token.substr(dot + 1), signature)
        || payload.empty() || signature.size() != 64) {
        return license;
    }

    if (crypto_ed25519_check(signature.data(), publicKey.data(), payload.data(), payload.size()) != 0) {
        license.status = Status::badSignature;
        return license;
    }

    // Signed by us, so the payload is ours: read its lines.
    std::string format;
    std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
    while (!text.empty()) {
        const size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        const size_t equals = line.find('=');
        if (equals != std::string_view::npos) {
            const std::string_view key = detail::trimmed(line.substr(0, equals));
            const std::string value(detail::trimmed(line.substr(equals + 1)));
            if (key == "format") format = value;
            else if (key == "products") license.products = value;
            else if (key == "licensee") license.licensee = value;
            else if (key == "email") license.email = value;
            else if (key == "id") license.id = value;
            else if (key == "issued") license.issued = value;
            else if (key == "major") license.major = std::atoi(value.c_str());
        }
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }

    if (format != "bdd-license-1") {
        license.status = Status::malformed;
    } else if (!detail::listContains(license.products, product)) {
        license.status = Status::otherProduct;
    } else if (license.major < productMajor) {
        license.status = Status::needsUpgrade;
    } else {
        license.status = Status::valid;
    }
    return license;
}

} // namespace bdd::license

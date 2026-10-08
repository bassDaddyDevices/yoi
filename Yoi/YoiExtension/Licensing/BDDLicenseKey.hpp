//
//  BDDLicenseKey.hpp
//  Bass Daddy Devices
//
//  The public half of Bass Daddy Devices' license signing key, shared by every synth, and whether
//  this build enforces licensing. The private half lives only in auth.bass-daddy.com's secrets
//  (and an offline backup); it never goes in this repo.
//
//  Enforced since 2026-10-08: an unactivated copy plays with the demo silence
//  (YOI_DOCS/decisions/licensing.md). Developers activate their own copy like any buyer.
//

#pragma once

#include <array>
#include <cstdint>

namespace bdd::license {

/// Made on the auth.bass-daddy.com server, 2026-10-08 (scripts/make-keys.mjs --env-file); its
/// private half exists only there and in the owner's backup. Never change it: every license already
/// issued is signed by its private half.
inline constexpr std::array<uint8_t, 32> kPublicKey {
    0x68, 0xe7, 0xe1, 0x87, 0x3e, 0x49, 0xdb, 0x33, 0xc7, 0x2b, 0xd1, 0xc8, 0x76, 0xa1, 0x94, 0x92,
    0x7b, 0x0d, 0x54, 0x69, 0xc8, 0x4c, 0x23, 0x1d, 0x21, 0xd3, 0x10, 0xa3, 0x47, 0x6e, 0x12, 0x37,
};

inline constexpr bool kLicensingEnforced = true;

} // namespace bdd::license

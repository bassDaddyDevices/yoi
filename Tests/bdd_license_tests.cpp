//
//  bdd_license_tests.cpp
//  YOI tests
//
//  The shared license checker (Licensing/BDDLicense.hpp): licenses signed with a test key pair are
//  accepted, and every way a license can be wrong is refused for the right reason. The last test
//  reads a license signed by the server's own code (Server/license-worker), so the two sides are
//  proven to agree.
//

#include "BDDLicense.hpp"
#include "BDDLicenseStore.hpp"

#include <filesystem>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition, ...)                                                 \
    do {                                                                      \
        ++checks;                                                             \
        if (!(condition)) {                                                   \
            ++failures;                                                       \
            std::printf("FAIL %s:%d: %s - ", __FILE__, __LINE__, #condition); \
            std::printf(__VA_ARGS__);                                         \
            std::printf("\n");                                                \
        }                                                                     \
    } while (0)

using bdd::license::Status;

std::string base64url(const uint8_t* data, size_t size) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    uint32_t buffer = 0;
    int bits = 0;
    for (size_t i = 0; i < size; ++i) {
        buffer = (buffer << 8) | data[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += alphabet[(buffer >> bits) & 0x3F];
        }
    }
    if (bits > 0) {
        out += alphabet[(buffer << (6 - bits)) & 0x3F];
    }
    return out;
}

struct TestKeys {
    uint8_t secret[64];
    std::array<uint8_t, 32> publicKey;

    TestKeys() {
        uint8_t seed[32];
        for (int i = 0; i < 32; ++i) {
            seed[i] = uint8_t(i * 7 + 1);
        }
        crypto_ed25519_key_pair(secret, publicKey.data(), seed);   // wipes the seed
    }

    std::string sign(const std::string& payload) const {
        uint8_t signature[64];
        crypto_ed25519_sign(signature, secret, reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
        return "BDD1." + base64url(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()) + "."
             + base64url(signature, 64);
    }
};

const std::string kPayload =
    "format=bdd-license-1\nproducts=YOI\nlicensee=Test Buyer\nemail=test@example.com\nid=gumroad:abc123\n"
    "issued=2026-10-08\nmajor=1\n";

void testValidLicense() {
    const TestKeys keys;
    const auto license = bdd::license::check(keys.sign(kPayload), keys.publicKey, "YOI", 1);
    CHECK(license.isValid(), "a properly signed license should be valid: %s", bdd::license::describe(license.status));
    CHECK(license.licensee == "Test Buyer" && license.email == "test@example.com" && license.id == "gumroad:abc123"
              && license.issued == "2026-10-08" && license.major == 1,
          "its fields should read back");

    const auto padded = bdd::license::check("  \n" + keys.sign(kPayload) + "\r\n ", keys.publicKey, "YOI", 1);
    CHECK(padded.isValid(), "whitespace around a pasted license should be ignored");
    CHECK(bdd::license::looksLikeLicense(" BDD1.abc.def") && !bdd::license::looksLikeLicense("A1B2C3D4-E5F6"),
          "a license should be told apart from a store key");
}

void testBundlesAndVersions() {
    const TestKeys keys;
    std::string bundle = kPayload;
    bundle.replace(bundle.find("products=YOI"), 12, "products=VOWL, YOI");
    CHECK(bdd::license::check(keys.sign(bundle), keys.publicKey, "YOI", 1).isValid(), "a bundle license should cover YOI");
    CHECK(bdd::license::check(keys.sign(bundle), keys.publicKey, "VOWL", 1).isValid(), "and the other synth");
    CHECK(bdd::license::check(keys.sign(kPayload), keys.publicKey, "VOWL", 1).status == Status::otherProduct,
          "a YOI license shouldn't unlock another synth");
    CHECK(bdd::license::check(keys.sign(kPayload), keys.publicKey, "YO", 1).status == Status::otherProduct,
          "products should match whole names, not prefixes");
    CHECK(bdd::license::check(keys.sign(kPayload), keys.publicKey, "YOI", 2).status == Status::needsUpgrade,
          "a version 1 license shouldn't unlock version 2");

    std::string extra = kPayload + "futureField=whatever\n";
    CHECK(bdd::license::check(keys.sign(extra), keys.publicKey, "YOI", 1).isValid(), "unknown fields should be ignored");
    std::string newer = kPayload;
    newer.replace(newer.find("bdd-license-1"), 13, "bdd-license-2");
    CHECK(bdd::license::check(keys.sign(newer), keys.publicKey, "YOI", 1).status == Status::malformed,
          "another payload format should be refused");
}

void testRefusals() {
    const TestKeys keys;
    const std::string token = keys.sign(kPayload);

    // Change one character of the payload: the signature no longer matches.
    std::string altered = token;
    altered[10] = (altered[10] == 'A') ? 'B' : 'A';
    CHECK(bdd::license::check(altered, keys.publicKey, "YOI", 1).status == Status::badSignature,
          "an altered license should fail its signature");

    TestKeys other;
    other.publicKey[0] ^= 0x01;
    CHECK(bdd::license::check(token, other.publicKey, "YOI", 1).status == Status::badSignature,
          "a license checked against another key should fail");

    CHECK(bdd::license::check(token, std::array<uint8_t, 32>{}, "YOI", 1).status == Status::notConfigured,
          "with no public key in the build, nothing is valid");
    CHECK(bdd::license::check("", keys.publicKey, "YOI", 1).status == Status::empty, "nothing is 'empty'");
    for (const char* junk : { "hello", "BDD1.", "BDD1.abc", "BDD1.a.b.c", "BDD1.!!!.???", "BDD2.abc.def" }) {
        CHECK(bdd::license::check(junk, keys.publicKey, "YOI", 1).status == Status::malformed, "'%s' should be malformed", junk);
    }
    CHECK(bdd::license::check(std::string(5000, 'A'), keys.publicKey, "YOI", 1).status == Status::malformed,
          "an oversized paste should be refused");
}

void testServerVector() {
    // Written by Server/license-worker/scripts/make-test-vector.mjs with its own test key: the
    // public key in hex, then a license it signed for YOI.
    std::ifstream file(YOI_LICENSE_VECTOR);
    std::string publicHex;
    std::string token;
    std::getline(file, publicHex);
    std::getline(file, token);
    CHECK(publicHex.size() == 64 && !token.empty(), "couldn't read the server's test vector");
    if (publicHex.size() != 64) {
        return;
    }
    std::array<uint8_t, 32> key{};
    for (size_t i = 0; i < 32; ++i) {
        key[i] = uint8_t(std::stoi(publicHex.substr(i * 2, 2), nullptr, 16));
    }
    const auto license = bdd::license::check(token, key, "YOI", 1);
    CHECK(license.isValid(), "a license signed by the server's code should be valid here: %s",
          bdd::license::describe(license.status));
    CHECK(license.licensee == "Vector Test", "and read back its licensee, got '%s'", license.licensee.c_str());
}


void testStore() {
    const TestKeys keys;
    const auto folder = std::filesystem::temp_directory_path() / "bdd-license-tests";
    std::filesystem::remove_all(folder);
    const std::string path = folder.string();

    CHECK(bdd::license::loadFromFolder(path, "YOI", 1, keys.publicKey).status == Status::empty,
          "no folder yet should mean not licensed");

    CHECK(bdd::license::installInFolder(path, "nonsense", "YOI", 1, keys.publicKey).status == Status::malformed,
          "junk should be refused");
    CHECK(!std::filesystem::exists(folder) || std::filesystem::is_empty(folder), "and nothing saved");

    std::string other = kPayload;
    other.replace(other.find("products=YOI"), 12, "products=VOWL");
    CHECK(bdd::license::installInFolder(path, keys.sign(other), "YOI", 1, keys.publicKey).status == Status::otherProduct,
          "another synth's license shouldn't install into YOI");

    const auto installed = bdd::license::installInFolder(path, keys.sign(kPayload), "YOI", 1, keys.publicKey);
    CHECK(installed.isValid(), "a valid license should install: %s", bdd::license::describe(installed.status));
    CHECK(bdd::license::installInFolder(path, keys.sign(kPayload), "YOI", 1, keys.publicKey).isValid(), "and again");
    size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        files += entry.path().extension() == ".bddlicense" ? 1 : 0;
    }
    CHECK(files == 1, "installing the same license twice should keep one file, found %zu", files);

    const auto loaded = bdd::license::loadFromFolder(path, "YOI", 1, keys.publicKey);
    CHECK(loaded.isValid() && loaded.licensee == "Test Buyer", "the saved license should load back");
    CHECK(bdd::license::loadFromFolder(path, "YOI", 2, keys.publicKey).status == Status::needsUpgrade,
          "for version 2, the stored version 1 license should say it needs an upgrade");
    std::filesystem::remove_all(folder);
}

} // namespace

int main() {
    testValidLicense();
    testBundlesAndVersions();
    testRefusals();
    testServerVector();
    testStore();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Network play's encryption, against the published test vectors: X25519
// (RFC 7748, section 6.1) and ChaCha20, Poly1305 and their sealed box
// (RFC 8439, sections 2.3.2, 2.5.2 and 2.8.2).
//
//   reluva-tests common.unit.crypto

#include "common/crypto.h"
#include "tests/support/test.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;

namespace {

std::vector<uint8_t> fromHex(const std::string &hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(uint8_t(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

template<size_t N>
std::array<uint8_t, N> arrayOf(const std::string &hex) {
    std::array<uint8_t, N> out{};
    const std::vector<uint8_t> bytes = fromHex(hex);
    for (size_t i = 0; i < N && i < bytes.size(); ++i)
        out[i] = bytes[i];
    return out;
}

template<size_t N>
std::string hexOf(const std::array<uint8_t, N> &bytes) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const uint8_t b : bytes) {
        out += kDigits[b >> 4];
        out += kDigits[b & 15];
    }
    return out;
}

void testX25519() {
    using namespace reluva::crypto;
    const Key alice = arrayOf<32>("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const Key bob = arrayOf<32>("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    const Key alicePublic = x25519Public(alice);
    const Key bobPublic = x25519Public(bob);
    CHECK(hexOf(alicePublic) == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    CHECK(hexOf(bobPublic) == "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    const std::string shared = "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742";
    CHECK(hexOf(x25519(alice, bobPublic)) == shared && hexOf(x25519(bob, alicePublic)) == shared);
    // Two keys made here agree too.
    const Key a = randomKey();
    const Key b = randomKey();
    CHECK(a != b && x25519(a, x25519Public(b)) == x25519(b, x25519Public(a)));
}

void testChaCha20Poly1305() {
    using namespace reluva::crypto;
    Key key;
    for (size_t i = 0; i < key.size(); ++i)
        key[i] = uint8_t(i);
    const auto block = chacha20Block(key, arrayOf<12>("000000090000004a00000000"), 1);
    CHECK(hexOf(block).substr(0, 64) == "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e");

    const Key polyKey = arrayOf<32>("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b");
    const std::string text = "Cryptographic Forum Research Group";
    CHECK(hexOf(poly1305(polyKey, reinterpret_cast<const uint8_t *>(text.data()), text.size())) ==
          "a8061dc1305136c6c22b8baf0c0127a9");

    Key aeadKey;
    for (size_t i = 0; i < aeadKey.size(); ++i)
        aeadKey[i] = uint8_t(0x80 + i);
    const Nonce nonce = arrayOf<12>("070000004041424344454647");
    const std::vector<uint8_t> aad = fromHex("50515253c0c1c2c3c4c5c6c7");
    const std::string plain = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the "
                              "future, sunscreen would be it.";
    const std::vector<uint8_t> sealed =
        seal(aeadKey, nonce, aad.data(), aad.size(), reinterpret_cast<const uint8_t *>(plain.data()), plain.size());
    CHECK(sealed.size() == plain.size() + 16);
    std::array<uint8_t, 16> head{};
    std::array<uint8_t, 16> tag{};
    std::copy(sealed.begin(), sealed.begin() + 16, head.begin());
    std::copy(sealed.end() - 16, sealed.end(), tag.begin());
    CHECK(hexOf(head) == "d31a8d34648e60db7b86afbc53ef7ec2" && hexOf(tag) == "1ae10b594f09e26a7e902ecbd0600691");
    std::vector<uint8_t> back;
    CHECK(open(aeadKey, nonce, aad.data(), aad.size(), sealed.data(), sealed.size(), back) &&
          std::string(back.begin(), back.end()) == plain);
    // Changed anywhere, it isn't opened.
    std::vector<uint8_t> changed = sealed;
    changed[40] ^= 1;
    CHECK(!open(aeadKey, nonce, aad.data(), aad.size(), changed.data(), changed.size(), back));
    std::vector<uint8_t> otherAad = aad;
    otherAad[0] ^= 1;
    CHECK(!open(aeadKey, nonce, otherAad.data(), otherAad.size(), sealed.data(), sealed.size(), back));
}

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.crypto",
           {
               {"x25519", [](const std::filesystem::path &) { testX25519(); }},
               {"chacha20-poly1305", [](const std::filesystem::path &) { testChaCha20Poly1305(); }},
           });

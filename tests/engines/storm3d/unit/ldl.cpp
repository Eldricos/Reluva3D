// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: the .ldl files' encoding. The real key is the
// game's, so these use one made here.

#include "engines/storm3d/formats/ldl.h"

#include "tests/support/test.h"

#include <fstream>
#include <string>

using namespace reluva;

namespace {

// Each byte is XORed with the key's byte at its offset modulo the key's
// length, past the key's end too, and decoding twice gives the text back.
void testDecode() {
    std::vector<uint8_t> key(storm3d::kLdlKeySize);
    for (size_t i = 0; i < key.size(); ++i)
        key[i] = uint8_t(i * 7 + 3);
    const std::string text = "# Star Trek: Armada - STRING.LDL\r\n" + std::string(900, 'x');
    std::vector<uint8_t> data(text.begin(), text.end());
    storm3d::decodeLdl(data, key);
    CHECK(data[0] == uint8_t('#' ^ 3));
    CHECK(data[400] == uint8_t(text[400] ^ 3));
    storm3d::decodeLdl(data, key);
    CHECK(std::string(data.begin(), data.end()) == text);
}

// A file that isn't the game's executable gives no key.
void testNoKey(const std::filesystem::path &tmp) {
    std::ofstream(tmp / "Armada.exe", std::ios::binary) << "MZ not a program";
    std::vector<uint8_t> key;
    CHECK(storm3d::readLdlKey(tmp / "Armada.exe", key));
    CHECK(key.empty());
    CHECK(storm3d::readLdlKey(tmp / "missing.exe", key));
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.ldl",
                                        {
                                            {"decode", [](const std::filesystem::path &) { testDecode(); }},
                                            {"no key", [](const std::filesystem::path &tmp) { testNoKey(tmp); }},
                                        });

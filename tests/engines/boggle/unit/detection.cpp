// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Boggle (1997)'s unit tests: detection. Add a file (and its topic, in the
// engine's CMakeLists.txt) for each part of the engine as it grows.

#include "engines/registry.h"

#include "tests/support/test.h"
#include "tests/engines/boggle/fixtures.h"

using namespace reluva;

namespace {

void testRegistered() {
    const MetaEngine* engine = findEngine("boggle");
    CHECK(engine != nullptr);
}

// A folder with none of a release's files isn't recognised.
void testEmptyFolder(const std::filesystem::path& tmp) {
    const MetaEngine* engine = findEngine("boggle");
    CHECK(engine && engine->detect(tmp).empty());
}

// Correct names and sizes alone must not recognize an install. These are
// zero-filled synthetic files, not bytes from the fingerprinted release.
void testFalsePositive(const std::filesystem::path& tmp) {
    using reluva::boggle::fixtures::write;
    write(tmp / "Boggle.exe", std::vector<uint8_t>(656384));
    write(tmp / "Language/English.dll", std::vector<uint8_t>(124416));
    write(tmp / "Dicts/English.dic", std::vector<uint8_t>(718084));
    write(tmp / "Dicts/EngOrg.dst", std::vector<uint8_t>(209));
    write(tmp / "Palettes/Title.pal", std::vector<uint8_t>(2716));
    write(tmp / "Scripts/Die.x", std::vector<uint8_t>(4496));
    write(tmp / "Anims/INTRO.AVI", std::vector<uint8_t>(4311730));
    write(tmp / "Fonts/TARA.TTF", std::vector<uint8_t>(54348));
    write(tmp / "Waves/Shake.wav", std::string("synthetic"));
    write(tmp / "Bitmaps/ClsM1CS.bmp", std::vector<uint8_t>(61078));
    write(tmp / "Textures/Letters.bmp", std::string("synthetic"));
    const auto* engine = findEngine("boggle");
    CHECK(engine && engine->detect(tmp).empty());
}

} // namespace

static const reluva::test::Suite suite("boggle.unit.detection", {
    {"names and sizes without matching hashes", testFalsePositive},
    {"registered", [](const std::filesystem::path&) { testRegistered(); }},
    {"empty folder", [](const std::filesystem::path& tmp) { testEmptyFolder(tmp); }},
});

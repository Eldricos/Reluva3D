// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Clue engine's unit tests: finding the game. Clue.exe tells the version
// (here a stand-in, not a known release's), and the language's files the
// language: the UK's alone have Sounds/end turn.wav. An install has one
// language in Data/Classic, the CD both in Data/US and Data/UK.

#include "engines/registry.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;

namespace {

std::string found(const std::filesystem::path& dir) {
    const MetaEngine* clue = findEngine("clue");
    if (!clue)
        return "(no engine)";
    std::string out;
    for (const DetectedGame& g : clue->detect(dir))
        out += (out.empty() ? "" : " | ") + g.gameId + " " + g.language + " " + g.description;
    return out;
}

void testDetection(const std::filesystem::path& tmp) {
    const std::vector<uint8_t> exe = {'M', 'Z'}, wav = {'R', 'I', 'F', 'F'};
    const auto us = tmp / "us", uk = tmp / "uk", cd = tmp / "cd", none = tmp / "none";
    test::writeFile(us / "Clue.exe", exe);
    test::writeFile(us / "Data" / "Classic" / "Sounds" / "turn_MS.wav", wav);
    test::writeFile(uk / "CLUE.EXE", exe);
    test::writeFile(uk / "Data" / "Classic" / "Sounds" / "End Turn.WAV", wav); // in any case
    test::writeFile(cd / "Clue.exe", exe);
    test::writeFile(cd / "Data" / "US" / "Sounds" / "turn_MS.wav", wav);
    test::writeFile(cd / "Data" / "UK" / "Sounds" / "end turn.wav", wav);
    test::writeFile(none / "Data" / "Classic" / "Sounds" / "end turn.wav", wav); // no Clue.exe
    CHECK(found(us) == "clue en Clue: Murder at Boddy Mansion (unknown release)");
    CHECK(found(uk) == "clue-uk en-gb Cluedo: Murder at Blackwell Grange (unknown release)");
    CHECK(found(cd) == "clue en Clue: Murder at Boddy Mansion (unknown release) | "
                       "clue-uk en-gb Cluedo: Murder at Blackwell Grange (unknown release)");
    CHECK(found(none).empty());
}

} // namespace

static const reluva::test::Suite suite("clue.unit.detection", {
    {"detection", [](const std::filesystem::path& tmp) { testDetection(tmp); }},
});

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Recognising the game and copying it from its CDs: a disc, both discs side
// by side, an install, names in any case (as Linux CD mounts show them),
// and an import that takes the other disc as it's planned.
//
//   reluva-tests stharbinger.unit.detection

#include "engines/registry.h"
#include "engines/stharbinger/detection.h"
#include "tests/engines/stharbinger/fixtures.h"
#include "tests/support/test.h"

#include <algorithm>
#include <filesystem>
#include <string>

using namespace reluva;
using namespace reluva::stharbinger;
using namespace reluva::stharbinger::testing;

namespace {

// A disc as on the real CDs, in miniature: disc 1 has the scripts (in
// SSE/SSE.ALL) and both have data under DS9 and their number in
// DS9/TORPEDOS/CD.
void makeDisc(const std::filesystem::path& root, int disc, bool lowerCase) {
    auto name = [&](std::string s) {
        if (lowerCase)
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    };
    putFile(root / name("DS9") / name("TORPEDOS") / name("CD"), bytes(std::to_string(disc) + "\r\n"));
    putFile(root / name("DS9") / name("PALETTES") / name("X.PAL"), std::vector<uint8_t>(768, 0));
    if (disc == 0) {
        putFile(root / name("SSE") / name("SSE.ALL"), {'a', 'b', 0, 0, 0, 0, 2, 0, 0, 0});
        putFile(root / name("SSE") / name("SSE.LST"), {0, 0, 0, 0, 2, 0, 0, 0, 10, 0, 0, 0});
        putFile(root / name("SSE") / name("SSE.CAT"), bytes("2\r\nCC.ALL\r\nCC.LST\r\n"));
    }
}

const MetaEngine& engine() { return *findEngine("stharbinger"); }

void testParts(const std::filesystem::path& tmp) {
    makeDisc(tmp / "cd1", 0, false);
    makeDisc(tmp / "cd2", 1, true);
    std::filesystem::create_directories(tmp / "empty");

    auto one = findGameParts(tmp / "cd1");
    CHECK(one.size() == 1 && one[0].scripts && one[0].data && one[0].disc == 0);
    auto two = findGameParts(tmp / "cd2");
    CHECK(two.size() == 1 && !two[0].scripts && two[0].data && two[0].disc == 1);
    CHECK(playable(one) && !playable(two));
    // A folder holding both: each disc, in name order.
    auto both = findGameParts(tmp);
    CHECK(both.size() == 2 && both[0].disc == 0 && both[1].disc == 1 && playable(both));
    CHECK(findGameParts(tmp / "empty").empty() && findGameParts(tmp / "missing").empty());
}

void testDetect(const std::filesystem::path& tmp) {
    makeDisc(tmp / "both" / "disc1", 0, true);
    makeDisc(tmp / "both" / "disc2", 1, false);
    makeDisc(tmp / "one", 0, false);
    makeDisc(tmp / "other", 1, false);

    auto found = engine().detect(tmp / "both");
    CHECK(found.size() == 1 && found[0].engineId == "stharbinger" && found[0].gameId == "ds9harbinger");
    CHECK(found[0].description == "Star Trek: Deep Space Nine - Harbinger" && found[0].platform == "dos");
    auto disc1 = engine().detect(tmp / "one");
    CHECK(disc1.size() == 1 && disc1[0].description == "Star Trek: Deep Space Nine - Harbinger (disc 1)");
    // Disc 2 alone has no scripts: nothing to play.
    CHECK(engine().detect(tmp / "other").empty());
}

void testImport(const std::filesystem::path& tmp) {
    makeDisc(tmp / "cd1", 0, false);
    makeDisc(tmp / "cd2", 1, true);

    auto plans = engine().planImport(tmp / "cd1");
    CHECK(plans.size() == 1);
    if (plans.empty())
        return;
    ImportPlan& plan = plans[0];
    CHECK(plan.addPatchLabel.empty() && plan.addFolderLabel == "Add other disc...");
    CHECK(
        std::all_of(plan.files.begin(), plan.files.end(), [](const auto& f) { return f.to.rfind("disc1/", 0) == 0; }));
    CHECK(std::any_of(plan.notes.begin(), plan.notes.end(),
                      [](const std::string& n) { return n.find("Add disc 2") != std::string::npos; }));
    const size_t before = plan.files.size();

    // The other disc, as it's planned; the same one again is refused.
    std::string message;
    CHECK(engine().addToImport(plan, tmp / "cd2", message));
    CHECK(plan.files.size() > before && std::any_of(plan.files.begin(), plan.files.end(),
                                                    [](const auto& f) { return f.to == "disc2/ds9/torpedos/cd"; }));
    CHECK(std::none_of(plan.notes.begin(), plan.notes.end(),
                       [](const std::string& n) { return n.find("Add disc") != std::string::npos; }));
    CHECK(!engine().addToImport(plan, tmp / "cd2", message) && message.find("already") != std::string::npos);
    CHECK(!engine().addToImport(plan, tmp / "nothing", message));

    // Copied, it's one game, found again from its library folder.
    CHECK(!runImport(plan, tmp / "library", [](uint64_t, uint64_t) { return true; }));
    auto parts = findGameParts(tmp / "library");
    CHECK(parts.size() == 2 && parts[0].disc == 0 && parts[1].disc == 1);
    CHECK(engine().detect(tmp / "library").size() == 1);
}

void testCdVersions(const std::filesystem::path& tmp) {
    // Some CD mounts show ISO 9660 versions: "SSE.ALL;1".
    putFile(tmp / "cd" / "DS9" / "TORPEDOS" / "CD;1", bytes("0"));
    putFile(tmp / "cd" / "DS9" / "PALETTES" / "X.PAL;1", std::vector<uint8_t>(768, 0));
    putFile(tmp / "cd" / "SSE" / "SSE.ALL;1", {'a', 'b', 0, 0, 0, 0, 2, 0, 0, 0});
    putFile(tmp / "cd" / "SSE" / "SSE.LST;1", {0, 0, 0, 0, 2, 0, 0, 0, 10, 0, 0, 0});
    putFile(tmp / "cd" / "SSE" / "SSE.CAT;1", bytes("2\r\nCC.ALL\r\nCC.LST\r\n"));
    auto parts = findGameParts(tmp / "cd");
    CHECK(parts.size() == 1 && parts[0].scripts && parts[0].disc == 0);
    Resources res;
    res.addGamePath((tmp / "cd").string());
    CHECK(res.hasFile("PALETTES/X.PAL") && res.hasFile("CC.ALL"));
    // Copied without the versions.
    auto plans = engine().planImport(tmp / "cd");
    CHECK(plans.size() == 1 && std::any_of(plans[0].files.begin(), plans[0].files.end(),
                                           [](const auto& f) { return f.to == "disc1/SSE/SSE.ALL"; }));
}

} // namespace

static const reluva::test::Suite
    suite("stharbinger.unit.detection",
          {
              {"game parts", [](const std::filesystem::path& tmp) { testParts(tmp); }},
              {"detect", [](const std::filesystem::path& tmp) { testDetect(tmp); }},
              {"import both discs", [](const std::filesystem::path& tmp) { testImport(tmp); }},
              {"cd versions", [](const std::filesystem::path& tmp) { testCdVersions(tmp); }},
          });

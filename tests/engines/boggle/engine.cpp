// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Boggle (1997) as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/boggle/data/engine-record.txt.
//
//   reluva-tests boggle.engine [--update]
//
// The synthetic resource corpus is checked and fingerprinted. It must not
// match the retail detection table; the actual Classic engine is then driven through a scripted System.

#include "engines/registry.h"
#include "engines/boggle/resources.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"

#include "tests/support/record.h"
#include "tests/support/test.h"

using namespace reluva;

namespace {

void engineTest(const std::filesystem::path& tmp) {
    const std::filesystem::path dir = tmp / "install";
    std::filesystem::create_directories(dir);
    boggle::fixtures::install(dir);
    test::Record rec;
    const MetaEngine* engine = findEngine("boggle");
    if (!engine)
        return test::fail("boggle isn't registered");
    rec.add("detect.games", static_cast<int64_t>(engine->detect(dir).size()));
    boggle::Resources resources;
    if (auto err = resources.open(dir)) return test::fail(err.message);
    boggle::ResourceSummary summary;
    if (auto err = resources.check(summary)) return test::fail(err.message);
    rec.add("resources.images", int64_t(summary.images));
    rec.add("resources.sounds", int64_t(summary.sounds));
    rec.add("resources.palettes", int64_t(summary.palettes));
    rec.add("resources.dice", int64_t(summary.dice));
    rec.add("resources.vertices", int64_t(summary.vertices));
    rec.add("resources.strings", int64_t(summary.strings));
    rec.add("resources.decoded", summary.digest);
    // Drive the actual registered Engine through resource loading, keyboard
    // submission, duplicate rejection, expiration, replay and manual finish.
    boggle::fixtures::classicInstall(dir);
    boggle::fixtures::ScriptedSystem system;
    system.events = [](size_t frame) -> std::vector<Event> {
        using namespace boggle::fixtures;
        if (frame == 1 || frame == 3 || frame == 5 || frame == 6 || frame == 7) return {keyEvent(key::Return)};
        if (frame == 2 || frame == 4) return {textEvent("aaa")};
        if (frame == 8) return {keyEvent(key::F1)};
        if (frame == 9) return {keyEvent(key::Escape)};
        return {};
    };
    system.clock = [](size_t frame) -> uint64_t { return frame >= 6 ? 180010 : frame * 10; };
    GameTarget target; target.path = dir; target.gameId = "boggle1997";
    auto game = engine->create(system,target);
    if (auto err = game->run()) return test::fail(err.message);
    CHECK(system.frame == 9 && system.w == 800 && system.h == 600);
    CHECK(!system.textEnabled && system.textStopped);
    if (system.hashes.size() == 9) {
        CHECK(system.hashes[1] == system.hashes[7]); // Replay clears score/words.
        rec.add("classic.ready",system.hashes[0]);
        rec.add("classic.accepted",system.hashes[3]);
        rec.add("classic.duplicate",system.hashes[5]);
        rec.add("classic.expired",system.hashes[6]);
        rec.add("classic.replay",system.hashes[7]);
    }
    boggle::fixtures::ScriptedSystem mouse;
    mouse.events = [](size_t frame) -> std::vector<Event> {
        using namespace boggle::fixtures;
        if (frame == 1) return {keyEvent(key::Return)};
        if (frame == 2) return {mouseEvent(EventType::MouseDown,40,116)};
        if (frame == 3) return {mouseEvent(EventType::MouseMove,132,116)};
        if (frame == 4) return {mouseEvent(EventType::MouseMove,224,116)};
        if (frame == 5) return {mouseEvent(EventType::MouseUp,224,116),keyEvent(key::Return)};
        if (frame == 6) return {keyEvent(key::Escape)};
        return {};
    };
    game = engine->create(mouse,target);
    if (auto err = game->run()) return test::fail(err.message);
    CHECK(mouse.hashes.size() == 6 && !mouse.textEnabled && mouse.textStopped);
    if (mouse.hashes.size() == 6 && system.hashes.size() == 9) CHECK(mouse.hashes[5] == system.hashes[3]);
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/engine-record.txt", "boggle.engine",
              "tests/engines/boggle/engine.cpp");
}

} // namespace

static const reluva::test::Suite suite("boggle.engine", {
    {"synthetic resources and Classic round", [](const std::filesystem::path& tmp) { engineTest(tmp); }},
});

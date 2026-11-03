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
#include "engines/boggle/classic/user_dictionary.h"
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
    // Unfinished input and an Enter event arriving exactly at the deadline
    // must not add a word or restart before results have been displayed.
    boggle::fixtures::ScriptedSystem expiry;
    expiry.events = [](size_t frame) -> std::vector<Event> {
        using namespace boggle::fixtures;
        if (frame == 1 || frame == 3 || frame == 5) return {keyEvent(key::Return)};
        if (frame == 2) return {textEvent("aaa")};
        if (frame == 4) return {textEvent("aaaa")};
        if (frame == 6) return {keyEvent(key::Escape)};
        return {};
    };
    expiry.clock = [](size_t frame) -> uint64_t { return frame >= 5 ? 180010 : frame * 10; };
    game = engine->create(expiry,target);
    if (auto err = game->run()) return test::fail(err.message);
    CHECK(expiry.hashes.size() == 6 && !expiry.textEnabled && expiry.textStopped);
    if (expiry.hashes.size() == 6 && system.hashes.size() == 9) CHECK(expiry.hashes[5] == system.hashes[6]);
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/engine-record.txt", "boggle.engine",
              "tests/engines/boggle/engine.cpp");
}

void dictionaryWorkflow(const std::filesystem::path& tmp) {
    using namespace boggle::fixtures;
    const auto dir = tmp / "install", save = tmp / "saves";
    classicInstall(dir);
    const auto* meta = findEngine("boggle"); CHECK(meta); if (!meta) return;
    GameTarget target; target.path = dir; target.savePath = save;
    ScriptedSystem system;
    system.events = [](size_t frame) -> std::vector<Event> {
        if (frame == 1 || frame == 3 || frame == 5 || frame == 7 || frame == 9 || frame == 14 || frame == 16)
            return {keyEvent(key::Return)};
        if (frame == 2 || frame == 15) return {textEvent("aaaaa")};
        if (frame == 4) return {textEvent("aaaaaa")};
        if (frame == 6) return {textEvent("aaaaaaa")};
        if (frame == 8) return {textEvent("aaaaaaaa")};
        if (frame == 10 || frame == 17) return {keyEvent(key::F1)};
        if (frame == 11) return {keyEvent('1')}; // Yes
        if (frame == 12) return {keyEvent('2')}; // No
        if (frame == 13) return {keyEvent('3')}; // Yes to all
        if (frame == 18) return {keyEvent(key::Escape)};
        return {};
    };
    auto game = meta->create(system,target); if (auto err = game->run()) return test::fail(err.message);
    CHECK(system.hashes.size() == 18 && !system.textEnabled && system.textStopped);
    boggle::UserDictionary saved; CHECK(!saved.load(save, dir));
    CHECK(saved.words().size() == 3 && saved.contains("AAAAA") && saved.contains("AAAAAAA") && saved.contains("AAAAAAAA"));
    CHECK(!saved.contains("AAAAAA"));
    // A separately constructed engine must read the addition from disk.
    ScriptedSystem restarted;
    restarted.events = [](size_t frame) -> std::vector<Event> {
        if (frame == 1 || frame == 3) return {keyEvent(key::Return)};
        if (frame == 2) return {textEvent("aaaaa")};
        if (frame == 4) return {keyEvent(key::F1)};
        if (frame == 5) return {keyEvent(key::Escape)};
        return {};
    };
    game = meta->create(restarted,target); if (auto err = game->run()) return test::fail(err.message);
    CHECK(restarted.hashes.size() == 5);
    if (system.hashes.size() == 18 && restarted.hashes.size() == 5) CHECK(system.hashes[17] == restarted.hashes[4]);
    // Save failure must retain the candidate, then No to all must still work.
    target.savePath = tmp / "blocked";
    std::filesystem::create_directories(target.savePath / "EnglishUser.dic.tmp");
    ScriptedSystem failure;
    failure.events = [](size_t frame) -> std::vector<Event> {
        if (frame == 1 || frame == 3 || frame == 5) return {keyEvent(key::Return)}; // Enter cannot approve
        if (frame == 2) return {textEvent("aaaaaa")};
        if (frame == 4) return {keyEvent(key::F1)};
        if (frame == 6) return {keyEvent('1')}; // failed save
        if (frame == 7) return {mouseEvent(EventType::MouseDown,620,368)}; // No to all
        if (frame == 8) return {keyEvent(key::Escape)};
        return {};
    };
    game = meta->create(failure,target); if (auto err = game->run()) return test::fail(err.message);
    CHECK(failure.hashes.size() == 8 && !failure.textEnabled && failure.textStopped);
    CHECK(!std::filesystem::exists(target.savePath / "EnglishUser.dic"));
    if (failure.hashes.size() == 8) CHECK(failure.hashes[4] == failure.hashes[5]);
    std::string multiDice; for (int i = 0; i < 16; ++i) multiDice += "AB,AB,AB,AB,AB,AB\n";
    write(dir / "Dicts/EngOrg.dst", multiDice);
    ScriptedSystem history;
    history.events = [](size_t frame) -> std::vector<Event> {
        if (frame == 1) {
            std::vector<Event> events{keyEvent(key::Return)};
            for (size_t n = 2; n <= 16; ++n) {
                std::string word; for (size_t i = 0; i < n; ++i) word += "AB";
                events.push_back(textEvent(word.substr(0,16)));
                if (word.size() > 16) events.push_back(textEvent(word.substr(16)));
                events.push_back(keyEvent(key::Return));
            }
            return events;
        }
        if (frame == 2) return {keyEvent(key::PageUp)};
        if (frame == 3) return {keyEvent(key::PageDown)};
        if (frame == 4) return {keyEvent(key::F1)};
        if (frame == 5) return {keyEvent('4')};
        if (frame == 6) return {keyEvent(key::Escape)};
        return {};
    };
    target.savePath = tmp / "history-save";
    game = meta->create(history,target); if (auto err = game->run()) return test::fail(err.message);
    CHECK(history.hashes.size() == 6);
    if (history.hashes.size() == 6) {
        CHECK(history.hashes[1] != history.hashes[2]);
        CHECK(history.hashes[1] == history.hashes[3]);
    }
    test::Record rec;
    if (system.hashes.size() == 18) {
        rec.add("dictionary.red",system.hashes[9]); rec.add("dictionary.prompt",system.hashes[10]);
        rec.add("dictionary.no",system.hashes[12]); rec.add("dictionary.yes_all",system.hashes[13]);
        rec.add("dictionary.reloaded",system.hashes[17]);
    }
    if (failure.hashes.size() == 8) {
        rec.add("dictionary.save_error",failure.hashes[6]); rec.add("dictionary.no_all",failure.hashes[7]);
    }
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/dictionary-record.txt", "boggle.engine",
              "tests/engines/boggle/engine.cpp");
}

} // namespace

static const reluva::test::Suite suite("boggle.engine", {
    {"rejected words, dictionary choices, write failure and engine restart", [](const auto& tmp) { dictionaryWorkflow(tmp); }},
    {"synthetic resources and Classic round", [](const std::filesystem::path& tmp) { engineTest(tmp); }},
});

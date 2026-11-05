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
    GameTarget target; target.settings = {{"classic_seed","6551"},{"classic_sound","0"},{"classic_animations","0"}}; target.path = dir; target.gameId = "boggle1997";
    target.settings["classic_menu"]="0";
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
    GameTarget target; target.settings = {{"classic_seed","6551"},{"classic_sound","0"},{"classic_animations","0"}}; target.path = dir; target.savePath = save;
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
    target.settings["classic_menu"]="0";
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

void masterWorkflow(const std::filesystem::path& tmp) {
    using namespace boggle::fixtures;
    const auto dir = tmp / "install"; masterInstall(dir);
    const auto* meta = findEngine("boggle"); CHECK(meta); if (!meta) return;
    GameTarget target; target.settings = {{"classic_seed","6551"},{"classic_sound","0"},{"classic_animations","0"}}; target.path = dir; target.gameId = "boggle1997";
    target.settings = {{"classic_seed","6551"},{"classic_variant","master"},{"classic_seconds","60"},{"classic_sound","0"},{"classic_animations","0"}};
    ScriptedSystem timed;
    timed.events = [](size_t frame) -> std::vector<Event> {
        if (frame == 1 || frame == 3 || frame == 10) return {keyEvent(key::Return)};
        if (frame == 2) return {textEvent("aaa")};
        if (frame == 4) return {mouseEvent(EventType::MouseDown,113,410)};
        if (frame == 5) return {mouseEvent(EventType::MouseMove,186,410)};
        if (frame == 6) return {mouseEvent(EventType::MouseMove,259,410)};
        if (frame == 7) return {mouseEvent(EventType::MouseMove,332,410)};
        if (frame == 8) return {mouseEvent(EventType::MouseUp,332,410),keyEvent(key::Return)};
        if (frame == 11) return {keyEvent(key::Escape)};
        return {};
    };
    timed.clock = [](size_t frame) -> uint64_t { return frame >= 9 ? 60010 : frame*10; };
    target.settings["classic_menu"]="0";
    auto game = meta->create(timed,target); if (auto err = game->run()) return test::fail(err.message);
    CHECK(timed.hashes.size() == 11 && !timed.textEnabled && timed.textStopped);
    if (timed.hashes.size() == 11) CHECK(timed.hashes[1] == timed.hashes[10]);
    target.settings["classic_seconds"] = "0";
    ScriptedSystem untimed;
    untimed.events = [](size_t frame) -> std::vector<Event> {
        if (frame == 1 || frame == 3 || frame == 6) return {keyEvent(key::Return)};
        if (frame == 2) return {textEvent("aaaa")};
        if (frame == 4) return {keyEvent(key::F1 + 1)}; // cannot replace a running board
        if (frame == 5 || frame == 7) return {keyEvent(key::F1)};
        if (frame == 8) return {keyEvent(key::Escape)};
        return {};
    };
    untimed.clock = [](size_t frame) -> uint64_t { return frame >= 4 ? UINT64_C(5000000000) : frame*10; };
    game = meta->create(untimed,target); if (auto err = game->run()) return test::fail(err.message);
    CHECK(untimed.hashes.size() == 8 && !untimed.textEnabled && untimed.textStopped);
    if (untimed.hashes.size() == 8) {
        CHECK(untimed.hashes[3] == untimed.hashes[4]);
        CHECK(untimed.hashes[1] == untimed.hashes[6]);
    }
    target.settings["classic_seconds"] = "301";
    ScriptedSystem invalid;
    game = meta->create(invalid,target); CHECK(bool(game->run())); CHECK(invalid.frame == 0 && invalid.w == 0);
    test::Record rec;
    if (timed.hashes.size() == 11) {
        rec.add("master.ready",timed.hashes[0]); rec.add("master.minimum",timed.hashes[3]);
        rec.add("master.last_cube",timed.hashes[8]); rec.add("master.expired",timed.hashes[9]);
        rec.add("master.replay",timed.hashes[10]);
    }
    if (untimed.hashes.size() == 8) {
        rec.add("untimed.ready",untimed.hashes[0]); rec.add("untimed.running",untimed.hashes[4]);
        rec.add("untimed.finished",untimed.hashes[5]); rec.add("untimed.replay",untimed.hashes[6]);
    }
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/master-record.txt", "boggle.engine",
              "tests/engines/boggle/engine.cpp");
}

void presentationWorkflow(const std::filesystem::path& tmp) {
    using namespace boggle::fixtures;
    const auto dir=tmp / "install"; presentationInstall(dir);
    const auto* meta=findEngine("boggle"); CHECK(meta); if(!meta)return;
    GameTarget target; target.path=dir; target.settings={{"classic_seed","6551"},{"classic_seconds","60"}};
    ScriptedSystem sys;sys.audioSupported=true;sys.audioRequiresGraphics=true;
    sys.events=[](size_t f)->std::vector<Event>{
        if(f==1 || f==6 || f==7)return{keyEvent(key::Return)};
        if(f==11)return{keyEvent(key::Return),keyEvent(key::F1+1)};
        if(f==2)return{textEvent("aaaaa"),keyEvent(key::F1+1)}; // ignored during opening
        if(f==5)return{textEvent("aaa")};
        if(f==8)return{textEvent("aaaaa")}; // expires with unfinished input
        if(f==10)return{keyEvent('1')}; // cannot approve before a prompt exists
        if(f==12)return{keyEvent(key::Escape)};
        return{};
    };
    sys.clock=[](size_t f)->uint64_t{
        const uint64_t times[]={0,10,1010,3110,4410,4420,4430,4440,64409,64410,65509,65510,65520};
        return times[std::min(f,size_t(12))];
    };
    target.settings["classic_menu"]="0";
    auto game=meta->create(sys,target);if(auto err=game->run())return test::fail(err.message);
    CHECK(sys.hashes.size()==12 && sys.audioStarted && sys.audioStopped && !sys.audioCallback);
    CHECK(sys.audioHashes.size()==12);
    CHECK(sys.textStopped && !sys.textEnabled);
    // Closing ends at frame 11; buffered Enter must not restart before results.
    if(sys.hashes.size()==12) {
        CHECK(sys.hashes[4]!=sys.hashes[3]);
        test::Record rec;
        rec.add("media.opening",sys.hashes[1]);rec.add("media.open_lid",sys.hashes[3]);
        rec.add("media.playing",sys.hashes[4]);rec.add("media.counted",sys.hashes[6]);
        rec.add("media.closing",sys.hashes[9]);rec.add("media.results",sys.hashes[11]);
        test::Hash pcm;for(const auto& hash:sys.audioHashes)pcm.text(hash);
        rec.add("media.audio",pcm.hex());
        rec.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/presentation-record.txt","boggle.engine","tests/engines/boggle/engine.cpp");
    }
    // Exit during opening must stop the callback before the mixer is destroyed.
    ScriptedSystem quit;quit.audioSupported=true;
    quit.events=[](size_t f)->std::vector<Event>{if(f==1)return{keyEvent(key::Return)};if(f==2)return{keyEvent(key::Escape)};return{};};
    game=meta->create(quit,target);if(auto err=game->run())return test::fail(err.message);
    CHECK(quit.hashes.size()==2 && quit.audioStopped && !quit.audioCallback && quit.textStopped);
    // An unknown word's review is postponed until closing finishes.
    ScriptedSystem review;review.audioSupported=true;
    review.events=[](size_t f)->std::vector<Event>{
        if(f==1 || f==4)return{keyEvent(key::Return)};
        if(f==3)return{textEvent("aaaaa")};
        if(f==5)return{keyEvent(key::F1)};
        if(f==6)return{keyEvent('4')};
        if(f==8)return{keyEvent('4')};
        if(f==9)return{keyEvent(key::Escape)};
        return{};
    };
    review.clock=[](size_t f)->uint64_t{const uint64_t times[]={0,10,4410,4420,4430,4440,4450,5540,5550,5560};return times[std::min(f,size_t(9))];};
    game=meta->create(review,target);if(auto err=game->run())return test::fail(err.message);
    CHECK(review.hashes.size()==9 && review.audioStopped && !review.audioCallback);
    if(review.hashes.size()==9){CHECK(review.hashes[7]!=review.hashes[6]);CHECK(review.hashes[8]!=review.hashes[7]);}
    target.settings={{"classic_seed","6551"},{"classic_menu","0"},{"classic_variant","master"},{"classic_seconds","0"},{"classic_sound","0"}};
    ScriptedSystem master;
    master.events=[](size_t f)->std::vector<Event>{if(f==1)return{keyEvent(key::Return)};if(f==3)return{keyEvent(key::F1)};if(f==5)return{keyEvent(key::Escape)};return{};};
    master.clock=[](size_t f)->uint64_t{const uint64_t times[]={0,10,4410,4420,5520,5530};return times[std::min(f,size_t(5))];};
    game=meta->create(master,target);if(auto err=game->run())return test::fail(err.message);
    CHECK(master.hashes.size()==5 && master.textStopped && !master.audioStarted);
}

void musicWorkflow(const std::filesystem::path& tmp) {
    namespace fixtures = boggle::fixtures;
    const auto install=tmp/"install"; fixtures::classicInstall(install);
    fixtures::write(install/"Waves/Clasical.wav",fixtures::wave());
    GameTarget target; target.gameId="boggle1997"; target.path=install; target.savePath=tmp/"saves";
    target.settings={{"classic_seed","6551"},{"classic_music","0"},{"classic_sound","0"},{"classic_animations","0"},{"classic_seconds","0"}};
    fixtures::ScriptedSystem sys; sys.audioSupported=true; sys.audioRequiresGraphics=true;
    sys.events=[](size_t frame)->std::vector<Event>{
        if(frame==1 || frame==5)return {fixtures::keyEvent(key::Return)};
        if(frame==3)return {fixtures::keyEvent(key::F1)};
        if(frame==7)return {fixtures::keyEvent(key::Escape)};
        return {};
    };
    const auto* meta=findEngine("boggle"); CHECK(meta); if(!meta)return;
    target.settings["classic_menu"]="0";
    auto game=meta->create(sys,target); if(auto err=game->run())return test::fail(err.message);
    CHECK(sys.audioStarted && sys.audioStopped && !sys.audioCallback);
    CHECK(sys.audioHashes.size()==7);
    if(sys.audioHashes.size()==7) {
        std::vector<int16_t> silence(512); const auto quiet=test::Hash().bytes(silence.data(),silence.size()*sizeof(int16_t)).hex();
        CHECK(sys.audioHashes[0]==quiet);
        for(size_t i=1;i<sys.audioHashes.size();++i) CHECK(sys.audioHashes[i]!=quiet);
    }
}

} // namespace

static const reluva::test::Suite suite("boggle.engine", {
    {"music-only start, finish, results, replay and callback cleanup",[](const auto& tmp){musicWorkflow(tmp);}},
    {"animated start/expiry/review, timer gating, sound and early exit",[](const auto& tmp){presentationWorkflow(tmp);}},
    {"Master geometry, configured deadline, replay and untimed finish", [](const auto& tmp) { masterWorkflow(tmp); }},
    {"rejected words, dictionary choices, write failure and engine restart", [](const auto& tmp) { dictionaryWorkflow(tmp); }},
    {"synthetic resources and Classic round", [](const std::filesystem::path& tmp) { engineTest(tmp); }},
});

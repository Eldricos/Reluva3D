// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/registry.h"
#include "engines/boggle/classic/options.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
using namespace reluva;
using namespace reluva::boggle;
namespace {
void artwork(const std::filesystem::path& path) {
    std::vector<uint8_t> bmp(54+640*480*3);
    bmp[0]='B';bmp[1]='M';
    test::fixtures::put32(bmp,2,uint32_t(bmp.size()));test::fixtures::put32(bmp,10,54);
    test::fixtures::put32(bmp,14,40);test::fixtures::put32(bmp,18,640);test::fixtures::put32(bmp,22,480);
    test::fixtures::put16(bmp,26,1);test::fixtures::put16(bmp,28,24);
    for(size_t i=54;i<bmp.size();++i) bmp[i]=uint8_t(i%173);
    fixtures::write(path/"Bitmaps/Select1.bmp",bmp);
}
GameTarget target(const std::filesystem::path& tmp) {
    fixtures::masterInstall(tmp/"install");
    GameTarget t;t.path=tmp/"install";t.savePath=tmp/"saves";t.gameId="boggle1997";
    t.settings={{"classic_sound","0"},{"classic_animations","0"}};return t;
}
void navigation(const std::filesystem::path& tmp) {
    auto t=target(tmp);artwork(t.path);
    fixtures::ScriptedSystem sys;
    sys.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==1)return {keyEvent(key::Return),keyEvent(key::Return)}; // cannot skip setup in one batch
        if(f==2)return {keyEvent('2'),keyEvent(key::Right),keyEvent('t'),keyEvent(key::Return)};
        if(f==3)return {textEvent("aaaa"),keyEvent(key::Return)};
        if(f==4)return {keyEvent(key::F1)};
        if(f==5)return {keyEvent(key::Escape)};
        if(f==6)return {keyEvent(key::Return)};
        if(f==7)return {keyEvent('1'),keyEvent(key::Return)};
        if(f==8 || f==9)return {keyEvent(key::Escape)};
        return {};
    };
    const auto* meta=findEngine("boggle");CHECK(meta);if(!meta)return;
    auto game=meta->create(sys,t);if(auto err=game->run())return test::fail(err.message);
    CHECK(sys.frame==9 && sys.w==640 && sys.h==480 && !sys.textEnabled);
    CHECK(!sys.audioStarted);
    if(sys.hashes.size()==9) {
        CHECK(sys.hashes[0]==sys.hashes[5] && sys.hashes[0]==sys.hashes[8]);
        CHECK(sys.hashes[1]!=sys.hashes[6]); // chosen Master / untimed preserved after return
        CHECK(sys.hashes[2]!=sys.hashes[3]); // submitted word changed score/list
        CHECK(sys.hashes[2]!=sys.hashes[7]); // Original and Master render different geometry
        test::Record rec;
        for(size_t i=0;i<sys.hashes.size();++i) rec.add("navigation."+std::to_string(i),sys.hashes[i]);
        rec.check(std::filesystem::path(RELUVA_SOURCE_DIR)/"tests/engines/boggle/data/frontend-record.txt",
                  "boggle.unit.frontend","tests/engines/boggle/unit/frontend.cpp");
    }
}
void mouseAndGuards(const std::filesystem::path& tmp) {
    auto t=target(tmp); // old imports with no Select1 art use native fallback
    fixtures::ScriptedSystem sys;
    sys.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==1) {auto repeat=keyEvent(key::Return);repeat.repeat=true;return {repeat};}
        if(f==2)return {mouseEvent(EventType::MouseDown,50,300)};
        if(f==3)return {mouseEvent(EventType::MouseDown,70,400),keyEvent(key::Return)};
        if(f==4)return {mouseEvent(EventType::MouseDown,210,300)}; // unavailable mode
        if(f==5)return {mouseEvent(EventType::MouseDown,480,430)};
        return {};
    };
    auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run());CHECK(sys.frame==5);
    if(sys.hashes.size()==5) {
        CHECK(sys.hashes[0]==sys.hashes[1] && sys.hashes[0]==sys.hashes[3] && sys.hashes[0]==sys.hashes[4]);
        CHECK(sys.hashes[0]!=sys.hashes[2]);
    }
    // Mouse selects Master, adjusts time and starts immediately; global quit
    // must leave the whole engine rather than re-opening a menu.
    for(const auto quitType : {EventType::Quit,EventType::ReturnToLauncher}) {
        fixtures::ScriptedSystem quit;
        quit.events=[quitType](size_t f)->std::vector<Event>{
            using namespace fixtures;
            if(f==1)return {mouseEvent(EventType::MouseDown,50,300)};
            if(f==2)return {mouseEvent(EventType::MouseDown,360,180),mouseEvent(EventType::MouseDown,430,270),mouseEvent(EventType::MouseDown,360,400)};
            if(f==3){Event e;e.type=quitType;return {e};}return {};
        };
        game=findEngine("boggle")->create(quit,t);CHECK(!game->run());
        CHECK(quit.frame==3 && quit.w==800 && quit.h==600 && quit.textStopped);
    }
}
void musicLifetime(const std::filesystem::path& tmp) {
    auto t=target(tmp);t.settings["classic_music"]="random";
    for(size_t i=0;i<classicMusicFiles.size();++i) {
        auto wave=fixtures::wave();test::fixtures::put16(wave,44,uint16_t(100+i));
        fixtures::write(t.path/"Waves"/(std::string(classicMusicFiles[i])+".wav"),wave);
    }
    fixtures::ScriptedSystem sys;sys.audioSupported=true;
    sys.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==1 || f==2 || f==4 || f==5)return {keyEvent(key::Return)};
        if(f==3 || f==6 || f==7)return {keyEvent(key::Escape)};
        return {};
    };
    auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run());
    CHECK(sys.frame==7 && sys.audioStopped && !sys.audioCallback);
    CHECK(sys.audioHashes.size()==2);
    if(sys.audioHashes.size()==2) CHECK(sys.audioHashes[0]==sys.audioHashes[1]);
}
void timerAndErrors(const std::filesystem::path& tmp) {
    auto t=target(tmp);t.settings["classic_seconds"]="60";
    fixtures::ScriptedSystem sys;
    sys.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==1 || f==2)return {keyEvent(key::Return)};
        if(f==4)return {keyEvent(key::Escape),keyEvent(key::Escape)}; // one batch only leaves the round
        if(f==5)return {keyEvent(key::Escape)};
        return {};
    };
    sys.clock=[](size_t f)->uint64_t{return f<2 ? 100000 : f<3 ? 200000 : 260000;};
    auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run());CHECK(sys.frame==5);
    if(sys.hashes.size()==5) CHECK(sys.hashes[0]==sys.hashes[4] && sys.hashes[2]!=sys.hashes[3]);
    // Optional artwork is not permission to ignore present but malformed data.
    fixtures::write(t.path/"Bitmaps/Select1.bmp","broken");
    fixtures::ScriptedSystem bad;game=findEngine("boggle")->create(bad,t);
    CHECK(bool(game->run()) && bad.w==0 && !bad.audioStarted);
    fixtures::write(t.path/"Bitmaps/Select1.bmp",fixtures::bitmap());
    game=findEngine("boggle")->create(bad,t);CHECK(bool(game->run()) && bad.w==0);
    t.settings["classic_menu"]="2";game=findEngine("boggle")->create(bad,t);CHECK(bool(game->run()) && bad.w==0);
}
}
static const test::Suite suite("boggle.unit.frontend",{
    {"selection, variant setup, auto-start, results, return and re-entry",[](const auto& tmp){navigation(tmp);}},
    {"mouse navigation, legacy artwork fallback and global exit",[](const auto& tmp){mouseAndGuards(tmp);}},
    {"random music stays selected across menu re-entry and releases output",[](const auto& tmp){musicLifetime(tmp);}},
    {"timer starts with play, input boundaries and invalid artwork/settings",[](const auto& tmp){timerAndErrors(tmp);}},
});

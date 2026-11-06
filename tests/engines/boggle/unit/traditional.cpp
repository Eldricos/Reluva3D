// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/highscores.h"
#include "engines/boggle/classic/player.h"
#include "engines/boggle/classic/user_dictionary.h"
#include "engines/registry.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <algorithm>
using namespace reluva;
using namespace reluva::boggle;
namespace {
GameTarget target(const std::filesystem::path& root,bool master=false,bool art=true) {
    fixtures::artworkInstall(root / "game");GameTarget t;t.path=root / "game";t.savePath=root / "save";
    t.settings={{"classic_seed","6551"},{"classic_sound","0"},{"classic_animations","0"},{"classic_seconds","0"},
                {"classic_artwork",art?"1":"0"},{"classic_variant",master?"master":"original"},{"frontend_movies","0"}};return t;
}
void workflow(const std::filesystem::path& tmp) {
    test::Record record;
    for(bool master:{false,true})for(bool art:{false,true}) {
        auto t=target(tmp / (std::to_string(master)+std::to_string(art)),master,art);
        fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==13 || f==19)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAA"),keyEvent(key::Return),keyEvent(key::F1)};
            if(f==2){CHECK(!std::filesystem::exists(t.savePath / "HighScores.txt"));return {keyEvent('1'),keyEvent(key::Return)};}
            if(f==3)return {textEvent("AAAA"),keyEvent(key::Return),keyEvent(key::F1+1)}; // handoff ignores text and consumes after Enter
            if(f==4)return {textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAAA"),keyEvent(key::Return),keyEvent(key::F1)};
            if(f==5){CHECK(!std::filesystem::exists(t.savePath / "HighScores.txt"));return {keyEvent('2'),keyEvent(key::Return)};}
            if(f==6 || f==12)return {keyEvent(key::Return),keyEvent(key::Return)};
            if(f==7)return {keyEvent('2')};
            if(f==8)return {keyEvent('a')};
            if(f==9)return {keyEvent('v')};
            if(f==10)return {keyEvent('1')};
            if(f==11)return {keyEvent('h')};
            if(f==14 || f==16 || f==20)return {keyEvent(key::F1)};
            if(f==15 || f==17)return {keyEvent(key::Return)};
            if(f==18)return {keyEvent(key::F1+1)};
            if(f==21)return {keyEvent(key::Escape)};
            return {};
        };
        ClassicGame game(sys,t,false,{"Alex","Sam"});CHECK(!game.run());CHECK(sys.hashes.size()==21 && !sys.textEnabled);
        if(sys.hashes.size()!=21)continue;
        CHECK(sys.hashes[2]==sys.hashes[14] && sys.hashes[14]==sys.hashes[20]);
        CHECK(sys.hashes[6]==sys.hashes[10] && sys.hashes[10]==sys.hashes[12]);
        CHECK(sys.hashes[7]!=sys.hashes[6] && sys.hashes[8]!=sys.hashes[7]);
        fixtures::ScriptedSystem control;control.clock=sys.clock;control.events=[](size_t f)->std::vector<Event>{
            if(f==0)return {fixtures::keyEvent(key::Return)};
            if(f==1)return {fixtures::keyEvent(key::Escape)};
            return {};
        };
        ClassicGame fresh(control,t,false,{"Sam"});CHECK(!fresh.run());CHECK(control.hashes.size()==1);
        if(control.hashes.size()==1)CHECK(sys.hashes[3]==control.hashes[0]);
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();
        CHECK(rows.size()==1 && rows[0].name=="Alex" && rows[0].score==2); // shared AAAA contributes nothing; replay/partial match add nothing
        const auto prefix=std::string(master?"master.":"original.")+(art?"art.":"plain.");
        for(size_t i=0;i<sys.hashes.size();++i)record.add(prefix+std::to_string(i),sys.hashes[i]);
    }
    record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/traditional-record.txt","boggle.unit.traditional","tests/engines/boggle/unit/traditional.cpp");
}
void larger(const std::filesystem::path& tmp) {
    for(size_t count:{size_t(3),size_t(4)}) {
        auto t=target(tmp / std::to_string(count));UserDictionary user;CHECK(!user.load(t.savePath,t.path));
        CHECK(!user.add({"AAAAA","AAAAAA","AAAAAAA","AAAAAAAA"}));
        std::vector<std::string> names={"Alex","Sam","Alex","Jo"};names.resize(count);
        fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==count*3){Event quit;quit.type=EventType::Quit;return {quit};}
            if(f%3==1){const auto player=(f-1)/3;return {textEvent("AAAA"),keyEvent(key::Return),textEvent(std::string(5+player,'A')),keyEvent(key::Return),keyEvent(key::F1)};}
            if(f%3==2)return {keyEvent(key::Return)};
            return {};
        };
        ClassicGame game(sys,t,false,names);CHECK(!game.run());CHECK(sys.hashes.size()==count*3);
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));auto rows=scores.table();CHECK(rows.size()==count);
        int total=0;for(const auto& row:rows)total+=row.score;CHECK(total==(count==3?10:21));
        CHECK(std::count_if(rows.begin(),rows.end(),[](const auto& row){return row.name=="Alex";})==2);
    }
}
void timing(const std::filesystem::path& tmp) {
    for(bool animations:{false,true}) {
        auto t=target(tmp / std::to_string(animations));t.settings["classic_seconds"]="60";t.settings["classic_animations"]=animations?"1":"0";
        fixtures::ScriptedSystem sys;
        sys.clock=[&](size_t f)->uint64_t {
            if(animations){const uint64_t times[]={0,4400,5500,100000,104400,105500,105500,105500};return times[std::min(f,size_t(7))];}
            const uint64_t times[]={0,0,60000,600000,600000,660000,660000,660000};return times[std::min(f,size_t(7))];
        };
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==3 || f==6)return {keyEvent(key::Return)};
            if(f==1){auto out=std::vector<Event>{textEvent("AAAA"),keyEvent(key::Return),textEvent("AAA"),keyEvent(key::Return),textEvent("AAAAAA")};if(animations)out.push_back(keyEvent(key::F1));return out;}
            if(f==4){auto out=std::vector<Event>{textEvent("AAAA"),keyEvent(key::Return)};if(animations)out.push_back(keyEvent(key::F1));return out;}
            if(f==7){Event quit;quit.type=EventType::Quit;return {quit};}return {};
        };
        ClassicGame game(sys,t,false,{"Alex","Sam"});CHECK(!game.run());CHECK(sys.hashes.size()==7);
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));auto rows=scores.table();
        CHECK(rows.size()==1 && rows[0].name=="Alex" && rows[0].score==1); // each timer starts after handoff/opening; partial input lost
    }
}
void saveRetry(const std::filesystem::path& tmp) {
    auto t=target(tmp);UserDictionary user;CHECK(!user.load(t.savePath,t.path));CHECK(!user.add({"AAAAA","AAAAAA"}));
    std::filesystem::create_directory(t.savePath / "HighScores.txt.tmp");fixtures::ScriptedSystem sys;
    sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==2 || f==4 || f==7)return {keyEvent(key::Return)};
        if(f==1)return {textEvent("AAAAA"),keyEvent(key::Return),keyEvent(key::F1)};
        if(f==3)return {textEvent("AAAAAA"),keyEvent(key::Return),keyEvent(key::F1)};
        if(f==5){CHECK(!std::filesystem::exists(t.savePath / "HighScores.txt"));return {keyEvent('h')};}
        if(f==6){std::filesystem::remove(t.savePath / "HighScores.txt.tmp");return {keyEvent('r'),keyEvent('r')};}
        if(f==8){Event quit;quit.type=EventType::Quit;return {quit};}return {};
    };
    ClassicGame game(sys,t,false,{"Alex","Sam"});CHECK(!game.run());CHECK(sys.hashes.size()==8);
    HighScores scores;CHECK(!scores.load(t.savePath,t.path));auto rows=scores.table();
    CHECK(rows.size()==2 && rows[0].name=="Sam" && rows[0].score==3 && rows[1].name=="Alex" && rows[1].score==2);
}

void roster(const std::filesystem::path& tmp) {
    auto t=target(tmp);PlayerProfile profile;CHECK(!profile.load(t.savePath,t.path));CHECK(!profile.rename("Alex"));CHECK(!profile.rename("Sam"));
    fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
    sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==1 || f==7 || f==9 || f==11 || f==13 || f==14 || f==19)return {keyEvent(key::Return)};
        if(f==2 || f==15)return {keyEvent('m')};
        if(f==3 || f==16)return {keyEvent('2')};
        if(f==4)return {keyEvent(key::Home)};
        if(f==5)return {keyEvent(key::Down),keyEvent(key::Return)};
        if(f==6)return {keyEvent(key::Return),keyEvent(key::Return)};
        if(f==8 || f==10)return {textEvent("AAAA"),keyEvent(key::Return),keyEvent(key::F1)};
        if(f==12 || f==21)return {keyEvent(key::F1+2)};
        if(f==17)return {keyEvent(key::End),keyEvent(key::Return)};
        if(f==18)return {keyEvent(key::Return)};
        if(f==20)return {keyEvent(key::F1)};
        if(f==22)return {keyEvent('l')};
        return {};
    };
    auto engine=findEngine("boggle")->create(sys,t);CHECK(!engine->run());CHECK(sys.hashes.size()==22 && !sys.textEnabled);
    CHECK(!profile.load(t.savePath,t.path));CHECK(profile.name()=="Sam");
    CHECK(!std::filesystem::exists(t.savePath / "HighScores.txt"));
    test::Record record;for(size_t i=0;i<sys.hashes.size();++i)record.add("roster."+std::to_string(i),sys.hashes[i]);
    record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/roster-record.txt","boggle.unit.traditional","tests/engines/boggle/unit/traditional.cpp");
    for(const auto& names:{std::vector<std::string>{""},std::vector<std::string>(5,"Alex")}) {
        fixtures::ScriptedSystem bad;ClassicGame game(bad,t,false,names);CHECK(bool(game.run()) && bad.w==0);
    }
}
}
static const test::Suite suite("boggle.unit.traditional",{
    {"failed match save retries all human scores once",[](const auto& tmp){saveRetry(tmp);}},
    {"two-player adjudication, hidden handoff, shared review, score save, replay and new board",[](const auto& tmp){workflow(tmp);}},
    {"three/four humans, independent totals and repeated names",[](const auto& tmp){larger(tmp);}},
    {"full per-player timers, idle handoff, lid transitions and discarded unfinished input",[](const auto& tmp){timing(tmp);}},
    {"live roster choices, optional slot removal, batch guards, default single-player and invalid rosters",[](const auto& tmp){roster(tmp);}},
});

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/turn_clock.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/classic/highscores.h"
#include "engines/boggle/classic/player.h"
#include "engines/boggle/classic/user_dictionary.h"
#include "engines/registry.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <algorithm>
#include <limits>
using namespace reluva;
using namespace reluva::boggle;
namespace {
void clocks() {
    ClassicClock game;CHECK(game.now(100)==100);game.focus(false,150);
    CHECK(game.paused() && game.now(100000)==150);game.focus(false,100001);
    game.focus(true,200000);CHECK(!game.paused() && game.now(200017)==167);
    CHECK(game.now(0)==167 && game.now(200020)==170);
    CHECK(game.now(UINT64_MAX)==UINT64_MAX-199850);
    ClassicClock nested;CHECK(nested.now(0)==0);nested.manual(true,10);
    nested.focus(false,20);nested.notice(true,30);nested.focus(true,40);
    CHECK(nested.manual() && nested.focused() && nested.now(10000)==10);
    nested.manual(false,10001);CHECK(nested.paused() && !nested.manual());
    nested.notice(false,10002);CHECK(!nested.paused() && nested.now(10012)==20);
    PlayerClocks c;CHECK(c.elapsed(0,1000)==0 && !c.active());CHECK(bool(c.reset(0)));CHECK(bool(c.reset(5)));
    CHECK(!c.reset(3));CHECK(!c.activate(0,1000));CHECK(c.elapsed(0,1250)==250 && c.elapsed(1,1250)==0);
    CHECK(!c.activate(1,1500));CHECK(c.elapsed(0,4000)==500 && c.elapsed(1,4000)==2500);
    c.stop(5000);CHECK(!c.active());CHECK(c.elapsed(1,50000)==3500 && c.elapsed(2,50000)==0);
    CHECK(!c.activate(0,50000));CHECK(c.elapsed(0,50100)==600 && c.elapsed(1,50100)==3500);
    CHECK(bool(c.activate(3,99999)));CHECK(c.active()==0);CHECK(c.elapsed(0,50200)==700);
    CHECK(c.elapsed(0,0)==700); // backward readings do not refund time
    CHECK(!c.activate(0,0));CHECK(c.elapsed(0,50250)==750);
    CHECK(!c.reset(1));CHECK(!c.activate(0,0));const auto maximum=std::numeric_limits<uint64_t>::max();
    CHECK(c.elapsed(0,maximum)==maximum);c.stop(maximum);CHECK(c.elapsed(0,0)==maximum);
    CHECK(!c.reset(4));for(size_t i=0;i<4;++i)CHECK(c.elapsed(i,0)==0);
    ClassicOptions options;CHECK(!readClassicOptions({},options));CHECK(options.turns==ClassicTurnMode::Traditional);
    CHECK(!readClassicOptions({{"classic_turns","chess"}},options));CHECK(options.turns==ClassicTurnMode::Chess);
    CHECK(!readClassicOptions({{"classic_turns","traditional"}},options));CHECK(options.turns==ClassicTurnMode::Traditional);
    for(const auto* bad:{"","Chess","0","chess ","unknown"}){CHECK(bool(readClassicOptions({{"classic_turns",bad}},options)));CHECK(options.turns==ClassicTurnMode::Traditional);}
}
GameTarget target(const std::filesystem::path& root,bool master=false,bool art=true) {
    fixtures::artworkInstall(root / "game");GameTarget t;t.path=root / "game";t.savePath=root / "save";
    t.settings={{"classic_seed","6551"},{"classic_turns","chess"},{"classic_seconds","0"},{"classic_sound","0"},{"classic_animations","0"},
                {"classic_variant",master?"master":"original"},{"classic_artwork",art?"1":"0"},{"frontend_movies","0"}};return t;
}
void focusLoss(const std::filesystem::path& tmp) {
    for(bool chess:{false,true})for(bool background:{false,true})for(bool animated:{false,true}) {
        const auto root=tmp / (std::to_string(chess)+std::to_string(background)+std::to_string(animated));
        std::vector<std::string> reference;
        for(bool paused:{false,true}) {
            auto t=target(root / std::to_string(paused));t.settings["classic_seconds"]="60";
            t.settings["classic_turns"]=chess?"chess":"traditional";
            t.settings["classic_animations"]=animated?"1":"0";
            t.settings["classic_background"]=background?"1":"0";
            fixtures::ScriptedSystem sys;
            sys.clock=[&](size_t f)->uint64_t {
                const uint64_t times[]={0,100,100,100,4500,4501,4502};
                return times[std::min(f,size_t(6))]+(paused && f>=2?1000000:0);
            };
            sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
                if(f==0)return {keyEvent(key::Return)};
                if(f==1 && paused){Event e;e.type=EventType::FocusLost;return {e,textEvent("AAAA"),keyEvent(key::F1)};}
                if(f==2 && paused){Event e;e.type=EventType::FocusLost;return {e};}
                if(f==3 && paused){Event e;e.type=EventType::FocusGained;return {e,textEvent("AAAA")};}
                if(f==4)return {textEvent("AAAA"),keyEvent(key::Return)};
                if(f==6){Event e;e.type=EventType::Quit;return {e};}return {};
            };
            ClassicGame game(sys,t,false,background?std::vector<std::string>{"Alex","CPU Mr. Boggle"}:std::vector<std::string>{"Alex"},
                background?std::vector<ComputerLevel>{ComputerLevel::Human,ComputerLevel::MrBoggle}:std::vector<ComputerLevel>{ComputerLevel::Human});
            CHECK(!game.run());CHECK(sys.hashes.size()==6);
            if(!paused)reference=sys.hashes;
            else if(sys.hashes.size()==6 && reference.size()==6) {
                CHECK(sys.hashes[1]==sys.hashes[2]); // duplicate loss stays paused
                CHECK(sys.hashes[3]==reference[3]); // queued text is ignored on regain
                CHECK(sys.hashes[4]==reference[4] && sys.hashes[5]==reference[5]);
            }
        }
    }
}
void manualPause(const std::filesystem::path& tmp) {
    for(bool chess:{false,true})for(bool art:{false,true})for(int cpu=0;cpu<3;++cpu) {
        const auto root=tmp / (std::to_string(chess)+std::to_string(art)+std::to_string(cpu));
        std::vector<std::string> reference;
        for(bool paused:{false,true}) {
            auto t=target(root / std::to_string(paused),false,art);t.settings["classic_seconds"]="60";
            t.settings["classic_turns"]=chess?"chess":"traditional";t.settings["classic_background"]=cpu==2?"1":"0";
            fixtures::ScriptedSystem sys;
            sys.clock=[&](size_t f)->uint64_t {return (f==0?0:f==1?1000:f<7?2000:3000)+(paused && f>=3?1000000:0);};
            sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
                if(f==0)return {keyEvent(key::Return)};
                if(f==1)return {textEvent("AAAA")};
                if(f==2 && paused)return {mouseEvent(EventType::MouseDown,art?700:440,570),keyEvent(key::Return),keyEvent(key::F1)};
                if(f==3 && paused){Event e;e.type=EventType::FocusLost;return {e};}
                if(f==4 && paused)return {keyEvent('p'),keyEvent(key::Return)}; // no keyboard pause/resume shortcut
                if(f==5 && paused){Event e;e.type=EventType::FocusGained;return {e,mouseEvent(EventType::MouseDown,400,360)};}
                if(f==6 && paused)return {mouseEvent(EventType::MouseDown,400,360),keyEvent(key::F1),textEvent("AAAA")};
                if(f==7)return {keyEvent(key::Return)};
                if(f==8){Event e;e.type=EventType::Quit;return {e};}return {};
            };
            const std::vector<std::string> names=cpu==1?std::vector<std::string>{"CPU Mr. Boggle","Alex"}:cpu==2?std::vector<std::string>{"Alex","CPU Mr. Boggle"}:std::vector<std::string>{"Alex"};
            const std::vector<ComputerLevel> levels=cpu==1?std::vector<ComputerLevel>{ComputerLevel::MrBoggle,ComputerLevel::Human}:cpu==2?std::vector<ComputerLevel>{ComputerLevel::Human,ComputerLevel::MrBoggle}:std::vector<ComputerLevel>{ComputerLevel::Human};
            ClassicGame game(sys,t,false,names,levels);CHECK(!game.run() && sys.hashes.size()==8);
            if(!paused)reference=sys.hashes;
            else if(reference.size()==8 && sys.hashes.size()==8) {
                CHECK(sys.hashes[2]!=reference[2] && sys.hashes[3]==sys.hashes[4]);
                CHECK(sys.hashes[2]==sys.hashes[5]); // returning to the window preserves manual pause
                CHECK(sys.hashes[6]==reference[6] && sys.hashes[7]==reference[7]);
            }
        }
    }
}
void shortNotice(const std::filesystem::path& tmp) {
    for(bool master:{false,true})for(bool art:{false,true}) {
        auto t=target(tmp / (std::to_string(master)+std::to_string(art)),master,art);t.settings["classic_seconds"]="60";
        fixtures::ScriptedSystem sys;
        sys.clock=[](size_t f)->uint64_t {return f==0?0:f==1?1000:f<6?1000000:1000000+(f-5)*1000;};
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==7 || f==9)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AA"),keyEvent(key::Return),keyEvent(key::Return),keyEvent(key::F1)};
            if(f==2)return {textEvent("AAAA"),keyEvent(key::F1)};
            if(f==5)return {art?mouseEvent(EventType::MouseDown,400,370):keyEvent(key::Escape),textEvent("AAAA"),keyEvent(key::F1)};
            if(f==6)return {textEvent("ZZZZ"),textEvent("AAAA"),keyEvent(key::Return)};
            if(f==8 || f==10)return {keyEvent(key::F1)};
            if(f==11){Event e;e.type=EventType::Quit;return {e};}return {};
        };
        ClassicGame game(sys,t,false,{"Alex","Sam"});CHECK(!game.run() && sys.hashes.size()==11);
        if(sys.hashes.size()==11)CHECK(sys.hashes[1]==sys.hashes[2] && sys.hashes[2]==sys.hashes[4]);
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();
        CHECK(rows.size()==2 && rows[0].name=="Alex" && rows[0].score==1 && rows[1].name=="Sam" && rows[1].score==0);
    }
}
void rotation(const std::filesystem::path& tmp) {
    test::Record record;
    for(bool master:{false,true})for(bool art:{false,true}) {
        auto t=target(tmp / (std::to_string(master)+std::to_string(art)),master,art);UserDictionary user;CHECK(!user.load(t.savePath,t.path));CHECK(!user.add({"AAAAAA"}));
        fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
        sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AA"),keyEvent(key::Return)};
            if(f==2)return {textEvent("ZZZZ"),keyEvent(key::Return)};
            if(f==3)return {textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAAA"),keyEvent(key::Return)};
            if(f==4 || f==6 || f==8 || f==12 || f==15)return {keyEvent(key::Return)};
            if(f==5)return {textEvent("AAAAA"),keyEvent(key::Return)};
            if(f==7)return {textEvent("aaaa"),keyEvent(key::Return)}; // repeated word also passes; no new entry or points
            if(f==9)return {textEvent("AAAA"),keyEvent(key::Return)};
            if(f==10)return {textEvent("AAAAAA"),keyEvent(key::Return)}; // handoff discards text before confirming Alex
            if(f==11)return {textEvent("AAAAAA"),keyEvent(key::Return)};
            if(f==13 || f==16)return {keyEvent(key::F1)};
            if(f==14)return {keyEvent(key::Return)}; // review is deferred until every player finishes
            if(f==17)return {keyEvent('1')};
            if(f==18)return {keyEvent('2')};
            if(f==19)return {keyEvent('a')};
            if(f==20)return {keyEvent('v')};
            if(f==21)return {keyEvent(key::Return),keyEvent(key::Return)};
            if(f==22)return {keyEvent(key::Return)};
            if(f==23 || f==25)return {keyEvent(key::F1)};
            if(f==24 || f==26)return {keyEvent(key::Return)};
            if(f==27)return {keyEvent(key::F1+1)};
            if(f==28)return {keyEvent(key::Escape)};
            return {};
        };
        ClassicGame game(sys,t,false,{"Alex","Sam"});CHECK(!game.run());CHECK(sys.hashes.size()==28 && !sys.textEnabled);
        if(sys.hashes.size()!=28)continue;
        CHECK(sys.hashes[3]==sys.hashes[7] && sys.hashes[3]==sys.hashes[11]); // new and repeated words pass to Sam
        CHECK(sys.hashes[3]!=sys.hashes[23]); // finished-player notice on retirement
        CHECK(sys.hashes[0]==sys.hashes[22]);
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();
        CHECK(rows.size()==4 && rows[0].name=="Alex" && rows[0].score==3 && rows[1].name=="Sam" && rows[1].score==2);
        const auto prefix=std::string(master?"master.":"original.")+(art?"art.":"plain.");
        for(size_t i=0;i<sys.hashes.size();++i)record.add(prefix+std::to_string(i),sys.hashes[i]);
    }
    record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/chess-record.txt","boggle.unit.chess","tests/engines/boggle/unit/chess.cpp");
}
void budget(const std::filesystem::path& tmp) {
    auto t=target(tmp);t.settings["classic_seconds"]="60";fixtures::ScriptedSystem sys;
    sys.clock=[](size_t f)->uint64_t {
        const uint64_t times[]={1000,11000,300000,400000,420000,500000,600000,649999,650000,700000,800000,839999,840000,840000,840000};
        return times[std::min(f,size_t(14))];
    };
    sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==3 || f==6 || f==10)return {keyEvent(key::Return)};
        if(f==1 || f==4)return {textEvent("AAAA"),keyEvent(key::Return)};
        if(f==7 || f==11){CHECK(!std::filesystem::exists(t.savePath / "HighScores.txt"));return {};}
        if(f==13)return {keyEvent(key::Return)};
        if(f==14){Event quit;quit.type=EventType::Quit;return {quit};}return {};
    };
    ClassicGame game(sys,t,false,{"Alex","Sam"});CHECK(!game.run());CHECK(sys.hashes.size()==14);
    if(sys.hashes.size()==14) {
        CHECK(sys.hashes[1]==sys.hashes[2]);CHECK(sys.hashes[4]==sys.hashes[5]); // handoff never drains inactive timers
        CHECK(sys.hashes[7]!=sys.hashes[8]);CHECK(sys.hashes[11]!=sys.hashes[12]); // separate exact deadlines, not full-time reset
    }
    HighScores scores;CHECK(!scores.load(t.savePath,t.path));CHECK(scores.table().size()==2 && scores.table()[0].score==0 && scores.table()[1].score==0); // shared word cancels
}
void animationAndSolo(const std::filesystem::path& tmp) {
    auto t=target(tmp / "animated");t.settings["classic_animations"]="1";t.settings["classic_seconds"]="60";
    fixtures::ScriptedSystem sys;
    sys.clock=[](size_t f)->uint64_t {
        const uint64_t times[]={0,4400,100000,110000,200000,259999,300000,350000,400000,400001,401101,401101,401101};
        return times[std::min(f,size_t(12))];
    };
    sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==2 || f==4 || f==6 || f==8 || f==11)return {keyEvent(key::Return)};
        if(f==1 || f==3)return {textEvent("AAAA"),keyEvent(key::Return)};
        if(f==5)return {textEvent("AAA"),keyEvent(key::Return)}; // one ms remains after resumed active time
        if(f==12){Event quit;quit.type=EventType::Quit;return {quit};}return {};
    };
    ClassicGame game(sys,t,false,{"Alex","Sam"});CHECK(!game.run());CHECK(sys.hashes.size()==12);
    HighScores scores;CHECK(!scores.load(t.savePath,t.path));auto rows=scores.table();
    CHECK(rows.size()==2 && rows[0].name=="Alex" && rows[0].score==1);
    auto solo=target(tmp / "solo");fixtures::ScriptedSystem one;
    one.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0)return {keyEvent(key::Return)};
        if(f==1)return {textEvent("AAAA"),keyEvent(key::Return),keyEvent(key::F1)};
        if(f==2){Event quit;quit.type=EventType::Quit;return {quit};}return {};
    };
    ClassicGame single(one,solo);CHECK(!single.run());CHECK(one.hashes.size()==2);
    CHECK(!scores.load(solo.savePath,solo.path));CHECK(scores.table().size()==1 && scores.table()[0].score==1);
}

void submissionBoundary(const std::filesystem::path& tmp) {
    for(uint64_t offset:{4u,8u,10u,12u,14u,16u,20u,32u}) {
        auto t=target(tmp / std::to_string(offset));t.settings["classic_seconds"]="60";
        fixtures::ScriptedSystem sys;uint64_t reads=0;
        sys.clock=[&](size_t frame)->uint64_t {
            if(!frame)return 1000;
            if(frame==1)return 61000-offset+reads++; // wall time crosses expiry while servicing an attempted word
            return 1000000+frame*100000;
        };
        sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(!f)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AAAAA"),keyEvent(key::Return)};
            if(f==12){Event quit;quit.type=EventType::Quit;return {quit};}
            return {keyEvent('2'),keyEvent(key::F1),keyEvent(key::Return)};
        };
        ClassicGame game(sys,t,false,{"Alex","Sam"});CHECK(!game.run());CHECK(!sys.textEnabled);
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));for(const auto& row:scores.table())CHECK(row.score==0);
    }
}

void rosterAndSkip(const std::filesystem::path& tmp) {
    auto t=target(tmp);t.settings["classic_turns"]="traditional";PlayerProfile profile;CHECK(!profile.load(t.savePath,t.path));CHECK(!profile.rename("Alex"));CHECK(!profile.rename("Sam"));
    fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
    sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==1 || f==8 || f==10 || f==14 || f==17)return {keyEvent(key::Return)};
        if(f==2)return {keyEvent('c'),keyEvent(key::Return)}; // change mode only
        if(f==3)return {keyEvent('m')};
        if(f==4)return {keyEvent('2')};
        if(f==5)return {keyEvent(key::Home)};
        if(f==6)return {keyEvent(key::Down),keyEvent(key::Return)};
        if(f==7)return {keyEvent(key::Return)};
        if(f==9)return {textEvent("AAAA"),keyEvent(key::Return)};
        if(f==11)return {keyEvent(key::F1)};
        if(f==12)return {keyEvent(key::Return)};
        if(f==13)return {textEvent("AAAAA"),keyEvent(key::Return)}; // only Sam remains: finished-player notice, then back to Sam
        if(f==15)return {keyEvent(key::F1)};
        if(f==16)return {keyEvent('1')};
        if(f==18)return {keyEvent(key::F1+2)};
        if(f==19)return {keyEvent('l')};
        return {};
    };
    auto engine=findEngine("boggle")->create(sys,t);CHECK(!engine->run());CHECK(sys.hashes.size()==19 && !sys.textEnabled);
    HighScores scores;CHECK(!scores.load(t.savePath,t.path));auto rows=scores.table();
    CHECK(rows.size()==2 && rows[0].name=="Sam" && rows[0].score==3);
    // Three/four players rotate in roster order and skip participants who end.
    for(size_t count:{size_t(3),size_t(4)}) {
        auto many=target(tmp / std::to_string(count));fixtures::ScriptedSystem gameSys;
        gameSys.clock=[](size_t){return uint64_t(0);};
        gameSys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==count*2){Event quit;quit.type=EventType::Quit;return {quit};}
            if(f%2==1)return {keyEvent(key::F1)};
            return {keyEvent(key::Return)};
        };
        std::vector<std::string> names(count,"Same Name");ClassicGame game(gameSys,many,false,names);CHECK(!game.run());CHECK(gameSys.hashes.size()==count*2);
    }
}
}
static const test::Suite suite("boggle.unit.chess",{
    {"mouse-only manual pause survives focus changes; humans and visible/background computers freeze",[](const auto& tmp){manualPause(tmp);}},
    {"minimum-length notice freezes time, consumes acknowledgment and preserves the Chess player",[](const auto& tmp){shortNotice(tmp);}},
    {"focus freezes foreground, background and opening; queued input and repeated notifications",[](const auto& tmp){focusLoss(tmp);}},
    {"deadline crossed during submission cannot bypass deferred review or corrupt handoff",[](const auto& tmp){submissionBoundary(tmp);}},
    {"initial opening excluded from budgets, final closing, one-ms resume and solo Chess",[](const auto& tmp){animationAndSolo(tmp);}},
    {"per-player active clocks, pause, rollback, limits and mode parsing",[](const auto&){clocks();}},
    {"word rotation, validation, unknown review, results, replay and new board in both variants/renderers",[](const auto& tmp){rotation(tmp);}},
    {"independent budgets, frozen handoff and exact expiry",[](const auto& tmp){budget(tmp);}},
    {"frontend mode selection, remaining player, retired-slot skipping and larger rosters",[](const auto& tmp){rosterAndSkip(tmp);}},
});

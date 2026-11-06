// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/computer.h"
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/highscores.h"
#include "engines/boggle/classic/player.h"
#include "engines/registry.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <algorithm>
#include <limits>
using namespace reluva;
using namespace reluva::boggle;
namespace {
std::vector<uint8_t> dictionaryBytes(const std::vector<std::string>& words) {
    std::vector<uint8_t> data(12);test::fixtures::put32(data,4,1);
    for(const auto& word:words){data.insert(data.end(),word.begin(),word.end());data.push_back(0);}
    data.push_back(0);test::fixtures::put32(data,8,uint32_t(data.size()-12));return data;
}
Dictionary dictionary(const std::vector<std::string>& words) {
    Dictionary d;CHECK(!d.read(dictionaryBytes(words)));return d;
}
void planning() {
    CHECK(computerName(ComputerLevel::Human).empty());CHECK(computerName(ComputerLevel::MrBoggle)=="CPU Mr. Boggle");
    for(bool master:{false,true}) {
        Board b;std::vector<std::string> faces(master?25:16,"A");faces[0]="QU";CHECK(!b.set(master?5:4,faces));
        std::vector<std::string> words={"AA","AAA","AAAA","AAAAA","AAAAAA","AAAAAAA","AAAAAAAA","QUAA","QUAAA","QUAAAA","QUAAAAA","QUAAAAAA","QUAAAAAAA","ZZZZ"};
        auto vocab=dictionary(words);words.erase(std::find(words.begin(),words.end(),"QUAAAAAAA"));auto english=dictionary(words);
        std::vector<std::string> previous;
        for(unsigned level=0;level<4;++level) {
            ComputerPlan p,repeat;CHECK(!p.build(b,vocab,english,ComputerLevel(level),6551,0));
            CHECK(!repeat.build(b,vocab,english,ComputerLevel(level),6551,0));
            std::vector<std::string> current;
            for(const auto& w:p.words()) {
                CHECK(english.contains(w.word) && b.matches(w.word,w.path));CHECK(w.word.size()>=(master?4:3));
                CHECK(w.word!="QUAAAAAAA" && w.word!="ZZZZ");current.push_back(w.word);
                ClassicRound round(english);CHECK(!round.board(b));CHECK(!round.start(0));CHECK(round.submit(w.word,0,w.path)==Submission::Accepted);
            }
            CHECK(!current.empty());for(const auto& word:previous)CHECK(std::find(current.begin(),current.end(),word)!=current.end());
            previous=current;
            const uint64_t ms[]={6000,4000,2000,1000};const auto interval=ms[level];
            CHECK(!p.take(0) && !p.take(interval-1));size_t count=0;
            for(uint64_t now=interval;!p.exhausted();now+=interval) {
                const auto* word=p.take(now);const auto* other=repeat.take(now);CHECK(word && other && word->word==other->word);++count;
                CHECK(!p.take(now) && !p.take(now-1)); // no burst or backwards-clock refund
            }
            CHECK(count==current.size() && !p.take(std::numeric_limits<uint64_t>::max()));
            CHECK(!p.build(b,vocab,english,ComputerLevel(level),6551,0));CHECK(!p.take(interval-1) && p.take(interval));
        }
        CHECK(previous.size()==(master?10:11));
        ComputerPlan bad;CHECK(bool(bad.build(b,vocab,english,ComputerLevel::Human,0,0)) && bad.exhausted());
        CHECK(bool(bad.build(b,vocab,english,ComputerLevel(4),0,0)));CHECK(bool(bad.build(b,vocab,english,ComputerLevel::Beginner,0,4)));
        CHECK(bool(bad.build(b,vocab,english,ComputerLevel::MrBoggle,0,0,0)) && bad.words().empty());
        CHECK(!bad.build(b,dictionary({"ZZZZ"}),english,ComputerLevel::MrBoggle,0,0) && bad.exhausted());
        CHECK(!bad.build(b,vocab,english,ComputerLevel::MrBoggle,0,0));
        CHECK(bad.take(100000) && !bad.take(100000) && !bad.take(100999) && bad.take(101000));
    }
}
GameTarget target(const std::filesystem::path& root,bool chess=false,bool art=false) {
    fixtures::artworkInstall(root/"game");GameTarget t;t.path=root/"game";t.savePath=root/"save";
    fixtures::write(t.path/"Dicts/AI.dic",dictionaryBytes({"AAA","AAAA","ZZZZ"}));
    fixtures::write(t.path/"Dicts/English.dic",dictionaryBytes({"AAA","AAAA","AAAAA","AAAAAA"}));
    t.settings={{"classic_seed","6551"},{"classic_seconds","0"},{"classic_turns",chess?"chess":"traditional"},
                {"classic_sound","0"},{"classic_animations","0"},{"classic_artwork",art?"1":"0"},{"frontend_movies","0"}};return t;
}
void live(const std::filesystem::path& tmp) {
    for(bool chess:{false,true})for(bool art:{false,true}) {
        std::vector<std::string> baseline;
        for(bool inject:{false,true}) {
            auto t=target(tmp/(std::to_string(chess)+std::to_string(art)+std::to_string(inject)),chess,art);
            fixtures::ScriptedSystem sys;sys.clock=[](size_t f){return uint64_t(f*1000);};
            sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
                if(f==0)return {keyEvent(key::Return)};
                if(!chess) {
                    if(f==1)return {textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAA"),keyEvent(key::Return),keyEvent(key::F1)};
                    if(inject && (f==3 || f==4))return {textEvent("AAAAAA"),keyEvent(key::Return),mouseEvent(EventType::MouseDown,30,110)};
                    if(f==7)return {keyEvent('2')};
                    if(f==8)return {keyEvent(key::Escape)};
                } else {
                    if(f==1)return {textEvent("AAAAA"),keyEvent(key::Return)};
                    if(inject && (f==2 || f==6))return {textEvent("AAAAAA"),keyEvent(key::Return)};
                    if(f==4 || f==8)return {keyEvent(key::Return)};
                    if(f==5)return {textEvent("AAAA"),keyEvent(key::Return)};
                    if(f==9)return {keyEvent(key::F1)};
                    if(f==11)return {keyEvent('2')};
                    if(f==12)return {keyEvent(key::Escape)};
                }
                return {};
            };
            // Same display name, different kinds; the human's positive unique word is saved.
            ClassicGame game(sys,t,false,{"CPU Mr. Boggle","CPU Mr. Boggle"},{ComputerLevel::Human,ComputerLevel::MrBoggle});
            CHECK(!game.run() && !sys.textEnabled);CHECK(sys.hashes.size()==(chess?12:8));
            HighScores scores;CHECK(!scores.load(t.savePath,t.path));auto rows=scores.table();
            CHECK(rows.size()==1 && rows[0].name=="CPU Mr. Boggle" && rows[0].score==2);
            if(!inject)baseline=sys.hashes;else CHECK(baseline==sys.hashes);
        }
    }
}
void computerOnly(const std::filesystem::path& tmp) {
    for(bool chess:{false,true})for(bool master:{false,true}) {
        auto t=target(tmp/(std::to_string(chess)+std::to_string(master)),chess);t.settings["classic_variant"]=master?"master":"original";
        fixtures::ScriptedSystem sys;sys.clock=[](size_t f){return uint64_t(f*6000);};
        sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==20){Event quit;quit.type=EventType::Quit;return {quit};}
            return {};
        };
        ClassicGame game(sys,t,false,{"CPU Beginner","CPU Intermediate","CPU Advanced","CPU Mr. Boggle"},
            {ComputerLevel::Beginner,ComputerLevel::Intermediate,ComputerLevel::Advanced,ComputerLevel::MrBoggle});
        CHECK(!game.run() && sys.hashes.size()==20);CHECK(!std::filesystem::exists(t.savePath/"HighScores.txt"));
        // All four finish without a key press after the first start, and replay is available.
        CHECK(sys.hashes[18]==sys.hashes[19]);
    }
}
void timingAndReplay(const std::filesystem::path& tmp) {
    for(bool animations:{false,true}) {
        auto t=target(tmp/std::to_string(animations));t.settings["classic_seconds"]="60";t.settings["classic_animations"]=animations?"1":"0";
        fixtures::ScriptedSystem sys;
        sys.clock=[&](size_t f)->uint64_t {
            if(animations){const uint64_t times[]={0,4400,5400,64400,65500,600000,604400,604401,605501,605501};return times[std::min(f,size_t(9))];}
            const uint64_t times[]={0,1000,60000,600000,600001,600001};return times[std::min(f,size_t(5))];
        };
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==(animations?5:3))return {keyEvent(key::Return)};
            if(f==(animations?7:4))return {textEvent("AAA"),keyEvent(key::Return),textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAA"),keyEvent(key::Return),keyEvent(key::F1)};
            if(f==(animations?9:5)){Event quit;quit.type=EventType::Quit;return {quit};}
            return {};
        };
        ClassicGame game(sys,t,false,{"CPU Mr. Boggle","Alex"},{ComputerLevel::MrBoggle,ComputerLevel::Human});CHECK(!game.run());
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();
        // One CPU word before its exact deadline, one cancels, human gets a fresh full budget after idle handoff/opening.
        CHECK(rows.size()==1 && rows[0].name=="Alex" && rows[0].score==3);
    }
    auto t=target(tmp/"replay");fixtures::ScriptedSystem sys;sys.clock=[](size_t f){return uint64_t(f*1000);};
    sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==5 || f==11)return {keyEvent(key::Return)};
        if(f==10)return {keyEvent(key::F1+1)};
        if(f==16)return {keyEvent(key::Escape)};
        return {};
    };
    ClassicGame game(sys,t,false,{"CPU Mr. Boggle"},{ComputerLevel::MrBoggle});CHECK(!game.run());CHECK(sys.hashes.size()==16);
    if(sys.hashes.size()==16){CHECK(sys.hashes[4]==sys.hashes[9]);CHECK(sys.hashes[0]==sys.hashes[5]);}
    fixtures::ScriptedSystem fresh;fresh.clock=[](size_t f){return uint64_t(f*1000);};
    fresh.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0)return {keyEvent(key::Return)};
        if(f==5)return {keyEvent(key::Escape)};
        return {};
    };
    t.settings["classic_seed"]="6552";
    ClassicGame control(fresh,t,false,{"CPU Mr. Boggle"},{ComputerLevel::MrBoggle});CHECK(!control.run());
    CHECK(fresh.hashes.size()==5);
    if(fresh.hashes.size()==5 && sys.hashes.size()==16)CHECK(fresh.hashes[4]==sys.hashes[15]);
    CHECK(!std::filesystem::exists(t.savePath/"HighScores.txt"));
    auto solo=target(tmp/"solo-chess",true);solo.settings["classic_seconds"]="60";
    fixtures::ScriptedSystem human;human.clock=[](size_t f)->uint64_t {
        const uint64_t times[]={0,1000,60000,600000,600001,600001};return times[std::min(f,size_t(5))];
    };
    human.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==3)return {keyEvent(key::Return)};
        if(f==1)return {textEvent("AAAA"),keyEvent(key::Return)};
        if(f==4)return {textEvent("AAAAA"),keyEvent(key::Return),keyEvent(key::F1)};
        if(f==5)return {keyEvent(key::Escape)};
        return {};
    };
    ClassicGame soloGame(human,solo);CHECK(!soloGame.run());HighScores scores;CHECK(!scores.load(solo.savePath,solo.path));
    const auto rows=scores.table();CHECK(rows.size()==2 && rows[0].score==2 && rows[1].score==1);
}
void guards(const std::filesystem::path& tmp) {
    auto t=target(tmp);std::filesystem::remove(t.path/"Dicts/AI.dic");
    fixtures::ScriptedSystem sys;sys.events=[](size_t)->std::vector<Event>{return {fixtures::keyEvent(key::Escape)};};
    ClassicGame human(sys,t);CHECK(!human.run());
    ClassicGame missing(sys,t,false,{"CPU Beginner"},{ComputerLevel::Beginner});
    CHECK(missing.run().message.find("Re-import")!=std::string::npos);
    for(const auto& kinds:{std::vector<ComputerLevel>{ComputerLevel(4)},std::vector<ComputerLevel>(2,ComputerLevel::Human)}) {
        fixtures::ScriptedSystem bad;ClassicGame game(bad,t,false,{"Alex"},kinds);CHECK(bool(game.run()) && bad.w==0);
    }
}
void frontend(const std::filesystem::path& tmp) {
    auto t=target(tmp);PlayerProfile profile;CHECK(!profile.load(t.savePath,t.path));CHECK(!profile.rename("Alex"));const auto history=profile.history();
    fixtures::ScriptedSystem sys;sys.clock=[](size_t f){return uint64_t(f*2000);};
    sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==1 || f==6 || f==7)return {keyEvent(key::Return)};
        if(f==2)return {keyEvent('m')};
        if(f==3)return {keyEvent('1')};
        if(f==4)return {keyEvent(key::End),keyEvent(key::Return)}; // primary slot can be Mr. Boggle, no profile rename
        if(f==5)return {keyEvent(key::Return)};
        if(f==12)return {keyEvent(key::F1+2)};
        if(f==13)return {keyEvent('l')};
        return {};
    };
    auto engine=findEngine("boggle")->create(sys,t);CHECK(!engine->run());CHECK(sys.hashes.size()==13);
    CHECK(!profile.load(t.savePath,t.path) && profile.name()=="Alex" && profile.history()==history);
    CHECK(!std::filesystem::exists(t.savePath/"HighScores.txt"));
}
}
static const test::Suite suite("boggle.unit.computer",{
    {"bounded legal plans, Qu, all levels, reproducible ordering, independent pacing and failure clearing",[](const auto&){planning();}},
    {"Traditional and Chess opponents cancel shared words, reject human input and save humans only",[](const auto& tmp){live(tmp);}},
    {"four computers finish both variants and turn modes without human handoff confirmation",[](const auto& tmp){computerOnly(tmp);}},
    {"exact CPU deadlines, opening and handoff exclusions, replay/new-board plan reset",[](const auto& tmp){timingAndReplay(tmp);}},
    {"typed roster guards and older human-only imports",[](const auto& tmp){guards(tmp);}},
    {"primary computer choice does not rename or pollute remembered human profiles",[](const auto& tmp){frontend(tmp);}},
});

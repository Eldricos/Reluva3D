// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/computer.h"
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/highscores.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/classic/player.h"
#include "engines/boggle/classic/user_dictionary.h"
#include "engines/boggle/resources.h"
#include "engines/registry.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <algorithm>
using namespace reluva;
using namespace reluva::boggle;
namespace {
std::vector<uint8_t> dictionaryBytes(const std::vector<std::string>& words) {
    std::vector<uint8_t> data(12);test::fixtures::put32(data,4,1);
    for(const auto& word:words){data.insert(data.end(),word.begin(),word.end());data.push_back(0);}
    data.push_back(0);test::fixtures::put32(data,8,uint32_t(data.size()-12));return data;
}
Event quit() {Event event;event.type=EventType::Quit;return event;}
GameTarget target(const std::filesystem::path& root,bool chess=false,bool master=false,bool art=false) {
    fixtures::artworkInstall(root/"game");GameTarget t;t.path=root/"game";t.savePath=root/"save";
    fixtures::write(t.path/"Dicts/AI.dic",dictionaryBytes({"AAA","AAAA"}));
    fixtures::write(t.path/"Dicts/English.dic",dictionaryBytes({"AAA","AAAA"}));
    UserDictionary user;CHECK(!user.load(t.savePath,t.path));CHECK(!user.add({"AAAAA","AAAAAAA"}));
    t.settings={{"classic_seed","6551"},{"classic_seconds","0"},{"classic_turns",chess?"chess":"traditional"},
        {"classic_variant",master?"master":"original"},{"classic_background","1"},{"classic_sound","0"},
        {"classic_animations","0"},{"classic_artwork",art?"1":"0"},{"frontend_movies","0"}};return t;
}
void scheduled() {
    Board board;CHECK(!board.set(4,std::vector<std::string>(16,"A")));
    Dictionary dictionary;CHECK(!dictionary.read(dictionaryBytes({"AAA","AAAA","AAAAA","AAAAAA"})));
    std::vector<std::pair<std::string,uint64_t>> baseline;
    for(uint64_t step:{100u,777u,10000u}) {
        ComputerPlan plan;CHECK(!plan.build(board,dictionary,dictionary,ComputerLevel::MrBoggle,6551,0));
        std::vector<std::pair<std::string,uint64_t>> moves;uint64_t at=123;
        CHECK(!plan.takeScheduled(999,at) && at==123);
        for(uint64_t now=step;now<=10000+step;now+=step)
            while(const auto* word=plan.takeScheduled(now,at))moves.push_back({word->word,at});
        CHECK(plan.exhausted() && moves.size()==4);
        for(size_t i=0;i<moves.size();++i)CHECK(moves[i].second==(i+1)*1000);
        if(baseline.empty())baseline=moves;else CHECK(baseline==moves);
    }
    ComputerPlan plan;CHECK(!plan.build(board,dictionary,dictionary,ComputerLevel::MrBoggle,6551,0));
    ClassicRound round(dictionary);CHECK(!round.board(board,3000) && !round.start(0,3000));uint64_t at=0;
    while(const auto* word=plan.takeScheduled(2999,at))CHECK(round.submit(word->word,at,word->path)==Submission::Accepted);
    CHECK(round.words().size()==2);
    const auto* word=plan.takeScheduled(3000,at);CHECK(word && at==3000);
    if(word)CHECK(round.submit(word->word,at,word->path)==Submission::NotPlaying);
}
void hidden(const std::filesystem::path& tmp) {
    for(bool chess:{false,true})for(bool master:{false,true})for(bool art:{false,true}) {
        const auto root=tmp/(std::to_string(chess)+std::to_string(master)+std::to_string(art));
        auto t=target(root,chess,master,art);t.settings["classic_sound"]="1";
        fixtures::ScriptedSystem sys;sys.audioSupported=true;sys.audioRequiresGraphics=true;
        std::vector<std::string> bodies,cpuScores;
        sys.present=[&](const Surface& screen,size_t) {
            bodies.push_back(test::Hash().bytes(screen.pixels.data(),size_t(screen.pitch)*590).hex());
            test::Hash score;
            for(int y=590;y<600;++y)score.bytes(screen.row(y)+204*4,196*4);
            cpuScores.push_back(score.hex());
        };
        sys.clock=[](size_t f){return uint64_t(f*1000);};
        sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAA"),keyEvent(key::Return)};
            if(f==2)return {textEvent("AAAAAA")}; // unfinished input disappears on manual end
            if(f==3)return {keyEvent(key::F1)};
            if(f==4)return {keyEvent('2')}; // result summary first, then computer word list
            if(f==5)return {keyEvent(key::Up)};
            if(f==6)return {keyEvent('a')};
            if(f==7)return {quit()};
            return {};
        };
        ClassicGame game(sys,t,false,{"Alex","CPU Mr. Boggle"},{ComputerLevel::Human,ComputerLevel::MrBoggle});
        CHECK(!game.run() && sys.hashes.size()==7 && !sys.textEnabled && sys.audioStopped);
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();
        CHECK(rows.size()==1 && rows[0].name=="Alex" && rows[0].score==2);
        if(sys.hashes.size()==7)CHECK(sys.hashes[3]!=sys.hashes[4] && sys.hashes[4]!=sys.hashes[5] && sys.hashes[5]!=sys.hashes[6]);
        auto solo=target(root/"control",chess,master,art);solo.settings["classic_sound"]="1";
        fixtures::ScriptedSystem control;control.audioSupported=true;control.audioRequiresGraphics=true;control.clock=sys.clock;
        std::vector<std::string> controlBodies;
        control.present=[&](const Surface& screen,size_t){controlBodies.push_back(test::Hash().bytes(screen.pixels.data(),size_t(screen.pitch)*590).hex());};
        control.events=[&](size_t f){return f==3?std::vector<Event>{quit()}:sys.events(f);};
        ClassicGame human(control,solo,false,{"Alex"});CHECK(!human.run());CHECK(control.hashes.size()==3);
        CHECK(cpuScores.size()==7 && cpuScores[0]!=cpuScores[1]); // computer totals rise visibly during human play
        for(size_t i=0;i<control.hashes.size();++i) {
            CHECK(controlBodies[i]==bodies[i]); // computer words/cursor stay hidden above the score strip
            CHECK(control.audioHashes[i]==sys.audioHashes[i]); // hidden words produce no writing/7/8 cues
        }
    }
}
void deadline(const std::filesystem::path& tmp) {
    for(bool chess:{false,true})for(bool animations:{false,true}) {
        auto t=target(tmp/(std::to_string(chess)+std::to_string(animations)),chess);
        t.settings["classic_seconds"]="60";t.settings["classic_animations"]=animations?"1":"0";
        // Invented A/B vocabulary; enough legal words to put one exactly at the deadline.
        std::vector<std::string> words;
        for(size_t length=3;length<=6;++length)for(unsigned bits=0;bits<(1u<<length);++bits) {
            std::string word(length,'A');for(size_t i=0;i<length;++i)if(bits&(1u<<i))word[i]='B';words.push_back(word);
        }
        fixtures::write(t.path/"Dicts/AI.dic",dictionaryBytes(words));
        fixtures::write(t.path/"Dicts/English.dic",dictionaryBytes(words));
        std::string dice;for(size_t i=0;i<16;++i)dice+=i%2?"B,B,B,B,B,B\n":"A,A,A,A,A,A\n";
        fixtures::write(t.path/"Dicts/EngOrg.dst",dice);
        Resources resources;CHECK(!resources.open(t.path));Dictionary dictionary;DiceSet cubes;Board board;
        CHECK(!resources.dictionary("Dicts/English.dic",dictionary));CHECK(!resources.diceSet("Dicts/EngOrg.dst",cubes));CHECK(!Board::generate(cubes,6551,board));
        ComputerPlan plan;CHECK(!plan.build(board,dictionary,dictionary,ComputerLevel::MrBoggle,6551,1));
        CHECK(plan.words().size()>60);if(plan.words().size()<=60)continue;
        const auto last=plan.words()[59];
        fixtures::ScriptedSystem sys;
        sys.clock=[&](size_t f)->uint64_t {
            if(animations){const uint64_t times[]={0,4400,5400,64400,65500,65500};return times[std::min(f,size_t(5))];}
            const uint64_t times[]={0,1000,60000,60000};return times[std::min(f,size_t(3))];
        };
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==(animations?2:1))return {textEvent(last.word),keyEvent(key::Return)};
            if(f==(animations?5:3))return {quit()};
            return {};
        };
        ClassicGame game(sys,t,false,{"Alex","CPU Mr. Boggle"},{ComputerLevel::Human,ComputerLevel::MrBoggle});CHECK(!game.run());
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();
        CHECK(rows.size()==1 && rows[0].name=="Alex" && rows[0].score==last.points);
    }
}
void paused(const std::filesystem::path& tmp) {
    for(bool chess:{false,true}) {
        auto t=target(tmp/std::to_string(chess),chess);fixtures::ScriptedSystem sys;
        std::vector<std::string> cpuScores;
        sys.present=[&](const Surface& screen,size_t) {
            test::Hash score;for(int y=590;y<600;++y)score.bytes(screen.row(y),196*4);cpuScores.push_back(score.hex());
        };
        sys.clock=[&](size_t f)->uint64_t {
            if(chess){const uint64_t times[]={1000,1499,1000000,1000500,2000000,2000000,3000000,3000000,4000000,4000000,5000000,5000000};return times[std::min(f,size_t(11))];}
            const uint64_t times[]={1000,1499,1000000,2000000,2000500,2000500};return times[std::min(f,size_t(5))];
        };
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(chess) {
                if(f==1)return {textEvent("AAAA"),keyEvent(key::Return)};
                if(f==2 || f==4 || f==6 || f==8)return {keyEvent(key::Return)};
                if(f==3)return {textEvent("AAAAAAA"),keyEvent(key::Return)};
                if(f==5)return {textEvent("AAAAAA"),keyEvent(key::Return)};
                if(f==7 || f==9)return {keyEvent(key::F1)};
                if(f==10)return {keyEvent('1')};
                if(f==11)return {quit()};
            } else {
                if(f==1)return {textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAA"),keyEvent(key::Return),textEvent("AAAAAA"),keyEvent(key::Return),keyEvent(key::F1)};
                if(f==2)return {keyEvent('1')};
                if(f==3)return {keyEvent(key::Return)};
                if(f==4)return {textEvent("AAAAAAA"),keyEvent(key::Return),keyEvent(key::F1)};
                if(f==5)return {quit()};
            }
            return {};
        };
        // CPU slots before/between humans must never receive a visible turn or a handoff notice.
        ClassicGame game(sys,t,false,{"CPU Mr. Boggle","First","CPU Mr. Boggle","Second"},
            {ComputerLevel::MrBoggle,ComputerLevel::Human,ComputerLevel::MrBoggle,ComputerLevel::Human});
        CHECK(!game.run() && sys.hashes.size()==(chess?11:5));
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();
        CHECK(rows.size()==2);
        CHECK(cpuScores.size()>=3 && cpuScores[1]!=cpuScores[2]); // continues through the paused human switch/review
        CHECK(std::any_of(rows.begin(),rows.end(),[&](const auto& row){return row.name=="First" && row.score==(chess?3:5);}));
        CHECK(std::any_of(rows.begin(),rows.end(),[](const auto& row){return row.name=="Second" && row.score==5;}));
    }
}
void earlyEnd(const std::filesystem::path& tmp) {
    for(bool chess:{false,true})for(bool timed:{false,true})for(bool master:{false,true}) {
        auto t=target(tmp/(std::to_string(chess)+std::to_string(timed)+std::to_string(master)),chess,master);
        t.settings["classic_seconds"]=timed?"60":"0";
        fixtures::ScriptedSystem sys;sys.clock=[](size_t f)->uint64_t {
            const uint64_t times[]={0,500,1000000,2000000};return times[std::min(f,size_t(3))];
        };
        sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAAA"),keyEvent(key::Return),keyEvent(key::F1)};
            if(f==2)return {keyEvent('1')};
            if(f==3)return {quit()};
            return {};
        };
        ClassicGame game(sys,t,false,{"Alex","CPU Mr. Boggle"},{ComputerLevel::Human,ComputerLevel::MrBoggle});CHECK(!game.run());
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();
        // Timed CPU completes its entire budget; untimed CPU stops at 500 ms,
        // without gaining words during the subsequent dictionary-review wait.
        CHECK(rows.size()==1 && rows[0].name=="Alex" && rows[0].score==(timed?3:4));
    }
}
void replay(const std::filesystem::path& tmp) {
    for(bool chess:{false,true}) {
        auto t=target(tmp/std::to_string(chess),chess);fixtures::ScriptedSystem sys;
        sys.clock=[](size_t f){return uint64_t(f*1000);};
        sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==5 || f==6 || f==12)return {keyEvent(key::Return)};
            if(f==1 || f==7 || f==13)return {textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAA"),keyEvent(key::Return)};
            if(f==3 || f==9 || f==15)return {keyEvent(key::F1)};
            if(f==4 || f==10 || f==16)return {keyEvent('1')};
            if(f==11)return {keyEvent(key::F1+1)};
            if(f==17)return {quit()};
            return {};
        };
        ClassicGame game(sys,t,false,{"Alex","CPU Mr. Boggle"},{ComputerLevel::Human,ComputerLevel::MrBoggle});CHECK(!game.run());CHECK(sys.hashes.size()==17);
        if(sys.hashes.size()==17)CHECK(sys.hashes[4]==sys.hashes[10]);
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();
        CHECK(rows.size()==3 && std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.name=="Alex" && row.score==2;}));
    }
}
void frontend(const std::filesystem::path& tmp) {
    for(bool click:{false,true}) {
        auto t=target(tmp/std::to_string(click));t.settings["classic_background"]="0";
        fixtures::ScriptedSystem sys;sys.clock=[](size_t f){return uint64_t(f*1000);};
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==1 || f==7 || f==13)return {keyEvent(key::Return)};
            if(f==2)return {keyEvent('m')};
            if(f==3)return {keyEvent('2')};
            if(f==4)return {keyEvent(key::End),keyEvent(key::Up),keyEvent(key::Return)};
            if(f==5)return {keyEvent(key::Return)};
            if(f==6)return {click?mouseEvent(EventType::MouseDown,320,236):keyEvent('b')};
            if(f==9)return {textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAA"),keyEvent(key::Return)};
            if(f==11)return {keyEvent(key::F1)};
            if(f==12)return {keyEvent('1')};
            if(f==14)return {keyEvent(key::F1+2)};
            if(f==15)return {keyEvent('l')};
            return {};
        };
        auto engine=findEngine("boggle")->create(sys,t);CHECK(!engine->run() && sys.hashes.size()==15);
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();
        CHECK(rows.size()==1 && rows[0].score==2);
    }
    // A background request cannot strand a roster with no human timer to drive it.
    auto t=target(tmp/"all-computer");fixtures::ScriptedSystem sys;
    sys.clock=[](size_t f){return uint64_t(f*1000);};
    sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0)return {keyEvent(key::Return)};
        if(f==5)return {quit()};
        return {};
    };
    ClassicGame game(sys,t,false,{"CPU Mr. Boggle"},{ComputerLevel::MrBoggle});CHECK(!game.run() && sys.hashes.size()==5);
    CHECK(!std::filesystem::exists(t.savePath/"HighScores.txt"));
    ClassicOptions options;CHECK(!readClassicOptions({},options) && !options.background);
    for(const auto* value:{"0","1"}){CHECK(!readClassicOptions({{"classic_background",value}},options));CHECK(options.background==(std::string(value)=="1"));}
    for(const auto* value:{"","2","true","01"," 1"})CHECK(bool(readClassicOptions({{"classic_background",value}},options)) && !options.background);
}
}
static const test::Suite suite("boggle.unit.background",{
    {"hidden schedules are frame-rate independent and obey exact round deadlines",[](const auto&){scheduled();}},
    {"hidden words stay silent while live scores rise; final cancellation and review work",[](const auto& tmp){hidden(tmp);}},
    {"computer words at the exact deadline are excluded, including after lid opening",[](const auto& tmp){deadline(tmp);}},
    {"computers continue through human switches/review without entering the turn rotation",[](const auto& tmp){paused(tmp);}},
    {"early timed endings complete CPU budgets; untimed endings stop before dictionary review",[](const auto& tmp){earlyEnd(tmp);}},
    {"replay and New Board reset background words and clocks",[](const auto& tmp){replay(tmp);}},
    {"Players keyboard/mouse switch, setting validation and all-computer fallback",[](const auto& tmp){frontend(tmp);}},
});

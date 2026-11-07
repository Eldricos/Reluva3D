// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/computer.h"
#include "engines/boggle/classic/game.h"
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
            const auto interval=p.due();CHECK(interval>0);
            CHECK(!p.take(0) && !p.take(interval-1));size_t count=0;
            while(!p.exhausted()) {
                const auto now=p.due();CHECK(now==repeat.due());
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
        CHECK(!bad.build(b,vocab,dictionary({"ZZZZ"}),ComputerLevel::MrBoggle,0,0) && bad.exhausted());
        CHECK(!bad.build(b,vocab,english,ComputerLevel::MrBoggle,0,0));
        CHECK(bad.take(100000) && !bad.take(100000) && !bad.take(100999) && bad.take(101000));
    }
}
void thinkingTimes() {
    // Numeric bounds observed by executing the original base-word timing phase.
    const uint64_t firstLow[]={1500,1200,1000},firstHigh[]={3499,2899,1999};
    const uint64_t secondLow[]={1650,1250,1050},secondHigh[]={4349,3449,2349};
    const uint64_t lastLow[]={25500,13200,5000},lastHigh[]={27499,14899,5999};
    for(bool master:{false,true}) {
        Board small;CHECK(!small.set(master?5:4,std::vector<std::string>(master?25:16,"A")));
        const auto one=dictionary({master?"AAAA":"AAA"});
        Board rich;std::vector<std::string> faces(master?25:16);
        for(size_t i=0;i<faces.size();++i)faces[i]=i%2?"B":"A";
        CHECK(!rich.set(master?5:4,faces));
        std::vector<std::string> words;
        for(size_t length=master?4:3;length<=6;++length)for(unsigned bits=0;bits<(1u<<length);++bits) {
            std::string word(length,'A');for(size_t i=0;i<length;++i)if(bits&(1u<<i))word[i]='B';words.push_back(word);
        }
        const auto many=dictionary(words);
        for(unsigned level=0;level<3;++level) {
            std::vector<uint64_t> seen;
            for(uint32_t seed=0;seed<16;++seed) {
                ComputerPlan isolated;CHECK(!isolated.build(small,one,one,ComputerLevel(level),seed,0));
                if(isolated.exhausted()){CHECK(isolated.due()==0);continue;}
                CHECK(isolated.due()>=lastLow[level] && isolated.due()<=lastHigh[level]);seen.push_back(isolated.due());
                CHECK(!isolated.take(isolated.due()-1));CHECK(isolated.take(isolated.due()) && isolated.exhausted());
                CHECK(isolated.due()==0);
            }
            CHECK(seen.size()>1 && std::any_of(seen.begin(),seen.end(),[&](auto value){return value!=seen.front();}));
            ComputerPlan plan;CHECK(!plan.build(rich,many,many,ComputerLevel(level),6551,0));
            CHECK(plan.words().size()>10);CHECK(plan.due()>=firstLow[level] && plan.due()<=firstHigh[level]);
            const auto first=plan.due();CHECK(plan.take(first));
            CHECK(plan.due()-first>=secondLow[level] && plan.due()-first<=secondHigh[level]);
            // A stalled visible window emits once and retains the next full delay.
            const auto secondDelay=plan.due()-first;
            ComputerPlan delayed;CHECK(!delayed.build(rich,many,many,ComputerLevel(level),6551,0));
            CHECK(delayed.take(1000000) && delayed.due()==1000000+secondDelay);
            CHECK(!delayed.take(1000000) && !delayed.take(999999));
            CHECK(!delayed.build(rich,many,many,ComputerLevel(level),6551,0) && delayed.due()==first);
            // Saturation must not wrap a long-running active clock to zero.
            const auto near=std::numeric_limits<uint64_t>::max()-1;
            CHECK(delayed.take(near) && delayed.due()==std::numeric_limits<uint64_t>::max());
            CHECK(!delayed.take(near));CHECK(delayed.take(std::numeric_limits<uint64_t>::max()));
            CHECK(!delayed.take(std::numeric_limits<uint64_t>::max()));uint64_t untouched=123;
            CHECK(!delayed.takeScheduled(std::numeric_limits<uint64_t>::max(),untouched) && untouched==123);
            CHECK(bool(delayed.build(rich,many,many,ComputerLevel(level),6551,0,0)));
            CHECK(delayed.exhausted() && delayed.due()==0 && !delayed.nextWord());
            // Hidden scheduling must produce identical words/times with sparse servicing.
            std::vector<std::pair<std::string,uint64_t>> baseline;
            for(uint64_t step:{997u,100000u}) {
                ComputerPlan hidden;CHECK(!hidden.build(rich,many,many,ComputerLevel(level),6551,0));
                std::vector<std::pair<std::string,uint64_t>> moves;
                for(uint64_t now=0;!hidden.exhausted();now+=step) {
                    uint64_t at=0;while(const auto* word=hidden.takeScheduled(now,at))moves.push_back({word->word,at});
                }
                CHECK(moves.size()==plan.words().size());
                if(baseline.empty())baseline=moves;else CHECK(moves==baseline);
            }
        }
    }
}
GameTarget target(const std::filesystem::path& root,bool chess=false,bool art=false) {
    fixtures::artworkInstall(root/"game");GameTarget t;t.path=root/"game";t.savePath=root/"save";
    fixtures::write(t.path/"Dicts/AI.dic",dictionaryBytes({"AAA","AAAA","ZZZZ"}));
    fixtures::write(t.path/"Dicts/English.dic",dictionaryBytes({"AAA","AAAA"}));
    UserDictionary user;CHECK(!user.load(t.savePath,t.path));CHECK(!user.add({"AAAAA","AAAAAA"}));
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
void vocabulary() {
    CHECK(!usesCommonVocabulary(ComputerLevel::Human) && !usesCommonVocabulary(ComputerLevel::MrBoggle));
    CHECK(!usesCommonVocabulary(ComputerLevel(4)));
    for(bool master:{false,true}) {
        Board board;std::vector<std::string> faces(master?25:16,"A");faces.back()="QU";CHECK(!board.set(master?5:4,faces));
        const std::string commonLong(9,'A'),tooLong(10,'A'),exclusive(12,'A'),qu=std::string(8,'A')+"QU";
        const auto english=dictionary({"AAA","AAAA",commonLong,tooLong,exclusive,qu});
        const auto common=dictionary({"AAA","AAAA",commonLong,tooLong,qu,"ZZZZ"});
        for(auto level:{ComputerLevel::Beginner,ComputerLevel::Intermediate,ComputerLevel::Advanced}) {
            CHECK(usesCommonVocabulary(level));bool sawLong=false,sawQu=false;
            for(uint32_t seed=0;seed<32;++seed) {
                ComputerPlan plan;CHECK(!plan.build(board,common,english,level,seed,0));
                for(const auto& word:plan.words()) {
                    CHECK(common.contains(word.word) && english.contains(word.word) && board.matches(word.word,word.path));
                    CHECK(word.word!=tooLong && word.word!=exclusive);
                    sawLong|=word.word==commonLong;sawQu|=word.word==qu;
                }
            }
            CHECK(sawLong && sawQu); // a common opponent is not restricted to the former minimum+N caps
        }
        ComputerPlan mr;Dictionary absentAuxiliary;
        CHECK(!mr.build(board,absentAuxiliary,english,ComputerLevel::MrBoggle,6551,0));
        CHECK(std::any_of(mr.words().begin(),mr.words().end(),[&](const auto& w){return w.word==exclusive;}));
        CHECK(std::any_of(mr.words().begin(),mr.words().end(),[&](const auto& w){return w.word==tooLong;}));
        CHECK(mr.words().size()==(master?5:6));
    }
}
void mrBehavior(const std::filesystem::path& tmp) {
    Board board;CHECK(!board.set(4,std::vector<std::string>(16,"A")));
    std::vector<std::string> list;for(size_t length=3;length<=12;++length)list.push_back(std::string(length,'A'));
    const auto english=dictionary(list);ComputerPlan mr;Dictionary auxiliary;
    CHECK(!mr.build(board,auxiliary,english,ComputerLevel::MrBoggle,6551,0));CHECK(mr.words().size()==10);
    for(size_t i=1;i<mr.words().size();++i)CHECK(mr.words()[i-1].word.size()>mr.words()[i].word.size());
    class System : public fixtures::ScriptedSystem {
    public:bool visible=true;void setMouseCursorVisible(bool value) override {visible=value;}
    };
    for(bool chess:{false,true})for(bool timed:{false,true})for(bool master:{false,true}) {
        auto t=target(tmp/(std::to_string(chess)+std::to_string(timed)+std::to_string(master)));t.settings["classic_turns"]=chess?"chess":"traditional";
        t.settings["classic_seconds"]=timed?"60":"0";t.settings["classic_variant"]=master?"master":"original";t.settings["classic_cursor"]="1";
        System sys;sys.clock=[](size_t f){return uint64_t(f*1000);};
        sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==4){Event stop;stop.type=EventType::Quit;return {stop};}
            return {};
        };
        sys.present=[&](const Surface&,size_t frame){if(frame==3)CHECK(sys.visible);}; // already ended, without an extra wordless turn or timer wait
        ClassicGame game(sys,t,false,{"CPU Mr. Boggle"},{ComputerLevel::MrBoggle});CHECK(!game.run() && sys.visible && sys.hashes.size()==4);
        CHECK(!std::filesystem::exists(t.savePath/"HighScores.txt"));
    }
}
void guards(const std::filesystem::path& tmp) {
    auto t=target(tmp);std::filesystem::remove(t.path/"Dicts/AI.dic");
    fixtures::ScriptedSystem sys;sys.events=[](size_t)->std::vector<Event>{return {fixtures::keyEvent(key::Escape)};};
    ClassicGame human(sys,t);CHECK(!human.run());
    fixtures::ScriptedSystem mr;mr.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0)return {keyEvent(key::Return)};
        if(f==5){Event stop;stop.type=EventType::Quit;return {stop};}
        return {};
    };mr.clock=[](size_t f){return uint64_t(f*1000);};
    ClassicGame mainOnly(mr,t,false,{"CPU Mr. Boggle"},{ComputerLevel::MrBoggle});CHECK(!mainOnly.run() && mr.hashes.size()==5);
    fixtures::write(t.path/"Dicts/AI.dic","invalid auxiliary dictionary");
    fixtures::ScriptedSystem ignored;ignored.events=mr.events;ignored.clock=mr.clock;
    ClassicGame noAuxiliaryRead(ignored,t,false,{"CPU Mr. Boggle"},{ComputerLevel::MrBoggle});CHECK(!noAuxiliaryRead.run());
    std::filesystem::remove(t.path/"Dicts/AI.dic");
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
    {"measured common base-word timing bounds, scarcity, progression and frame-independent replay",[](const auto&){thinkingTimes();}},
    {"Traditional and Chess opponents cancel shared words, reject human input and save humans only",[](const auto& tmp){live(tmp);}},
    {"four computers finish both variants and turn modes without human handoff confirmation",[](const auto& tmp){computerOnly(tmp);}},
    {"exact CPU deadlines, opening and handoff exclusions, replay/new-board plan reset",[](const auto& tmp){timingAndReplay(tmp);}},
    {"Classic common dictionary/search boundary versus full English Mr. Boggle",[](const auto&){vocabulary();}},
    {"Mr. Boggle searches longest first and ends timed/untimed turns on exhaustion",[](const auto& tmp){mrBehavior(tmp);}},
    {"typed roster guards and older human-only imports",[](const auto& tmp){guards(tmp);}},
    {"primary computer choice does not rename or pollute remembered human profiles",[](const auto& tmp){frontend(tmp);}},
});

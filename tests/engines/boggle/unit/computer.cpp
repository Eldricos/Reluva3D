// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/computer.h"
#include "engines/boggle/classic/ai_policy.h"
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
        Board board;std::vector<std::string> faces(master?25:16,"A");faces[0]="QU";CHECK(!board.set(master?5:4,faces));
        std::vector<std::string> entries={"AAA","AAAA","AAAAA","AAAAAA","AAAAAAA","AAAAAAAA","AAAAAAAAA","QUAA","QUAAA","QUAAAA"};
        const auto words=dictionary(entries);
        for(unsigned level=0;level<4;++level)for(uint32_t seed=0;seed<16;++seed) {
            ComputerPlan plan,repeat;CHECK(!plan.build(board,words,words,ComputerLevel(level),seed,0));
            CHECK(!repeat.build(board,words,words,ComputerLevel(level),seed,0));
            size_t count=0;uint64_t last=0;
            while(!plan.exhausted()) {
                const auto due=plan.due();CHECK(due>last && due==repeat.due());
                CHECK(!plan.take(due-1));const auto* word=plan.take(due);const auto* other=repeat.take(due);
                CHECK(word && other && word->word==other->word && board.matches(word->word,word->path));
                CHECK(!plan.take(due));last=due;++count;
            }
            CHECK(count==plan.words().size() && plan.finished(std::numeric_limits<uint64_t>::max()));
            if(!count)CHECK(plan.finished(0));
            CHECK(!plan.build(board,words,words,ComputerLevel(level),seed,0));CHECK(plan.exhausted()?plan.due()==0:plan.due()>0);
        }
        ComputerPlan bad;CHECK(bool(bad.build(board,words,words,ComputerLevel::Human,0,0)) && bad.exhausted());
        CHECK(bool(bad.build(board,words,words,ComputerLevel(4),0,0)));CHECK(bool(bad.build(board,words,words,ComputerLevel::Beginner,0,4)));
        CHECK(bool(bad.build(board,words,words,ComputerLevel::MrBoggle,0,0,0)) && bad.words().empty());
        CHECK(!bad.build(board,words,words,ComputerLevel::MrBoggle,0,0));
        CHECK(bad.take(100000) && !bad.take(100000));const auto due=bad.due();CHECK(!bad.take(due-1) && bad.take(due));
    }
}
void thinkingTimes() {
    // Full-policy boundaries are observable contracts, not the old nested samples.
    std::vector<AcceptedWord> pool;
    for(const std::string word:{"ZAA","ZAAB","ZAABC","ZAABCD","ZAABCDE","ZAABCDEF","ZAABCDEFG","ZAB","ZABS","ZABES","ZABBED","ZAC","ZACE","ZACED","ZACES","ZAD","ZADS","ZADD","ZADDED","ZADE"})pool.push_back({word,0,{}});
    for(unsigned level=0;level<3;++level) {
        std::vector<AiChoice> choices;
        CHECK(!chooseComputerWords(pool,ComputerLevel(level),[]{return 999u;},choices) && choices.empty());
        CHECK(!chooseComputerWords(pool,ComputerLevel(level),[]{return 0u;},choices));CHECK(!choices.empty());
        bool plural=false,past=false,ending=false,prefix=false;
        for(const auto& choice:choices) {
            CHECK(choice.word<pool.size() && choice.remaining>0);
            plural|=choice.followup && pool[choice.word].word=="ZABS";
            past|=choice.followup && pool[choice.word].word=="ZABBED";
            ending|=!choice.followup && choice.credit==2;
            prefix|=!choice.followup && choice.credit>=3;
        }
        CHECK(plural && past && ending && prefix);
        const std::vector<AcceptedWord> small(pool.begin(),pool.begin()+7);
        CHECK(!chooseComputerWords(small,ComputerLevel(level),[]{return 0u;},choices));
        const size_t expected[]={2,3,7};CHECK(choices.size()==expected[level]);
        CHECK(!chooseComputerWords(small,ComputerLevel(level),[]{return 999u;},choices) && choices.empty());
    }
    std::vector<AiChoice> mr;std::vector<AcceptedWord> ties={{"AAAA",0,{}},{"BBBB",0,{}},{"CCC",0,{}}};
    CHECK(!chooseComputerWords(ties,ComputerLevel::MrBoggle,[]{return 0u;},mr));
    CHECK(mr.size()==3 && mr[0].word==0 && mr[1].word==1 && mr[2].word==2);
    CHECK(bool(chooseComputerWords(pool,ComputerLevel::Human,[]{return 0u;},mr)) && mr.empty());
    // A mixed-suffix reference case distinguishes prefix gate order from
    // initial-group gate order. Both the full result and draw count are observed.
    const std::vector<std::string> mixed={"ZABY","ZABYS","ZABYES","ZABIES","ZABIED","ZABE","ZABED","ZABEES","ZAE","ZAED","ZAB","ZABS","ZABES","ZABBED","ZAC","ZACS","ZACCED","ZAX","ZAXS","ZAXXED","QUAB","QUABS","QUABBED","QUABES","ZAABCDEFQU"};
    pool.clear();for(size_t i=0;i<mixed.size();++i)pool.push_back({mixed[(i+1)%mixed.size()],0,{}});
    uint32_t state=1;size_t draws=0;std::vector<AiChoice> choices;
    CHECK(!chooseComputerWords(pool,ComputerLevel::Beginner,[&](){++draws;state=state*214013u+2531011u;return (state>>16)&32767u;},choices));
    std::vector<std::string> actual;for(const auto& choice:choices)actual.push_back(pool[choice.word].word);
    CHECK(actual==std::vector<std::string>({"ZAB","ZABES","ZABY","ZABYS","ZABS","ZABE","ZABEES","ZABED"}));CHECK(draws==472);
}
void motionAndRetirement() {
    auto geometry=defaultComputerGeometry(4);AcceptedWord word{"QUAA",1,{0,1,2}};ComputerMotion motion;
    CHECK(!makeComputerMotion(word,geometry,geometry.origin,400,500,{300,300,300},motion));
    CHECK(motion.presses.size()==3 && motion.duration>motion.releases.back());
    for(size_t i=0;i<3;++i)CHECK(motion.releases[i]-motion.presses[i]==350);
    CHECK(bool(makeComputerMotion(word,geometry,geometry.origin,0,500,{300,300,300},motion)) && !motion.duration);
    geometry.centers[0].x=10001;CHECK(bool(makeComputerMotion(word,geometry,geometry.origin,400,500,{300,300,300},motion)));
    geometry=defaultComputerGeometry(4);geometry.origin={0,0};geometry.centers[0]={0,0};geometry.submit={550,220};
    word.path={0};CHECK(!makeComputerMotion(word,geometry,geometry.origin,400,600,{50},motion));
    CHECK(motion.presses==std::vector<uint64_t>({50}) && motion.releases==std::vector<uint64_t>({150}) && motion.duration==1200);
    CHECK(bool(makeComputerMotion(word,geometry,geometry.origin,1,1,{50},motion)) && !motion.duration);
    Board board;CHECK(!board.set(4,std::vector<std::string>(16,"A")));const auto vocabulary=dictionary({"AAA","AAAA","AAAAA"});
    bool sawCommon=false;
    for(uint32_t seed=0;seed<32 && !sawCommon;++seed) {
        ComputerPlan plan;CHECK(!plan.build(board,vocabulary,vocabulary,ComputerLevel::Advanced,seed,0));
        if(plan.exhausted())continue;
        sawCommon=true;uint64_t last=0;while(!plan.exhausted()){last=plan.due();CHECK(plan.take(last));}
        CHECK(!plan.finished(last) && !plan.finished(last+10000) && plan.finished(last+18000));
    }
    CHECK(sawCommon);
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
            fixtures::ScriptedSystem sys;sys.clock=[](size_t f){return uint64_t(f*5000);};
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
            if(animations){const uint64_t times[]={0,4400,14400,64400,65500,600000,604400,604401,605501,605501};return times[std::min(f,size_t(9))];}
            const uint64_t times[]={0,10000,60000,600000,600001,600001};return times[std::min(f,size_t(5))];
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
    auto t=target(tmp/"replay");fixtures::ScriptedSystem sys;sys.clock=[](size_t f){return uint64_t(f*5000);};
    sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==5 || f==11)return {keyEvent(key::Return)};
        if(f==10)return {keyEvent(key::F1+1)};
        if(f==16)return {keyEvent(key::Escape)};
        return {};
    };
    ClassicGame game(sys,t,false,{"CPU Mr. Boggle"},{ComputerLevel::MrBoggle});CHECK(!game.run());CHECK(sys.hashes.size()==16);
    if(sys.hashes.size()==16){CHECK(sys.hashes[4]==sys.hashes[9]);CHECK(sys.hashes[0]==sys.hashes[5]);}
    fixtures::ScriptedSystem fresh;fresh.clock=[](size_t f){return uint64_t(f*5000);};
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
        const uint64_t times[]={0,10000,60000,600000,600001,600001};return times[std::min(f,size_t(5))];
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
    CHECK(usesCommonVocabulary(ComputerLevel::Beginner) && !usesCommonVocabulary(ComputerLevel::MrBoggle));
    for(bool master:{false,true}) {
        Board board;CHECK(!board.set(master?5:4,std::vector<std::string>(master?25:16,"A")));
        auto main=dictionary({"AAA","AAAA","AAAAAAAAA","AAAAAAAAAA","AAAAAAAAAAAA"});
        auto common=dictionary({"AAA","AAAA","AAAAAAAAA","ZZZZ"});
        for(unsigned level=0;level<3;++level)for(uint32_t seed=0;seed<32;++seed) {
            ComputerPlan plan;CHECK(!plan.build(board,common,main,ComputerLevel(level),seed,0));
            for(const auto& word:plan.words())CHECK(common.contains(word.word) && main.contains(word.word) && word.word.size()<=9);
        }
        ComputerPlan mr;Dictionary absent;CHECK(!mr.build(board,absent,main,ComputerLevel::MrBoggle,6551,0));
        CHECK(mr.words().size()==(master?4:5) && mr.words().front().word.size()==12);
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
        System sys;sys.clock=[](size_t f){return uint64_t(f*5000);};
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
    };mr.clock=[](size_t f){return uint64_t(f*5000);};
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
    {"whole-plan difficulty, suffix/prefix recognition, threshold behavior and stable Mr. Boggle ties",[](const auto&){thinkingTimes();}},
    {"measured cube holds, rounded path travel, common retirement and invalid geometry",[](const auto&){motionAndRetirement();}},
    {"Traditional and Chess opponents cancel shared words, reject human input and save humans only",[](const auto& tmp){live(tmp);}},
    {"four computers finish both variants and turn modes without human handoff confirmation",[](const auto& tmp){computerOnly(tmp);}},
    {"exact CPU deadlines, opening and handoff exclusions, replay/new-board plan reset",[](const auto& tmp){timingAndReplay(tmp);}},
    {"Classic common dictionary/search boundary versus full English Mr. Boggle",[](const auto&){vocabulary();}},
    {"Mr. Boggle searches longest first and ends timed/untimed turns on exhaustion",[](const auto& tmp){mrBehavior(tmp);}},
    {"typed roster guards and older human-only imports",[](const auto& tmp){guards(tmp);}},
    {"primary computer choice does not rename or pollute remembered human profiles",[](const auto& tmp){frontend(tmp);}},
});

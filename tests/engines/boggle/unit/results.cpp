// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/results.h"
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/user_dictionary.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
using namespace reluva;
using namespace reluva::boggle;
namespace {
Dictionary words(const std::vector<std::string>& list) {
    std::vector<uint8_t> bytes(12);uint32_t mask=0;
    for(const auto& text:list){mask|=uint32_t(1)<<unsigned(text[0]-'A');bytes.insert(bytes.end(),text.begin(),text.end());bytes.push_back(0);}
    bytes.push_back(0);test::fixtures::put32(bytes,4,mask);test::fixtures::put32(bytes,8,uint32_t(bytes.size()-12));
    Dictionary out;CHECK(!out.read(bytes));return out;
}
void adjudication(const std::filesystem::path& tmp) {
    auto dictionary=words({"CAT","QUIT"});UserDictionary user;CHECK(!user.load(tmp / "save"));
    Board board;CHECK(!board.set(4,{"C","A","T","S","X","Qu","I","T","B","D","E","F","G","H","J","K"}));
    ClassicRound round(dictionary,&user);RoundResults results;CHECK(!round.board(board));CHECK(bool(results.build(round)));
    CHECK(!round.start(0));CHECK(bool(results.build(round)));
    CHECK(round.submit("CAT",0)==Submission::Accepted);CHECK(round.submit("QUIT",0)==Submission::Accepted);
    CHECK(round.submit("QUITS",0)==Submission::UnknownWord);CHECK(round.submit("CAX",0)==Submission::UnknownWord);
    round.finish();CHECK(bool(results.build(round)));CHECK(!results.ready() && results.words().empty());
    CHECK(!round.decide(DictionaryChoice::No));CHECK(bool(results.build(round)));
    CHECK(!round.decide(DictionaryChoice::Yes));CHECK(!results.build(round));
    CHECK(results.ready() && results.score()==3 && results.words().size()==3);
    CHECK(results.words()[1].entry.word=="QUIT" && results.words()[1].entry.path.size()==3);
    CHECK(results.words()[2].state==WordState::Added && results.words()[2].entry.points==1);
    CHECK(results.rowAt(633,58,true,0)==0);CHECK(results.rowAt(442,217,false,0)==0);
    CHECK(!results.rowAt(632,58,true,0));CHECK(!results.rowAt(792,58,true,0));
    CHECK(!results.rowAt(633,109,true,0)); // empty row after the three results
    CHECK(!round.start(1));CHECK(results.words().size()==3); // snapshot is independent of replay
    results.clear();CHECK(!results.ready() && results.words().empty());CHECK(!results.rowAt(633,58,true,0));
}
void pagedRows() {
    std::vector<std::string> list;std::string text="AB";
    for(size_t n=2;n<=25;++n){text+="AB";list.push_back(text);}
    auto dictionary=words(list);Board board;CHECK(!board.set(5,std::vector<std::string>(25,"AB")));
    ClassicRound round(dictionary);CHECK(!round.board(board));CHECK(!round.start(0));
    CHECK(round.submit(list[0],0,{20,21})==Submission::Accepted);
    for(size_t i=1;i<list.size();++i)CHECK(round.submit(list[i],0)==Submission::Accepted);
    round.finish();RoundResults results;CHECK(!results.build(round));CHECK(results.score()==246);
    CHECK(results.words()[0].entry.path==std::vector<size_t>({20,21}));
    CHECK(results.rowAt(633,58,true,0)==1);CHECK(results.rowAt(633,58,true,SIZE_MAX)==0);
    CHECK(!results.rowAt(780,448,true,0));CHECK(!results.rowAt(780,480,true,0)); // arrow pixels never select a row
    CHECK(results.scrollFor(0,true,0)==1);CHECK(results.scrollFor(23,true,1)==0);
    CHECK(results.scrollFor(23,false,10)==0);CHECK(results.scrollFor(0,false,0)==10);
    CHECK(results.rowAt(442,217,false,0)==10);CHECK(!results.rowAt(442,455,false,0));
}
void review(const std::filesystem::path& tmp) {
    test::Record record;
    for(bool master:{false,true})for(bool art:{false,true}) {
        const auto root=tmp / (std::string(master?"master-":"original-")+(art?"art":"plain"));fixtures::artworkInstall(root);
        GameTarget target;target.path=root;target.savePath=root.parent_path() / (root.filename().string()+"-save");
        target.settings={{"classic_variant",master?"master":"original"},{"classic_artwork",art?"1":"0"},{"classic_seconds","60"},{"classic_animations","0"},{"classic_sound","0"}};
        fixtures::ScriptedSystem sys;sys.clock=[](size_t f){return f<2?uint64_t(0):uint64_t(60000);};
        auto row=fixtures::mouseEvent(EventType::MouseDown,art?640:450,art?60:220);
        sys.events=[&](size_t f)->std::vector<Event>{
            using namespace fixtures;
            if(f==0 || f==9 || f==13)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAA"),keyEvent(key::Return),textEvent("AAAAAA")};
            if(f==3)return {keyEvent('a'),keyEvent(key::Return),keyEvent(key::F1+1)};
            if(f==4)return {keyEvent('2'),keyEvent(key::Return),keyEvent('a')}; // final results must be displayed first
            if(f==5 || f==11)return {row};
            if(f==8)return {keyEvent(key::Down)};
            if(f==6){auto right=row;right.button=MouseButton::Right;return {right};}
            if(f==7)return {textEvent("AAAAAAA"),mouseEvent(EventType::MouseDown,200,200),mouseEvent(EventType::MouseUp,200,200)};
            if(f==10)return {keyEvent(key::F1)};
            if(f==12)return {keyEvent(key::F1+1)};
            if(f==14)return {keyEvent(key::Escape)};
            return {};
        };
        ClassicGame game(sys,target);CHECK(!game.run());CHECK(sys.hashes.size()==14);
        if(sys.hashes.size()!=14)continue;
        CHECK(sys.hashes[2]==sys.hashes[3]);CHECK(sys.hashes[4]!=sys.hashes[5]);
        CHECK(sys.hashes[4]==sys.hashes[6] && sys.hashes[6]==sys.hashes[7]);
        CHECK(sys.hashes[5]==sys.hashes[8]);CHECK(sys.hashes[10]==sys.hashes[11]);
        const auto prefix=std::string(master?"master.":"original.")+(art?"art.":"plain.");
        for(size_t i=0;i<sys.hashes.size();++i)record.add(prefix+std::to_string(i),sys.hashes[i]);
    }
    record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/results-record.txt","boggle.unit.results","tests/engines/boggle/unit/results.cpp");
}
void missedReview(const std::filesystem::path& tmp) {
    test::Record record;
    for(bool master:{false,true})for(bool art:{false,true}) {
        const auto root=tmp / (std::string(master?"master-":"original-")+(art?"art":"plain"));fixtures::artworkInstall(root);
        GameTarget target;target.path=root;target.savePath=root.parent_path() / (root.filename().string()+"-save");
        target.settings={{"classic_variant",master?"master":"original"},{"classic_artwork",art?"1":"0"},{"classic_animations","0"},{"classic_sound","0"}};
        UserDictionary user;CHECK(!user.load(target.savePath,root));CHECK(!user.add({"AAAAA"}));
        fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
        Event toggle=fixtures::mouseEvent(EventType::MouseDown,art?30:438,art?460:82);
        sys.events=[&](size_t f)->std::vector<Event> {
            using namespace fixtures;
            if(f==0 || f==9 || f==14)return {keyEvent(key::Return)};
            if(f==1)return {keyEvent('a'),toggle}; // unavailable while playing
            if(f==2)return {textEvent("AAAA"),keyEvent(key::Return)};
            if(f==3 || f==10)return {keyEvent(key::F1)};
            if(f==4 || f==8 || f==11)return {keyEvent('a')};
            if(f==5 || f==12)return {keyEvent(key::Up)};
            if(f==6){auto right=toggle;right.button=MouseButton::Right;return {right};}
            if(f==7)return {toggle};
            if(f==13)return {keyEvent(key::F1+1)};
            if(f==15)return {keyEvent(key::Escape)};
            return {};
        };
        std::vector<std::string> scores;
        sys.present=[&](const Surface& screen,size_t) {
            test::Hash hash;const int left=art?630:430,top=art?565:135,bottom=art?590:165;
            for(int y=top;y<bottom;++y)hash.bytes(screen.row(y)+left*4,size_t(screen.w-left)*4);
            scores.push_back(hash.hex());
        };
        ClassicGame game(sys,target);CHECK(!game.run());CHECK(sys.hashes.size()==15);
        if(sys.hashes.size()!=15)continue;
        CHECK(sys.hashes[0]==sys.hashes[1] && sys.hashes[0]==sys.hashes[9] && sys.hashes[0]==sys.hashes[14]);
        CHECK(sys.hashes[3]==sys.hashes[7]);CHECK(sys.hashes[4]==sys.hashes[6] && sys.hashes[4]==sys.hashes[8]);
        CHECK(sys.hashes[4]!=sys.hashes[5]);CHECK(scores[3]==scores[4] && scores[4]==scores[5]);
        CHECK(scores[10]==scores[11] && scores[11]==scores[12]); // empty-round discovery awards nothing
        UserDictionary after;CHECK(!after.load(target.savePath,root));CHECK(after.words()==user.words());
        const auto prefix=std::string(master?"master.":"original.")+(art?"art.":"plain.");
        for(size_t i=0;i<sys.hashes.size();++i)record.add(prefix+std::to_string(i),sys.hashes[i]);
    }
    record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/missed-words-record.txt","boggle.unit.results","tests/engines/boggle/unit/results.cpp");
}
void missedPages(const std::filesystem::path& tmp) {
    fixtures::artworkInstall(tmp / "install");std::string dice;for(int i=0;i<25;++i)dice+="AB,AB,AB,AB,AB,AB\n";
    fixtures::write(tmp / "install/Dicts/EngMas.dst",dice);
    std::vector<uint8_t> bytes(12);std::string word="AB";
    for(size_t i=2;i<=25;++i){word+="AB";bytes.insert(bytes.end(),word.begin(),word.end());bytes.push_back(0);}
    bytes.push_back(0);test::fixtures::put32(bytes,4,1);test::fixtures::put32(bytes,8,uint32_t(bytes.size()-12));
    fixtures::write(tmp / "install/Dicts/English.dic",bytes);
    GameTarget target;target.path=tmp / "install";target.savePath=tmp / "save";
    target.settings={{"classic_variant","master"},{"classic_animations","0"},{"classic_sound","0"}};
    fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
    sys.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==0)return {keyEvent(key::Return)};
        if(f==1)return {keyEvent(key::F1)};
        if(f==2)return {keyEvent('a')};
        if(f==3)return {keyEvent(key::PageDown)};
        if(f==4)return {keyEvent(key::PageUp)};
        if(f==5)return {keyEvent(key::Down)};
        if(f==6){auto e=mouseEvent(EventType::MouseDown,640,60);e.button=MouseButton::Right;return {e};}
        if(f==7)return {keyEvent(key::Escape)};
        return {};
    };
    ClassicGame game(sys,target);CHECK(!game.run());CHECK(sys.hashes.size()==7);
    if(sys.hashes.size()==7){CHECK(sys.hashes[2]!=sys.hashes[3]);CHECK(sys.hashes[2]==sys.hashes[4]);CHECK(sys.hashes[2]==sys.hashes[6]);}
}
}
static const test::Suite suite("boggle.unit.results",{
    {"all-word page navigation and viewport preservation on deselection",[](const auto& tmp){missedPages(tmp);}},
    {"missed-word toggles, saved vocabulary, read-only scores, replay and new boards",[](const auto& tmp){missedReview(tmp);}},
    {"adjudicated snapshots, approved/declined words, Qu and immutable replay",[](const auto& tmp){adjudication(tmp);}},
    {"paged row mapping, empty areas and history button precedence",[](const auto&){pagedRows();}},
    {"timeout, discarded input, final score, selected paths, replay and new board",[](const auto& tmp){review(tmp);}},
});

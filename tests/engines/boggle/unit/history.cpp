// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/history.h"
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/round.h"
#include "engines/boggle/resources.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <limits>
#include <set>
using namespace reluva;
using namespace reluva::boggle;
namespace {
void ranges() {
    auto p=wordHistoryView(0,23,0);CHECK(p.begin==0 && p.end==0 && p.maximum==0);
    p=wordHistoryView(23,23,999);CHECK(p.begin==0 && p.end==23 && !p.maximum);
    p=wordHistoryView(24,23,0);CHECK(p.begin==1 && p.end==24 && p.maximum==1);
    CHECK(scrollWordHistory(0,24,23,1)==1);CHECK(scrollWordHistory(1,24,23,-1)==0);
    p=wordHistoryView(60,23,23);CHECK(p.begin==14 && p.end==37 && p.maximum==37);
    CHECK(scrollWordHistory(23,60,23,23)==37);CHECK(scrollWordHistory(37,60,23,-23)==14);
    p=wordHistoryView(20,23,37);CHECK(p.begin==0 && p.end==20 && !p.maximum);
    CHECK(scrollWordHistory(37,20,23,1)==0);
    CHECK(scrollWordHistory(0,60,23,INT64_MAX)==37);
    CHECK(scrollWordHistory(37,60,23,INT64_MIN)==0);
    p=wordHistoryView(60,0,0);CHECK(p.begin==60 && p.end==60 && !p.maximum);
    const size_t maximum=std::numeric_limits<size_t>::max();
    p=wordHistoryView(maximum,23,0);CHECK(p.begin==maximum-23 && p.end==maximum);
    CHECK(scrollWordHistory(maximum,maximum,23,INT64_MAX)==maximum-23);
    CHECK(historyOlder.contains(768,448));CHECK(!historyOlder.contains(792,448));
    CHECK(!historyNewer.contains(767,472));CHECK(historyNewer.contains(791,493));
}
std::vector<std::string> vocabulary(const Board& board,size_t minimum) {
    std::vector<std::string> words;std::set<std::string> seen;
    auto walk=[&](auto&& self,size_t cell,uint32_t used,std::string word)->void {
        word+=board.faces()[cell];used|=uint32_t(1)<<cell;
        if(word.size()>=minimum && seen.insert(word).second)words.push_back(word);
        if(words.size()>=61 || word.size()>=6)return;
        for(size_t next=0;next<board.faces().size() && words.size()<61;++next)
            if(!(used&(uint32_t(1)<<next)) && board.adjacent(cell,next))self(self,next,used,word);
    };
    for(size_t cell=0;cell<board.faces().size() && words.size()<61;++cell)walk(walk,cell,0,{});
    return words;
}
Event wheel(int amount){Event event;event.type=EventType::MouseWheel;event.wheelY=amount;return event;}
void browsing(const std::filesystem::path& tmp) {
    test::Record record;
    for(bool master:{false,true})for(bool art:{false,true}) {
        const auto root=tmp / (std::string(master?"master-":"original-")+(art?"art":"plain"));
        fixtures::artworkInstall(root);const size_t side=master?5:4;
        // Independent, invented dice. Unique letters make the generated paths
        // easy to distinguish; no retail board or vocabulary is embedded.
        const std::string alphabet="ABCDEFGHIJKLMNOPRSTUVWXYZ";std::string text;
        for(size_t i=0;i<side*side;++i){for(int n=0;n<6;++n){if(n)text+=',';text+=alphabet[i];}text+='\n';}
        fixtures::write(root / (master?"Dicts/EngMas.dst":"Dicts/EngOrg.dst"),text);
        Resources resources;CHECK(!resources.open(root));DiceSet dice;
        CHECK(!resources.diceSet(master?"Dicts/EngMas.dst":"Dicts/EngOrg.dst",dice));Board board;
        CHECK(!Board::generate(dice,0x1997,board));const auto words=vocabulary(board,master?4:3);CHECK(words.size()==61);
        std::vector<uint8_t> dictionary(12);uint32_t mask=0;
        for(size_t i=0;i<60;++i){mask|=uint32_t(1)<<unsigned(words[i][0]-'A');dictionary.insert(dictionary.end(),words[i].begin(),words[i].end());dictionary.push_back(0);}
        dictionary.push_back(0);test::fixtures::put32(dictionary,4,mask);test::fixtures::put32(dictionary,8,uint32_t(dictionary.size()-12));
        fixtures::write(root / "Dicts/English.dic",dictionary);
        GameTarget target;target.path=root;target.savePath=root.parent_path() / (root.filename().string()+"-save");
        target.settings={{"classic_seed","6551"},{"classic_variant",master?"master":"original"},{"classic_artwork",art?"1":"0"},{"classic_animations","0"},{"classic_sound","0"}};
        fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
        sys.events=[&](size_t frame)->std::vector<Event> {
            if(frame==0 || frame==13)return {fixtures::keyEvent(key::Return)};
            if(frame==1) {std::vector<Event> events;for(size_t i=0;i<60;++i){events.push_back(fixtures::textEvent(words[i]));events.push_back(fixtures::keyEvent(key::Return));}events.push_back(fixtures::textEvent(words.back()));return events;}
            if(frame==2 || frame==12)return {fixtures::keyEvent(key::PageUp)};
            if(frame==3)return art?std::vector<Event>{fixtures::mouseEvent(EventType::MouseDown,780,480)}:std::vector<Event>{fixtures::mouseEvent(EventType::MouseMove,500,300),wheel(-1)};
            if(frame==4)return art?std::vector<Event>{fixtures::mouseEvent(EventType::MouseDown,780,455),fixtures::mouseEvent(EventType::MouseMove,200,200)}:std::vector<Event>{wheel(1)};
            if(frame==5)return {fixtures::mouseEvent(EventType::MouseMove,0,0),wheel(INT32_MAX)};
            if(frame==6)return {fixtures::mouseEvent(EventType::MouseMove,art?700:500,300),wheel(INT32_MIN)};
            if(frame==7)return {wheel(INT32_MAX)};
            if(frame==8)return {fixtures::keyEvent(key::Return)};
            if(frame==9)return {fixtures::keyEvent(key::F1)};
            if(frame==10)return {fixtures::mouseEvent(EventType::MouseDown,780,455),fixtures::keyEvent(key::PageUp),wheel(1)};
            if(frame==11)return {fixtures::keyEvent('4')};
            if(frame==14)return {fixtures::keyEvent(key::Escape)};
            return {};
        };
        std::vector<std::string> summaries;
        sys.present=[&](const Surface& screen,size_t) {
            test::Hash hash;const int x=art?621:430;
            for(int y=art?530:130;y<(art?600:195);++y) {
                if(art && y>=557 && y<586)continue; // Pause is available only during play.
                hash.bytes(screen.row(y)+x*4,size_t(screen.w-x)*4);
            }
            summaries.push_back(hash.hex());
        };
        ClassicGame game(sys,target);CHECK(!game.run());CHECK(sys.hashes.size()==14);
        if(sys.hashes.size()!=14)continue;
        CHECK(sys.hashes[2]==sys.hashes[4]);CHECK(sys.hashes[4]==sys.hashes[5]);
        CHECK(sys.hashes[1]==sys.hashes[6]);CHECK(sys.hashes[6]!=sys.hashes[7]);
        CHECK(sys.hashes[9]==sys.hashes[10]);CHECK(sys.hashes[0]==sys.hashes[13]);
        CHECK(summaries[1]==summaries[11]); // Declining removes only the provisional word's points.
        const auto prefix=std::string(master?"master.":"original.")+(art?"art.":"plain.");
        for(size_t i=0;i<sys.hashes.size();++i)record.add(prefix+std::to_string(i),sys.hashes[i]);
    }
    record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/history-record.txt","boggle.unit.history","tests/engines/boggle/unit/history.cpp");
}
}
static const test::Suite suite("boggle.unit.history",{
    {"visible ranges, changing word counts, limits and button edges",[](const auto&){ranges();}},
    {"60-word rounds, mouse/keyboard/wheel browsing, modal review and replay",[](const auto& tmp){browsing(tmp);}},
});

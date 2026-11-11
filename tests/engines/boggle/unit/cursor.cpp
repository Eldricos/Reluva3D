// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/cursor.h"
#include "engines/boggle/classic/computer.h"
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/layout.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/classic/highscores.h"
#include "engines/registry.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <algorithm>
using namespace reluva;
using namespace reluva::boggle;
namespace {
class CursorSystem : public fixtures::ScriptedSystem {
public:
    bool cursorVisible=true;size_t hides=0;
    std::vector<bool> shown;
    void setMouseCursorVisible(bool value) override {cursorVisible=value;if(!value)++hides;}
    void updateScreen(const Surface& screen) override {shown.push_back(cursorVisible);fixtures::ScriptedSystem::updateScreen(screen);}
};
Event quit(){Event event;event.type=EventType::Quit;return event;}
GameTarget target(const std::filesystem::path& root,bool chess=false,bool master=false,bool art=false) {
    fixtures::artworkInstall(root/"game");GameTarget t;t.path=root/"game";t.savePath=root/"save";
    std::filesystem::copy_file(t.path/"Dicts/English.dic",t.path/"Dicts/AI.dic");
    t.settings={{"classic_seed","6551"},{"classic_seconds","0"},{"classic_turns",chess?"chess":"traditional"},
        {"classic_variant",master?"master":"original"},{"classic_cursor","1"},{"classic_sound","0"},
        {"classic_animations","0"},{"classic_artwork",art?"1":"0"},{"frontend_movies","0"}};return t;
}
void geometry() {
    std::vector<ClassicPoint> centers(16,{100,100});centers[0]={100,20};centers[1]={100,100};centers[5]={20,100};
    const ClassicPoint origin{20,20};AcceptedWord word{"QUAA",1,{0,1,5}};
    auto sample=[&](uint64_t time){return computerCursorFrame(word,centers,origin,time,6000,{140,100});};
    CHECK(!sample(4999).active);
    auto frame=sample(5000);CHECK(frame.active && !frame.cubes && frame.point.x==20 && frame.point.y==20);
    frame=sample(5125);CHECK(!frame.cubes && frame.point.x==60 && frame.point.y==20);
    frame=sample(5250);CHECK(frame.cubes==1 && frame.point.x==100 && frame.point.y==20);
    frame=sample(5500);CHECK(frame.cubes==2 && frame.point.x==100 && frame.point.y==100);
    frame=sample(5750);CHECK(frame.cubes==3 && frame.point.x==20 && frame.point.y==100);
    frame=sample(5875);CHECK(frame.cubes==3 && frame.point.x==80 && frame.point.y==100);
    for(uint64_t time:{6000u,999999u}) {frame=sample(time);CHECK(frame.cubes==3 && frame.point.x==140 && frame.point.y==100);}
    Board board;std::vector<std::string> faces(16,"A");faces[0]="QU";CHECK(!board.set(4,faces));CHECK(board.matches(word.word,word.path));
    std::string input;for(size_t i=0;i<sample(5250).cubes;++i)input+=board.faces()[word.path[i]];CHECK(input=="QU");
    CHECK(!computerCursorFrame(word,centers,origin,0,0,{140,100}).active);
    word.path={16};CHECK(!sample(5250).active);word.path.clear();CHECK(!sample(5250).active);
    Surface rgba;rgba.create(32,32,PixelFormat::RGBA32);const auto empty=rgba.pixels;drawComputerCursor(rgba,{-3,-3});CHECK(rgba.pixels!=empty);
    auto before=rgba.pixels;drawComputerCursor(rgba,{100,100});CHECK(rgba.pixels==before);
    Surface indexed;indexed.create(32,32,PixelFormat::CLUT8);before=indexed.pixels;drawComputerCursor(indexed,{1,1});CHECK(indexed.pixels==before);
    ClassicOptions options;CHECK(!readClassicOptions({},options) && !options.cursor);
    for(const auto* value:{"0","1"}){CHECK(!readClassicOptions({{"classic_cursor",value}},options));CHECK(options.cursor==(std::string(value)=="1"));}
    for(const auto* value:{"","2","true","01"," 1"})CHECK(bool(readClassicOptions({{"classic_cursor",value}},options)) && !options.cursor);
    word.path={0};centers.assign(16,{0,0});ComputerMotion timeline{{50},{150},1200};
    auto timed=[&](uint64_t time){return computerCursorFrame(word,centers,{0,0},time,1700,{550,220},&timeline);};
    CHECK(!timed(499).active && timed(500).active && !timed(549).cubes);
    CHECK(timed(550).cubes==1 && timed(649).point.x==0);
    CHECK(timed(1700).point.x==550 && timed(1700).point.y==220);
    timeline.releases={1200};CHECK(!timed(1700).active);
}
void visible(const std::filesystem::path& tmp) {
    for(bool chess:{false,true})for(bool master:{false,true})for(bool art:{false,true}) {
        std::vector<std::string> control;
        for(bool cursor:{false,true}) {
            auto t=target(tmp/(std::to_string(chess)+std::to_string(master)+std::to_string(art)+std::to_string(cursor)),chess,master,art);
            t.settings["classic_cursor"]=cursor?"1":"0";
            CursorSystem sys;sys.clock=[](size_t f){return uint64_t(f*1000);};
            sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
                if(f==0)return {keyEvent(key::Return)};
                if(f==2)return {textEvent("AAAAAA"),keyEvent(key::Return)}; // cannot inject a computer word
                if(f==11)return {quit()};
                return {};
            };
            ClassicGame game(sys,t,false,{"CPU Mr. Boggle"},{ComputerLevel::MrBoggle});CHECK(!game.run());CHECK(sys.hashes.size()==11 && sys.cursorVisible && !sys.textEnabled);
            CHECK(sys.hides==(cursor?1u:0u));
            if(!cursor)control=sys.hashes;
            else if(sys.hashes.size()==11 && control.size()==11) {
                CHECK(sys.hashes[1]!=control[1] && sys.hashes[2]!=control[2]);
                CHECK(sys.hashes[9]==control[9] && sys.hashes[10]==control[10]); // same words/score, completed at the same time
                CHECK(!sys.shown[1] && sys.shown[9]);
            }
            CHECK(!std::filesystem::exists(t.savePath/"HighScores.txt"));
        }
    }
}
void handoffs(const std::filesystem::path& tmp) {
    auto t=target(tmp,true);CursorSystem sys;sys.clock=[](size_t f){return uint64_t(f*1000);};
    sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==7)return {keyEvent(key::Return)};
        if(f==1)return {textEvent("AAAA"),mouseEvent(EventType::MouseDown,600,300)};
        if(f==3 || f==4)return {textEvent("AAAAAA"),keyEvent(key::Return)};
        if(f==8)return {keyEvent(key::F1)};
        if(f==16)return {quit()};
        return {};
    };
    ClassicGame game(sys,t,false,{"Alex","CPU Mr. Boggle"},{ComputerLevel::Human,ComputerLevel::MrBoggle});CHECK(!game.run());CHECK(sys.hashes.size()==16 && sys.cursorVisible);
    if(sys.shown.size()==16)CHECK(sys.shown[0] && sys.shown[1] && !sys.shown[2] && sys.shown[5] && sys.shown[6] && sys.shown[7] && sys.shown[14]);
    HighScores scores;CHECK(!scores.load(t.savePath,t.path));CHECK(scores.table().size()==1);for(const auto& row:scores.table())CHECK(row.score==0); // shared AAAA still cancels
}
void exits(const std::filesystem::path& tmp) {
    for(int mode=0;mode<4;++mode) {
        std::vector<std::string> baseline;
        for(bool cursor:{false,true}) {
            auto t=target(tmp/(std::to_string(mode)+std::to_string(cursor)));t.settings["classic_cursor"]=cursor?"1":"0";
            if(mode==3)t.settings["classic_seconds"]="60";
            CursorSystem sys;sys.clock=[&](size_t f){return uint64_t(mode==3 && f>=2?60000:f*1000);};
            sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
                if(f==0 && mode!=2)return {keyEvent(key::Return)};
                if(f==2){if(mode==0)return {quit()};if(mode==1)return {keyEvent(key::F1)};if(mode==2)return {keyEvent(key::F1+2)};}
                if(f==4)return {quit()};
                return {};
            };
            ClassicGame game(sys,t,mode==2,{"CPU Mr. Boggle"},{ComputerLevel::MrBoggle});CHECK(!game.run());CHECK(sys.cursorVisible && !sys.textEnabled);
            if(mode==2)CHECK(game.returnToMenu());
            if(!cursor)baseline=sys.hashes;
            else if(mode==1 || mode==3)CHECK(sys.hashes.back()==baseline.back()); // no partly traced word survives end/expiry
        }
    }
}
void replay(const std::filesystem::path& tmp) {
    auto t=target(tmp);CursorSystem sys;sys.clock=[](size_t f){return uint64_t(f*1000);};
    sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==10 || f==21)return {keyEvent(key::Return)};
        if(f==20)return {keyEvent(key::F1+1)};
        if(f==23)return {keyEvent(key::F1)};
        if(f==24)return {quit()};
        return {};
    };
    ClassicGame game(sys,t,false,{"CPU Mr. Boggle"},{ComputerLevel::MrBoggle});CHECK(!game.run() && sys.cursorVisible);
    CHECK(sys.hashes.size()==24 && sys.hides==3);
    if(sys.hashes.size()==24) {
        CHECK(sys.hashes[0]==sys.hashes[10] && sys.hashes[9]==sys.hashes[19]);
        CHECK(sys.shown[20] && !sys.shown[21] && sys.shown[23]);
    }
}
void background(const std::filesystem::path& tmp) {
    std::vector<std::string> baseline;
    for(bool cursor:{false,true}) {
        auto t=target(tmp/std::to_string(cursor));t.settings["classic_background"]="1";t.settings["classic_cursor"]=cursor?"1":"0";
        CursorSystem sys;sys.clock=[](size_t f){return uint64_t(f*1000);};
        sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AAAA"),keyEvent(key::Return)};
            if(f==2)return {keyEvent(key::F1)};
            if(f==4)return {quit()};
            return {};
        };
        ClassicGame game(sys,t,false,{"Alex","CPU Mr. Boggle"},{ComputerLevel::Human,ComputerLevel::MrBoggle});CHECK(!game.run());CHECK(!sys.hides && sys.cursorVisible);
        if(!cursor)baseline=sys.hashes;else CHECK(sys.hashes==baseline);
    }
}
void listSubmission(const std::filesystem::path& tmp) {
    for(bool chess:{false,true})for(bool master:{false,true})for(bool art:{false,true}) {
        std::vector<std::string> baseline;
        for(bool click:{false,true}) {
            auto t=target(tmp/(std::to_string(chess)+std::to_string(master)+std::to_string(art)+std::to_string(click)),chess,master,art);
            Resources resources;ClassicArtwork artwork;ClassicOptions options;video::AviVideo1 empty;
            CHECK(!resources.open(t.path) && !readClassicOptions(t.settings,options));CHECK(!artwork.open(resources,t.path,options,empty));
            CursorSystem sys;sys.clock=[](size_t){return uint64_t(0);};
            sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
                auto submit=[&](){return click?mouseEvent(EventType::MouseDown,art?700:600,300):keyEvent(key::Return);};
                if(f==0)return {keyEvent(key::Return)};
                if(f==1) {
                    std::vector<Event> out;
                    for(size_t cube=0;cube<4;++cube) {
                        const int cell=368/int(options.side());
                        const auto point=art?artwork.cellCenter(cube,options.side()):ClassicPoint{24+int(cube)*cell+cell/2,100+cell/2};
                        out.push_back(mouseEvent(EventType::MouseDown,point.x,point.y));
                    }
                    out.push_back(submit());return out;
                }
                if(f==2)return {textEvent("AAAA"),submit()};
                if(f==3)return {textEvent("AA"),submit()};
                if(f==4)return {textEvent("ZZZZ"),submit()};
                if(f==5)return {textEvent("AAAAA"),submit()};
                if(f==6)return {keyEvent(key::F1)};
                if(f==7)return {keyEvent('2')};
                if(f==8)return {quit()};
                return {};
            };
            ClassicGame game(sys,t,false,{"Alex"});CHECK(!game.run());
            HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();CHECK(rows.size()==1 && rows[0].score==1);
            if(!click)baseline=sys.hashes;else CHECK(sys.hashes==baseline);
        }
    }
}
void frontend(const std::filesystem::path& tmp) {
    for(bool click:{false,true}) {
        auto t=target(tmp/std::to_string(click));t.settings["classic_cursor"]="0";
        CursorSystem sys;sys.clock=[](size_t f){return uint64_t(f*100);};
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==1 || f==7)return {keyEvent(key::Return)};
            if(f==2)return {keyEvent('m')};
            if(f==3)return {keyEvent('1')};
            if(f==4)return {keyEvent(key::End),keyEvent(key::Return)};
            if(f==5)return {keyEvent(key::Return)};
            if(f==6)return {click?mouseEvent(EventType::MouseDown,320,292):keyEvent('v')};
            if(f==11)return {keyEvent(key::F1+2)};
            if(f==12)return {keyEvent('l')};
            return {};
        };
        auto engine=findEngine("boggle")->create(sys,t);CHECK(!engine->run() && sys.hashes.size()==12 && sys.hides==1 && sys.cursorVisible);
    }
}
}
static const test::Suite suite("boggle.unit.cursor",{
    {"cube/list interpolation, Qu prefix, invalid paths, clipping and strict settings",[](const auto&){geometry();}},
    {"both boards/renderers/modes animate without changing words, scores or deadlines",[](const auto& tmp){visible(tmp);}},
    {"Chess handoffs restore the user cursor and preserve shared-word scoring",[](const auto& tmp){handoffs(tmp);}},
    {"quit, menu, manual end and expiry discard partial traces and restore the pointer",[](const auto& tmp){exits(tmp);}},
    {"Replay and New Board reset traces and restore the pointer between rounds",[](const auto& tmp){replay(tmp);}},
    {"background play suppresses the cursor without affecting hidden results",[](const auto& tmp){background(tmp);}},
    {"word-list clicks match Enter for selected cubes, repeats, validation and dictionary review",[](const auto& tmp){listSubmission(tmp);}},
    {"Players keyboard and mouse switches launch visible computer cursors",[](const auto& tmp){frontend(tmp);}},
});

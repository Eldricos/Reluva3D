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
#include <map>
#include <limits>
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
std::vector<uint8_t> cursorBitmap(unsigned depth=1) {
    using namespace test::fixtures;
    const size_t palette=size_t(1)<<depth,offset=44+palette*4;
    std::vector<uint8_t> b(offset+16);put16(b,0,1);put16(b,2,1);put32(b,4,40);put32(b,8,2);put32(b,12,4);
    put16(b,16,1);put16(b,18,uint16_t(depth));put32(b,24,8);
    b[48]=b[49]=b[50]=255;
    b[offset]=uint8_t(1u<<(8-depth));b[offset+4+depth/8]=depth==1?0x40:1; // bottom first: white/black, then black/white
    b[offset+12]=0xc0; // transparent and inverted pixels on the top row
    return b;
}
PeResource cursorResource(uint32_t type,uint32_t id,std::vector<uint8_t> bytes) {
    PeResource r;r.type.id=type;r.name.id=id;r.language=1033;r.data=std::move(bytes);return r;
}
std::vector<uint8_t> cursorGroup(const std::vector<PeResource>& images) {
    using namespace test::fixtures;
    std::vector<uint8_t> b(6+images.size()*14);put16(b,2,2);put16(b,4,uint16_t(images.size()));
    for(size_t i=0;i<images.size();++i) {
        CursorImage decoded;CHECK(!readCursorImage(images[i].data,decoded));const auto at=6+i*14;
        put16(b,at,uint16_t(decoded.colors.w));put16(b,at+2,uint16_t(decoded.colors.h*2));put16(b,at+4,1);
        put16(b,at+6,uint16_t(decoded.depth));put32(b,at+8,uint32_t(images[i].data.size()));put16(b,at+12,uint16_t(images[i].name.id));
    }
    return b;
}
std::vector<PeResource> inventedCursors() {
    std::vector<PeResource> r;
    for(unsigned i=0;i<3;++i)r.push_back(cursorResource(1,i+1,cursorBitmap(i==0?1:i==1?4:8)));
    r.push_back(cursorResource(12,5201,cursorGroup(r)));
    auto pressed=cursorBitmap(8);pressed[48]=31;pressed[49]=97;pressed[50]=211;
    r.push_back(cursorResource(1,4,pressed));r.push_back(cursorResource(12,5200,cursorGroup({r[4]})));
    return r;
}
void cursorImages() {
    using namespace test::fixtures;
    for(unsigned depth:{1u,4u,8u}) {
        auto b=cursorBitmap(depth);CursorImage image;CHECK(!readCursorImage(b,image));
        CHECK(image.colors.w==2 && image.colors.h==2 && image.hotspotX==1 && image.hotspotY==1 && image.depth==depth);
        Surface screen;screen.create(5,5,PixelFormat::RGBA32);
        for(size_t i=0;i<screen.pixels.size();i+=4){screen.pixels[i]=10;screen.pixels[i+1]=20;screen.pixels[i+2]=30;screen.pixels[i+3]=91;}
        image.draw(screen,2,2,1,1);
        CHECK(screen.row(1)[4]==10 && screen.row(1)[8]==245 && screen.row(1)[9]==235 && screen.row(1)[10]==225);
        CHECK(screen.row(1)[7]==91 && screen.row(2)[4]==255 && screen.row(2)[8]==0); // opaque white and black
        const auto before=screen.pixels;image.draw(screen,INT32_MIN,INT32_MAX);CHECK(screen.pixels==before);
        image.draw(screen,-1,-1); // clipped hotspot at the top-left edge
        image.draw(screen,2,2,0,1);
        for(size_t n=0;n<b.size();++n){CHECK(bool(readCursorImage({b.begin(),b.begin()+n},image)));CHECK(!image.depth && image.colors.rgba.empty());}
        for(auto change:std::vector<std::pair<size_t,uint32_t>>{{4,12},{8,0},{8,257},{12,3},{16,0},{18,24},{20,1},{24,3},{36,(1u<<depth)+1}}) {
            auto bad=b;put32(bad,change.first,change.second);CHECK(bool(readCursorImage(bad,image)));
        }
        auto bad=b;put16(bad,0,2);CHECK(bool(readCursorImage(bad,image)));
    }
    auto r=inventedCursors();CursorImage chosen;CHECK(!readCursorGroup(r,5201,1033,chosen) && chosen.depth==8);
    CHECK(bool(readCursorGroup(r,5201,1031,chosen)) && !chosen.depth);
    auto invalid=r;invalid[3].data.back()=99;CHECK(bool(readCursorGroup(invalid,5201,1033,chosen)));
    invalid=r;invalid[0].data.pop_back();CHECK(bool(readCursorGroup(invalid,5201,1033,chosen)));
    ClassicCursorArtwork artwork;CHECK(!artwork.read(r) && artwork.active());
    Surface up,down;up.create(8,8,PixelFormat::RGBA32);down=up;artwork.draw(up,{3,3},false);artwork.draw(down,{3,3},true);CHECK(up.pixels!=down.pixels);
    r.pop_back();CHECK(bool(artwork.read(r)) && !artwork.active());
    CHECK(!artwork.read({}) && !artwork.active());
}
// Construct a data-only multi-resource PE from invented payloads, not game bytes.
std::vector<uint8_t> cursorLanguage(std::vector<PeResource> resources) {
    using namespace test::fixtures;
    std::array<std::u16string,16> strings{};strings[0]=u"Invented cursor fixture";
    resources.push_back(cursorResource(6,1,stringBlock(strings)));
    std::map<uint32_t,std::vector<PeResource>> types;for(auto& r:resources)types[r.type.id].push_back(std::move(r));
    std::vector<uint8_t> tree(16+types.size()*8);put16(tree,14,uint16_t(types.size()));size_t typeAt=16;
    auto allocate=[&](size_t n){const auto at=tree.size();tree.resize(at+n);return at;};
    for(auto& [type,items]:types) {
        std::sort(items.begin(),items.end(),[](const auto& a,const auto& b){return a.name.id<b.name.id;});
        const auto dir=allocate(16+items.size()*8);put32(tree,typeAt,type);put32(tree,typeAt+4,0x80000000u|uint32_t(dir));typeAt+=8;
        put16(tree,dir+14,uint16_t(items.size()));
        for(size_t i=0;i<items.size();++i) {
            const auto lang=allocate(24),entry=allocate(16);const auto& item=items[i];
            put32(tree,dir+16+i*8,item.name.id);put32(tree,dir+20+i*8,0x80000000u|uint32_t(lang));
            put16(tree,lang+14,1);put32(tree,lang+16,1033);put32(tree,lang+20,uint32_t(entry));
            const auto payload=allocate((item.data.size()+3)&~size_t(3));put32(tree,entry,uint32_t(0x3000+payload));put32(tree,entry+4,uint32_t(item.data.size()));
            std::copy(item.data.begin(),item.data.end(),tree.begin()+payload);
        }
    }
    auto pe=resourcePe({});const size_t raw=(tree.size()+511)&~size_t(511);pe.resize(512+raw);
    put32(pe,0x98+96+20,uint32_t(tree.size()));put32(pe,0x98+224+8,uint32_t(tree.size()));put32(pe,0x98+224+16,uint32_t(raw));
    std::copy(tree.begin(),tree.end(),pe.begin()+512);return pe;
}
void installedCursorFlow(const std::filesystem::path& tmp) {
    std::vector<std::string> fallback;
    for(bool installed:{false,true}) {
        auto t=target(tmp / std::to_string(installed));
        if(installed)fixtures::write(t.path / "Language/English.dll",cursorLanguage(inventedCursors()));
        CursorSystem sys;sys.clock=[](size_t f){return uint64_t(f*300);};
        sys.events=[](size_t f)->std::vector<Event>{
            if(f==0)return {fixtures::keyEvent(key::Return)};
            if(f==11)return {quit()};
            return {};
        };
        ClassicGame game(sys,t,false,{"CPU Mr. Boggle"},{ComputerLevel::MrBoggle});CHECK(!game.run() && sys.cursorVisible && !sys.textEnabled);
        if(!installed)fallback=sys.hashes;
        else {CHECK(fallback.size()==sys.hashes.size());CHECK(fallback!=sys.hashes);}
    }
}
void humanCursorFlow(const std::filesystem::path& tmp) {
    using namespace fixtures;
    ClassicCursorArtwork hand;CHECK(!hand.read(inventedCursors()));
    for(bool master:{false,true})for(bool art:{false,true}) {
        std::vector<std::string> expected;
        for(bool installed:{false,true})for(bool computerOption:{false,true}) {
            auto t=target(tmp/(std::to_string(master)+std::to_string(art)+std::to_string(installed)+std::to_string(computerOption)),false,master,art);
            t.settings["classic_cursor"]=computerOption?"1":"0";
            if(installed)write(t.path/"Language/English.dll",cursorLanguage(inventedCursors()));
            Resources resources;ClassicArtwork artwork;ClassicOptions options;
            CHECK(!resources.open(t.path) && !readClassicOptions(t.settings,options));
            CHECK(!artwork.open(resources,t.path,options,{}));
            const auto first=art?artwork.cellCenter(0,options.side()):ClassicPoint{50,150};
            const auto second=art?artwork.cellCenter(1,options.side()):ClassicPoint{150,150};
            auto event=[](EventType type){Event e;e.type=type;return e;};
            auto right=mouseEvent(EventType::MouseDown,600,300);right.button=MouseButton::Right;
            CursorSystem sys;sys.clock=[](size_t){return uint64_t(0);};
            sys.events=[&](size_t f)->std::vector<Event> {
                auto mouse=[&](EventType type,ClassicPoint p){return mouseEvent(type,p.x,p.y);};
                if(f==0 || f==21)return {keyEvent(key::Return)};
                if(f==1 || f==7 || f==11 || f==20 || f==24)return {mouse(EventType::MouseMove,first)};
                if(f==2 || f==8)return {mouse(EventType::MouseDown,first)};
                if(f==3)return {mouse(EventType::MouseMove,second)};
                if(f==4)return {mouse(EventType::MouseUp,second)};
                if(f==5)return {right};
                if(f==6)return {event(EventType::MouseLeave)};
                if(f==9)return {event(EventType::FocusLost)};
                if(f==10)return {event(EventType::FocusGained)};
                if(f==12)return {mouse(EventType::MouseUp,first)};
                if(f==13)return {mouseEvent(EventType::MouseDown,art?700:420,570)};
                if(f==14)return {mouseEvent(EventType::MouseUp,art?700:420,570)};
                if(f==15)return {mouseEvent(EventType::MouseDown,400,360)};
                if(f==16)return {mouseEvent(EventType::MouseUp,400,360)};
                if(f==17)return {right,textEvent("AAAA"),keyEvent(key::Return)};
                if(f==18)return {keyEvent(key::F1)};
                if(f==19)return {keyEvent(key::F1+1)};
                if(f==22)return {mouseEvent(EventType::MouseMove,-1,300)};
                if(f==23)return {mouseEvent(EventType::MouseMove,800,300)};
                if(f==25)return {quit()};
                return {};
            };
            auto visible=[](size_t f){return f==1 || f==2 || f==3 || f==4 || f==5 || f==7 || f==8 ||
                f==11 || f==12 || f==15 || f==16 || f==17 || f==21 || f==24;};
            if(!installed && !computerOption)sys.present=[&](const Surface& screen,size_t f) {
                Surface painted=screen;
                if(art && visible(f)) {
                    const auto point=f==3 || f==4?second:f==5 || f==17?ClassicPoint{600,300}:
                                     f==15 || f==16?ClassicPoint{400,360}:first;
                    hand.draw(painted,point,f==2 || f==3 || f==8 || f==15);
                }
                expected.push_back(test::Hash().bytes(painted.pixels).hex());
            };
            ClassicGame game(sys,t,false,{"Alex"});CHECK(!game.run());
            CHECK(sys.hashes.size()==25 && sys.cursorVisible && sys.textStopped);
            if(installed)CHECK(sys.hashes==expected);
            for(size_t f=0;f<sys.shown.size();++f)CHECK(sys.shown[f]==!(art && installed && visible(f)));
            HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();
            CHECK(rows.size()==1 && rows[0].score==1); // Cursor drawing never changes entry/scoring.
        }
    }
}
void humanCursorBundle(const std::filesystem::path& tmp) {
    auto t=target(tmp,false,false,true);auto incomplete=inventedCursors();incomplete.pop_back();
    fixtures::write(t.path/"Language/English.dll",cursorLanguage(incomplete));
    CursorSystem sys;sys.events=[](size_t){return std::vector<Event>{quit()};};
    {ClassicGame game(sys,t);CHECK(bool(game.run()) && sys.cursorVisible && !sys.hides);}
    t.settings["classic_artwork"]="0";
    {ClassicGame game(sys,t);CHECK(!game.run() && sys.cursorVisible && !sys.hides);}
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
    CHECK(!timed(549).pressed && timed(550).cubes==1 && timed(550).pressed && timed(649).pressed);
    CHECK(!timed(650).pressed && timed(649).point.x==0);
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
                if(f==4)return {click?mouseEvent(EventType::MouseDown,400,370):keyEvent(key::Return)};
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
    {"human raised/pressed hand, drag, right click, leave, focus, pause, replay and opt-out",[](const auto& tmp){humanCursorFlow(tmp);}},
    {"human-only cursor bundles reject partial data and respect artwork opt-out",[](const auto& tmp){humanCursorBundle(tmp);}},
    {"indexed cursor masks, inversion, hotspots, groups, truncation and fallback",[](const auto&){cursorImages();}},
    {"installed cursor states replace the native arrow without retaining the system pointer",[](const auto& tmp){installedCursorFlow(tmp);}},
    {"cube/list interpolation, Qu prefix, invalid paths, clipping and strict settings",[](const auto&){geometry();}},
    {"both boards/renderers/modes animate without changing words, scores or deadlines",[](const auto& tmp){visible(tmp);}},
    {"Chess handoffs restore the user cursor and preserve shared-word scoring",[](const auto& tmp){handoffs(tmp);}},
    {"quit, menu, manual end and expiry discard partial traces and restore the pointer",[](const auto& tmp){exits(tmp);}},
    {"Replay and New Board reset traces and restore the pointer between rounds",[](const auto& tmp){replay(tmp);}},
    {"background play suppresses the cursor without affecting hidden results",[](const auto& tmp){background(tmp);}},
    {"word-list clicks match Enter for selected cubes, repeats, validation and dictionary review",[](const auto& tmp){listSubmission(tmp);}},
    {"Players keyboard and mouse switches launch visible computer cursors",[](const auto& tmp){frontend(tmp);}},
});

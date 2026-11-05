// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/layout.h"
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/orientation.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include "tests/support/truetype_fixture.h"
using namespace reluva;
using namespace reluva::boggle;
namespace {
void orientation() {
    constexpr int expected[4][9]={{1,2,3,4,5,6,7,8,9},{7,4,1,8,5,2,9,6,3},
                                 {9,8,7,6,5,4,3,2,1},{3,6,9,2,5,8,1,4,7}};
    for(unsigned turns=0;turns<4;++turns)for(int y=0;y<3;++y)for(int x=0;x<3;++x) {
        const auto p=cubeTexturePoint(x,y,2,turns);
        CHECK(p[1]*3+p[0]+1==expected[turns][y*3+x]);
    }
    for(size_t side:{4u,5u}) {
        std::array<bool,4> seen{};bool changed=false;
        for(size_t cell=0;cell<side*side;++cell) {
            CHECK(cubeQuarterTurns(0x1997,cell,true)==0);
            const unsigned turns=cubeQuarterTurns(0x1997,cell,false);
            CHECK(turns<4);seen[turns]=true;
            CHECK(turns==cubeQuarterTurns(0x1997,cell,false));
            changed|=turns!=cubeQuarterTurns(0x1998,cell,false);
        }
        CHECK(changed);CHECK(seen[0] && seen[1] && seen[2] && seen[3]);
    }
}
void projection() {
    PlaneProjection rectangle({{{0,0},{100,0},{100,100},{0,100}}});
    const auto center=rectangle.project(1,1,2);
    CHECK(center.x==50 && center.y==50 && center.reciprocalDepth>0);
    const auto scaled=rectangle.project(1,1,2,5,4);
    CHECK(scaled.x==62 && scaled.y==62);
    // An invented trapezoid whose lower edge is twice as wide. The
    // intersection of its diagonals is (200/3,100/3), not (75,50).
    PlaneProjection tilted({{{0,0},{100,0},{200,100},{0,100}}});
    const auto middle=tilted.project(1,1,2);
    CHECK(middle.x==66 && middle.y==33);
    CHECK(tilted.project(0,0,2).x==0 && tilted.project(2,0,2).x==100);
    CHECK(tilted.project(2,2,2).x==200 && tilted.project(0,2,2).y==100);
    CHECK(tilted.project(1,2,2).reciprocalDepth==2*tilted.project(1,0,2).reciprocalDepth);
    CHECK(tilted.project(50,50,100).x==middle.x);
    CHECK(tilted.project(1,1,0).reciprocalDepth==0);
    CHECK(tilted.project(-1,1,2).reciprocalDepth==0);
    CHECK(tilted.project(1,3,2).reciprocalDepth==0);
    CHECK(tilted.project(1,1,2048).reciprocalDepth==0);
    CHECK(tilted.project(1,1,2,0,1).reciprocalDepth==0);
    PlaneProjection degenerate({{{0,0},{1,1},{2,2},{3,3}}});
    CHECK(degenerate.project(1,1,2).reciprocalDepth==0);
    PlaneProjection crossed({{{0,0},{100,100},{100,0},{0,100}}});
    CHECK(crossed.project(1,1,2).reciprocalDepth==0);
    PlaneProjection extreme({{{INT32_MIN,0},{100,0},{100,100},{0,100}}});
    CHECK(extreme.project(1,1,2).reciprocalDepth==0);
}
void progress() {
    for(uint64_t duration=60000;duration<=300000;duration+=30000) {
        CHECK(ClassicArtwork::timerFrame(duration,duration)==0);
        CHECK(ClassicArtwork::timerFrame(duration+1,duration)==0);
        CHECK(ClassicArtwork::timerFrame(duration/2,duration)==17);
        CHECK(ClassicArtwork::timerFrame(1,duration)==34);
        CHECK(ClassicArtwork::timerFrame(0,duration)==35);
    }
    CHECK(ClassicArtwork::timerFrame(0,0)==0);
    CHECK(ClassicArtwork::timerFrame(0,UINT64_MAX)==0);
}
void rendering(const std::filesystem::path& tmp) {
    fixtures::artworkInstall(tmp);Resources resources;CHECK(!resources.open(tmp));
    image::Image letters,qu;CHECK(!resources.image("Textures/Letters.bmp",letters));
    CHECK(!resources.image("Textures/LettersQu.bmp",qu));
    // Invented ramps expose texture-coordinate changes that a flat atlas
    // cannot catch. QU deliberately uses a different ramp.
    for(int y=0;y<512;++y)for(int x=0;x<512;++x) {
        auto* p=letters.rgba.data()+(y*512+x)*4;
        p[0]=uint8_t(x/2);p[1]=uint8_t(y/2);p[2]=uint8_t((x+y)/4);p[3]=255;
        auto* q=qu.rgba.data()+(y*512+x)*4;
        q[0]=p[1];q[1]=p[2];q[2]=p[0];q[3]=255;
    }
    test::Record record;
    for(bool master:{false,true}) {
        ClassicOptions options;options.variant=master?ClassicVariant::Master:ClassicVariant::Original;
        ClassicArtwork artwork;CHECK(!artwork.open(resources,tmp,options,{}));CHECK(artwork.active());
        const size_t side=options.side();
        for(size_t i=0;i<side*side;++i) {
            const auto p=artwork.cellCenter(i,side);CHECK(artwork.cellAt(p.x,p.y,side)==i);
        }
        CHECK(!artwork.cellAt(650,100,side));CHECK(!artwork.cellAt(-1,-1,side));
        CHECK(!artwork.cellAt(200,200,0));CHECK(artwork.cellCenter(99,side).x==-1);
        auto faces=std::vector<std::string>(side*side,"A");faces.back()="Qu";
        Board board;CHECK(!board.set(side,faces));
        Dictionary dictionary;CHECK(!resources.dictionary("Dicts/English.dic",dictionary));ClassicRound round(dictionary);CHECK(!round.board(board));
        Surface screen;screen.create(800,600,PixelFormat::RGBA32);
        auto capture=[&](const char* name,uint64_t now,const std::vector<size_t>& selected={}) {
            artwork.draw(screen,round,now,180000,letters,qu,selected,0);
            record.add(std::string(master?"master.":"original.")+name,test::Hash().bytes(screen.pixels).hex());
        };
        capture("ready",0);CHECK(!round.start(0));capture("start",0);capture("half",90000,{0,1});
        CHECK(round.submit("AAAAA",90001)==Submission::UnknownWord);capture("pending",90001);
        round.finish();CHECK(!round.decide(DictionaryChoice::NoAll));capture("results",180000);
        CHECK(!round.start(0,0));capture("untimed",90000);
        round.finish();CHECK(!round.start(0));
        options.autoOrient=false;CHECK(!artwork.open(resources,tmp,options,{}));
        capture("quarter-turns",0,{side*side-1}); // includes the distinct Qu atlas
    }
    record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/layout-record.txt","boggle.unit.layout","tests/engines/boggle/unit/layout.cpp");
}
void fallback(const std::filesystem::path& tmp) {
    fixtures::presentationInstall(tmp);Resources resources;CHECK(!resources.open(tmp));
    ClassicOptions options;ClassicArtwork artwork;
    CHECK(!artwork.open(resources,tmp,options,{}));CHECK(!artwork.active());
    fixtures::write(tmp / "Bitmaps/ClsO1W.bmp",fixtures::bitmap());
    CHECK(bool(artwork.open(resources,tmp,options,{})));CHECK(!artwork.active());
    options.artwork=false;CHECK(!artwork.open(resources,tmp,options,{}));CHECK(!artwork.active());
    fixtures::artworkInstall(tmp);options.artwork=true;
    fixtures::write(tmp / "Bitmaps/ClsO1W.bmp",fixtures::bitmap());
    CHECK(bool(artwork.open(resources,tmp,options,{})));CHECK(!artwork.active());
}
void mouse(const std::filesystem::path& tmp) {
    fixtures::artworkInstall(tmp / "install");Resources resources;CHECK(!resources.open(tmp / "install"));
    ClassicOptions options;options.variant=ClassicVariant::Master;
    ClassicArtwork artwork;CHECK(!artwork.open(resources,tmp / "install",options,{}));
    GameTarget target;target.path=tmp / "install";target.savePath=tmp / "save";
    target.settings={{"classic_seed","6551"},{"classic_variant","master"},{"classic_animations","0"},{"classic_sound","0"}};
    fixtures::ScriptedSystem sys;
    sys.events=[&](size_t frame) -> std::vector<Event> {
        if(frame==1)return {fixtures::keyEvent(key::Return)};
        if(frame==2) {
            std::vector<Event> events;
            for(size_t i=20;i<24;++i) {auto p=artwork.cellCenter(i,5);events.push_back(fixtures::mouseEvent(EventType::MouseDown,p.x,p.y));}
            return events;
        }
        if(frame==3)return {fixtures::keyEvent(key::Return)};
        if(frame==4)return {fixtures::keyEvent(key::F1)};
        if(frame==5)return {fixtures::keyEvent(key::Return)};
        if(frame==6)return {fixtures::keyEvent(key::Escape)};
        return {};
    };
    ClassicGame game(sys,target);CHECK(!game.run());CHECK(sys.hashes.size()==6);
    test::Record record;for(size_t i=0;i<sys.hashes.size();++i)record.add("frame."+std::to_string(i),sys.hashes[i]);
    record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/layout-mouse-record.txt","boggle.unit.layout","tests/engines/boggle/unit/layout.cpp");
    CHECK(sys.textStopped);
}
void orientationGame(const std::filesystem::path& tmp) {
    fixtures::artworkInstall(tmp / "install");
    auto atlas=fixtures::atlas();
    for(size_t y=0;y<512;++y)for(size_t x=0;x<512;++x) {
        const size_t at=54+(y*512+x)*3;
        atlas[at]=uint8_t(x/2);atlas[at+1]=uint8_t(y/2);atlas[at+2]=uint8_t((x+y)/4);
    }
    fixtures::write(tmp / "install/Textures/Letters.bmp",atlas);
    fixtures::write(tmp / "install/Textures/LettersQu.bmp",atlas);
    test::Record record;
    for(bool master:{false,true})for(bool artwork:{false,true}) {
        std::vector<std::string> alignedPanel;
        for(bool autoOrient:{true,false}) {
            GameTarget target;target.path=tmp / "install";target.savePath=tmp / "save";
            target.settings={{"classic_seed","6551"},{"classic_variant",master?"master":"original"},{"classic_artwork",artwork?"1":"0"},
                             {"classic_auto_orient",autoOrient?"1":"0"},{"classic_animations","0"},{"classic_sound","0"}};
            fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
            sys.events=[](size_t frame) -> std::vector<Event> {
                if(frame==1 || frame==4 || frame==7)return {fixtures::keyEvent(key::Return)};
                if(frame==2)return {fixtures::textEvent("AAAA"),fixtures::keyEvent(key::Return)};
                if(frame==3 || frame==5)return {fixtures::keyEvent(key::F1)};
                if(frame==6)return {fixtures::keyEvent(key::F1+1)};
                if(frame==8)return {fixtures::keyEvent(key::Escape)};
                return {};
            };
            std::vector<std::string> panels;
            sys.present=[&](const Surface& screen,size_t) {
                test::Hash hash;const int x=artwork?621:430;
                for(int y=0;y<480;++y)hash.bytes(screen.row(y)+x*4,size_t(screen.w-x)*4);
                panels.push_back(hash.hex());
            };
            ClassicGame game(sys,target);CHECK(!game.run());CHECK(sys.hashes.size()==8);
            if(sys.hashes.size()!=8)continue;
            CHECK(sys.hashes[1]==sys.hashes[4]); // Replay retains orientations.
            CHECK((sys.hashes[1]==sys.hashes[7])==autoOrient); // New seed changes only rotated view.
            if(autoOrient)alignedPanel=panels;else CHECK(panels==alignedPanel); // same words, score and time
            const std::string prefix=std::string(master?"master.":"original.")+(artwork?"art.":"plain.")+(autoOrient?"aligned.":"rotated.");
            for(size_t i=0;i<sys.hashes.size();++i)record.add(prefix+std::to_string(i),sys.hashes[i]);
        }
    }
    record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/orientation-record.txt","boggle.unit.layout","tests/engines/boggle/unit/layout.cpp");
}
void handwriting(const std::filesystem::path& tmp) {
    fixtures::artworkInstall(tmp);Resources resources;CHECK(!resources.open(tmp));
    Dictionary dictionary;CHECK(!resources.dictionary("Dicts/English.dic",dictionary));
    image::Image letters,qu;CHECK(!resources.image("Textures/Letters.bmp",letters));CHECK(!resources.image("Textures/LettersQu.bmp",qu));
    Board board;CHECK(!board.set(4,std::vector<std::string>(16,"A")));
    ClassicRound round(dictionary);CHECK(!round.board(board));CHECK(!round.start(0));
    CHECK(round.submit("AAAA",1)==Submission::Accepted);
    CHECK(round.submit("AAAAA",2)==Submission::UnknownWord);
    CHECK(round.submit(std::string(16,'A'),3)==Submission::UnknownWord);
    Surface screen;screen.create(800,600,PixelFormat::RGBA32);ClassicArtwork artwork;ClassicOptions options;
    auto capture=[&]{artwork.draw(screen,round,10,180000,letters,qu,{},0);return test::Hash().bytes(screen.pixels).hex();};
    CHECK(!artwork.open(resources,tmp,options,{}));const auto fallback=capture();
    fixtures::write(tmp / "Fonts/KPSI.TTF",test::fixtures::asciiTrueType());CHECK(!resources.open(tmp));
    CHECK(!artwork.open(resources,tmp,options,{}));const auto installed=capture();CHECK(installed!=fallback);
    options.handwriting=false;CHECK(!artwork.open(resources,tmp,options,{}));CHECK(capture()==fallback);
    fixtures::write(tmp / "Fonts/KPSI.TTF","malformed");CHECK(!resources.open(tmp));options.handwriting=true;
    CHECK(bool(artwork.open(resources,tmp,options,{})));CHECK(!artwork.active());
    options.handwriting=false;CHECK(!artwork.open(resources,tmp,options,{}));CHECK(capture()==fallback);
    test::Record record;record.add("fallback",fallback);record.add("handwriting",installed);
    record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/handwriting-record.txt","boggle.unit.layout","tests/engines/boggle/unit/layout.cpp");
}
}
static const test::Suite suite("boggle.unit.layout",{
    {"clockwise atlas sampling and independent per-board orientations",[](const auto&){orientation();}},
    {"plane projection, analytic perspective and invalid geometry",[](const auto&){projection();}},
    {"source-derived timer progression and bounds",[](const auto&){progress();}},
    {"variant projection, selected cells and round render states",[](const auto& tmp){rendering(tmp);}},
    {"legacy imports, partial bundles, malformed dimensions and disabled artwork",[](const auto& tmp){fallback(tmp);}},
    {"Master last-row mouse entry, scoring, results and replay",[](const auto& tmp){mouse(tmp);}},
    {"both variants and renderers preserve scoring, replay and new-board behavior",[](const auto& tmp){orientationGame(tmp);}},
    {"optional handwriting, word fitting, rejected words and corrupt-font fallback",[](const auto& tmp){handwriting(tmp);}},
});

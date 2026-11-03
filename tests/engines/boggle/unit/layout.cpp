// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/layout.h"
#include "engines/boggle/classic/game.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
using namespace reluva;
using namespace reluva::boggle;
namespace {
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
    target.settings={{"classic_variant","master"},{"classic_animations","0"},{"classic_sound","0"}};
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
}
static const test::Suite suite("boggle.unit.layout",{
    {"plane projection, analytic perspective and invalid geometry",[](const auto&){projection();}},
    {"source-derived timer progression and bounds",[](const auto&){progress();}},
    {"variant projection, selected cells and round render states",[](const auto& tmp){rendering(tmp);}},
    {"legacy imports, partial bundles, malformed dimensions and disabled artwork",[](const auto& tmp){fallback(tmp);}},
    {"Master last-row mouse entry, scoring, results and replay",[](const auto& tmp){mouse(tmp);}},
});

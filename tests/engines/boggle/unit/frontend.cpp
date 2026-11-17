// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/registry.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/frontend/layout.h"
#include "engines/boggle/frontend/name_dialog.h"
#include "engines/boggle/frontend/roster_dialog.h"
#include "engines/boggle/frontend/options_dialog.h"
#include "engines/boggle/classic/highscores.h"
#include "engines/boggle/classic/player.h"
#include "engines/boggle/frontend/media.h"
#include "tests/engines/boggle/dialog_fixture.h"
#include <limits>
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include "tests/support/cinepak_fixture.h"
using namespace reluva;
using namespace reluva::boggle;
namespace {
void artwork(const std::filesystem::path& path) {
    std::vector<uint8_t> bmp(54+640*480*3);
    bmp[0]='B';bmp[1]='M';
    test::fixtures::put32(bmp,2,uint32_t(bmp.size()));test::fixtures::put32(bmp,10,54);
    test::fixtures::put32(bmp,14,40);test::fixtures::put32(bmp,18,640);test::fixtures::put32(bmp,22,480);
    test::fixtures::put16(bmp,26,1);test::fixtures::put16(bmp,28,24);
    for(size_t i=54;i<bmp.size();++i) bmp[i]=uint8_t(i%173);
    fixtures::write(path/"Bitmaps/Select1.bmp",bmp);
}
GameTarget target(const std::filesystem::path& tmp) {
    fixtures::masterInstall(tmp/"install");
    GameTarget t;t.path=tmp/"install";t.savePath=tmp/"saves";t.gameId="boggle1997";
    t.settings={{"classic_seed","6551"},{"classic_sound","0"},{"classic_animations","0"}};return t;
}
void navigation(const std::filesystem::path& tmp) {
    auto t=target(tmp);artwork(t.path);
    fixtures::ScriptedSystem sys;
    sys.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==1)return {keyEvent(key::Return),keyEvent(key::Return)};
        if(f==2)return {keyEvent('2'),keyEvent(key::Right),keyEvent('t'),keyEvent(key::Return)};
        if(f==3 || f==4 || f==10)return {keyEvent(key::Escape)};
        if(f==5 || f==6 || f==7 || f==12 || f==14)return {keyEvent(key::Return)};
        if(f==8)return {keyEvent(key::Escape),textEvent("aaaa"),keyEvent(key::Return)};
        if(f==9)return {keyEvent(key::F1)};
        if(f==11 || f==15)return {keyEvent(key::F1+2)};
        if(f==13)return {keyEvent('1'),keyEvent(key::Return)};
        if(f==16)return {keyEvent('l')};
        return {};
    };
    const auto* meta=findEngine("boggle");CHECK(meta);if(!meta)return;
    auto game=meta->create(sys,t);if(auto err=game->run())return test::fail(err.message);
    CHECK(sys.frame==16 && sys.w==640 && sys.h==480 && !sys.textEnabled);
    CHECK(!sys.audioStarted);
    if(sys.hashes.size()==16) {
        CHECK(sys.hashes[0]==sys.hashes[3] && sys.hashes[0]==sys.hashes[4]); // Players Escape / main Escape
        CHECK(sys.hashes[0]==sys.hashes[11] && sys.hashes[0]==sys.hashes[15]);
        CHECK(sys.hashes[1]!=sys.hashes[5]); // Master / untimed selection preserved
        CHECK(sys.hashes[2]==sys.hashes[6]); // same Players confirmation
        CHECK(sys.hashes[7]!=sys.hashes[8]); // Escape did not interrupt word submission
        CHECK(sys.hashes[9]==sys.hashes[10]); // Escape also leaves the native results visible
        CHECK(sys.hashes[7]!=sys.hashes[14]); // Master and Original geometry
        test::Record rec;
        for(size_t i=0;i<sys.hashes.size();++i) rec.add("navigation."+std::to_string(i),sys.hashes[i]);
        rec.check(std::filesystem::path(RELUVA_SOURCE_DIR)/"tests/engines/boggle/data/frontend-record.txt",
                  "boggle.unit.frontend","tests/engines/boggle/unit/frontend.cpp");
    }
}
void mouseAndGuards(const std::filesystem::path& tmp) {
    auto t=target(tmp); // old imports with no Select1 art use native fallback
    fixtures::ScriptedSystem sys;
    sys.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==1) {auto repeat=keyEvent(key::Return);repeat.repeat=true;return {repeat};}
        if(f==2)return {mouseEvent(EventType::MouseDown,50,300)};
        if(f==3)return {mouseEvent(EventType::MouseDown,70,400),keyEvent(key::Return)};
        if(f==4)return {mouseEvent(EventType::MouseDown,210,300)}; // unavailable mode
        if(f==5)return {mouseEvent(EventType::MouseDown,480,430)};
        return {};
    };
    auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run());CHECK(sys.frame==5);
    if(sys.hashes.size()==5) {
        CHECK(sys.hashes[0]==sys.hashes[1] && sys.hashes[0]==sys.hashes[3] && sys.hashes[0]==sys.hashes[4]);
        CHECK(sys.hashes[0]!=sys.hashes[2]);
    }
    // Mouse selects Master, confirms Players and starts; global quit
    // must leave the whole engine rather than re-opening a menu.
    for(const auto quitType : {EventType::Quit,EventType::ReturnToLauncher,EventType::None}) {
        fixtures::ScriptedSystem quit;
        quit.events=[quitType](size_t f)->std::vector<Event>{
            using namespace fixtures;
            if(f==1)return {mouseEvent(EventType::MouseDown,50,300)};
            if(f==2)return {mouseEvent(EventType::MouseDown,360,180),mouseEvent(EventType::MouseDown,430,270),mouseEvent(EventType::MouseDown,360,400)};
            if(f==3)return {mouseEvent(EventType::MouseDown,360,400)};
            if(f==4){if(quitType==EventType::None)return {mouseEvent(EventType::MouseDown,620,565)};Event e;e.type=quitType;return {e};}
            if(f==5)return {keyEvent('l')};
            return {};
        };
        game=findEngine("boggle")->create(quit,t);CHECK(!game->run());
        CHECK(quit.frame==(quitType==EventType::None ? 5 : 4) && quit.w==(quitType==EventType::None ? 640 : 800) && quit.textStopped);
    }
}
void escapePages(const std::filesystem::path& tmp) {
    auto t=target(tmp);fixtures::ScriptedSystem sys;
    sys.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==1 || f==3 || f==6)return {keyEvent(key::Escape)};
        if(f==2 || f==4 || f==5)return {keyEvent(key::Return)};
        if(f==7)return {keyEvent('l')};
        return {};
    };
    auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run());CHECK(sys.frame==7);
    if(sys.hashes.size()==7) {
        CHECK(sys.hashes[0]==sys.hashes[1] && sys.hashes[0]==sys.hashes[3] && sys.hashes[0]==sys.hashes[6]);
        CHECK(sys.hashes[2]==sys.hashes[4] && sys.hashes[4]!=sys.hashes[5]);
    }
}

void musicLifetime(const std::filesystem::path& tmp) {
    auto t=target(tmp);t.settings["classic_music"]="random";
    for(size_t i=0;i<classicMusicFiles.size();++i) {
        auto wave=fixtures::wave();test::fixtures::put16(wave,44,uint16_t(100+i));
        fixtures::write(t.path/"Waves"/(std::string(classicMusicFiles[i])+".wav"),wave);
    }
    fixtures::ScriptedSystem sys;sys.audioSupported=true;
    sys.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==1 || f==2 || f==3 || f==5 || f==6 || f==7)return {keyEvent(key::Return)};
        if(f==4 || f==8)return {keyEvent(key::F1+2)};
        if(f==9)return {keyEvent('l')};
        return {};
    };
    auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run());
    CHECK(sys.frame==9 && sys.audioStopped && !sys.audioCallback);
    CHECK(sys.audioHashes.size()==2);
    if(sys.audioHashes.size()==2) CHECK(sys.audioHashes[0]==sys.audioHashes[1]);
}
void movies(const std::filesystem::path& tmp) {
    auto t=target(tmp);using test::fixtures::grayMovie;
    fixtures::write(t.path/"Anims/INTRO.AVI",grayMovie(300,180));
    fixtures::write(t.path/"Anims/ClsBut8.avi",grayMovie(100,100,false));
    fixtures::ScriptedSystem sys;sys.audioSupported=true;sys.audioRequiresGraphics=true;
    sys.clock=[](size_t f)->uint64_t{return f*100;};
    sys.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==2)return {keyEvent(key::Return)}; // ignored on natural intro completion
        if(f==3)return {keyEvent(key::Escape)}; // main menu Escape remains inactive
        if(f==5)return {keyEvent('l')};
        return {};
    };
    sys.present=[&](const Surface& screen,size_t f){
        if(f<2){const auto* p=screen.row(60)+20*4;CHECK(p[0]==(f?90:30) && p[3]==255);}
        if(f==2)CHECK(!sys.audioCallback);
    };
    auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run());CHECK(sys.frame==5);
    CHECK(sys.audioStarted && sys.audioStopped && !sys.audioCallback && !sys.textEnabled);
    if(sys.hashes.size()==5) {
        CHECK(sys.hashes[2]!=sys.hashes[3] && sys.hashes[2]==sys.hashes[4]);
        test::Record record;
        for(size_t i=0;i<sys.hashes.size();++i)record.add("movie."+std::to_string(i),sys.hashes[i]);
        record.check(std::filesystem::path(RELUVA_SOURCE_DIR)/"tests/engines/boggle/data/movie-frontend-record.txt",
                     "boggle.unit.frontend","tests/engines/boggle/unit/frontend.cpp");
    }
    fixtures::ScriptedSystem skip;skip.clock=[](size_t f)->uint64_t{return f*10;};
    skip.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==0){auto e=keyEvent(key::Escape);e.repeat=true;return {e};}
        if(f==1)return {keyEvent(key::Escape),keyEvent(key::Return)};
        if(f==2)return {keyEvent('l')};
        return {};
    };
    game=findEngine("boggle")->create(skip,t);CHECK(!game->run());
    CHECK(skip.frame==2 && !skip.audioStarted && !skip.audioCallback);
    if(skip.hashes.size()==2 && sys.hashes.size()==5)CHECK(skip.hashes[1]==sys.hashes[2]);
    for(auto type:{EventType::Quit,EventType::ReturnToLauncher}) {
        fixtures::ScriptedSystem quit;quit.audioSupported=true;
        quit.events=[type](size_t f)->std::vector<Event>{if(f==1){Event e;e.type=type;return {e};}return {};};
        game=findEngine("boggle")->create(quit,t);CHECK(!game->run());
        CHECK(quit.frame==1 && quit.audioStopped && !quit.audioCallback);
    }
    fixtures::write(t.path/"Anims/INTRO.AVI","broken");
    fixtures::ScriptedSystem bad;game=findEngine("boggle")->create(bad,t);CHECK(bool(game->run()) && !bad.audioStarted);
    t.settings["frontend_movies"]="0";
    bad.events=[](size_t)->std::vector<Event>{return {fixtures::keyEvent('l')};};
    game=findEngine("boggle")->create(bad,t);CHECK(!game->run() && !bad.audioStarted);
    t.settings.erase("frontend_movies");fixtures::write(t.path/"Anims/ClsBut8.avi",grayMovie(4,4,false));
    fixtures::ScriptedSystem dimensions;game=findEngine("boggle")->create(dimensions,t);
    CHECK(bool(game->run()) && dimensions.w==0);
}

void publisherSequence(const std::filesystem::path& tmp) {
    auto t=target(tmp);
    fixtures::write(t.path/"Anims/Hasbro.avi",test::fixtures::grayMovie(320,240));
    fixtures::write(t.path/"Anims/INTRO.AVI",test::fixtures::grayMovie(300,180));
    fixtures::ScriptedSystem natural;natural.audioSupported=true;
    natural.clock=[](size_t f)->uint64_t{return f*100;};
    natural.events=[](size_t f)->std::vector<Event>{if(f==15)return {fixtures::keyEvent('l')};return {};};
    natural.present=[&](const Surface& screen,size_t f){
        if(f<2){CHECK(screen.pixels[0]==(f?90:30));CHECK(bool(natural.audioCallback));}
        if(f>=2 && f<12){CHECK(!natural.audioCallback);CHECK(screen.pixels[0]==0);}
        if(f==12 || f==13){const auto* p=screen.row(60)+20*4;CHECK(p[0]==(f==12?30:90));CHECK(bool(natural.audioCallback));}
        if(f==14)CHECK(!natural.audioCallback);
    };
    auto game=findEngine("boggle")->create(natural,t);CHECK(!game->run());
    CHECK(natural.frame==15 && natural.audioHashes.size()==4 && !natural.audioCallback);
    // Skipping either the publisher or the intervening gap skips the complete
    // remaining sequence, even when the later game movie is malformed.
    fixtures::write(t.path/"Anims/INTRO.AVI","not decoded after skip");
    for(size_t skipFrame:{size_t(0),size_t(2),size_t(3)}) {
        fixtures::ScriptedSystem skip;skip.audioSupported=true;
        skip.clock=[](size_t f)->uint64_t{return f*100;};
        skip.events=[skipFrame](size_t f)->std::vector<Event>{
            if(f==skipFrame)return {fixtures::keyEvent(key::Escape),fixtures::keyEvent(key::Return)};
            if(f==skipFrame+1)return {fixtures::keyEvent('l')};
            return {};
        };
        game=findEngine("boggle")->create(skip,t);CHECK(!game->run());
        CHECK(skip.frame==skipFrame+1 && !skip.audioCallback);
        if(natural.hashes.size()==15 && skip.hashes.size()==skipFrame+1)CHECK(skip.hashes.back()==natural.hashes[14]);
    }
    fixtures::write(t.path/"Anims/INTRO.AVI",test::fixtures::grayMovie(300,180));
    for(auto type:{EventType::Quit,EventType::ReturnToLauncher})for(size_t frame:{size_t(1),size_t(3),size_t(13)}) {
        fixtures::ScriptedSystem quit;quit.audioSupported=true;
        quit.clock=[](size_t f)->uint64_t{return f*100;};
        quit.events=[frame,type](size_t f)->std::vector<Event>{if(f==frame){Event e;e.type=type;return {e};}return {};};
        game=findEngine("boggle")->create(quit,t);CHECK(!game->run());CHECK(quit.frame==frame && !quit.audioCallback);
    }
    fixtures::write(t.path/"Anims/Hasbro.avi","broken");
    fixtures::ScriptedSystem invalid;game=findEngine("boggle")->create(invalid,t);CHECK(bool(game->run()));
    t.settings["frontend_movies"]="0";invalid.events=[](size_t)->std::vector<Event>{return {fixtures::keyEvent('l')};};
    game=findEngine("boggle")->create(invalid,t);CHECK(!game->run());
}

void selectionLayouts(){
    FrontendSelectionLayout layout;CHECK(!layout.read({}) && !layout.installed() && layout.modes()[0].contains(50,390));
    for(bool extended:{false,true}){
        const auto f=fixtures::selectionDialog(extended);std::vector<PeResource> own;
        CHECK(!readPeResources(test::fixtures::resourcePe(f.bytes,5,10813),own));
        CHECK(!layout.read(own) && layout.installed());CHECK(layout.modes()[0].contains(30,250) && !layout.modes()[0].contains(50,390));
        CHECK(!layout.read(own,0x0411) && !layout.installed());CHECK(!layout.read(own));
        auto bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?20:16),15005);CHECK(bool(layout.read(bad)) && !layout.installed());
        bad=own;test::fixtures::put16(bad[0].data,f.controls[0]+(extended?24:18)+2,0x80);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[0]+(extended?12:8),399);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[0]+(extended?12:8),250);CHECK(bool(layout.read(bad))); // modes overlap
        bad=own;test::fixtures::put16(bad[0].data,f.controls[0]+(extended?12:8)+2,200);CHECK(bool(layout.read(bad))); // native navigation clearance
        bad=own;bad[0].data.resize(19);CHECK(bool(layout.read(bad)));
    }
}
void selectionNavigation(const std::filesystem::path& tmp){
    for(bool extended:{false,true}){
        auto t=target(tmp/(extended?"extended":"standard"));const auto f=fixtures::selectionDialog(extended);
        fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage(f.bytes,10813));
        fixtures::ScriptedSystem sys;sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==1)return {mouseEvent(EventType::MouseDown,50,390)}; // retired Classic target
            if(f==2)return {mouseEvent(EventType::MouseDown,30,250),keyEvent(key::Return)};
            if(f==3)return {keyEvent('2')};
            if(f==4 || f==5)return {keyEvent(key::Return)};
            if(f==6)return {keyEvent(key::F1+2),keyEvent(key::Return)};
            if(f==7)return {mouseEvent(EventType::MouseDown,160,250)}; // unavailable mode
            if(f==8)return {keyEvent('l')};
            return {};
        };
        auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run() && sys.frame==8);
        if(sys.hashes.size()==8){CHECK(sys.hashes[0]==sys.hashes[1] && sys.hashes[0]==sys.hashes[6] && sys.hashes[0]==sys.hashes[7]);CHECK(sys.hashes[2]!=sys.hashes[3] && sys.hashes[3]!=sys.hashes[4] && sys.hashes[4]!=sys.hashes[5]);}
        fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage({1,2,3},10813));
        fixtures::ScriptedSystem bad;bad.events=[](size_t){return std::vector<Event>{fixtures::keyEvent('l')};};game=findEngine("boggle")->create(bad,t);CHECK(bool(game->run()));
        t.settings["classic_artwork"]="0";game=findEngine("boggle")->create(bad,t);CHECK(!game->run());
    }
}
void fittedMovies(const std::filesystem::path& tmp){
    fixtures::write(tmp/"Anims/ClsBut8.avi",test::fixtures::grayMovie(100,100,false));Resources resources;CHECK(!resources.open(tmp));
    FrontendMedia media;CHECK(!media.open(resources,tmp,true));Surface screen;screen.create(40,30,PixelFormat::RGBA32);
    for(const auto b:{SelectionRect{5,4,20,10},SelectionRect{-5,-3,20,16},SelectionRect{std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),std::numeric_limits<int>::max()},SelectionRect{std::numeric_limits<int>::min(),std::numeric_limits<int>::min(),20,20}}){
        std::fill(screen.pixels.begin(),screen.pixels.end(),17);CHECK(media.buttonFit(screen,0,b,0));size_t changed=0;
        for(int y=0;y<screen.h;++y)for(int x=0;x<screen.w;++x)if(screen.row(y)[x*4]!=17){++changed;CHECK(b.contains(x,y) && screen.row(y)[x*4]==30 && screen.row(y)[x*4+3]==255);}
        if(b.x==5)CHECK(changed==100 && screen.row(4)[10*4]==30 && screen.row(4)[9*4]==17);
        else if(b.x==-5)CHECK(changed>0 && changed<256);
        else CHECK(!changed);
    }
    CHECK(media.buttonFit(screen,0,{0,0,10,10},100) && screen.row(0)[0]==90);
    CHECK(!media.buttonFit(screen,0,{0,0,0,10},0) && !media.buttonFit(screen,5,{0,0,10,10},0));
    CHECK(!media.open(resources,tmp,false) && !media.buttonFit(screen,0,{0,0,10,10},0));
}

void nameLayouts(){
    FrontendNameDialog layout;CHECK(!layout.read({}) && !layout.installed() && layout.field().contains(100,200) && layout.save().contains(280,310));
    for(bool extended:{false,true}){
        const auto f=fixtures::nameDialog(extended);std::vector<PeResource> own;CHECK(!readPeResources(test::fixtures::resourcePe(f.bytes,5,1600),own));
        CHECK(!layout.read(own) && layout.installed() && layout.save().contains(480,300) && layout.cancel().contains(100,300) && !layout.save().contains(280,310));
        CHECK(!layout.read(own,0x0411) && !layout.installed());CHECK(!layout.read(own));
        Surface screen;screen.create(640,480,PixelFormat::RGBA32);std::fill(screen.pixels.begin(),screen.pixels.end(),17);NameEdit edit;CHECK(!edit.begin("ABCDEFGHIJKLMNOPQR"));layout.draw(screen,edit,"Own validation error");CHECK(screen.row(0)[0]==17 && screen.row(184)[320*4+3]==255);
        CHECK(screen.row(400)[100*4+3]==17 && screen.row(444)[40*4+3]==255); // help stays in the footer
        const auto selected=screen.pixels;edit.end(false);layout.draw(screen,edit,"Own validation error");CHECK(selected!=screen.pixels);
        auto bad=own;test::fixtures::put16(bad[0].data,f.controls[2]+(extended?20:16),1601);CHECK(bool(layout.read(bad)) && !layout.installed());
        bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?24:18)+2,0x82);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?12:8),259);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?12:8)+4,100);CHECK(bool(layout.read(bad))); // name will not fit
        bad=own;test::fixtures::put16(bad[0].data,f.controls[2]+(extended?12:8)+2,40);CHECK(bool(layout.read(bad))); // Save overlaps field
        bad=own;test::fixtures::put16(bad[0].data,f.controls[2]+(extended?12:8),20);CHECK(bool(layout.read(bad))); // Cancel clearance
        bad=own;bad[0].data.resize(19);CHECK(bool(layout.read(bad)));
    }
}
void nameNavigation(const std::filesystem::path& tmp){
    for(bool extended:{false,true}){
        auto t=target(tmp/(extended?"extended":"standard"));const auto f=fixtures::nameDialog(extended);
        fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage(f.bytes,1600));std::vector<PeResource> own;CHECK(!readPeResources(test::fixtures::resourcePe(f.bytes,5,1600),own));FrontendNameDialog layout;CHECK(!layout.read(own));
        auto click=[](SelectionRect b){return fixtures::mouseEvent(EventType::MouseDown,b.x+b.w/2,b.y+b.h/2);};
        fixtures::ScriptedSystem sys;sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==1)return {keyEvent(key::Return)};
            if(f==2 || f==7 || f==10)return {keyEvent('n')};
            if(f==3)return {textEvent("ABCDEFGHIJKLMNOPQR")};
            if(f==4)return {mouseEvent(EventType::MouseDown,280,310)}; // retired Save target
            if(f==5)return {click(layout.field()),textEvent("New Name")};
            if(f==6)return {click(layout.save()),keyEvent(key::Return)}; // save cannot also start a round
            if(f==8)return {textEvent("Discarded")};
            if(f==9)return {click(layout.cancel()),keyEvent(key::Return)};
            if(f==11)return {textEvent("  ")};
            if(f==12)return {keyEvent(key::Return)};
            if(f==13)return {click(layout.field()),textEvent("Second Name")};
            if(f==14)return {keyEvent(key::Return)};
            if(f==15)return {keyEvent(key::Escape)};
            if(f==16)return {keyEvent('l')};
            return {};
        };
        auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run() && sys.frame==16 && sys.w==640 && sys.textStopped);
        if(sys.hashes.size()==16){CHECK(sys.hashes[3]==sys.hashes[4] && sys.hashes[6]==sys.hashes[9]);CHECK(sys.hashes[11]!=sys.hashes[12] && sys.hashes[12]!=sys.hashes[13]);}
        PlayerProfile profile;CHECK(!profile.load(t.savePath,t.path) && profile.name()=="Second Name" && profile.history().size()==3);
        CHECK(std::find(profile.history().begin(),profile.history().end(),"New Name")!=profile.history().end());CHECK(std::find(profile.history().begin(),profile.history().end(),"Discarded")==profile.history().end());
        fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage({1,2,3},1600));fixtures::ScriptedSystem bad;bad.events=[](size_t){return std::vector<Event>{fixtures::keyEvent('l')};};game=findEngine("boggle")->create(bad,t);CHECK(bool(game->run()));
        t.settings["classic_artwork"]="0";game=findEngine("boggle")->create(bad,t);CHECK(!game->run());
    }
}

void rosterLayouts(){
    FrontendRosterDialog layout;CHECK(!layout.read({}) && !layout.installed() && layout.fields()[0].contains(550,160) && layout.done().contains(400,410));
    for(bool extended:{false,true}){
        const auto f=fixtures::rosterDialog(extended);std::vector<PeResource> own;CHECK(!readPeResources(test::fixtures::resourcePe(f.bytes,5,10000),own));
        CHECK(!layout.read(own) && layout.installed());CHECK(!layout.read(own,0x0411) && !layout.installed());CHECK(!layout.read(own));
        Surface screen;screen.create(640,480,PixelFormat::RGBA32);layout.draw(screen,{"ABCDEFGHIJKLMNOPQR","CPU Mr. Boggle","Sam",""},{ComputerLevel::Human,ComputerLevel::MrBoggle,ComputerLevel::Human,ComputerLevel::Human},{});const auto before=screen.pixels;layout.draw(screen,{"Alex","CPU Mr. Boggle","Sam",""},{ComputerLevel::Human,ComputerLevel::MrBoggle,ComputerLevel::Human,ComputerLevel::Human},{});CHECK(before!=screen.pixels);
        auto bad=own;test::fixtures::put16(bad[0].data,f.controls[2]+(extended?20:16),4107);CHECK(bool(layout.read(bad)) && !layout.installed());
        bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?24:18)+2,0x81);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?12:8)+6,513);CHECK(bool(layout.read(bad))); // unreasonable popup size
        bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?12:8)+2,155);CHECK(bool(layout.read(bad))); // collapsed face outside client
        bad=own;test::fixtures::put16(bad[0].data,f.controls[2]+(extended?12:8),12);CHECK(bool(layout.read(bad))); // fields overlap
        bad=own;test::fixtures::put16(bad[0].data,f.controls[6]+(extended?12:8),160);CHECK(bool(layout.read(bad))); // buttons overlap
        bad=own;bad[0].data.resize(19);CHECK(bool(layout.read(bad)));
    }
}
void rosterNavigation(const std::filesystem::path& tmp){
    for(bool extended:{false,true}){
        auto t=target(tmp/(extended?"extended":"standard"));const auto f=fixtures::rosterDialog(extended);fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage(f.bytes,10000));
        std::vector<PeResource> own;CHECK(!readPeResources(test::fixtures::resourcePe(f.bytes,5,10000),own));FrontendRosterDialog layout;CHECK(!layout.read(own));
        PlayerProfile profile;CHECK(!profile.load(t.savePath,t.path));for(const char* name:{"Alex","Sam","ABCDEFGHIJKLMNOPQR"})CHECK(!profile.rename(name));
        auto click=[](SelectionRect b){return fixtures::mouseEvent(EventType::MouseDown,b.x+b.w/2,b.y+b.h/2);};
        fixtures::ScriptedSystem sys;sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==1)return {keyEvent(key::Return)};
            if(f==2 || f==13 || f==15 || f==19 || f==23)return {keyEvent('m')};
            if(f==3)return {mouseEvent(EventType::MouseDown,550,160)}; // obsolete row target
            if(f==4)return {click(layout.fields()[1])};
            if(f==5)return {keyEvent(key::End),keyEvent(key::Up),keyEvent(key::Return)}; // Mr. Boggle
            if(f==6)return {click(layout.fields()[2])};
            if(f==7)return {keyEvent(key::Home),keyEvent(key::Down),keyEvent(key::Return)}; // Sam
            if(f==8 || f==16 || f==20)return {click(layout.fields()[3])};
            if(f==9 || f==21)return {keyEvent(key::End),keyEvent(key::Up),keyEvent(key::Up),keyEvent(key::Up),keyEvent(key::Up),keyEvent(key::Return)}; // Beginner
            if(f==10)return {click(layout.fields()[0])};
            if(f==11)return {keyEvent(key::Home),keyEvent(key::Down),keyEvent(key::Down),keyEvent(key::Return)}; // Alex
            if(f==12)return {click(layout.done()),keyEvent(key::Return)};
            if(f==14)return {click(layout.cancel()),keyEvent(key::Return)};
            if(f==17)return {keyEvent(key::End),keyEvent(key::Return)}; // empty optional slot
            if(f==18 || f==24)return {keyEvent(key::Return)};
            if(f==22 || f==25)return {keyEvent(key::Escape)};
            if(f==26)return {keyEvent('l')};
            return {};
        };
        auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run() && sys.frame==26 && sys.w==640 && sys.textStopped);
        if(sys.hashes.size()==26){CHECK(sys.hashes[2]==sys.hashes[3]);CHECK(sys.hashes[11]==sys.hashes[13] && sys.hashes[11]==sys.hashes[21]);CHECK(sys.hashes[12]==sys.hashes[14]);CHECK(sys.hashes[17]==sys.hashes[19] && sys.hashes[17]==sys.hashes[23] && sys.hashes[17]!=sys.hashes[11]);CHECK(sys.hashes[22]==sys.hashes[24] && sys.hashes[22]!=sys.hashes[12]);}
        CHECK(!profile.load(t.savePath,t.path) && profile.name()=="Alex" && profile.history().size()==4);
        fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage({1,2,3},10000));fixtures::ScriptedSystem bad;bad.events=[](size_t){return std::vector<Event>{fixtures::keyEvent('l')};};game=findEngine("boggle")->create(bad,t);CHECK(bool(game->run()));
        t.settings["classic_artwork"]="0";game=findEngine("boggle")->create(bad,t);CHECK(!game->run());
    }
}

void rosterCancellation(const std::filesystem::path& tmp){
    for(int mode=0;mode<3;++mode){
        auto t=target(tmp/std::to_string(mode));FrontendRosterDialog layout;
        if(mode){const auto f=fixtures::rosterDialog(mode==2);fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage(f.bytes,10000));std::vector<PeResource> own;CHECK(!readPeResources(test::fixtures::resourcePe(f.bytes,5,10000),own));CHECK(!layout.read(own));}
        PlayerProfile profile;CHECK(!profile.load(t.savePath,t.path) && !profile.rename("Alex") && !profile.rename("Sam"));const auto before=profile.history();
        auto click=[](SelectionRect b){return fixtures::mouseEvent(EventType::MouseDown,b.x+b.w/2,b.y+b.h/2);};
        fixtures::ScriptedSystem sys;sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f>=5 && f<=26){PlayerProfile disk;CHECK(!disk.load(t.savePath,t.path) && disk.name()=="Sam" && disk.history()==before);}
            if(f==0 || f==1)return {keyEvent(key::Return)};
            if(f==2 || f==12 || f==18 || f==23)return {keyEvent('m')};
            if(f==3 || f==13 || f==19 || f==24)return {keyEvent('1')};
            if(f==4 || f==20 || f==25)return {keyEvent(key::Home),keyEvent(key::Down),keyEvent(key::Return)};
            if(f==5 || f==15)return {keyEvent('2')};
            if(f==6)return {keyEvent(key::End),keyEvent(key::Up),keyEvent(key::Return)};
            if(f==7)return {keyEvent('3')};
            if(f==8 || f==16)return {keyEvent(key::Home),keyEvent(key::Return)};
            if(f==9)return {keyEvent('4')};
            if(f==10)return {keyEvent(key::End),keyEvent(key::Up),keyEvent(key::Up),keyEvent(key::Return)};
            if(f==11 || f==22){if(f==22)std::filesystem::remove(t.savePath/"Player.txt.tmp");return {click(layout.cancel()),keyEvent(key::Return)};}
            if(f==14)return {keyEvent(key::End),keyEvent(key::Return)}; // first slot becomes computer only in draft
            if(f==17)return {keyEvent(key::Escape),keyEvent(key::Return)};
            if(f==21){std::filesystem::create_directory(t.savePath/"Player.txt.tmp");return {click(layout.done()),keyEvent(key::Return)};}
            if(f==26)return {click(layout.done()),keyEvent(key::Return)};
            if(f==27)return {keyEvent(key::Escape)};
            if(f==28)return {keyEvent('l')};
            return {};
        };
        auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run() && sys.frame==28 && sys.w==640);
        if(sys.hashes.size()==28){CHECK(sys.hashes[1]==sys.hashes[11] && sys.hashes[1]==sys.hashes[17] && sys.hashes[1]==sys.hashes[22]);CHECK(sys.hashes[2]==sys.hashes[12] && sys.hashes[2]==sys.hashes[18] && sys.hashes[2]==sys.hashes[23]);CHECK(sys.hashes[20]!=sys.hashes[21] && sys.hashes[1]!=sys.hashes[26]);}
        CHECK(!profile.load(t.savePath,t.path) && profile.name()=="Alex" && profile.history().size()==3);
    }
}

void optionsLayouts(){
    FrontendOptionsDialog layout;CHECK(!layout.read({}) && !layout.installed());
    for(bool extended:{false,true}){
        const auto f=fixtures::optionsDialog(extended);std::vector<PeResource> own;CHECK(!readPeResources(test::fixtures::resourcePe(f.bytes,5,10834),own));CHECK(!layout.read(own) && layout.installed());
        const auto slider=layout.control(FrontendOptionsDialog::Slider);for(int i=0;i<9;++i)CHECK(layout.durationAt(slider.x+8+((slider.w-16)*i+4)/8)==60000+uint64_t(i)*30000);
        CHECK(layout.durationAt(std::numeric_limits<int>::min())==60000 && layout.durationAt(std::numeric_limits<int>::max())==300000);
        CHECK(!layout.read(own,0x0411) && !layout.installed());CHECK(!layout.read(own));
        ClassicOptions o;Surface screen;screen.create(640,480,PixelFormat::RGBA32);layout.draw(screen,o,true,true);const auto timed=screen.pixels;o.duration=0;layout.draw(screen,o,false,false);CHECK(timed!=screen.pixels);
        auto bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?20:16),1312);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[0]+(extended?24:18)+2,0x82);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[2]+(extended?12:8),10);CHECK(bool(layout.read(bad))); // overlaps cursor
        bad=own;test::fixtures::put16(bad[0].data,f.controls[5]+(extended?12:8)+4,100);CHECK(bool(layout.read(bad))); // narrow slider
        bad=own;test::fixtures::put16(bad[0].data,f.controls[6]+(extended?12:8),279);CHECK(bool(layout.read(bad)));
        bad=own;bad[0].data.resize(19);CHECK(bool(layout.read(bad)));
    }
}
void optionsNavigation(const std::filesystem::path& tmp){
    for(bool extended:{false,true}){
        auto t=target(tmp/(extended?"extended":"standard"));const auto f=fixtures::optionsDialog(extended);fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage(f.bytes,10834));
        std::vector<PeResource> own;CHECK(!readPeResources(test::fixtures::resourcePe(f.bytes,5,10834),own));FrontendOptionsDialog layout;CHECK(!layout.read(own));
        auto click=[](SelectionRect b){return fixtures::mouseEvent(EventType::MouseDown,b.x+b.w/2,b.y+b.h/2);};
        fixtures::ScriptedSystem sys;sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==1)return {keyEvent(key::Return)};
            if(f==2 || f==10 || f==16 || f==21)return {keyEvent('o'),keyEvent(key::Return)};
            if(f==3)return {keyEvent('v')};
            if(f==4)return {keyEvent('b')};
            if(f==5 || f==17){auto b=layout.control(FrontendOptionsDialog::Orient);return {mouseEvent(EventType::MouseMove,b.x,b.y),click(b)};}
            if(f==6)return {click(layout.control(FrontendOptionsDialog::Chess))};
            if(f==7){auto b=layout.control(FrontendOptionsDialog::Slider);return {mouseEvent(EventType::MouseDown,b.x+b.w-8,b.y+10)};}
            if(f==8 || f==11 || f==13 || f==19 || f==22)return {keyEvent('t')};
            if(f==9)return {click(layout.cancel()),keyEvent(key::Return)};
            if(f==12)return {keyEvent(key::Right)};
            if(f==14)return {keyEvent(key::Left)};
            if(f==15 || f==24)return {keyEvent(key::Escape)};
            if(f==20 || f==23)return {keyEvent(key::Return),keyEvent(key::Return)};
            if(f==25)return {keyEvent('l')};
            return {};
        };
        auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run() && sys.frame==25 && sys.w==640);
        if(sys.hashes.size()==25){CHECK(sys.hashes[2]==sys.hashes[3] && sys.hashes[2]==sys.hashes[4] && sys.hashes[2]==sys.hashes[10] && sys.hashes[2]==sys.hashes[13] && sys.hashes[2]==sys.hashes[16]);CHECK(sys.hashes[11]==sys.hashes[12]);CHECK(sys.hashes[1]==sys.hashes[9] && sys.hashes[1]==sys.hashes[15]);CHECK(sys.hashes[19]==sys.hashes[21] && sys.hashes[1]!=sys.hashes[20]);}
        fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage({1,2,3},10834));fixtures::ScriptedSystem bad;bad.events=[](size_t){return std::vector<Event>{fixtures::keyEvent('l')};};game=findEngine("boggle")->create(bad,t);CHECK(bool(game->run()));t.settings["classic_artwork"]="0";game=findEngine("boggle")->create(bad,t);CHECK(!game->run());
    }
}
void optionsGame(const std::filesystem::path& tmp){
    for(bool master:{false,true}){
        auto t=target(tmp/(master?"master":"original"));t.settings["classic_variant"]=master?"master":"original";const auto f=fixtures::optionsDialog(master);fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage(f.bytes,10834));
        fixtures::ScriptedSystem sys;sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==1 || f==6 || f==7 || f==11 || f==12)return {keyEvent(key::Return)};
            if(f==2 || f==13)return {keyEvent('o')};
            if(f==3)return {keyEvent(key::Left)};
            if(f==4)return {keyEvent('a')};
            if(f==5)return {keyEvent('c')};
            if(f==8)return {textEvent("AAAA"),keyEvent(key::Return)};
            if(f==9)return {keyEvent(key::F1)};
            if(f==10)return {keyEvent(key::F1+2)};
            if(f==14 || f==15)return {keyEvent(key::Escape)};
            if(f==16)return {keyEvent('l')};
            return {};
        };
        auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run() && sys.frame==16);
        if(sys.hashes.size()==16)CHECK(sys.hashes[5]==sys.hashes[13]);
        HighScores scores;CHECK(!scores.load(t.savePath,t.path));const auto rows=scores.table();CHECK(rows.size()==1);if(rows.size()==1)CHECK(rows[0].duration==150000 && rows[0].score==1 && rows[0].variant==(master?ClassicVariant::Master:ClassicVariant::Original));
    }
}

void optionsComputers(const std::filesystem::path& tmp){
    auto t=target(tmp);const auto f=fixtures::optionsDialog();fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage(f.bytes,10834));std::vector<PeResource> own;CHECK(!readPeResources(test::fixtures::resourcePe(f.bytes,5,10834),own));FrontendOptionsDialog layout;CHECK(!layout.read(own));
    auto click=[](SelectionRect b){return fixtures::mouseEvent(EventType::MouseDown,b.x+b.w/2,b.y+b.h/2);};
    fixtures::ScriptedSystem sys;sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==1 || f==5)return {keyEvent(key::Return)};
        if(f==2)return {keyEvent('m')};
        if(f==3)return {keyEvent('2')};
        if(f==4)return {keyEvent(key::End),keyEvent(key::Up),keyEvent(key::Return)};
        if(f==6 || f==18)return {keyEvent('o')};
        if(f==7 || f==9)return {click(layout.control(FrontendOptionsDialog::Cursor))};
        if(f==8 || f==10)return {click(layout.control(FrontendOptionsDialog::Background))};
        if(f==11 || f==13)return {click(layout.control(FrontendOptionsDialog::Traditional))};
        if(f==12)return {click(layout.control(FrontendOptionsDialog::Chess))};
        if(f==14 || f==16)return {keyEvent('t')};
        if(f==15)return {click(layout.control(FrontendOptionsDialog::Slider))};
        if(f==17)return {click(layout.done()),keyEvent(key::Return)};
        if(f==19)return {click(layout.cancel()),keyEvent(key::Return)};
        if(f==20)return {keyEvent(key::Escape)};
        if(f==21)return {keyEvent('l')};
        return {};
    };
    auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run() && sys.frame==21 && sys.w==640);
    if(sys.hashes.size()==21){CHECK(sys.hashes[7]!=sys.hashes[6] && sys.hashes[8]!=sys.hashes[7] && sys.hashes[8]==sys.hashes[9]);CHECK(sys.hashes[7]==sys.hashes[10] && sys.hashes[11]==sys.hashes[13] && sys.hashes[11]==sys.hashes[16] && sys.hashes[11]==sys.hashes[18]);CHECK(sys.hashes[14]==sys.hashes[15] && sys.hashes[17]==sys.hashes[19]);}
}

void optionsDragging(const std::filesystem::path& tmp){
    for(bool master:{false,true}){
        auto t=target(tmp/(master?"master":"original"));t.settings["classic_variant"]=master?"master":"original";const auto f=fixtures::optionsDialog(master);fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage(f.bytes,10834));
        std::vector<PeResource> own;CHECK(!readPeResources(test::fixtures::resourcePe(f.bytes,5,10834),own));FrontendOptionsDialog layout;CHECK(!layout.read(own));const auto b=layout.control(FrontendOptionsDialog::Slider);const int left=b.x+8,mid=b.x+b.w/2,right=b.x+b.w-8,y=b.y+12;
        auto mouse=[&](EventType type,int x){return fixtures::mouseEvent(type,x,y);};
        fixtures::ScriptedSystem sys;sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==1)return {keyEvent(key::Return)};
            if(f==2 || f==20 || f==23)return {keyEvent('o')};
            if(f==3)return {mouse(EventType::MouseDown,left),mouse(EventType::MouseMove,mid),mouse(EventType::MouseUp,right),mouse(EventType::MouseMove,left)};
            if(f==4)return {mouse(EventType::MouseMove,left)};
            if(f==5 || f==10 || f==14)return {mouse(EventType::MouseDown,mid)};
            if(f==6)return {mouse(EventType::MouseMove,std::numeric_limits<int>::max())};
            if(f==7)return {mouse(EventType::MouseMove,std::numeric_limits<int>::min())};
            if(f==8){Event e;e.type=EventType::MouseLeave;return {e,mouse(EventType::MouseMove,right)};}
            if(f==9)return {mouse(EventType::MouseUp,right)};
            if(f==11){Event e;e.type=EventType::FocusLost;return {e,mouse(EventType::MouseMove,right),mouse(EventType::MouseUp,right)};}
            if(f==12){Event e;e.type=EventType::FocusGained;return {e,mouse(EventType::MouseDown,right),mouse(EventType::MouseMove,right),mouse(EventType::MouseUp,right)};}
            if(f==13)return {mouse(EventType::MouseMove,right)};
            if(f==15)return {keyEvent('t'),mouse(EventType::MouseMove,right),mouse(EventType::MouseUp,right)};
            if(f==16)return {mouse(EventType::MouseDown,right),mouse(EventType::MouseMove,right),mouse(EventType::MouseUp,right)};
            if(f==17)return {keyEvent('t')};
            if(f==18)return {mouse(EventType::MouseDown,right)};
            if(f==19)return {keyEvent(key::Escape),mouse(EventType::MouseMove,left),mouse(EventType::MouseUp,left)};
            if(f==21)return {mouse(EventType::MouseDown,left),mouse(EventType::MouseMove,right),mouse(EventType::MouseUp,left+(right-left)*3/8)};
            if(f==22){auto d=layout.done();return {mouseEvent(EventType::MouseDown,d.x+10,d.y+10),keyEvent(key::Return)};}
            if(f==24)return {mouse(EventType::MouseMove,left)};
            if(f==25 || f==26)return {keyEvent(key::Escape)};
            if(f==27)return {keyEvent('l')};
            return {};
        };
        auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run() && sys.frame==27 && sys.w==640);
        if(sys.hashes.size()==27){CHECK(sys.hashes[3]==sys.hashes[4] && sys.hashes[3]==sys.hashes[6] && sys.hashes[3]==sys.hashes[18]);CHECK(sys.hashes[7]==sys.hashes[8] && sys.hashes[7]==sys.hashes[9]);for(size_t i:{5u,10u,11u,12u,13u,14u,17u,20u})CHECK(sys.hashes[2]==sys.hashes[i]);CHECK(sys.hashes[15]==sys.hashes[16] && sys.hashes[1]==sys.hashes[19]);CHECK(sys.hashes[21]==sys.hashes[23] && sys.hashes[21]==sys.hashes[24] && sys.hashes[21]!=sys.hashes[2]);}
    }
}

void nameCaretState(){
    NameEdit e;CHECK(!e.begin("ALPHA") && e.first()==0 && e.last()==5);e.move(1,false);CHECK(e.caret()==5 && e.first()==e.last());e.home(false);e.move(1,false);CHECK(!e.insert("x") && e.text()=="AxLPHA" && e.caret()==2);e.erase(true);CHECK(e.text()=="AxPHA");e.erase(false);CHECK(e.text()=="APHA" && e.caret()==1);e.end(true);CHECK(e.first()==1 && e.last()==4);CHECK(!e.insert("BC") && e.text()=="ABC" && e.caret()==3);e.home(true);CHECK(!e.insert("Z") && e.text()=="Z");e.erase(true);CHECK(e.text()=="Z");e.erase(false);CHECK(e.text().empty() && e.caret()==0);
    CHECK(!e.begin("ABCDE"));e.move(-1,false);CHECK(e.caret()==0);e.end(false);e.move(-1,true);e.move(-1,true);CHECK(e.first()==3 && e.last()==5);e.move(1,false);CHECK(e.caret()==5 && e.first()==e.last());e.home(false);e.move(1,true);e.move(1,true);e.erase(false);CHECK(e.text()=="CDE" && e.caret()==0);
    CHECK(!e.begin("ABCDEFGHIJKLMNOPQR"));e.end(false);CHECK(bool(e.insert("X")) && e.text()=="ABCDEFGHIJKLMNOPQR" && e.caret()==18);e.home(true);CHECK(!e.insert("XYZ") && e.text()=="XYZ");e.selectAll();CHECK(bool(e.insert("\xc3\xa9")) && e.text()=="XYZ" && e.first()==0 && e.last()==3);CHECK(bool(e.begin("ABCDEFGHIJKLMNOPQRS")) && e.text()=="XYZ");CHECK(bool(e.begin("\n")) && e.text()=="XYZ");
    e.home(false);for(int i=0;i<100;++i)e.move(-1,true);CHECK(e.caret()==0);for(int i=0;i<100;++i)e.move(1,true);CHECK(e.caret()==3);CHECK(!e.insert({}) && e.text()=="XYZ");
    Surface screen;screen.create(640,480,PixelFormat::RGBA32);CHECK(!e.begin("ABCDEFGHIJKLMNOPQR"));e.end(false);e.draw(screen,{10,10,240,24});CHECK(screen.row(14)[234*4]==245);const auto before=screen.pixels;e.draw(screen,{-1,10,240,24});CHECK(screen.pixels==before);
}
void nameCaretNavigation(const std::filesystem::path& tmp){
    for(bool installed:{false,true}){
        auto t=target(tmp/(installed?"installed":"fallback"));if(installed){const auto f=fixtures::nameDialog(true);fixtures::write(t.path/"Language/English.dll",fixtures::dialogLanguage(f.bytes,1600));}
        PlayerProfile profile;CHECK(!profile.load(t.savePath,t.path) && !profile.rename("ALPHA"));
        fixtures::ScriptedSystem sys;sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            auto shift=[](uint32_t key){auto e=keyEvent(key);e.mods=keymod::Shift;return e;};
            if(f==0 || f==1)return {keyEvent(key::Return)};
            if(f==2 || f==13)return {keyEvent('n')};
            if(f==3)return {keyEvent(key::End)};
            if(f==4)return {keyEvent(key::Left)};
            if(f==5)return {textEvent("X")};
            if(f==6 || f==14)return {keyEvent(key::Home)};
            if(f==7){auto repeat=shift(key::Right);repeat.repeat=true;return {shift(key::Right),repeat};}
            if(f==8)return {textEvent("BE")};
            if(f==9)return {keyEvent(key::Delete),keyEvent(key::Backspace)};
            if(f==10)return {shift(key::End)};
            if(f==11)return {textEvent("ETA")};
            if(f==12)return {keyEvent(key::Return),keyEvent(key::Return)};
            if(f==15)return {shift(key::End),textEvent("DRAFT")};
            if(f==16)return {keyEvent(key::Escape),keyEvent(key::Return)};
            if(f==17)return {keyEvent(key::Escape)};
            if(f==18)return {keyEvent('l')};
            return {};
        };
        auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run() && sys.frame==18 && sys.w==640 && sys.textStopped);
        CHECK(!profile.load(t.savePath,t.path) && profile.name()=="BETA" && profile.history().size()==3);CHECK(std::find(profile.history().begin(),profile.history().end(),"DRAFT")==profile.history().end());
        if(sys.hashes.size()==18)CHECK(sys.hashes[12]==sys.hashes[16] && sys.hashes[3]!=sys.hashes[4] && sys.hashes[6]!=sys.hashes[7]);
    }
}

void timerAndErrors(const std::filesystem::path& tmp) {
    auto t=target(tmp);t.settings["classic_seconds"]="60";
    fixtures::ScriptedSystem sys;
    sys.events=[](size_t f)->std::vector<Event>{
        using namespace fixtures;
        if(f==1 || f==2 || f==3)return {keyEvent(key::Return)};
        if(f==5)return {keyEvent(key::Escape)};
        if(f==6)return {keyEvent(key::F1+2),keyEvent(key::Return)};
        if(f==7)return {keyEvent('l')};
        return {};
    };
    sys.clock=[](size_t f)->uint64_t{return f<3 ? 100000 : f<4 ? 200000 : 260000;};
    auto game=findEngine("boggle")->create(sys,t);CHECK(!game->run());CHECK(sys.frame==7);
    if(sys.hashes.size()==7) {
        CHECK(sys.hashes[0]==sys.hashes[6] && sys.hashes[3]!=sys.hashes[4]);
        CHECK(sys.hashes[4]==sys.hashes[5]);
    }
    // Optional artwork is not permission to ignore present but malformed data.
    fixtures::write(t.path/"Bitmaps/Select1.bmp","broken");
    fixtures::ScriptedSystem bad;game=findEngine("boggle")->create(bad,t);
    CHECK(bool(game->run()) && bad.w==0 && !bad.audioStarted);
    fixtures::write(t.path/"Bitmaps/Select1.bmp",fixtures::bitmap());
    game=findEngine("boggle")->create(bad,t);CHECK(bool(game->run()) && bad.w==0);
    t.settings["classic_menu"]="2";game=findEngine("boggle")->create(bad,t);CHECK(bool(game->run()) && bad.w==0);
}
}
static const test::Suite suite("boggle.unit.frontend",{
    {"ASCII name caret/selection, insertion, deletion, bounds and rejected edits",[](const auto&){nameCaretState();}},
    {"keyboard name editing in installed/fallback dialogs, repeat, Save/Cancel and history",nameCaretNavigation},
    {"options ruler dragging, coalesced release, bounds, focus, No Timer and Cancel",optionsDragging},
    {"Classic options control IDs, ranges, encodings and malformed layouts",[](const auto&){optionsLayouts();}},
    {"Classic options editing, disabled controls, remembered timer and Cancel",optionsNavigation},
    {"Classic options computer availability and background/cursor interaction",optionsComputers},
    {"Classic options apply to both versions and survive menu return",optionsGame},
    {"roster Cancel/Escape restores names/kinds, defers profile writes and handles failed Done",rosterCancellation},
    {"roster fields, popup geometry, exact language and malformed controls",[](const auto&){rosterLayouts();}},
    {"installed roster, all four selectors, humans/computers, empty slots and close guards",rosterNavigation},
    {"name dialog controls, fitting, language and malformed layouts",[](const auto&){nameLayouts();}},
    {"name dialog Save/Cancel, editing, validation, history and input guards",nameNavigation},
    {"installed selection IDs, encodings, languages and malformed geometry",[](const auto&){selectionLayouts();}},
    {"installed Classic picking, navigation guards, opt-out and return",selectionNavigation},
    {"fitted movie aspect ratio, frame timing, clipping and extreme rectangles",fittedMovies},
    {"selection, variant setup, auto-start, results, return and re-entry",[](const auto& tmp){navigation(tmp);}},
    {"mouse navigation, legacy artwork fallback and global exit",[](const auto& tmp){mouseAndGuards(tmp);}},
    {"owner-confirmed Escape behavior on selection, version and Players",[](const auto& tmp){escapePages(tmp);}},
    {"random music stays selected across menu re-entry and releases output",[](const auto& tmp){musicLifetime(tmp);}},
    {"Cinepak intro completion/skip, PCM cleanup, animated buttons and disabled media",[](const auto& tmp){movies(tmp);}},
    {"publisher then game intro, one-second gap, sequence skip and cleanup",[](const auto& tmp){publisherSequence(tmp);}},
    {"timer starts with play, input boundaries and invalid artwork/settings",[](const auto& tmp){timerAndErrors(tmp);}},
});

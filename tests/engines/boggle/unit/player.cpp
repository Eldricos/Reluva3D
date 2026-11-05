// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/player.h"
#include "engines/boggle/classic/game.h"
#include "engines/registry.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <fstream>
#include <iterator>
using namespace reluva;
using namespace reluva::boggle;
namespace {
std::string read(const std::filesystem::path& p){std::ifstream file(p,std::ios::binary);return {(std::istreambuf_iterator<char>(file)),{}};}
void persistence(const std::filesystem::path& tmp) {
    const auto game=tmp / "game",save=tmp / "save",file=save / "Player.txt";
    fixtures::write(game / "Player.txt","ORIGINAL");PlayerProfile profile;
    CHECK(!profile.load(save,game));CHECK(profile.name()=="PLAYER 1" && profile.persistent());CHECK(!std::filesystem::exists(save));
    CHECK(!profile.rename("  Alex  "));CHECK(profile.name()=="Alex");CHECK(read(file)=="RELUVA BOGGLE PLAYER 2\nAlex\nPLAYER 1\n");
    PlayerProfile restart;CHECK(!restart.load(save,game));CHECK(restart.name()=="Alex");
    CHECK(!restart.rename("Sam O'Neil"));CHECK(!std::filesystem::exists(save / "Player.txt.bak"));
    std::filesystem::rename(file,save / "Player.txt.bak");
    fixtures::write(save / "Player.txt.tmp","INTERRUPTED");
    CHECK(!profile.load(save,game));CHECK(profile.name()=="Sam O'Neil");
    CHECK(!profile.rename("ABCDEFGHIJKLMNOPQR"));CHECK(profile.name().size()==18);
    CHECK(read(game / "Player.txt")=="ORIGINAL");
    CHECK(bool(profile.load(game / "saves",game)));CHECK(!profile.persistent());CHECK(bool(profile.rename("No write")));
    CHECK(!profile.load({},game));CHECK(!profile.persistent());CHECK(!profile.rename("Session only"));CHECK(profile.name()=="Session only");
}
void malformed(const std::filesystem::path& tmp) {
    const auto save=tmp / "save",game=tmp / "game",file=save / "Player.txt";
    std::filesystem::create_directories(game);PlayerProfile profile;CHECK(!profile.load(save,game));CHECK(!profile.rename("Alex"));
    const auto before=read(file);std::filesystem::create_directory(save / "Player.txt.tmp");
    CHECK(bool(profile.rename("Sam")));CHECK(profile.name()=="Alex" && read(file)==before);
    std::filesystem::remove(save / "Player.txt.tmp");CHECK(!profile.rename("Sam"));
    std::string normalized;
    for(const auto& bad:{std::string{},std::string("   "),std::string(19,'A'),std::string("A\nB"),std::string("A\0B",3),std::string("\xc3\xa9")})
        CHECK(!playerName(bad,normalized) && normalized.empty());
    for(const auto& text:{std::string{},std::string("WRONG\nSam\n"),std::string("RELUVA BOGGLE PLAYER 1\n\n"),std::string("RELUVA BOGGLE PLAYER 1\nSam\nExtra\n"),std::string(129,'A')}) {
        fixtures::write(file,text);CHECK(bool(profile.load(save,game)));CHECK(profile.name()=="PLAYER 1");CHECK(bool(profile.rename("No write")));CHECK(read(file)==text);
    }
    fixtures::write(file,"RELUVA BOGGLE PLAYER 1\r\nCase Kept\r\n");CHECK(!profile.load(save,game));CHECK(profile.name()=="Case Kept");
    std::filesystem::remove(file);fixtures::write(tmp / "other","UNTOUCHED");std::error_code ec;
    std::filesystem::create_symlink(tmp / "other",file,ec);
    if(!ec){CHECK(bool(profile.load(save,game)));CHECK(read(tmp / "other")=="UNTOUCHED");}
}
void history(const std::filesystem::path& tmp) {
    const auto save=tmp / "save",game=tmp / "game",file=save / "Player.txt";
    std::filesystem::create_directories(game);
    const std::string legacy="RELUVA BOGGLE PLAYER 1\r\nLegacy Name\r\n";
    fixtures::write(file,legacy);PlayerProfile profile;CHECK(!profile.load(save,game));
    CHECK(profile.history()==std::vector<std::string>{"Legacy Name"});
    CHECK(!profile.rename("Legacy Name") && read(file)==legacy); // no migration on read/no-op
    CHECK(!profile.rename("Alex"));CHECK(!profile.rename("alex"));CHECK(!profile.rename("Alex"));
    CHECK(profile.history()==std::vector<std::string>({"Alex","alex","Legacy Name"}));
    PlayerProfile restart;CHECK(!restart.load(save,game));CHECK(restart.history()==profile.history());
    const auto before=profile.history();const auto bytes=read(file);
    std::filesystem::create_directory(save / "Player.txt.tmp");CHECK(bool(profile.rename("Legacy Name")));
    CHECK(profile.history()==before && profile.name()=="Alex" && read(file)==bytes);
    std::filesystem::remove(save / "Player.txt.tmp");CHECK(!profile.rename("Legacy Name"));
    CHECK(profile.history().size()==3 && profile.history()[0]=="Legacy Name");
    for(size_t i=0;i<40;++i)CHECK(!profile.rename("Person "+std::to_string(i)));
    CHECK(profile.history().size()==PlayerProfile::maximumNames && profile.history().back()=="Person 8");
    CHECK(!restart.load(save,game));CHECK(restart.history()==profile.history());
    const auto valid=read(file);fixtures::write(file,valid+"One more\n");CHECK(bool(restart.load(save,game)));
    for(const auto& bad:{std::string("RELUVA BOGGLE PLAYER 2\n"),std::string("RELUVA BOGGLE PLAYER 2\nAlex\nAlex\n"),
        std::string("RELUVA BOGGLE PLAYER 2\n Alex\n"),std::string("RELUVA BOGGLE PLAYER 2\nAlex\n\n"),std::string(1025,'X')}) {
        fixtures::write(file,bad);CHECK(bool(restart.load(save,game)));CHECK(bool(restart.rename("No write")));CHECK(read(file)==bad);
    }
    fixtures::write(file,valid);std::filesystem::rename(file,save / "Player.txt.bak");
    fixtures::write(save / "Player.txt.tmp","INCOMPLETE");CHECK(!restart.load(save,game));CHECK(restart.history()==profile.history());
    CHECK(!restart.load({},game));CHECK(!restart.rename("One"));CHECK(!restart.rename("Two"));CHECK(!restart.rename("One"));
    CHECK(restart.history()==std::vector<std::string>({"One","Two","PLAYER 1"}) && !restart.persistent());
}
void dropdown(const std::filesystem::path& tmp) {
    fixtures::artworkInstall(tmp / "game");GameTarget target;target.path=tmp / "game";target.savePath=tmp / "save";
    target.settings={{"classic_seed","6551"},{"classic_sound","0"},{"classic_animations","0"},{"frontend_movies","0"}};
    PlayerProfile profile;CHECK(!profile.load(target.savePath,target.path));
    for(int i=0;i<20;++i)CHECK(!profile.rename("Human "+std::to_string(i)));
    fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
    sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==1 || f==19)return {keyEvent(key::Return)};
        if(f==2)return {keyEvent('p'),keyEvent(key::Return)};
        if(f==3 || f==13)return {keyEvent(key::End)};
        if(f==4)return {keyEvent(key::PageUp)};
        if(f==5)return {keyEvent(key::Home)};
        if(f==6 || f==16)return {keyEvent(key::Down)};
        if(f==7)return {keyEvent(key::Return),keyEvent(key::Return)};
        if(f==8 || f==10 || f==15)return {keyEvent('p')};
        if(f==9)return {mouseEvent(EventType::MouseDown,80,260)};
        if(f==11)return {keyEvent(key::Escape),keyEvent(key::Escape)};
        if(f==12)return {mouseEvent(EventType::MouseDown,80,180)};
        if(f==14)return {mouseEvent(EventType::MouseDown,80,380)};
        if(f==17){std::filesystem::create_directory(target.savePath / "Player.txt.tmp");return {keyEvent(key::Return)};}
        if(f==18){std::filesystem::remove(target.savePath / "Player.txt.tmp");return {keyEvent(key::Return),keyEvent(key::Return)};}
        if(f==20)return {keyEvent(key::F1)};
        if(f==21)return {keyEvent(key::F1+2)};
        if(f==22)return {keyEvent('l')};
        return {};
    };
    std::vector<bool> text;sys.present=[&](const Surface&,size_t){text.push_back(sys.textEnabled);};
    auto engine=findEngine("boggle")->create(sys,target);CHECK(!engine->run());CHECK(sys.hashes.size()==22 && !sys.textEnabled);
    if(sys.hashes.size()==22) {
        CHECK(sys.hashes[2]==sys.hashes[5]);CHECK(sys.hashes[7]!=sys.hashes[9]);CHECK(sys.hashes[9]==sys.hashes[11]);
        CHECK(sys.hashes[9]==sys.hashes[18]);CHECK(!text[2] && !text[17] && !text[18]);
        test::Record record;for(size_t i=0;i<sys.hashes.size();++i)record.add("choose."+std::to_string(i),sys.hashes[i]);
        record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/player-history-record.txt","boggle.unit.player","tests/engines/boggle/unit/player.cpp");
    }
    CHECK(!profile.load(target.savePath,target.path));CHECK(profile.name()=="Human 19" && profile.history().size()==21);
    CHECK(profile.history()[1]=="PLAYER 1" && profile.history()[2]=="Human 18");
    const auto before=read(target.savePath / "Player.txt");
    for(auto type:{EventType::Quit,EventType::ReturnToLauncher}) {
        fixtures::ScriptedSystem quit;quit.events=[&](size_t f)->std::vector<Event>{
            if(f==0 || f==1)return {fixtures::keyEvent(key::Return)};
            if(f==2)return {fixtures::keyEvent('p')};
            if(f==3){Event event;event.type=type;return {event};}return {};
        };
        engine=findEngine("boggle")->create(quit,target);CHECK(!engine->run());CHECK(quit.frame==3 && !quit.textEnabled);
        CHECK(read(target.savePath / "Player.txt")==before);
    }
}

void editing(const std::filesystem::path& tmp) {
    fixtures::artworkInstall(tmp / "install");
    GameTarget target;target.path=tmp / "install";target.savePath=tmp / "save";
    target.settings={{"classic_seed","6551"},{"classic_sound","0"},{"classic_animations","0"},{"frontend_movies","0"}};
    fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
    sys.events=[&](size_t frame)->std::vector<Event> {
        using namespace fixtures;
        if(frame==1 || frame==2 || frame==14 || frame==16)return {keyEvent(key::Return)};
        if(frame==3)return {keyEvent('n'),textEvent("ignored")};
        if(frame==4)return {textEvent("Alex")};
        if(frame==5)return {keyEvent(key::Return),keyEvent(key::Return)}; // save must not also launch
        if(frame==6)return {keyEvent('n')};
        if(frame==7)return {textEvent("Cancel this")};
        if(frame==8)return {keyEvent(key::Escape)};
        if(frame==9)return {mouseEvent(EventType::MouseDown,100,330)};
        if(frame==10)return {keyEvent(key::Delete),keyEvent(key::Return)}; // empty name remains modal
        if(frame==11)return {textEvent("ABCDEFGHIJKLMNOPQRS")}; // over length remains modal
        if(frame==12)return {textEvent("Sam O'Neil")};
        if(frame==13)return {mouseEvent(EventType::MouseDown,100,310)};
        if(frame==15)return {keyEvent(key::F1)};
        if(frame==17)return {keyEvent(key::F1+2)};
        if(frame==18)return {keyEvent('l')};
        return {};
    };
    std::vector<bool> textActive;sys.present=[&](const Surface&,size_t){textActive.push_back(sys.textEnabled);};
    auto engine=findEngine("boggle")->create(sys,target);CHECK(!engine->run());CHECK(sys.hashes.size()==18);
    CHECK(!sys.textEnabled);CHECK(read(target.savePath / "Player.txt")=="RELUVA BOGGLE PLAYER 2\nSam O'Neil\nAlex\nPLAYER 1\n");
    if(sys.hashes.size()==18) {
        CHECK(sys.hashes[5]==sys.hashes[8]); // cancel restores the saved name
        CHECK(!textActive[2] && textActive[3] && !textActive[5] && textActive[10]);
        CHECK(sys.hashes[14]==sys.hashes[16]); // same name and board on replay
        test::Record record;for(size_t i=0;i<sys.hashes.size();++i)record.add("edit."+std::to_string(i),sys.hashes[i]);
        record.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/player-record.txt","boggle.unit.player","tests/engines/boggle/unit/player.cpp");
    }
    PlayerProfile loaded;CHECK(!loaded.load(target.savePath,target.path));CHECK(loaded.name()=="Sam O'Neil");
    // Both variants and both renderers consume the same isolated profile in
    // direct entry, without adding a launcher text-setting dependency.
    for(bool master:{false,true})for(bool art:{false,true}) {
        target.settings["classic_variant"]=master?"master":"original";target.settings["classic_artwork"]=art?"1":"0";
        fixtures::ScriptedSystem direct;direct.events=[](size_t f)->std::vector<Event>{if(f==1)return {fixtures::keyEvent(key::Escape)};return {};};
        ClassicGame game(direct,target);CHECK(!game.run());CHECK(direct.hashes.size()==1);
        CHECK(!direct.textEnabled);
        auto anonymous=target;anonymous.savePath=tmp / "unused-save";
        fixtures::ScriptedSystem control;control.events=direct.events;
        ClassicGame unnamed(control,anonymous);CHECK(!unnamed.run());CHECK(control.hashes.size()==1);
        if(direct.hashes.size()==1 && control.hashes.size()==1)CHECK(direct.hashes[0]!=control.hashes[0]);
    }
    // Global exits while editing always release text input.
    fixtures::ScriptedSystem quit;quit.events=[](size_t f)->std::vector<Event>{
        if(f==0 || f==1)return {fixtures::keyEvent(key::Return)};
        if(f==2)return {fixtures::keyEvent('n')};
        if(f==3){Event e;e.type=EventType::Quit;return {e};}return {};
    };
    engine=findEngine("boggle")->create(quit,target);CHECK(!engine->run());CHECK(!quit.textEnabled);
    // A failed write stays in the editor; cancelling keeps the saved name.
    fixtures::ScriptedSystem failure;bool errorHasFocus=false;
    failure.events=[&](size_t f)->std::vector<Event> {
        if(f==0 || f==1)return {fixtures::keyEvent(key::Return)};
        if(f==2)return {fixtures::keyEvent('n')};
        if(f==3)return {fixtures::textEvent("Retry Name")};
        if(f==4){std::filesystem::create_directory(target.savePath / "Player.txt.tmp");return {fixtures::keyEvent(key::Return)};}
        if(f==5 || f==6)return {fixtures::keyEvent(key::Escape)};
        if(f==7)return {fixtures::keyEvent('l')};
        return {};
    };
    failure.present=[&](const Surface&,size_t f){if(f==4)errorHasFocus=failure.textEnabled;};
    engine=findEngine("boggle")->create(failure,target);CHECK(!engine->run());CHECK(errorHasFocus && !failure.textEnabled);
    CHECK(read(target.savePath / "Player.txt")=="RELUVA BOGGLE PLAYER 2\nSam O'Neil\nAlex\nPLAYER 1\n");
}
}
static const test::Suite suite("boggle.unit.player",{
    {"native history migration, recall, bounds, malformed profiles and backup",[](const auto& tmp){history(tmp);}},
    {"name dropdown paging, keyboard/mouse recall, save retry, input guards and exits",[](const auto& tmp){dropdown(tmp);}},
    {"native profile persistence, restart, backup recovery and game-data isolation",[](const auto& tmp){persistence(tmp);}},
    {"bounded names, failed saves, corrupt data, session-only and symlink protection",[](const auto& tmp){malformed(tmp);}},
    {"name editing, save/cancel, input guards, replay, direct variants and cleanup",[](const auto& tmp){editing(tmp);}},
});

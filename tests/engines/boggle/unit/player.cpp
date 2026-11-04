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
    CHECK(!profile.rename("  Alex  "));CHECK(profile.name()=="Alex");CHECK(read(file)=="RELUVA BOGGLE PLAYER 1\nAlex\n");
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
void editing(const std::filesystem::path& tmp) {
    fixtures::artworkInstall(tmp / "install");
    GameTarget target;target.path=tmp / "install";target.savePath=tmp / "save";
    target.settings={{"classic_sound","0"},{"classic_animations","0"},{"frontend_movies","0"}};
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
    CHECK(!sys.textEnabled);CHECK(read(target.savePath / "Player.txt")=="RELUVA BOGGLE PLAYER 1\nSam O'Neil\n");
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
    CHECK(read(target.savePath / "Player.txt")=="RELUVA BOGGLE PLAYER 1\nSam O'Neil\n");
}
}
static const test::Suite suite("boggle.unit.player",{
    {"native profile persistence, restart, backup recovery and game-data isolation",[](const auto& tmp){persistence(tmp);}},
    {"bounded names, failed saves, corrupt data, session-only and symlink protection",[](const auto& tmp){malformed(tmp);}},
    {"name editing, save/cancel, input guards, replay, direct variants and cleanup",[](const auto& tmp){editing(tmp);}},
});

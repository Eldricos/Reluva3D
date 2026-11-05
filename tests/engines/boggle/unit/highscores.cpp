// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/highscores.h"
#include "engines/boggle/classic/game.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <fstream>
#include <iterator>
using namespace reluva;
using namespace reluva::boggle;
namespace {
constexpr auto original=ClassicVariant::Original,master=ClassicVariant::Master;
std::string read(const std::filesystem::path& file){std::ifstream in(file,std::ios::binary);return {(std::istreambuf_iterator<char>(in)),{}};}
void ranking(const std::filesystem::path& tmp) {
    const auto save=tmp / "save",game=tmp / "game",file=save / "HighScores.txt";
    fixtures::write(game / "HighScores.txt","ORIGINAL UNTOUCHED");HighScores scores;
    CHECK(!scores.load(save,game));CHECK(scores.persistent());CHECK(!scores.record(original,180000,0,"Alex"));CHECK(!std::filesystem::exists(save));
    CHECK(!scores.record(original,180000,12,"Alex"));CHECK(!scores.record(original,180000,12,"Sam"));
    const auto before=read(file);CHECK(!scores.record(original,180000,9,"Alex"));CHECK(read(file)==before);
    HighScores restart;CHECK(!restart.load(save,game));auto rows=restart.table();
    CHECK(rows.size()==2 && rows[0].name=="Alex" && rows[1].name=="Sam");
    CHECK(!restart.record(master,180000,13,"Sam"));CHECK(restart.table()[0].name=="Sam");
    CHECK(!restart.record(original,0,14,"Alex"));CHECK(restart.table().size()==2 && restart.table()[0].duration==0);
    CHECK(!restart.record(master,90000,15,"Sam"));CHECK(restart.table()[0].duration==90000);
    for(int i=0;i<12;++i)CHECK(!restart.record(original,180000,20+i,"Player "+std::to_string(i)));
    rows=restart.table();CHECK(rows.size()==10 && rows.front().score==31 && rows.back().score==22);
    const auto top=read(file);CHECK(!restart.record(original,180000,22,"Too late"));CHECK(read(file)==top);
    CHECK(!restart.record(original,180000,100,"Player 2"));CHECK(restart.table().size()==10);
    CHECK(read(game / "HighScores.txt")=="ORIGINAL UNTOUCHED");
    CHECK(bool(restart.record(original,30000,2,"Bad duration")));CHECK(bool(restart.record(original,60000,-1,"Alex")));
    CHECK(bool(restart.record(original,60000,1,"\n")));
    CHECK(!restart.load({},game));CHECK(!restart.persistent());CHECK(!restart.record(original,180000,2,"Session"));CHECK(restart.table().size()==1);
}
void storage(const std::filesystem::path& tmp) {
    const auto save=tmp / "save",game=tmp / "game",file=save / "HighScores.txt";
    std::filesystem::create_directories(game);HighScores scores;CHECK(!scores.load(save,game));CHECK(!scores.record(original,180000,3,"Alex"));
    const auto good=read(file);std::filesystem::create_directory(save / "HighScores.txt.tmp");
    CHECK(bool(scores.record(original,180000,9,"Alex")));CHECK(read(file)==good && scores.table()[0].score==3);
    std::filesystem::remove(save / "HighScores.txt.tmp");CHECK(!scores.record(original,180000,9,"Alex"));
    std::filesystem::rename(file,save / "HighScores.txt.bak");fixtures::write(save / "HighScores.txt.tmp","INTERRUPTED");
    CHECK(!scores.load(save,game));CHECK(scores.table()[0].score==9);CHECK(!scores.record(original,180000,10,"Alex"));
    CHECK(std::filesystem::exists(file) && !std::filesystem::exists(save / "HighScores.txt.bak"));
    for(const auto& bad:{std::string{},std::string("WRONG\n"),std::string("original\t180\t1\tAlex\n"),
        std::string("RELUVA BOGGLE HIGHSCORES 1\noriginal\t180\t1\tAlex\noriginal\t180\t2\tAlex\n"),
        std::string("RELUVA BOGGLE HIGHSCORES 1\noriginal\t0180\t1\tAlex\n"),
        std::string("RELUVA BOGGLE HIGHSCORES 1\noriginal\t180\t2147483648\tAlex\n"),
        std::string("RELUVA BOGGLE HIGHSCORES 1\noriginal\t180\t1\t Alex\n"),std::string(4097,'X')}) {
        fixtures::write(file,bad);CHECK(bool(scores.load(save,game)));CHECK(bool(scores.record(original,180000,5,"No write")));CHECK(read(file)==bad);
    }
    fixtures::write(file,"RELUVA BOGGLE HIGHSCORES 1\r\nmaster\t0\t4\tCase Kept\r\n");CHECK(!scores.load(save,game));CHECK(scores.table()[0].name=="Case Kept");
    CHECK(bool(scores.load(game / "saves",game)));CHECK(bool(scores.record(original,180000,2,"No write")));
    std::filesystem::remove(file);fixtures::write(tmp / "other","UNTOUCHED");std::error_code ec;
    std::filesystem::create_symlink(tmp / "other",file,ec);if(!ec){CHECK(bool(scores.load(save,game)));CHECK(read(tmp / "other")=="UNTOUCHED");}
}
void gameFlow(const std::filesystem::path& tmp) {
    for(bool isMaster:{false,true})for(bool art:{false,true})for(bool approve:{false,true}) {
        const auto root=tmp / (std::to_string(isMaster)+std::to_string(art)+std::to_string(approve));
        fixtures::artworkInstall(root / "game");GameTarget target;target.path=root / "game";target.savePath=root / "save";
        target.settings={{"classic_variant",isMaster?"master":"original"},{"classic_seconds","0"},{"classic_artwork",art?"1":"0"},{"classic_sound","0"},{"classic_animations","0"},{"classic_player_name","Alex"}};
        fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AAAA"),keyEvent(key::Return),textEvent("AAAAA"),keyEvent(key::Return),keyEvent('h')};
            if(f==2)return {keyEvent(key::F1)};
            if(f==3){CHECK(!std::filesystem::exists(target.savePath / "HighScores.txt"));return {keyEvent(approve?'1':'2'),keyEvent('h')};}
            if(f==4)return {mouseEvent(EventType::MouseDown,art?240:640,art?460:82)};
            if(f==5)return {keyEvent(key::Escape),keyEvent(key::Return)}; // close consumes remaining events
            if(f==6)return {keyEvent('a')};
            if(f==7)return {keyEvent('h')};
            if(f==8)return {keyEvent(key::Return),keyEvent(key::Return)};
            if(f==9)return {keyEvent(key::Escape)};
            return {};
        };
        std::vector<std::string> tables;sys.present=[&](const Surface& screen,size_t){test::Hash hash;for(int y=94;y<498;++y)hash.bytes(screen.row(y)+128*4,544*4);tables.push_back(hash.hex());};
        ClassicGame game(sys,target);CHECK(!game.run());CHECK(sys.hashes.size()==9);
        if(sys.hashes.size()==9){CHECK(sys.hashes[3]==sys.hashes[5]);CHECK(tables[4]==tables[7]);CHECK(sys.hashes[6]==sys.hashes[8]);}
        HighScores loaded;CHECK(!loaded.load(target.savePath,target.path));auto rows=loaded.table();
        CHECK(rows.size()==1 && rows[0].name=="Alex" && rows[0].score==(approve?3:1));
    }
}
void retry(const std::filesystem::path& tmp) {
    fixtures::artworkInstall(tmp / "game");GameTarget target;target.path=tmp / "game";target.savePath=tmp / "save";
    target.settings={{"classic_sound","0"},{"classic_animations","0"}};
    fixtures::write(target.savePath / "HighScores.txt","CORRUPT");fixtures::ScriptedSystem sys;
    sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0)return {keyEvent(key::Return)};
        if(f==1)return {textEvent("AAAA"),keyEvent(key::Return),keyEvent(key::F1)};
        if(f==2)return {keyEvent('h')};
        if(f==3){CHECK(read(target.savePath / "HighScores.txt")=="CORRUPT");std::filesystem::remove(target.savePath / "HighScores.txt");return {keyEvent('r')};}
        if(f==4){Event quit;quit.type=EventType::Quit;return {quit};}return {};
    };
    ClassicGame game(sys,target);CHECK(!game.run());CHECK(sys.hashes.size()==4 && !sys.textEnabled);
    HighScores loaded;CHECK(!loaded.load(target.savePath,target.path));CHECK(loaded.table().size()==1);
}
}
static const test::Suite suite("boggle.unit.highscores",{
    {"personal bests, stable ties, shared Classic table, top ten and restart",[](const auto& tmp){ranking(tmp);}},
    {"bounded parsing, backup recovery, failed writes and source isolation",[](const auto& tmp){storage(tmp);}},
    {"post-adjudication recording, both variants/renderers, modal guards and discovery",[](const auto& tmp){gameFlow(tmp);}},
    {"corrupt scores do not block play, explicit retry and global exit",[](const auto& tmp){retry(tmp);}},
});

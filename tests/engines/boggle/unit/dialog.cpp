// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/dialog.h"
#include "engines/boggle/classic/review_dialog.h"
#include "engines/boggle/classic/notice_dialog.h"
#include "engines/boggle/classic/scores_dialog.h"
#include "engines/boggle/classic/match_dialog.h"
#include "engines/boggle/classic/handoff_dialog.h"
#include <limits>
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/highscores.h"
#include "tests/engines/boggle/dialog_fixture.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
using namespace reluva;using namespace reluva::boggle;
namespace {
std::vector<PeResource> resources(const std::vector<uint8_t>& bytes){std::vector<PeResource> out;CHECK(!readPeResources(test::fixtures::resourcePe(bytes,5,16003),out));return out;}
void templates(){
    for(bool extended:{false,true})for(bool extra:{false,true}){
        auto fixture=fixtures::dialog(extended,extra);DialogTemplate d;CHECK(!readDialog(fixture.bytes,d));
        CHECK(d.extended==extended && d.controls.size()==5 && d.hasFont && d.pointSize==8 && d.typeface==u"InventedFont");
        CHECK(d.rect.x==-5 && d.rect.y==7 && d.rect.w==300 && d.rect.h==120 && d.title==u"Own prompt");
        CHECK(d.controls[0].id==6 && d.controls[0].windowClass.ordinal && d.controls[0].windowClass.id==0x80);
        CHECK(d.controls[0].creationData==(extra?std::vector<uint8_t>{1,2,3}:std::vector<uint8_t>{}));
        if(extended)CHECK(d.helpId==0x12345678 && d.weight==650 && d.italic==1 && d.charset==2 && d.controls[0].helpId==42);
        for(size_t n=0;n<fixture.bytes.size();++n){auto truncated=fixture.bytes;truncated.resize(n);CHECK(bool(readDialog(truncated,d)) && d.controls.empty());}
        auto padded=fixture.bytes;while(padded.size()%4)padded.push_back(0);CHECK(!readDialog(padded,d));padded.push_back(99);CHECK(bool(readDialog(padded,d)));
        auto bad=fixture.bytes;test::fixtures::put16(bad,extended?16:8,1025);CHECK(bool(readDialog(bad,d)));
        bad=fixture.bytes;test::fixtures::put16(bad,fixture.caption,0xd800);CHECK(bool(readDialog(bad,d)));
        bad=fixture.bytes;test::fixtures::put16(bad,fixture.caption,0xdc00);CHECK(bool(readDialog(bad,d)));
        auto unicode=fixture.bytes;test::fixtures::put16(unicode,fixture.caption,0xd83d);test::fixtures::put16(unicode,fixture.caption+2,0xde00);CHECK(!readDialog(unicode,d) && d.title[0]==0xd83d && d.title[1]==0xde00);
        if(extended){bad=fixture.bytes;test::fixtures::put16(bad,0,2);CHECK(bool(readDialog(bad,d)));bad=fixture.bytes;test::fixtures::put32(bad,fixture.controls[0]+20,0x12340006);CHECK(!readDialog(bad,d) && d.controls[0].id==0x12340006);}
    }
    DialogTemplate d;CHECK(bool(readDialog(std::vector<uint8_t>(1024*1024+1),d)));
}
void layouts(){
    ClassicReviewDialog layout;CHECK(!layout.read({}) && !layout.installed());CHECK(layout.buttons()[0].contains(40,352) && !layout.buttons()[0].contains(208,352));
    auto fixture=fixtures::dialog();auto own=resources(fixture.bytes);CHECK(!layout.read(own) && layout.installed());
    const auto b=layout.buttons();CHECK(b[3].x<b[2].x && b[2].x<b[1].x && b[1].x<b[0].x);
    CHECK(!layout.read(own,0x0411) && !layout.installed());
    auto bad=fixture.bytes;test::fixtures::put16(bad,fixture.controls[1]+16,6);CHECK(bool(layout.read(resources(bad))) && !layout.installed());
    bad=fixture.bytes;test::fixtures::put16(bad,fixture.controls[1]+8,230);CHECK(bool(layout.read(resources(bad)))); // overlapping actions
    bad=fixture.bytes;test::fixtures::put16(bad,fixture.controls[0]+8,299);CHECK(bool(layout.read(resources(bad))));
    bad=fixture.bytes;bad.resize(23);CHECK(bool(layout.read(resources(bad))));
    CHECK(!layout.read(own));Surface screen;screen.create(800,600,PixelFormat::RGBA32);const auto blank=screen.pixels;layout.draw(screen,"OWNWORD",{});CHECK(screen.pixels!=blank);
}
void noticeLayouts(){
    ClassicNoticeDialog layout;CHECK(!layout.read({}) && !layout.installed() && layout.okay().contains(400,360));
    for(bool extended:{false,true}) {
        const auto f=fixtures::dialog(extended,false,true);auto own=resources(f.bytes);own[0].name.id=16006;
        CHECK(!layout.read(own) && layout.installed());const auto b=layout.okay();CHECK(b.x==480 && b.y==390 && b.w==100 && b.h==32);
        CHECK(!layout.read(own,0x0411) && !layout.installed());CHECK(!layout.read(own));
        Surface screen;screen.create(800,600,PixelFormat::RGBA32);const auto empty=screen.pixels;
        layout.draw(screen,"Own message with enough words to wrap inside this invented notice.");CHECK(screen.pixels!=empty);
        const auto offset=f.controls[1]+(extended?20:16);auto bad=f.bytes;test::fixtures::put16(bad,offset,1348);own[0].data=bad;CHECK(bool(layout.read(own)) && !layout.installed());
        own[0].data=f.bytes;test::fixtures::put16(own[0].data,f.controls[1]+(extended?12:8)+2,40);CHECK(bool(layout.read(own))); // button crosses message area
        own[0].data=f.bytes;test::fixtures::put16(own[0].data,f.controls[2]+(extended?12:8)+4,30);CHECK(bool(layout.read(own))); // narrow body
    }
}
void noticeGame(const std::filesystem::path& tmp){
    for(bool master:{false,true})for(bool chess:{false,true}) {
        const auto root=tmp/(std::to_string(master)+std::to_string(chess));fixtures::artworkInstall(root);
        const auto f=fixtures::dialog(master,false,true);fixtures::write(root/"Language/English.dll",fixtures::dialogLanguage(f.bytes,16006));
        auto own=resources(f.bytes);own[0].name.id=16006;ClassicNoticeDialog layout;CHECK(!layout.read(own));const auto okay=layout.okay();
        GameTarget target;target.path=root;target.savePath=tmp/("save-"+std::to_string(master)+std::to_string(chess));target.settings={{"classic_variant",master?"master":"original"},{"classic_turns",chess?"chess":"traditional"},{"classic_seed","6551"},{"classic_animations","0"},{"classic_sound","0"},{"classic_seconds","60"}};
        fixtures::ScriptedSystem sys;sys.clock=[](size_t f){const uint64_t times[]={0,1100,1000000,1000010,2000000,2000010,2000020,2000030,2000040};return times[std::min(f,size_t(8))];};
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==1)return {textEvent(master?"AAA":"AA"),keyEvent(key::Return)};
            if(f==2)return {mouseEvent(EventType::MouseDown,400,360)}; // old position cannot dismiss installed layout
            if(f==3){Event e;e.type=EventType::FocusLost;return {e};}
            if(f==4){Event e;e.type=EventType::FocusGained;return {e,mouseEvent(EventType::MouseDown,okay.x+10,okay.y+10)};}
            if(f==5)return {chess?keyEvent(master?key::Space:key::Return):mouseEvent(EventType::MouseDown,okay.x+10,okay.y+10),textEvent("AAAA"),keyEvent(key::Return)};
            if(f==6)return {textEvent("AAAA"),keyEvent(key::Return)};
            if(f==7)return {keyEvent(key::F1)};
            if(f==8)return {keyEvent(key::Escape)};
            return {};
        };
        ClassicGame game(sys,target);if(auto error=game.run())test::fail(error.message);
        CHECK(sys.hashes.size()==8);if(sys.hashes.size()==8){CHECK(sys.hashes[1]==sys.hashes[2] && sys.hashes[1]==sys.hashes[4]);CHECK(sys.hashes[5]!=sys.hashes[6]);}
        HighScores scores;CHECK(!scores.load(target.savePath,target.path));CHECK(scores.table().size()==1 && scores.table()[0].score==1);
    }
}
void scoresLayouts(){
    ClassicScoresDialog layout;CHECK(!layout.read({}) && !layout.installed() && layout.close().contains(500,460) && layout.retry().contains(200,460));
    for(bool extended:{false,true}) {
        const auto f=fixtures::scoresDialog(extended);auto own=resources(f.bytes);own[0].name.id=16007;
        CHECK(!layout.read(own) && layout.installed());const auto close=layout.close(),retry=layout.retry();CHECK(close.x+close.w<=784 && close.y+close.h<=576 && retry.x+retry.w<close.x);
        CHECK(!layout.read(own,0x0411) && !layout.installed());CHECK(!layout.read(own));
        HighScores scores;CHECK(!scores.load({},{}));for(int i=0;i<10;++i)CHECK(!scores.record(i%2?ClassicVariant::Master:ClassicVariant::Original,180000,i,"ABCDEFGHIJKLMNOPQR"));const auto before=scores.table();
        Surface screen;screen.create(800,600,PixelFormat::RGBA32);std::fill(screen.pixels.begin(),screen.pixels.end(),17);layout.draw(screen,scores,"Own error");
        for(int y=0;y<600;++y)for(int x=0;x<800;++x)if(screen.row(y)[x*4+3]!=17)CHECK(x>=16 && x<784 && y>=24 && y<576);
        CHECK(scores.table()==before);
        auto bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?20:16),15400);CHECK(bool(layout.read(bad)) && !layout.installed());
        bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?12:8),40);CHECK(bool(layout.read(bad))); // columns overlap
        bad=own;test::fixtures::put16(bad[0].data,f.controls.back()+(extended?12:8)+2,100);CHECK(bool(layout.read(bad))); // Close crosses rows
        bad=own;bad[0].data.resize(19);CHECK(bool(layout.read(bad)));
    }
}
void scoresGame(const std::filesystem::path& tmp){
    for(bool master:{false,true})for(bool recovery:{false,true}) {
        const auto root=tmp/(std::to_string(master)+std::to_string(recovery));fixtures::artworkInstall(root);
        const auto f=fixtures::scoresDialog(master);fixtures::write(root/"Language/English.dll",fixtures::dialogLanguage(f.bytes,16007));
        auto own=resources(f.bytes);own[0].name.id=16007;ClassicScoresDialog layout;CHECK(!layout.read(own));
        GameTarget target;target.path=root;target.savePath=tmp/("save-"+std::to_string(master)+std::to_string(recovery));target.settings={{"classic_variant",master?"master":"original"},{"classic_seed","6551"},{"classic_animations","0"},{"classic_sound","0"},{"classic_seconds","0"}};
        HighScores initial;CHECK(!initial.load(target.savePath,target.path));
        if(recovery)fixtures::write(target.savePath/"HighScores.txt","Own corrupt file");
        else for(int i=0;i<9;++i)CHECK(!initial.record(i%2?ClassicVariant::Master:ClassicVariant::Original,180000,i+2,"ABCDEFGHIJKLMNOPQR"));
        fixtures::ScriptedSystem sys;sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            auto click=[&](HistoryButton b){return mouseEvent(EventType::MouseDown,b.x+b.w/2,b.y+b.h/2);};
            if(f==0)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AAAA"),keyEvent(key::Return)};
            if(f==2)return {keyEvent(key::F1)};
            if(f==3)return {keyEvent('h')};
            if(f==4)return recovery?std::vector<Event>{click(layout.retry())}:std::vector<Event>{click(layout.close()),keyEvent(key::Return)};
            if(f==5){if(recovery){std::filesystem::remove(target.savePath/"HighScores.txt");return {click(layout.retry()),click(layout.retry())};}return {keyEvent('h')};}
            if(f==6)return {keyEvent(key::Return),keyEvent(key::Return)};
            if(f==7)return {keyEvent(key::Escape)};
            return {};
        };
        ClassicGame game(sys,target);if(auto error=game.run())test::fail(error.message);CHECK(sys.hashes.size()==7);
        if(sys.hashes.size()==7){if(recovery)CHECK(sys.hashes[3]==sys.hashes[4] && sys.hashes[4]!=sys.hashes[5]);else CHECK(sys.hashes[3]==sys.hashes[5] && sys.hashes[2]==sys.hashes[4] && sys.hashes[4]==sys.hashes[6]);}
        HighScores final;CHECK(!final.load(target.savePath,target.path));CHECK(final.table().size()==(recovery?1u:10u));if(!final.table().empty())CHECK(final.table().back().score==1);
    }
}
void matchClose(const std::filesystem::path& tmp){
    fixtures::artworkInstall(tmp/"game");const auto f=fixtures::scoresDialog();fixtures::write(tmp/"game/Language/English.dll",fixtures::dialogLanguage(f.bytes,16007));
    auto own=resources(f.bytes);own[0].name.id=16007;ClassicScoresDialog layout;CHECK(!layout.read(own));
    GameTarget target;target.path=tmp/"game";target.savePath=tmp/"save";target.settings={{"classic_seed","6551"},{"classic_animations","0"},{"classic_sound","0"},{"classic_seconds","0"}};
    fixtures::ScriptedSystem sys;sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==3)return {keyEvent(key::Return)};
        if(f==1 || f==4)return {textEvent("AAAA"),keyEvent(key::Return)};
        if(f==2 || f==5)return {keyEvent(key::F1)};
        if(f==6)return {mouseEvent(EventType::MouseDown,540,465)};
        if(f==7)return {keyEvent('h')};
        if(f==8){const auto b=layout.close();return {mouseEvent(EventType::MouseDown,b.x+b.w/2,b.y+b.h/2)};}
        if(f==9)return {keyEvent(key::Escape)};
        return {};
    };
    ClassicGame game(sys,target,false,{"One","Two"});if(auto error=game.run())test::fail(error.message);
    CHECK(sys.hashes.size()==9);if(sys.hashes.size()==9)CHECK(sys.hashes[5]!=sys.hashes[6] && sys.hashes[6]==sys.hashes[8]);
}
void matchLayouts(){
    ClassicMatchDialog layout;CHECK(!layout.read({}) && !layout.installed() && layout.close().contains(540,465));
    CHECK(layout.playerAt(180,190,2)==0 && !layout.playerAt(180,280,2));
    for(bool extended:{false,true}){
        const auto f=fixtures::matchDialog(extended);auto own=resources(f.bytes);own[0].name.id=16008;
        CHECK(!layout.read(own) && layout.installed());CHECK(!layout.read(own,0x0411) && !layout.installed());CHECK(!layout.read(own));
        CHECK(layout.playerAt(170,240,4)==0 && layout.playerAt(470,360,4)==3 && !layout.playerAt(170,360,3));
        CHECK(!layout.playerAt(170,261,4) && !layout.playerAt(layout.close().x,layout.close().y,4));
        for(size_t control:{size_t(1),size_t(9),size_t(10)}){
            auto bad=own;test::fixtures::put16(bad[0].data,f.controls[control]+(extended?20:16),15400);CHECK(bool(layout.read(bad)) && !layout.installed());
        }
        auto bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?12:8),30);CHECK(bool(layout.read(bad))); // score overlaps name
        bad=own;test::fixtures::put16(bad[0].data,f.controls[2]+(extended?12:8)+2,70);CHECK(bool(layout.read(bad))); // rows overlap
        bad=own;test::fixtures::put16(bad[0].data,f.controls[8]+(extended?12:8),220);CHECK(bool(layout.read(bad))); // buttons overlap
        bad=own;test::fixtures::put16(bad[0].data,f.controls[8]+(extended?12:8)+6,40);CHECK(bool(layout.read(bad))); // action leaves no footer room
        bad=own;test::fixtures::put16(bad[0].data,f.controls[10]+(extended?12:8)+6,80);CHECK(bool(layout.read(bad))); // message crosses rows
        bad=own;test::fixtures::put16(bad[0].data,f.controls[9]+(extended?12:8),299);CHECK(bool(layout.read(bad))); // outside panel
        bad=own;test::fixtures::put16(bad[0].data,f.controls[0]+(extended?24:18)+2,0x80);CHECK(bool(layout.read(bad))); // wrong class
        bad=own;bad[0].data.resize(19);CHECK(bool(layout.read(bad)));
        CHECK(!layout.read(own));Surface screen;screen.create(800,600,PixelFormat::RGBA32);std::fill(screen.pixels.begin(),screen.pixels.end(),17);
        ClassicCompetition empty;layout.draw(screen,{"Own name"},empty,true,true);
        for(int y=0;y<600;++y)for(int x=0;x<800;++x)if(screen.row(y)[x*4+3]!=17)CHECK(x>=100 && x<700 && y>=90 && y<510);
    }
}
void matchGame(const std::filesystem::path& tmp){
    for(bool master:{false,true}){
        const auto root=tmp/(master?"master":"original");fixtures::artworkInstall(root);
        const auto f=fixtures::matchDialog(master);fixtures::write(root/"Language/English.dll",fixtures::dialogLanguage(f.bytes,16008));
        auto own=resources(f.bytes);own[0].name.id=16008;ClassicMatchDialog layout;CHECK(!layout.read(own));
        GameTarget target;target.path=root;target.savePath=tmp/(master?"save-master":"save-original");target.settings={{"classic_variant",master?"master":"original"},{"classic_seed","6551"},{"classic_animations","0"},{"classic_sound","0"},{"classic_seconds","0"}};
        std::vector<std::string> panels,tables;
        fixtures::ScriptedSystem sys;sys.present=[&](const Surface& screen,size_t){
            test::Hash panel,table;
            for(int y=90;y<510;++y)panel.bytes(screen.row(y)+100*4,600*4);
            for(int y=94;y<498;++y)table.bytes(screen.row(y)+128*4,544*4);
            panels.push_back(panel.hex());tables.push_back(table.hex());
        };sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            auto click=[](HistoryButton b){return mouseEvent(EventType::MouseDown,b.x+b.w/2,b.y+b.h/2);};
            if(f<12){switch(f%3){case 0:return {keyEvent(key::Return)};case 1:return {textEvent("AAAA"),keyEvent(key::Return)};default:return {keyEvent(key::F1)};}}
            if(f==12)return {mouseEvent(EventType::MouseDown,540,465)}; // retired Close does nothing
            if(f==13)return {click(layout.scores()),keyEvent(key::Return)}; // queued Return cannot close scores
            if(f==14)return {keyEvent(key::Escape),keyEvent(key::Escape)}; // return to summary only
            if(f==15)return {mouseEvent(EventType::MouseDown,170,360),keyEvent(key::Return)}; // fourth player, consume replay
            if(f==16 || f==18)return {keyEvent('v')};
            if(f==17)return {keyEvent('2')};
            if(f==19)return {click(layout.close()),keyEvent(key::Return)};
            if(f==20 || f==21)return {keyEvent('h')}; // standalone scores return to words
            if(f==22)return {keyEvent(key::Escape)};
            return {};
        };
        ClassicGame game(sys,target,false,{"ABCDEFGHIJKLMNOPQR","Second","Third","Fourth"});if(auto error=game.run())test::fail(error.message);
        CHECK(sys.hashes.size()==22);if(sys.hashes.size()==22){CHECK(sys.hashes[11]==sys.hashes[12] && sys.hashes[11]==sys.hashes[14] && panels[11]==panels[16] && panels[11]==panels[18]);CHECK(tables[13]==tables[20]);CHECK(sys.hashes[15]!=sys.hashes[17]);CHECK(sys.hashes[19]==sys.hashes[21]);}
        HighScores scores;CHECK(!scores.load(target.savePath,target.path));CHECK(scores.table().size()==4);for(const auto& row:scores.table())CHECK(row.score==0);
    }
}
void handoffLayouts(){
    ClassicHandoffDialog layout;CHECK(!layout.read({}) && !layout.installed() && layout.start().contains(100,400));
    for(bool extended:{false,true}){
        const auto f=fixtures::handoffDialog(extended);auto own=resources(f.bytes);own[0].name.id=15900;
        CHECK(!layout.read(own) && layout.installed() && layout.start().contains(600,450) && !layout.start().contains(100,400));
        CHECK(!layout.read(own,0x0411) && !layout.installed());CHECK(!layout.read(own));
        Surface screen;screen.create(800,600,PixelFormat::RGBA32);std::fill(screen.pixels.begin(),screen.pixels.end(),17);
        layout.draw(screen,"ABCDEFGHIJKLMNOPQR",true,std::numeric_limits<uint64_t>::max(),true,{"ABCDEFGHIJKLMNOPQR","Second","Third"});
        for(const auto& p:{std::pair{0,0},std::pair{799,599},std::pair{24,100},std::pair{799,300}}){const auto* c=screen.row(p.second)+p.first*4;CHECK(c[0]==18 && c[1]==32 && c[2]==48 && c[3]==255);}
        auto bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?20:16),15901);CHECK(bool(layout.read(bad)) && !layout.installed());
        bad=own;test::fixtures::put16(bad[0].data,f.controls[0]+(extended?24:18)+2,0x80);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?12:8),319);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?12:8)+2,80);CHECK(bool(layout.read(bad))); // button crosses message
        bad=own;test::fixtures::put16(bad[0].data,f.controls[1]+(extended?12:8)+2,200);CHECK(bool(layout.read(bad))); // no footer clearance
        bad=own;test::fixtures::put16(bad[0].data,f.controls[0]+(extended?12:8)+6,40);CHECK(bool(layout.read(bad))); // insufficient message height
        bad=own;bad[0].data.resize(19);CHECK(bool(layout.read(bad)));
    }
}
void handoffGame(const std::filesystem::path& tmp){
    for(bool master:{false,true})for(bool chess:{false,true}){
        const auto root=tmp/(std::to_string(master)+std::to_string(chess));fixtures::artworkInstall(root);
        const auto f=fixtures::handoffDialog(master);fixtures::write(root/"Language/English.dll",fixtures::dialogLanguage(f.bytes,15900));
        auto own=resources(f.bytes);own[0].name.id=15900;ClassicHandoffDialog layout;CHECK(!layout.read(own));
        const auto start=layout.start();auto click=[&](){return fixtures::mouseEvent(EventType::MouseDown,start.x+start.w/2,start.y+start.h/2);};
        GameTarget target;target.path=root;target.savePath=tmp/("save-"+std::to_string(master)+std::to_string(chess));target.settings={{"classic_variant",master?"master":"original"},{"classic_turns",chess?"chess":"traditional"},{"classic_seed","6551"},{"classic_animations","0"},{"classic_sound","0"},{"classic_seconds","60"}};
        fixtures::ScriptedSystem sys;sys.clock=[](size_t f)->uint64_t{return f<4?f*1000:f==4?1000000:2000000+(f-5)*10;};
        sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==1){auto e=std::vector<Event>{textEvent("AAAA"),keyEvent(key::Return)};if(chess)e.push_back(click());return e;}
            if(f==2)return chess?std::vector<Event>{keyEvent(key::F1)}:std::vector<Event>{keyEvent(key::F1),click()};
            if(f==3)return {mouseEvent(EventType::MouseDown,100,400)};
            if(f==4){Event e;e.type=EventType::FocusLost;return {e};}
            if(f==5){Event e;e.type=EventType::FocusGained;return {e,click()};}
            if(f==6)return {click(),textEvent("AAAA"),keyEvent(key::Return)};
            if(f==7)return {textEvent("AAAA"),keyEvent(key::Return)};
            if(!chess){if(f==8)return {keyEvent(key::F1)};if(f==9)return {keyEvent(key::Return)};if(f==10)return {keyEvent(key::Escape)};}
            else {if(f==8 || f==10 || f==12)return {keyEvent(key::Return)};if(f==9 || f==11)return {keyEvent(key::F1)};if(f==13)return {keyEvent(key::Escape)};}
            return {};
        };
        ClassicGame game(sys,target,false,{"First","Second"});if(auto error=game.run())test::fail(error.message);
        CHECK(sys.hashes.size()==(chess?13u:10u));if(sys.hashes.size()>=8){CHECK(sys.hashes[2]==sys.hashes[3] && sys.hashes[2]==sys.hashes[5]);CHECK(sys.hashes[6]!=sys.hashes[7]);}
        HighScores scores;CHECK(!scores.load(target.savePath,target.path));CHECK(scores.table().size()==2);for(const auto& row:scores.table())CHECK(row.score==0);
    }
}
void decisions(const std::filesystem::path& tmp){
    for(bool master:{false,true})for(size_t choice=0;choice<4;++choice){
        const auto root=tmp/(std::to_string(master)+std::to_string(choice));fixtures::artworkInstall(root);
        const auto data=fixtures::dialog(master);fixtures::write(root/"Language/English.dll",fixtures::dialogLanguage(data.bytes));
        ClassicReviewDialog layout;CHECK(!layout.read(resources(data.bytes)) && layout.installed());
        GameTarget target;target.path=root;target.savePath=tmp/("save-"+std::to_string(master)+std::to_string(choice));target.settings={{"classic_variant",master?"master":"original"},{"classic_seed","6551"},{"classic_animations","0"},{"classic_sound","0"},{"classic_seconds","0"}};
        fixtures::ScriptedSystem sys;sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            auto click=[&](size_t i){const auto b=layout.buttons()[i];return mouseEvent(EventType::MouseDown,b.x+b.w/2,b.y+b.h/2);};
            if(f==0)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AAAAA"),keyEvent(key::Return),textEvent("AAAAAA"),keyEvent(key::Return)};
            if(f==2)return {keyEvent(key::F1),click(choice)}; // requires a displayed prompt
            if(f==3)return {click(choice)};
            if(f==4 && choice<2)return {click(choice==0?3:2)};
            if(f==5)return {keyEvent(key::Escape)};
            return {};
        };
        ClassicGame game(sys,target);if(auto error=game.run())test::fail(error.message);HighScores scores;CHECK(!scores.load(target.savePath,target.path));const int expected[]={2,3,5,0};CHECK(scores.table().size()==1 && scores.table()[0].score==expected[choice]);
    }
}
}
static const test::Suite suite("boggle.unit.dialog",{
    {"standard/extended headers, font, Unicode, alignment, creation data and malformed input",[](const auto&){templates();}},
    {"exact language, missing layouts, action IDs, geometry and native drawing",[](const auto&){layouts();}},
    {"installed button positions preserve four choices, prompt guard and scoring",decisions},
    {"notice layout, exact language, wrapping and invalid controls",[](const auto&){noticeLayouts();}},
    {"installed OK, focus nesting, frozen clocks and acknowledgment batch",noticeGame},
    {"fitted score rows, languages, clipping and malformed controls",[](const auto&){scoresLayouts();}},
    {"score Close, replay guard, persisted ten rows and retry recovery",scoresGame},
    {"multiplayer results keep their own Close target",matchClose},
    {"match rows, exact language, malformed controls and hit targets",[](const auto&){matchLayouts();}},
    {"four-player results, nested highscores, review and event-batch guards",matchGame},
    {"handoff IDs, language, privacy cover and malformed layouts",[](const auto&){handoffLayouts();}},
    {"installed human handoffs, Traditional/Chess, frozen budgets and input guards",handoffGame},
});

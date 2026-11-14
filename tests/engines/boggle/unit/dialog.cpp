// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/dialog.h"
#include "engines/boggle/classic/review_dialog.h"
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
});

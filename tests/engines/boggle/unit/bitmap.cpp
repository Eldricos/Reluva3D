// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/bitmap.h"
#include "engines/boggle/frontend/roster_dialog.h"
#include "tests/engines/boggle/bitmap_fixture.h"
#include "tests/engines/boggle/dialog_fixture.h"
#include "tests/support/test.h"
using namespace reluva;using namespace reluva::boggle;
namespace {
void pixels(){
    image::Image baseline;CHECK(!readBitmapResource(fixtures::bitmap(),baseline));CHECK(baseline.w==3 && baseline.h==2);
    CHECK(baseline.rgba[0]==11 && baseline.rgba[1]==22 && baseline.rgba[2]==33 && baseline.rgba[3]==255 && baseline.pixel(0,1)[0]==44);
    for(bool rgb:{false,true})for(bool top:{false,true}){
        const auto own=fixtures::bitmap(rgb,top);image::Image result;CHECK(!readBitmapResource(own,result) && result.rgba==baseline.rgba);
        for(size_t n=0;n<own.size();++n){auto b=own;b.resize(n);CHECK(bool(readBitmapResource(b,result)) && result.rgba.empty() && !result.w && !result.h);}
        auto b=own;test::fixtures::put32(b,20,0);CHECK(!readBitmapResource(b,result) && result.rgba==baseline.rgba);
        b=own;b.push_back(0);CHECK(bool(readBitmapResource(b,result)));
        b=own;test::fixtures::put32(b,20,1);CHECK(bool(readBitmapResource(b,result)));
        b=own;test::fixtures::put16(b,12,2);CHECK(bool(readBitmapResource(b,result)));
        b=own;test::fixtures::put16(b,14,16);CHECK(bool(readBitmapResource(b,result)));
        b=own;test::fixtures::put32(b,16,1);CHECK(bool(readBitmapResource(b,result)));
        b=own;test::fixtures::put32(b,8,0x80000000);CHECK(bool(readBitmapResource(b,result)));
        b=own;test::fixtures::put32(b,4,2049);CHECK(bool(readBitmapResource(b,result)));
        b=own;test::fixtures::put32(b,0,124);CHECK(bool(readBitmapResource(b,result)));
    }
    image::Image result;auto b=fixtures::bitmap(false,false,3,2,256);test::fixtures::put32(b,32,0);CHECK(!readBitmapResource(b,result) && result.rgba==baseline.rgba);
    b=fixtures::bitmap();b[48]=2;CHECK(bool(readBitmapResource(b,result)) && result.rgba.empty());
    b=fixtures::bitmap();test::fixtures::put32(b,32,257);CHECK(bool(readBitmapResource(b,result)));
    b=fixtures::bitmap();test::fixtures::put32(b,36,3);CHECK(bool(readBitmapResource(b,result)));
    b=fixtures::bitmap(true);test::fixtures::put32(b,32,1);CHECK(bool(readBitmapResource(b,result)));
    CHECK(bool(readBitmapResource(std::vector<uint8_t>(16*1024*1024+1),result)));
}
void portraits(){
    for(bool extended:{false,true}){
        const auto f=fixtures::rosterDialog(extended,true);std::vector<PeResource> own;CHECK(!readPeResources(test::fixtures::resourcePe(f.bytes,5,10000),own));FrontendRosterDialog layout;
        CHECK(!layout.read(own) && layout.installed()); // no bitmaps: preserve native labels
        for(unsigned i=0;i<3;++i){std::vector<PeResource> r;CHECK(!readPeResources(test::fixtures::resourcePe(fixtures::portrait(uint8_t(10+i*60)),2,1300+i),r));own.push_back(r[0]);}
        CHECK(!layout.read(own) && layout.installed());Surface screen;screen.create(640,480,PixelFormat::RGBA32);
        const std::array<std::string,4> names{"CPU Mr. Boggle","Named","","Human"};const std::array<ComputerLevel,4> kinds{ComputerLevel::Human,ComputerLevel::Beginner,ComputerLevel::MrBoggle,ComputerLevel::Human};layout.draw(screen,names,kinds,{});
        const unsigned red[]={10,70,130,10};for(size_t i=0;i<4;++i){auto p=layout.fields()[i];const auto* c=screen.row(p.y-40)+(p.x+p.w/2)*4;CHECK(c[0]==red[i] && c[1]==20 && c[2]==30 && c[3]==255);}
        auto changed=kinds;changed[0]=ComputerLevel::MrBoggle;layout.draw(screen,names,changed,{});auto p=layout.fields()[0];CHECK(screen.row(p.y-40)[(p.x+p.w/2)*4]==70);
        auto bad=own;bad.pop_back();CHECK(bool(layout.read(bad)) && !layout.installed());
        bad=own;bad[3].language=0x0411;CHECK(bool(layout.read(bad)));
        CHECK(!layout.read(own,0x0411) && !layout.installed());
        bad=own;bad[1].data.resize(39);CHECK(bool(layout.read(bad)));
        bad=own;bad[1].data=fixtures::bitmap(false,false,63,64);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[7]+(extended?12:8),0);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[7]+(extended?12:8)+2,20);CHECK(bool(layout.read(bad)));
        bad=own;test::fixtures::put16(bad[0].data,f.controls[10]+(extended?20:16),4111);CHECK(bool(layout.read(bad)));
    }
}
}
static const test::Suite suite("boggle.unit.bitmap",{
    {"indexed/BGR DIB pixels, row order, padding, palette and malformed data",[](const auto&){pixels();}},
    {"roster portraits follow kinds/empty slots, exact language and validated placement",[](const auto&){portraits();}},
});

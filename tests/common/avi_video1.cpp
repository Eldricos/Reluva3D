// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "common/video/avi_video1.h"
#include "tests/support/avi_fixture.h"
#include "tests/support/test.h"
#include <algorithm>
using namespace reluva;
using namespace reluva::test::fixtures;
namespace {
void blocks() {
    std::vector<uint8_t> two; aviWord(two,0x0001); aviWord(two,0x7c00); aviWord(two,0x03e0);
    video::AviVideo1 clip; CHECK(!clip.read(avi(4,4,{two,{1,0x84}})));
    CHECK(clip.frames.size()==2 && clip.rate==30 && clip.scale==1);
    if(clip.frames.size()!=2)return;
    CHECK(clip.frames[0].pixel(0,3)[0]==255 && clip.frames[0].pixel(0,3)[1]==0);
    CHECK(clip.frames[0].pixel(1,3)[1]==255); CHECK(clip.frames[0].pixel(0,0)[1]==255);
    CHECK(clip.frames[0].rgba==clip.frames[1].rgba);
    std::vector<uint8_t> eight;aviWord(eight,0);aviWord(eight,0x8000);
    for(uint16_t n : {uint16_t(1),uint16_t(2),uint16_t(3),uint16_t(4),uint16_t(5),uint16_t(6),uint16_t(7)})aviWord(eight,n);
    CHECK(!clip.read(avi(4,4,{eight})));
    CHECK(clip.frames[0].pixel(0,3)[2]==8); CHECK(clip.frames[0].pixel(2,3)[2]==24);
    CHECK(clip.frames[0].pixel(0,1)[2]==41); CHECK(clip.frames[0].pixel(2,1)[2]==57);
    std::vector<uint8_t> solid; aviWord(solid,0xfc00);aviWord(solid,0x83e0);aviWord(solid,0);
    CHECK(!clip.read(avi(8,4,{solid})));
    CHECK(clip.frames[0].pixel(0,0)[0]==255 && clip.frames[0].pixel(7,0)[1]==255);
}
void indexed() {
    video::AviVideo1 clip;CHECK(!clip.read(avi8(4,4,{{1,0,2,3},{1,0x84}})));
    CHECK(clip.frames.size()==2);if(clip.frames.size()!=2)return;
    auto pixel=[&](int x,int y,uint8_t color){const auto* p=clip.frames[0].pixel(x,y);CHECK(p[0]==color && p[1]==255-color && p[2]==uint8_t(color*3) && p[3]==255);};
    pixel(0,3,2);pixel(1,3,3);CHECK(clip.frames[0].rgba==clip.frames[1].rgba);
    CHECK(!clip.read(avi8(4,4,{{0,0x90,0,1,2,3,4,5,6,7}})));
    pixel(0,3,1);pixel(2,3,3);pixel(1,1,5);pixel(3,0,6);
    CHECK(!clip.read(avi8(4,4,{{7,0x88}})));pixel(2,2,7);
    CHECK(bool(clip.read(avi8(4,4,{{2,0x80}},2))));CHECK(clip.frames.empty());
    CHECK(bool(clip.read(avi8(4,4,{{0,0x90,0,1,2,3,4,5,6}}))));
    CHECK(bool(clip.read(avi8(4,4,{{0,0x80}},257))));
    auto data=avi8(4,4,{{0,0x80}});auto f=std::search(data.begin(),data.end(),"strf","strf"+4)-data.begin();avi32(data,size_t(f)+8+32,257);
    CHECK(bool(clip.read(data)));CHECK(clip.frames.empty() && clip.width==0);
}
void invalid() {
    const auto good=classicAvi();video::AviVideo1 clip;
    for(size_t n=0;n<good.size();++n){auto bytes=good;bytes.resize(n);CHECK(bool(clip.read(bytes)));CHECK(clip.frames.empty());}
    for(auto [w,h]:{std::pair{0,4},std::pair{5,4},std::pair{4,5},std::pair{4096,4},std::pair{4,-4}})
        CHECK(bool(clip.read(avi(w,h,{{0,0x80}}))));
    CHECK(bool(clip.read(avi(4,4,{{0,0x84}})))); // zero skip
    CHECK(bool(clip.read(avi(4,4,{{2,0x84}})))); // skip outside frame
    CHECK(bool(clip.read(avi(4,4,{{0,0}})))); // missing colors
    CHECK(bool(clip.read(avi(4,4,{{0,0x80,1,2,3}}))));
    CHECK(bool(clip.read(avi(4,4,std::vector<std::vector<uint8_t>>(513,{0,0x80})))));
    CHECK(bool(clip.read(avi(2048,2048,std::vector<std::vector<uint8_t>>(9,{0,0x80}))))); // decoded limit before allocation
    auto data=good;data.push_back(0);CHECK(bool(clip.read(data)));
    CHECK(!clip.read(good));CHECK(clip.frames.size()==55);
    data=good;for(size_t i=0;i+4<data.size();++i)if(std::memcmp(data.data()+i,"CRAM",4)==0){data[i]='X';break;}
    CHECK(bool(clip.read(data)));CHECK(clip.frames.empty() && clip.width==0);
}
}
static const test::Suite suite("common.unit.avi_video1", {
    {"two/eight colors, bottom-up blocks, solids and delta skips",[](const auto&){blocks();}},
    {"indexed solids, two/eight colors, palette bounds and retained blocks",[](const auto&){indexed();}},
    {"bounds, truncation, unsupported formats and clearing",[](const auto&){invalid();}},
});

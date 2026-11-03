// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "common/video/avi_cinepak.h"
#include "tests/support/cinepak_fixture.h"
#include "tests/support/test.h"
#include <algorithm>
using namespace reluva;
using namespace reluva::test::fixtures;
namespace {
void pixel(const image::Image& im,int x,int y,int r,int g,int b) {
    const auto* p=im.pixel(x,y);CHECK(p[0]==r && p[1]==g && p[2]==b && p[3]==255);
}
void basics() {
    video::AviCinepak v;CHECK(!v.read(grayMovie()));CHECK(v.frames.size()==2 && v.width==4 && v.height==4);
    CHECK(v.durationMillis()==200 && v.frameAt(99)==0 && v.frameAt(100)==1 && v.frameAt(200)==1 && v.frameAt(200,true)==0);
    CHECK(v.frameAt(UINT64_MAX,true)<2);
    if(v.frames.size()!=2)return;
    pixel(v.frames[0],0,0,30,30,30);pixel(v.frames[1],3,3,90,90,90);
    CHECK(v.soundtrack.samples==std::vector<int16_t>({-32768,0,32512}));
    auto p=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x2200,{10,20,30,40,1,1}),cvChunk(0x3200,{0})})});
    CHECK(!v.read(cinepakAvi(4,4,{p},{0,128,255,127},16,2)));
    if(v.frames.empty())return;
    pixel(v.frames[0],0,0,12,9,12);pixel(v.frames[0],3,0,22,19,22);
    pixel(v.frames[0],0,3,32,29,32);pixel(v.frames[0],3,3,42,39,42);
    CHECK(v.soundtrack.samples==std::vector<int16_t>({-32768,32767}));
    std::vector<uint8_t> refs;cv32(refs,0x80000000u);refs.insert(refs.end(),{0,1,2,3});
    p=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x2000,{40,41,42,43,1,1,80,81,82,83,255,255,120,121,122,123,3,254,250,251,252,253,10,10}),cvChunk(0x3000,refs)})});
    CHECK(!v.read(cinepakAvi(4,4,{p})));if(v.frames.empty())return;
    pixel(v.frames[0],0,0,42,39,42);pixel(v.frames[0],2,0,78,81,78);
    pixel(v.frames[0],0,2,116,121,126);pixel(v.frames[0],3,3,255,238,255);
}
void deltas() {
    auto first=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x2600,{10,20,30,40}),cvChunk(0x3200,{0})})});
    std::vector<uint8_t> update;cv32(update,0x80000000u);update.insert(update.end(),{50,60,70,80});
    std::vector<uint8_t> refs;cv32(refs,0x80000000u);refs.push_back(0);
    auto second=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x2700,update),cvChunk(0x3100,refs)},0,true)},1);
    auto third=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x3100,{0,0,0,0})},0,true)},1);
    video::AviCinepak v;CHECK(!v.read(cinepakAvi(4,4,{first,second,third})));CHECK(v.frames.size()==3);
    if(v.frames.size()==3){pixel(v.frames[1],3,3,80,80,80);CHECK(v.frames[1].rgba==v.frames[2].rgba);}
    // Last macroblock's update bit ends one flag word; its V4 selector starts the next.
    first=cvFrame(128,4,{cvStrip(128,4,{cvChunk(0x2600,{10,10,10,10}),cvChunk(0x2400,{99,99,99,99}),cvChunk(0x3200,std::vector<uint8_t>(32))})});
    refs.clear();cv32(refs,1);cv32(refs,0x80000000u);refs.insert(refs.end(),{0,0,0,0});
    second=cvFrame(128,4,{cvStrip(128,4,{cvChunk(0x3100,refs)},0,true)},1);
    CHECK(!v.read(cinepakAvi(128,4,{first,second})));if(v.frames.size()!=2)return;
    pixel(v.frames[1],120,0,10,10,10);pixel(v.frames[1],124,0,99,99,99);
}
void strips() {
    auto strip=[](uint8_t value,int top){return cvStrip(4,top?8:4,{cvChunk(0x2600,{value,value,value,value}),cvChunk(0x3200,{0})},top);};
    auto a=cvFrame(4,8,{strip(10,0),strip(20,4)},1);
    auto b=cvFrame(4,8,{strip(30,0),cvStrip(4,4,{cvChunk(0x3200,{0})})},1);
    auto c=cvFrame(4,8,{cvStrip(4,4,{cvChunk(0x3200,{0})}),cvStrip(4,4,{cvChunk(0x3200,{0})})},0);
    video::AviCinepak v;CHECK(!v.read(cinepakAvi(4,8,{a,b,c})));if(v.frames.size()!=3)return;
    pixel(v.frames[0],0,7,20,20,20);pixel(v.frames[1],0,7,20,20,20);pixel(v.frames[2],0,7,30,30,30);
}
void malformed() {
    const auto good=grayMovie();video::AviCinepak v;
    auto bad=[&](std::vector<uint8_t> bytes){CHECK(!v.read(good));CHECK(bool(v.read(bytes)));CHECK(v.frames.empty() && v.soundtrack.samples.empty() && !v.width && !v.rate);};
    for(size_t n=0;n<good.size();++n)bad(std::vector<uint8_t>(good.begin(),good.begin()+n));
    auto bytes=good;avi32(bytes,4,UINT32_MAX);bad(bytes);
    auto packet=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x3100,{0,0,0,0})})});bad(cinepakAvi(4,4,{packet})); // no prior frame
    packet=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x3200,{0})})});bad(cinepakAvi(4,4,{packet})); // undefined entry
    packet=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x2700,{128,0,0,0}),cvChunk(0x3200,{0})})});bad(cinepakAvi(4,4,{packet}));
    packet=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x2600,std::vector<uint8_t>(257*4)),cvChunk(0x3200,{0})})});bad(cinepakAvi(4,4,{packet}));
    packet=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x2900,{})})});bad(cinepakAvi(4,4,{packet}));
    auto one=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x2600,{1,1,1,1}),cvChunk(0x3200,{0})})});
    for(auto id:{0x3000,0x3100,0x3200}) {
        packet=cvFrame(4,4,{cvStrip(4,4,{cvChunk(0x2600,{1,1,1,1}),cvChunk(uint16_t(id),{})})});
        bad(cinepakAvi(4,4,{packet}));
    }
    auto corrupt=one;corrupt[3]--;bad(cinepakAvi(4,4,{corrupt}));
    corrupt=one;corrupt[16]=0;corrupt[17]=4;bad(cinepakAvi(4,4,{corrupt})); // left != 0
    bad(cinepakAvi(4,4,std::vector<std::vector<uint8_t>>(513,one)));
    bad(cinepakAvi(2048,2048,std::vector<std::vector<uint8_t>>(9,one))); // decoded budget
    bad(cinepakAvi(4,4,{one},{1,2,3},16,2)); // partial stereo sample
    bytes=cinepakAvi(4,4,{one},{1},8,1);auto at=std::search(bytes.begin(),bytes.end(),"01wb","01wb"+4)-bytes.begin();bytes[size_t(at)+1]='2';bad(bytes);
}
}
static const test::Suite suite("common.unit.avi_cinepak",{
 {"V1/V4 placement, signed chroma rounding and PCM",[](const auto&){basics();}},
 {"partial codebooks, retained pixels and flags crossing word boundaries",[](const auto&){deltas();}},
 {"strip inheritance, independent books and relative strip heights",[](const auto&){strips();}},
 {"truncation, invalid references, structure and resource limits",[](const auto&){malformed();}},
});

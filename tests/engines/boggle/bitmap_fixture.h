// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "tests/support/pe_resources_fixture.h"
#include <algorithm>
namespace reluva::boggle::fixtures {
inline std::vector<uint8_t> bitmap(bool rgb=false,bool topDown=false,unsigned w=3,unsigned h=2,unsigned colors=2){
    using namespace test::fixtures;const unsigned pitch=(w*(rgb?24:8)+31)/32*4,offset=40+(rgb?0:colors*4);
    std::vector<uint8_t> b(offset+pitch*h,99);std::fill(b.begin(),b.begin()+offset,0);
    put32(b,0,40);put32(b,4,w);put32(b,8,topDown?uint32_t(0-h):h);put16(b,12,1);put16(b,14,rgb?24:8);put32(b,20,pitch*h);
    if(!rgb){put32(b,32,colors);put32(b,36,colors);for(unsigned i=0;i<colors;++i){b[40+i*4]=uint8_t(33+i*33);b[41+i*4]=uint8_t(22+i*33);b[42+i*4]=uint8_t(11+i*33);b[43+i*4]=222;}}
    for(unsigned row=0;row<h;++row)for(unsigned x=0;x<w;++x){const unsigned y=topDown?row:h-1-row,index=(x+y)%2;const size_t at=offset+row*pitch+x*(rgb?3:1);if(rgb){b[at]=uint8_t(33+index*33);b[at+1]=uint8_t(22+index*33);b[at+2]=uint8_t(11+index*33);}else b[at]=uint8_t(index);}
    return b;
}
inline std::vector<uint8_t> portrait(uint8_t red){auto b=bitmap(false,false,64,64);for(unsigned i=0;i<2;++i){b[40+i*4]=30;b[41+i*4]=20;b[42+i*4]=red;}return b;}
}

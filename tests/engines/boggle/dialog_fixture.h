// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "tests/support/pe_resources_fixture.h"
#include <string_view>
#include <algorithm>
namespace reluva::boggle::fixtures {
struct DialogFixture {std::vector<uint8_t> bytes;std::vector<size_t> controls;size_t caption=0;};
inline DialogFixture dialog(bool extended=false,bool extra=false){
    DialogFixture f;auto& b=f.bytes;
    auto word=[&](uint16_t n){b.push_back(uint8_t(n));b.push_back(uint8_t(n>>8));};
    auto integer=[&](uint32_t n){word(uint16_t(n));word(uint16_t(n>>16));};
    auto text=[&](std::u16string_view s){for(auto c:s)word(c);word(0);};
    if(extended){word(1);word(0xffff);integer(0x12345678);integer(0x1234);integer(0x40);}
    else {integer(0x40);integer(0x1234);}
    word(5);word(uint16_t(-5));word(7);word(300);word(120);word(0);word(0);f.caption=b.size();text(u"Own prompt");
    word(8);if(extended){word(650);b.push_back(1);b.push_back(2);}text(u"InventedFont");
    const unsigned ids[]={6,7,4,3,15801};
    for(unsigned i=0;i<5;++i){while(b.size()%4)b.push_back(0);f.controls.push_back(b.size());
        if(extended){integer(42+i);integer(0);integer(0x50000000);}else{integer(0x50000000);integer(0);}
        word(i<4?uint16_t(230-i*70):30);word(i<4?90:28);word(i<4?50:230);word(i<4?16:34);
        if(extended)integer(ids[i]);else word(uint16_t(ids[i]));word(0xffff);word(i<4?0x80:0x82);text(i<4?u"Own choice":u"Own body");
        if(extra && i==0){word(extended?3:5);b.insert(b.end(),{1,2,3});}else word(0);
    }
    return f;
}
inline std::vector<uint8_t> dialogLanguage(const std::vector<uint8_t>& payload){
    using namespace test::fixtures;
    std::array<std::u16string,16> text{};text[1]=u"Own menu";const auto strings=stringBlock(text);
    const size_t stringAt=(160+payload.size()+3)&~size_t(3);std::vector<uint8_t> tree(stringAt+strings.size());
    put16(tree,14,2);put32(tree,16,5);put32(tree,20,0x80000020);put32(tree,24,6);put32(tree,28,0x80000060);
    put16(tree,46,1);put32(tree,48,16003);put32(tree,52,0x80000038);
    put16(tree,70,1);put32(tree,72,1033);put32(tree,76,80);put32(tree,80,0x3000+160);put32(tree,84,uint32_t(payload.size()));put32(tree,88,1200);
    put16(tree,110,1);put32(tree,112,1);put32(tree,116,0x80000078);
    put16(tree,134,1);put32(tree,136,1033);put32(tree,140,144);put32(tree,144,uint32_t(0x3000+stringAt));put32(tree,148,uint32_t(strings.size()));put32(tree,152,1200);
    std::copy(payload.begin(),payload.end(),tree.begin()+160);std::copy(strings.begin(),strings.end(),tree.begin()+stringAt);
    auto pe=resourcePe({});const size_t raw=(tree.size()+511)&~size_t(511);pe.resize(512+raw);
    put32(pe,0x98+96+20,uint32_t(tree.size()));put32(pe,0x98+224+8,uint32_t(tree.size()));put32(pe,0x98+224+16,uint32_t(raw));
    std::copy(tree.begin(),tree.end(),pe.begin()+512);return pe;
}

}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "tests/support/pe_resources_fixture.h"
#include <string_view>
#include <algorithm>
namespace reluva::boggle::fixtures {
struct DialogFixture {std::vector<uint8_t> bytes;std::vector<size_t> controls;size_t caption=0;};
inline DialogFixture dialog(bool extended=false,bool extra=false,bool notice=false){
    DialogFixture f;auto& b=f.bytes;
    auto word=[&](uint16_t n){b.push_back(uint8_t(n));b.push_back(uint8_t(n>>8));};
    auto integer=[&](uint32_t n){word(uint16_t(n));word(uint16_t(n>>16));};
    auto text=[&](std::u16string_view s){for(auto c:s)word(c);word(0);};
    if(extended){word(1);word(0xffff);integer(0x12345678);integer(0x1234);integer(0x40);}
    else {integer(0x40);integer(0x1234);}
    word(notice?3:5);word(uint16_t(-5));word(7);word(notice?240:300);word(notice?150:120);word(0);word(0);f.caption=b.size();text(u"Own prompt");
    word(8);if(extended){word(650);b.push_back(1);b.push_back(2);}text(u"InventedFont");
    const unsigned reviewIds[]={6,7,4,3,15801},noticeIds[]={1348,1349,1350};
    const unsigned count=notice?3:5;
    for(unsigned i=0;i<count;++i){while(b.size()%4)b.push_back(0);
        f.controls.push_back(b.size());
        if(extended){integer(42+i);integer(0);integer(0x50000000);}else{integer(0x50000000);integer(0);}
        const bool action=i+1<count;
        if(notice){word(action?(i?160:30):20);word(action?120:24);word(action?50:200);word(action?16:70);}
        else {word(action?uint16_t(230-i*70):30);word(action?90:28);word(action?50:230);word(action?16:34);}
        const unsigned id=notice?noticeIds[i]:reviewIds[i];
        if(extended)integer(id);else word(uint16_t(id));word(0xffff);word(action?0x80:0x82);text(action?u"Own choice":u"Own body");
        if(extra && i==0){word(extended?3:5);b.insert(b.end(),{1,2,3});}else word(0);
    }
    return f;
}
inline DialogFixture scoresDialog(bool extended=false){
    DialogFixture f;auto& b=f.bytes;
    auto word=[&](uint16_t n){b.push_back(uint8_t(n));b.push_back(uint8_t(n>>8));};
    auto integer=[&](uint32_t n){word(uint16_t(n));word(uint16_t(n>>16));};
    auto text=[&](std::u16string_view s){for(auto c:s)word(c);word(0);};
    if(extended){word(1);word(0xffff);integer(0);integer(0);integer(0);}else{integer(0);integer(0);}
    word(31);word(0);word(0);word(420);word(270);word(0);word(0);f.caption=b.size();text(u"Own scores");
    auto control=[&](unsigned id,unsigned klass,int x,int y,int w,int h){
        while(b.size()%4)b.push_back(0);
        f.controls.push_back(b.size());
        if(extended){integer(0);integer(0);integer(0x50000000);}else{integer(0x50000000);integer(0);}
        word(uint16_t(x));word(uint16_t(y));word(uint16_t(w));word(uint16_t(h));
        if(extended)integer(id);else word(uint16_t(id));word(0xffff);word(uint16_t(klass));text(u"Own cell");word(0);
    };
    for(unsigned row=0;row<10;++row)for(unsigned col=0;col<3;++col)
        control(15400+col*10+row,0x82,col==0?40:col==1?180:280,80+int(row)*14,col==0?40:col==1?60:90,12);
    control(1,0x80,350,240,50,16);return f;
}
inline DialogFixture matchDialog(bool extended=false){
    DialogFixture f;auto& b=f.bytes;
    auto word=[&](uint16_t n){b.push_back(uint8_t(n));b.push_back(uint8_t(n>>8));};
    auto integer=[&](uint32_t n){word(uint16_t(n));word(uint16_t(n>>16));};
    auto text=[&](std::u16string_view s){for(auto c:s)word(c);word(0);};
    if(extended){word(1);word(0xffff);integer(0);integer(0);integer(0);}else{integer(0);integer(0);}
    word(11);word(0);word(0);word(300);word(210);word(0);word(0);f.caption=b.size();text(u"Own match");
    auto control=[&](unsigned id,unsigned klass,int x,int y,int w,int h){
        while(b.size()%4)b.push_back(0);
        f.controls.push_back(b.size());
        if(extended){integer(0);integer(0);integer(0x50000000);}else{integer(0x50000000);integer(0);}
        word(uint16_t(x));word(uint16_t(y));word(uint16_t(w));word(uint16_t(h));
        if(extended)integer(id);else word(uint16_t(id));word(0xffff);word(uint16_t(klass));text(u"Own field");word(0);
    };
    for(unsigned row=0;row<4;++row){control(15400+row,0x82,30,70+int(row)*20,120,14);control(15410+row,0x82,180,70+int(row)*20,50,14);}
    control(2,0x80,130,170,70,16);control(1,0x80,220,170,60,16);control(1352,0x82,20,15,260,28);return f;
}
inline DialogFixture selectionDialog(bool extended=false){
    DialogFixture f;auto& b=f.bytes;
    auto word=[&](uint16_t n){b.push_back(uint8_t(n));b.push_back(uint8_t(n>>8));};
    auto integer=[&](uint32_t n){word(uint16_t(n));word(uint16_t(n>>16));};
    auto text=[&](std::u16string_view s){for(auto c:s)word(c);word(0);};
    if(extended){word(1);word(0xffff);integer(0);integer(0);integer(0);}else{integer(0);integer(0);}
    word(5);word(0);word(0);word(400);word(300);word(0);word(0);f.caption=b.size();text(u"Own selection");
    for(int i=4;i>=0;--i){
        while(b.size()%4)b.push_back(0);
        f.controls.push_back(b.size());
        if(extended){integer(0);integer(0);integer(0x50000000);}else{integer(0x50000000);integer(0);}
        word(uint16_t(10+i*78));word(150);word(60);word(75);
        if(extended)integer(15001+i);else word(uint16_t(15001+i));word(0xffff);word(0x82);text(u"Own mode");word(0);
    }
    return f;
}
inline DialogFixture handoffDialog(bool extended=false){
    DialogFixture f;auto& b=f.bytes;
    auto word=[&](uint16_t n){b.push_back(uint8_t(n));b.push_back(uint8_t(n>>8));};
    auto integer=[&](uint32_t n){word(uint16_t(n));word(uint16_t(n>>16));};
    auto text=[&](std::u16string_view s){for(auto c:s)word(c);word(0);};
    if(extended){word(1);word(0xffff);integer(0);integer(0);integer(0);}else{integer(0);integer(0);}
    word(2);word(0);word(0);word(320);word(220);word(0);word(0);f.caption=b.size();text(u"Own handoff");
    for(unsigned i=0;i<2;++i){
        while(b.size()%4)b.push_back(0);
        f.controls.push_back(b.size());
        if(extended){integer(0);integer(0);integer(0x50000000);}else{integer(0x50000000);integer(0);}
        word(i?240:20);word(i?180:35);word(i?60:280);word(i?16:100);
        if(extended)integer(i?1:15901);else word(i?1:15901);word(0xffff);word(i?0x80:0x82);text(u"Own handoff field");word(0);
    }
    return f;
}
inline DialogFixture nameDialog(bool extended=false){
    DialogFixture f;auto& b=f.bytes;
    auto word=[&](uint16_t n){b.push_back(uint8_t(n));b.push_back(uint8_t(n>>8));};
    auto integer=[&](uint32_t n){word(uint16_t(n));word(uint16_t(n>>16));};
    auto text=[&](std::u16string_view s){for(auto c:s)word(c);word(0);};
    if(extended){word(1);word(0xffff);integer(0);integer(0);integer(0);}else{integer(0);integer(0);}
    word(3);word(0);word(0);word(260);word(140);word(0);word(0);f.caption=b.size();text(u"Own name editor");
    for(unsigned i=0;i<3;++i){
        while(b.size()%4)b.push_back(0);
        f.controls.push_back(b.size());
        if(extended){integer(0);integer(0);integer(0x50000000);}else{integer(0x50000000);integer(0);}
        word(i==2?180:10);word(i==0?8:i==1?32:92);word(i==0?220:i==1?240:60);word(i==0?12:i==1?20:18);
        const unsigned id=i==0?65535:i==1?1601:1;if(extended)integer(id);else word(uint16_t(id));word(0xffff);word(i==0?0x82:i==1?0x81:0x80);text(u"Own field");word(0);
    }
    return f;
}
inline DialogFixture rosterDialog(bool extended=false){
    DialogFixture f;auto& b=f.bytes;
    auto word=[&](uint16_t n){b.push_back(uint8_t(n));b.push_back(uint8_t(n>>8));};
    auto integer=[&](uint32_t n){word(uint16_t(n));word(uint16_t(n>>16));};
    auto text=[&](std::u16string_view s){for(auto c:s)word(c);word(0);};
    if(extended){word(1);word(0xffff);integer(0);integer(0);integer(0);}else{integer(0);integer(0);}
    word(7);word(0);word(0);word(360);word(160);word(0);word(0);f.caption=b.size();text(u"Own roster");
    auto control=[&](unsigned id,unsigned klass,int x,int y,int w,int h){
        while(b.size()%4)b.push_back(0);
        f.controls.push_back(b.size());
        if(extended){integer(0);integer(0);integer(0x50000000);}else{integer(0x50000000);integer(0);}
        word(uint16_t(x));word(uint16_t(y));word(uint16_t(w));word(uint16_t(h));
        if(extended)integer(id);else word(uint16_t(id));word(0xffff);word(uint16_t(klass));text(u"Own roster control");word(0);
    };
    control(1344,0x82,12,10,336,26);
    for(unsigned i=0;i<4;++i)control(4107+i,0x85,12+int(i)*86,80,68,200); // popup extends beyond client
    control(1,0x80,150,126,58,16);control(2,0x80,218,126,58,16);return f;
}
inline DialogFixture optionsDialog(bool extended=false){
    DialogFixture f;auto& b=f.bytes;
    auto word=[&](uint16_t n){b.push_back(uint8_t(n));b.push_back(uint8_t(n>>8));};
    auto integer=[&](uint32_t n){word(uint16_t(n));word(uint16_t(n>>16));};
    auto text=[&](std::u16string_view s){for(auto c:s)word(c);word(0);};
    if(extended){word(1);word(0xffff);integer(0);integer(0);integer(0);}else{integer(0);integer(0);}
    word(7);word(0);word(0);word(280);word(145);word(0);word(0);f.caption=b.size();text(u"Own options");
    const unsigned ids[]={1312,1313,1311,1299,1300,1907,15303};
    const int rects[][4]={{10,10,120,10},{10,28,120,10},{150,10,120,10},{150,62,100,10},{150,82,110,10},{10,64,125,24},{35,110,70,10}};
    for(unsigned i=0;i<7;++i){
        while(b.size()%4)b.push_back(0);
        f.controls.push_back(b.size());
        if(extended){integer(0);integer(0);integer(0x50000000);}else{integer(0x50000000);integer(0);}
        for(auto n:rects[i])word(uint16_t(n));
        if(extended)integer(ids[i]);else word(uint16_t(ids[i]));
        if(i==5)text(u"msctls_trackbar32");else {word(0xffff);word(0x80);}text(u"Own option");word(0);
    }
    return f;
}
inline std::vector<uint8_t> dialogLanguage(const std::vector<uint8_t>& payload,uint32_t id=16003){
    using namespace test::fixtures;
    std::array<std::u16string,16> text{};text[1]=u"Own menu";const auto strings=stringBlock(text);
    const size_t stringAt=(160+payload.size()+3)&~size_t(3);std::vector<uint8_t> tree(stringAt+strings.size());
    put16(tree,14,2);put32(tree,16,5);put32(tree,20,0x80000020);put32(tree,24,6);put32(tree,28,0x80000060);
    put16(tree,46,1);put32(tree,48,id);put32(tree,52,0x80000038);
    put16(tree,70,1);put32(tree,72,1033);put32(tree,76,80);put32(tree,80,0x3000+160);put32(tree,84,uint32_t(payload.size()));put32(tree,88,1200);
    put16(tree,110,1);put32(tree,112,1);put32(tree,116,0x80000078);
    put16(tree,134,1);put32(tree,136,1033);put32(tree,140,144);put32(tree,144,uint32_t(0x3000+stringAt));put32(tree,148,uint32_t(strings.size()));put32(tree,152,1200);
    std::copy(payload.begin(),payload.end(),tree.begin()+160);std::copy(strings.begin(),strings.end(),tree.begin()+stringAt);
    auto pe=resourcePe({});const size_t raw=(tree.size()+511)&~size_t(511);pe.resize(512+raw);
    put32(pe,0x98+96+20,uint32_t(tree.size()));put32(pe,0x98+224+8,uint32_t(tree.size()));put32(pe,0x98+224+16,uint32_t(raw));
    std::copy(tree.begin(),tree.end(),pe.begin()+512);return pe;
}

}

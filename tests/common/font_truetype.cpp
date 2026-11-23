// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "common/font_truetype.h"
#include "tests/support/truetype_fixture.h"
#include "tests/support/test.h"
#include <algorithm>
using namespace reluva;
namespace {
using namespace test::fixtures;
void outlines() {
    for(bool longLoca:{false,true})for(bool shortMetrics:{false,true})for(bool packed:{false,true}) {
        font::TrueTypeAscii font;CHECK(!font.read(asciiTrueType(longLoca,shortMetrics,packed),20));CHECK(font.loaded());
        CHECK(font.measure("A B")==54);CHECK(font.measure("\x01")==font.measure("?"));
        Surface screen;screen.create(80,40,PixelFormat::RGBA32);std::fill(screen.pixels.begin(),screen.pixels.end(),255);
        CHECK(font.draw(screen,0,20,"A BC",{0,0,0},80)==72);
        CHECK(screen.row(6)[2*4]==0);CHECK(screen.row(12)[8*4]==255); // outer and hole
        CHECK(screen.row(12)[36*4+8*4]==0); // curved cap interior
        CHECK(screen.row(12)[54*4+8*4]==0); // implied-point contour interior
        CHECK(screen.row(30)[0]==255);
        CHECK(std::any_of(screen.pixels.begin(),screen.pixels.end(),[](uint8_t n){return n>0 && n<255;}));
        const auto before=screen.pixels;CHECK(font.draw(screen,0,20,"ABC",{255,0,0},0)==0);CHECK(before==screen.pixels);
        CHECK(font.draw(screen,-10,-1,"ABC",{255,0,0},36)==26); // clipped to surface and width
        CHECK(font.draw(screen,INT32_MAX,INT32_MIN,"A",{0,0,0},20)==INT32_MAX);
        Surface indexed;indexed.create(2,2,PixelFormat::CLUT8);CHECK(font.draw(indexed,0,0,"A",{},20)==0);
    }
}
void unicode(){
    const auto original=asciiTrueType();font::TrueTypeUnicode font;
    const uint32_t chars[]={'A','B','C',' ',0x259};
    CHECK(!font.read(original,20,chars) && font.loaded() && font.has('A') && !font.has(0x259));
    CHECK(font.advance('A')==18 && font.advance(0x259)==font.advance('?'));
    Surface s;s.create(80,40,PixelFormat::RGBA32);std::fill(s.pixels.begin(),s.pixels.end(),255);
    font.draw(s,0,20,'A',{0,0,0});CHECK(s.row(6)[2*4]==0 && s.row(12)[8*4]==255);
    const auto plain=s.pixels;font.draw(s,0,20,'A',{0,0,0},true,true);CHECK(s.pixels!=plain);
    font.draw(s,INT32_MAX,INT32_MIN,'A',{},true,true);
    for(size_t n=0;n<original.size();++n){CHECK(bool(font.read({original.begin(),original.begin()+n},20,chars)));CHECK(!font.loaded());}
    auto b=original;tt32(b,ttTable(b,"head"),0);CHECK(bool(font.read(b,20,chars)));
    const uint32_t invalid[]={0xd800};CHECK(bool(font.read(original,20,invalid)));
}
void unicodeComposite(){
    using Bytes=std::vector<uint8_t>;
    const auto original=asciiTrueType();std::map<std::string,Bytes> tables;
    for(const auto* tag:{"head","hhea","maxp","hmtx","glyf","loca"}){
        const auto off=ttTable(original,tag);size_t end=original.size();
        for(size_t n=0;n<7;++n){const auto d=12+n*16;const size_t next=uint32_t(original[d+8])<<24|uint32_t(original[d+9])<<16|uint32_t(original[d+10])<<8|original[d+11];if(next>off)end=std::min(end,next);}
        tables[tag]={original.begin()+off,original.begin()+end};
    }
    auto& loca=tables["loca"];auto offset=[&](size_t id){return size_t(loca[id*2])*512+size_t(loca[id*2+1])*2;};
    const auto at=offset(3);Bytes composite(18);tt16(composite,0,-1);tt16(composite,2,100);tt16(composite,6,900);tt16(composite,8,800);
    tt16(composite,10,3);tt16(composite,12,1);tt16(composite,14,100);
    auto& glyf=tables["glyf"];glyf.insert(glyf.begin()+at,composite.begin(),composite.end());
    for(size_t id:{4u,5u})tt16(loca,id*2,int(offset(id)/2+9));
    tt16(tables["hmtx"],14,100);
    auto& cmap=tables["cmap"];cmap.resize(28+4*12);tt16(cmap,2,1);tt16(cmap,4,3);tt16(cmap,6,10);tt32(cmap,8,12);
    tt16(cmap,12,12);tt32(cmap,16,uint32_t(cmap.size()-12));tt32(cmap,24,4);
    const unsigned cps[]={'?','A',0x259,0x301},ids[]={1,1,3,4};
    for(size_t n=0;n<4;++n){tt32(cmap,28+n*12,cps[n]);tt32(cmap,32+n*12,cps[n]);tt32(cmap,36+n*12,ids[n]);}
    Bytes b(12+tables.size()*16);tt32(b,0,0x10000);tt16(b,4,int(tables.size()));size_t index=0;
    for(const auto&[tag,data]:tables){while(b.size()%4)b.push_back(0);const auto d=12+index++*16;std::copy(tag.begin(),tag.end(),b.begin()+d);tt32(b,d+8,uint32_t(b.size()));tt32(b,d+12,uint32_t(data.size()));b.insert(b.end(),data.begin(),data.end());}
    font::TrueTypeUnicode font;CHECK(!font.read(original,20,cps)&&!font.has(0x259));
    CHECK(!font.addFallback(b,cps)&&font.has(0x259)&&font.advance(0x301)==0);
    CHECK(bool(font.addFallback({},cps))&&font.has(0x259));
    CHECK(!font.read(b,20,cps)&&font.has(0x259)&&font.advance(0x301)==0);
    Surface screen;screen.create(30,30,PixelFormat::RGBA32);std::fill(screen.pixels.begin(),screen.pixels.end(),255);
    font.draw(screen,0,20,0x259,{0,0,0});CHECK(screen.row(6)[4*4]==0&&screen.row(12)[10*4]==255);
    auto invalid=[&](Bytes bad){CHECK(bool(font.read(bad,20,cps))&&!font.loaded());};
    auto bad=b;tt16(bad,ttTable(b,"glyf")+at+12,3);invalid(bad); // self cycle
    bad=b;tt16(bad,ttTable(b,"glyf")+at+10,1);invalid(bad); // unsupported point attachment
    bad=b;tt32(bad,ttTable(b,"cmap")+36,999);invalid(bad); // glyph outside font
    bad=b;tt32(bad,ttTable(b,"cmap")+40,'?');invalid(bad); // overlapping cmap groups
}
void malformed() {
    const auto original=asciiTrueType();font::TrueTypeAscii font;
    auto bad=[&](std::vector<uint8_t> b){CHECK(!font.read(original,20));CHECK(bool(font.read(b,20)));CHECK(!font.loaded());CHECK(font.measure("ABC")==0);};
    for(size_t n=0;n<original.size();++n)bad(std::vector<uint8_t>(original.begin(),original.begin()+n));
    auto b=original;tt32(b,20,0xffffffff);bad(b); // table offset
    b=original;tt16(b,ttTable(b,"head")+18,0);bad(b);
    b=original;tt16(b,ttTable(b,"head")+50,2);bad(b);
    b=original;tt16(b,ttTable(b,"hhea")+34,0);bad(b);
    b=original;tt16(b,ttTable(b,"loca")+4,65535);bad(b);
    b=original;const auto cmap=ttTable(b,"cmap");tt16(b,cmap+40,65534);bad(b);
    b=original;tt16(b,ttTable(b,"glyf"),-1);bad(b); // composite ASCII unsupported
    b=original;tt16(b,ttTable(b,"glyf")+14,65535);bad(b);
    b=original;b[ttTable(b,"glyf")+16]=255;bad(b); // flag run exceeds point count
    CHECK(bool(font.read(original,0)));CHECK(bool(font.read(original,49)));
}
}
static const test::Suite suite("common.unit.font_truetype",{
    {"Unicode format 12, composite offsets, combining marks, cycles and malformed groups",[](const auto&){unicodeComposite();}},
    {"Unicode subset metrics, fallback, styles, clipping and all truncations",[](const auto&){unicode();}},
    {"metrics, winding holes, quadratic curves, implied points, antialiasing and clipping",[](const auto&){outlines();}},
    {"all truncations, table/glyph bounds and unsupported ASCII composites",[](const auto&){malformed();}},
});

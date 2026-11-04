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
    {"metrics, winding holes, quadratic curves, implied points, antialiasing and clipping",[](const auto&){outlines();}},
    {"all truncations, table/glyph bounds and unsupported ASCII composites",[](const auto&){malformed();}},
});

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/definitions.h"
#include "engines/boggle/classic/definition_view.h"
#include "tests/engines/boggle/definition_fixture.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <limits>
using namespace reluva;using namespace reluva::boggle;
namespace {
void reader(){
    const auto bytes=fixtures::definitionArchive();Definitions d;CHECK(!d.read(bytes) && d.entries()==3 && d.headwords()==2);
    std::vector<DefinitionEntry> rows;CHECK(!d.lookup("taz",rows) && rows.size()==2);if(rows.size()==2)CHECK(rows[0].text=="taz\nOwn text." && rows[1].text=="TAZ\nOther text.");
    CHECK(!d.lookup("QUIZ",rows) && rows.size()==1);if(!rows.empty())CHECK(rows[0].headword=="QU-IZ" && rows[0].id==3 && rows[0].text=="QU-IZ\nFixture definition.\nOwn history.");
    CHECK(!d.lookup("MISSING",rows) && rows.empty());CHECK(bool(d.lookup("A B",rows)) && rows.empty());
    DefinitionEntry entry;CHECK(!d.entry(1,entry) && entry.id==1);CHECK(bool(d.entry(0,entry)) && entry.text.empty());CHECK(bool(d.entry(4,entry)));
    auto invalid=[&](std::vector<uint8_t> b){CHECK(bool(d.read(b)) && d.entries()==0 && d.headwords()==0);};
    auto b=bytes;b.resize(0x500);invalid(b);b=bytes;b[0x476]=1;invalid(b);b=bytes;b[0x472]=0;b[0x473]=3;invalid(b);
    b=bytes;b[0x404]='X';invalid(b);b=bytes;b[0x4a0]=0;b[0x4a1]=10;invalid(b); // repeated section tag
    b=bytes;b[0x600+4]=1;invalid(b); // directory is in symbols, with the previous terminator
    b=bytes;test::fixtures::put16(b,0x608+12,6);invalid(b); // cyclic pair
    b=bytes;test::fixtures::put16(b,0x608+12,9);invalid(b); // child outside the codebook
    b=bytes;test::fixtures::put16(b,0x608+2,65535);invalid(b); // literal outside the string pool
    b=bytes;b[0x492]=0xff;invalid(b); // section beyond the file
    b=bytes;b[0x4a2]=b[0x492];b[0x4a3]=b[0x493];b[0x4a4]=b[0x494];b[0x4a5]=b[0x495];invalid(b); // overlapping sections
    b=bytes;b[0x468]=0x7f;invalid(b); // excessive article count
    b=bytes;b.pop_back();invalid(b);CHECK(!d.read(bytes));
}
void view(){
    DefinitionView view;DefinitionEntry a{1,"TAZ",std::string(6000,'A')},b{2,"TAZ","Second meaning."};view.open("TAZ",{a,b});
    Surface s;s.create(800,600,PixelFormat::RGBA32);view.draw(s);const auto first=s.pixels;
    CHECK(!view.event(fixtures::keyEvent(key::PageDown)));view.draw(s);CHECK(first!=s.pixels);
    CHECK(!view.event(fixtures::keyEvent(key::Right)));view.draw(s);const auto second=s.pixels;CHECK(second!=first);
    CHECK(!view.event(fixtures::keyEvent(key::Left)));view.draw(s);CHECK(s.pixels==first);
    a.inflected=true;view.open("TAZZED",{a});view.draw(s);CHECK(s.pixels!=first);a.inflected=false;view.open("TAZ",{a,b});
    Event wheel;wheel.type=EventType::MouseWheel;wheel.wheelY=std::numeric_limits<int>::min();CHECK(!view.event(wheel));CHECK(!view.event(fixtures::keyEvent(key::Home)));view.draw(s);CHECK(s.pixels==first);
    auto repeat=fixtures::keyEvent(key::Return);repeat.repeat=true;CHECK(!view.event(repeat));CHECK(view.event(fixtures::keyEvent(key::Escape)));
    view.open("TAZ",{},"Missing archive");view.draw(s);CHECK(view.event(fixtures::mouseEvent(EventType::MouseDown,620,544)));
    s.pixels.clear();view.draw(s);
}
void inflections(){
    Definitions d;CHECK(!d.read(fixtures::definitionArchive()));const auto exactChecksum=d.indexChecksum();
    const auto bytes=fixtures::definitionArchive(false,
        "<INFL><INF>taz\xb7zed<INF>TAZZED<INF>quiz<INF>taz<INF>two words<INF>hy-phen<INF>bad!<INF>\xff</INFL><DEF>Outside<INF>NOTAKEY",
        "<INFL><INF>TAZZED<INF>tazzing\xff</INFL><INFL><INF>UNFINISHED");
    CHECK(!d.read(bytes) && d.inflections()==4 && d.indexChecksum()!=exactChecksum);
    const auto checksum=d.indexChecksum();std::vector<DefinitionEntry> rows;
    CHECK(!d.lookup("tazzed",rows) && rows.size()==2);if(rows.size()==2)CHECK(rows[0].id==1 && rows[1].id==2 && rows[0].inflected && rows[1].inflected);
    CHECK(!d.lookup("QUIZ",rows) && rows.size()==2);if(rows.size()==2)CHECK(rows[0].id==3 && !rows[0].inflected && rows[1].id==1 && rows[1].inflected);
    CHECK(!d.lookup("TAZ",rows) && rows.size()==2);for(const auto& row:rows)CHECK(!row.inflected); // exact first, no duplicate articles
    CHECK(!d.lookup("TAZZING",rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].id==2 && rows[0].inflected);
    for(const auto* word:{"NOTAKEY","UNFINISHED","TAZZES","TWOWORDS","HYPHEN","BAD"})CHECK(!d.lookup(word,rows) && rows.empty());
    CHECK(!d.read(bytes) && d.indexChecksum()==checksum);CHECK(bool(d.read({})) && d.inflections()==0);
    CHECK(!d.lookup("TAZZED",rows) && rows.empty());
}
}
static const test::Suite suite("boggle.unit.definitions",{
    {"stored inflections, exact priority, duplicate suppression, scope, normalization and reset",[](const auto&){inflections();}},
    {"independent entry archive, paired symbols, paging, variants, truncation and malformed data",[](const auto&){reader();}},
    {"plain definition paging, match switching, close guards and invalid surfaces",[](const auto&){view();}},
});

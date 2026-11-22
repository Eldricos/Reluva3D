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
    std::vector<DefinitionEntry> rows;CHECK(!d.lookup("taz",rows) && rows.size()==2);if(rows.size()==2)CHECK(rows[0].text=="taz\nOwn text." && rows[1].text=="taz\nOther text.");
    CHECK(!d.lookup("QUIZ",rows) && rows.size()==1);if(!rows.empty())CHECK(rows[0].headword=="qu-iz" && rows[0].id==3 && rows[0].text=="qu-iz\nFixture definition.\nOwn history.");
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
    view.open("TAZ",{a});view.draw(s);const auto headword=s.pixels;
    for(auto match:{DefinitionMatch::Variant,DefinitionMatch::Subentry,DefinitionMatch::Inflection,DefinitionMatch::RootDerivative,DefinitionMatch::RelatedForm}){
        a.match=match;view.open("TAZ",{a});view.draw(s);CHECK(s.pixels!=headword);
    }
    a.match=DefinitionMatch::Headword;view.open("TAZ",{a,b});
    Event wheel;wheel.type=EventType::MouseWheel;wheel.wheelY=std::numeric_limits<int>::min();CHECK(!view.event(wheel));CHECK(!view.event(fixtures::keyEvent(key::Home)));view.draw(s);CHECK(s.pixels==first);
    auto repeat=fixtures::keyEvent(key::Return);repeat.repeat=true;CHECK(!view.event(repeat));CHECK(view.event(fixtures::keyEvent(key::Escape)));
    view.open("TAZ",{},"Missing archive");view.draw(s);CHECK(view.event(fixtures::mouseEvent(EventType::MouseDown,620,544)));
    s.pixels.clear();view.draw(s);
}
void inflections(){
    Definitions d;CHECK(!d.read(fixtures::definitionArchive()));const auto exactChecksum=d.indexChecksum();
    const auto bytes=fixtures::definitionArchive(false,
        "<INFL><INF>taz\xb7zed<INF>tazzed<INF>quiz<INF>taz<INF>two words<INF>hy-phen<INF>bad!<INF>\xff</INFL><DEF>Outside<INF>NOTAKEY",
        "<INFL><INF>tazzed<INF>tazzing\xff</INFL><INFL><INF>UNFINISHED");
    CHECK(!d.read(bytes) && d.inflections()==4 && d.indexChecksum()!=exactChecksum);
    const auto checksum=d.indexChecksum();std::vector<DefinitionEntry> rows;
    CHECK(!d.lookup("tazzed",rows) && rows.size()==2);if(rows.size()==2)CHECK(rows[0].id==1 && rows[1].id==2 && rows[0].match==DefinitionMatch::Inflection && rows[1].match==DefinitionMatch::Inflection);
    CHECK(!d.lookup("QUIZ",rows) && rows.size()==2);if(rows.size()==2)CHECK(rows[0].id==3 && rows[0].match==DefinitionMatch::Headword && rows[1].id==1 && rows[1].match==DefinitionMatch::Inflection);
    CHECK(!d.lookup("TAZ",rows) && rows.size()==2);for(const auto& row:rows)CHECK(row.match==DefinitionMatch::Headword); // exact first, no duplicate articles
    CHECK(!d.lookup("TAZZING",rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].id==2 && rows[0].match==DefinitionMatch::Inflection);
    for(const auto* word:{"NOTAKEY","UNFINISHED","TAZZES","TWOWORDS","HYPHEN","BAD"})CHECK(!d.lookup(word,rows) && rows.empty());
    CHECK(!d.read(bytes) && d.indexChecksum()==checksum);CHECK(bool(d.read({})) && d.inflections()==0);
    CHECK(!d.lookup("TAZZED",rows) && rows.empty());
}
void lexicalFields(){
    Definitions d;const auto bytes=fixtures::definitionArchive(false,
        "<UNEQ type=SP><WD>quiz<WD>varonly<RUNSEG><RUN> wu\\bprime/g\xb7let\\lprime/ <RUN>quiz<RUN>bad\\unknown/let<INFL><INF>quiz</INFL><KW>NOTAKEY<DRV>NOTAROOT",
        "<INFL><INF>quiz<INF>wuglet</INFL>",
        "<IWD>wug-<DLIST><DRV>quiz<DRV>quiz<DRV>rootonly<DRV>taz<DEFN><ITX>Own root description.");
    CHECK(!d.read(bytes) && d.variants()==2 && d.subentries()==2 && d.inflections()==2 && d.derivatives()==3);
    std::vector<DefinitionEntry> rows;
    CHECK(!d.lookup("QUIZ",rows) && rows.size()==3);
    if(rows.size()==3)CHECK(rows[0].id==1 && rows[0].match==DefinitionMatch::Variant && rows[1].id==2 && rows[1].match==DefinitionMatch::Inflection && rows[2].id==3 && rows[2].match==DefinitionMatch::RootDerivative && rows[2].headword=="wug-");
    CHECK(!d.lookup("WUGLET",rows) && rows.size()==2);
    if(rows.size()==2)CHECK(rows[0].id==1 && rows[0].match==DefinitionMatch::Subentry && rows[1].id==2 && rows[1].match==DefinitionMatch::Inflection);
    CHECK(!d.lookup("TAZ",rows) && rows.size()==3);
    if(rows.size()==3)CHECK(rows[0].match==DefinitionMatch::Headword && rows[1].match==DefinitionMatch::Headword && rows[2].match==DefinitionMatch::RootDerivative);
    CHECK(!d.lookup("ROOTONLY",rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].id==3 && !rows[0].text.empty());
    CHECK(!d.lookup("VARONLY",rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].match==DefinitionMatch::Variant);
    for(const auto* word:{"NOTAKEY","NOTAROOT","BADLET","WUGLETS"})CHECK(!d.lookup(word,rows) && rows.empty());
    const auto checksum=d.indexChecksum();CHECK(!d.read(bytes) && d.indexChecksum()==checksum);
    CHECK(bool(d.read({})) && d.variants()==0 && d.subentries()==0 && d.derivatives()==0);CHECK(!d.lookup("QUIZ",rows) && rows.empty());
}
void queryCase(){
    Definitions d;const auto bytes=fixtures::definitionArchive(false,
        "<UNEQ type=SP><WD>ProperOnly<WD>loweralias<RUNSEG><RUN>lower\\bprime/run<RUN>Upper\\bprime/run<INFL><INF>lowered<INF>Lowered</INFL>",
        "<UNEQ type=SP><WD>taz<WD>ProperAlias<RUNSEG><RUN>hidden\\bprime/run<INFL><INF>shifted<INF>Shifted</INFL>",
        "<WD>Quiz<DEF>Own capitalized article.","TAZ");
    CHECK(!d.read(bytes) && d.entries()==3 && d.headwords()==1 && d.variants()==2 && d.subentries()==2 && d.inflections()==2);
    std::vector<DefinitionEntry> rows;
    for(const auto* input:{"taz","TAZ","TaZ"}){
        CHECK(!d.lookup(input,rows) && rows.size()==2);
        if(rows.size()==2)CHECK(rows[0].id==1 && rows[0].match==DefinitionMatch::Headword && rows[1].id==2 && rows[1].match==DefinitionMatch::Variant);
    }
    for(const auto* input:{"lowered","LOWERED","LoWeReD"}){CHECK(!d.lookup(input,rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].id==1 && rows[0].match==DefinitionMatch::Inflection);}
    CHECK(!d.lookup("HIDDENRUN",rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].id==2 && rows[0].headword=="TAZ" && rows[0].match==DefinitionMatch::Subentry);
    CHECK(!d.lookup("SHIFTED",rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].id==2);
    for(const auto* input:{"ProperOnly","PROPERALIAS","UPPERRUN","quiz","QUIZ"})CHECK(!d.lookup(input,rows) && rows.empty());
    DefinitionEntry article;CHECK(!d.entry(2,article) && article.headword=="TAZ");CHECK(!d.entry(3,article) && article.headword=="Quiz");
    const auto checksum=d.indexChecksum();CHECK(!d.read(bytes) && d.indexChecksum()==checksum);
    CHECK(!d.read(fixtures::definitionArchive(false,{},{},"<IWD>CaseRoot-<DLIST><DRV>lowerroot<DRV>UpperRoot<DEFN><ITX>Own root text.")));
    CHECK(!d.lookup("LOWERROOT",rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].id==3 && rows[0].headword=="CaseRoot-" && rows[0].match==DefinitionMatch::RootDerivative);
    CHECK(!d.lookup("UPPERROOT",rows) && rows.empty());CHECK(bool(d.read({})) && d.headwords()==0);CHECK(!d.lookup("LOWERROOT",rows) && rows.empty());
}
void authoredIndex(){
    const auto bytes=fixtures::definitionIndexedArchive();Definitions d;
    CHECK(!d.read(bytes) && d.entries()==3 && d.indexedWords()==5 && d.indexedLinks()==7 && d.wordRecords()==11);
    std::vector<DefinitionEntry> rows;
    CHECK(!d.lookup("TAZZED",rows) && rows.size()==2);
    if(rows.size()==2)CHECK(rows[0].id==2 && rows[0].match==DefinitionMatch::Headword && rows[1].id==1 && rows[1].match==DefinitionMatch::RelatedForm);
    CHECK(!d.lookup("TaZ",rows) && rows.size()==2);if(rows.size()==2)CHECK(rows[0].id==1 && rows[1].id==2 && rows[1].match==DefinitionMatch::Inflection);
    CHECK(!d.lookup("TAZZES",rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].id==1 && rows[0].match==DefinitionMatch::RelatedForm);
    CHECK(!d.lookup("CAPITAL",rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].id==3 && rows[0].match==DefinitionMatch::RootDerivative);
    CHECK(!d.lookup("ZOT",rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].match==DefinitionMatch::Variant);
    for(const auto* word:{"UPPER","NOTINDEXED","TWOWORDS","ELAN","TAZZESS"})CHECK(!d.lookup(word,rows) && rows.empty());
    const auto sum=d.indexChecksum();CHECK(!d.read(bytes) && d.indexChecksum()==sum);
    auto invalid=[&](std::vector<uint8_t> b){CHECK(bool(d.read(b)) && !d.indexedWords() && !d.entries());CHECK(!d.lookup("TAZZES",rows) && rows.empty());};
    auto rd32=[&](size_t p){return uint32_t(bytes[p])<<24|uint32_t(bytes[p+1])<<16|uint32_t(bytes[p+2])<<8|bytes[p+3];};
    const size_t component=rd32(0x33a),stream=rd32(0x33e),offsets=component+rd32(component+0xbc),lengths=component+rd32(component+0xcc);
    auto b=bytes;b[0x317]=4;invalid(b);b=bytes;b[0x31b]=0xff;invalid(b);b=bytes;b[component+3]=3;invalid(b);
    b=bytes;b[component+0x95]=13;invalid(b);b=bytes;b[component+0x8d]=3;invalid(b);b=bytes;b[component+0x93]=0;invalid(b);
    b=bytes;test::fixtures::put16(b,offsets+2,65535);invalid(b);b=bytes;b[lengths+1]=0;invalid(b);
    b=bytes;b[component+0xbb]=200;invalid(b); // repeated section tag
    b=bytes;b[0x33e]=0xff;invalid(b);b=bytes;b.pop_back();invalid(b);
    auto code=[&](unsigned symbol,unsigned value){auto copy=bytes;for(unsigned k=0;k<10;++k){const auto bit=symbol*10+k;const auto mask=uint8_t(1u<<(7-bit%8));copy[stream+bit/8]=uint8_t((copy[stream+bit/8]&~mask)|(((value>>(9-k))&1)?mask:0));}invalid(copy);};
    code(0,64); // impossible backtrack in a block's first word
    code(6,33); // unsupported record metadata
    code(9,1); // zero article ID
    invalid(fixtures::definitionIndexedArchive(false,true));invalid(fixtures::definitionIndexedArchive(false,false,true));
    CHECK(!d.read(bytes) && d.indexedWords()==5);
}
void inflectionAlternatives(){
    Definitions d;CHECK(!d.read(fixtures::definitionArchive(false,
        "<INFL><INF>tazzes<EQ type=SP><WD>tazzen</INFL>","<INFL><WD>ignored")));
    CHECK(d.variants()==0 && d.inflections()==2);std::vector<DefinitionEntry> rows;
    for(const auto* word:{"TAZZES","TAZZEN"}){CHECK(!d.lookup(word,rows) && rows.size()==1);if(rows.size()==1)CHECK(rows[0].match==DefinitionMatch::Inflection);}
    CHECK(!d.lookup("IGNORED",rows) && rows.empty());
}
}
static const test::Suite suite("boggle.unit.definitions",{
    {"alternate spellings inside inflection groups retain inflection rank and require complete groups",[](const auto&){inflectionAlternatives();}},
    {"authored word index, implicit forms, root spelling, priority, boundaries, sentinel and malformed data",[](const auto&){authoredIndex();}},
    {"Boggle query folding, per-field case eligibility, capitalized parents, direct IDs and reset",[](const auto&){queryCase();}},
    {"alternate headwords, stressed subentries, root derivatives, match priority and excluded fields",[](const auto&){lexicalFields();}},
    {"stored inflections, exact priority, duplicate suppression, scope, normalization and reset",[](const auto&){inflections();}},
    {"independent entry archive, paired symbols, paging, variants, truncation and malformed data",[](const auto&){reader();}},
    {"plain definition paging, match switching, close guards and invalid surfaces",[](const auto&){view();}},
});

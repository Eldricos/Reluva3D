// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "tests/engines/boggle/fixtures.h"
#include <stdexcept>
namespace reluva::boggle::fixtures {
inline std::vector<uint8_t> definitionArchive(bool game=false,std::string extra={},std::string secondExtra={},std::string third={},std::string secondHeading={}){
    std::vector<uint8_t> b(0x600);
    auto be16=[&](size_t p,uint16_t n){b[p]=uint8_t(n>>8);b[p+1]=uint8_t(n);};
    auto be32=[&](size_t p,uint32_t n){be16(p,uint16_t(n>>16));be16(p+2,uint16_t(n));};
    be32(0x400,0x00010001);const std::string marker="Entry file";std::copy(marker.begin(),marker.end(),b.begin()+0x404);
    be32(0x468,3);be16(0x472,2);be16(0x476,3);
    if(secondHeading.empty())secondHeading=game?"aaa":"taz";
    std::vector<uint8_t> pool;const std::vector<std::string> literals={"",std::string(game?"<WD>aaa<DEF>Own":"<WD>taz<DEF>Own")+extra,"<WD>"+secondHeading+"<DEF>Other"+secondExtra,third.empty()?"<WD>qu\xb7iz<DEF>Fixture<LANG>definition<EM1>.</EM1><ETYS type=ETY>Own<LANG>history.":third,std::string(4,char(254))," text.\xff"};
    std::vector<uint8_t> left(16),right(16);test::fixtures::put16(left,0,2);
    for(size_t i=1;i<literals.size();++i){test::fixtures::put16(left,i*2,uint16_t(pool.size()));test::fixtures::put16(right,i*2,uint16_t(literals[i].size()));pool.insert(pool.end(),literals[i].begin(),literals[i].end());}
    test::fixtures::put16(left,12,1);test::fixtures::put16(right,12,5);test::fixtures::put16(left,14,2);test::fixtures::put16(right,14,5);
    std::vector<uint8_t> directory(8);test::fixtures::put32(directory,4,2);
    const std::vector<std::vector<uint8_t>> data={directory,left,right,{0},pool,{0},{0}};
    const unsigned tags[]={10,20,30,40,60,80,90},strides[]={4,2,2,1,1,1,1},counts[]={2,8,8,1,unsigned(pool.size()),1,1};
    for(size_t i=0;i<data.size();++i){const auto p=0x490+i*16;be16(p,uint16_t(tags[i]));be32(p+2,uint32_t(b.size()-0x400));be16(p+8,uint16_t(strides[i]));be16(p+10,uint16_t(counts[i]));be32(p+12,uint32_t(data[i].size()));b.insert(b.end(),data[i].begin(),data[i].end());}
    unsigned used=0;uint8_t byte=0;for(unsigned code:{4,6,4,7,4,3,4})for(int shift=2;shift>=0;--shift){byte=uint8_t((byte<<1)|((code>>shift)&1));if(++used==8){b.push_back(byte);byte=0;used=0;}}
    if(used)b.push_back(uint8_t(byte<<(8-used)));
    return b;
}
// A second independent fixture: an authored index with two data blocks,
// front-compressed words, signed ID deltas, a partial boundary and a sentinel.
inline std::vector<uint8_t> definitionIndexedArchive(bool game=false,bool badBoundary=false,bool badSentinel=false){
    const std::string baseWord=game?"aaa":"taz",past=game?"aaaa":"tazzed",plural=game?"aaaaa":"tazzes",variant=game?"aab":"zot",rootWord=game?"aaaaaa":"capital";
    auto b=definitionArchive(game,"<UNEQ type=SP><WD>"+variant+"<WD>notindexed<DEF>","<INFL><INF>"+baseWord+"</INFL>","<IWD>own-<DLIST><DRV>Capital<DEFN><ITX>Own root text.",past);
    auto rd32=[&](size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];};
    const auto entryStream=0x400+rd32(0x4f2)+rd32(0x4fc);
    auto be16=[&](size_t p,uint16_t n){b[p]=uint8_t(n>>8);b[p+1]=uint8_t(n);};
    auto be32=[&](size_t p,uint32_t n){be16(p,uint16_t(n>>16));be16(p+2,uint16_t(n));};
    b.resize((b.size()+511)&~size_t(511));const auto component=b.size();b.resize(component+512);
    be32(component,0x00020002);const std::string marker="Index file";std::copy(marker.begin(),marker.end(),b.begin()+component+4);
    constexpr unsigned pageBytes=256,n=1024,pages=3;
    be16(component+0x1e,pages);be16(component+0x8c,pageBytes);be16(component+0x8e,4);be16(component+0x92,64);be16(component+0x94,10);be16(component+0xa0,96);
    struct Row {std::string word;uint32_t id;uint8_t kind=0;};
    const std::vector<std::vector<Row>> rows={{{baseWord,1},{baseWord,2},{baseWord+"'s",1},{"upper",2,192},{"two words",1},{"\xe9lan",1}},{{past,1},{past,2},{plural,1},{variant,1},{rootWord,3}}};
    auto append=[](std::string& bytes,uint32_t v,unsigned size){for(unsigned i=size;i;--i)bytes+=char(v>>((i-1)*8));};
    auto record=[&](const Row& previous,const Row& next){
        size_t keep=0;while(keep<std::min(previous.word.size(),next.word.size()) && previous.word[keep]==next.word[keep])++keep;
        const auto cut=previous.word.size()-keep;if(cut>63)throw std::runtime_error("fixture prefix limit");
        std::string raw(1,char(next.kind|cut));raw+=next.word.substr(keep);raw.append("\7\0\20",3);append(raw,next.id-previous.id,3);return raw;
    };
    auto prefix=[](std::string s){s.resize(4,' ');for(auto& c:s)if(c>='A'&&c<='Z')c+=32;return s;};
    std::vector<uint8_t> pool(256),offsets(n*2),lengths(n),directory;for(unsigned i=0;i<256;++i)pool[i]=uint8_t(i);
    for(unsigned i=1;i<n;++i){test::fixtures::put16(offsets,i*2,uint16_t(i<=256?i-1:'X'));lengths[i]=1;}
    for(const auto& s:{std::string(4,' '),prefix(rows[1][0].word),prefix(rows[1].back().word)})directory.insert(directory.end(),s.begin(),s.end());
    const std::vector<std::vector<uint8_t>> data={pool,offsets,lengths,directory,{0},{0},{0},{0},{0},{0}};
    const unsigned tags[]={200,210,220,230,250,240,260,270,290,300};
    for(size_t i=0;i<data.size();++i){
        const auto p=component+0xaa+i*16;be16(p,uint16_t(tags[i]));be32(p+2,uint32_t(b.size()-component));
        const unsigned stride=i==1?2:i==2?1:i==3?4:65535,count=i==0?n-1:i==1||i==2?n:i==3?pages:65535;
        be16(p+8,uint16_t(stride));be16(p+10,uint16_t(count));be32(p+12,uint32_t(data[i].size()));b.insert(b.end(),data[i].begin(),data[i].end());
    }
    const auto stream=b.size();
    for(unsigned page=0;page<pages;++page){
        std::string raw;
        if(page<2){Row previous{};for(const auto& next:rows[page]){raw+=record(previous,next);previous=next;}
            if(page==0){auto tail=record(rows[0].back(),rows[1][0]).substr(0,2);if(badBoundary)tail[1]='Q';raw+=tail;}
        }else {raw+='\0';raw+=rows[1].back().word;raw+='\0';append(raw,rows[1].back().id+(badSentinel?1:0),4);}
        if(raw.size()>pageBytes*8/10)throw std::runtime_error("fixture block limit");
        const auto start=b.size();b.resize(start+pageBytes);
        for(unsigned i=0;i<pageBytes*8/10;++i){const unsigned code=i<raw.size()?unsigned(uint8_t(raw[i]))+1:page==0?0:1;
            for(unsigned k=0;k<10;++k){const auto bit=i*10+k;b[start+bit/8]|=uint8_t(((code>>(9-k))&1)<<(7-bit%8));}}
    }
    b.resize((b.size()+511)&~size_t(511));const auto boolean=b.size();b.resize(boolean+512);be32(boolean,0x00020003);
    be16(0x314,1);be16(0x316,3);be32(0x318,0x32a);
    const uint32_t values[]={0x00010001,0x400,uint32_t(entryStream),0x00020002,uint32_t(component),uint32_t(stream),0x00020003,uint32_t(boolean),uint32_t(boolean+512)};
    for(unsigned i=0;i<9;++i)be32(0x32a+i*4,values[i]);
    return b;
}
}

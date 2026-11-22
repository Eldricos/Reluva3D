// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "tests/engines/boggle/fixtures.h"
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
}

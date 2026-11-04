// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace reluva::test::fixtures {
inline void tt16(std::vector<uint8_t>& b,size_t at,int n){b[at]=uint8_t(unsigned(n)>>8);b[at+1]=uint8_t(n);}
inline void tt32(std::vector<uint8_t>& b,size_t at,uint32_t n){tt16(b,at,int(n>>16));tt16(b,at+2,int(n));}
inline void ttAppend(std::vector<uint8_t>& b,int n){const auto at=b.size();b.resize(at+2);tt16(b,at,n);}
inline std::vector<uint8_t> ttGlyph(const std::vector<std::array<int,3>>& points,const std::vector<int>& ends,bool packed=false) {
    auto scaled=points;if(packed)for(auto& p:scaled){p[0]/=4;p[1]/=4;}
    std::vector<uint8_t> b(10);tt16(b,0,int(ends.size()));int xmax=0,ymax=0;
    for(auto p:scaled){if(p[0]>xmax)xmax=p[0];if(p[1]>ymax)ymax=p[1];}
    tt16(b,6,xmax);tt16(b,8,ymax);for(int n:ends)ttAppend(b,n);ttAppend(b,0);
    std::vector<uint8_t> flags;int lastX=0,lastY=0;
    for(auto p:scaled) {
        uint8_t flag=uint8_t(p[2]);
        if(packed) {
            const int dx=p[0]-lastX,dy=p[1]-lastY;
            if(!dx)flag|=16;else if(dx>=-255 && dx<=255)flag|=2|(dx>0?16:0);
            if(!dy)flag|=32;else if(dy>=-255 && dy<=255)flag|=4|(dy>0?32:0);
        }
        flags.push_back(flag);lastX=p[0];lastY=p[1];
    }
    for(size_t i=0;i<flags.size();) {
        size_t count=1;while(i+count<flags.size() && flags[i+count]==flags[i])++count;
        b.push_back(uint8_t(flags[i]|(count>1?8:0)));if(count>1)b.push_back(uint8_t(count-1));i+=count;
    }
    for(int axis=0;axis<2;++axis) {
        int last=0;
        for(size_t i=0;i<scaled.size();++i) {
            const int delta=scaled[i][axis]-last;last=scaled[i][axis];
            if(flags[i]&(axis?4:2))b.push_back(uint8_t(delta<0?-delta:delta));
            else if(!(flags[i]&(axis?32:16)))ttAppend(b,delta);
        }
    }
    if(b.size()&1)b.push_back(0);
    return b;
}
// Invented square with a hole, quadratic cap, and four off-curve points.
// Every table and outline is assembled here; no font file is embedded.
inline std::vector<uint8_t> asciiTrueType(bool longLoca=false,bool shortMetrics=false,bool packed=false) {
    std::map<std::string,std::vector<uint8_t>> t;
    auto& head=t["head"];head.resize(54);tt32(head,0,0x10000);tt32(head,12,0x5f0f3cf5);tt16(head,18,packed?250:1000);tt16(head,50,longLoca?1:0);
    auto& maxp=t["maxp"];maxp.resize(32);tt32(maxp,0,0x10000);tt16(maxp,4,5);
    auto& hhea=t["hhea"];hhea.resize(36);tt32(hhea,0,0x10000);tt16(hhea,34,shortMetrics?2:5);
    auto& hmtx=t["hmtx"];for(int i=0;i<5;++i){if(!shortMetrics || i<2)ttAppend(hmtx,packed?225:900);ttAppend(hmtx,0);}
    auto& glyf=t["glyf"];auto& loca=t["loca"];
    const std::vector<std::vector<uint8_t>> shapes={{},
        ttGlyph({{0,0,1},{800,0,1},{800,800,1},{0,800,1},{200,200,1},{200,600,1},{600,600,1},{600,200,1}},{3,7},packed),
        ttGlyph({{0,0,1},{0,800,1},{400,1200,0},{800,1200,0},{800,800,1},{800,0,1}},{5},packed),{},
        ttGlyph({{0,400,0},{400,800,0},{800,400,0},{400,0,0}},{3},packed)};
    auto offset=[&]{if(longLoca){const size_t at=loca.size();loca.resize(at+4);tt32(loca,at,uint32_t(glyf.size()));}else ttAppend(loca,int(glyf.size()/2));};
    for(const auto& g:shapes){offset();glyf.insert(glyf.end(),g.begin(),g.end());}offset();
    auto& cmap=t["cmap"];cmap.resize(12+32+95*2);tt16(cmap,2,1);tt16(cmap,4,3);tt16(cmap,6,1);tt32(cmap,8,12);
    tt16(cmap,12,4);tt16(cmap,14,int(cmap.size()-12));tt16(cmap,18,4);
    tt16(cmap,26,126);tt16(cmap,28,65535);tt16(cmap,32,32);tt16(cmap,34,65535);tt16(cmap,38,1);tt16(cmap,40,4);
    for(int c=32;c<=126;++c)tt16(cmap,44+size_t(c-32)*2,c==32?3:c=='B'?2:c=='C'?4:1);
    std::vector<uint8_t> b(12+t.size()*16);tt32(b,0,0x10000);tt16(b,4,int(t.size()));size_t i=0;
    for(const auto& [tag,data]:t) {
        while(b.size()%4)b.push_back(0);
        const size_t at=12+i++*16;
        for(size_t n=0;n<4;++n)b[at+n]=uint8_t(tag[n]);
        tt32(b,at+8,uint32_t(b.size()));tt32(b,at+12,uint32_t(data.size()));b.insert(b.end(),data.begin(),data.end());
    }
    return b;
}
inline size_t ttTable(const std::vector<uint8_t>& b,const std::string& tag) {
    const size_t count=size_t(b[4])*256+b[5];
    for(size_t i=0;i<count;++i){const size_t at=12+i*16;if(std::string(b.begin()+at,b.begin()+at+4)==tag)return (uint32_t(b[at+8])<<24)|(uint32_t(b[at+9])<<16)|(uint32_t(b[at+10])<<8)|b[at+11];}return 0;
}
} // namespace reluva::test::fixtures

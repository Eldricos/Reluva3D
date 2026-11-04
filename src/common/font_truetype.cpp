// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "common/font_truetype.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <map>
#include <span>
namespace reluva::font {
namespace {
using Bytes=std::span<const uint8_t>;
uint16_t u16(Bytes b,size_t p){return uint16_t(b[p])<<8|b[p+1];}
int s16(Bytes b,size_t p){const unsigned n=u16(b,p);return n<32768?int(n):int(n)-65536;}
uint32_t u32(Bytes b,size_t p){return uint32_t(u16(b,p))<<16|u16(b,p+2);}
struct Cursor {
    Bytes b;size_t at=0;
    bool byte(uint8_t& n){if(at==b.size())return false;n=b[at++];return true;}
    bool word(uint16_t& n){if(b.size()-at<2)return false;n=u16(b,at);at+=2;return true;}
    bool skip(size_t n){if(n>b.size()-at)return false;at+=n;return true;}
};
struct Point {int x,y;bool on=true;};
struct Edge {Point a,b;};
Point middle(Point a,Point b){return {(a.x+b.x)/2,(a.y+b.y)/2,true};}
bool curve(Point a,Point control,Point b,std::vector<Edge>& edges,int depth=0) {
    if(edges.size()>=4096)return false;
    if(depth==8 || std::abs(a.x-2*control.x+b.x)+std::abs(a.y-2*control.y+b.y)<=16) {
        edges.push_back({a,b});return true;
    }
    const auto left=middle(a,control),right=middle(control,b),center=middle(left,right);
    return curve(a,left,center,edges,depth+1) && curve(center,right,b,edges,depth+1);
}
bool outline(Bytes b,int units,int height,int bearing,std::vector<Edge>& edges) {
    if(b.empty())return true;
    if(b.size()<10)return false;
    const int contours=s16(b,0),xmin=s16(b,2),ymin=s16(b,4),xmax=s16(b,6),ymax=s16(b,8);
    if(contours<0 || contours>128 || xmin>xmax || ymin>ymax)return false;
    if(!contours)return true;
    Cursor cursor{b,10};std::vector<uint16_t> ends;uint16_t n=0;
    for(int i=0;i<contours;++i){if(!cursor.word(n) || (!ends.empty() && n<=ends.back()))return false;ends.push_back(n);}
    const size_t count=size_t(ends.back())+1;
    if(count>2048 || !cursor.word(n) || !cursor.skip(n))return false;
    std::vector<uint8_t> flags;
    while(flags.size()<count) {
        uint8_t flag,repeat=0;if(!cursor.byte(flag) || (flag&0x80))return false;
        if((flag&8) && !cursor.byte(repeat))return false;
        if(size_t(repeat)+1>count-flags.size())return false;
        flags.insert(flags.end(),size_t(repeat)+1,flag);
    }
    std::vector<Point> points(count);
    for(int axis=0;axis<2;++axis) {
        int value=0;
        for(size_t i=0;i<count;++i) {
            const unsigned shortBit=axis?4:2,sameBit=axis?32:16;int delta=0;
            if(flags[i]&shortBit) {uint8_t d;if(!cursor.byte(d))return false;delta=(flags[i]&sameBit)?int(d):-int(d);}
            else if(!(flags[i]&sameBit)) {uint16_t d;if(!cursor.word(d))return false;delta=d<32768?int(d):int(d)-65536;}
            value+=delta;
            if(value<(axis?ymin:xmin) || value>(axis?ymax:xmax))return false;
            if(axis)points[i].y=-value*height*64/units;
            else points[i].x=(value+bearing-xmin)*height*64/units;
            points[i].on=bool(flags[i]&1);
        }
    }
    size_t begin=0;
    for(const auto end:ends) {
        std::vector<Point> expanded;
        for(size_t i=begin;i<=end;++i) {
            const auto p=points[i],next=points[i==end?begin:i+1];expanded.push_back(p);
            if(!p.on && !next.on)expanded.push_back(middle(p,next));
        }
        size_t start=0;while(start<expanded.size() && !expanded[start].on)++start;
        if(start==expanded.size())return false;
        size_t i=start,consumed=0;Point current=expanded[i];
        while(consumed<expanded.size()) {
            const size_t next=(i+1)%expanded.size();
            if(expanded[next].on) {
                if(edges.size()>=4096)return false;
                edges.push_back({current,expanded[next]});current=expanded[next];i=next;++consumed;
            } else {
                const size_t endPoint=(i+2)%expanded.size();
                if(!curve(current,expanded[next],expanded[endPoint],edges))return false;
                current=expanded[endPoint];i=endPoint;consumed+=2;
            }
        }
        begin=size_t(end)+1;
    }
    return true;
}
int floorPixel(int n){return n>=0?n/64:-int((unsigned(-n)+63)/64);}
// Four-by-four sample coverage with a nonzero winding fill. All intersections
// use fixed-point coordinates; neither platform font APIs nor hint programs run.
std::vector<uint8_t> raster(const std::vector<Edge>& edges,int left,int top,int w,int h) {
    std::vector<uint8_t> result(size_t(w)*h);
    for(int row=0;row<h;++row)for(int sub=0;sub<4;++sub) {
        const int y=(top+row)*64+sub*16+8;
        std::vector<std::pair<int,int>> hits;
        for(auto edge:edges) {
            int winding=1;if(edge.a.y>edge.b.y){std::swap(edge.a,edge.b);winding=-1;}
            if(y<edge.a.y || y>=edge.b.y)continue;
            const int x=edge.a.x+int(int64_t(y-edge.a.y)*(edge.b.x-edge.a.x)/(edge.b.y-edge.a.y));
            hits.emplace_back(x,winding);
        }
        std::sort(hits.begin(),hits.end());
        size_t at=0;int winding=0;
        for(int col=0;col<w;++col)for(int sx=0;sx<4;++sx) {
            const int x=(left+col)*64+sx*16+8;
            while(at<hits.size() && hits[at].first<=x){winding+=hits[at].second;++at;}
            if(winding)++result[size_t(row)*w+col];
        }
    }
    for(auto& a:result)a=uint8_t((unsigned(a)*255+8)/16);
    return result;
}
size_t index(unsigned char c){return c>=32 && c<=126 ? size_t(c-32) : size_t('?'-32);}
}
Error TrueTypeAscii::read(const std::vector<uint8_t>& bytes,int pixelHeight) {
    *this={};auto invalid=[] {return Error::fail("Unsupported or malformed ASCII TrueType font");};
    const Bytes b(bytes);
    if(b.size()<12 || b.size()>4*1024*1024 || pixelHeight<8 || pixelHeight>48 || u32(b,0)!=0x10000)return invalid();
    const size_t tables=u16(b,4),directoryEnd=12+tables*16;
    if(!tables || tables>64 || directoryEnd>b.size())return invalid();
    std::map<std::string,Bytes> table;
    std::vector<std::pair<size_t,size_t>> ranges;
    for(size_t i=0;i<tables;++i) {
        const size_t p=12+i*16,offset=u32(b,p+8),length=u32(b,p+12);
        const std::string tag(reinterpret_cast<const char*>(b.data()+p),4);
        if(offset<directoryEnd || offset>b.size() || length>b.size()-offset || table.contains(tag))return invalid();
        if(length)ranges.emplace_back(offset,offset+length);
        table[tag]=b.subspan(offset,length);
    }
    std::sort(ranges.begin(),ranges.end());for(size_t i=1;i<ranges.size();++i)if(ranges[i].first<ranges[i-1].second)return invalid();
    if(table.contains("fvar") || table.contains("gvar") || table.contains("CFF ") || table.contains("CFF2"))return invalid();
    const auto head=table["head"],hhea=table["hhea"],maxp=table["maxp"],hmtx=table["hmtx"],loca=table["loca"],glyf=table["glyf"],cmap=table["cmap"];
    if(head.size()<54 || hhea.size()<36 || maxp.size()<32 || cmap.size()<4 ||
       u32(head,0)!=0x10000 || u32(head,12)!=0x5f0f3cf5 || u32(hhea,0)!=0x10000 || u32(maxp,0)!=0x10000 ||
       s16(head,52)!=0 || s16(hhea,32)!=0)return invalid();
    const unsigned units=u16(head,18),glyphs=u16(maxp,4),metrics=u16(hhea,34),format=u16(head,50);
    if(units<16 || units>16384 || !glyphs || glyphs>8192 || !metrics || metrics>glyphs || format>1 ||
       hmtx.size()<size_t(metrics)*4+size_t(glyphs-metrics)*2 || loca.size()<size_t(glyphs+1)*(format?4:2))return invalid();
    std::vector<size_t> offsets;
    for(size_t i=0;i<=glyphs;++i) {
        const size_t offset=format?u32(loca,i*4):size_t(u16(loca,i*2))*2;
        if(offset>glyf.size() || (!offsets.empty() && offset<offsets.back()))return invalid();
        offsets.push_back(offset);
    }
    const size_t maps=u16(cmap,2);if(maps>64 || cmap.size()<4+maps*8 || u16(cmap,0))return invalid();
    Bytes mapping;int priority=-1;
    for(size_t i=0;i<maps;++i) {
        const size_t at=4+i*8,offset=u32(cmap,at+4);const unsigned platform=u16(cmap,at),encoding=u16(cmap,at+2);
        if(offset>cmap.size() || cmap.size()-offset<2)return invalid();
        const int rank=platform==3 && encoding==1?2:platform==0?1:-1;
        if(u16(cmap,offset)!=4 || rank<=priority)continue;
        if(cmap.size()-offset<4)return invalid();
        const size_t length=u16(cmap,offset+2);
        if(length<16 || length>cmap.size()-offset)return invalid();
        mapping=cmap.subspan(offset,length);priority=rank;
    }
    if(mapping.empty())return invalid();
    const size_t segments=u16(mapping,6)/2;
    if(!segments || segments>4096 || (u16(mapping,6)&1) || mapping.size()<16+segments*8 || u16(mapping,14+segments*2))return invalid();
    const size_t starts=16+segments*2,deltas=starts+segments*2,rangesAt=deltas+segments*2;
    for(size_t i=0;i<segments;++i) {
        if(u16(mapping,starts+i*2)>u16(mapping,14+i*2) || (i && u16(mapping,starts+i*2)<=u16(mapping,14+(i-1)*2)))return invalid();
    }
    if(u16(mapping,14+(segments-1)*2)!=65535)return invalid();
    TrueTypeAscii next;size_t edgeBudget=0;
    for(unsigned c=32;c<=126;++c) {
        unsigned id=0;
        for(size_t i=0;i<segments;++i)if(c<=u16(mapping,14+i*2)) {
            const unsigned first=u16(mapping,starts+i*2);if(c<first)break;
            const unsigned delta=u16(mapping,deltas+i*2),range=u16(mapping,rangesAt+i*2);
            if(range) {
                const size_t at=rangesAt+i*2+range+size_t(c-first)*2;
                if(range&1 || at+2>mapping.size() || at<rangesAt+segments*2)return invalid();
                id=u16(mapping,at);if(id)id=(id+delta)&65535;
            } else id=(c+delta)&65535;
            break;
        }
        if(!id || id>=glyphs)return invalid();
        const size_t metric=std::min<unsigned>(id,metrics-1)*4;
        const int bearing=s16(hmtx,id<metrics?metric+2:size_t(metrics)*4+size_t(id-metrics)*2);
        auto& out=next.glyphs_[c-32];
        out.advance=int((uint64_t(u16(hmtx,metric))*pixelHeight+units/2)/units);
        if(out.advance>256)return invalid();
        std::vector<Edge> edges;
        if(!outline(glyf.subspan(offsets[id],offsets[id+1]-offsets[id]),int(units),pixelHeight,bearing,edges))return invalid();
        edgeBudget+=edges.size();if(edgeBudget>65536)return invalid();
        if(!edges.empty()) {
            int xmin=INT_MAX,ymin=INT_MAX,xmax=INT_MIN,ymax=INT_MIN;
            for(const auto& edge:edges)for(auto p:{edge.a,edge.b}){xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);}
            out.left=floorPixel(xmin);out.top=floorPixel(ymin);
            out.w=floorPixel(xmax+63)-out.left;out.h=floorPixel(ymax+63)-out.top;
            if(out.w>256 || out.h>256)return invalid();
            out.alpha=raster(edges,out.left,out.top,out.w,out.h);
        }
    }
    next.height_=pixelHeight;*this=std::move(next);return Error::success();
}
int TrueTypeAscii::measure(std::string_view text) const {
    int width=0;for(unsigned char c:text.substr(0,4096))width+=glyphs_[index(c)].advance;return width;
}
int TrueTypeAscii::draw(Surface& surface,int x,int baseline,std::string_view text,Color color,int maxWidth) const {
    if(!loaded() || surface.format!=PixelFormat::RGBA32 || maxWidth<0)return x;
    const int64_t right=int64_t(x)+maxWidth;int64_t pen=x;
    for(unsigned char c:text.substr(0,4096)) {
        const auto& glyph=glyphs_[index(c)];if(pen+glyph.advance>right)break;
        for(int y=0;y<glyph.h;++y)for(int col=0;col<glyph.w;++col) {
            const int64_t px=pen+glyph.left+col,py=int64_t(baseline)+glyph.top+y;
            if(px<x || px>=right || px<0 || px>=surface.w || py<0 || py>=surface.h)continue;
            const unsigned alpha=glyph.alpha[size_t(y)*glyph.w+col];if(!alpha)continue;
            auto* out=surface.row(int(py))+int(px)*4;
            const uint8_t ink[3]={color.r,color.g,color.b};
            for(int n=0;n<3;++n)out[n]=uint8_t((ink[n]*alpha+out[n]*(255-alpha)+127)/255);
            out[3]=255;
        }
        pen+=glyph.advance;
    }
    return int(std::min<int64_t>(pen,INT_MAX));
}
} // namespace reluva::font

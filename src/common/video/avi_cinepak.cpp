// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "common/video/avi_cinepak.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <memory>
namespace reluva::video {
namespace {
using Bytes=std::span<const uint8_t>;
uint16_t le16(const uint8_t* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
uint32_t le32(const uint8_t* p){return uint32_t(le16(p))|uint32_t(le16(p+2))<<16;}
uint16_t be16(const uint8_t* p){return uint16_t(p[0])<<8|p[1];}
uint32_t be32(const uint8_t* p){return uint32_t(be16(p))<<16|be16(p+2);}
bool tag(const uint8_t* p,const char* s){return std::memcmp(p,s,4)==0;}
struct Parser {
    const std::vector<uint8_t>& bytes;
    Bytes main;
    struct Stream {Bytes header,format;};
    std::array<Stream,2> streams{};
    std::vector<Bytes> packets;
    std::vector<uint8_t> pcm;
    size_t streamCount=0;
    // Context: root=0, headers=1, video=2, audio=3, movie/rec=4.
    bool chunks(size_t at,size_t end,int context,int depth=0) {
        if(depth>8)return false;
        while(at<end) {
            if(end-at<8)return false;
            const auto* p=bytes.data()+at;const size_t n=le32(p+4),begin=at+8;
            if(n>end-begin || (n&1)>end-begin-n)return false;
            Bytes data(bytes.data()+begin,n);
            if(tag(p,"LIST")) {
                if(n<4)return false;
                int next=-1;
                if(context==0 && tag(data.data(),"hdrl"))next=1;
                else if(context==1 && tag(data.data(),"strl")) {
                    if(streamCount==2)return false;
                    next=int(2+streamCount++);
                } else if(context==0 && tag(data.data(),"movi"))next=4;
                else if(context==4 && tag(data.data(),"rec "))next=4;
                if(next>=0 && !chunks(begin+4,begin+n,next,depth+1))return false;
            } else if(context==1 && tag(p,"avih")) {
                if(!main.empty() || n<56)return false;
                main=data;
            } else if((context==2 || context==3) && (tag(p,"strh") || tag(p,"strf"))) {
                auto& stream=streams[size_t(context-2)];const bool header=tag(p,"strh");
                auto& field=header ? stream.header : stream.format;
                if(!field.empty() || n<(header ? 56u : context==2 ? 40u : 16u))return false;
                field=data;
            } else if(context==4) {
                if(tag(p,"00dc") || tag(p,"00db")) {
                    if(packets.size()==512)return false;
                    packets.push_back(data);
                } else if(tag(p,"01wb")) {
                    if(n>16*1024*1024-pcm.size())return false;
                    pcm.insert(pcm.end(),data.begin(),data.end());
                } else if(p[0]>='0' && p[0]<='9' && p[1]>='0' && p[1]<='9')return false;
            }
            at=begin+n+(n&1);
        }
        return at==end;
    }
};
struct Entry {
    std::array<std::array<uint8_t,4>,4> colors{};
    bool valid=false;
};
struct Books { std::array<Entry,256> v1{},v4{}; };
struct Cursor {
    Bytes data;size_t at=0;uint32_t flags=0;unsigned remaining=0;
    bool byte(uint8_t& out){if(at==data.size())return false;out=data[at++];return true;}
    bool bit(bool& out){
        if(!remaining){if(data.size()-at<4)return false;flags=be32(data.data()+at);at+=4;remaining=32;}
        out=(flags&0x80000000u)!=0;flags<<=1;--remaining;return true;
    }
};
bool codebook(Bytes data,unsigned id,Books& books) {
    auto& book=(id&0x200) ? books.v1 : books.v4;
    const bool gray=(id&0x400)!=0,partial=(id&0x100)!=0;
    const size_t stride=gray?4:6;
    Cursor cursor{data};size_t index=0;
    while(cursor.at<data.size() || (partial && cursor.remaining)) {
        if(index==256)return false;
        bool update=true;
        if(partial) {if(!cursor.bit(update))return false;}
        ++index;
        if(!update)continue;
        if(data.size()-cursor.at<stride)return false;
        const auto* values=data.data()+cursor.at;cursor.at+=stride;
        const int u=gray?0:int(values[4])-(values[4]>=128?256:0);
        const int v=gray?0:int(values[5])-(values[5]>=128?256:0);
        // The integer U/2 chroma contribution truncates toward zero.
        const int half=u/2;
        auto& entry=book[index-1];entry.valid=true;
        for(size_t y=0;y<4;++y) {
            entry.colors[y]={uint8_t(std::clamp(int(values[y])+2*v,0,255)),
                             uint8_t(std::clamp(int(values[y])-half-v,0,255)),
                             uint8_t(std::clamp(int(values[y])+2*u,0,255)),255};
        }
        // A final flags word may describe untouched entries after the last
        // replacement. Consume its remaining bits even with no payload left.
    }
    return true;
}
bool vectors(Bytes data,unsigned id,const Books& books,int top,int bottom,image::Image& image,bool prior) {
    Cursor cursor{data};
    for(int y=top;y<bottom;y+=4)for(int x=0;x<image.w;x+=4) {
        bool four=false,update=true;
        if(id==0x3100 && !cursor.bit(update))return false;
        if(!update){if(!prior)return false;continue;}
        if(id!=0x3200 && !cursor.bit(four))return false;
        std::array<uint8_t,4> refs{};
        for(size_t i=0;i<(four?4u:1u);++i) {
            if(!cursor.byte(refs[i]) || !(four ? books.v4 : books.v1)[refs[i]].valid)return false;
        }
        for(int dy=0;dy<4;++dy)for(int dx=0;dx<4;++dx) {
            const size_t ref=four ? size_t(dy/2*2+dx/2) : 0;
            const size_t sample=four ? size_t(dy%2*2+dx%2) : size_t(dy/2*2+dx/2);
            const auto& color=(four?books.v4:books.v1)[refs[ref]].colors[sample];
            std::copy(color.begin(),color.end(),image.pixel(x+dx,y+dy));
        }
    }
    // Encoders word-align vector chunks; the alignment byte is not pixel data.
    return data.size()-cursor.at<=1;
}
bool frame(Bytes data,std::array<Books,32>& books,image::Image& image,bool prior) {
    if(data.size()<10 || (data[0]&~1) || ((uint32_t(data[1])<<16)|be16(data.data()+2))!=data.size() ||
       be16(data.data()+4)!=image.w || be16(data.data()+6)!=image.h)return false;
    const size_t strips=be16(data.data()+8);if(!strips || strips>books.size())return false;
    size_t at=10;int bottom=0;
    for(size_t strip=0;strip<strips;++strip) {
        if(data.size()-at<12)return false;
        const auto* p=data.data()+at;const unsigned id=be16(p),size=be16(p+2);
        if((id!=0x1000 && id!=0x1100) || size<12 || size>data.size()-at)return false;
        int top=be16(p+4),end=be16(p+8);
        if(strip && !top){top=bottom;end+=bottom;}
        if(top!=bottom || be16(p+6)!=0 || be16(p+10)!=image.w || end<=top || end>image.h || top%4 || end%4)return false;
        if(strip && !(data[0]&1))books[strip]=books[strip-1];
        const size_t limit=at+size;at+=12;bool haveVectors=false;
        while(at<limit) {
            if(limit-at<4)return false;
            const unsigned kind=be16(data.data()+at),length=be16(data.data()+at+2);
            if(length<4 || length>limit-at)return false;
            const auto payload=data.subspan(at+4,length-4);
            if(kind>=0x2000 && kind<=0x2700 && !(kind&0xff)) {
                if(haveVectors || !codebook(payload,kind,books[strip]))return false;
            } else if(kind==0x3000 || kind==0x3100 || kind==0x3200) {
                if(haveVectors || !vectors(payload,kind,books[strip],top,end,image,prior))return false;
                haveVectors=true;
            } else return false;
            at+=length;
        }
        if(!haveVectors)return false;
        bottom=end;
    }
    return at==data.size() && bottom==image.h;
}
}
uint64_t AviCinepak::durationMillis() const {
    return rate ? (uint64_t(frames.size())*scale*1000+rate-1)/rate : 0;
}
size_t AviCinepak::frameAt(uint64_t elapsed,bool loop) const {
    const auto duration=durationMillis();
    if(!duration || !scale || frames.empty())return 0;
    elapsed=loop ? elapsed%duration : std::min(elapsed,duration-1);
    return size_t(std::min<uint64_t>(frames.size()-1,elapsed*rate/(uint64_t(scale)*1000)));
}
Error AviCinepak::read(const std::vector<uint8_t>& bytes) {
    *this={};
    auto invalid=[] {return Error::fail("Unsupported or malformed AVI: expected bounded Cinepak with optional PCM audio");};
    if(bytes.size()<12 || bytes.size()>64*1024*1024 || !tag(bytes.data(),"RIFF") || !tag(bytes.data()+8,"AVI ") ||
       le32(bytes.data()+4)!=bytes.size()-8)return invalid();
    Parser parser{bytes,{},{},{},{},0};
    if(!parser.chunks(12,bytes.size(),0) || parser.main.empty() || parser.packets.empty())return invalid();
    const auto& video=parser.streams[0];if(video.header.empty() || video.format.empty())return invalid();
    const auto* a=parser.main.data();const auto* s=video.header.data();const auto* f=video.format.data();
    const uint32_t w=le32(f+4),h=le32(f+8),rate=le32(s+24),scale=le32(s+20);
    if(le32(a+24)!=parser.streamCount || le32(a+32)!=w || le32(a+36)!=h || !tag(s,"vids") ||
       (!tag(s+4,"cvid") && !tag(s+4,"CVID")) || le32(s+28) || le32(f)<40 || le32(f)>video.format.size() ||
       le16(f+12)!=1 || le16(f+14)!=24 || (!tag(f+16,"cvid") && !tag(f+16,"CVID")) ||
       !w || !h || w>2048 || h>2048 || w%4 || h%4 || !rate || !scale || rate>1000000 || scale>1000000 ||
       le32(a+16)!=parser.packets.size() || le32(s+32)!=parser.packets.size() ||
       uint64_t(w)*h*4*parser.packets.size()+parser.pcm.size()*2>128*1024*1024)return invalid();
    AviCinepak result;result.width=int(w);result.height=int(h);result.rate=rate;result.scale=scale;
    if(parser.streamCount==2) {
        const auto& audio=parser.streams[1];
        if(audio.header.empty() || audio.format.empty() || !tag(audio.header.data(),"auds") || le32(audio.header.data()+28))return invalid();
        const auto* fmt=audio.format.data();const unsigned channels=le16(fmt+2),bits=le16(fmt+14),align=le16(fmt+12);
        const auto hz=le32(fmt+4);
        if(le16(fmt)!=1 || (channels!=1 && channels!=2) || (bits!=8 && bits!=16) || !hz || hz>192000 ||
           align!=channels*(bits/8) || le32(fmt+8)!=hz*align || parser.pcm.size()%align || parser.pcm.empty())return invalid();
        auto& sound=result.soundtrack;sound.rate=int(hz);sound.channels=int(channels);sound.format=1;
        sound.samples.reserve(parser.pcm.size()/(bits/8));
        for(size_t i=0;i<parser.pcm.size();i+=bits/8) {
            const int n=bits==8 ? (int(parser.pcm[i])-128)*256 : int(le16(parser.pcm.data()+i));
            sound.samples.push_back(bits==8 ? int16_t(n) : int16_t(n<32768?n:n-65536));
        }
    } else if(!parser.pcm.empty())return invalid();
    // Codebooks are bounded and heap allocated; no large decoder stack frame.
    auto books=std::make_unique<std::array<Books,32>>();
    image::Image image;image.w=int(w);image.h=int(h);image.rgba.resize(size_t(w)*h*4);
    bool prior=false;
    for(auto packet:parser.packets) {
        if(!frame(packet,*books,image,prior))return invalid();
        result.frames.push_back(image);prior=true;
    }
    *this=std::move(result);return Error::success();
}
} // namespace reluva::video

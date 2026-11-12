// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/cursor_image.h"
#include <algorithm>
namespace reluva::boggle {
namespace {
uint16_t u16(const std::vector<uint8_t>& b,size_t p){return uint16_t(b[p])|uint16_t(b[p+1])<<8;}
uint32_t u32(const std::vector<uint8_t>& b,size_t p){return uint32_t(u16(b,p))|uint32_t(u16(b,p+2))<<16;}
Error bad(){return Error::fail("Invalid or unsupported indexed cursor bitmap");}
}
Error readCursorImage(const std::vector<uint8_t>& b,CursorImage& out) {
    out={};
    if(b.size()<44 || b.size()>1024*1024 || u32(b,4)!=40)return bad();
    const auto w=u32(b,8),dualHeight=u32(b,12);const auto depth=u16(b,18);
    if(!w || w>256 || !dualHeight || dualHeight>512 || dualHeight%2 ||
       u16(b,16)!=1 || (depth!=1 && depth!=4 && depth!=8) || u32(b,20))return bad();
    const unsigned h=dualHeight/2;
    const unsigned palette=u32(b,36)?u32(b,36):1u<<depth;
    if(!palette || palette>(1u<<depth) || u32(b,40)>palette || u16(b,0)>=w || u16(b,2)>=h)return bad();
    const size_t pitch=(w*depth+31)/32*4,maskPitch=(w+31)/32*4,offset=44+palette*4;
    const size_t xorBytes=pitch*h,maskBytes=maskPitch*h;
    if(b.size()!=offset+xorBytes+maskBytes ||
       (u32(b,24) && u32(b,24)!=xorBytes && u32(b,24)!=xorBytes+maskBytes))return bad();
    CursorImage result;result.colors.w=int(w);result.colors.h=int(h);
    result.colors.rgba.resize(size_t(w)*h*4);result.mask.resize(size_t(w)*h);
    result.hotspotX=u16(b,0);result.hotspotY=u16(b,2);result.depth=depth;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        const size_t row=offset+(h-1-y)*pitch;
        const unsigned bit=x*depth,index=(b[row+bit/8]>>(8-depth-bit%8))&((1u<<depth)-1);
        if(index>=palette)return bad();
        auto* pixel=result.colors.pixel(int(x),int(y));const size_t color=44+index*4;
        pixel[0]=b[color+2];pixel[1]=b[color+1];pixel[2]=b[color];pixel[3]=255;
        const size_t mask=offset+xorBytes+(h-1-y)*maskPitch+x/8;
        result.mask[size_t(y)*w+x]=(b[mask]>>(7-x%8))&1?255:0;
    }
    out=std::move(result);return Error::success();
}
Error readCursorGroup(const std::vector<PeResource>& resources,uint32_t id,uint16_t language,CursorImage& out) {
    out={};const auto* group=findPeResource(resources,12,id,language);
    if(!group || group->data.size()<6)return Error::fail("Missing cursor group");
    const auto& b=group->data;const auto count=u16(b,4);
    if(u16(b,0) || u16(b,2)!=2 || !count || count>32 || b.size()!=6+size_t(count)*14)
        return Error::fail("Invalid cursor group directory");
    CursorImage best;
    for(size_t i=0;i<count;++i) {
        const size_t at=6+i*14;const auto* member=findPeResource(resources,1,u16(b,at+12),language);
        if(!member || member->data.size()!=u32(b,at+8))return Error::fail("Missing or mismatched cursor group member");
        CursorImage image;if(auto error=readCursorImage(member->data,image))return error;
        if(u16(b,at)!=image.colors.w || u16(b,at+2)!=image.colors.h*2 ||
           u16(b,at+4)!=1 || u16(b,at+6)!=image.depth)return Error::fail("Cursor group metadata mismatch");
        if(image.depth>best.depth)best=std::move(image);
    }
    out=std::move(best);return Error::success();
}
void CursorImage::draw(Surface& screen,int x,int y,int numerator,int denominator) const {
    const auto w=colors.w,h=colors.h;
    if(screen.format!=PixelFormat::RGBA32 || w<1 || w>256 || h<1 || h>256 ||
       numerator<1 || numerator>8 || denominator<1 || denominator>8 || hotspotX>=unsigned(w) || hotspotY>=unsigned(h) ||
       colors.rgba.size()!=size_t(w)*h*4 || mask.size()!=size_t(w)*h)return;
    const int64_t left=int64_t(x)-hotspotX*numerator/denominator,top=int64_t(y)-hotspotY*numerator/denominator;
    const int64_t right=left+(w*numerator+denominator-1)/denominator,bottom=top+(h*numerator+denominator-1)/denominator;
    for(int64_t py=std::max<int64_t>(0,top);py<std::min<int64_t>(screen.h,bottom);++py)
        for(int64_t px=std::max<int64_t>(0,left);px<std::min<int64_t>(screen.w,right);++px) {
            const auto sx=int((px-left)*denominator/numerator),sy=int((py-top)*denominator/numerator);
            const auto* color=colors.pixel(sx,sy);const auto andMask=mask[size_t(sy)*w+sx];
            if(andMask==255 && !color[0] && !color[1] && !color[2])continue;
            auto* dst=screen.row(int(py))+px*4;
            for(unsigned c=0;c<3;++c)dst[c]=(dst[c]&andMask)^color[c];
            dst[3]=255;
        }
}
} // namespace reluva::boggle

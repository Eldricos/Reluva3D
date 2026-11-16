// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/bitmap.h"
namespace reluva::boggle {
namespace {
uint16_t u16(const std::vector<uint8_t>& b,size_t p){return uint16_t(b[p])|uint16_t(b[p+1])<<8;}
uint32_t u32(const std::vector<uint8_t>& b,size_t p){return uint32_t(u16(b,p))|uint32_t(u16(b,p+2))<<16;}
Error bad(){return Error::fail("Invalid or unsupported bitmap resource");}
}
Error readBitmapResource(const std::vector<uint8_t>& b,image::Image& out){
    out={};if(b.size()<40 || b.size()>16*1024*1024 || u32(b,0)!=40)return bad();
    const auto w=u32(b,4),rawHeight=u32(b,8);const int64_t signedHeight=rawHeight&0x80000000u?int64_t(rawHeight)-0x100000000ll:rawHeight;
    const uint64_t h=signedHeight<0?uint64_t(-signedHeight):uint64_t(signedHeight);const auto depth=u16(b,14);
    if(!w || w>2048 || !h || h>2048 || u16(b,12)!=1 || (depth!=8 && depth!=24) || u32(b,16))return bad();
    const uint32_t colors=depth==8?(u32(b,32)?u32(b,32):256):0;
    if((depth==8 && (colors>256 || u32(b,36)>colors)) || (depth==24 && (u32(b,32) || u32(b,36))))return bad();
    const size_t pitch=(size_t(w)*depth+31)/32*4,pixels=pitch*size_t(h),offset=40+size_t(colors)*4;
    if(b.size()!=offset+pixels || (u32(b,20) && u32(b,20)!=pixels))return bad();
    image::Image result;result.w=int(w);result.h=int(h);result.rgba.resize(size_t(w)*size_t(h)*4);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const size_t row=offset+(signedHeight<0?y:size_t(h)-1-y)*pitch;
        size_t color=row+size_t(x)*3;
        if(depth==8){const auto index=b[row+x];if(index>=colors)return bad();color=40+size_t(index)*4;}
        auto* p=result.pixel(int(x),int(y));p[0]=b[color+2];p[1]=b[color+1];p[2]=b[color];p[3]=255;
    }
    out=std::move(result);return Error::success();
}
} // namespace reluva::boggle

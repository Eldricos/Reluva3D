// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/frame_comparison.h"
#include <algorithm>
#include <cmath>
namespace reluva::boggle {
Error compareFrames(const image::Image& a,const image::Image& b,int x,int y,int w,int h,FrameComparison& out){
    out={};
    if(a.w<=0 || a.h<=0 || a.w>4096 || a.h>4096 || b.w!=a.w || b.h!=a.h ||
       a.rgba.size()!=size_t(a.w)*a.h*4 || b.rgba.size()!=a.rgba.size() || x<0 || y<0 || w<=0 || h<=0 || int64_t(x)+w>a.w || int64_t(y)+h>a.h)
        return Error::fail("Frames must have matching bounded RGBA dimensions and a valid comparison region");
    uint64_t sum=0,squared=0;FrameComparison next;next.pixels=uint64_t(w)*h;
    for(int row=y;row<y+h;++row)for(int col=x;col<x+w;++col){
        bool changed=false;for(int channel=0;channel<3;++channel){
            const auto delta=unsigned(std::abs(int(a.pixel(col,row)[channel])-int(b.pixel(col,row)[channel])));
            changed|=delta!=0;next.maxDelta=std::max(next.maxDelta,delta);sum+=delta;squared+=delta*delta;
        }
        next.changed+=changed;
    }
    next.meanAbsolute=double(sum)/double(next.pixels*3);next.rmse=std::sqrt(double(squared)/double(next.pixels*3));out=next;return Error::success();
}
}

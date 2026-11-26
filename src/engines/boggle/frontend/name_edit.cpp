// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/name_edit.h"
#include "common/font.h"
#include "engines/boggle/frontend/appearance.h"
#include <algorithm>
namespace reluva::boggle {
namespace {
Error valid(std::string_view text){
    if(text.size()>18)return Error::fail("Use at most 18 characters.");
    if(!std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32 && c<=126;}))return Error::fail("Use printable English characters.");
    return Error::success();
}
}
Error NameEdit::begin(std::string_view value){if(auto error=valid(value))return error;text_=value;selectAll();return Error::success();}
Error NameEdit::insert(std::string_view value){
    if(value.empty())return Error::success();
    if(auto error=valid(value))return error;
    const size_t begin=first(),count=last()-begin;
    if(text_.size()-count+value.size()>18)return Error::fail("Use at most 18 characters.");
    text_.replace(begin,count,value);place(begin+value.size(),false);return Error::success();
}
void NameEdit::move(int direction,bool extend){
    if(!direction)return;
    if(!extend && first()!=last()){place(direction<0?first():last(),false);return;}
    place(direction<0?(caret_?caret_-1:0):std::min(caret_+1,text_.size()),extend);
}
void NameEdit::pointer(int x,SelectionRect field,int padding,bool extend){
    // Use the same glyph origin/stride as draw(), rounding to the nearest
    // insertion boundary. Wide arithmetic also handles out-of-window samples.
    if(ui::active()){place(ui::position(text_,x,field,padding,caret_),extend);return;}
    const auto position=std::clamp<int64_t>((int64_t(x)-field.x-padding+6)/12,0,int64_t(text_.size()));
    place(size_t(position),extend);
}
void NameEdit::erase(bool forward){
    if(first()!=last()){const size_t begin=first();text_.erase(begin,last()-begin);place(begin,false);}
    else if(forward){if(caret_<text_.size())text_.erase(caret_,1);}
    else if(caret_){text_.erase(caret_-1,1);place(caret_-1,false);}
}
void NameEdit::draw(Surface& screen,SelectionRect f,int padding) const {
    if(ui::active()){ui::field(screen,f,text_,first(),last(),caret_,padding);return;}
    if(screen.format!=PixelFormat::RGBA32 || screen.w<=0 || screen.h<=0 || screen.w>8192 || screen.h>8192 || screen.pitch<screen.w*4 || size_t(screen.pitch)>screen.pixels.size()/size_t(screen.h) ||
       padding<4 || padding>32 || f.x<0 || f.y<0 || f.w<216+padding*2 || f.h<24 || int64_t(f.x)+f.w>screen.w || int64_t(f.y)+f.h>screen.h)return;
    auto box=[&](SelectionRect b,Color color){for(int y=b.y;y<b.y+b.h;++y)for(int x=b.x;x<b.x+b.w;++x){auto* p=screen.row(y)+x*4;p[0]=color.r;p[1]=color.g;p[2]=color.b;p[3]=255;}};
    box(f,{224,189,99});box({f.x+2,f.y+2,f.w-4,f.h-4},{28,31,49});
    const int x=f.x+padding,y=f.y+(f.h-16)/2;
    if(first()!=last())box({x+int(first())*12,y-2,int(last()-first())*12,20},{43,83,112});
    ui::text(screen,x,y,text_,{245,245,255},2);
    box({x+int(caret_)*12,y,1,16},{245,245,255});
}
} // namespace reluva::boggle

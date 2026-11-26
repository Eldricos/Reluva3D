// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/panel_art.h"
#include "common/fs.h"
#include "common/font.h"
#include "engines/boggle/frontend/appearance.h"
#include <algorithm>
namespace reluva::boggle {
Error PanelArt::open(const Resources& resources,const std::filesystem::path& root,std::string_view path,int w,int h){
    *this={};PanelArt next;
    if(fs::findFile(root,std::string(path))){
        if(auto e=resources.image(path,next.image_))return e;
        if(next.image_.w!=w || next.image_.h!=h)return Error::fail("Unsupported Classic panel artwork dimensions");
    }
    if(fs::findFile(root,"Fonts/NELSON.TTF"))if(auto e=resources.font("Fonts/NELSON.TTF",20,next.font_))return e;
    *this=std::move(next);return Error::success();
}
bool PanelArt::draw(Surface& s,int x,int y,int w,int h) const {
    if(image_.rgba.empty() || s.format!=PixelFormat::RGBA32 || s.w<=0 || s.h<=0 || s.w>8192 || s.h>8192 ||
       s.pitch<s.w*4 || size_t(s.pitch)>s.pixels.size()/size_t(s.h) || x<0 || y<0 || w<=0 || h<=0 || int64_t(x)+w>s.w || int64_t(y)+h>s.h)return false;
    for(int row=0;row<h;++row)for(int col=0;col<w;++col)
        std::copy_n(image_.pixel(col*image_.w/w,row*image_.h/h),4,s.row(y+row)+(x+col)*4);
    return true;
}
void PanelArt::title(Surface& s,int x,int y,std::string_view text,Color color,int width) const {
    if(ui::active()){ui::text(s,x,y,text,color,2);return;}
    if(font_.loaded())font_.draw(s,x,y+20,text,color,width);
    else ui::text(s,x,y,text,color,2);
}
}

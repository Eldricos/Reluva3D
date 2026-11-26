// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/notice_dialog.h"
#include "engines/boggle/formats/dialog.h"
#include "common/font.h"
#include "engines/boggle/frontend/appearance.h"
#include <algorithm>
namespace reluva::boggle {
Error ClassicNoticeDialog::read(const std::vector<PeResource>& resources,uint16_t language){
    *this={};const auto* resource=findPeResource(resources,5,16006,language);if(!resource)return Error::success();
    DialogTemplate dialog;if(auto error=readDialog(resource->data,dialog))return error;
    auto fail=[] {return Error::fail("Unsupported Classic minimum-word notice layout");};
    if(dialog.rect.w<180 || dialog.rect.w>384 || dialog.rect.h<90 || dialog.rect.h>220)return fail();
    ClassicNoticeDialog result;result.bounds_={(800-ui::dx(dialog.rect.w,800))/2,(600-ui::dy(dialog.rect.h,600))/2,ui::dx(dialog.rect.w,800),ui::dy(dialog.rect.h,600)};
    const DialogControl* button=nullptr;const DialogControl* message=nullptr;
    for(const auto& c:dialog.controls){
        if(c.id==1349){if(button)return fail();button=&c;}
        if(c.id==1350){if(message)return fail();message=&c;}
    }
    auto valid=[&](const DialogControl* c,int klass,int width,int height){
        if(!c || !c->windowClass.ordinal || c->windowClass.id!=klass)return false;
        const auto r=c->rect;return r.x>=0 && r.y>=12 && r.w>=width && r.h>=height && r.x+r.w<=dialog.rect.w && r.y+r.h<=dialog.rect.h;
    };
    if(!valid(button,0x80,40,12) || !valid(message,0x82,120,24) || message->rect.y+message->rect.h+4>button->rect.y)return fail();
    auto placed=[&](DialogRect r){return HistoryButton{result.bounds_.x+ui::dx(r.x,800),result.bounds_.y+ui::dy(r.y,600),ui::dx(r.w,800),ui::dy(r.h,600)};};
    result.okay_=placed(button->rect);result.message_=placed(message->rect);result.installed_=true;*this=std::move(result);return Error::success();
}
void ClassicNoticeDialog::draw(Surface& screen,std::string_view text) const {
    if(!installed_ || screen.format!=PixelFormat::RGBA32 || screen.w!=800 || screen.h!=600 || screen.pitch<3200 || screen.pixels.size()<size_t(screen.pitch)*600)return;
    auto box=[&](HistoryButton b,Color color){for(int y=std::max(0,b.y);y<std::min(screen.h,b.y+b.h);++y)for(int x=std::max(0,b.x);x<std::min(screen.w,b.x+b.w);++x){auto* p=screen.row(y)+x*4;p[0]=color.r;p[1]=color.g;p[2]=color.b;p[3]=255;}};
    ui::Paper paper(ui::active());
    const Color white{240,240,255};box(bounds_,{50,78,108});box({bounds_.x+3,bounds_.y+3,bounds_.w-6,bounds_.h-6},{22,40,60});ui::panel(screen,{bounds_.x,bounds_.y,bounds_.w,bounds_.h});
    ui::text(screen,bounds_.x+16,bounds_.y+12,"WORD TOO SHORT",white);
    // Native bitmap-font wrapping within the installed message area. No
    // Windows font metrics, format strings or resource text are executed.
    if(ui::active()){ui::wrapped(screen,{message_.x,message_.y,message_.w,message_.h},text,white);ui::button(screen,{okay_.x,okay_.y,okay_.w,okay_.h},"OK",true);return;}
    const size_t columns=size_t(message_.w/6);int y=message_.y;
    text=text.substr(0,1024);
    while(!text.empty() && y+8<=message_.y+message_.h){
        size_t n=std::min(columns,text.size());
        if(n<text.size()){const auto space=text.substr(0,n+1).find_last_of(' ');if(space!=std::string_view::npos && space)n=space;}
        ui::text(screen,message_.x,y,std::string(text.substr(0,n)),white);text.remove_prefix(n);
        while(!text.empty() && text.front()==' ')text.remove_prefix(1);
        y+=12;
    }
    if(ui::active()){ui::button(screen,{okay_.x,okay_.y,okay_.w,okay_.h},"OK",true);return;}
    box(okay_,{48,88,120});ui::text(screen,okay_.x+(okay_.w-12)/2,okay_.y+(okay_.h-8)/2,"OK",white);
}
} // namespace reluva::boggle

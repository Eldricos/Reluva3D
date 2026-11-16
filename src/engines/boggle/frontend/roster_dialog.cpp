// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/roster_dialog.h"
#include "engines/boggle/formats/dialog.h"
#include "common/font.h"
#include <algorithm>
namespace reluva::boggle {
Error FrontendRosterDialog::read(const std::vector<PeResource>& resources,uint16_t language){
    *this={};const auto* resource=findPeResource(resources,5,10000,language);if(!resource)return Error::success();
    DialogTemplate dialog;if(auto error=readDialog(resource->data,dialog))return error;
    auto fail=[] {return Error::fail("Unsupported local-player roster dialog layout");};
    if(dialog.rect.w<300 || dialog.rect.w>640 || dialog.rect.h<100 || dialog.rect.h>300)return fail();
    const int scale=std::min({131072,608*65536/dialog.rect.w,400*65536/dialog.rect.h});
    auto scaled=[&](int n){return (n*scale+32768)/65536;};
    FrontendRosterDialog result;result.bounds_={(640-scaled(dialog.rect.w))/2,(480-scaled(dialog.rect.h))/2,scaled(dialog.rect.w),scaled(dialog.rect.h)};
    auto control=[&](uint32_t id,uint16_t klass)->const DialogControl* {
        const DialogControl* found=nullptr;
        for(const auto& c:dialog.controls)if(c.id==id){if(found)return nullptr;found=&c;}
        if(!found || !found->windowClass.ordinal || found->windowClass.id!=klass)return nullptr;
        auto r=found->rect;
        // A dropdown combo's template height includes its popup list. Only
        // its collapsed face must fit inside the dialog client area.
        if(klass==0x85){if(r.h<14 || r.h>512)return nullptr;r.h=14;}
        if(r.x<0 || r.y<0 || r.w<=0 || r.h<=0 || r.x+r.w>dialog.rect.w || r.y+r.h>dialog.rect.h)return nullptr;
        return found;
    };
    auto placed=[&](DialogRect r){return SelectionRect{result.bounds_.x+scaled(r.x),result.bounds_.y+scaled(r.y),scaled(r.x+r.w)-scaled(r.x),scaled(r.y+r.h)-scaled(r.y)};};
    const auto* done=control(1,0x80);const auto* back=control(2,0x80);const auto* message=control(1344,0x82);if(!done || !back || !message)return fail();
    result.done_=placed(done->rect);result.back_=placed(back->rect);result.message_=placed(message->rect);
    const auto d=result.done_,b=result.back_,m=result.message_;
    if(d.w<80 || b.w<72 || d.h<20 || b.h<20 || d.y!=b.y || d.x+d.w+8>b.x || m.w<300 || m.h<30)return fail();
    for(size_t i=0;i<4;++i){
        const auto* c=control(uint32_t(4107+i),0x85);if(!c)return fail();auto r=c->rect;r.h=14;
        const auto f=placed(r);
        if(f.w<80 || f.h<20 || f.y<m.y+m.h+28 || f.y+f.h+8>d.y ||
           (i && (f.y!=result.fields_[i-1].y || result.fields_[i-1].x+result.fields_[i-1].w+4>f.x)))return fail();
        result.fields_[i]=f;
    }
    result.installed_=true;*this=result;return Error::success();
}
void FrontendRosterDialog::draw(Surface& screen,const std::array<std::string,4>& roster) const {
    if(!installed_ || screen.format!=PixelFormat::RGBA32 || screen.w!=640 || screen.h!=480 || screen.pitch<2560 || screen.pixels.size()<size_t(screen.pitch)*480)return;
    auto box=[&](SelectionRect b,Color color){for(int y=b.y;y<b.y+b.h;++y)for(int x=b.x;x<b.x+b.w;++x){auto* p=screen.row(y)+x*4;p[0]=color.r;p[1]=color.g;p[2]=color.b;p[3]=255;}};
    const Color white{245,245,255},muted{170,181,205},dark{17,15,35},gold{224,189,99};
    box({0,0,640,480},dark);box(bounds_,gold);box({bounds_.x+2,bounds_.y+2,bounds_.w-4,bounds_.h-4},dark);
    font::drawText(screen,message_.x,message_.y,"LOCAL PLAYERS",white,2);
    font::drawText(screen,message_.x,message_.y+22,"1-4 / click: choose a human or computer",muted);
    for(size_t i=0;i<4;++i){
        const auto f=fields_[i];font::drawText(screen,f.x,f.y-18,"PLAYER "+std::to_string(i+1),gold);
        box(f,{88,81,105});box({f.x+2,f.y+2,f.w-4,f.h-4},{41,34,64});
        std::string name=roster[i].empty()?"NO PLAYER":roster[i];const auto columns=size_t((f.w-24)/6);
        if(name.size()>columns)name=name.substr(0,columns-3)+"...";
        font::drawText(screen,f.x+6,f.y+(f.h-8)/2,name,white);font::drawText(screen,f.x+f.w-12,f.y+(f.h-8)/2,"v",white);
    }
    auto button=[&](SelectionRect b,const char* title){box(b,{88,81,105});box({b.x+2,b.y+2,b.w-4,b.h-4},{41,34,64});font::drawText(screen,b.x+(b.w-int(std::char_traits<char>::length(title))*6)/2,b.y+(b.h-8)/2,title,white);};
    button(done_,"DONE (ENTER)");button(back_,"BACK (ESC)");
    font::drawText(screen,40,444,"Use Change Name outside this panel to add names.",muted);
    font::drawText(screen,40,462,"Done / Back keeps this session's roster choices.",muted);
}
} // namespace reluva::boggle

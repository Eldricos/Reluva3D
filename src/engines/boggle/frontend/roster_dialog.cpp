// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/roster_dialog.h"
#include "engines/boggle/formats/dialog.h"
#include "engines/boggle/formats/bitmap.h"
#include "common/font.h"
#include "engines/boggle/frontend/appearance.h"
#include <algorithm>
namespace reluva::boggle {
Error FrontendRosterDialog::read(const std::vector<PeResource>& resources,uint16_t language){
    *this={};const auto* resource=findPeResource(resources,5,10000,language);if(!resource)return Error::success();
    DialogTemplate dialog;if(auto error=readDialog(resource->data,dialog))return error;
    auto fail=[] {return Error::fail("Unsupported local-player roster dialog layout");};
    if(dialog.rect.w<300 || dialog.rect.w>640 || dialog.rect.h<100 || dialog.rect.h>300)return fail();
    const int scale=std::min({131072,608*65536/dialog.rect.w,400*65536/dialog.rect.h});
    auto scaled=[&](int n){return (n*scale+32768)/65536;};
    FrontendRosterDialog result;result.bounds_={(640-(ui::active()?ui::dx(dialog.rect.w,640):scaled(dialog.rect.w)))/2,(480-(ui::active()?ui::dy(dialog.rect.h,480):scaled(dialog.rect.h)))/2,(ui::active()?ui::dx(dialog.rect.w,640):scaled(dialog.rect.w)),(ui::active()?ui::dy(dialog.rect.h,480):scaled(dialog.rect.h))};
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
    auto placed=[&](DialogRect r){return SelectionRect{result.bounds_.x+(ui::active()?ui::dx(r.x,640):scaled(r.x)),result.bounds_.y+(ui::active()?ui::dy(r.y,480):scaled(r.y)),(ui::active()?ui::dx(r.x+r.w,640):scaled(r.x+r.w))-(ui::active()?ui::dx(r.x,640):scaled(r.x)),(ui::active()?ui::dy(r.y+r.h,480):scaled(r.y+r.h))-(ui::active()?ui::dy(r.y,480):scaled(r.y))};};
    const auto* done=control(1,0x80);const auto* back=control(2,0x80);const auto* message=control(1344,0x82);if(!done || !back || !message)return fail();
    result.done_=placed(done->rect);result.cancel_=placed(back->rect);result.message_=placed(message->rect);result.captionText_=ui::caption(message->title.text);result.doneText_=ui::caption(done->title.text);result.cancelText_=ui::caption(back->title.text);
    const auto d=result.done_,b=result.cancel_,m=result.message_;
    if(d.w<80 || b.w<72 || d.h<20 || b.h<20 || d.y!=b.y || d.x+d.w+8>b.x || m.w<300 || m.h<30)return fail();
    for(size_t i=0;i<4;++i){
        const auto* c=control(uint32_t(4107+i),0x85);if(!c)return fail();auto r=c->rect;r.h=14;
        const auto f=placed(r);
        if(f.w<80 || f.h<20 || f.y<m.y+m.h+28 || f.y+f.h+8>d.y ||
           (i && (f.y!=result.fields_[i-1].y || result.fields_[i-1].x+result.fields_[i-1].w+4>f.x)))return fail();
        result.fields_[i]=f;
    }
    bool any=false;for(unsigned id=1300;id<=1302;++id)any|=findPeResource(resources,2,id,language)!=nullptr;
    if(any){
        for(unsigned i=0;i<3;++i){
            const auto* image=findPeResource(resources,2,1300+i,language);if(!image)return Error::fail("Incomplete roster portrait artwork");
            if(auto error=readBitmapResource(image->data,result.portraitsImages_[i]))return error;
            if(result.portraitsImages_[i].w!=64 || result.portraitsImages_[i].h!=64)return fail();
        }
        for(size_t i=0;i<4;++i){
            const auto* icon=control(uint32_t(4111+i),0x82);if(!icon)return fail();const auto p=placed(icon->rect),f=result.fields_[i];
            if(p.w<32 || p.h<32 || p.x<f.x || p.x+p.w>f.x+f.w || p.y<m.y+m.h || p.y+p.h>f.y-10)return fail();
            result.portraitsRects_[i]=p;
        }
        result.portraits_=true;
    }
    result.installed_=true;*this=std::move(result);return Error::success();
}
void FrontendRosterDialog::draw(Surface& screen,const std::array<std::string,4>& roster,const std::array<ComputerLevel,4>& kinds,std::string_view error) const {
    if(!installed_ || screen.format!=PixelFormat::RGBA32 || screen.w!=640 || screen.h!=480 || screen.pitch<2560 || screen.pixels.size()<size_t(screen.pitch)*480)return;
    auto box=[&](SelectionRect b,Color color){for(int y=b.y;y<b.y+b.h;++y)for(int x=b.x;x<b.x+b.w;++x){auto* p=screen.row(y)+x*4;p[0]=color.r;p[1]=color.g;p[2]=color.b;p[3]=255;}};
    const Color white{245,245,255},muted{170,181,205},dark{17,15,35},gold{224,189,99};
    box({0,0,640,480},dark);box(bounds_,gold);box({bounds_.x+2,bounds_.y+2,bounds_.w-4,bounds_.h-4},dark);
    const bool background=art_.draw(screen,bounds_.x,bounds_.y,bounds_.w,bounds_.h);
    ui::Paper paper(ui::active()&&!background);
    if(!background)ui::panel(screen,{bounds_.x,bounds_.y,bounds_.w,bounds_.h});
    if(ui::active())ui::wrapped(screen,message_,captionText_,white);
    else {art_.title(screen,message_.x,message_.y,"LOCAL PLAYERS",white,message_.w);
    ui::text(screen,message_.x,message_.y+22,"1-4 / click: choose a human or computer",muted);}
    for(size_t i=0;i<4;++i){
        const auto f=fields_[i];
        if(portraits_){
            const auto p=portraitsRects_[i];const auto& picture=portraitsImages_[roster[i].empty()?2:kinds[i]==ComputerLevel::Human?0:1];
            const int side=ui::active()?64:std::min({p.w,p.h,picture.w}),left=p.x+(p.w-side)/2,top=p.y+(p.h-side)/2;
            for(int y=0;y<side;++y)for(int x=0;x<side;++x){const auto* src=picture.pixel(x*64/side,y*64/side);auto* dst=screen.row(top+y)+(left+x)*4;std::copy_n(src,4,dst);}
        }
        ui::text(screen,f.x,f.y-(ui::active()?16:portraits_?10:18),"PLAYER "+std::to_string(i+1),gold);
        if(ui::active()){ui::choice(screen,f,roster[i].empty()?"No player":roster[i]);continue;}
        box(f,{88,81,105});box({f.x+2,f.y+2,f.w-4,f.h-4},{41,34,64});
        std::string name=roster[i].empty()?"NO PLAYER":roster[i];const auto columns=size_t((f.w-24)/6);
        if(name.size()>columns)name=name.substr(0,columns-3)+"...";
        ui::text(screen,f.x+6,f.y+(f.h-8)/2,name,white);ui::text(screen,f.x+f.w-12,f.y+(f.h-8)/2,"v",white);
    }
    auto button=[&](SelectionRect b,const char* title){if(ui::active()){if(background)ui::glassButton(screen,b,title);else ui::button(screen,b,title);return;}box(b,{88,81,105});box({b.x+2,b.y+2,b.w-4,b.h-4},{41,34,64});ui::text(screen,b.x+(b.w-int(std::char_traits<char>::length(title))*6)/2,b.y+(b.h-8)/2,title,white);};
    button(done_,ui::active()?doneText_.c_str():"DONE (ENTER)");button(cancel_,ui::active()?cancelText_.c_str():"CANCEL (ESC)");
    ui::text(screen,40,444,"Use Change Name outside this panel to add names.",muted);
    ui::text(screen,40,462,error.empty()?"Done saves choices. Cancel discards changes.":std::string(error.substr(0,90)),error.empty()?muted:Color{255,150,130});
}
} // namespace reluva::boggle

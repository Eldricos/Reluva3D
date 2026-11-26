// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/options_dialog.h"
#include "engines/boggle/formats/dialog.h"
#include "common/font.h"
#include "engines/boggle/frontend/appearance.h"
#include <algorithm>
namespace reluva::boggle {
Error FrontendOptionsDialog::read(const std::vector<PeResource>& resources,uint16_t language){
    *this={};const auto* resource=findPeResource(resources,5,10834,language);if(!resource)return Error::success();
    DialogTemplate dialog;if(auto error=readDialog(resource->data,dialog))return error;
    auto fail=[] {return Error::fail("Unsupported Classic options dialog layout");};
    if(dialog.rect.w<240 || dialog.rect.w>300 || dialog.rect.h<130 || dialog.rect.h>150)return fail();
    FrontendOptionsDialog result;result.bounds_={(640-ui::dx(dialog.rect.w,640))/2,(480-ui::dy(dialog.rect.h,480))/2,ui::dx(dialog.rect.w,640),ui::dy(dialog.rect.h,480)};
    const unsigned ids[]={1312,1313,1311,1299,1300,1907,15303};const int widths[]={126,132,108,108,108,220,90};
    for(size_t i=0;i<Count;++i){
        const DialogControl* found=nullptr;
        for(const auto& c:dialog.controls)if(c.id==ids[i]){if(found)return fail();found=&c;}
        if(!found)return fail();
        const auto& klass=found->windowClass;
        if(i==Slider){if(klass.ordinal || klass.text!=u"msctls_trackbar32")return fail();}
        else if(!klass.ordinal || klass.id!=0x80)return fail();
        const auto r=found->rect;if(r.x<0 || r.y<0 || r.w<=0 || r.h<=0 || r.x+r.w>dialog.rect.w || r.y+r.h>dialog.rect.h)return fail();
        const SelectionRect b{result.bounds_.x+ui::dx(r.x,640),result.bounds_.y+ui::dy(r.y,480),ui::dx(r.w,640),ui::dy(r.h,480)};
        if(b.w<ui::dx(widths[i]/2) || b.h<ui::dy(i==Slider?21:8))return fail();
        for(size_t j=0;j<i;++j){const auto a=result.controls_[j];if(a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h)return fail();}
        result.controls_[i]=b;result.labels_[i]=ui::caption(found->title.text);
    }
    for(const auto& c:dialog.controls)if(c.windowClass.ordinal && c.windowClass.id==0x80 && (c.style&15)==7){
        const auto r=c.rect;if(r.x<0||r.y<0||r.w<=0||r.h<=0||r.x+r.w>dialog.rect.w||r.y+r.h>dialog.rect.h)return fail();
        result.groups_.push_back({{result.bounds_.x+ui::dx(r.x),result.bounds_.y+ui::dy(r.y),ui::dx(r.w),ui::dy(r.h)},ui::caption(c.title.text)});
    }
    result.installed_=true;*this=result;return Error::success();
}
uint64_t FrontendOptionsDialog::durationAt(int x) const {
    if(!installed_)return 180000;
    const auto b=control(Slider);const int span=b.w-16;
    const auto relative=std::clamp<int64_t>(int64_t(x)-b.x-8,0,span);
    return 60000+uint64_t((relative*8+span/2)/span)*30000;
}
void FrontendOptionsDialog::draw(Surface& screen,const ClassicOptions& options,bool backgroundAllowed,bool cursorAllowed) const {
    if(!installed_ || screen.format!=PixelFormat::RGBA32 || screen.w!=640 || screen.h!=480 || screen.pitch<2560 || screen.pixels.size()<size_t(screen.pitch)*480)return;
    auto box=[&](SelectionRect b,Color color){for(int y=b.y;y<b.y+b.h;++y)for(int x=b.x;x<b.x+b.w;++x){auto* p=screen.row(y)+x*4;p[0]=color.r;p[1]=color.g;p[2]=color.b;p[3]=255;}};
    ui::Paper paper(ui::active());
    const Color white{245,245,255},muted{170,181,205},dark{17,15,35},gold{224,189,99};
    box({0,0,640,480},ui::active()?Color{192,192,192}:dark);ui::text(screen,48,48,"CLASSIC OPTIONS",white,2);
    box(bounds_,gold);box({bounds_.x+2,bounds_.y+2,bounds_.w-4,bounds_.h-4},dark);ui::panel(screen,{bounds_.x,bounds_.y,bounds_.w,bounds_.h});
    for(const auto& group:groups_)ui::group(screen,group.first,group.second);
    auto check=[&](Control id,const char* title,bool selected,bool enabled=true){const auto b=control(id);if(ui::active()){ui::check(screen,b,labels_[size_t(id)],selected,enabled,id==Chess||id==Traditional);return;}box({b.x,b.y+(b.h-10)/2,10,10},enabled?muted:Color{60,60,75});if(selected)box({b.x+2,b.y+(b.h-10)/2+2,6,6},gold);ui::text(screen,b.x+18,b.y+(b.h-8)/2,title,enabled?white:muted);};
    check(Cursor,"COMPUTER CURSOR (V)",options.cursor,cursorAllowed);check(Background,"BACKGROUND PLAY (B)",options.background,backgroundAllowed);
    check(Orient,"AUTO ORIENT (A)",options.autoOrient);check(Chess,"CHESS TIMER (C)",options.turns==ClassicTurnMode::Chess);check(Traditional,"TRADITIONAL (R)",options.turns==ClassicTurnMode::Traditional);
    check(Untimed,"NO TIMER (T)",!options.duration);
    const auto s=control(Slider);const auto ink=options.duration?gold:muted;const int left=s.x+8,right=s.x+s.w-8,y=s.y+12;
    box({left,y,right-left,2},ink);
    for(int i=0;i<9;++i)box({left+(right-left)*i/8,y-3,1,8},ink);
    if(options.duration){const auto value=std::clamp<uint64_t>(options.duration,60000,300000);box({left+int((value-60000)*(right-left)/240000)-3,y-5,7,12},white);}
    ui::text(screen,left,s.y+s.h-8,"1:00",muted);ui::text(screen,right-24,s.y+s.h-8,"5:00",muted);
    const auto seconds=options.duration/1000;const auto time=options.duration?std::to_string(seconds/60)+(seconds%60?":30":":00"):"NO TIMER";
    ui::text(screen,s.x+s.w/2-int(time.size())*3,s.y+s.h-8,time,white);
    auto button=[&](SelectionRect b,const char* title){if(ui::active()){ui::button(screen,b,title);return;}box(b,{88,81,105});box({b.x+2,b.y+2,b.w-4,b.h-4},{41,34,64});ui::text(screen,b.x+12,b.y+14,title,white);};
    button(done(),"DONE (ENTER)");button(cancel(),"CANCEL (ESC)");
    ui::text(screen,40,458,"Drag / Left/Right: 30 sec. Done applies; Cancel discards.",muted);
}
} // namespace reluva::boggle

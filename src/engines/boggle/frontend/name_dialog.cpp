// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/name_dialog.h"
#include "engines/boggle/formats/dialog.h"
#include "common/font.h"
#include "engines/boggle/frontend/appearance.h"
namespace reluva::boggle {
Error FrontendNameDialog::read(const std::vector<PeResource>& resources,uint16_t language){
    *this={};const auto* resource=findPeResource(resources,5,1600,language);if(!resource)return Error::success();
    DialogTemplate dialog;if(auto error=readDialog(resource->data,dialog))return error;
    auto fail=[] {return Error::fail("Unsupported player-name dialog layout");};
    if(dialog.rect.w<131 || dialog.rect.w>300 || dialog.rect.h<51 || dialog.rect.h>160)return fail();
    FrontendNameDialog result;result.bounds_={(640-ui::dx(dialog.rect.w,640))/2,(480-ui::dy(dialog.rect.h,480))/2,ui::dx(dialog.rect.w,640),ui::dy(dialog.rect.h,480)};
    auto control=[&](uint32_t id,uint16_t klass)->const DialogControl* {
        const DialogControl* found=nullptr;
        for(const auto& c:dialog.controls)if(c.id==id){if(found)return nullptr;found=&c;}
        if(!found || !found->windowClass.ordinal || found->windowClass.id!=klass)return nullptr;
        const auto r=found->rect;if(r.x<0 || r.y<0 || r.w<=0 || r.h<=0 || r.x+r.w>dialog.rect.w || r.y+r.h>dialog.rect.h)return nullptr;
        return found;
    };
    const auto* field=control(1601,0x81);const auto* save=control(1,0x80);const auto* caption=control(65535,0x82);if(!field || !save || !caption)return fail();
    auto placed=[&](DialogRect r){return SelectionRect{result.bounds_.x+ui::dx(r.x,640),result.bounds_.y+ui::dy(r.y,480),ui::dx(r.w,640),ui::dy(r.h,480)};};
    result.field_=placed(field->rect);result.save_=placed(save->rect);result.caption_=placed(caption->rect);result.captionText_=ui::caption(caption->title.text);result.saveText_=ui::caption(save->title.text);
    const auto f=result.field_,b=result.save_,c=result.caption_;
    if(f.w<ui::dx(116) || f.h<ui::dy(12) || b.w<ui::dx(40) || b.h<ui::dy(12) || c.w<ui::dx(33) || c.h<ui::dy(4) ||
       c.y+c.h+ui::dy(2)>f.y || f.y+f.h+ui::dy(3)>b.y)return fail();
    result.cancel_={result.bounds_.x+10,b.y,ui::dx(50),b.h};
    if(result.cancel_.x+result.cancel_.w+8>b.x)return fail();
    result.installed_=true;*this=result;return Error::success();
}
void FrontendNameDialog::draw(Surface& screen,const NameEdit& edit,std::string_view error) const {
    if(!installed_ || screen.format!=PixelFormat::RGBA32 || screen.w!=640 || screen.h!=480 || screen.pitch<2560 || screen.pixels.size()<size_t(screen.pitch)*480)return;
    auto box=[&](SelectionRect b,Color color){for(int y=b.y;y<b.y+b.h;++y)for(int x=b.x;x<b.x+b.w;++x){auto* p=screen.row(y)+x*4;p[0]=color.r;p[1]=color.g;p[2]=color.b;p[3]=255;}};
    ui::Paper paper(ui::active());
    const Color white{245,245,255},muted{170,181,205},dark{17,15,35},gold{224,189,99};
    box(bounds_,gold);box({bounds_.x+2,bounds_.y+2,bounds_.w-4,bounds_.h-4},dark);ui::panel(screen,{bounds_.x,bounds_.y,bounds_.w,bounds_.h});
    ui::text(screen,caption_.x,caption_.y,ui::active()?captionText_:"PLAYER NAME",white);
    edit.draw(screen,field_);
    auto button=[&](SelectionRect b,const char* title){if(ui::active()){ui::button(screen,b,title);return;}box(b,{88,81,105});box({b.x+2,b.y+2,b.w-4,b.h-4},{41,34,64});ui::text(screen,b.x+(b.w-int(std::char_traits<char>::length(title))*6)/2,b.y+(b.h-8)/2,title,white);};
    button(save_,ui::active()?saveText_.c_str():"SAVE (ENTER)");button(cancel_,ui::active()?"Cancel":"CANCEL (ESC)");
    // Keep native help outside the compact template and the setup controls.
    box({0,440,640,40},ui::active()?Color{192,192,192}:dark);ui::text(screen,40,444,"18 chars. Arrows/Home/End: move. Shift: select. Ctrl+A: all",muted);
    if(!error.empty())ui::text(screen,40,462,std::string(error.substr(0,76)),{255,150,130});
}
} // namespace reluva::boggle

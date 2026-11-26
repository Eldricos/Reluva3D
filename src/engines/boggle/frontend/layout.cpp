// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/layout.h"
#include "engines/boggle/formats/dialog.h"
#include "engines/boggle/resources.h"
#include "common/fs.h"
#include <algorithm>
#include "engines/boggle/frontend/appearance.h"
namespace reluva::boggle {
Error FrontendSelectionLayout::read(const std::vector<PeResource>& resources,uint16_t language){
    *this={};const auto* resource=findPeResource(resources,5,10813,language);if(!resource)return Error::success();
    DialogTemplate dialog;if(auto error=readDialog(resource->data,dialog))return error;
    auto fail=[] {return Error::fail("Unsupported game-selection dialog layout");};
    if(dialog.rect.w<320 || dialog.rect.w>640 || dialog.rect.h<240 || dialog.rect.h>480)return fail();
    // Fit each axis to the existing selection bitmap's 640x480 canvas. This
    // native mapping does not use Windows font-dependent dialog-unit metrics.
    auto x=[&](int n){return (n*640+dialog.rect.w/2)/dialog.rect.w;};
    auto y=[&](int n){return (n*480+dialog.rect.h/2)/dialog.rect.h;};
    FrontendSelectionLayout result;
    for(size_t i=0;i<result.modes_.size();++i){
        const DialogControl* found=nullptr;
        for(const auto& c:dialog.controls)if(c.id==15001+i){if(found)return fail();found=&c;}
        if(!found || !found->windowClass.ordinal || found->windowClass.id!=0x82)return fail();
        const auto r=found->rect;
        if(r.x<0 || r.y<0 || r.w<=0 || r.h<=0 || r.x+r.w>dialog.rect.w || r.y+r.h>dialog.rect.h)return fail();
        const SelectionRect b{x(r.x),y(r.y),x(r.x+r.w)-x(r.x),y(r.y+r.h)-y(r.y)};
        // Leave room for the existing native title and launcher controls.
        if(b.w<88 || b.h<80 || b.y<190 || b.y+b.h>410)return fail();
        if(i && result.modes_[i-1].x+result.modes_[i-1].w>b.x)return fail();
        result.modes_[i]=b;
    }
    result.installed_=true;*this=result;return Error::success();
}
Error FrontendVersionLayout::read(const std::vector<PeResource>& resources,uint16_t language){
    *this={};const auto* resource=findPeResource(resources,5,10002,language);if(!resource)return Error::success();
    DialogTemplate dialog;if(auto error=readDialog(resource->data,dialog))return error;
    auto fail=[] {return Error::fail("Unsupported Classic version dialog layout");};
    if(dialog.rect.w<180 || dialog.rect.w>280 || dialog.rect.h<100 || dialog.rect.h>130)return fail();
    FrontendVersionLayout result;result.bounds_={(640-ui::dx(dialog.rect.w,640))/2,ui::active()?(480-ui::dy(dialog.rect.h,480))/2:80,ui::dx(dialog.rect.w,640),ui::dy(dialog.rect.h,480)};
    auto control=[&](uint32_t id,uint16_t klass)->const DialogControl* {
        const DialogControl* found=nullptr;
        for(const auto& c:dialog.controls)if(c.id==id){if(found)return nullptr;found=&c;}
        if(!found || !found->windowClass.ordinal || found->windowClass.id!=klass)return nullptr;
        const auto r=found->rect;
        if(r.x<0 || r.y<0 || r.w<=0 || r.h<=0 || r.x+r.w>dialog.rect.w || r.y+r.h>dialog.rect.h)return nullptr;
        return found;
    };
    auto placed=[&](DialogRect r){return SelectionRect{result.bounds_.x+ui::dx(r.x,640),result.bounds_.y+ui::dy(r.y,480),ui::dx(r.w,640),ui::dy(r.h,480)};};
    for(size_t i=0;i<2;++i){const auto* c=control(1800+uint32_t(i),0x80);if(!c)return fail();result.modes_[i]=placed(c->rect);result.labels_[i]=ui::caption(c->title.text);}
    const auto* caption=control(1337,0x82);if(!caption)return fail();result.prompt_=placed(caption->rect);result.caption_=ui::caption(caption->title.text);
    const auto p=result.prompt_,a=result.modes_[0],b=result.modes_[1];
    // Reserve each native board hint below its button, and leave the native
    // duration/actions outside the supplied template's client area.
    if(p.w<ui::dx(78) || p.h<ui::dy(8) || a.w<ui::dx(82) || b.w<ui::dx(82) || a.h<ui::dy(14) || b.h<ui::dy(14) ||
       p.y+p.h+4>a.y || a.y+a.h+14>b.y || b.y+b.h+14>result.bounds_.y+result.bounds_.h)return fail();
    result.installed_=true;*this=result;return Error::success();
}
Error FrontendVersionLayout::readBackground(const Resources& resources,const std::filesystem::path& root){
    background_={};if(!installed_ || !fs::findFile(root,"Bitmaps/GS1CS.bmp"))return Error::success();
    image::Image result;if(auto error=resources.image("Bitmaps/GS1CS.bmp",result))return error;
    if(result.w!=300 || result.h!=200)return Error::fail("Unsupported Classic version background dimensions");
    background_=std::move(result);return Error::success();
}
bool FrontendVersionLayout::drawBackground(Surface& screen) const {
    if(!installed_ || background_.rgba.empty() || screen.format!=PixelFormat::RGBA32 || screen.w!=640 || screen.h!=480 ||
       screen.pitch<2560 || size_t(screen.pitch)>screen.pixels.size()/480)return false;
    for(int y=0;y<bounds_.h;++y)for(int x=0;x<bounds_.w;++x){
        const auto* src=background_.pixel(x*background_.w/bounds_.w,y*background_.h/bounds_.h);
        std::copy_n(src,4,screen.row(bounds_.y+y)+(bounds_.x+x)*4);
    }
    return true;
}
} // namespace reluva::boggle

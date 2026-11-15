// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/layout.h"
#include "engines/boggle/formats/dialog.h"
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
} // namespace reluva::boggle

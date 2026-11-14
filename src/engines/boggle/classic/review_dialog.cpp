// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/review_dialog.h"
#include "engines/boggle/formats/dialog.h"
#include "common/font.h"
#include <algorithm>
namespace reluva::boggle {
Error ClassicReviewDialog::read(const std::vector<PeResource>& resources,uint16_t language){
    *this={};const auto* resource=findPeResource(resources,5,16003,language);if(!resource)return Error::success();
    DialogTemplate dialog;if(auto error=readDialog(resource->data,dialog))return error;
    auto fail=[] {return Error::fail("Unsupported Classic dictionary-review dialog layout");};
    if(dialog.rect.w<200 || dialog.rect.w>384 || dialog.rect.h<70 || dialog.rect.h>220)return fail();
    ClassicReviewDialog result;result.bounds_={(800-dialog.rect.w*2)/2,(600-dialog.rect.h*2)/2,dialog.rect.w*2,dialog.rect.h*2};
    const std::array<uint32_t,5> ids={6,7,4,3,15801};
    for(size_t i=0;i<ids.size();++i){
        const DialogControl* found=nullptr;
        for(const auto& c:dialog.controls)if(c.id==ids[i]){if(found)return fail();found=&c;}
        if(!found || !found->windowClass.ordinal || found->windowClass.id!=(i<4?0x80:0x82))return fail();
        const auto r=found->rect;
        if(r.x<0 || r.y<0 || r.w<(i<4?42:200) || r.h<(i<4?12:24) || r.x+r.w>dialog.rect.w || r.y+r.h>dialog.rect.h)return fail();
        if(i<4)result.buttons_[i]={result.bounds_.x+r.x*2,result.bounds_.y+r.y*2,r.w*2,r.h*2};
        else result.message_={result.bounds_.x+r.x*2,result.bounds_.y+r.y*2};
    }
    for(size_t i=0;i<4;++i)for(size_t j=i+1;j<4;++j){const auto a=result.buttons_[i],b=result.buttons_[j];
        if(a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h)return fail();
    }
    result.installed_=true;*this=std::move(result);return Error::success();
}
void ClassicReviewDialog::draw(Surface& screen,std::string_view word,std::string_view message) const {
    if(!installed_ || screen.format!=PixelFormat::RGBA32 || screen.w!=800 || screen.h!=600 || screen.pitch<3200 || screen.pixels.size()<size_t(screen.pitch)*600)return;
    auto box=[&](HistoryButton b,Color color){for(int y=std::max(0,b.y);y<std::min(screen.h,b.y+b.h);++y)for(int x=std::max(0,b.x);x<std::min(screen.w,b.x+b.w);++x){auto* p=screen.row(y)+x*4;p[0]=color.r;p[1]=color.g;p[2]=color.b;p[3]=255;}};
    box(bounds_,{50,78,108});box({bounds_.x+3,bounds_.y+3,bounds_.w-6,bounds_.h-6},{22,40,60});
    const Color white{240,240,255},muted{180,195,210};
    font::drawText(screen,bounds_.x+16,bounds_.y+12,"ADD TO YOUR DICTIONARY?",white);
    font::drawText(screen,message_.x,message_.y,std::string(word),{255,116,116},word.size()>35?1:2);
    font::drawText(screen,message_.x,message_.y+24,"Yes keeps its points; No removes them.",muted);
    font::drawText(screen,bounds_.x+16,bounds_.y+bounds_.h-58,message.empty()?"All: remaining words for this player.":std::string(message.substr(0,size_t((bounds_.w-32)/6))),muted);
    constexpr std::array<const char*,4> labels={"YES (1)","NO (2)","YES TO ALL (3)","NO TO ALL (4)"};
    for(size_t i=0;i<4;++i){const auto b=buttons_[i];box(b,{48,88,120});font::drawText(screen,b.x+(b.w-int(std::char_traits<char>::length(labels[i]))*6)/2,b.y+(b.h-8)/2,labels[i],white);}
}
} // namespace reluva::boggle

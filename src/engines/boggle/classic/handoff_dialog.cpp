// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/handoff_dialog.h"
#include "engines/boggle/formats/dialog.h"
#include "common/font.h"
#include <algorithm>
namespace reluva::boggle {
Error ClassicHandoffDialog::read(const std::vector<PeResource>& resources,uint16_t language){
    *this={};const auto* resource=findPeResource(resources,5,15900,language);if(!resource)return Error::success();
    DialogTemplate dialog;if(auto error=readDialog(resource->data,dialog))return error;
    auto fail=[] {return Error::fail("Unsupported Classic turn-handoff dialog layout");};
    if(dialog.rect.w<240 || dialog.rect.w>384 || dialog.rect.h<160 || dialog.rect.h>276)return fail();
    ClassicHandoffDialog result;result.bounds_={(800-dialog.rect.w*2)/2,(600-dialog.rect.h*2)/2,dialog.rect.w*2,dialog.rect.h*2};
    auto control=[&](uint32_t id,uint16_t klass)->const DialogControl* {
        const DialogControl* found=nullptr;
        for(const auto& c:dialog.controls)if(c.id==id){if(found)return nullptr;found=&c;}
        if(!found || !found->windowClass.ordinal || found->windowClass.id!=klass)return nullptr;
        const auto r=found->rect;if(r.x<0 || r.y<0 || r.w<=0 || r.h<=0 || r.x+r.w>dialog.rect.w || r.y+r.h>dialog.rect.h)return nullptr;
        return found;
    };
    const auto* start=control(1,0x80);const auto* message=control(15901,0x82);if(!start || !message)return fail();
    auto placed=[&](DialogRect r){return HistoryButton{result.bounds_.x+r.x*2,result.bounds_.y+r.y*2,r.w*2,r.h*2};};
    result.start_=placed(start->rect);result.message_=placed(message->rect);
    const auto b=result.start_,m=result.message_;
    if(b.w<88 || b.h<24 || m.w<360 || m.h<112 || m.y<result.bounds_.y+48 ||
       m.y+m.h+8>b.y || b.y+b.h+32>result.bounds_.y+result.bounds_.h)return fail();
    result.installed_=true;*this=result;return Error::success();
}
void ClassicHandoffDialog::draw(Surface& screen,std::string_view next,bool chess,uint64_t remaining,bool timed,const std::vector<std::string>& finished) const {
    if(!installed_ || screen.format!=PixelFormat::RGBA32 || screen.w!=800 || screen.h!=600 || screen.pitch<3200 || screen.pixels.size()<size_t(screen.pitch)*600)return;
    auto box=[&](HistoryButton b,Color color){for(int y=b.y;y<b.y+b.h;++y)for(int x=b.x;x<b.x+b.w;++x){auto* p=screen.row(y)+x*4;p[0]=color.r;p[1]=color.g;p[2]=color.b;p[3]=255;}};
    const Color white{240,240,255},muted{180,195,210};
    // Passing the controls must not expose the current board or word list.
    box({0,0,800,600},{18,32,48});box(bounds_,{50,78,108});box({bounds_.x+3,bounds_.y+3,bounds_.w-6,bounds_.h-6},{22,40,60});
    art_.draw(screen,bounds_.x,bounds_.y,bounds_.w,bounds_.h);
    art_.title(screen,bounds_.x+16,bounds_.y+16,chess && !finished.empty()?"PLAYER FINISHED":"NEXT PLAYER",white,bounds_.w-32);
    int y=message_.y;
    if(chess)for(size_t i=0;i<std::min(finished.size(),size_t(3));++i){
        font::drawText(screen,message_.x,y,finished[i].substr(0,18)+"'s game is over.",muted);y+=14;
    }
    font::drawText(screen,message_.x,y+4,std::string(next.substr(0,18))+(chess && !finished.empty()?" is up.":""),white,2);y+=30;
    std::string detail="The same board / a full round for each player.";
    if(chess){
        // Ceiling without overflowing a caller-supplied remaining duration.
        const uint64_t seconds=remaining/1000+bool(remaining%1000);
        detail=timed?"Your paused timer: "+std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60):"Chess Timer / Not Timed";
    }
    font::drawText(screen,message_.x,y,detail,muted);
    box(start_,{48,88,120});font::drawText(screen,start_.x+(start_.w-78)/2,start_.y+(start_.h-8)/2,"START (ENTER)",white);
    font::drawText(screen,bounds_.x+16,bounds_.y+bounds_.h-20,"Pass the controls, then start when ready.",muted);
}
} // namespace reluva::boggle

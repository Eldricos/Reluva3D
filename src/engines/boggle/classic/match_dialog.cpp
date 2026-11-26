// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/match_dialog.h"
#include "engines/boggle/formats/dialog.h"
#include "common/font.h"
#include "engines/boggle/frontend/appearance.h"
#include <algorithm>
namespace reluva::boggle {
Error ClassicMatchDialog::read(const std::vector<PeResource>& resources,uint16_t language){
    *this={};const auto* resource=findPeResource(resources,5,16008,language);if(!resource)return Error::success();
    DialogTemplate dialog;if(auto error=readDialog(resource->data,dialog))return error;
    auto fail=[] {return Error::fail("Unsupported Classic match-results dialog layout");};
    if(dialog.rect.w<240 || dialog.rect.w>384 || dialog.rect.h<180 || dialog.rect.h>276)return fail();
    ClassicMatchDialog result;result.bounds_={(800-ui::dx(dialog.rect.w,800))/2,(600-ui::dy(dialog.rect.h,600))/2,ui::dx(dialog.rect.w,800),ui::dy(dialog.rect.h,600)};
    auto control=[&](uint32_t id,uint16_t klass)->const DialogControl* {
        const DialogControl* found=nullptr;
        for(const auto& c:dialog.controls)if(c.id==id){if(found)return nullptr;found=&c;}
        if(!found || !found->windowClass.ordinal || found->windowClass.id!=klass)return nullptr;
        const auto r=found->rect;if(r.x<0 || r.y<0 || r.w<=0 || r.h<=0 || r.x+r.w>dialog.rect.w || r.y+r.h>dialog.rect.h)return nullptr;
        return found;
    };
    auto placed=[&](DialogRect r){return HistoryButton{result.bounds_.x+ui::dx(r.x,800),result.bounds_.y+ui::dy(r.y,600),ui::dx(r.w,800),ui::dy(r.h,600)};};
    const auto* close=control(1,0x80);const auto* scores=control(2,0x80);const auto* message=control(1352,0x82);
    if(!close || !scores || !message)return fail();
    result.close_=placed(close->rect);result.scores_=placed(scores->rect);result.message_=placed(message->rect);
    const auto a=result.scores_,b=result.close_,m=result.message_;
    if(a.w<108 || b.w<60 || a.h<24 || b.h<24 || a.y!=b.y || a.x+a.w+8>b.x ||
       std::max(a.y+a.h,b.y+b.h)+24>result.bounds_.y+result.bounds_.h || m.w<360 || m.h<48)return fail();
    for(size_t row=0;row<4;++row)for(size_t col=0;col<2;++col){
        const auto* c=control(uint32_t(15400+col*10+row),0x82);if(!c)return fail();
        auto cell=placed(c->rect);if(cell.w<(col?72:132) || cell.h<16)return fail();result.rows_[row][col]=cell;
    }
    for(size_t row=0;row<4;++row){
        const auto n=result.rows_[row][0],s=result.rows_[row][1];const int bottom=std::max(n.y+n.h,s.y+s.h);
        if(n.y!=s.y || n.x+n.w+8>s.x || n.y<m.y+m.h+8)return fail();
        if(row<3 && bottom+4>result.rows_[row+1][0].y)return fail();
        if(row==3 && bottom+24>a.y)return fail();
    }
    result.installed_=true;*this=std::move(result);return Error::success();
}
std::optional<size_t> ClassicMatchDialog::playerAt(int x,int y,size_t count) const {
    for(size_t i=0;i<std::min(count,size_t(4));++i){
        if(installed_){if(rows_[i][0].contains(x,y) || rows_[i][1].contains(x,y))return i;}
        else if(HistoryButton{150,180+int(i)*40,480,40}.contains(x,y))return i;
    }
    return {};
}
void ClassicMatchDialog::draw(Surface& screen,const std::vector<std::string>& names,const ClassicCompetition& competition,bool chess,bool background) const {
    if(!installed_ || screen.format!=PixelFormat::RGBA32 || screen.w!=800 || screen.h!=600 || screen.pitch<3200 || screen.pixels.size()<size_t(screen.pitch)*600)return;
    auto box=[&](HistoryButton b,Color color){for(int y=b.y;y<b.y+b.h;++y)for(int x=b.x;x<b.x+b.w;++x){auto* p=screen.row(y)+x*4;p[0]=color.r;p[1]=color.g;p[2]=color.b;p[3]=255;}};
    ui::Paper paper(ui::active());
    const Color white{240,240,255},muted{180,195,210};
    box(bounds_,{50,78,108});box({bounds_.x+3,bounds_.y+3,bounds_.w-6,bounds_.h-6},{22,40,60});ui::panel(screen,{bounds_.x,bounds_.y,bounds_.w,bounds_.h});
    ui::text(screen,message_.x,message_.y,"CLASSIC RESULTS",white,2);
    ui::text(screen,message_.x,message_.y+26,std::string(chess?"CHESS TIMER":"TRADITIONAL")+(background?" / BACKGROUND CPU":""),muted);
    ui::text(screen,message_.x,message_.y+40,"SHARED WORDS SCORE ZERO",muted);
    for(size_t i=0;i<std::min({names.size(),competition.players().size(),size_t(4)});++i){
        const auto n=rows_[i][0],s=rows_[i][1];const int y=n.y+(n.h-8)/2;
        ui::text(screen,n.x,y,std::to_string(i+1),white);
        ui::text(screen,n.x+24,y,names[i].substr(0,size_t((n.w-24)/6)),white);
        const auto score=std::to_string(competition.players()[i].score);const int scale=int(score.size())*12<=s.w?2:1;
        ui::text(screen,s.x,s.y+(s.h-8*scale)/2,score,white,scale);
    }
    ui::text(screen,bounds_.x+16,scores_.y-16,"1-4 or click a player to review words.",muted);
    ui::text(screen,bounds_.x+16,std::max(close_.y+close_.h,scores_.y+scores_.h)+12,"Grey review words are shared / worth zero.",muted);
    auto button=[&](HistoryButton b,const char* text){if(ui::active()){ui::button(screen,{b.x,b.y,b.w,b.h},text);return;}box(b,{48,88,120});ui::text(screen,b.x+(b.w-int(std::char_traits<char>::length(text))*6)/2,b.y+(b.h-8)/2,text,white);};
    button(close_,"OK");button(scores_,ui::active()?"High scores":"HIGH SCORES (H)");
}
} // namespace reluva::boggle

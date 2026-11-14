// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/scores_dialog.h"
#include "engines/boggle/formats/dialog.h"
#include "common/font.h"
#include <algorithm>
namespace reluva::boggle {
Error ClassicScoresDialog::read(const std::vector<PeResource>& resources,uint16_t language){
    *this={};const auto* resource=findPeResource(resources,5,16007,language);if(!resource)return Error::success();
    DialogTemplate dialog;if(auto error=readDialog(resource->data,dialog))return error;
    auto fail=[] {return Error::fail("Unsupported Classic high-score dialog layout");};
    if(dialog.rect.w<240 || dialog.rect.w>768 || dialog.rect.h<180 || dialog.rect.h>512)return fail();
    // Keep the two-pixel native policy where it fits, then uniformly shrink
    // large templates inside an 800x600 viewport with a surrounding margin.
    const int scale=std::min({131072,768*65536/dialog.rect.w,552*65536/dialog.rect.h});
    auto scaled=[&](int n){return (n*scale+32768)/65536;};
    ClassicScoresDialog result;result.bounds_={(800-scaled(dialog.rect.w))/2,(600-scaled(dialog.rect.h))/2,scaled(dialog.rect.w),scaled(dialog.rect.h)};
    if(result.bounds_.w<360 || result.bounds_.h<240)return fail();
    auto control=[&](uint32_t id,uint16_t klass)->const DialogControl* {
        const DialogControl* found=nullptr;
        for(const auto& c:dialog.controls)if(c.id==id){if(found)return nullptr;found=&c;}
        if(!found || !found->windowClass.ordinal || found->windowClass.id!=klass)return nullptr;
        const auto r=found->rect;if(r.x<0 || r.y<0 || r.w<=0 || r.h<=0 || r.x+r.w>dialog.rect.w || r.y+r.h>dialog.rect.h)return nullptr;
        return found;
    };
    auto placed=[&](DialogRect r){const int x=scaled(r.x),y=scaled(r.y);return HistoryButton{result.bounds_.x+x,result.bounds_.y+y,scaled(r.x+r.w)-x,scaled(r.y+r.h)-y};};
    const auto* close=control(1,0x80);if(!close)return fail();result.close_=placed(close->rect);
    if(result.close_.w<48 || result.close_.h<20)return fail();
    for(size_t row=0;row<10;++row)for(size_t column=0;column<3;++column){
        const auto* c=control(uint32_t(15400+column*10+row),0x82);if(!c)return fail();
        const auto b=placed(c->rect);if(b.w<(column==2?48:60) || b.h<8)return fail();
        result.rows_[row][column]=b;
    }
    for(size_t row=0;row<10;++row){
        const auto& cells=result.rows_[row];
        if(cells[0].x<result.bounds_.x+40 || cells[0].y<result.bounds_.y+140)return fail();
        for(size_t column=1;column<3;++column)
            if(cells[column].y!=cells[0].y || cells[column-1].x+cells[column-1].w>cells[column].x)return fail();
        int bottom=0;for(const auto b:cells)bottom=std::max(bottom,b.y+b.h);
        if(row<9 && bottom>result.rows_[row+1][0].y)return fail();
        if(row==9 && bottom+8>result.close_.y)return fail();
    }
    result.retry_={result.bounds_.x+16,result.close_.y,144,result.close_.h};
    if(result.retry_.x+result.retry_.w+12>result.close_.x)return fail();
    result.installed_=true;*this=std::move(result);return Error::success();
}
void ClassicScoresDialog::draw(Surface& screen,const HighScores& scores,std::string_view error) const {
    if(!installed_ || screen.format!=PixelFormat::RGBA32 || screen.w!=800 || screen.h!=600 || screen.pitch<3200 || screen.pixels.size()<size_t(screen.pitch)*600)return;
    auto box=[&](HistoryButton b,Color color){for(int y=std::max(0,b.y);y<std::min(screen.h,b.y+b.h);++y)for(int x=std::max(0,b.x);x<std::min(screen.w,b.x+b.w);++x){auto* p=screen.row(y)+x*4;p[0]=color.r;p[1]=color.g;p[2]=color.b;p[3]=255;}};
    const Color white{240,240,255},muted{180,195,210};
    box(bounds_,{50,78,108});box({bounds_.x+3,bounds_.y+3,bounds_.w-6,bounds_.h-6},{22,40,60});
    const int x=bounds_.x+16,y=bounds_.y;
    font::drawText(screen,x,y+16,"HIGH SCORES",white,2);
    font::drawText(screen,x,y+48,"CLASSIC / ALL VERSIONS AND ROUND LENGTHS",muted);
    font::drawText(screen,x,y+68,"Top ten Classic scores; player names may repeat.",muted);
    font::drawText(screen,x,y+88,error.empty()?(scores.persistent()?"Saved in your launcher profile.":"Session only; no save directory configured."):"Scores unavailable until a successful retry.",muted);
    if(!error.empty())font::drawText(screen,x,y+110,std::string(error.substr(0,size_t((bounds_.w-32)/6))),{255,116,116});
    const int heading=rows_[0][0].y-20;
    font::drawText(screen,rows_[0][0].x-36,heading,"RANK",white);
    const char* headings[]={"NAME","SCORE","VERSION"};for(size_t col=0;col<3;++col)font::drawText(screen,rows_[0][col].x,heading,headings[col],white);
    const auto scoresRows=scores.table();
    for(size_t i=0;i<scoresRows.size() && i<10;++i){
        const auto& row=scoresRows[i];const auto& cells=rows_[i];font::drawText(screen,cells[0].x-36,cells[0].y,std::to_string(i+1),white);
        auto name=row.name;const size_t columns=size_t(cells[0].w/6);if(name.size()>columns)name=name.substr(0,columns-3)+"...";
        font::drawText(screen,cells[0].x,cells[0].y,name,white);font::drawText(screen,cells[1].x,cells[1].y,std::to_string(row.score),white);
        font::drawText(screen,cells[2].x,cells[2].y,row.variant==ClassicVariant::Master?"MASTER":"ORIGINAL",white);
    }
    if(scoresRows.empty())font::drawText(screen,rows_[0][0].x,rows_[0][0].y,error.empty()?"No scores yet.":"Table unavailable.",muted);
    auto button=[&](HistoryButton b,const char* text){box(b,{48,88,120});font::drawText(screen,b.x+(b.w-int(std::char_traits<char>::length(text))*6)/2,b.y+(b.h-8)/2,text,white);};
    button(close_,"CLOSE");if(!error.empty())button(retry_,"RETRY (R)");
}
} // namespace reluva::boggle

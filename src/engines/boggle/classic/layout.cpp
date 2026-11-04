// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/layout.h"
#include "engines/boggle/classic/orientation.h"
#include "common/font.h"
#include "common/fs.h"
#include <algorithm>
#include <cstdio>
namespace reluva::boggle {
namespace {
void box(Surface& screen,int x,int y,int w,int h,Color color) {
    for(int py=std::max(0,y);py<std::min(screen.h,y+h);++py)
        for(int px=std::max(0,x);px<std::min(screen.w,x+w);++px) {
            auto* p=screen.row(py)+px*4;p[0]=color.r;p[1]=color.g;p[2]=color.b;p[3]=255;
        }
}
void scaled(Surface& screen,const image::Image& image,int x,int y,int w,int h,bool key=false) {
    for(int py=std::max(0,y);py<std::min(screen.h,y+h);++py)
        for(int px=std::max(0,x);px<std::min(screen.w,x+w);++px) {
            const auto* p=image.pixel((px-x)*image.w/w,(py-y)*image.h/h);
            if(!key || p[0] || p[1] || p[2])std::copy_n(p,4,screen.row(py)+px*4);
        }
}
using Vertex=ProjectedVertex;
int64_t edge(Vertex a,Vertex b,int x,int y) {return int64_t(b.x-a.x)*(y-a.y)-int64_t(b.y-a.y)*(x-a.x);}
Vertex point(bool master,int u,int v,int d) {
    // Native approximation of the open tray, measured in the source 640x480
    // images. This is not the original 3D camera/projection or dice rotation.
    const std::array<ClassicPoint,4> p=master ? std::array<ClassicPoint,4>{{{137,107},{418,107},{447,351},{94,351}}}
                                                : std::array<ClassicPoint,4>{{{135,145},{386,142},{409,350},{109,350}}};
    return PlaneProjection(p).project(u,v,d,5,4);
}
std::array<Vertex,4> cell(bool master,size_t index,size_t side,int margin=6) {
    const int x=int(index%side)*100,y=int(index/side)*100,d=int(side)*100;
    return {point(master,x+margin,y+margin,d),point(master,x+100-margin,y+margin,d),
            point(master,x+100-margin,y+100-margin,d),point(master,x+margin,y+100-margin,d)};
}
void triangle(Surface& screen,Vertex a,Vertex b,Vertex c,Color color,const image::Image* texture=nullptr) {
    int64_t area=edge(a,b,c.x,c.y);if(!area)return;
    if(area<0){std::swap(b,c);area=-area;}
    for(int y=std::max(0,std::min({a.y,b.y,c.y}));y<=std::min(screen.h-1,std::max({a.y,b.y,c.y}));++y)
        for(int x=std::max(0,std::min({a.x,b.x,c.x}));x<=std::min(screen.w-1,std::max({a.x,b.x,c.x}));++x) {
            const int64_t wa=edge(b,c,x,y),wb=edge(c,a,x,y),wc=edge(a,b,x,y);
            if(wa<0 || wb<0 || wc<0)continue;
            auto* out=screen.row(y)+x*4;
            if(texture) {
                // Interpolate texture coordinates divided by depth, then undo
                // that division. Affine UVs introduce a diagonal seam and bend
                // letter strokes on the tilted plane.
                const int64_t qa=wa*a.reciprocalDepth,qb=wb*b.reciprocalDepth,qc=wc*c.reciprocalDepth;
                const int64_t divisor=qa+qb+qc;
                if(!divisor)continue;
                const int u=int((qa*a.u+qb*b.u+qc*c.u)/divisor),v=int((qa*a.v+qb*b.v+qc*c.v)/divisor);
                std::copy_n(texture->pixel(u,v),4,out);
            } else {out[0]=color.r;out[1]=color.g;out[2]=color.b;out[3]=255;}
        }
}
void quad(Surface& s,std::array<Vertex,4> p,Color c,const image::Image* image=nullptr) {
    triangle(s,p[0],p[1],p[2],c,image);triangle(s,p[0],p[2],p[3],c,image);
}
}
std::array<std::string,3> classicArtworkPaths(ClassicVariant variant) {
    const std::string prefix=variant==ClassicVariant::Master ? "ClsM1" : "ClsO1";
    return {"Bitmaps/"+prefix+"W.bmp","Bitmaps/"+prefix+"N.bmp","Anims/"+prefix+"Tim.avi"};
}
Error ClassicArtwork::open(const Resources& resources,const std::filesystem::path& root,const ClassicOptions& options,
                           const video::AviVideo1& prepared) {
    *this={};if(!options.artwork)return Error::success();
    const auto paths=classicArtworkPaths(options.variant);
    size_t found=0;for(const auto& path:paths)if(fs::findFile(root,path))++found;
    if(!found)return Error::success(); // legacy imports retain the prototype
    if(found!=paths.size())return Error::fail("Incomplete Classic artwork; re-import or disable Classic installed artwork");
    if(auto err=resources.image(paths[0],words_))return err;
    if(auto err=resources.image(paths[1],names_))return err;
    if(auto err=resources.animation(paths[2],timer_))return err;
    master_=options.variant==ClassicVariant::Master;autoOrient_=options.autoOrient;
    if(words_.w!=143 || words_.h!=480 || names_.w!=497 || names_.h!=32 ||
       timer_.width!=(master_?80:68) || timer_.height!=(master_?140:128) || timer_.frames.size()!=(master_?39:38))
        return Error::fail("Unsupported Classic artwork dimensions or timer length");
    if(!prepared.frames.empty()) {
        if(prepared.frames.size()!=55)return Error::fail("Unsupported Classic scene length");
        open_=prepared.frames[43];closed_=prepared.frames[54];
    } else {
        video::AviVideo1 still;
        if(auto err=resources.animation(master_?"Anims/ClsM1All.avi":"Anims/ClsO1All.avi",still))return err;
        if(still.frames.size()!=55)return Error::fail("Unsupported Classic scene length");
        open_=std::move(still.frames[43]);closed_=std::move(still.frames[54]);
    }
    if(options.handwriting && fs::findFile(root,"Fonts/KPSI.TTF"))
        if(auto error=resources.font("Fonts/KPSI.TTF",18,handwriting_))return error;
    active_=true;return Error::success();
}
size_t ClassicArtwork::timerFrame(uint64_t remaining,uint64_t duration) {
    if(!duration || duration>300000 || remaining>=duration)return 0;
    const uint64_t progress=(duration-remaining)*36/duration;
    return size_t(progress ? progress-1 : 0);
}
ClassicPoint ClassicArtwork::cellCenter(size_t index,size_t side) const {
    if((side!=4 && side!=5) || index>=side*side)return {-1,-1};
    const auto p=point(master_,int(index%side)*100+50,int(index/side)*100+50,int(side)*100);return {p.x,p.y};
}
std::optional<size_t> ClassicArtwork::cellAt(int x,int y,size_t side) const {
    if(side!=4 && side!=5)return {};
    for(size_t i=0;i<side*side;++i) {
        const auto p=cell(master_,i,side);bool inside=true;
        for(size_t n=0;n<4;++n)if(edge(p[n],p[(n+1)%4],x,y)<0){inside=false;break;}
        if(inside)return i;
    }
    return {};
}
void ClassicArtwork::draw(Surface& screen,const ClassicRound& round,uint64_t now,uint64_t duration,
                          const image::Image& letters,const image::Image& qu,const std::vector<size_t>& selected,size_t scroll,uint32_t boardSeed) const {
    const bool playing=round.phase()==RoundPhase::Playing;
    scaled(screen,playing?open_:closed_,0,0,800,600);
    scaled(screen,names_,0,0,621,40);scaled(screen,words_,621,0,179,600);
    const Color white{245,245,255},black{12,12,20};
    font::drawText(screen,12,13,"PLAYER 1",white);
    font::drawText(screen,113,13,std::to_string(round.score()),white);
    font::drawText(screen,180,13,master_?"CLASSIC MASTER":"CLASSIC ORIGINAL",white);
    font::drawText(screen,638,12,"PLAYER 1",white,2);
    const size_t side=round.board().side();
    if(playing)for(size_t i=0;i<side*side;++i) {
        auto outer=cell(master_,i,side),inner=cell(master_,i,side,10);
        auto front=outer;front[0]=outer[3];front[1]=outer[2];front[2].y+=7;front[3].y+=7;
        quad(screen,front,{90,96,115});
        const bool marked=std::find(selected.begin(),selected.end(),i)!=selected.end();
        quad(screen,outer,marked?Color{50,220,240}:Color{174,176,192});
        const auto& face=round.board().faces()[i];
        if(face.size()==1 || face=="QU") {
            const auto& texture=face=="QU" ? qu : letters;const int index=face[0]-'A';
            const int x=index%5*512/6,y=index/5*512/6;
            const int right=(index%5+1)*512/6-1,bottom=(index/5+1)*512/6-1;
            constexpr std::array<std::array<int,2>,4> corners={{{0,0},{1,0},{1,1},{0,1}}};
            const unsigned turns=cubeQuarterTurns(boardSeed,i,autoOrient_);
            for(size_t n=0;n<4;++n) {
                const auto uv=cubeTexturePoint(corners[n][0],corners[n][1],1,turns);
                inner[n].u=x+uv[0]*(right-x);inner[n].v=y+uv[1]*(bottom-y);
            }
            quad(screen,inner,white,&texture);
        } else {
            quad(screen,inner,white);const auto center=cellCenter(i,side);
            font::drawText(screen,center.x-int(face.size())*3,center.y-5,face,{0,0,190});
        }
    }
    const uint64_t remaining=round.remaining(now);
    const int tx=master_?0:38,ty=master_?119:123,tw=master_?100:85,th=master_?175:160;
    if(playing && round.timed())scaled(screen,timer_.frames[timerFrame(remaining,duration)],tx,ty,tw,th,true);
    box(screen,tx,ty+th+6,105,30,{12,10,24});
    char time[32];const auto seconds=remaining/1000;
    std::snprintf(time,sizeof(time),"%llu:%02llu",(unsigned long long)(seconds/60),(unsigned long long)(seconds%60));
    font::drawText(screen,tx+6,ty+th+12,round.timed()?time:"NOT TIMED",white,round.timed()?2:1);
    std::vector<const RoundWord*> words;
    for(const auto& word:round.words())if(word.state!=WordState::Declined)words.push_back(&word);
    const size_t maximum=words.size()>visibleWords ? words.size()-visibleWords : 0,begin=maximum-std::min(scroll,maximum);
    for(size_t i=begin;i<std::min(words.size(),begin+visibleWords);++i) {
        const auto& word=*words[i];auto shown=word.entry.word;if(shown.size()>19)shown=shown.substr(0,16)+"...";
        const auto color=word.state==WordState::Pending ? Color{180,0,0} : black;
        if(handwriting_.loaded()) {
            shown=word.entry.word;
            if(handwriting_.measure(shown)>132) {
                while(!shown.empty() && handwriting_.measure(shown+"...")>132)shown.pop_back();
                shown+="...";
            }
            handwriting_.draw(screen,633,72+int(i-begin)*17,shown,color,132);
            handwriting_.draw(screen,772,72+int(i-begin)*17,std::to_string(word.entry.points),color,24);
        } else {
            font::drawText(screen,633,58+int(i-begin)*17,shown,color);
            font::drawText(screen,774,58+int(i-begin)*17,std::to_string(word.entry.points),color);
        }
    }
    if(words.size()>visibleWords)font::drawText(screen,633,494,"PGUP/PGDN",white);
    font::drawText(screen,633,530,"WORDS  "+std::to_string(words.size()),white,2);
    font::drawText(screen,633,568,"SCORE  "+std::to_string(round.score()),white,2);
}
} // namespace reluva::boggle

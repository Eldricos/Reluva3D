// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/layout.h"
#include "engines/boggle/classic/orientation.h"
#include "engines/boggle/classic/history.h"
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
Vertex point(const CameraProjection& camera,int u,int v,int d) {
    return camera.project(11*(2*u-d),11*(d-2*v),0,2000,5,4);
}
const CameraProjection& playingCamera(bool master) {
    static const CameraProjection original(classicLidView(false,43).camera);
    static const CameraProjection masters(classicLidView(true,43).camera);
    return master?masters:original;
}
std::array<Vertex,4> cell(const CameraProjection& camera,size_t index,size_t side,int margin=6) {
    const int x=int(index%side)*100,y=int(index/side)*100,d=int(side)*100;
    return {point(camera,x+margin,y+margin,d),point(camera,x+100-margin,y+margin,d),
            point(camera,x+100-margin,y+100-margin,d),point(camera,x+margin,y+100-margin,d)};
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
ClassicLidView classicLidView(bool master,size_t frame) {
    ClassicLidView view;
    view.camera=master?CameraParameters{-54,.25,{.8f,-.3f,-13.199999f},47,640,480}
                      :CameraParameters{-55.75,0,{1.025f,.4f,-15.050009f},44,640,480};
    view.visible=frame>=34 && frame<=51;
    if(!view.visible)return view;
    const int pose=frame==43?2:(frame==42 || frame==44)?1:0;
    if(master) {
        if(pose==1){view.camera.tilt=-45.75;view.camera.position[1]=-.4f;}
        if(pose==2){view.camera.tilt=-34;view.camera.position[1]=-.5f;}
    } else {
        if(pose==1){view.camera.tilt=-45.5;view.camera.position={1.025f,.25f,-14.750009f};}
        if(pose==2){view.camera.tilt=-36.5;view.camera.position={1.025f,.125f,-14.250009f};}
    }
    if(master) {
        const int top=frame==34?306:frame==35?262:frame==36?179:frame==42 || frame==45?110:
                      frame==43 || frame==44?70:frame==50?180:frame==51?251:127;
        const int bottom=frame==42 || frame==44?346:frame==43?355:338;
        view.clip={80,top,486,bottom};
    } else {
        const int top=frame==34?312:frame==35 || frame==51?279:frame==36 || frame==50?215:
                      frame==42 || frame==45?150:frame==43 || frame==44?125:175;
        const int bottom=frame==42 || frame==45?339:frame==43 || frame==44?350:328;
        view.clip={110,top,396,bottom};
    }
    return view;
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
    const std::string_view selected=options.cubeStyle==ClassicCubeStyle::Rounded?classicCubeMeshPaths[0]:
                                    options.cubeStyle==ClassicCubeStyle::Angular?classicCubeMeshPaths[1]:"";
    if(!selected.empty() && !fs::findFile(root,selected))
        return Error::fail("Selected Classic cube style is missing; re-import the game or choose Automatic cube style");
    for(const auto* path:classicCubeMeshPaths)if((selected.empty() || selected==path) && fs::findFile(root,path)) {
        CubeGeometry geometry;if(auto error=readClassicCubeGeometry(resources,path,geometry))return error;
        if(auto error=cubes_.prepare(geometry))return error;
        break;
    }
    active_=true;return Error::success();
}
size_t ClassicArtwork::timerFrame(uint64_t remaining,uint64_t duration) {
    if(!duration || duration>300000 || remaining>=duration)return 0;
    const uint64_t progress=(duration-remaining)*36/duration;
    return size_t(progress ? progress-1 : 0);
}
ClassicPoint ClassicArtwork::cellCenter(size_t index,size_t side) const {
    if((side!=4 && side!=5) || index>=side*side)return {-1,-1};
    if(cubes_.active())return cubes_.cellCenter(playingCamera(master_),index,side);
    const auto p=point(playingCamera(master_),int(index%side)*100+50,int(index/side)*100+50,int(side)*100);return {p.x,p.y};
}
std::optional<size_t> ClassicArtwork::cellAt(int x,int y,size_t side) const {
    if(side!=4 && side!=5)return {};
    if(cubes_.active())return cubes_.cellAt(playingCamera(master_),x,y,side);
    for(size_t i=0;i<side*side;++i) {
        const auto p=cell(playingCamera(master_),i,side);bool inside=true;
        for(size_t n=0;n<4;++n)if(edge(p[n],p[(n+1)%4],x,y)<0){inside=false;break;}
        if(inside)return i;
    }
    return {};
}
ClassicPoint ClassicArtwork::cellCenter(size_t cell,const Board& board,uint32_t seed) const {
    return cubes_.active()?cubes_.cellCenter(playingCamera(master_),board,cell,seed,autoOrient_):cellCenter(cell,board.side());
}
std::optional<size_t> ClassicArtwork::cellAt(int x,int y,const Board& board,uint32_t seed) const {
    return cubes_.active()?cubes_.cellAt(playingCamera(master_),board,x,y,seed,autoOrient_):cellAt(x,y,board.side());
}
void ClassicArtwork::drawCubes(Surface& screen,const Board& board,const CameraProjection& camera,
                               const image::Image& letters,const image::Image& qu,const std::vector<size_t>& selected,
                               uint32_t boardSeed,bool results) const {
    const size_t side=board.side();
    const Color white{245,245,255};
    if(cubes_.active()) {
        cubes_.draw(screen,board,camera,letters,qu,selected,boardSeed,autoOrient_);
        if(results)for(size_t n=0;n<selected.size();++n) {
            const auto p=cubes_.cellCenter(camera,board,selected[n],boardSeed,autoOrient_);const auto text=std::to_string(n+1);
            box(screen,p.x-16,p.y-19,int(text.size())*6+2,11,{20,48,60});font::drawText(screen,p.x-15,p.y-18,text,white);
        }
        return;
    }
    for(size_t i=0;i<side*side;++i) {
        auto outer=cell(camera,i,side),inner=cell(camera,i,side,10);
        auto front=outer;front[0]=outer[3];front[1]=outer[2];front[2].y+=7;front[3].y+=7;
        quad(screen,front,{90,96,115});
        const bool marked=std::find(selected.begin(),selected.end(),i)!=selected.end();
        quad(screen,outer,marked?Color{50,220,240}:Color{174,176,192});
        const auto& face=board.faces()[i];
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
            quad(screen,inner,white);const auto center=point(camera,int(i%side)*100+50,int(i/side)*100+50,int(side)*100);
            font::drawText(screen,center.x-int(face.size())*3,center.y-5,face,{0,0,190});
        }
        if(results && marked) {
            const auto at=std::find(selected.begin(),selected.end(),i);
            const auto number=std::to_string(size_t(at-selected.begin())+1);
            box(screen,inner[0].x+2,inner[0].y+2,int(number.size())*6+2,11,{20,48,60});
            font::drawText(screen,inner[0].x+3,inner[0].y+3,number,white);
        }
    }
}
void ClassicArtwork::drawTransition(Surface& screen,const Board& board,size_t frame,const image::Image& letters,
                                    const image::Image& qu,uint32_t boardSeed) const {
    if(!active_)return;
    const auto view=classicLidView(master_,frame);
    if(!view.visible)return;
    // Render only our native cube geometry. Transparent pixels preserve the
    // decoded AVI; clipping reproduces the original dice-over-video layer.
    Surface cubes;cubes.create(800,600,PixelFormat::RGBA32);
    std::fill(cubes.pixels.begin(),cubes.pixels.end(),0);
    drawCubes(cubes,board,CameraProjection(view.camera),letters,qu,{},boardSeed,false);
    const auto scaled=[](int value){return (value*5+3)/4;};
    for(int y=scaled(view.clip[1]);y<std::min(screen.h,scaled(view.clip[3]));++y)
        for(int x=scaled(view.clip[0]);x<std::min(screen.w,scaled(view.clip[2]));++x) {
            const auto* p=cubes.row(y)+x*4;
            if(p[3])std::copy_n(p,4,screen.row(y)+x*4);
        }
}
void ClassicArtwork::draw(Surface& screen,const ClassicRound& round,uint64_t now,uint64_t duration,
                          const image::Image& letters,const image::Image& qu,const std::vector<size_t>& selected,size_t scroll,uint32_t boardSeed,std::string_view player,const RoundResults* review,bool coverBoard) const {
    const bool boardVisible=round.phase()!=RoundPhase::Ready && !coverBoard;
    scaled(screen,boardVisible?open_:closed_,0,0,800,600);
    scaled(screen,names_,0,0,621,40);scaled(screen,words_,621,0,179,600);
    const Color white{245,245,255},black{12,12,20};
    font::drawText(screen,12,13,player.size()>15?std::string(player.substr(0,12))+"...":std::string(player),white);
    font::drawText(screen,113,13,std::to_string(review && review->ready()?review->score():round.score()),white);
    font::drawText(screen,180,13,master_?"CLASSIC MASTER":"CLASSIC ORIGINAL",white);
    font::drawText(screen,638,12,player,white,player.size()>12?1:2);
    if(boardVisible)drawCubes(screen,round.board(),playingCamera(master_),letters,qu,selected,boardSeed,round.phase()==RoundPhase::Results);
    const uint64_t remaining=round.remaining(now);
    const int tx=master_?0:38,ty=master_?119:123,tw=master_?100:85,th=master_?175:160;
    if(boardVisible && round.timed())scaled(screen,timer_.frames[timerFrame(remaining,duration)],tx,ty,tw,th,true);
    box(screen,tx,ty+th+6,105,30,{12,10,24});
    char time[32];const auto seconds=remaining/1000;
    std::snprintf(time,sizeof(time),"%llu:%02llu",(unsigned long long)(seconds/60),(unsigned long long)(seconds%60));
    font::drawText(screen,tx+6,ty+th+12,round.timed()?time:"NOT TIMED",white,round.timed()?2:1);
    std::vector<const RoundWord*> words;
    const bool all=review && review->ready() && review->allWords();
    if(review && review->ready())for(const auto& word:review->words())words.push_back(&word);
    else for(const auto& word:round.words())if(word.state!=WordState::Declined)words.push_back(&word);
    const auto view=wordHistoryView(words.size(),visibleWords,scroll);
    const size_t begin=view.begin;
    for(size_t i=begin;i<view.end;++i) {
        const auto& word=*words[i];
        if(round.phase()==RoundPhase::Results && !selected.empty() && word.entry.path==selected)
            box(screen,630,58+int(i-begin)*17,164,17,{225,226,173});
        auto shown=word.entry.word;if(shown.size()>19)shown=shown.substr(0,16)+"...";
        const auto color=word.state==WordState::Pending ? Color{180,0,0} : review && review->shared(word.entry.word)?Color{105,105,105} : all && review->missed(word.entry.word)?Color{25,65,135}:black;
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
    if(view.maximum) {
        const std::array<HistoryButton,2> buttons={historyOlder,historyNewer};
        const bool enabled[]={view.begin>0,view.end<words.size()};
        for(size_t n=0;n<buttons.size();++n) {
            const auto b=buttons[n];box(screen,b.x,b.y,b.w,b.h,{30,42,62});
            font::drawText(screen,b.x+7,b.y+6,n?"v":"^",enabled[n]?white:Color{88,99,113});
        }
        font::drawText(screen,633,479,std::to_string(view.begin+1)+"-"+std::to_string(view.end)+"/"+std::to_string(words.size()),black);
    }
    font::drawText(screen,633,530,std::string(all?"ALL    ":"WORDS  ")+std::to_string(words.size()),white,2);
    font::drawText(screen,633,568,"SCORE  "+std::to_string(review && review->ready()?review->score():round.score()),white,2);
}
} // namespace reluva::boggle

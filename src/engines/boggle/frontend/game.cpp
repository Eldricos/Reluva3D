// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/game.h"
#include "engines/boggle/frontend/media.h"
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/resources.h"
#include "common/fs.h"
#include "common/font.h"
#include <algorithm>

namespace reluva::boggle {
namespace {
struct Box {
    int x,y,w,h;
    bool contains(int px,int py) const { return px>=x && py>=y && px<x+w && py<y+h; }
};
constexpr Box classic{40,288,112,112}, original{48,155,252,65}, master{340,155,252,65},
              less{174,258,48,36}, more{418,258,48,36}, untimed{224,312,192,36},
              play{340,388,252,48}, back{48,388,252,48}, exit{462,420,140,36};
enum class Page { Selection, Version, Players };
constexpr Color white{245,245,255}, muted{170,181,205}, dark{17,15,35}, gold{224,189,99};
void fill(Surface& s,Box b,Color c) {
    for(int y=std::max(0,b.y);y<std::min(s.h,b.y+b.h);++y)
        for(int x=std::max(0,b.x);x<std::min(s.w,b.x+b.w);++x) {
            auto* p=s.row(y)+x*4; p[0]=c.r;p[1]=c.g;p[2]=c.b;p[3]=255;
        }
}
void text(Surface& s,int x,int y,std::string_view value,Color c=white,int scale=1) { font::drawText(s,x,y,value,c,scale); }
void button(Surface& s,Box box,std::string_view title,bool selected=false,bool enabled=true) {
    fill(s,box,selected?gold:Color{88,81,105});
    fill(s,{box.x+2,box.y+2,box.w-4,box.h-4},enabled?Color{41,34,64}:Color{23,23,31});
    const int scale=enabled && title.size()*12<=size_t(box.w-20) && box.h>=36 ? 2 : 1;
    text(s,box.x+10,box.y+box.h/2-4*scale,title,enabled?white:muted,scale);
}
std::string duration(uint64_t ms) {
    const auto seconds=ms/1000;
    return std::to_string(seconds/60)+":"+(seconds%60 ? "30" : "00");
}
void selection(Surface& s,const image::Image& background,const FrontendMedia& media,uint64_t elapsed) {
    fill(s,{0,0,640,480},dark);
    if(!background.rgba.empty()) image::blit(s,background,0,0);
    else { text(s,56,70,"BOGGLE",white,5); text(s,56,142,"CHOOSE A GAME",muted,2); }
    button(s,classic,"CLASSIC",true);
    if(media.button(s,0,classic.x+6,classic.y+4,elapsed)) {
        fill(s,{classic.x+2,classic.y+97,classic.w-4,13},dark);
        text(s,classic.x+10,classic.y+99,"CLASSIC");
    }
    const char* names[]={"BREAKAWAY","BATTLE","SPACE","IN YOUR FACE"};
    for(int i=0;i<4;++i) {
        const Box b{154+i*112,288,110,112};button(s,b,names[i],false,false);
        if(media.button(s,size_t(i+1),b.x+5,b.y+4,elapsed)) {
            fill(s,{b.x+2,b.y+85,b.w-4,25},dark);
            text(s,b.x+8,b.y+87,names[i],muted);text(s,b.x+8,b.y+99,"Unavailable",muted);
        } else text(s,b.x+10,369,"Unavailable",muted);
    }
    text(s,40,425,"Enter: Classic",white,2);button(s,exit,"LAUNCHER (L)");
}
void setup(Surface& s,const ClassicOptions& options) {
    fill(s,{0,0,640,480},dark);fill(s,{24,26,592,2},gold);
    text(s,48,48,"CLASSIC",white,3);
    text(s,48,91,"1 human player  |  U.S. English",muted);
    text(s,48,129,"CHOOSE YOUR BOARD",gold);
    button(s,original,"ORIGINAL (1)",options.variant==ClassicVariant::Original);
    button(s,master,"MASTER (2)",options.variant==ClassicVariant::Master);
    text(s,64,230,"4 x 4 / minimum 3 letters",muted);
    text(s,356,230,"5 x 5 / minimum 4 letters",muted);
    button(s,less,"-",false,options.duration!=0);button(s,more,"+",false,options.duration!=0);
    text(s,252,270,options.duration ? duration(options.duration) : "NOT TIMED",white,2);
    button(s,untimed,"NO TIMER (T)",options.duration==0);
    button(s,back,"MAIN MENU (ESC)");button(s,play,"PLAYERS (ENTER)",true);
    text(s,48,458,"1/2: board   Left/Right: duration   T: timer",muted);
}
void players(Surface& s,const ClassicOptions& options) {
    fill(s,{0,0,640,480},dark);fill(s,{24,26,592,2},gold);
    text(s,48,48,"PLAYERS",white,3);
    text(s,48,110,options.variant==ClassicVariant::Master ? "CLASSIC MASTER" : "CLASSIC ORIGINAL",gold,2);
    text(s,48,170,"PLAYER 1: HUMAN",white,2);
    text(s,48,214,"U.S. English  |  No computer players",muted);
    text(s,48,260,options.duration ? "Round length: "+duration(options.duration) : "Round length: Not Timed",white,2);
    button(s,back,"MAIN MENU (ESC)");button(s,play,"PLAY (ENTER)",true);
}

}
Error FrontendGame::run() {
    ClassicOptions options;
    if(auto err=readClassicOptions(target_.settings,options)) return err;
    if(!options.menu) { ClassicGame game(system_,target_); return game.run(); }
    options.music = chooseClassicMusic(options.music,uint32_t(system_.getMillis()));
    Resources resources; image::Image background;
    if(auto err=resources.open(target_.path)) return err;
    // Existing 39-file Classic imports remain playable without the new artwork.
    if(fs::findFile(target_.path,"Bitmaps/Select1.bmp")) {
        if(auto err=resources.image("Bitmaps/Select1.bmp",background)) return err;
        if(background.w!=640 || background.h!=480) return Error::fail("Unsupported game-selection bitmap dimensions");
    }
    FrontendMedia media;
    if(auto err=media.open(resources,target_.path,options.movies))return err;
    auto graphics=[&]() {
        system_.setTextInput(false); system_.setMouseCursorVisible(true);
        auto err=system_.initGraphics(640,480,PixelFormat::RGBA32);
        if(!err) system_.setWindowTitle("Reluva - Boggle");
        return err;
    };
    if(auto err=graphics()) return err;
    if(options.movies) {
        bool quit=false;
        if(auto err=FrontendMedia::intro(system_,resources,target_.path,quit))return err;
        if(quit)return Error::success();
    }
    uint64_t menuStarted=system_.getMillis();
    Surface screen;screen.create(640,480,PixelFormat::RGBA32);
    Page page=Page::Selection;
    uint64_t timedDuration=options.duration ? options.duration : 180000;
    for(;;) {
        bool launch=false, changed=false;
        Event event;
        while(system_.pollEvent(event)) {
            if(event.type==EventType::Quit || event.type==EventType::ReturnToLauncher) return Error::success();
            if(changed || launch) continue; // one visible navigation step per batch
            const bool isKey=event.type==EventType::KeyDown && !event.repeat;
            const bool click=event.type==EventType::MouseDown && event.button==MouseButton::Left;
            auto pressed=[&](Box box){return click && box.contains(event.mouseX,event.mouseY);};
            if(page==Page::Selection) {
                if((isKey && event.key=='l') || pressed(exit)) return Error::success();
                if((isKey && (event.key==key::Return || event.key==key::KpEnter || event.key=='c')) || pressed(classic)) {
                    page=Page::Version;changed=true;
                }
            } else if(page==Page::Version) {
                if((isKey && event.key==key::Escape) || pressed(back)) { page=Page::Selection;changed=true; }
                else if((isKey && event.key=='1') || pressed(original)) options.variant=ClassicVariant::Original;
                else if((isKey && event.key=='2') || pressed(master)) options.variant=ClassicVariant::Master;
                else if((isKey && event.key=='t') || pressed(untimed)) {
                    options.duration=options.duration ? 0 : timedDuration;
                } else if(options.duration && ((isKey && event.key==key::Left) || pressed(less))) {
                    options.duration=std::max<uint64_t>(60000,options.duration-30000);timedDuration=options.duration;
                } else if(options.duration && ((isKey && event.key==key::Right) || pressed(more))) {
                    options.duration=std::min<uint64_t>(300000,options.duration+30000);timedDuration=options.duration;
                } else if((isKey && (event.key==key::Return || event.key==key::KpEnter)) || pressed(play)) {
                    page=Page::Players;changed=true;
                }
            } else {
                if((isKey && event.key==key::Escape) || pressed(back)) { page=Page::Selection;changed=true; }
                else if((isKey && (event.key==key::Return || event.key==key::KpEnter)) || pressed(play)) launch=true;
            }
        }
        if(launch) {
            GameTarget selected=target_;
            selected.settings["classic_variant"]=options.variant==ClassicVariant::Master ? "master" : "original";
            selected.settings["classic_seconds"]=std::to_string(options.duration/1000);
            selected.settings["classic_music"]=options.music==musicNone ? "none" : std::to_string(options.music);
            ClassicGame game(system_,selected,true);
            if(auto err=game.run()) return err;
            if(!game.returnToMenu()) return Error::success();
            if(auto err=graphics()) return err;
            // Discard the rest of the round's input batch before showing menus.
            // Window close / launcher requests still take effect immediately.
            while(system_.pollEvent(event))
                if(event.type==EventType::Quit || event.type==EventType::ReturnToLauncher) return Error::success();
            page=Page::Selection;menuStarted=system_.getMillis();
        }
        if(page==Page::Version) setup(screen,options);
        else if(page==Page::Players) players(screen,options);
        else {const auto now=system_.getMillis();selection(screen,background,media,now>=menuStarted ? now-menuStarted : 0); }
        system_.updateScreen(screen);system_.delayMillis(16);
    }
}
} // namespace reluva::boggle

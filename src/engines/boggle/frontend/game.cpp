// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/game.h"
#include "engines/boggle/frontend/media.h"
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/classic/player.h"
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
constexpr size_t nameRows=8;
constexpr Box turnMode{48,352,544,28},backgroundPlay{48,224,544,28};
constexpr Box localPlayers{324,310,268,40};
constexpr Box playerChoice{48,162,544,40},nameList{48,204,544,228};
constexpr Box changeName{48,310,252,40},nameField{88,188,464,42},saveName{88,296,210,40},cancelName{318,296,210,40};
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
    text(s,48,91,"1-4 players  |  U.S. English",muted);
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
void players(Surface& s,const ClassicOptions& options,const PlayerProfile& profile,const std::array<std::string,4>& roster,bool backgroundAvailable) {
    fill(s,{0,0,640,480},dark);fill(s,{24,26,592,2},gold);
    text(s,48,48,"PLAYERS",white,3);
    text(s,48,110,options.variant==ClassicVariant::Master ? "CLASSIC MASTER" : "CLASSIC ORIGINAL",gold,2);
    button(s,playerChoice,"HUMAN NAME: "+profile.name()+"  v (P)");
    button(s,backgroundPlay,backgroundAvailable?(options.background?"COMPUTER BACKGROUND PLAY: ON (B)":"COMPUTER BACKGROUND PLAY: OFF (B)"):"BACKGROUND PLAY NEEDS A HUMAN AND COMPUTER",options.background && backgroundAvailable,backgroundAvailable);
    text(s,48,260,options.duration ? "Round length: "+duration(options.duration) : "Round length: Not Timed",white,2);
    button(s,changeName,"CHANGE NAME (N)");
    button(s,localPlayers,"LOCAL PLAYERS (M)");
    const auto count=std::count_if(roster.begin(),roster.end(),[](const auto& name){return !name.empty();});
    button(s,turnMode,std::string(options.turns==ClassicTurnMode::Chess?"CHESS TIMER":"TRADITIONAL")+" / "+std::to_string(count)+(count==1?" PLAYER":" PLAYERS")+" (C: CHANGE)");
    button(s,back,"MAIN MENU (ESC)");button(s,play,"PLAY (ENTER)",true);
}

void rosterScreen(Surface& s,const std::array<std::string,4>& roster) {
    fill(s,{32,96,576,354},gold);fill(s,{34,98,572,350},dark);
    text(s,60,112,"LOCAL PLAYERS",white,2);
    for(size_t i=0;i<4;++i)button(s,{72,142+int(i)*50,496,38},std::to_string(i+1)+": "+(roster[i].empty()?"NO PLAYER":roster[i])+"  v");
    text(s,60,350,"1-4 / click: choose a human or computer",muted);
    text(s,60,370,"Use Change Name outside this panel to add names.",muted);
    button(s,play,"DONE (ENTER)",true);
}
void nameDropdown(Surface& s,const std::vector<std::string>& names,size_t selected,size_t scroll,const std::string& error,bool computers) {
    fill(s,nameList,gold);fill(s,{50,206,540,224},dark);
    text(s,60,214,computers?"HUMAN NAMES / COMPUTER OPPONENTS":"PREVIOUS HUMAN NAMES",gold);
    for(size_t row=0;row<nameRows && scroll+row<names.size();++row) {
        const auto index=scroll+row;const int y=234+int(row)*20;
        if(index==selected)fill(s,{56,y,528,20},{43,83,112});
        text(s,64,y+5,names[index]);
    }
    text(s,60,400,"Up/Down: choose   Enter/click: select   Esc: cancel",muted);
    if(!error.empty())text(s,60,416,error.substr(0,84),{255,150,130});
    else text(s,60,416,std::to_string(selected+1)+" / "+std::to_string(names.size())+"   PgUp/PgDn: page",muted);
}
void nameEditor(Surface& s,const std::string& draft,bool selected,const std::string& error) {
    fill(s,{64,120,512,234},gold);fill(s,{66,122,508,230},dark);
    text(s,88,143,"PLAYER NAME",white,2);
    fill(s,nameField,gold);fill(s,{90,190,460,38},selected?Color{43,83,112}:Color{28,31,49});
    text(s,100,201,draft,white,2);
    text(s,88,244,"Up to 18 characters. Ctrl+A: replace name",muted);
    if(!error.empty())text(s,88,265,error.substr(0,76),{255,150,130});
    button(s,saveName,"SAVE (ENTER)",true);button(s,cancelName,"CANCEL (ESC)");
}
}
Error FrontendGame::run() {
    ClassicOptions options;
    if(auto err=readClassicOptions(target_.settings,options)) return err;
    if(!options.menu) { ClassicGame game(system_,target_); return game.run(); }
    PlayerProfile profile;
    if(auto error=profile.load(target_.savePath,target_.path))return error;
    struct TextCleanup {System& system;~TextCleanup(){system.setTextInput(false);}} cleanup{system_};
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
    bool editingName=false,selectAll=false,selectingName=false,rosterOpen=false,fromRoster=false;
    std::array<std::string,4> roster{profile.name(),"","",""};size_t rosterSlot=0;
    std::vector<std::string> choices;
    std::array<ComputerLevel,4> kinds; kinds.fill(ComputerLevel::Human);
    std::vector<ComputerLevel> choiceKinds;std::optional<size_t> emptyChoice;
    size_t nameSelection=0,nameScroll=0;
    std::string draftName,nameError;
    auto backgroundAvailable=[&]() {
        bool human=false,computer=false;
        for(size_t i=0;i<roster.size();++i)if(!roster[i].empty()) {
            if(kinds[i]==ComputerLevel::Human)human=true;else computer=true;
        }
        return human && computer;
    };
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
            if(selectingName) {
                const auto last=choices.size()-1;
                auto choose=[&]() {
                    const auto name=choices[nameSelection];
                    const auto kind=choiceKinds[nameSelection];
                    if(fromRoster && (rosterSlot>0 || kind!=ComputerLevel::Human)) {
                        roster[rosterSlot]=emptyChoice==nameSelection?"":name;
                        kinds[rosterSlot]=kind;
                        selectingName=false;changed=true;nameError.clear();
                    } else if(auto error=profile.rename(name))nameError=error.message;
                    else {roster[0]=profile.name();kinds[0]=ComputerLevel::Human;selectingName=false;changed=true;nameError.clear();}
                };
                if((isKey && event.key==key::Escape) || (click && !nameList.contains(event.mouseX,event.mouseY))) {
                    selectingName=false;changed=true;nameError.clear();
                } else if(isKey && (event.key==key::Return || event.key==key::KpEnter))choose();
                else if(click && event.mouseX>=56 && event.mouseX<584 && event.mouseY>=234 && event.mouseY<394) {
                    const size_t index=nameScroll+size_t((event.mouseY-234)/20);
                    if(index<choices.size()){nameSelection=index;choose();}
                } else {
                    int64_t delta=0;
                    if(isKey && event.key==key::Up)delta=-1;
                    if(isKey && event.key==key::Down)delta=1;
                    if(isKey && event.key==key::PageUp)delta=-int64_t(nameRows);
                    if(isKey && event.key==key::PageDown)delta=int64_t(nameRows);
                    if(isKey && event.key==key::Home)delta=-int64_t(last);
                    if(isKey && event.key==key::End)delta=int64_t(last);
                    if(event.type==EventType::MouseWheel)delta=-int64_t(event.wheelY)*3;
                    if(delta) {nameSelection=size_t(std::clamp<int64_t>(int64_t(nameSelection)+delta,0,int64_t(last)));nameError.clear();}
                    if(nameSelection<nameScroll)nameScroll=nameSelection;
                    if(nameSelection>=nameScroll+nameRows)nameScroll=nameSelection-nameRows+1;
                }
                continue;
            }
            if(editingName) {
                if((isKey && event.key==key::Escape) || pressed(cancelName)) {
                    editingName=false;system_.setTextInput(false);changed=true;
                } else if((isKey && (event.key==key::Return || event.key==key::KpEnter)) || pressed(saveName)) {
                    if(auto error=profile.rename(draftName))nameError=error.message;
                    else {roster[0]=profile.name();kinds[0]=ComputerLevel::Human;editingName=false;system_.setTextInput(false);changed=true;}
                } else if((isKey && event.key=='a' && (event.mods&keymod::Ctrl)) || pressed(nameField))selectAll=true;
                else if(event.type==EventType::KeyDown && (event.key==key::Backspace || event.key==key::Delete)) {
                    if(selectAll)draftName.clear();else if(!draftName.empty())draftName.pop_back();
                    selectAll=false;nameError.clear();
                } else if(event.type==EventType::TextInput) {
                    const auto end=std::find(std::begin(event.text),std::end(event.text),'\0');
                    const std::string text(event.text,end);
                    if(text.empty())continue;
                    const bool printable=std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32 && c<=126;});
                    if(!printable)nameError="Use printable English characters.";
                    else if((selectAll?0:draftName.size())+text.size()>18)nameError="Use at most 18 characters.";
                    else {if(selectAll)draftName.clear();draftName+=text;selectAll=false;nameError.clear();}
                }
                continue;
            }
            if(rosterOpen) {
                if((isKey && (event.key==key::Escape || event.key==key::Return || event.key==key::KpEnter)) || pressed(play)) {
                    rosterOpen=false;changed=true;
                } else {
                    std::optional<size_t> slot;
                    if(isKey && event.key>='1' && event.key<='4')slot=size_t(event.key-'1');
                    if(click)for(size_t i=0;i<4;++i)if(Box{72,142+int(i)*50,496,38}.contains(event.mouseX,event.mouseY))slot=i;
                    if(slot) {
                        rosterSlot=*slot;fromRoster=true;choices=profile.history();
                        choiceKinds.assign(choices.size(),ComputerLevel::Human);emptyChoice.reset();
                        for(auto kind:{ComputerLevel::Beginner,ComputerLevel::Intermediate,ComputerLevel::Advanced,ComputerLevel::MrBoggle}) {
                            choices.push_back(computerName(kind));choiceKinds.push_back(kind);
                        }
                        if(rosterSlot){emptyChoice=choices.size();choices.push_back("<NO PLAYER / EMPTY SLOT>");choiceKinds.push_back(ComputerLevel::Human);}
                        selectingName=true;nameSelection=0;
                        for(size_t i=0;i<choices.size();++i)
                            if(choices[i]==roster[rosterSlot] && choiceKinds[i]==kinds[rosterSlot])nameSelection=i;
                        if(roster[rosterSlot].empty() && emptyChoice)nameSelection=*emptyChoice;
                        nameScroll=nameSelection>=nameRows?nameSelection-nameRows+1:0;
                        nameError.clear();changed=true;
                    }
                }
                continue;
            }
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
                if((isKey && event.key=='c') || pressed(turnMode)) {options.turns=options.turns==ClassicTurnMode::Chess?ClassicTurnMode::Traditional:ClassicTurnMode::Chess;changed=true;}
                else if(((isKey && event.key=='b') || pressed(backgroundPlay)) && backgroundAvailable()) {options.background=!options.background;changed=true;}
                else if((isKey && event.key=='m') || pressed(localPlayers)) {rosterOpen=true;changed=true;}
                else if((isKey && event.key=='p') || pressed(playerChoice)) {
                    fromRoster=false;choices=profile.history();choiceKinds.assign(choices.size(),ComputerLevel::Human);emptyChoice.reset();selectingName=true;nameSelection=0;nameScroll=0;nameError.clear();changed=true;
                } else if((isKey && event.key=='n') || pressed(changeName)) {
                    draftName=profile.name();nameError.clear();selectAll=true;editingName=true;
                    system_.setTextInput(true);changed=true;
                } else if((isKey && event.key==key::Escape) || pressed(back)) { page=Page::Selection;changed=true; }
                else if((isKey && (event.key==key::Return || event.key==key::KpEnter)) || pressed(play)) launch=true;
            }
        }
        if(launch) {
            system_.setTextInput(false);
            GameTarget selected=target_;
            selected.settings["classic_background"]=options.background?"1":"0";
            selected.settings["classic_turns"]=options.turns==ClassicTurnMode::Chess?"chess":"traditional";
            selected.settings["classic_player_name"]=profile.name();
            selected.settings["classic_variant"]=options.variant==ClassicVariant::Master ? "master" : "original";
            selected.settings["classic_seconds"]=std::to_string(options.duration/1000);
            selected.settings["classic_music"]=options.music==musicNone ? "none" : std::to_string(options.music);
            std::vector<std::string> names;std::vector<ComputerLevel> computers;
            for(size_t i=0;i<roster.size();++i)if(!roster[i].empty()){names.push_back(roster[i]);computers.push_back(kinds[i]);}
            ClassicGame game(system_,selected,true,std::move(names),std::move(computers));
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
        else if(page==Page::Players) {
            players(screen,options,profile,roster,backgroundAvailable());
            if(rosterOpen)rosterScreen(screen,roster);
            if(selectingName)nameDropdown(screen,choices,nameSelection,nameScroll,nameError,fromRoster);
            if(editingName)nameEditor(screen,draftName,selectAll,nameError);
        }
        else {const auto now=system_.getMillis();selection(screen,background,media,now>=menuStarted ? now-menuStarted : 0); }
        system_.updateScreen(screen);system_.delayMillis(16);
    }
}
} // namespace reluva::boggle

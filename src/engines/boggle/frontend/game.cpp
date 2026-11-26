// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/game.h"
#include "engines/boggle/frontend/media.h"
#include "engines/boggle/frontend/name_dialog.h"
#include "engines/boggle/frontend/roster_dialog.h"
#include "engines/boggle/frontend/options_dialog.h"
#include "engines/boggle/frontend/dictionary_editor.h"
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/classic/player.h"
#include "engines/boggle/resources.h"
#include "common/fs.h"
#include "common/font.h"
#include "engines/boggle/frontend/appearance.h"
#include <algorithm>

namespace reluva::boggle {
namespace {
using Box=SelectionRect;
constexpr Box classic{40,288,112,112},
              play{340,388,252,48}, back{48,388,252,48}, exit{462,420,140,36};
constexpr Box optionsButton{344,100,248,36};
constexpr size_t nameRows=8;
constexpr Box turnMode{48,352,544,28},backgroundPlay{48,224,544,28},computerCursor{48,280,544,24};
constexpr Box localPlayers{324,310,268,40};
constexpr Box playerChoice{48,162,544,40},nameList{48,204,544,228};
constexpr Box changeName{48,310,252,40},nameField{88,188,464,42},saveName{88,296,210,40},cancelName{318,296,210,40};
enum class Page { Selection, Version, Players };
constexpr Color white{245,245,255}, muted{170,181,205}, dark{17,15,35}, gold{224,189,99};
constexpr const char* wordPolicyKeys[]={"deferred","ask","add","reject"};
constexpr const char* wordPolicyLabels[]={"REVIEW AT END","ASK NOW","AUTO-ADD","REJECT"};
void fill(Surface& s,Box b,Color c) {
    for(int y=std::max(0,b.y);y<std::min(s.h,b.y+b.h);++y)
        for(int x=std::max(0,b.x);x<std::min(s.w,b.x+b.w);++x) {
            auto* p=s.row(y)+x*4; p[0]=c.r;p[1]=c.g;p[2]=c.b;p[3]=255;
        }
}
void text(Surface& s,int x,int y,std::string_view value,Color c=white,int scale=1) { ui::text(s,x,y,value,c,scale); }
void button(Surface& s,Box box,std::string_view title,bool selected=false,bool enabled=true) {
    if(ui::active()){ui::button(s,box,title,selected,enabled);return;}
    fill(s,box,selected?gold:Color{88,81,105});
    fill(s,{box.x+2,box.y+2,box.w-4,box.h-4},enabled?Color{41,34,64}:Color{23,23,31});
    const int scale=enabled && title.size()*12<=size_t(box.w-20) && box.h>=36 ? 2 : 1;
    text(s,box.x+10,box.y+box.h/2-4*scale,title,enabled?white:muted,scale);
}
std::string duration(uint64_t ms) {
    const auto seconds=ms/1000;
    return std::to_string(seconds/60)+":"+(seconds%60 ? "30" : "00");
}
void selection(Surface& s,const image::Image& background,const FrontendMedia& media,uint64_t elapsed,const FrontendSelectionLayout& layout) {
    ui::Serif serif(ui::active());
    fill(s,{0,0,640,480},dark);
    if(!background.rgba.empty()) image::blit(s,background,0,0);
    else { text(s,56,70,"BOGGLE",white,5); text(s,56,142,"CHOOSE A GAME",muted,2); }
    if(layout.installed()) {
        const char* titles[]={"CLASSIC","BREAKAWAY","BATTLE","SPACE","IN YOUR FACE"};
        for(size_t i=0;i<layout.modes().size();++i){
            const auto b=layout.modes()[i];
            if(ui::active()){
                if(!media.buttonFit(s,i,b,elapsed))button(s,b,"",i==0,i==0);
                const int top=b.y+b.h+3;
                text(s,b.x+(b.w-ui::measure(titles[i]))/2,top,titles[i],i?muted:white);
                continue;
            }
            button(s,b,"",i==0,i==0);
            media.buttonFit(s,i,{b.x+2,b.y+2,b.w-4,b.h-4},elapsed);
            const int rows=i?2:1,top=b.y+b.h-rows*12-3;
            fill(s,{b.x+2,top,b.w-4,rows*12+1},dark);
            text(s,b.x+(b.w-int(std::char_traits<char>::length(titles[i]))*6)/2,top+2,titles[i],i?muted:white);
            if(i)text(s,b.x+(b.w-66)/2,top+14,"Unavailable",muted);
        }
        text(s,40,425,"Enter: Classic",white,2);button(s,exit,"LAUNCHER (L)");
        if(ui::active())text(s,40,459,"Other modes are unavailable.",muted);
        return;
    }
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
void setup(Surface& s,const ClassicOptions& options,const FrontendVersionLayout& layout) {
    fill(s,{0,0,640,480},dark);fill(s,{24,26,592,2},gold);
    text(s,48,48,"CLASSIC",white,3);
    if(layout.installed()) {
        const auto b=layout.bounds(),p=layout.prompt();
        if(!layout.drawBackground(s)){fill(s,b,gold);fill(s,{b.x+2,b.y+2,b.w-4,b.h-4},dark);}
        text(s,p.x,p.y,ui::active()?layout.caption():"CHOOSE A GAME",ui::active()?white:gold,2);
    } else {text(s,48,91,"1-4 players  |  U.S. English",muted);text(s,48,129,"CHOOSE YOUR BOARD",gold);}
    const auto original=layout.modes()[0],master=layout.modes()[1];
    if(ui::active()&&layout.installed()){
        ui::rollover(s,original,layout.label(0),options.variant==ClassicVariant::Original);
        ui::rollover(s,master,layout.label(1),options.variant==ClassicVariant::Master);
    }else{button(s,original,"ORIGINAL (1)",options.variant==ClassicVariant::Original);button(s,master,"MASTER (2)",options.variant==ClassicVariant::Master);}
    if(!ui::active() || !layout.installed()){text(s,layout.installed()?original.x:64,layout.installed()?original.y+original.h+3:230,"4 x 4 / minimum 3 letters",muted);
    text(s,layout.installed()?master.x:356,layout.installed()?master.y+master.h+3:230,"5 x 5 / minimum 4 letters",muted);}
    else text(s,48,105,options.variant==ClassicVariant::Master?"5 x 5 / minimum 4 letters":"4 x 4 / minimum 3 letters",muted);
    button(s,layout.less(),"-",false,options.duration!=0);button(s,layout.more(),"+",false,options.duration!=0);
    text(s,252,layout.installed()?356:270,options.duration ? duration(options.duration) : "NOT TIMED",white,2);
    button(s,layout.untimed(),"NO TIMER (T)",options.duration==0);
    button(s,back,"MAIN MENU (ESC)");button(s,play,"PLAYERS (ENTER)",true);
    text(s,48,458,"1/2: board   Left/Right: duration   T: timer",muted);
}
void players(Surface& s,const ClassicOptions& options,const PlayerProfile& profile,const std::array<std::string,4>& roster,bool backgroundAvailable,bool cursorAvailable,bool optionsAvailable) {
    fill(s,{0,0,640,480},dark);fill(s,{24,26,592,2},gold);
    text(s,48,48,"PLAYERS",white,3);
    text(s,48,110,options.variant==ClassicVariant::Master ? "CLASSIC MASTER" : "CLASSIC ORIGINAL",gold,2);
    if(optionsAvailable)button(s,optionsButton,"OPTIONS (O)");
    button(s,playerChoice,"HUMAN NAME: "+profile.name()+"  v (P)");
    button(s,backgroundPlay,backgroundAvailable?(options.background?"COMPUTER BACKGROUND PLAY: ON (B)":"COMPUTER BACKGROUND PLAY: OFF (B)"):"BACKGROUND PLAY NEEDS A HUMAN AND COMPUTER",options.background && backgroundAvailable,backgroundAvailable);
    text(s,48,260,options.duration ? "Round length: "+duration(options.duration) : "Round length: Not Timed",white,2);
    button(s,computerCursor,cursorAvailable?(options.cursor?"COMPUTER CURSOR: ON (V)":"COMPUTER CURSOR: OFF (V)"):"CURSOR MOVEMENT NEEDS VISIBLE COMPUTER TURNS",options.cursor && cursorAvailable,cursorAvailable);
    button(s,changeName,"CHANGE NAME (N)");
    button(s,localPlayers,"LOCAL PLAYERS (M)");
    const auto count=std::count_if(roster.begin(),roster.end(),[](const auto& name){return !name.empty();});
    button(s,turnMode,std::string(options.turns==ClassicTurnMode::Chess?"CHESS TIMER":"TRADITIONAL")+" / "+std::to_string(count)+(count==1?" PLAYER":" PLAYERS")+" (C: CHANGE)");
    button(s,back,"MAIN MENU (ESC)");button(s,play,"PLAY (ENTER)",true);
    button(s,{48,444,252,28},"DICTIONARY (D)");
    button(s,{316,444,276,28},std::string("WORDS: ")+wordPolicyLabels[int(options.wordPolicy)]+" (W)");
}

void rosterScreen(Surface& s,const std::array<std::string,4>& roster,const std::array<ComputerLevel,4>& kinds,const FrontendRosterDialog& dialog,const std::string& error) {
    if(dialog.installed()){dialog.draw(s,roster,kinds,error);return;}
    fill(s,{32,96,576,354},gold);fill(s,{34,98,572,350},dark);
    text(s,60,112,"LOCAL PLAYERS",white,2);
    for(size_t i=0;i<4;++i)button(s,{72,142+int(i)*50,496,38},std::to_string(i+1)+": "+(roster[i].empty()?"NO PLAYER":roster[i])+"  v");
    text(s,60,350,"1-4 / click: choose a human or computer",muted);
    text(s,60,370,error.empty()?"Use Change Name outside this panel to add names.":error.substr(0,84),error.empty()?muted:Color{255,150,130});
    button(s,play,"DONE (ENTER)",true);button(s,back,"CANCEL (ESC)");
}
void nameDropdown(Surface& s,const std::vector<std::string>& names,size_t selected,size_t scroll,const std::string& error,bool computers) {
    fill(s,nameList,gold);fill(s,{50,206,540,224},dark);
    if(ui::active()){ui::panel(s,nameList);ui::fill(s,{nameList.x+2,nameList.y+2,nameList.w-4,nameList.h-4},{255,255,255});}
    ui::Paper paper(ui::active());
    text(s,60,214,computers?"HUMAN NAMES / COMPUTER OPPONENTS":"PREVIOUS HUMAN NAMES",gold);
    for(size_t row=0;row<nameRows && scroll+row<names.size();++row) {
        const auto index=scroll+row;const int y=234+int(row)*20;
        if(index==selected)fill(s,{56,y,528,20},{43,83,112});
        {ui::Paper rowPaper(ui::active()&&index!=selected);text(s,64,y+(ui::active()?2:5),names[index]);}
    }
    text(s,60,400,"Up/Down: choose   Enter/click: select   Esc: cancel",muted);
    if(!error.empty())text(s,60,416,error.substr(0,84),{255,150,130});
    else text(s,60,416,std::to_string(selected+1)+" / "+std::to_string(names.size())+"   PgUp/PgDn: page",muted);
}
void nameEditor(Surface& s,const NameEdit& edit,const std::string& error,const FrontendNameDialog& dialog) {
    if(dialog.installed()){dialog.draw(s,edit,error);return;}
    fill(s,{64,120,512,234},gold);fill(s,{66,122,508,230},dark);
    text(s,88,143,"PLAYER NAME",white,2);
    edit.draw(s,nameField,12);
    text(s,88,244,"18 chars. Arrows/Home/End: move. Shift: select.",muted);
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
    ui::Appearance appearance;ui::Scope appearanceScope(appearance);
    Resources resources; image::Image background;
    if(auto err=resources.open(target_.path)) return err;
    // Existing 39-file Classic imports remain playable without the new artwork.
    if(fs::findFile(target_.path,"Bitmaps/Select1.bmp")) {
        if(auto err=resources.image("Bitmaps/Select1.bmp",background)) return err;
        if(background.w!=640 || background.h!=480) return Error::fail("Unsupported game-selection bitmap dimensions");
    }
    FrontendSelectionLayout selectionLayout;
    FrontendVersionLayout versionLayout;
    FrontendNameDialog nameDialog;
    FrontendRosterDialog rosterDialog;
    FrontendOptionsDialog optionsDialog;
    if(options.artwork && fs::findFile(target_.path,"Language/English.dll")) {
        Language language;if(auto err=resources.language("Language/English.dll",language))return err;
        appearance.enable(findPeResource(language.resources,5,10813,0x0409)&&findPeResource(language.resources,5,10000,0x0409)&&findPeResource(language.resources,5,16007,0x0409));
        if(auto err=selectionLayout.read(language.resources))return err;
        if(auto err=versionLayout.read(language.resources))return err;
        if(auto err=versionLayout.readBackground(resources,target_.path))return err;
        if(auto err=nameDialog.read(language.resources))return err;
        if(auto err=rosterDialog.read(language.resources))return err;
        if(auto err=optionsDialog.read(language.resources))return err;
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
    MenuAudio menuAudio(system_);if(auto error=menuAudio.open(resources,target_.path,options.sound))return error;
    uint64_t menuStarted=system_.getMillis();
    media.activate(menuStarted);menuAudio.enter();bool menuActive=true;int menuPointer=-1;
    Surface screen;screen.create(640,480,PixelFormat::RGBA32);
    Page page=Page::Selection;
    bool editingName=false,selectingName=false,rosterOpen=false,fromRoster=false;
    std::array<std::string,4> roster{profile.name(),"","",""};size_t rosterSlot=0;
    std::vector<std::string> choices;
    std::array<ComputerLevel,4> kinds; kinds.fill(ComputerLevel::Human);
    auto savedRoster=roster;auto savedKinds=kinds;
    std::vector<ComputerLevel> choiceKinds;std::optional<size_t> emptyChoice;
    size_t nameSelection=0,nameScroll=0;
    NameEdit nameEdit;std::string nameError;bool nameDragging=false;
    DictionaryEditor dictionaryEditor;bool dictionaryOpen=false;
    auto backgroundAvailable=[&]() {
        bool human=false,computer=false;
        for(size_t i=0;i<roster.size();++i)if(!roster[i].empty()) {
            if(kinds[i]==ComputerLevel::Human)human=true;else computer=true;
        }
        return human && computer;
    };
    auto cursorAvailable=[&]() {
        for(size_t i=0;i<roster.size();++i)if(!roster[i].empty() && kinds[i]!=ComputerLevel::Human)
            return !(options.background && backgroundAvailable());
        return false;
    };
    uint64_t timedDuration=options.duration ? options.duration : 180000;
    bool optionsOpen=false,optionsDragging=false,menuFocused=true;ClassicOptions savedOptions=options;uint64_t savedTimedDuration=timedDuration;
    for(;;) {
        bool launch=false, changed=false;
        Event event;
        while(system_.pollEvent(event)) {
            appearance.event(event);
            if(event.type==EventType::MouseMove || event.type==EventType::MouseDown){
                menuPointer=-1;for(size_t i=0;i<5;++i)if(selectionLayout.modes()[i].contains(event.mouseX,event.mouseY))menuPointer=int(i);
            }
            if(event.type==EventType::MouseLeave || event.type==EventType::FocusLost)menuPointer=-1;
            if(event.type==EventType::Quit || event.type==EventType::ReturnToLauncher) return Error::success();
            // Releases and focus changes must end a drag even after another
            // action consumed this event batch. Motion/release samples may be
            // coalesced with the initial press in the same polling batch.
            if(event.type==EventType::FocusLost || event.type==EventType::FocusGained) {
                menuFocused=event.type==EventType::FocusGained;optionsDragging=false;nameDragging=false;
                if(optionsOpen || editingName || dictionaryOpen)changed=true;
                continue;
            }
            if(optionsOpen && event.type==EventType::MouseLeave){optionsDragging=false;continue;}
            if(editingName && event.type==EventType::MouseLeave){nameDragging=false;continue;}
            if(editingName && !menuFocused)continue;
            if(editingName && nameDragging && (event.type==EventType::MouseMove ||
               (event.type==EventType::MouseUp && event.button==MouseButton::Left))) {
                nameEdit.pointer(event.mouseX,nameDialog.field(),nameDialog.installed()?8:12,true);
                if(event.type==EventType::MouseUp)nameDragging=false;
                continue;
            }
            if(optionsOpen && !menuFocused)continue;
            if(optionsOpen && optionsDragging && (event.type==EventType::MouseMove ||
               (event.type==EventType::MouseUp && event.button==MouseButton::Left))) {
                if(options.duration)timedDuration=options.duration=optionsDialog.durationAt(event.mouseX);
                if(event.type==EventType::MouseUp)optionsDragging=false;
                continue;
            }
            if(changed || launch) continue; // one visible navigation step per batch
            if(dictionaryOpen){
                if(menuFocused && dictionaryEditor.event(event)){dictionaryOpen=false;system_.setTextInput(false);changed=true;}
                continue;
            }
            const bool isKey=event.type==EventType::KeyDown && !event.repeat;
            const bool click=event.type==EventType::MouseDown && event.button==MouseButton::Left;
            auto pressed=[&](Box box){return click && box.contains(event.mouseX,event.mouseY);};
            if(optionsOpen) {
                if(click)optionsDragging=false;
                bool handled=true;
                if((isKey && event.key==key::Escape) || pressed(optionsDialog.cancel())) {options=savedOptions;timedDuration=savedTimedDuration;optionsOpen=false;optionsDragging=false;}
                else if((isKey && (event.key==key::Return || event.key==key::KpEnter)) || pressed(optionsDialog.done())){optionsOpen=false;optionsDragging=false;}
                else if(((isKey && event.key=='v') || pressed(optionsDialog.control(FrontendOptionsDialog::Cursor))) && cursorAvailable())options.cursor=!options.cursor;
                else if(((isKey && event.key=='b') || pressed(optionsDialog.control(FrontendOptionsDialog::Background))) && backgroundAvailable())options.background=!options.background;
                else if((isKey && event.key=='a') || pressed(optionsDialog.control(FrontendOptionsDialog::Orient)))options.autoOrient=!options.autoOrient;
                else if((isKey && event.key=='c') || pressed(optionsDialog.control(FrontendOptionsDialog::Chess)))options.turns=ClassicTurnMode::Chess;
                else if((isKey && event.key=='r') || pressed(optionsDialog.control(FrontendOptionsDialog::Traditional)))options.turns=ClassicTurnMode::Traditional;
                else if((isKey && event.key=='t') || pressed(optionsDialog.control(FrontendOptionsDialog::Untimed))){optionsDragging=false;options.duration=options.duration?0:timedDuration;}
                else if(options.duration && pressed(optionsDialog.control(FrontendOptionsDialog::Slider))){optionsDragging=true;timedDuration=options.duration=optionsDialog.durationAt(event.mouseX);}
                else if(options.duration && isKey && event.key==key::Left)timedDuration=options.duration=std::max<uint64_t>(60000,options.duration-30000);
                else if(options.duration && isKey && event.key==key::Right)timedDuration=options.duration=std::min<uint64_t>(300000,options.duration+30000);
                else handled=false;
                changed=handled;continue;
            }
            if(selectingName) {
                const auto last=choices.size()-1;
                auto choose=[&]() {
                    const auto name=choices[nameSelection];
                    const auto kind=choiceKinds[nameSelection];
                    if(fromRoster) {
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
                if(click || event.type==EventType::KeyDown || event.type==EventType::TextInput)nameDragging=false;
                if((isKey && event.key==key::Escape) || pressed(nameDialog.cancel())) {
                    editingName=false;system_.setTextInput(false);changed=true;
                } else if((isKey && (event.key==key::Return || event.key==key::KpEnter)) || pressed(nameDialog.save())) {
                    if(auto error=profile.rename(nameEdit.text()))nameError=error.message;
                    else {roster[0]=profile.name();kinds[0]=ComputerLevel::Human;editingName=false;system_.setTextInput(false);changed=true;}
                } else if(isKey && event.key=='a' && (event.mods&keymod::Ctrl)) {nameEdit.selectAll();nameError.clear();}
                else if(pressed(nameDialog.field())) {
                    nameEdit.pointer(event.mouseX,nameDialog.field(),nameDialog.installed()?8:12,event.mods&keymod::Shift);
                    nameDragging=true;nameError.clear();
                }
                else if(event.type==EventType::KeyDown) {
                    const bool extend=event.mods&keymod::Shift;
                    if(event.key==key::Left)nameEdit.move(-1,extend);
                    else if(event.key==key::Right)nameEdit.move(1,extend);
                    else if(event.key==key::Home)nameEdit.home(extend);
                    else if(event.key==key::End)nameEdit.end(extend);
                    else if(event.key==key::Backspace || event.key==key::Delete)nameEdit.erase(event.key==key::Delete);
                    else continue;
                    nameError.clear();
                } else if(event.type==EventType::TextInput) {
                    const auto end=std::find(std::begin(event.text),std::end(event.text),'\0');
                    const auto error=nameEdit.insert(std::string_view(event.text,size_t(end-std::begin(event.text))));
                    nameError=error.message;
                }
                continue;
            }
            if(rosterOpen) {
                if((isKey && event.key==key::Escape) || pressed(rosterDialog.cancel())) {
                    roster=savedRoster;kinds=savedKinds;rosterOpen=false;nameError.clear();changed=true;
                } else if((isKey && (event.key==key::Return || event.key==key::KpEnter)) || pressed(rosterDialog.done())) {
                    Error error;
                    if(kinds[0]==ComputerLevel::Human)error=profile.rename(roster[0]);
                    if(error)nameError=error.message;
                    else {rosterOpen=false;nameError.clear();}
                    changed=true;
                } else {
                    std::optional<size_t> slot;
                    if(isKey && event.key>='1' && event.key<='4')slot=size_t(event.key-'1');
                    if(click)for(size_t i=0;i<4;++i)if(rosterDialog.fields()[i].contains(event.mouseX,event.mouseY))slot=i;
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
                if((isKey && (event.key==key::Return || event.key==key::KpEnter || event.key=='c')) || pressed(selectionLayout.modes()[0])) {
                    page=Page::Version;changed=true;
                }
            } else if(page==Page::Version) {
                if((isKey && event.key==key::Escape) || pressed(back)) { page=Page::Selection;changed=true; }
                else if((isKey && event.key=='1') || pressed(versionLayout.modes()[0])) options.variant=ClassicVariant::Original;
                else if((isKey && event.key=='2') || pressed(versionLayout.modes()[1])) options.variant=ClassicVariant::Master;
                else if((isKey && event.key=='t') || pressed(versionLayout.untimed())) {
                    options.duration=options.duration ? 0 : timedDuration;
                } else if(options.duration && ((isKey && event.key==key::Left) || pressed(versionLayout.less()))) {
                    options.duration=std::max<uint64_t>(60000,options.duration-30000);timedDuration=options.duration;
                } else if(options.duration && ((isKey && event.key==key::Right) || pressed(versionLayout.more()))) {
                    options.duration=std::min<uint64_t>(300000,options.duration+30000);timedDuration=options.duration;
                } else if((isKey && (event.key==key::Return || event.key==key::KpEnter)) || pressed(play)) {
                    page=Page::Players;changed=true;
                }
            } else {
                if((isKey && event.key=='d') || pressed({48,444,252,28})) {
                    if(auto error=dictionaryEditor.open(target_.savePath,target_.path))return error;
                    dictionaryOpen=true;system_.setTextInput(true);changed=true;
                }
                else if((isKey && event.key=='w') || pressed({316,444,276,28})){options.wordPolicy=WordPolicy((int(options.wordPolicy)+1)%4);changed=true;}
                else if(optionsDialog.installed() && ((isKey && event.key=='o') || pressed(optionsButton))) {savedOptions=options;savedTimedDuration=timedDuration;optionsOpen=true;optionsDragging=false;changed=true;}
                else if((isKey && event.key=='c') || pressed(turnMode)) {options.turns=options.turns==ClassicTurnMode::Chess?ClassicTurnMode::Traditional:ClassicTurnMode::Chess;changed=true;}
                else if(((isKey && event.key=='b') || pressed(backgroundPlay)) && backgroundAvailable()) {options.background=!options.background;changed=true;}
                else if(((isKey && event.key=='v') || pressed(computerCursor)) && cursorAvailable()) {options.cursor=!options.cursor;changed=true;}
                else if((isKey && event.key=='m') || pressed(localPlayers)) {
                    if(auto error=rosterDialog.artwork(resources,target_.path,options.variant==ClassicVariant::Master))return error;
                    savedRoster=roster;savedKinds=kinds;rosterOpen=true;nameError.clear();changed=true;
                }
                else if((isKey && event.key=='p') || pressed(playerChoice)) {
                    fromRoster=false;choices=profile.history();choiceKinds.assign(choices.size(),ComputerLevel::Human);emptyChoice.reset();selectingName=true;nameSelection=0;nameScroll=0;nameError.clear();changed=true;
                } else if((isKey && event.key=='n') || pressed(changeName)) {
                    if(auto error=nameEdit.begin(profile.name()))return error;
                    nameError.clear();nameDragging=false;editingName=true;
                    system_.setTextInput(true);changed=true;
                } else if((isKey && event.key==key::Escape) || pressed(back)) { page=Page::Selection;changed=true; }
                else if((isKey && (event.key==key::Return || event.key==key::KpEnter)) || pressed(play)) launch=true;
            }
        }
        if(launch) {
            menuAudio.stop();menuActive=false;
            appearance.reset();
            system_.setTextInput(false);
            GameTarget selected=target_;
            selected.settings["classic_word_policy"]=wordPolicyKeys[int(options.wordPolicy)];
            selected.settings["classic_auto_orient"]=options.autoOrient?"1":"0";
            selected.settings["classic_cursor"]=options.cursor?"1":"0";
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
        const bool onMenu=page==Page::Selection;
        if(onMenu!=menuActive){
            if(onMenu){media.activate(system_.getMillis());menuAudio.enter();menuPointer=-1;}
            else menuAudio.stop();
            menuActive=onMenu;
        }
        if(onMenu){media.motion().update(system_.getMillis(),menuPointer,menuFocused);menuAudio.update(media.motion());}
        if(page==Page::Version) setup(screen,options,versionLayout);
        else if(page==Page::Players) {
            players(screen,options,profile,roster,backgroundAvailable(),cursorAvailable(),optionsDialog.installed());
            if(optionsOpen)optionsDialog.draw(screen,options,backgroundAvailable(),cursorAvailable());
            if(rosterOpen)rosterScreen(screen,roster,kinds,rosterDialog,nameError);
            if(selectingName)nameDropdown(screen,choices,nameSelection,nameScroll,nameError,fromRoster);
            if(editingName)nameEditor(screen,nameEdit,nameError,nameDialog);
            if(dictionaryOpen)dictionaryEditor.draw(screen);
        }
        else {const auto now=system_.getMillis();selection(screen,background,media,now>=menuStarted ? now-menuStarted : 0,selectionLayout); }
        system_.updateScreen(screen);system_.delayMillis(16);
    }
}
} // namespace reluva::boggle

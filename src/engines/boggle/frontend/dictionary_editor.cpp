// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/dictionary_editor.h"
#include "engines/boggle/frontend/layout.h"
#include "engines/boggle/classic/round.h"
#include "common/font.h"
#include "engines/boggle/frontend/appearance.h"
#include <algorithm>
namespace reluva::boggle {
namespace {
constexpr SelectionRect add{40,382,126,36},remove{180,382,126,36},done{320,382,126,36},cancel{460,382,140,36};
constexpr SelectionRect clear{480,430,120,26},yesClear{160,252,140,36},noClear{340,252,140,36};
constexpr size_t visible=13;
}
Error DictionaryEditor::open(const std::filesystem::path& save,const std::filesystem::path& game){
    *this={};if(auto e=user_.load(save,game))return e;draft_=user_.words();refresh();return Error::success();
}
void DictionaryEditor::refresh(){
    rows_.assign(draft_.begin(),draft_.end());selected_=rows_.empty()?0:std::min(selected_,rows_.size()-1);
    if(selected_<scroll_)scroll_=selected_;
    if(selected_>=scroll_+visible)scroll_=selected_-visible+1;
    if(rows_.empty())scroll_=0;
}
bool DictionaryEditor::event(const Event& e){
    const bool key=e.type==EventType::KeyDown,once=key&&!e.repeat,click=e.type==EventType::MouseDown && e.button==MouseButton::Left;
    auto hit=[&](SelectionRect b){return click&&b.contains(e.mouseX,e.mouseY);};
    if(clearPrompt_){
        if((once&&(e.key=='y'||e.key==key::Return||e.key==key::KpEnter))||hit(yesClear)){
            draft_.clear();input_.clear();refresh();error_.clear();clearPrompt_=false;replace_=true;
        }else if((once&&(e.key=='n'||e.key==key::Escape))||hit(noClear))clearPrompt_=false;
        return false;
    }
    if((once&&e.key==key::F1+3)||hit(clear)){if(!draft_.empty())clearPrompt_=true;return false;}
    if((once&&e.key==key::Escape)||hit(cancel))return true;
    if((once&&e.key==key::F1+1)||hit(done)){
        const auto result=user_.replace({draft_.begin(),draft_.end()});error_=result.message;return !result;
    }
    if((once&&(e.key==key::Return||e.key==key::KpEnter))||hit(add)){
        std::string word;if(!englishWord(input_,word)||word.size()<3){error_="Use 3 to 64 English letters.";return false;}
        if(draft_.size()>=32768 && !draft_.contains(word)){error_="User dictionary word limit reached.";return false;}
        draft_.insert(word);refresh();selected_=size_t(std::lower_bound(rows_.begin(),rows_.end(),word)-rows_.begin());refresh();replace_=true;error_.clear();return false;
    }
    if((once&&e.key==key::Delete)||hit(remove)){
        if(!rows_.empty())draft_.erase(rows_[selected_]);
        refresh();error_.clear();return false;
    }
    if(once&&e.key=='a'&&(e.mods&keymod::Ctrl)){replace_=true;return false;}
    if(key&&e.key==key::Backspace){if(replace_)input_.clear();else if(!input_.empty())input_.pop_back();replace_=false;error_.clear();return false;}
    if(e.type==EventType::TextInput){
        const auto end=std::find(std::begin(e.text),std::end(e.text),'\0');const std::string text(e.text,end);if(text.empty())return false;
        std::string normalized;if(!englishWord(text,normalized)||(replace_?0:input_.size())+normalized.size()>64){error_="Use up to 64 English letters.";return false;}
        if(replace_)input_.clear();
        input_+=normalized;replace_=false;error_.clear();
        const auto found=std::lower_bound(rows_.begin(),rows_.end(),input_);if(found!=rows_.end()){selected_=size_t(found-rows_.begin());refresh();}return false;
    }
    int delta=0;if(key&&e.key==key::Up)delta=-1;if(key&&e.key==key::Down)delta=1;
    if(key&&e.key==key::PageUp)delta=-int(visible);
    if(key&&e.key==key::PageDown)delta=int(visible);
    if(e.type==EventType::MouseWheel)delta=int(std::clamp<int64_t>(-int64_t(e.wheelY)*3,-32768,32768));
    if(key&&e.key==key::Home)delta=-32768;
    if(key&&e.key==key::End)delta=32768;
    if(delta&&!rows_.empty()){selected_=size_t(std::clamp<int64_t>(int64_t(selected_)+delta,0,int64_t(rows_.size()-1)));input_=rows_[selected_];replace_=true;refresh();}
    if(click&&SelectionRect{40,100,560,int(visible)*16}.contains(e.mouseX,e.mouseY)){
        const size_t row=scroll_+size_t((e.mouseY-100)/16);if(row<rows_.size()){selected_=row;input_=rows_[row];replace_=true;}
    }
    return false;
}
void DictionaryEditor::draw(Surface& s) const {
    if(s.format!=PixelFormat::RGBA32||s.w!=640||s.h!=480||s.pitch<2560||size_t(s.pitch)>s.pixels.size()/480)return;
    auto box=[&](SelectionRect b,Color c){for(int y=b.y;y<b.y+b.h;++y)for(int x=b.x;x<b.x+b.w;++x){auto*p=s.row(y)+x*4;p[0]=c.r;p[1]=c.g;p[2]=c.b;p[3]=255;}};
    ui::Paper paper(ui::active());
    const Color white{245,245,255},muted{170,181,205};box({0,0,640,480},ui::active()?Color{192,192,192}:Color{17,15,35});
    if(ui::active())box({40,100,560,int(visible)*16},{255,255,255});
    ui::text(s,40,30,"PERSONAL DICTIONARY",white,2);ui::text(s,40,64,"Edit your added words. The main game dictionary stays unchanged.",muted);
    ui::text(s,40,82,std::to_string(rows_.size())+(rows_.size()==1?" word / ":" words / ")+"Up-Down, PgUp-PgDn, wheel or type a prefix",muted);
    for(size_t i=0;i<visible&&scroll_+i<rows_.size();++i){if(scroll_+i==selected_)box({40,100+int(i)*16,560,16},{43,83,112});{ui::Paper rowPaper(ui::active() && scroll_+i!=selected_);ui::text(s,48,(ui::active()?102:104)+int(i)*16,rows_[scroll_+i],white);}}
    if(rows_.empty())ui::text(s,48,108,"No personal words yet.",muted);
    if(ui::active())ui::field(s,{40,326,560,36},input_,0,replace_?input_.size():0,input_.size(),6);
    else {box({40,326,560,36},{43,83,112});ui::text(s,48,338,input_,white);}
    const SelectionRect buttons[]={add,remove,done,cancel};const char* labels[]={"ADD (ENTER)","REMOVE (DEL)","DONE (F2)","CANCEL (ESC)"};
    for(size_t i=0;i<4;++i){if(ui::active()){ui::button(s,buttons[i],labels[i]);continue;}box(buttons[i],{48,64,88});ui::text(s,buttons[i].x+8,buttons[i].y+14,labels[i],white);}
    ui::text(s,40,438,"Done saves. Cancel discards all changes.",muted);
    if(ui::active())ui::button(s,clear,"Clear (F4)");else {box(clear,{48,64,88});ui::text(s,clear.x+8,clear.y+9,"CLEAR (F4)",white);}
    if(clearPrompt_){
        box({120,180,400,130},{35,45,65});ui::panel(s,{120,180,400,130});ui::text(s,144,206,"Clear every personal word?",white,2);
        ui::text(s,144,232,"The draft is saved only when you choose Done.",muted);
        if(ui::active()){ui::button(s,yesClear,"Yes (Enter / Y)");ui::button(s,noClear,"No (Esc / N)");}
        else {box(yesClear,{48,64,88});box(noClear,{48,64,88});
        ui::text(s,172,266,"YES (ENTER / Y)",white);ui::text(s,352,266,"NO (ESC / N)",white);}
    }
    if(!error_.empty())ui::text(s,40,460,error_.substr(0,90),{255,150,130});
}
}

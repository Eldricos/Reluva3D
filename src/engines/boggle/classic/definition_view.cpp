// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/definition_view.h"
#include "common/font.h"
#include <algorithm>
namespace reluva::boggle {
namespace {constexpr size_t visible=32,columns=112;}
void DefinitionView::open(std::string word,std::vector<DefinitionEntry> entries,std::string error){
    word_=word.substr(0,64);entries_=std::move(entries);error_=std::move(error);selected_=scroll_=0;
    if(entries_.size()>64 || std::any_of(entries_.begin(),entries_.end(),[](const auto& e){return e.text.size()>1024*1024;})){entries_.clear();error_="Definition exceeds display limits.";}
    wrap();
}
void DefinitionView::wrap(){
    lines_.clear();scroll_=0;if(entries_.empty())return;
    std::string_view text=entries_[selected_].text;
    while(!text.empty()){
        size_t count=std::min(columns,text.size());const auto newline=text.find('\n');
        if(newline<count)count=newline;
        else if(count<text.size()){const auto space=text.substr(0,count).find_last_of(' ');if(space!=text.npos && space)count=space;}
        lines_.emplace_back(text.substr(0,count));text.remove_prefix(count);
        if(!text.empty() && text.front()=='\n')text.remove_prefix(1);
        else while(!text.empty() && text.front()==' ')text.remove_prefix(1);
    }
}
bool DefinitionView::event(const Event& e){
    const bool key=e.type==EventType::KeyDown,once=key&&!e.repeat,click=e.type==EventType::MouseDown&&e.button==MouseButton::Left;
    if((once&&(e.key==key::Escape||e.key==key::Return||e.key==key::KpEnter||e.key=='d'))||(click&&e.mouseX>=600&&e.mouseX<756&&e.mouseY>=530&&e.mouseY<568))return true;
    int direction=0;if(once&&e.key==key::Left)direction=-1;if(once&&e.key==key::Right)direction=1;
    if(click&&e.mouseY>=530&&e.mouseY<568){if(e.mouseX>=44&&e.mouseX<200)direction=-1;else if(e.mouseX>=216&&e.mouseX<372)direction=1;}
    if(direction && !entries_.empty()){const auto next=size_t(std::clamp<int64_t>(int64_t(selected_)+direction,0,int64_t(entries_.size()-1)));if(next!=selected_){selected_=next;wrap();}}
    int64_t delta=0;if(key&&e.key==key::Up)delta=-1;if(key&&e.key==key::Down)delta=1;
    if(key&&e.key==key::PageUp)delta=-int64_t(visible);
    if(key&&e.key==key::PageDown)delta=visible;
    if(e.type==EventType::MouseWheel)delta=-int64_t(e.wheelY)*3;
    const size_t last=lines_.size()>visible?lines_.size()-visible:0;
    if(key&&e.key==key::Home)scroll_=0;
    if(key&&e.key==key::End)scroll_=last;
    if(delta)scroll_=size_t(std::clamp<int64_t>(int64_t(scroll_)+delta,0,int64_t(last)));
    return false;
}
void DefinitionView::draw(Surface& s) const {
    if(s.format!=PixelFormat::RGBA32 || s.w!=800 || s.h!=600 || s.pitch<3200 || size_t(s.pitch)>s.pixels.size()/600)return;
    auto box=[&](int x,int y,int w,int h,Color c){for(int row=y;row<y+h;++row)for(int col=x;col<x+w;++col){auto*p=s.row(row)+col*4;p[0]=c.r;p[1]=c.g;p[2]=c.b;p[3]=255;}};
    box(24,24,752,552,{17,25,40});const Color white{245,245,255},muted{170,181,205};
    font::drawText(s,44,42,"DEFINITION: "+word_,white,word_.size()>44?1:2);
    const auto match=entries_.empty()?std::string{}:entries_[selected_].inflected?"Stored form of "+entries_[selected_].headword:"Headword match";
    const auto status=!error_.empty()?error_:entries_.empty()?"No English headword or stored form found.":std::to_string(selected_+1)+" / "+std::to_string(entries_.size())+" articles / "+match+" / Up-Down, PgUp-PgDn, wheel: scroll";
    font::drawText(s,44,74,status.substr(0,116),muted);
    for(size_t i=0;i<visible&&scroll_+i<lines_.size();++i)font::drawText(s,44,100+int(i)*12,lines_[scroll_+i],white);
    font::drawText(s,44,500,"Headwords and stored inflections. Pronunciation/style escapes are shown in brackets.",muted);
    auto button=[&](int x,const char* text){box(x,530,156,38,{48,64,88});font::drawText(s,x+10,544,text,white);};
    button(44,"PREVIOUS (LEFT)");button(216,"NEXT (RIGHT)");button(600,"CLOSE (ENTER)");
}
}

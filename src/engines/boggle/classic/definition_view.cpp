// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/definition_view.h"
#include "common/font.h"
#include "common/font_truetype.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <algorithm>
namespace reluva::boggle {
namespace {
constexpr size_t visible=18;
const font::TrueTypeUnicode& definitionFont(){
    static const auto font=[] {
        font::TrueTypeUnicode result;std::vector<uint32_t> chars;
        for(unsigned c=32;c<0x400;++c)chars.push_back(c);
        for(unsigned c=0x1d00;c<0x2700;++c)chars.push_back(c);
        std::vector<std::filesystem::path> paths;
        if(const auto windows=std::getenv("WINDIR"))paths.emplace_back(std::filesystem::path(windows)/"Fonts"/"times.ttf");
        paths.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf");
        paths.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSerif-Regular.ttf");
        paths.emplace_back("/Library/Fonts/Times New Roman.ttf");
        paths.emplace_back("/System/Library/Fonts/Supplemental/Times New Roman.ttf");
        for(const auto& path:paths){
            std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)continue;
            const auto size=input.tellg();if(size<=0||size>8*1024*1024)continue;
            std::vector<uint8_t> bytes(size_t(size),uint8_t{});input.seekg(0);
            if(input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))&&!result.read(bytes,18,chars))break;
        }
        std::vector<std::filesystem::path> fallbacks;
        if(const auto windows=std::getenv("WINDIR"))for(const auto* name:{"arial.ttf","seguisym.ttf"})fallbacks.emplace_back(std::filesystem::path(windows)/"Fonts"/name);
        fallbacks.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        fallbacks.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
        fallbacks.emplace_back("/System/Library/Fonts/Supplemental/Arial.ttf");
        for(const auto& path:fallbacks){
            std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)continue;
            const auto size=input.tellg();if(size<=0||size>8*1024*1024)continue;
            std::vector<uint8_t> bytes(size_t(size),uint8_t{});input.seekg(0);
            if(input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))){
                if(result.loaded())(void)result.addFallback(bytes,chars);
                else (void)result.read(bytes,18,chars);
            }
        }
        return result;
    }();return font;
}
int advance(uint32_t cp,unsigned style){const auto& f=definitionFont();return f.loaded()?f.advance(cp)+((style&DefinitionBold)&&f.advance(cp)?1:0):cp>=0x300&&cp<=0x36f?0:6;}

bool boundedEntries(const std::vector<DefinitionEntry>& entries){
    if(entries.size()>64)return false;
    size_t bytes=0;
    for(const auto& e:entries){
        if(e.text.size()>1024*1024||e.formatted.spans.size()>32768)return false;
        bytes+=e.text.size();for(const auto& span:e.formatted.spans){
            if(span.reference.size()>128)return false;
            bytes+=span.text.size()+span.reference.size();if(bytes>8*1024*1024)return false;
        }
        if(bytes>8*1024*1024)return false;
    }
    return true;
}
std::string matchLabel(const DefinitionEntry& entry){
    switch(entry.match){
    case DefinitionMatch::Headword:return "Headword match";
    case DefinitionMatch::Variant:return "Variant in "+entry.headword;
    case DefinitionMatch::Subentry:return "Subentry in "+entry.headword;
    case DefinitionMatch::Inflection:return "Stored form of "+entry.headword;
    case DefinitionMatch::RootDerivative:return "Derivative of root "+entry.headword;
    case DefinitionMatch::RelatedForm:return "Word form of "+entry.headword;
    }
    return "Article match";
}
}
void DefinitionView::open(std::string word,std::vector<DefinitionEntry> entries,std::string error,const Definitions* dictionary){
    dictionary_=dictionary;back_.clear();
    word_=word.substr(0,64);entries_=std::move(entries);error_=std::move(error);selected_=scroll_=0;
    if(!boundedEntries(entries_)){entries_.clear();error_="Definition exceeds display limits.";}
    wrap();
}
void DefinitionView::wrap(){
    referenceSelection_.reset();glyphs_.clear();scroll_=0;lines_=0;if(entries_.empty())return;
    const auto& entry=entries_[selected_];DefinitionText fallback;
    if(entry.formatted.spans.empty())fallback.spans.push_back({entry.text,0,{},false});
    const auto& spans=entry.formatted.spans.empty()?fallback.spans:entry.formatted.spans;
    int x=0;size_t line=0;
    for(const auto& span:spans){
        for(size_t at=0;at<span.text.size();){
            const auto cp=definitionCodepoint(span.text,at);
            if(cp=='\n'){x=0;++line;continue;}
            // Wrap at spaces when possible; also split overlong tokens safely.
            if(cp==' '){
                int wordWidth=0;for(size_t next=at;next<span.text.size();){const auto c=definitionCodepoint(span.text,next);if(c==' '||c=='\n')break;wordWidth+=advance(c,span.style);if(wordWidth>700)break;}
                if(x+advance(cp,span.style)+wordWidth>700){x=0;++line;continue;}
            }
            const int width=advance(cp,span.style);
            if(x+width>700&&x){x=0;++line;}
            if(glyphs_.size()==262144){glyphs_.clear();error_="Definition exceeds layout limits.";return;}
            glyphs_.push_back({cp,span.style,x,line,span.reference,span.root});x+=width;
        }
    }
    lines_=line+1;
}
void DefinitionView::follow(const std::string& word,bool root){
    if(!dictionary_)return;
    std::vector<DefinitionEntry> entries;auto error=dictionary_->reference(word,root,entries);
    if(!boundedEntries(entries)){entries.clear();error=Error::fail("Definition exceeds display limits.");}
    if(back_.size()==32)back_.erase(back_.begin());
    back_.push_back({word_,error_,std::move(entries_),selected_,scroll_});
    word_=word;error_=error.message;entries_=std::move(entries);selected_=0;wrap();
}
bool DefinitionView::event(const Event& e){
    const bool key=e.type==EventType::KeyDown,once=key&&!e.repeat,click=e.type==EventType::MouseDown&&e.button==MouseButton::Left;
    if(once&&e.key==key::Tab&&dictionary_){
        const auto start=referenceSelection_?*referenceSelection_+1:0;
        for(size_t step=0;step<glyphs_.size();++step){const auto n=(start+step)%glyphs_.size();const auto& g=glyphs_[n];
            if(g.reference.empty()||(n&&g.reference==glyphs_[n-1].reference&&g.root==glyphs_[n-1].root))continue;
            referenceSelection_=n;scroll_=std::min(g.line,lines_>visible?lines_-visible:0);break;
        }
        return false;
    }
    if(once&&(e.key==key::Return||e.key==key::KpEnter)&&referenceSelection_){
        const auto g=glyphs_[*referenceSelection_];follow(g.reference,g.root);return false;
    }
    if((once&&(e.key==key::Escape||e.key==key::Return||e.key==key::KpEnter||e.key=='d'))||(click&&e.mouseX>=600&&e.mouseX<756&&e.mouseY>=530&&e.mouseY<568))return true;
    if((once&&e.key==key::Backspace)||(click&&e.mouseX>=388&&e.mouseX<544&&e.mouseY>=530&&e.mouseY<568)){
        if(!back_.empty()){auto p=std::move(back_.back());back_.pop_back();word_=std::move(p.word);error_=std::move(p.error);entries_=std::move(p.entries);selected_=p.selected;wrap();scroll_=p.scroll;}
        return false;
    }
    if(click&&e.mouseY>=100&&e.mouseY<496&&dictionary_){
        for(const auto& glyph:glyphs_)if(!glyph.reference.empty()&&glyph.line>=scroll_&&glyph.line<scroll_+visible&&e.mouseX>=44+glyph.x&&e.mouseX<44+glyph.x+advance(glyph.cp,glyph.style)&&e.mouseY>=100+int(glyph.line-scroll_)*22&&e.mouseY<122+int(glyph.line-scroll_)*22){const auto reference=glyph.reference;const auto root=glyph.root;follow(reference,root);break;}
        return false;
    }
    int direction=0;if(once&&e.key==key::Left)direction=-1;if(once&&e.key==key::Right)direction=1;
    if(click&&e.mouseY>=530&&e.mouseY<568){if(e.mouseX>=44&&e.mouseX<200)direction=-1;else if(e.mouseX>=216&&e.mouseX<372)direction=1;}
    if(direction && !entries_.empty()){const auto next=size_t(std::clamp<int64_t>(int64_t(selected_)+direction,0,int64_t(entries_.size()-1)));if(next!=selected_){selected_=next;wrap();}}
    int64_t delta=0;if(key&&e.key==key::Up)delta=-1;if(key&&e.key==key::Down)delta=1;
    if(key&&e.key==key::PageUp)delta=-int64_t(visible);
    if(key&&e.key==key::PageDown)delta=visible;
    if(e.type==EventType::MouseWheel)delta=-int64_t(e.wheelY)*3;
    const size_t last=lines_>visible?lines_-visible:0;
    if(key&&e.key==key::Home)scroll_=0;
    if(key&&e.key==key::End)scroll_=last;
    if(delta)scroll_=size_t(std::clamp<int64_t>(int64_t(scroll_)+delta,0,int64_t(last)));
    return false;
}
void DefinitionView::draw(Surface& s) const {
    if(s.format!=PixelFormat::RGBA32 || s.w!=800 || s.h!=600 || s.pitch<3200 || size_t(s.pitch)>s.pixels.size()/600)return;
    auto box=[&](int x,int y,int w,int h,Color c){for(int row=y;row<y+h;++row)for(int col=x;col<x+w;++col){auto*p=s.row(row)+col*4;p[0]=c.r;p[1]=c.g;p[2]=c.b;p[3]=255;}};
    box(24,24,752,552,{17,25,40});const Color white{245,245,255},muted{170,181,205};
    const auto& f=definitionFont();const auto title="DEFINITION: "+word_;
    if(f.loaded()){int x=44;for(size_t at=0;at<title.size();){const auto cp=definitionCodepoint(title,at);if(x+f.advance(cp)>752)break;f.draw(s,x,58,cp,white,true);x+=f.advance(cp)+1;}}
    else font::drawText(s,44,42,title,white,word_.size()>44?1:2);
    const auto match=entries_.empty()?std::string{}:matchLabel(entries_[selected_]);
    const auto status=!error_.empty()?error_:entries_.empty()?"No indexed English article found.":std::to_string(selected_+1)+" / "+std::to_string(entries_.size())+" articles / "+match+" / Line "+std::to_string(scroll_+1)+"/"+std::to_string(lines_);
    font::drawText(s,44,74,status.substr(0,116),muted);
    for(const auto& g:glyphs_)if(g.line>=scroll_&&g.line<scroll_+visible){
        const int y=116+int(g.line-scroll_)*22+((g.style&DefinitionSuper)?-4:(g.style&DefinitionSub)?4:0);
        const bool focused=referenceSelection_&&g.reference==glyphs_[*referenceSelection_].reference&&g.root==glyphs_[*referenceSelection_].root;
        const Color color=focused?Color{255,220,115}:g.reference.empty()?white:Color{115,195,255};
        if(f.loaded())f.draw(s,44+g.x,y,g.cp,color,g.style&DefinitionBold,g.style&DefinitionItalic);
        else font::drawText(s,44+g.x,y-9,std::string(1,g.cp<127?char(g.cp):'?'),color);
    }
    font::drawText(s,44,500,"References: click or Tab, Enter. Backspace: return.",muted);
    auto button=[&](int x,const char* text){box(x,530,156,38,{48,64,88});font::drawText(s,x+10,544,text,white);};
    button(44,"PREVIOUS (LEFT)");button(216,"NEXT (RIGHT)");button(388,"BACK (BKSP)");button(600,"CLOSE (ENTER)");
}
}

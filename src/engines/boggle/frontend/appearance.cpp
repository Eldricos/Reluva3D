// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/appearance.h"
#include "common/font.h"
#include "common/font_truetype.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
namespace reluva::boggle::ui {
namespace {
thread_local Appearance* current=nullptr;
thread_local bool paper=false,serif=false;
constexpr Color face{192,192,192},light{255,255,255},shadow{128,128,128},ink{0,0,0},blue{0,0,128};
int height(int scale,int canvas){const int base=scale==1?11:scale==2?16:scale==3?18:20;return (base*canvas+320)/640;}
struct Fonts {
    std::array<font::TrueTypeAscii,8> faces;
    static constexpr std::array<int,8> sizes={11,14,16,18,20,23,25,28};
    explicit Fonts(bool useSerif){
        std::vector<std::filesystem::path> paths;
        if(useSerif){
            if(const auto windows=std::getenv("WINDIR"))paths.emplace_back(std::filesystem::path(windows)/"Fonts/times.ttf");
            paths.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSerif-Regular.ttf");
            paths.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf");
            paths.emplace_back("/System/Library/Fonts/Supplemental/Times New Roman.ttf");
        }
        if(const auto windows=std::getenv("WINDIR"))paths.emplace_back(std::filesystem::path(windows)/"Fonts/arial.ttf");
        paths.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
        paths.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        paths.emplace_back("/System/Library/Fonts/Supplemental/Arial.ttf");
        for(const auto& path:paths){
            std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)continue;
            const auto bytes=in.tellg();if(bytes<=0||bytes>4*1024*1024)continue;
            std::vector<uint8_t> data(size_t(bytes),uint8_t{});in.seekg(0);
            if(!in.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size())))continue;
            bool okay=true;for(size_t i=0;i<sizes.size();++i)if(faces[i].read(data,sizes[i])){okay=false;break;}
            if(okay)break;
        }
    }
    const font::TrueTypeAscii& get(int size) const {
        const auto at=std::min_element(sizes.begin(),sizes.end(),[&](int a,int b){return std::abs(a-size)<std::abs(b-size);});return faces[size_t(at-sizes.begin())];
    }
};
const font::TrueTypeAscii& fontAt(int scale,int canvas){static const Fonts sans(false),roman(true);return (serif?roman:sans).get(height(scale,canvas));}
Color color(Color c){if(!paper)return c;if(c.r>=200&&c.g>=180&&c.b>=90)return ink;if(c.r>=150&&c.g>=150&&c.b>=150)return Color{80,80,80};return c;}
bool valid(const Surface& s){return s.format==PixelFormat::RGBA32&&s.w>0&&s.h>0&&s.w<=8192&&s.h<=8192&&int64_t(s.pitch)>=int64_t(s.w)*4&&size_t(s.pitch)<=s.pixels.size()/size_t(s.h);}
void edge(Surface& s,SelectionRect b,bool down){
    fill(s,b,down?shadow:light);fill(s,{b.x+1,b.y+1,b.w-1,b.h-1},down?light:ink);
    fill(s,{b.x+1,b.y+1,b.w-2,b.h-2},down?ink:face);
    fill(s,{b.x+2,b.y+2,b.w-4,b.h-4},face);
    if(!down){fill(s,{b.x+2,b.y+b.h-3,b.w-4,1},shadow);fill(s,{b.x+b.w-3,b.y+2,1,b.h-4},shadow);}
}
void fitted(Surface& s,SelectionRect b,std::string_view title,int scale,Color c,int offset=0){
    const auto& f=fontAt(scale,s.w);const int w=f.loaded()?f.measure(title):int(title.size())*6;
    const int x=b.x+std::max(4,(b.w-w)/2)+offset,y=b.y+(b.h-height(scale,s.w))/2+offset;
    if(f.loaded())f.draw(s,x,y+height(scale,s.w),title,c,std::max(0,b.x+b.w-4-x));
    else font::drawText(s,x,y,title,c);
}
}
Scope::Scope(Appearance& a):previous_(current){current=&a;}
Scope::~Scope(){current=previous_;}
Paper::Paper(bool value):previous_(paper){paper=value;}
Paper::~Paper(){paper=previous_;}
Serif::Serif(bool value):previous_(serif){serif=value;}
Serif::~Serif(){serif=previous_;}
bool active(){return current&&current->enabled();}
void Appearance::event(const Event& e){
    if(e.type==EventType::FocusLost||e.type==EventType::FocusGained||e.type==EventType::MouseLeave){reset();return;}
    if(e.type==EventType::MouseMove||e.type==EventType::MouseDown||e.type==EventType::MouseUp){x_=e.mouseX;y_=e.mouseY;}
    if(e.button==MouseButton::Left&&e.type==EventType::MouseDown)pressed_=true;
    if(e.button==MouseButton::Left&&e.type==EventType::MouseUp)pressed_=false;
}
int dx(int n,int canvas){return active()?int((int64_t(n)*6*canvas+1280)/(4*640)):n*2;}
int dy(int n,int canvas){return active()?int((int64_t(n)*13*canvas+1920)/(8*480)):n*2;}
void fill(Surface& s,SelectionRect b,Color c){
    if(!valid(s)||b.w<=0||b.h<=0)return;
    for(int y=std::max(0,b.y);y<std::min<int64_t>(s.h,int64_t(b.y)+b.h);++y)for(int x=std::max(0,b.x);x<std::min<int64_t>(s.w,int64_t(b.x)+b.w);++x){auto* p=s.row(y)+x*4;p[0]=c.r;p[1]=c.g;p[2]=c.b;p[3]=255;}
}
void text(Surface& s,int x,int y,std::string_view value,Color c,int scale){
    if(!active()){font::drawText(s,x,y,value,c,scale);return;}
    c=color(c);const auto& f=fontAt(scale,s.w);
    if(f.loaded())f.draw(s,x,y+height(scale,s.w),value,c,std::max(0,s.w-x));else font::drawText(s,x,y,value,c,scale);
}
int measure(std::string_view value,int scale,int canvas){if(!active())return int(value.size())*6*scale;const auto& f=fontAt(scale,canvas);return f.loaded()?f.measure(value):int(value.size())*6;}
void button(Surface& s,SelectionRect b,std::string_view title,bool selected,bool enabled){
    if(!active()||!valid(s)||b.w<8||b.h<8)return;
    const bool down=enabled&&current->over(b)&&current->pressed();
    if(selected){fill(s,b,ink);b={b.x+1,b.y+1,b.w-2,b.h-2};}
    edge(s,b,down);
    if(!enabled)fitted(s,b,title,1,light,1);
    fitted(s,b,title,1,enabled?ink:shadow,down?1:0);
    if(enabled&&current->over(b))for(int x=b.x+4;x<b.x+b.w-4;x+=2){fill(s,{x,b.y+4,1,1},ink);fill(s,{x,b.y+b.h-5,1,1},ink);}
}
void glassButton(Surface& s,SelectionRect b,std::string_view title,bool selected){
    if(!active()||!valid(s)||b.w<8||b.h<8)return;
    const bool down=current->over(b)&&current->pressed();
    const Color top=down?Color{64,64,64}:face,bottom=down?face:Color{64,64,64};
    for(int i=0;i<2;++i){
        fill(s,{b.x+i,b.y+i,b.w-i*2,1},top);fill(s,{b.x+i,b.y+i,1,b.h-i*2},top);
        fill(s,{b.x+i,b.y+b.h-1-i,b.w-i*2,1},bottom);fill(s,{b.x+b.w-1-i,b.y+i,1,b.h-i*2},bottom);
    }
    fitted(s,b,title,1,light,down?1:0);
    if(selected)for(int x=b.x+4;x<b.x+b.w-4;x+=2)fill(s,{x,b.y+b.h-5,1,1},face);
}
void rollover(Surface& s,SelectionRect b,std::string_view title,bool selected){
    if(!active())return;
    text(s,b.x,b.y,title,current->over(b)?Color{255,0,0}:light,3);
    if(selected)fill(s,{b.x,b.y+b.h-2,std::min(b.w,measure(title,3,s.w)),1},light);
}
void panel(Surface& s,SelectionRect b){if(active()&&valid(s)&&b.w>=8&&b.h>=8){edge(s,b,false);fill(s,{b.x+3,b.y+3,b.w-6,b.h-6},face);}}
void choice(Surface& s,SelectionRect b,std::string_view title,bool enabled){
    if(!active())return;
    edge(s,b,true);fill(s,{b.x+2,b.y+2,b.w-4,b.h-4},enabled?light:face);
    const SelectionRect arrow{b.x+b.w-20,b.y+2,18,b.h-4};edge(s,arrow,current->over(b)&&current->pressed());
    for(int row=0;row<4;++row)fill(s,{arrow.x+5+row,arrow.y+arrow.h/2-2+row,7-row*2,1},enabled?ink:shadow);
    const auto& f=fontAt(1,s.w);const int y=b.y+(b.h-height(1,s.w))/2;
    if(f.loaded())f.draw(s,b.x+5,y+height(1,s.w),title,enabled?ink:shadow,std::max(0,b.w-27));else font::drawText(s,b.x+5,y,title.substr(0,size_t(std::max(0,b.w-27)/6)),ink);
}
void check(Surface& s,SelectionRect b,std::string_view title,bool selected,bool enabled,bool radio){
    if(!active())return;
    const int side=s.w==800?16:13;SelectionRect mark{b.x,b.y+(b.h-side)/2,side,side};
    if(radio){
        const int r=side/2;
        for(int y=-r;y<=r;++y)for(int x=-r;x<=r;++x)if(x*x+y*y<=r*r)fill(s,{mark.x+r+x,mark.y+r+y,1,1},x*x+y*y>=(r-1)*(r-1)?shadow:light);
        if(selected)fill(s,{mark.x+r-2,mark.y+r-2,5,5},enabled?ink:shadow);
    }else{edge(s,mark,true);fill(s,{mark.x+2,mark.y+2,side-4,side-4},enabled?light:face);if(selected)for(int x=0;x<7;++x)fill(s,{mark.x+3+x,mark.y+side/2+(x<2?x:4-x),1,2},enabled?ink:shadow);}
    const auto& f=fontAt(1,s.w);const int y=b.y+(b.h-height(1,s.w))/2;
    if(f.loaded())f.draw(s,b.x+side+5,y+height(1,s.w),title,enabled?ink:shadow,std::max(0,b.w-side-5));
    else font::drawText(s,b.x+side+5,y,title.substr(0,size_t(std::max(0,b.w-side-5)/6)),enabled?ink:shadow);
}
size_t position(std::string_view value,int x,SelectionRect b,int padding,size_t caret){
    const int scroll=std::max(0,measure(value.substr(0,caret))-(b.w-padding*2-1));
    int64_t left=int64_t(b.x)+padding-scroll;
    for(size_t i=0;i<value.size();++i){const int w=measure(value.substr(i,1));if(int64_t(x)<left+(w+1)/2)return i;left+=w;}
    return value.size();
}
std::string caption(std::u16string_view value){
    std::string result;if(value.size()>2048)return result;
    for(size_t i=0;i<value.size();++i){if(value[i]=='&'&&i+1<value.size()){if(value[i+1]!='&')continue;++i;}result+=value[i]>=32&&value[i]<127?char(value[i]):' ';}
    return result;
}
void group(Surface& s,SelectionRect b,std::string_view title){
    if(!active()||b.w<8||b.h<8)return;
    fill(s,{b.x,b.y+5,b.w,b.h-5},shadow);fill(s,{b.x+1,b.y+6,b.w-2,b.h-7},light);fill(s,{b.x+2,b.y+7,b.w-4,b.h-9},face);
    fill(s,{b.x+7,b.y,measure(title)+6,13},face);text(s,b.x+10,b.y,title,ink);
}
void wrapped(Surface& s,SelectionRect b,std::string_view value,Color c,int scale){
    int y=b.y;const int line=height(scale,s.w)+2;value=value.substr(0,2048);
    while(!value.empty()&&y+line<=b.y+b.h){
        size_t n=0,last=0;for(;n<value.size();++n){if(value[n]==' ')last=n;if(value[n]=='\n'||measure(value.substr(0,n+1),scale,s.w)>b.w)break;}
        if(n<value.size()&&value[n]!='\n'&&last)n=last;
        if(!n)n=1;
        text(s,b.x,y,value.substr(0,n),c,scale);value.remove_prefix(n);
        while(!value.empty()&&(value.front()==' '||value.front()=='\n'))value.remove_prefix(1);
        y+=line;
    }
}
void field(Surface& s,SelectionRect b,std::string_view value,size_t first,size_t last,size_t caret,int padding){
    if(!active()||!valid(s)||b.w<16||b.h<16||b.x<0||b.y<0||int64_t(b.x)+b.w>s.w||int64_t(b.y)+b.h>s.h||value.size()>64||padding<2||padding>32)return;
    edge(s,b,true);Surface content;content.create(b.w-4,b.h-4,PixelFormat::RGBA32);fill(content,{0,0,content.w,content.h},light);
    const int scroll=std::max(0,measure(value.substr(0,caret))-(b.w-padding*2-1));
    const int x=padding-2-scroll,y=(content.h-height(1,s.w))/2;
    for(size_t i=0;i<value.size();++i){const int at=x+measure(value.substr(0,i)),w=measure(value.substr(i,1));
        const bool selected=i>=first&&i<last;if(selected)fill(content,{at,y,w,height(1,s.w)},blue);
        const auto& f=fontAt(1,s.w);if(f.loaded())f.draw(content,at,y+height(1,s.w),value.substr(i,1),selected?light:ink,std::max(0,content.w-at));else font::drawText(content,at,y,value.substr(i,1),selected?light:ink);
    }
    fill(content,{x+measure(value.substr(0,caret)),y,1,height(1,s.w)},ink);
    for(int row=0;row<content.h;++row)std::copy_n(content.row(row),content.w*4,s.row(b.y+2+row)+(b.x+2)*4);
}
}

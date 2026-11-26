// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/frontend/layout.h"
#include "common/events.h"
#include <string_view>
namespace reluva::boggle::ui {
// Drawing context is scoped to the current native engine invocation/thread.
// It never owns a System or calls platform GUI/font services.
class Appearance {
public:
    void enable(bool value){enabled_=value;}
    void event(const Event& event);
    void reset(){x_=y_=-1;pressed_=false;}
    bool enabled() const{return enabled_;}
    bool over(SelectionRect b) const{return b.contains(x_,y_);}
    bool pressed() const{return pressed_;}
private:
    bool enabled_=false,pressed_=false;
    int x_=-1,y_=-1;
};
class Scope {
public:
    explicit Scope(Appearance& appearance);
    ~Scope();
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
private:Appearance* previous_;
};
class Paper {
public:explicit Paper(bool value=true);~Paper();
    Paper(const Paper&)=delete;Paper& operator=(const Paper&)=delete;
private:bool previous_;
};
class Serif {
public:explicit Serif(bool value=true);~Serif();
    Serif(const Serif&)=delete;Serif& operator=(const Serif&)=delete;
private:bool previous_;
};
bool active();
// Win95 MS Sans Serif 8-pt dialog units, scaled from the 640x480 canvas.
int dx(int units,int canvas=640);
int dy(int units,int canvas=480);
void fill(Surface& screen,SelectionRect bounds,Color color);
void text(Surface& screen,int x,int y,std::string_view value,Color color,int scale=1);
int measure(std::string_view value,int scale=1,int canvas=640);
void button(Surface& screen,SelectionRect bounds,std::string_view title,bool selected=false,bool enabled=true);
void glassButton(Surface& screen,SelectionRect bounds,std::string_view title,bool selected=false);
void rollover(Surface& screen,SelectionRect bounds,std::string_view title,bool selected=false);
void panel(Surface& screen,SelectionRect bounds);
void choice(Surface& screen,SelectionRect bounds,std::string_view title,bool enabled=true);
void check(Surface& screen,SelectionRect bounds,std::string_view title,bool selected,bool enabled=true,bool radio=false);
std::string caption(std::u16string_view value);
void group(Surface& screen,SelectionRect bounds,std::string_view title);
void wrapped(Surface& screen,SelectionRect bounds,std::string_view text,Color color,int scale=1);
void field(Surface& screen,SelectionRect bounds,std::string_view value,size_t first,size_t last,size_t caret,int padding);
size_t position(std::string_view value,int x,SelectionRect field,int padding,size_t caret);
}

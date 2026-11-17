// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/frontend/layout.h"
#include <string_view>
#include <algorithm>
namespace reluva::boggle {
class NameEdit {
public:
    Error begin(std::string_view value);
    Error insert(std::string_view value);
    void selectAll(){anchor_=0;caret_=text_.size();}
    void move(int direction,bool extend);
    void home(bool extend){place(0,extend);}
    void end(bool extend){place(text_.size(),extend);}
    void pointer(int x,SelectionRect field,int padding,bool extend);
    void erase(bool forward);
    const std::string& text() const {return text_;}
    size_t caret() const {return caret_;}
    size_t first() const {return std::min(caret_,anchor_);}
    size_t last() const {return std::max(caret_,anchor_);}
    void draw(Surface& screen,SelectionRect field,int padding=8) const;
private:
    void place(size_t position,bool extend){caret_=position;if(!extend)anchor_=caret_;}
    std::string text_;
    size_t caret_=0,anchor_=0;
};
} // namespace reluva::boggle

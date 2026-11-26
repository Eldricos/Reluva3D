// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/frontend/layout.h"
#include "engines/boggle/classic/options.h"
namespace reluva::boggle {
class FrontendOptionsDialog {
public:
    enum Control { Cursor, Background, Orient, Chess, Traditional, Slider, Untimed, Count };
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    bool installed() const {return installed_;}
    const SelectionRect& control(Control id) const {return controls_[size_t(id)];}
    static constexpr SelectionRect done(){return {340,402,252,36};}
    static constexpr SelectionRect cancel(){return {48,402,252,36};}
    uint64_t durationAt(int x) const;
    void draw(Surface& screen,const ClassicOptions& options,bool backgroundAllowed,bool cursorAllowed) const;
private:
    bool installed_=false;
    std::array<std::string,Count> labels_{};
    std::vector<std::pair<SelectionRect,std::string>> groups_;
    SelectionRect bounds_{};
    std::array<SelectionRect,Count> controls_{};
};
} // namespace reluva::boggle

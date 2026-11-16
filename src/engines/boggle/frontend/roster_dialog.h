// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/frontend/layout.h"
namespace reluva::boggle {
class FrontendRosterDialog {
public:
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    bool installed() const {return installed_;}
    const std::array<SelectionRect,4>& fields() const {return fields_;}
    const SelectionRect& done() const {return done_;}
    const SelectionRect& back() const {return back_;}
    void draw(Surface& screen,const std::array<std::string,4>& roster) const;
private:
    bool installed_=false;
    SelectionRect bounds_{},message_{},done_{340,388,252,48},back_{};
    std::array<SelectionRect,4> fields_{{{72,142,496,38},{72,192,496,38},{72,242,496,38},{72,292,496,38}}};
};
} // namespace reluva::boggle

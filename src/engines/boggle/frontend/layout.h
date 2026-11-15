// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/pe_resources.h"
#include <array>
namespace reluva::boggle {
struct SelectionRect {
    int x,y,w,h;
    bool contains(int px,int py) const {return px>=x && py>=y && int64_t(px)<int64_t(x)+w && int64_t(py)<int64_t(y)+h;}
};
class FrontendSelectionLayout {
public:
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    bool installed() const {return installed_;}
    const std::array<SelectionRect,5>& modes() const {return modes_;}
private:
    bool installed_=false;
    std::array<SelectionRect,5> modes_{{{40,288,112,112},{154,288,110,112},{266,288,110,112},{378,288,110,112},{490,288,110,112}}};
};
} // namespace reluva::boggle

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/frontend/layout.h"
#include <string_view>
namespace reluva::boggle {
class FrontendNameDialog {
public:
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    bool installed() const {return installed_;}
    const SelectionRect& field() const {return field_;}
    const SelectionRect& save() const {return save_;}
    const SelectionRect& cancel() const {return cancel_;}
    void draw(Surface& screen,std::string_view draft,bool selected,std::string_view error) const;
private:
    bool installed_=false;
    SelectionRect bounds_{},caption_{},field_{88,188,464,42},save_{88,296,210,40},cancel_{318,296,210,40};
};
} // namespace reluva::boggle

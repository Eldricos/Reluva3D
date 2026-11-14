// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/pe_resources.h"
#include "engines/boggle/classic/history.h"
#include <string_view>
namespace reluva::boggle {
class ClassicNoticeDialog {
public:
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    bool installed() const {return installed_;}
    const HistoryButton& okay() const {return okay_;}
    void draw(Surface& screen,std::string_view text) const;
private:
    bool installed_=false;
    HistoryButton bounds_{},message_{},okay_{320,350,160,40};
};
} // namespace reluva::boggle

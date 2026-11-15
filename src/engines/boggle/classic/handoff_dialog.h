// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/pe_resources.h"
#include "engines/boggle/classic/history.h"
#include <string_view>
namespace reluva::boggle {
class ClassicHandoffDialog {
public:
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    bool installed() const {return installed_;}
    const HistoryButton& start() const {return start_;}
    void draw(Surface& screen,std::string_view next,bool chess,uint64_t remaining,bool timed,const std::vector<std::string>& finished) const;
private:
    bool installed_=false;
    HistoryButton bounds_{},message_{},start_{80,380,640,60};
};
} // namespace reluva::boggle

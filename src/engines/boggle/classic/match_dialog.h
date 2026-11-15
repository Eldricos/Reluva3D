// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/pe_resources.h"
#include "engines/boggle/classic/competition.h"
#include "engines/boggle/classic/history.h"
#include <array>
#include <optional>
namespace reluva::boggle {
class ClassicMatchDialog {
public:
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    bool installed() const {return installed_;}
    const HistoryButton& close() const {return close_;}
    const HistoryButton& scores() const {return scores_;}
    std::optional<size_t> playerAt(int x,int y,size_t count) const;
    void draw(Surface& screen,const std::vector<std::string>& names,const ClassicCompetition& competition,bool chess,bool background) const;
private:
    bool installed_=false;
    HistoryButton bounds_{},message_{},close_{448,450,184,30},scores_{};
    std::array<std::array<HistoryButton,2>,4> rows_{};
};
} // namespace reluva::boggle

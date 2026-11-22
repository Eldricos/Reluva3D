// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/pe_resources.h"
#include "engines/boggle/classic/highscores.h"
#include "engines/boggle/classic/history.h"
#include "engines/boggle/frontend/panel_art.h"
#include <array>
#include <string_view>
namespace reluva::boggle {
class ClassicScoresDialog {
public:
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    bool installed() const {return installed_;}
    Error artwork(const Resources& r,const std::filesystem::path& root){return installed_?art_.open(r,root,"Bitmaps/High1.bmp",640,480):Error::success();}
    const HistoryButton& close() const {return close_;}
    const HistoryButton& retry() const {return retry_;}
    void draw(Surface& screen,const HighScores& scores,std::string_view error) const;
private:
    bool installed_=false;
    PanelArt art_;
    HistoryButton bounds_{},close_{448,450,184,30},retry_{160,450,184,30};
    std::array<std::array<HistoryButton,3>,10> rows_{};
};
} // namespace reluva::boggle

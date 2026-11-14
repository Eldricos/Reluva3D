// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/pe_resources.h"
#include <array>
#include <string_view>
#include "engines/boggle/classic/history.h"
#include "engines/boggle/classic/projection.h"
namespace reluva::boggle {
class ClassicReviewDialog {
public:
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    bool installed() const {return installed_;}
    const std::array<HistoryButton,4>& buttons() const {return buttons_;}
    void draw(Surface& screen,std::string_view word,std::string_view message) const;
private:
    bool installed_=false;
    HistoryButton bounds_{};
    ClassicPoint message_;
    std::array<HistoryButton,4> buttons_={{{40,352,168,40},{224,352,168,40},{408,352,168,40},{592,352,168,40}}};
};
} // namespace reluva::boggle

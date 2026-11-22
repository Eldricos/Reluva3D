// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/formats/definitions.h"
#include "common/events.h"
namespace reluva::boggle {
class DefinitionView {
public:
    void open(std::string word,std::vector<DefinitionEntry> entries,std::string error={});
    bool event(const Event& event);
    void draw(Surface& screen) const;
private:
    std::string word_,error_;
    std::vector<DefinitionEntry> entries_;
    std::vector<std::string> lines_;
    size_t selected_=0,scroll_=0;
    void wrap();
};
}
